#include "search/geocode_query.hpp"

#include "search/text_normalizer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_set>

namespace osm::search {

GeocodeQueryResult run_geocode_query_internal(
    const DataStore& data,
    const SearchIndex& index,
    const std::string& input,
    const GeocodeQueryOptions& options,
    bool allow_nearest_intent);

namespace {

struct TokenSpan {
    std::size_t begin{0};
    std::size_t end{0};
};

struct LocalitySpan {
    std::size_t begin{0};
    std::size_t end{0};
    std::string normalized_name;
    std::vector<std::uint32_t> locality_indices;
};

struct CandidateAccumulatorEntry {
    GeocodeCandidate candidate;
    int specificity{0};
};

using CandidateMap = std::map<SearchObjectRef, CandidateAccumulatorEntry>;

struct NearestCategoryIntent {
    PoiCategory category{PoiCategory::Other};
    std::string category_text;
    std::string reference_query;
};

[[nodiscard]] double elapsed_ms(const std::chrono::steady_clock::time_point start, const std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

[[nodiscard]] bool has_digit(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](const char c) {
        return c >= '0' && c <= '9';
    });
}

[[nodiscard]] std::optional<PoiCategory> poi_category_from_query(std::string_view value) {
    if (value == "shop" || value == "shops") return PoiCategory::Shop;
    if (value == "restaurant" || value == "restaurants") return PoiCategory::Restaurant;
    if (value == "cafe" || value == "cafes") return PoiCategory::Cafe;
    if (value == "fast food" || value == "fastfood") return PoiCategory::FastFood;
    if (value == "park" || value == "parks") return PoiCategory::Park;
    if (value == "hotel" || value == "hotels") return PoiCategory::Hotel;
    if (value == "school" || value == "schools") return PoiCategory::School;
    if (value == "hospital" || value == "hospitals") return PoiCategory::Hospital;
    if (value == "station" || value == "stations") return PoiCategory::Station;
    return std::nullopt;
}

[[nodiscard]] bool is_single_letter_token(std::string_view token) {
    return token.size() == 1 && token[0] >= 'a' && token[0] <= 'z';
}

[[nodiscard]] bool is_house_number_token(std::string_view token) {
    if (!has_digit(token)) {
        return false;
    }
    bool previous_separator = false;
    for (const char c : token) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (alnum) {
            previous_separator = false;
            continue;
        }
        if ((c == '-' || c == '/') && !previous_separator) {
            previous_separator = true;
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<TokenSpan> detect_house_spans(const std::vector<std::string>& tokens) {
    std::vector<TokenSpan> spans;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (!is_house_number_token(tokens[i])) {
            continue;
        }
        spans.push_back(TokenSpan{.begin = i, .end = i + 1});
        if (i + 1 < tokens.size() && is_single_letter_token(tokens[i + 1])) {
            spans.push_back(TokenSpan{.begin = i, .end = i + 2});
        }
    }
    return spans;
}

[[nodiscard]] std::string join_tokens(const std::vector<std::string>& tokens, const std::size_t begin, const std::size_t end) {
    std::string out;
    for (std::size_t i = begin; i < end && i < tokens.size(); ++i) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += tokens[i];
    }
    return out;
}

[[nodiscard]] std::optional<NearestCategoryIntent> parse_nearest_category_intent(
    const std::vector<std::string>& tokens) {
    if (tokens.size() < 4 || (tokens.front() != "closest" && tokens.front() != "nearest")) {
        return std::nullopt;
    }
    for (std::size_t connector = 2; connector + 1 < tokens.size(); ++connector) {
        if (tokens[connector] != "to" && tokens[connector] != "near" && tokens[connector] != "from") {
            continue;
        }
        const auto category_text = join_tokens(tokens, 1, connector);
        const auto category = poi_category_from_query(category_text);
        if (!category.has_value()) {
            return std::nullopt;
        }
        return NearestCategoryIntent{
            .category = *category,
            .category_text = category_text,
            .reference_query = join_tokens(tokens, connector + 1, tokens.size()),
        };
    }
    return std::nullopt;
}

[[nodiscard]] bool spans_overlap(const std::size_t a_begin, const std::size_t a_end, const std::size_t b_begin, const std::size_t b_end) {
    return a_begin < b_end && b_begin < a_end;
}

[[nodiscard]] std::vector<LocalitySpan> detect_locality_spans(const SearchIndex& index, const std::vector<std::string>& tokens) {
    std::vector<LocalitySpan> spans;
    for (std::size_t begin = 0; begin < tokens.size(); ++begin) {
        for (std::size_t end = begin + 1; end <= tokens.size(); ++end) {
            const auto name = join_tokens(tokens, begin, end);
            if (const auto it = index.locality_name_index.find(name); it != index.locality_name_index.end()) {
                spans.push_back(LocalitySpan{.begin = begin, .end = end, .normalized_name = name, .locality_indices = it->second});
            }
        }
    }
    std::sort(spans.begin(), spans.end(), [](const auto& lhs, const auto& rhs) {
        const auto lhs_len = lhs.end - lhs.begin;
        const auto rhs_len = rhs.end - rhs.begin;
        if (lhs_len != rhs_len) return lhs_len > rhs_len;
        if (lhs.begin != rhs.begin) return lhs.begin < rhs.begin;
        return lhs.normalized_name < rhs.normalized_name;
    });
    return spans;
}

[[nodiscard]] std::size_t max_fuzzy_distance_for_token(std::string_view token) {
    if (token.size() < 3) {
        return 0;
    }
    if (token.size() <= 6) {
        return 1;
    }
    return 2;
}

struct TokenVariant { std::vector<std::string> tokens; std::size_t edit_cost{0}; };

[[nodiscard]] std::vector<TokenVariant> fuzzy_token_variants(const SearchIndex& index, const std::vector<std::string>& tokens) {
    constexpr std::size_t kBeamWidth = 12;
    std::vector<TokenVariant> beam{{.tokens = tokens, .edit_cost = 0}};
    for (std::size_t position = 0; position < tokens.size(); ++position) {
        const auto& token = tokens[position];
        const auto max_distance = max_fuzzy_distance_for_token(token);
        if (max_distance == 0 || has_digit(token) || index.token_index.contains(token)) continue;
        std::vector<FuzzyTokenMatch> alternatives;
        for (const auto kind : {IndexedNameKind::Street, IndexedNameKind::Locality, IndexedNameKind::Poi, IndexedNameKind::Region}) {
            auto typed = findFuzzyTokenMatches(index, token, kind, max_distance, 3);
            alternatives.insert(alternatives.end(), typed.begin(), typed.end());
        }
        std::sort(alternatives.begin(), alternatives.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.distance != rhs.distance) return lhs.distance < rhs.distance;
            if (lhs.kind != rhs.kind) return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
            return lhs.token < rhs.token;
        });
        alternatives.erase(std::unique(alternatives.begin(), alternatives.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.token == rhs.token;
        }), alternatives.end());
        if (alternatives.size() > 4) alternatives.resize(4);
        if (alternatives.empty()) continue;
        std::vector<TokenVariant> next;
        for (const auto& current : beam) {
            for (const auto& alternative : alternatives) {
                auto candidate = current;
                candidate.tokens[position] = alternative.token;
                candidate.edit_cost += alternative.distance;
                next.push_back(std::move(candidate));
            }
        }
        std::sort(next.begin(), next.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.edit_cost != rhs.edit_cost) return lhs.edit_cost < rhs.edit_cost;
            return lhs.tokens < rhs.tokens;
        });
        if (next.size() > kBeamWidth) next.resize(kBeamWidth);
        beam = std::move(next);
    }
    beam.erase(std::remove_if(beam.begin(), beam.end(), [&](const auto& variant) { return variant.tokens == tokens; }), beam.end());
    return beam;
}

[[nodiscard]] std::vector<std::vector<std::string>> partial_token_variants(
    const SearchIndex& index,
    const std::vector<std::string>& tokens) {
    constexpr std::size_t kMaxPartialVariants = 16;
    constexpr std::size_t kMatchesPerSpan = 8;
    std::vector<std::vector<std::string>> variants;
    std::set<std::vector<std::string>> seen;
    const auto locality_spans = detect_locality_spans(index, tokens);
    const auto house_spans = detect_house_spans(tokens);

    for (std::size_t span_length = tokens.size(); span_length > 0 && variants.size() < kMaxPartialVariants; --span_length) {
        for (std::size_t begin = 0; begin + span_length <= tokens.size() && variants.size() < kMaxPartialVariants; ++begin) {
            const auto end = begin + span_length;
            const bool overlaps_recognized_locality = std::any_of(locality_spans.begin(), locality_spans.end(), [&](const auto& span) {
                return spans_overlap(begin, end, span.begin, span.end);
            });
            const bool overlaps_house_number = std::any_of(house_spans.begin(), house_spans.end(), [&](const auto& span) {
                return spans_overlap(begin, end, span.begin, span.end);
            });
            if (overlaps_recognized_locality || overlaps_house_number) continue;
            bool contains_number = false;
            for (std::size_t i = begin; i < end; ++i) {
                contains_number = contains_number || has_digit(tokens[i]);
            }
            if (contains_number) continue;

            const auto fragment = join_tokens(tokens, begin, end);
            if (fragment.size() < 2) continue;
            const auto matches = findSubstringNameMatches(
                index,
                fragment,
                kMatchesPerSpan,
                house_spans.empty() ? std::nullopt : std::optional{IndexedNameKind::Street});
            for (const auto& match : matches) {
                if (match.name_id >= index.names.size()) continue;
                const auto& full_name = index.names[match.name_id].normalized_name;
                if (full_name == fragment) continue;
                if (has_digit(full_name)) continue;
                const auto replacement = tokenizeNormalizedText(full_name);
                if (replacement.empty()) continue;

                std::vector<std::string> variant;
                variant.reserve(tokens.size() - span_length + replacement.size());
                variant.insert(variant.end(), tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(begin));
                variant.insert(variant.end(), replacement.begin(), replacement.end());
                variant.insert(variant.end(), tokens.begin() + static_cast<std::ptrdiff_t>(end), tokens.end());
                if (variant != tokens && seen.insert(variant).second) {
                    variants.push_back(std::move(variant));
                    if (variants.size() >= kMaxPartialVariants) break;
                }
            }
        }
    }
    return variants;
}

[[nodiscard]] std::string house_number_from_span(const std::vector<std::string>& tokens, const TokenSpan& span) {
    std::string raw;
    for (std::size_t i = span.begin; i < span.end && i < tokens.size(); ++i) {
        raw += tokens[i];
    }
    return normalizeHouseNumber(raw);
}

[[nodiscard]] std::string entity_from_remaining_tokens(
    const std::vector<std::string>& tokens,
    const TokenSpan* house_span,
    const LocalitySpan* locality_span,
    std::size_t& unexplained) {
    unexplained = 0;
    std::string entity;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (house_span != nullptr && i >= house_span->begin && i < house_span->end) {
            continue;
        }
        if (locality_span != nullptr && i >= locality_span->begin && i < locality_span->end) {
            continue;
        }
        if (!entity.empty()) {
            entity.push_back(' ');
        }
        entity += tokens[i];
    }
    return entity;
}

[[nodiscard]] std::vector<QueryInterpretation> generate_address_interpretations(
    const SearchIndex& index,
    const std::string& normalized_query,
    const std::vector<std::string>& tokens,
    const std::vector<TokenSpan>& house_spans,
    const std::vector<LocalitySpan>& locality_spans) {
    std::vector<QueryInterpretation> interpretations;
    std::set<std::tuple<std::string, std::string, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t>> seen;

    auto add_interpretation = [&](const std::string& entity, const TokenSpan& house_span, const LocalitySpan* locality_span, const std::size_t unexplained) {
        if (entity.empty()) {
            return;
        }
        QueryInterpretation interpretation;
        interpretation.intent = QueryIntent::Address;
        interpretation.normalized_query = normalized_query;
        interpretation.tokens = tokens;
        interpretation.entity_name = entity;
        interpretation.normalized_house_number = house_number_from_span(tokens, house_span);
        interpretation.house_token_begin = house_span.begin;
        interpretation.house_token_end = house_span.end;
        interpretation.unexplained_token_count = unexplained;
        if (locality_span != nullptr) {
            interpretation.locality_indices = locality_span->locality_indices;
            interpretation.locality_token_begin = locality_span->begin;
            interpretation.locality_token_end = locality_span->end;
        }
        const auto key = makeAddressKey(interpretation.entity_name, interpretation.normalized_house_number);
        if (!key.empty()) {
            if (const auto it = index.address_index.find(key); it != index.address_index.end()) {
                interpretation.exact_address_key_match = true;
                interpretation.raw_candidate_count = it->second.size();
            }
        }
        const auto marker = std::make_tuple(
            interpretation.entity_name,
            interpretation.normalized_house_number,
            interpretation.house_token_begin,
            interpretation.house_token_end,
            interpretation.locality_token_begin,
            interpretation.locality_token_end,
            interpretation.unexplained_token_count);
        if (seen.insert(marker).second) {
            interpretations.push_back(std::move(interpretation));
        }
    };

    for (const auto& house_span : house_spans) {
        for (const auto& locality_span : locality_spans) {
            if (spans_overlap(house_span.begin, house_span.end, locality_span.begin, locality_span.end)) {
                continue;
            }
            std::size_t unexplained = 0;
            const auto entity = entity_from_remaining_tokens(tokens, &house_span, &locality_span, unexplained);
            add_interpretation(entity, house_span, &locality_span, unexplained);
        }

        std::size_t unexplained = 0;
        const auto entity = entity_from_remaining_tokens(tokens, &house_span, nullptr, unexplained);
        add_interpretation(entity, house_span, nullptr, unexplained);

        for (std::size_t begin = 0; begin < tokens.size(); ++begin) {
            for (std::size_t end = begin + 1; end <= tokens.size(); ++end) {
                if (spans_overlap(begin, end, house_span.begin, house_span.end)) {
                    continue;
                }
                const auto entity = join_tokens(tokens, begin, end);
                if (entity.empty()) {
                    continue;
                }
                const std::size_t covered = (end - begin) + (house_span.end - house_span.begin);
                const std::size_t unexplained_tokens = tokens.size() > covered ? tokens.size() - covered : 0;
                add_interpretation(entity, house_span, nullptr, unexplained_tokens);
            }
        }
    }

    return interpretations;
}

[[nodiscard]] std::vector<QueryInterpretation> generate_named_interpretations(
    const SearchIndex& index,
    const std::string& normalized_query,
    const std::vector<std::string>& tokens,
    const std::vector<LocalitySpan>& locality_spans) {
    std::vector<QueryInterpretation> interpretations;
    std::set<std::tuple<std::string, std::size_t, std::size_t, std::size_t>> seen;

    auto add_named = [&](const std::string& entity, const LocalitySpan* locality_span, const std::size_t unexplained) {
        if (entity.empty()) return;
        QueryInterpretation interpretation;
        interpretation.intent = QueryIntent::NamedObject;
        interpretation.normalized_query = normalized_query;
        interpretation.tokens = tokens;
        interpretation.entity_name = entity;
        interpretation.unexplained_token_count = unexplained;
        if (locality_span != nullptr) {
            interpretation.locality_indices = locality_span->locality_indices;
            interpretation.locality_token_begin = locality_span->begin;
            interpretation.locality_token_end = locality_span->end;
        }
        if (const auto it = index.exact_name_index.find(entity); it != index.exact_name_index.end()) {
            interpretation.exact_entity_name_match = true;
            interpretation.raw_candidate_count = it->second.size();
        } else {
            const auto entity_tokens = tokenizeNormalizedText(entity);
            bool missing = false;
            std::size_t min_count = 0;
            for (const auto& token : entity_tokens) {
                const auto token_it = index.token_index.find(token);
                if (token_it == index.token_index.end()) {
                    missing = true;
                    break;
                }
                min_count = min_count == 0 ? token_it->second.size() : std::min(min_count, token_it->second.size());
            }
            if (!missing) {
                interpretation.raw_candidate_count = min_count;
            }
        }
        const auto marker = std::make_tuple(
            interpretation.entity_name,
            interpretation.locality_token_begin,
            interpretation.locality_token_end,
            interpretation.unexplained_token_count);
        if (seen.insert(marker).second) {
            interpretations.push_back(std::move(interpretation));
        }
    };

    for (const auto& locality_span : locality_spans) {
        std::size_t unexplained = 0;
        const auto entity = entity_from_remaining_tokens(tokens, nullptr, &locality_span, unexplained);
        add_named(entity, &locality_span, unexplained);
    }
    add_named(normalized_query, nullptr, 0);

    for (std::size_t begin = 0; begin < tokens.size(); ++begin) {
        for (std::size_t end = begin + 1; end <= tokens.size(); ++end) {
            const auto entity = join_tokens(tokens, begin, end);
            const std::size_t unexplained = tokens.size() - (end - begin);
            add_named(entity, nullptr, unexplained);
        }
    }

    return interpretations;
}

[[nodiscard]] std::string normalized_query_from_tokens(const std::vector<std::string>& tokens) {
    return join_tokens(tokens, 0, tokens.size());
}

void append_interpretations_for_tokens(
    const SearchIndex& index,
    const std::vector<std::string>& tokens,
    const QueryMatchStrategy strategy,
    std::vector<QueryInterpretation>& interpretations,
    const std::size_t edit_cost = 0) {
    const auto normalized_query = normalized_query_from_tokens(tokens);
    const auto house_spans = detect_house_spans(tokens);
    const auto locality_spans = detect_locality_spans(index, tokens);
    auto address_interpretations = generate_address_interpretations(index, normalized_query, tokens, house_spans, locality_spans);
    auto named_interpretations = generate_named_interpretations(index, normalized_query, tokens, locality_spans);
    for (auto& interpretation : address_interpretations) {
        interpretation.match_strategy = strategy;
        interpretation.edit_cost = edit_cost;
    }
    for (auto& interpretation : named_interpretations) {
        interpretation.match_strategy = strategy;
        interpretation.edit_cost = edit_cost;
    }
    interpretations.reserve(interpretations.size() + address_interpretations.size() + named_interpretations.size());
    interpretations.insert(interpretations.end(), std::make_move_iterator(address_interpretations.begin()), std::make_move_iterator(address_interpretations.end()));
    interpretations.insert(interpretations.end(), std::make_move_iterator(named_interpretations.begin()), std::make_move_iterator(named_interpretations.end()));
}

[[nodiscard]] std::vector<std::uint32_t> containing_region_indices_for_object(const DataStore& data, const SearchObjectRef& ref) {
    std::vector<std::uint32_t> out;
    std::uint32_t begin = 0;
    std::uint32_t count = 0;
    const std::vector<std::uint32_t>* ids = nullptr;
    switch (ref.type) {
        case SearchObjectType::House:
            if (ref.index >= data.houses.size()) return out;
            begin = data.houses[ref.index].containing_regions_begin;
            count = data.houses[ref.index].containing_regions_count;
            ids = &data.house_containing_region_ids;
            break;
        case SearchObjectType::Poi:
            if (ref.index >= data.pois.size()) return out;
            begin = data.pois[ref.index].containing_regions_begin;
            count = data.pois[ref.index].containing_regions_count;
            ids = &data.poi_containing_region_ids;
            break;
        case SearchObjectType::Locality:
            if (ref.index >= data.localities.size()) return out;
            begin = data.localities[ref.index].containing_regions_begin;
            count = data.localities[ref.index].containing_regions_count;
            ids = &data.locality_containing_region_ids;
            break;
        case SearchObjectType::Street:
            if (ref.index >= data.streets.size()) return out;
            begin = data.streets[ref.index].containing_regions_begin;
            count = data.streets[ref.index].containing_regions_count;
            ids = &data.street_containing_region_ids;
            break;
        case SearchObjectType::Region:
            if (ref.index >= data.regions.size()) return out;
            begin = data.regions[ref.index].containing_regions_begin;
            count = data.regions[ref.index].containing_regions_count;
            ids = &data.region_containing_region_ids;
            break;
    }
    if (ids == nullptr) return out;
    const auto end = static_cast<std::size_t>(begin) + static_cast<std::size_t>(count);
    if (end > ids->size()) return out;
    out.insert(out.end(), ids->begin() + begin, ids->begin() + end);
    return out;
}

struct LogicalRegion {
    std::uint64_t relation_id{0};
    StringId name_id{kInvalidStringId};
    std::int32_t admin_level{-1};
};

[[nodiscard]] std::vector<LogicalRegion> useful_logical_regions(const DataStore& data, const std::vector<std::uint32_t>& polygon_indices) {
    std::vector<LogicalRegion> regions;
    std::set<std::uint64_t> seen;
    for (const auto polygon_index : polygon_indices) {
        if (polygon_index >= data.regions.size()) continue;
        const auto& region = data.regions[polygon_index];
        if (region.source_relation_id == 0 || region.is_postal_region || regionSpecificity(region.admin_level) == 0) {
            continue;
        }
        if (seen.insert(region.source_relation_id).second) {
            regions.push_back(LogicalRegion{.relation_id = region.source_relation_id, .name_id = region.name_id, .admin_level = region.admin_level});
        }
    }
    std::sort(regions.begin(), regions.end(), [](const auto& lhs, const auto& rhs) {
        const auto lhs_spec = regionSpecificity(lhs.admin_level);
        const auto rhs_spec = regionSpecificity(rhs.admin_level);
        if (lhs_spec != rhs_spec) return lhs_spec > rhs_spec;
        return lhs.relation_id < rhs.relation_id;
    });
    return regions;
}

[[nodiscard]] std::vector<LogicalRegion> locality_regions(const DataStore& data, const std::uint32_t locality_index) {
    if (locality_index >= data.localities.size()) return {};
    const auto& locality = data.localities[locality_index];
    std::vector<std::uint32_t> polygon_indices;
    const auto begin = static_cast<std::size_t>(locality.containing_regions_begin);
    const auto count = static_cast<std::size_t>(locality.containing_regions_count);
    if (begin + count <= data.locality_containing_region_ids.size()) {
        polygon_indices.insert(polygon_indices.end(), data.locality_containing_region_ids.begin() + begin, data.locality_containing_region_ids.begin() + begin + count);
    }
    return useful_logical_regions(data, polygon_indices);
}

[[nodiscard]] std::pair<std::int32_t, LogicalRegion> best_shared_region(const std::vector<LogicalRegion>& locality, const std::vector<LogicalRegion>& object) {
    LogicalRegion best;
    int best_spec = 0;
    for (const auto& lhs : locality) {
        for (const auto& rhs : object) {
            if (lhs.relation_id != rhs.relation_id) continue;
            const auto spec = regionSpecificity(lhs.admin_level);
            if (spec > best_spec) {
                best_spec = spec;
                best = lhs;
            }
        }
    }
    return {best_spec, best};
}

[[nodiscard]] bool object_coordinate(const DataStore& data, const SearchObjectRef& ref, double& lat, double& lon) {
    switch (ref.type) {
        case SearchObjectType::House:
            if (ref.index >= data.houses.size()) return false;
            lat = data.houses[ref.index].lat;
            lon = data.houses[ref.index].lon;
            return true;
        case SearchObjectType::Poi:
            if (ref.index >= data.pois.size()) return false;
            lat = data.pois[ref.index].lat;
            lon = data.pois[ref.index].lon;
            return true;
        case SearchObjectType::Locality:
            if (ref.index >= data.localities.size()) return false;
            lat = data.localities[ref.index].lat;
            lon = data.localities[ref.index].lon;
            return true;
        case SearchObjectType::Street:
            if (ref.index >= data.streets.size() || data.streets[ref.index].points_count == 0) return false;
            lat = (data.streets[ref.index].bbox.min_lat + data.streets[ref.index].bbox.max_lat) / 2.0;
            lon = (data.streets[ref.index].bbox.min_lon + data.streets[ref.index].bbox.max_lon) / 2.0;
            return true;
        case SearchObjectType::Region:
            if (ref.index >= data.regions.size()) return false;
            lat = (data.regions[ref.index].bbox.min_lat + data.regions[ref.index].bbox.max_lat) / 2.0;
            lon = (data.regions[ref.index].bbox.min_lon + data.regions[ref.index].bbox.max_lon) / 2.0;
            return true;
    }
    return false;
}

[[nodiscard]] double haversine_m(const double lat1, const double lon1, const double lat2, const double lon2) {
    constexpr double earth_radius_m = 6371000.0;
    constexpr double pi = 3.14159265358979323846;
    const auto to_rad = [](const double deg) { return deg * pi / 180.0; };
    const double dlat = to_rad(lat2 - lat1);
    const double dlon = to_rad(lon2 - lon1);
    const double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                     std::cos(to_rad(lat1)) * std::cos(to_rad(lat2)) *
                     std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return earth_radius_m * c;
}

[[nodiscard]] double cell_distance_lower_bound_m(
    const double query_lat,
    const double query_lon,
    const GridCellKey cell,
    const float cell_size_deg) {
    constexpr double earth_radius_m = 6371000.0;
    constexpr double pi = 3.14159265358979323846;
    const auto to_rad = [](const double deg) { return deg * pi / 180.0; };
    const double scale = static_cast<double>(cell_size_deg);
    const double min_lat = static_cast<double>(cell.y) * scale;
    const double max_lat = static_cast<double>(cell.y + 1) * scale;
    const double min_lon = static_cast<double>(cell.x) * scale;
    const double max_lon = static_cast<double>(cell.x + 1) * scale;

    double lon_delta = 0.0;
    if (query_lon < min_lon) lon_delta = min_lon - query_lon;
    else if (query_lon > max_lon) lon_delta = query_lon - max_lon;
    lon_delta = std::min(lon_delta, 360.0 - std::min(lon_delta, 360.0));

    const double query_lat_rad = to_rad(query_lat);
    const double lon_delta_rad = to_rad(lon_delta);
    const double a = std::sin(query_lat_rad);
    const double b = std::cos(query_lat_rad) * std::cos(lon_delta_rad);
    const double unconstrained_lat = std::atan2(a, b);
    const double min_lat_rad = to_rad(min_lat);
    const double max_lat_rad = to_rad(max_lat);
    const double closest_lat = std::clamp(unconstrained_lat, min_lat_rad, max_lat_rad);
    const double max_dot = std::clamp(
        a * std::sin(closest_lat) + b * std::cos(closest_lat),
        -1.0,
        1.0);
    return earth_radius_m * std::acos(max_dot);
}

[[nodiscard]] std::string object_label(const DataStore& data, const SearchObjectRef& ref) {
    const auto resolve = [&](const StringId id) -> std::string {
        return id != kInvalidStringId && id < data.strings.size() ? data.strings.resolve(id) : std::string{};
    };
    switch (ref.type) {
        case SearchObjectType::House: {
            if (ref.index >= data.houses.size()) return {};
            const auto& house = data.houses[ref.index];
            auto label = resolve(house.street_name_id);
            const auto number = resolve(house.house_number_id);
            const auto city = resolve(house.city_id);
            if (!number.empty()) label += (label.empty() ? "" : " ") + number;
            if (!city.empty()) label += (label.empty() ? "" : ", ") + city;
            return label;
        }
        case SearchObjectType::Poi:
            return ref.index < data.pois.size() ? resolve(data.pois[ref.index].name_id) : std::string{};
        case SearchObjectType::Locality:
            return ref.index < data.localities.size() ? resolve(data.localities[ref.index].name_id) : std::string{};
        case SearchObjectType::Street:
            return ref.index < data.streets.size() ? resolve(data.streets[ref.index].name_id) : std::string{};
        case SearchObjectType::Region:
            return ref.index < data.regions.size() ? resolve(data.regions[ref.index].name_id) : std::string{};
    }
    return {};
}

[[nodiscard]] double best_distance_to_locality_m(const DataStore& data, const QueryInterpretation& interpretation, const SearchObjectRef& ref) {
    double obj_lat = 0.0;
    double obj_lon = 0.0;
    if (!object_coordinate(data, ref, obj_lat, obj_lon)) {
        return std::numeric_limits<double>::infinity();
    }
    double best = std::numeric_limits<double>::infinity();
    for (const auto locality_index : interpretation.locality_indices) {
        if (locality_index >= data.localities.size()) continue;
        const auto& locality = data.localities[locality_index];
        best = std::min(best, haversine_m(locality.lat, locality.lon, obj_lat, obj_lon));
    }
    return best;
}

void improve_candidate(CandidateMap& candidates, GeocodeCandidate candidate, const int specificity) {
    auto [it, inserted] = candidates.emplace(candidate.ref, CandidateAccumulatorEntry{.candidate = candidate, .specificity = specificity});
    if (inserted) return;
    auto& current = it->second;
    const auto current_tuple = std::make_tuple(
        current.candidate.exact_address_match,
        current.candidate.exact_name_match,
        current.candidate.locality_recognized,
        current.specificity,
        -static_cast<long long>(current.candidate.unexplained_token_count),
        -current.candidate.distance_to_locality_m);
    const auto next_tuple = std::make_tuple(
        candidate.exact_address_match,
        candidate.exact_name_match,
        candidate.locality_recognized,
        specificity,
        -static_cast<long long>(candidate.unexplained_token_count),
        -candidate.distance_to_locality_m);
    if (next_tuple > current_tuple) {
        current = CandidateAccumulatorEntry{.candidate = candidate, .specificity = specificity};
    }
}

void enrich_candidate_with_locality(const DataStore& data, const QueryInterpretation& interpretation, GeocodeCandidate& candidate) {
    candidate.locality_recognized = !interpretation.locality_indices.empty();
    candidate.unexplained_token_count = interpretation.unexplained_token_count;
    candidate.distance_to_locality_m = best_distance_to_locality_m(data, interpretation, candidate.ref);

    if (interpretation.locality_indices.empty()) {
        return;
    }

    const auto object_regions = useful_logical_regions(data, containing_region_indices_for_object(data, candidate.ref));
    int best_spec = 0;
    LogicalRegion best_region;
    for (const auto locality_index : interpretation.locality_indices) {
        const auto loc_regions = locality_regions(data, locality_index);
        auto [spec, region] = best_shared_region(loc_regions, object_regions);
        if (spec > best_spec) {
            best_spec = spec;
            best_region = region;
        }
    }
    if (best_spec > 0) {
        candidate.shared_relation_id = best_region.relation_id;
        candidate.shared_relation_name_id = best_region.name_id;
        candidate.shared_admin_level = best_region.admin_level;
    }
}

void retrieve_address_candidates(const DataStore& data, const SearchIndex& index, const QueryInterpretation& interpretation, const std::size_t interpretation_index, CandidateMap& candidates) {
    const auto key = makeAddressKey(interpretation.entity_name, interpretation.normalized_house_number);
    if (key.empty()) return;
    const auto it = index.address_index.find(key);
    if (it == index.address_index.end()) return;
    for (const auto& ref : it->second) {
        GeocodeCandidate candidate;
        candidate.ref = ref;
        candidate.interpretation_index = interpretation_index;
        candidate.match_strategy = interpretation.match_strategy;
        candidate.exact_address_match = true;
        candidate.edit_cost = interpretation.edit_cost;
        candidate.source_name_postings = interpretation.raw_candidate_count;
        enrich_candidate_with_locality(data, interpretation, candidate);
        improve_candidate(candidates, candidate, regionSpecificity(candidate.shared_admin_level));
    }
}

void retrieve_named_candidates(const DataStore& data, const SearchIndex& index, const QueryInterpretation& interpretation, const std::size_t interpretation_index, CandidateMap& candidates) {
    std::vector<SearchObjectRef> refs;
    bool exact = false;
    if (const auto it = index.exact_name_index.find(interpretation.entity_name); it != index.exact_name_index.end()) {
        refs = it->second;
        exact = true;
    } else {
        const auto tokens = tokenizeNormalizedText(interpretation.entity_name);
        std::vector<std::vector<SearchObjectRef>> postings;
        bool missing = false;
        for (const auto& token : tokens) {
            const auto it = index.token_index.find(token);
            if (it == index.token_index.end()) {
                missing = true;
                break;
            }
            postings.push_back(it->second);
        }
        if (!missing && !postings.empty()) {
            refs = intersectPostingLists(postings);
        }
    }
    for (const auto& ref : refs) {
        GeocodeCandidate candidate;
        candidate.ref = ref;
        candidate.interpretation_index = interpretation_index;
        candidate.match_strategy = interpretation.match_strategy;
        candidate.exact_name_match = exact;
        candidate.edit_cost = interpretation.edit_cost;
        candidate.source_name_postings = interpretation.raw_candidate_count;
        enrich_candidate_with_locality(data, interpretation, candidate);
        improve_candidate(candidates, candidate, regionSpecificity(candidate.shared_admin_level));
    }
}

[[nodiscard]] bool candidate_less(const GeocodeCandidate& lhs, const GeocodeCandidate& rhs) {
    const auto lhs_spec = regionSpecificity(lhs.shared_admin_level);
    const auto rhs_spec = regionSpecificity(rhs.shared_admin_level);
    if (lhs.exact_address_match != rhs.exact_address_match) return lhs.exact_address_match > rhs.exact_address_match;
    if (lhs.locality_recognized != rhs.locality_recognized) return lhs.locality_recognized > rhs.locality_recognized;
    if (lhs_spec != rhs_spec) return lhs_spec > rhs_spec;
    if (lhs.unexplained_token_count != rhs.unexplained_token_count) return lhs.unexplained_token_count < rhs.unexplained_token_count;
    if (lhs.source_name_postings != rhs.source_name_postings) return lhs.source_name_postings > rhs.source_name_postings;
    if (lhs.edit_cost != rhs.edit_cost) return lhs.edit_cost < rhs.edit_cost;
    if (lhs.exact_name_match != rhs.exact_name_match) return lhs.exact_name_match > rhs.exact_name_match;
    if (lhs.distance_to_locality_m != rhs.distance_to_locality_m) return lhs.distance_to_locality_m < rhs.distance_to_locality_m;
    return lhs.ref < rhs.ref;
}

void run_nearest_category_query(
    const DataStore& data,
    const SearchIndex& index,
    const NearestCategoryIntent& intent,
    const GeocodeQueryOptions& options,
    GeocodeQueryResult& result) {
    result.nearest_category_intent = true;
    result.nearest_category = intent.category;
    result.reference_query = intent.reference_query;
    QueryInterpretation interpretation;
    interpretation.intent = QueryIntent::NearestCategory;
    interpretation.normalized_query = result.normalized_query;
    interpretation.tokens = tokenizeNormalizedText(result.normalized_query);
    interpretation.entity_name = intent.category_text;
    result.interpretations.push_back(std::move(interpretation));

    GeocodeQueryOptions reference_options = options;
    reference_options.max_ranked_candidates = 20;
    const auto reference = run_geocode_query_internal(
        data,
        index,
        intent.reference_query,
        reference_options,
        false);
    const auto usable_reference = std::find_if(
        reference.ranked_candidates.begin(),
        reference.ranked_candidates.end(),
        [&](const GeocodeCandidate& candidate) {
            double lat = 0.0;
            double lon = 0.0;
            return candidate.unexplained_token_count == 0 &&
                   (candidate.exact_address_match || candidate.exact_name_match) &&
                   object_coordinate(data, candidate.ref, lat, lon);
        });
    if (usable_reference == reference.ranked_candidates.end() ||
        !object_coordinate(data, usable_reference->ref, result.reference_lat, result.reference_lon)) {
        result.failure_reason = "reference_not_resolved";
        return;
    }
    result.reference_resolved = true;
    result.reference_label = object_label(data, usable_reference->ref);

    const auto& cells = index.poi_cells_by_category[static_cast<std::size_t>(intent.category)];
    if (cells.empty()) {
        result.failure_reason = "category_has_no_pois";
        return;
    }

    struct CellEntry {
        double lower_bound_m{0.0};
        GridCellKey key{};
    };
    const auto farther_cell = [](const CellEntry& lhs, const CellEntry& rhs) {
        if (lhs.lower_bound_m != rhs.lower_bound_m) return lhs.lower_bound_m > rhs.lower_bound_m;
        if (lhs.key.x != rhs.key.x) return lhs.key.x > rhs.key.x;
        return lhs.key.y > rhs.key.y;
    };
    std::priority_queue<CellEntry, std::vector<CellEntry>, decltype(farther_cell)> pending_cells(farther_cell);
    for (const auto& [key, _] : cells) {
        pending_cells.push(CellEntry{
            .lower_bound_m = cell_distance_lower_bound_m(
                result.reference_lat,
                result.reference_lon,
                key,
                data.grid.cell_size_deg),
            .key = key,
        });
    }

    struct PoiDistance {
        double distance_m{0.0};
        std::uint32_t poi_index{0};
    };
    const auto nearer_poi = [](const PoiDistance& lhs, const PoiDistance& rhs) {
        if (lhs.distance_m != rhs.distance_m) return lhs.distance_m < rhs.distance_m;
        return lhs.poi_index < rhs.poi_index;
    };
    std::priority_queue<PoiDistance, std::vector<PoiDistance>, decltype(nearer_poi)> best_pois(nearer_poi);
    const std::size_t limit = options.max_ranked_candidates;

    while (!pending_cells.empty()) {
        const auto cell = pending_cells.top();
        if (limit > 0 && best_pois.size() >= limit && cell.lower_bound_m > best_pois.top().distance_m) {
            break;
        }
        pending_cells.pop();
        ++result.spatial_cells_examined;
        const auto found = cells.find(cell.key);
        if (found == cells.end()) continue;
        for (const auto poi_index : found->second) {
            if (poi_index >= data.pois.size() || data.pois[poi_index].category != intent.category) continue;
            ++result.spatial_pois_tested;
            const auto& poi = data.pois[poi_index];
            const PoiDistance candidate{
                .distance_m = haversine_m(result.reference_lat, result.reference_lon, poi.lat, poi.lon),
                .poi_index = poi_index,
            };
            if (limit == 0 || best_pois.size() < limit) {
                best_pois.push(candidate);
            } else if (nearer_poi(candidate, best_pois.top())) {
                best_pois.pop();
                best_pois.push(candidate);
            }
        }
    }

    std::vector<PoiDistance> ordered;
    ordered.reserve(best_pois.size());
    while (!best_pois.empty()) {
        ordered.push_back(best_pois.top());
        best_pois.pop();
    }
    std::sort(ordered.begin(), ordered.end(), nearer_poi);
    result.ranked_candidates.reserve(ordered.size());
    for (const auto& match : ordered) {
        GeocodeCandidate candidate;
        candidate.ref = SearchObjectRef{.type = SearchObjectType::Poi, .index = match.poi_index};
        candidate.interpretation_index = 0;
        candidate.nearest_distance_m = match.distance_m;
        result.ranked_candidates.push_back(candidate);
    }
    if (result.ranked_candidates.empty()) result.failure_reason = "category_has_no_pois";
}

} // namespace

const char* queryIntentName(const QueryIntent intent) {
    switch (intent) {
        case QueryIntent::Address: return "Address";
        case QueryIntent::NamedObject: return "NamedObject";
        case QueryIntent::NearestCategory: return "NearestCategory";
        case QueryIntent::Unknown: return "Unknown";
    }
    return "Unknown";
}

const char* queryMatchStrategyName(const QueryMatchStrategy strategy) {
    switch (strategy) {
        case QueryMatchStrategy::Original: return "original";
        case QueryMatchStrategy::Fuzzy: return "fuzzy";
        case QueryMatchStrategy::Partial: return "partial";
    }
    return "original";
}

int regionSpecificity(const std::int32_t admin_level) {
    switch (admin_level) {
        case 8: return 3;
        case 6: return 2;
        case 4: return 1;
        default: return 0;
    }
}

GeocodeQueryResult run_geocode_query_internal(
    const DataStore& data,
    const SearchIndex& index,
    const std::string& input,
    const GeocodeQueryOptions& options,
    const bool allow_nearest_intent) {
    const auto total_start = std::chrono::steady_clock::now();
    GeocodeQueryResult result;
    result.input = input;

    const auto norm_start = std::chrono::steady_clock::now();
    result.normalized_query = normalizeSearchText(input);
    const auto tokens = tokenizeNormalizedText(result.normalized_query);
    const auto norm_end = std::chrono::steady_clock::now();
    result.timings.normalization_ms = elapsed_ms(norm_start, norm_end);

    if (allow_nearest_intent) {
        const auto nearest_intent = parse_nearest_category_intent(tokens);
        if (nearest_intent.has_value()) {
            const auto interp_start = std::chrono::steady_clock::now();
            run_nearest_category_query(data, index, *nearest_intent, options, result);
            const auto end = std::chrono::steady_clock::now();
            result.timings.interpretation_ms = elapsed_ms(interp_start, end);
            result.timings.candidate_lookup_ms = result.timings.interpretation_ms;
            result.timings.total_ms = elapsed_ms(total_start, end);
            return result;
        }
    }

    const auto interp_start = std::chrono::steady_clock::now();
    append_interpretations_for_tokens(index, tokens, QueryMatchStrategy::Original, result.interpretations);
    const auto fuzzy_variants = fuzzy_token_variants(index, tokens);
    for (const auto& variant : fuzzy_variants) {
        append_interpretations_for_tokens(index, variant.tokens, QueryMatchStrategy::Fuzzy, result.interpretations, variant.edit_cost);
    }
    for (const auto& partial_tokens : partial_token_variants(index, tokens)) {
        append_interpretations_for_tokens(index, partial_tokens, QueryMatchStrategy::Partial, result.interpretations);
    }
    std::stable_sort(result.interpretations.begin(), result.interpretations.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.exact_address_key_match != rhs.exact_address_key_match) return lhs.exact_address_key_match > rhs.exact_address_key_match;
        if (lhs.exact_entity_name_match != rhs.exact_entity_name_match) return lhs.exact_entity_name_match > rhs.exact_entity_name_match;
        if (lhs.locality_indices.empty() != rhs.locality_indices.empty()) return !lhs.locality_indices.empty();
        if (lhs.unexplained_token_count != rhs.unexplained_token_count) return lhs.unexplained_token_count < rhs.unexplained_token_count;
        if (lhs.raw_candidate_count != rhs.raw_candidate_count) return lhs.raw_candidate_count > rhs.raw_candidate_count;
        return lhs.entity_name < rhs.entity_name;
    });
    const auto interp_end = std::chrono::steady_clock::now();
    result.timings.interpretation_ms = elapsed_ms(interp_start, interp_end);

    const auto lookup_start = std::chrono::steady_clock::now();
    CandidateMap candidates;
    for (std::size_t i = 0; i < result.interpretations.size(); ++i) {
        const auto& interpretation = result.interpretations[i];
        if (interpretation.intent == QueryIntent::Address) {
            retrieve_address_candidates(data, index, interpretation, i, candidates);
        } else if (interpretation.intent == QueryIntent::NamedObject) {
            retrieve_named_candidates(data, index, interpretation, i, candidates);
        }
    }
    const auto lookup_end = std::chrono::steady_clock::now();
    result.timings.candidate_lookup_ms = elapsed_ms(lookup_start, lookup_end);
    result.timings.region_matching_ms = result.timings.candidate_lookup_ms;

    const auto ranking_start = std::chrono::steady_clock::now();
    result.ranked_candidates.reserve(candidates.size());
    for (auto& [_, entry] : candidates) {
        result.ranked_candidates.push_back(std::move(entry.candidate));
    }
    std::sort(result.ranked_candidates.begin(), result.ranked_candidates.end(), candidate_less);
    if (options.max_ranked_candidates > 0 && result.ranked_candidates.size() > options.max_ranked_candidates) {
        result.ranked_candidates.resize(options.max_ranked_candidates);
    }
    const auto ranking_end = std::chrono::steady_clock::now();
    result.timings.ranking_ms = elapsed_ms(ranking_start, ranking_end);
    result.timings.total_ms = elapsed_ms(total_start, ranking_end);
    return result;
}

GeocodeQueryResult runGeocodeQuery(
    const DataStore& data,
    const SearchIndex& index,
    const std::string& input,
    const GeocodeQueryOptions& options) {
    return run_geocode_query_internal(data, index, input, options, true);
}

} // namespace osm::search
#include "search/search_index.hpp"

#include "search/text_normalizer.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace osm::search {
namespace {

[[nodiscard]] std::size_t levenshtein_distance(std::string_view lhs, std::string_view rhs) {
    std::vector<std::size_t> previous(rhs.size() + 1);
    std::vector<std::size_t> current(rhs.size() + 1);
    for (std::size_t j = 0; j <= rhs.size(); ++j) previous[j] = j;
    for (std::size_t i = 1; i <= lhs.size(); ++i) {
        current[0] = i;
        for (std::size_t j = 1; j <= rhs.size(); ++j) {
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1,
                                   previous[j - 1] + (lhs[i - 1] == rhs[j - 1] ? 0U : 1U)});
        }
        previous.swap(current);
    }
    return previous[rhs.size()];
}

void bk_insert(BkTree& tree, const std::string& token) {
    if (tree.nodes.empty()) {
        tree.nodes.push_back(BkTreeNode{.token = token});
        return;
    }
    std::uint32_t node = 0;
    while (true) {
        const auto distance = levenshtein_distance(token, tree.nodes[node].token);
        const auto child = tree.nodes[node].children.find(distance);
        if (child == tree.nodes[node].children.end()) {
            const auto next = static_cast<std::uint32_t>(tree.nodes.size());
            tree.nodes[node].children.emplace(distance, next);
            tree.nodes.push_back(BkTreeNode{.token = token});
            return;
        }
        node = child->second;
    }
}

void add_exact_name(SearchIndex& index, const std::string& normalized_name, const SearchObjectRef ref) {
    if (normalized_name.empty()) {
        return;
    }
    index.exact_name_index[normalized_name].push_back(ref);
}

void add_tokens(SearchIndex& index, const std::string& normalized_name, const SearchObjectRef ref) {
    if (normalized_name.empty()) {
        return;
    }

    auto tokens = tokenizeNormalizedText(normalized_name);
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());

    for (const auto& token : tokens) {
        if (!token.empty()) {
            index.token_index[token].push_back(ref);
        }
    }
}

struct NameKey {
    std::string normalized_name;
    IndexedNameKind kind{IndexedNameKind::Street};

    [[nodiscard]] bool operator==(const NameKey& other) const noexcept {
        return kind == other.kind && normalized_name == other.normalized_name;
    }
};

struct NameKeyHash {
    [[nodiscard]] std::size_t operator()(const NameKey& key) const noexcept {
        const auto text_hash = std::hash<std::string>{}(key.normalized_name);
        return text_hash ^ (static_cast<std::size_t>(key.kind) * 0x9e3779b97f4a7c15ULL);
    }
};

using NameIdByKey = std::unordered_map<NameKey, std::uint32_t, NameKeyHash>;

void add_full_name(
    SearchIndex& index,
    NameIdByKey& ids,
    const std::string& normalized_name,
    const IndexedNameKind kind,
    const SearchObjectRef ref) {
    if (normalized_name.empty()) {
        return;
    }
    const NameKey key{.normalized_name = normalized_name, .kind = kind};
    const auto found = ids.find(key);
    if (found != ids.end()) {
        index.names[found->second].objects.push_back(ref);
        return;
    }
    const auto name_id = static_cast<std::uint32_t>(index.names.size());
    index.names.push_back(IndexedName{
        .normalized_name = normalized_name,
        .kind = kind,
        .objects = {ref},
    });
    ids.emplace(key, name_id);
}

template <typename T>
void sort_and_dedup(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

void finalize_index(SearchIndex& index) {
    for (auto& [_, postings] : index.exact_name_index) {
        sort_and_dedup(postings);
    }
    for (auto& [_, postings] : index.token_index) {
        sort_and_dedup(postings);
    }
    for (auto& [_, regions] : index.region_name_index) {
        sort_and_dedup(regions);
    }
    for (auto& [_, localities] : index.locality_name_index) {
        sort_and_dedup(localities);
    }
    for (auto& [_, postings] : index.address_index) {
        sort_and_dedup(postings);
    }
    for (auto& name : index.names) {
        sort_and_dedup(name.objects);
    }

    std::size_t suffix_count = 0;
    for (const auto& name : index.names) {
        for (std::size_t offset = 0; offset < name.normalized_name.size(); ++offset) {
            if (name.normalized_name[offset] != ' ') {
                ++suffix_count;
            }
        }
    }
    index.suffix_array.reserve(suffix_count);
    for (std::uint32_t name_id = 0; name_id < index.names.size(); ++name_id) {
        const auto& value = index.names[name_id].normalized_name;
        for (std::uint32_t offset = 0; offset < value.size(); ++offset) {
            if (value[offset] != ' ') {
                index.suffix_array.push_back(SuffixRef{.name_id = name_id, .offset = offset});
            }
        }
    }
    std::sort(index.suffix_array.begin(), index.suffix_array.end(), [&](const SuffixRef& lhs, const SuffixRef& rhs) {
        const std::string_view lhs_suffix{index.names[lhs.name_id].normalized_name.data() + lhs.offset,
                                          index.names[lhs.name_id].normalized_name.size() - lhs.offset};
        const std::string_view rhs_suffix{index.names[rhs.name_id].normalized_name.data() + rhs.offset,
                                          index.names[rhs.name_id].normalized_name.size() - rhs.offset};
        if (lhs_suffix != rhs_suffix) return lhs_suffix < rhs_suffix;
        if (lhs.name_id != rhs.name_id) return lhs.name_id < rhs.name_id;
        return lhs.offset < rhs.offset;
    });

    std::array<std::unordered_set<std::string>, 4> vocabularies;
    for (const auto& name : index.names) {
        auto& vocabulary = vocabularies[static_cast<std::size_t>(name.kind)];
        for (const auto& token : tokenizeNormalizedText(name.normalized_name)) {
            if (token.size() >= 3 && std::none_of(token.begin(), token.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                vocabulary.insert(token);
            }
        }
    }
    for (std::size_t kind = 0; kind < vocabularies.size(); ++kind) {
        std::vector<std::string> sorted(vocabularies[kind].begin(), vocabularies[kind].end());
        std::sort(sorted.begin(), sorted.end());
        for (const auto& token : sorted) bk_insert(index.fuzzy_trees[kind], token);
    }
}

[[nodiscard]] std::size_t posting_count(const std::unordered_map<std::string, std::vector<SearchObjectRef>>& map) {
    std::size_t total = 0;
    for (const auto& [_, postings] : map) {
        total += postings.size();
    }
    return total;
}

[[nodiscard]] std::size_t region_posting_count(const std::unordered_map<std::string, std::vector<std::uint32_t>>& map) {
    std::size_t total = 0;
    for (const auto& [_, postings] : map) {
        total += postings.size();
    }
    return total;
}

[[nodiscard]] std::vector<std::pair<std::string, std::size_t>> largest_token_postings(const SearchIndex& index, const std::size_t limit) {
    std::vector<std::pair<std::string, std::size_t>> values;
    values.reserve(index.token_index.size());
    for (const auto& [token, postings] : index.token_index) {
        values.emplace_back(token, postings.size());
    }
    std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) return lhs.second > rhs.second;
        return lhs.first < rhs.first;
    });
    if (values.size() > limit) {
        values.resize(limit);
    }
    return values;
}

[[nodiscard]] std::string object_type_name(const SearchObjectType type) {
    switch (type) {
        case SearchObjectType::House: return "House";
        case SearchObjectType::Street: return "Street";
        case SearchObjectType::Poi: return "POI";
        case SearchObjectType::Region: return "Region";
        case SearchObjectType::Locality: return "Locality";
    }
    return "Unknown";
}

void add_limited_example(std::vector<std::string>& examples, std::string value, const std::size_t limit = 20) {
    if (examples.size() < limit) {
        examples.push_back(std::move(value));
    }
}

} // namespace

std::string makeAddressKey(std::string_view normalized_street, std::string_view normalized_house_number) {
    if (normalized_street.empty() || normalized_house_number.empty()) {
        return {};
    }
    std::string key;
    key.reserve(normalized_street.size() + normalized_house_number.size() + 1);
    key.append(normalized_street);
    key.push_back('|');
    key.append(normalized_house_number);
    return key;
}

SearchIndexBuildResult buildSearchIndex(const DataStore& data) {
    const auto start = std::chrono::steady_clock::now();

    SearchIndexBuildResult result;
    auto& index = result.index;
    auto& metrics = result.metrics;
    NameIdByKey full_name_ids;

    for (std::uint32_t i = 0; i < data.houses.size(); ++i) {
        const auto& house = data.houses[i];
        if (house.street_name_id == kInvalidStringId || house.street_name_id >= data.strings.size()) {
            ++metrics.skipped_addresses_missing_street;
            continue;
        }
        if (house.house_number_id == kInvalidStringId || house.house_number_id >= data.strings.size()) {
            ++metrics.skipped_addresses_missing_house_number;
            continue;
        }
        const auto normalized_street = normalizeSearchText(data.strings.resolve(house.street_name_id));
        const auto normalized_house_number = normalizeHouseNumber(data.strings.resolve(house.house_number_id));
        const auto address_key = makeAddressKey(normalized_street, normalized_house_number);
        if (address_key.empty()) {
            ++metrics.skipped_addresses_empty_normalized_key;
            continue;
        }
        index.address_index[address_key].push_back(SearchObjectRef{.type = SearchObjectType::House, .index = i});
        add_full_name(
            index,
            full_name_ids,
            normalized_street,
            IndexedNameKind::Street,
            SearchObjectRef{.type = SearchObjectType::House, .index = i});
        ++metrics.indexed_addresses;
    }

    for (std::uint32_t i = 0; i < data.streets.size(); ++i) {
        const auto& street = data.streets[i];
        if (street.name_id == kInvalidStringId || street.name_id >= data.strings.size()) {
            ++metrics.skipped_unnamed_streets;
            continue;
        }

        const auto normalized = normalizeSearchText(data.strings.resolve(street.name_id));
        if (normalized.empty()) {
            ++metrics.skipped_unnamed_streets;
            continue;
        }

        const SearchObjectRef ref{.type = SearchObjectType::Street, .index = i};
        add_exact_name(index, normalized, ref);
        add_tokens(index, normalized, ref);
        add_full_name(index, full_name_ids, normalized, IndexedNameKind::Street, ref);
        ++metrics.indexed_streets;
    }

    for (std::uint32_t i = 0; i < data.pois.size(); ++i) {
        const auto& poi = data.pois[i];
        if (poi.name_id == kInvalidStringId || poi.name_id >= data.strings.size()) {
            ++metrics.skipped_pois_invalid_name_id;
            std::ostringstream example;
            example << "osm_id=" << poi.osm_id << " name_id=" << poi.name_id << " reason=invalid_name_id";
            add_limited_example(metrics.skipped_poi_examples, example.str());
            continue;
        }

        const auto& original_name = data.strings.resolve(poi.name_id);
        const auto normalized = normalizeSearchText(original_name);
        if (normalized.empty()) {
            ++metrics.skipped_pois_empty_normalized_name;
            std::ostringstream example;
            example << "osm_id=" << poi.osm_id << " original_name=\"" << original_name << "\" reason=empty_normalized_name";
            add_limited_example(metrics.skipped_poi_examples, example.str());
            continue;
        }

        const SearchObjectRef ref{.type = SearchObjectType::Poi, .index = i};
        add_exact_name(index, normalized, ref);
        add_tokens(index, normalized, ref);
        add_full_name(index, full_name_ids, normalized, IndexedNameKind::Poi, ref);
        const auto cell = to_grid_cell(poi.lon, poi.lat, data.grid.cell_size_deg);
        index.poi_cells_by_category[static_cast<std::size_t>(poi.category)][cell].push_back(i);
        ++metrics.indexed_pois;
    }

    for (std::uint32_t i = 0; i < data.localities.size(); ++i) {
        const auto& locality = data.localities[i];
        if (locality.name_id == kInvalidStringId || locality.name_id >= data.strings.size()) continue;
        const auto normalized = normalizeSearchText(data.strings.resolve(locality.name_id));
        if (normalized.empty()) continue;
        const SearchObjectRef ref{.type = SearchObjectType::Locality, .index = i};
        add_exact_name(index, normalized, ref);
        add_tokens(index, normalized, ref);
        add_full_name(index, full_name_ids, normalized, IndexedNameKind::Locality, ref);
        index.locality_name_index[normalized].push_back(i);
        ++metrics.indexed_localities;
    }

    metrics.regions_seen = data.regions.size();
    for (std::uint32_t i = 0; i < data.regions.size(); ++i) {
        const auto& region = data.regions[i];
        if (region.name_id == kInvalidStringId || region.name_id >= data.strings.size()) {
            ++metrics.regions_skipped_invalid_name_id;
            std::ostringstream example;
            example << "region_index=" << i << " admin_level=" << region.admin_level << " reason=invalid_name_id";
            add_limited_example(metrics.skipped_region_examples, example.str());
            continue;
        }

        const auto& original_name = data.strings.resolve(region.name_id);
        const auto normalized = normalizeSearchText(original_name);
        if (normalized.empty()) {
            ++metrics.regions_skipped_empty_normalized_name;
            std::ostringstream example;
            example << "region_index=" << i << " admin_level=" << region.admin_level << " original_name=\"" << original_name << "\" reason=empty_normalized_name";
            add_limited_example(metrics.skipped_region_examples, example.str());
            continue;
        }

        index.region_name_index[normalized].push_back(i);
        const SearchObjectRef ref{.type = SearchObjectType::Region, .index = i};
        add_exact_name(index, normalized, ref);
        add_tokens(index, normalized, ref);
        add_full_name(index, full_name_ids, normalized, IndexedNameKind::Region, ref);
        ++metrics.indexed_regions;
        std::ostringstream example;
        example << "region_index=" << i << " admin_level=" << region.admin_level << " name=\"" << original_name << "\" normalized=\"" << normalized << "\"";
        add_limited_example(metrics.indexed_region_examples, example.str());
    }

    finalize_index(index);

    metrics.address_keys = index.address_index.size();
    metrics.address_postings = posting_count(index.address_index);
    metrics.exact_name_keys = index.exact_name_index.size();
    metrics.exact_name_postings = posting_count(index.exact_name_index);
    metrics.token_keys = index.token_index.size();
    metrics.token_postings = posting_count(index.token_index);
    metrics.region_name_keys = index.region_name_index.size();
    metrics.region_name_postings = region_posting_count(index.region_name_index);
    metrics.locality_name_keys = index.locality_name_index.size();
    metrics.locality_name_postings = region_posting_count(index.locality_name_index);
    metrics.indexed_full_names = index.names.size();
    metrics.suffix_count = index.suffix_array.size();
    metrics.estimated_suffix_bytes = index.suffix_array.capacity() * sizeof(SuffixRef);
    for (const auto& tree : index.fuzzy_trees) metrics.fuzzy_vocabulary_tokens += tree.nodes.size();
    metrics.largest_token_postings = largest_token_postings(index, 5);
    if (!metrics.largest_token_postings.empty()) {
        metrics.longest_posting_token = metrics.largest_token_postings.front().first;
        metrics.longest_posting_list = metrics.largest_token_postings.front().second;
    }
    metrics.build_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    return result;
}

std::vector<SearchObjectRef> intersectPostingLists(const std::vector<std::vector<SearchObjectRef>>& postings) {
    if (postings.empty()) {
        return {};
    }

    std::vector<const std::vector<SearchObjectRef>*> ordered;
    ordered.reserve(postings.size());
    for (const auto& posting : postings) {
        if (posting.empty()) {
            return {};
        }
        ordered.push_back(&posting);
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->size() < rhs->size();
    });

    std::vector<SearchObjectRef> result = *ordered.front();
    for (std::size_t i = 1; i < ordered.size(); ++i) {
        std::vector<SearchObjectRef> next;
        std::set_intersection(
            result.begin(),
            result.end(),
            ordered[i]->begin(),
            ordered[i]->end(),
            std::back_inserter(next));
        result = std::move(next);
        if (result.empty()) {
            break;
        }
    }
    return result;
}

std::vector<SubstringNameMatch> findSubstringNameMatches(
    const SearchIndex& index,
    const std::string_view normalized_fragment,
    const std::size_t limit,
    const std::optional<IndexedNameKind> required_kind) {
    if (normalized_fragment.size() < 2 || limit == 0 || index.suffix_array.empty()) {
        return {};
    }

    const auto suffix_view = [&](const SuffixRef& ref) {
        const auto& value = index.names[ref.name_id].normalized_name;
        return std::string_view{value}.substr(ref.offset);
    };
    const auto lower = std::lower_bound(
        index.suffix_array.begin(),
        index.suffix_array.end(),
        normalized_fragment,
        [&](const SuffixRef& ref, const std::string_view fragment) {
            return suffix_view(ref) < fragment;
        });

    std::vector<SubstringNameMatch> matches;
    std::unordered_set<std::uint32_t> seen;
    for (auto it = lower; it != index.suffix_array.end(); ++it) {
        const auto suffix = suffix_view(*it);
        if (!suffix.starts_with(normalized_fragment)) {
            break;
        }
        if (!seen.insert(it->name_id).second) {
            continue;
        }
        if (required_kind.has_value() && index.names[it->name_id].kind != *required_kind) continue;
        matches.push_back(SubstringNameMatch{
            .name_id = it->name_id,
            .starts_at_name_begin = it->offset == 0,
        });
    }

    std::sort(matches.begin(), matches.end(), [&](const auto& lhs, const auto& rhs) {
        if (lhs.starts_at_name_begin != rhs.starts_at_name_begin) {
            return lhs.starts_at_name_begin > rhs.starts_at_name_begin;
        }
        const auto& lhs_name = index.names[lhs.name_id].normalized_name;
        const auto& rhs_name = index.names[rhs.name_id].normalized_name;
        const auto lhs_postings = index.names[lhs.name_id].objects.size();
        const auto rhs_postings = index.names[rhs.name_id].objects.size();
        if (lhs_postings != rhs_postings) return lhs_postings > rhs_postings;
        const auto lhs_gap = lhs_name.size() - normalized_fragment.size();
        const auto rhs_gap = rhs_name.size() - normalized_fragment.size();
        if (lhs_gap != rhs_gap) return lhs_gap < rhs_gap;
        if (lhs_name != rhs_name) return lhs_name < rhs_name;
        return static_cast<std::uint8_t>(index.names[lhs.name_id].kind) <
               static_cast<std::uint8_t>(index.names[rhs.name_id].kind);
    });
    if (matches.size() > limit) {
        matches.resize(limit);
    }
    return matches;
}

std::vector<FuzzyTokenMatch> findFuzzyTokenMatches(
    const SearchIndex& index,
    const std::string_view token,
    const IndexedNameKind kind,
    const std::size_t max_distance,
    const std::size_t limit) {
    const auto& tree = index.fuzzy_trees[static_cast<std::size_t>(kind)];
    if (tree.nodes.empty() || token.size() < 3 || max_distance == 0 || limit == 0) return {};
    std::vector<FuzzyTokenMatch> matches;
    std::vector<std::uint32_t> pending{0};
    while (!pending.empty()) {
        const auto node_index = pending.back();
        pending.pop_back();
        const auto& node = tree.nodes[node_index];
        const auto distance = levenshtein_distance(token, node.token);
        if (distance <= max_distance) {
            matches.push_back(FuzzyTokenMatch{.token = node.token, .kind = kind, .distance = distance});
        }
        const auto minimum = distance > max_distance ? distance - max_distance : 0;
        const auto maximum = distance + max_distance;
        for (const auto& [edge, child] : node.children) {
            if (edge >= minimum && edge <= maximum) pending.push_back(child);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.distance != rhs.distance) return lhs.distance < rhs.distance;
        return lhs.token < rhs.token;
    });
    if (matches.size() > limit) matches.resize(limit);
    return matches;
}

} // namespace osm::search

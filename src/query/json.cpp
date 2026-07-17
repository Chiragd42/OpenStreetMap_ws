#include "query/json.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>

namespace osm {

namespace {

std::string escape_json(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (const char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string resolve_string_or_empty(const StringPool& pool, const StringId id) {
    if (id == kInvalidStringId || id >= pool.size()) {
        return {};
    }
    return pool.resolve(id);
}

std::string join_tokens(const std::vector<std::string>& tokens, const std::size_t begin, const std::size_t end) {
    std::string out;
    for (std::size_t i = begin; i < end && i < tokens.size(); ++i) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += tokens[i];
    }
    return out;
}

std::string first_locality_name(const DataStore& data, const std::vector<std::uint32_t>& locality_indices) {
    if (locality_indices.empty() || locality_indices.front() >= data.localities.size()) {
        return {};
    }
    return resolve_string_or_empty(data.strings, data.localities[locality_indices.front()].name_id);
}

const char* search_object_type_json(const SearchObjectType type) {
    switch (type) {
        case SearchObjectType::House: return "house";
        case SearchObjectType::Street: return "street";
        case SearchObjectType::Poi: return "poi";
        case SearchObjectType::Region: return "region";
        case SearchObjectType::Locality: return "locality";
    }
    return "unknown";
}

std::string geocode_object_name(const DataStore& data, const SearchObjectRef& ref) {
    switch (ref.type) {
        case SearchObjectType::House: {
            if (ref.index >= data.houses.size()) return {};
            const auto& house = data.houses[ref.index];
            auto name = resolve_string_or_empty(data.strings, house.street_name_id);
            const auto house_number = resolve_string_or_empty(data.strings, house.house_number_id);
            if (!house_number.empty()) {
                if (!name.empty()) name.push_back(' ');
                name += house_number;
            }
            return name;
        }
        case SearchObjectType::Street:
            return ref.index < data.streets.size() ? resolve_string_or_empty(data.strings, data.streets[ref.index].name_id) : std::string{};
        case SearchObjectType::Poi:
            return ref.index < data.pois.size() ? resolve_string_or_empty(data.strings, data.pois[ref.index].name_id) : std::string{};
        case SearchObjectType::Region:
            return ref.index < data.regions.size() ? resolve_string_or_empty(data.strings, data.regions[ref.index].name_id) : std::string{};
        case SearchObjectType::Locality:
            return ref.index < data.localities.size() ? resolve_string_or_empty(data.strings, data.localities[ref.index].name_id) : std::string{};
    }
    return {};
}

GeoPoint geocode_object_point(const DataStore& data, const SearchObjectRef& ref) {
    switch (ref.type) {
        case SearchObjectType::House:
            if (ref.index < data.houses.size()) return GeoPoint{.lat = data.houses[ref.index].lat, .lon = data.houses[ref.index].lon};
            break;
        case SearchObjectType::Poi:
            if (ref.index < data.pois.size()) return GeoPoint{.lat = data.pois[ref.index].lat, .lon = data.pois[ref.index].lon};
            break;
        case SearchObjectType::Locality:
            if (ref.index < data.localities.size()) return GeoPoint{.lat = data.localities[ref.index].lat, .lon = data.localities[ref.index].lon};
            break;
        case SearchObjectType::Street:
            if (ref.index < data.streets.size()) {
                const auto& bbox = data.streets[ref.index].bbox;
                return GeoPoint{.lat = static_cast<float>((bbox.min_lat + bbox.max_lat) * 0.5), .lon = static_cast<float>((bbox.min_lon + bbox.max_lon) * 0.5)};
            }
            break;
        case SearchObjectType::Region:
            if (ref.index < data.regions.size()) {
                const auto& bbox = data.regions[ref.index].bbox;
                return GeoPoint{.lat = static_cast<float>((bbox.min_lat + bbox.max_lat) * 0.5), .lon = static_cast<float>((bbox.min_lon + bbox.max_lon) * 0.5)};
            }
            break;
    }
    return {};
}

std::string candidate_city(const DataStore& data, const SearchObjectRef& ref) {
    if (ref.type == SearchObjectType::House && ref.index < data.houses.size()) {
        return resolve_string_or_empty(data.strings, data.houses[ref.index].city_id);
    }
    return {};
}

std::string candidate_postcode(const DataStore& data, const SearchObjectRef& ref) {
    if (ref.type == SearchObjectType::House && ref.index < data.houses.size()) {
        return resolve_string_or_empty(data.strings, data.houses[ref.index].postcode_id);
    }
    return {};
}

std::string rank_reason(const search::GeocodeCandidate& candidate) {
    std::string reason;
    if (candidate.exact_address_match) reason += "exact address";
    if (candidate.exact_name_match) {
        if (!reason.empty()) reason += " + ";
        reason += "exact name";
    }
    if (candidate.shared_admin_level != -1) {
        if (!reason.empty()) reason += " + ";
        reason += "shared level " + std::to_string(candidate.shared_admin_level) + " relation";
    } else if (candidate.locality_recognized) {
        if (!reason.empty()) reason += " + ";
        reason += "recognized locality";
    }
    if (candidate.unexplained_token_count > 0 && candidate.unexplained_token_count != std::numeric_limits<std::size_t>::max()) {
        if (!reason.empty()) reason += " + ";
        reason += std::to_string(candidate.unexplained_token_count) + " unexplained token(s)";
    }
    if (reason.empty()) reason = "stable fallback ordering";
    return reason;
}

void serialize_house_containing_regions(
    std::ostringstream& out,
    const DataStore& data,
    const std::size_t house_index) {
    out << "[";
    if (house_index >= data.houses.size()) {
        out << "]";
        return;
    }

    const auto& house = data.houses[house_index];
    const auto begin = static_cast<std::size_t>(house.containing_regions_begin);
    const auto count = static_cast<std::size_t>(house.containing_regions_count);
    if (begin + count > data.house_containing_region_ids.size()) {
        out << "]";
        return;
    }

    bool first = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto region_index = static_cast<std::size_t>(data.house_containing_region_ids[begin + i]);
        if (region_index >= data.regions.size()) {
            continue;
        }
        const auto& region = data.regions[region_index];
        if (!first) {
            out << ',';
        }
        first = false;
        out << '{'
            << "\"name\":\"" << escape_json(resolve_string_or_empty(data.strings, region.name_id)) << "\","
            << "\"admin_level\":" << region.admin_level << ','
            << "\"relation_id\":" << region.source_relation_id
            << '}';
    }
    out << "]";
}

} // namespace

std::optional<BBox> parse_bbox_csv(std::string_view csv) {
    std::array<double, 4> values{};

    std::size_t value_idx = 0;
    std::size_t start = 0;

    while (start <= csv.size() && value_idx < values.size()) {
        const auto end = csv.find(',', start);
        const std::size_t token_end = (end == std::string_view::npos) ? csv.size() : end;
        const auto token = csv.substr(start, token_end - start);
        if (token.empty()) {
            return std::nullopt;
        }

        double parsed = 0.0;
        const auto* begin_ptr = token.data();
        const auto* end_ptr = token.data() + token.size();
        const auto result = std::from_chars(begin_ptr, end_ptr, parsed);
        if (result.ec != std::errc{} || result.ptr != end_ptr) {
            return std::nullopt;
        }

        values[value_idx++] = parsed;

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    if (value_idx != values.size()) {
        return std::nullopt;
    }

    BBox bbox;
    bbox.min_lon = values[0];
    bbox.min_lat = values[1];
    bbox.max_lon = values[2];
    bbox.max_lat = values[3];

    if (bbox.min_lon > bbox.max_lon || bbox.min_lat > bbox.max_lat) {
        return std::nullopt;
    }

    return bbox;
}

std::string serialize_stats_json(const ParseStats& stats) {
    std::ostringstream out;
    out << "{"
        << "\"processed_nodes\":" << stats.processed_nodes << ','
        << "\"processed_ways\":" << stats.processed_ways << ','
        << "\"processed_relations\":" << stats.processed_relations << ','
        << "\"extracted_houses\":" << stats.extracted_houses << ','
        << "\"extracted_streets\":" << stats.extracted_streets << ','
        << "\"extracted_regions\":" << stats.extracted_regions << ','
        << "\"extracted_pois_total\":" << stats.extracted_pois_total << ','
        << "\"extracted_poi_nodes\":" << stats.extracted_poi_nodes << ','
        << "\"extracted_poi_ways\":" << stats.extracted_poi_ways << ','
        << "\"skipped_unnamed_pois\":" << stats.skipped_unnamed_pois << ','
        << "\"skipped_invalid_poi_geometry\":" << stats.skipped_invalid_poi_geometry << ','
        << "\"pois_assigned_to_region\":" << stats.pois_assigned_to_region << ','
        << "\"pois_without_region\":" << stats.pois_without_region << ','
        << "\"poi_region_assignment_seconds\":" << stats.poi_region_assignment_seconds << ','
        << "\"houses_from_address_nodes\":" << stats.houses_from_address_nodes << ','
        << "\"houses_from_polygon_centroid\":" << stats.houses_from_polygon_centroid << ','
        << "\"houses_from_polygon_bbox_fallback\":" << stats.houses_from_polygon_bbox_fallback << ','
        << "\"houses_skipped_invalid_geometry\":" << stats.houses_skipped_invalid_geometry << ','
        << "\"unnamed_streets\":" << stats.unnamed_streets << ','
        << "\"regions_skipped_complex_relations\":" << stats.regions_skipped_complex_relations << ','
        << "\"parse_seconds\":" << stats.parse_seconds << ','
        << "\"estimated_memory_bytes\":" << stats.estimated_memory_bytes << ','
        << "\"houses_with_assigned_city\":" << stats.houses_with_assigned_city << ','
        << "\"houses_with_assigned_state\":" << stats.houses_with_assigned_state << ','
        << "\"houses_with_assigned_postcode\":" << stats.houses_with_assigned_postcode << ','
        << "\"houses_with_assigned_country\":" << stats.houses_with_assigned_country << ','
        << "\"country_assigned_by_pip\":" << stats.country_assigned_by_pip << ','
        << "\"country_assigned_by_fallback\":" << stats.country_assigned_by_fallback << ','
        << "\"region_assignment_seconds\":" << stats.region_assignment_seconds << ','
        << "\"reverse_index_build_seconds\":" << stats.reverse_index_build_seconds << ','
        << "\"spatial_index_cells\":" << stats.spatial_index_cells << ','
        << "\"avg_pip_candidates_per_house\":" << stats.avg_pip_candidates_per_house
        << '}';
    return out.str();
}

std::string serialize_houses_json(const DataStore& data, const std::vector<std::size_t>& indices) {
    std::ostringstream out;
    out << "{\"houses\":[";

    bool first = true;
    for (const auto idx : indices) {
        if (idx >= data.houses.size()) {
            continue;
        }

        if (!first) {
            out << ',';
        }
        first = false;

        const auto& h = data.houses[idx];
        const auto street = escape_json(resolve_string_or_empty(data.strings, h.street_name_id));
        const auto house_no = escape_json(resolve_string_or_empty(data.strings, h.house_number_id));
        const auto city = escape_json(resolve_string_or_empty(data.strings, h.city_id));
        const auto state = escape_json(resolve_string_or_empty(data.strings, h.state_id));
        const auto country = escape_json(resolve_string_or_empty(data.strings, h.country_id));
        const auto postcode = escape_json(resolve_string_or_empty(data.strings, h.postcode_id));

        out << '{'
            << "\"lat\":" << h.lat << ','
            << "\"lon\":" << h.lon << ','
            << "\"street\":\"" << street << "\"," 
            << "\"house_number\":\"" << house_no << "\"," 
            << "\"city\":\"" << city << "\"," 
            << "\"state\":\"" << state << "\"," 
            << "\"postcode\":\"" << postcode << "\""
            << ",\"country\":\"" << country << "\""
            << '}';
    }

    out << "]}";
    return out.str();
}

std::string serialize_streets_json(const DataStore& data, const std::vector<std::size_t>& indices) {
    std::ostringstream out;
    out << "{\"streets\":[";

    bool first_street = true;
    for (const auto idx : indices) {
        if (idx >= data.streets.size()) {
            continue;
        }

        if (!first_street) {
            out << ',';
        }
        first_street = false;

        const auto& s = data.streets[idx];
        const auto name = escape_json(resolve_string_or_empty(data.strings, s.name_id));
        const auto highway = escape_json(resolve_string_or_empty(data.strings, s.highway_class_id));

        out << '{'
            << "\"name\":\"" << name << "\"," 
            << "\"highway\":\"" << highway << "\"," 
            << "\"is_unnamed\":" << (s.is_unnamed ? "true" : "false") << ','
            << "\"points\":[";

        bool first_point = true;
        for (std::uint32_t j = 0; j < s.points_count; ++j) {
            const auto point_idx = static_cast<std::size_t>(s.points_begin + j);
            if (point_idx >= data.street_points.size()) {
                break;
            }

            if (!first_point) {
                out << ',';
            }
            first_point = false;

            const auto& p = data.street_points[point_idx];
            out << "[" << p.lat << ',' << p.lon << "]";
        }

        out << "]}";
    }

    out << "]}";
    return out.str();
}

std::string serialize_regions_json(const DataStore& data, const std::vector<std::size_t>& indices) {
    std::ostringstream out;
    out << "{\"regions\":[";

    bool first_region = true;
    for (const auto idx : indices) {
        if (idx >= data.regions.size()) {
            continue;
        }

        if (!first_region) {
            out << ',';
        }
        first_region = false;

        const auto& r = data.regions[idx];
        const auto name = escape_json(resolve_string_or_empty(data.strings, r.name_id));

        out << '{'
            << "\"name\":\"" << name << "\","
            << "\"admin_level\":" << r.admin_level << ','
            << "\"points\":[";

        bool first_point = true;
        for (std::uint32_t j = 0; j < r.points_count; ++j) {
            const auto point_idx = static_cast<std::size_t>(r.points_begin + j);
            if (point_idx >= data.region_points.size()) {
                break;
            }

            if (!first_point) {
                out << ',';
            }
            first_point = false;

            const auto& p = data.region_points[point_idx];
            out << "[" << p.lat << ',' << p.lon << "]";
        }

        out << "]}";
    }

    out << "]}";
    return out.str();
}

std::string serialize_geocode_json(const DataStore& data, const search::GeocodeQueryResult& result) {
    std::ostringstream out;
    out << '{'
        << "\"query\":\"" << escape_json(result.input) << "\","
        << "\"normalized_query\":\"" << escape_json(result.normalized_query) << "\","
        << "\"timing\":{"
        << "\"normalization_ms\":" << result.timings.normalization_ms << ','
        << "\"interpretation_ms\":" << result.timings.interpretation_ms << ','
        << "\"candidate_lookup_ms\":" << result.timings.candidate_lookup_ms << ','
        << "\"region_matching_ms\":" << result.timings.region_matching_ms << ','
        << "\"ranking_ms\":" << result.timings.ranking_ms << ','
        << "\"total_ms\":" << result.timings.total_ms
        << "},"
        << "\"interpretations\":[";

    bool first_interpretation = true;
    for (const auto& interpretation : result.interpretations) {
        if (!first_interpretation) out << ',';
        first_interpretation = false;

        out << '{'
            << "\"intent\":\"" << search::queryIntentName(interpretation.intent) << "\","
            << "\"locality\":\"" << escape_json(first_locality_name(data, interpretation.locality_indices)) << "\","
            << "\"locality_span\":\"" << escape_json(join_tokens(interpretation.tokens, interpretation.locality_token_begin, interpretation.locality_token_end)) << "\","
            << "\"locality_candidates\":" << interpretation.locality_indices.size() << ','
            << "\"entity\":\"" << escape_json(interpretation.entity_name) << "\",";
        if (interpretation.intent == search::QueryIntent::Address) {
            out << "\"house_number\":\"" << escape_json(interpretation.normalized_house_number) << "\","
                << "\"address_key_found\":" << (interpretation.exact_address_key_match ? "true" : "false") << ',';
        } else {
            out << "\"exact_name_match\":" << (interpretation.exact_entity_name_match ? "true" : "false") << ',';
        }
        out << "\"unexplained_tokens\":" << interpretation.unexplained_token_count << ','
            << "\"raw_candidates\":" << interpretation.raw_candidate_count
            << '}';
    }

    out << "],\"results\":[";

    bool first_result = true;
    for (const auto& candidate : result.ranked_candidates) {
        if (!first_result) out << ',';
        first_result = false;

        const auto point = geocode_object_point(data, candidate.ref);
        out << '{'
            << "\"type\":\"" << search_object_type_json(candidate.ref.type) << "\","
            << "\"name\":\"" << escape_json(geocode_object_name(data, candidate.ref)) << "\","
            << "\"city\":\"" << escape_json(candidate_city(data, candidate.ref)) << "\","
            << "\"postcode\":\"" << escape_json(candidate_postcode(data, candidate.ref)) << "\","
            << "\"lat\":" << point.lat << ','
            << "\"lon\":" << point.lon << ','
            << "\"exact_address\":" << (candidate.exact_address_match ? "true" : "false") << ','
            << "\"exact_name\":" << (candidate.exact_name_match ? "true" : "false") << ','
            << "\"locality_recognized\":" << (candidate.locality_recognized ? "true" : "false") << ','
            << "\"shared_relation\":\"" << escape_json(resolve_string_or_empty(data.strings, candidate.shared_relation_name_id)) << "\","
            << "\"shared_relation_id\":" << candidate.shared_relation_id << ','
            << "\"shared_admin_level\":" << candidate.shared_admin_level << ','
            << "\"distance_to_locality_m\":";
        if (std::isfinite(candidate.distance_to_locality_m)) {
            out << candidate.distance_to_locality_m;
        } else {
            out << "null";
        }
        out << ",\"rank_reason\":\"" << escape_json(rank_reason(candidate)) << "\""
            << '}';
    }

    out << "]}";
    return out.str();
}

std::string serialize_error_json(std::string_view message) {
    return std::string{"{\"error\":\""} + escape_json(message) + "\"}";
}

std::string serialize_reverse_json(
    const DataStore& data,
    const std::size_t house_index,
    const double query_lat,
    const double query_lon,
    const double result_lat,
    const double result_lon,
    const double distance_m,
    const std::string_view street,
    const std::string_view house_number,
    const std::string_view city,
    const std::string_view state,
    const std::string_view postcode,
    const std::string_view country) {
    std::ostringstream out;
    out << '{'
        << "\"query\":{\"lat\":" << query_lat << ",\"lon\":" << query_lon << "},"
        << "\"nearest\":{"
        << "\"type\":\"house\"," 
        << "\"lat\":" << result_lat << ','
        << "\"lon\":" << result_lon << ','
        << "\"distance_m\":" << distance_m << ','
        << "\"street\":\"" << escape_json(street) << "\"," 
        << "\"house_number\":\"" << escape_json(house_number) << "\"," 
        << "\"city\":\"" << escape_json(city) << "\"," 
        << "\"state\":\"" << escape_json(state) << "\"," 
        << "\"postcode\":\"" << escape_json(postcode) << "\"," 
        << "\"country\":\"" << escape_json(country) << "\""
        << "},\"containing_regions\":";
    serialize_house_containing_regions(out, data, house_index);
    out << '}';
    return out.str();
}

} // namespace osm

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

const char* poi_category_json(const PoiCategory category) {
    switch (category) {
        case PoiCategory::Shop: return "shop";
        case PoiCategory::Restaurant: return "restaurant";
        case PoiCategory::Cafe: return "cafe";
        case PoiCategory::FastFood: return "fast_food";
        case PoiCategory::Park: return "park";
        case PoiCategory::Hotel: return "hotel";
        case PoiCategory::School: return "school";
        case PoiCategory::Hospital: return "hospital";
        case PoiCategory::Station: return "station";
        case PoiCategory::Other: return "other";
    }
    return "other";
}

const char* osm_element_type_json(const OsmElementType type) {
    switch (type) {
        case OsmElementType::Node: return "node";
        case OsmElementType::Way: return "way";
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

void serialize_optional_bbox(std::ostringstream& out, const std::optional<BBox>& bbox) {
    if (!bbox.has_value()) {
        out << "null";
        return;
    }
    out << '{'
        << "\"min_lon\":" << bbox->min_lon << ','
        << "\"min_lat\":" << bbox->min_lat << ','
        << "\"max_lon\":" << bbox->max_lon << ','
        << "\"max_lat\":" << bbox->max_lat
        << '}';
}

std::string rank_reason(const search::GeocodeCandidate& candidate) {
    std::string reason;
    if (candidate.match_strategy == search::QueryMatchStrategy::Fuzzy) reason += "typo corrected";
    if (candidate.match_strategy == search::QueryMatchStrategy::Partial) reason += "partial match";
    if (candidate.exact_address_match) {
        if (!reason.empty()) reason += " + ";
        reason += "exact address";
    }
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

void serialize_street_containing_regions(
    std::ostringstream& out,
    const DataStore& data,
    const std::size_t street_index) {
    out << "[";
    if (street_index >= data.streets.size()) {
        out << "]";
        return;
    }

    const auto& street = data.streets[street_index];
    const auto begin = static_cast<std::size_t>(street.containing_regions_begin);
    const auto count = static_cast<std::size_t>(street.containing_regions_count);
    if (begin + count > data.street_containing_region_ids.size()) {
        out << "]";
        return;
    }

    bool first = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto region_index = static_cast<std::size_t>(data.street_containing_region_ids[begin + i]);
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

void serialize_region_containing_regions(
    std::ostringstream& out,
    const DataStore& data,
    const std::size_t region_index) {
    out << "[";
    if (region_index >= data.regions.size()) {
        out << "]";
        return;
    }

    const auto& region = data.regions[region_index];
    const auto begin = static_cast<std::size_t>(region.containing_regions_begin);
    const auto count = static_cast<std::size_t>(region.containing_regions_count);
    if (begin + count > data.region_containing_region_ids.size()) {
        out << "]";
        return;
    }

    bool first = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto parent_index = static_cast<std::size_t>(data.region_containing_region_ids[begin + i]);
        if (parent_index >= data.regions.size()) {
            continue;
        }
        const auto& parent = data.regions[parent_index];
        if (!first) {
            out << ',';
        }
        first = false;
        out << '{'
            << "\"name\":\"" << escape_json(resolve_string_or_empty(data.strings, parent.name_id)) << "\","
            << "\"admin_level\":" << parent.admin_level << ','
            << "\"relation_id\":" << parent.source_relation_id
            << '}';
    }
    out << "]";
}

void serialize_region_object(
    std::ostringstream& out,
    const DataStore& data,
    const std::size_t region_index,
    const double query_lat,
    const double query_lon) {
    if (region_index >= data.regions.size()) {
        out << "null";
        return;
    }

    const auto& region = data.regions[region_index];
    out << '{'
        << "\"type\":\"region\","
        << "\"name\":\"" << escape_json(resolve_string_or_empty(data.strings, region.name_id)) << "\","
        << "\"admin_level\":" << region.admin_level << ','
        << "\"relation_id\":" << region.source_relation_id << ','
        << "\"lat\":" << query_lat << ','
        << "\"lon\":" << query_lon << ','
        << "\"distance_m\":0,"
        << "\"containing_regions\":";
    serialize_region_containing_regions(out, data, region_index);
    out << '}';
}

void serialize_street_object(
    std::ostringstream& out,
    const DataStore& data,
    const std::size_t street_index,
    const double result_lat,
    const double result_lon,
    const double distance_m) {
    if (street_index >= data.streets.size()) {
        out << "null";
        return;
    }

    const auto& street = data.streets[street_index];
    out << '{'
        << "\"type\":\"street\","
        << "\"name\":\"" << escape_json(resolve_string_or_empty(data.strings, street.name_id)) << "\","
        << "\"highway\":\"" << escape_json(resolve_string_or_empty(data.strings, street.highway_class_id)) << "\","
        << "\"is_unnamed\":" << (street.is_unnamed ? "true" : "false") << ','
        << "\"lat\":" << result_lat << ','
        << "\"lon\":" << result_lon << ','
        << "\"distance_m\":" << distance_m << ','
        << "\"containing_regions\":";
    serialize_street_containing_regions(out, data, street_index);
    out << '}';
}

void serialize_region_indices(
    std::ostringstream& out,
    const DataStore& data,
    const std::vector<std::size_t>& region_indices) {
    out << "[";

    bool first = true;
    for (const auto region_index : region_indices) {
        if (region_index >= data.regions.size()) {
            continue;
        }

        const auto& region = data.regions[region_index];
        if (region.name_id == kInvalidStringId || region.name_id >= data.strings.size()) {
            continue;
        }

        const auto name = resolve_string_or_empty(data.strings, region.name_id);
        if (name.empty()) {
            continue;
        }

        if (!first) {
            out << ',';
        }
        first = false;
        out << '{'
            << "\"name\":\"" << escape_json(name) << "\","
            << "\"admin_level\":" << region.admin_level << ','
            << "\"relation_id\":" << region.source_relation_id
            << '}';
    }

    out << "]";
}

void serialize_poi_containing_regions(
    std::ostringstream& out,
    const DataStore& data,
    const std::size_t poi_index) {
    out << "[";
    if (poi_index >= data.pois.size()) {
        out << "]";
        return;
    }

    const auto& poi = data.pois[poi_index];
    const auto begin = static_cast<std::size_t>(poi.containing_regions_begin);
    const auto count = static_cast<std::size_t>(poi.containing_regions_count);
    if (begin + count > data.poi_containing_region_ids.size()) {
        out << "]";
        return;
    }

    bool first = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto region_index = static_cast<std::size_t>(data.poi_containing_region_ids[begin + i]);
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

void serialize_nearest_poi(
    std::ostringstream& out,
    const DataStore& data,
    const std::optional<std::size_t> nearest_poi_index,
    const double nearest_poi_distance_m) {
    if (!nearest_poi_index.has_value() || *nearest_poi_index >= data.pois.size()) {
        out << "null";
        return;
    }

    const auto& poi = data.pois[*nearest_poi_index];
    out << '{'
        << "\"type\":\"poi\","
        << "\"name\":\"" << escape_json(resolve_string_or_empty(data.strings, poi.name_id)) << "\","
        << "\"category\":\"" << poi_category_json(poi.category) << "\","
        << "\"subtype\":\"" << escape_json(resolve_string_or_empty(data.strings, poi.subtype_id)) << "\","
        << "\"osm_type\":\"" << osm_element_type_json(poi.osm_type) << "\","
        << "\"osm_id\":" << poi.osm_id << ','
        << "\"lat\":" << poi.lat << ','
        << "\"lon\":" << poi.lon << ','
        << "\"distance_m\":" << nearest_poi_distance_m << ','
        << "\"containing_regions\":";
    serialize_poi_containing_regions(out, data, *nearest_poi_index);
    out << '}';
}

void serialize_bbox_metadata(
    std::ostringstream& out,
    const std::size_t matched_count,
    const std::size_t returned_count) {
    out << "\"matched\":" << matched_count << ','
        << "\"returned\":" << returned_count << ','
        << "\"limited\":" << (returned_count < matched_count ? "true" : "false") << ',';
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

std::string serialize_houses_json(
    const DataStore& data,
    const std::vector<std::size_t>& indices,
    const std::size_t matched_count,
    const bool include_metadata) {
    std::ostringstream out;
    out << "{";
    if (include_metadata) {
        serialize_bbox_metadata(out, matched_count, indices.size());
    }
    out << "\"houses\":[";

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

std::string serialize_streets_json(
    const DataStore& data,
    const std::vector<std::size_t>& indices,
    const std::size_t matched_count,
    const bool include_metadata) {
    std::ostringstream out;
    out << "{";
    if (include_metadata) {
        serialize_bbox_metadata(out, matched_count, indices.size());
    }
    out << "\"streets\":[";

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
            << "\"containing_regions\":";
        serialize_street_containing_regions(out, data, idx);
        out << ",\"points\":[";

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

std::string serialize_regions_json(
    const DataStore& data,
    const std::vector<std::size_t>& indices,
    const std::size_t matched_count,
    const bool include_metadata) {
    std::ostringstream out;
    out << "{";
    if (include_metadata) {
        serialize_bbox_metadata(out, matched_count, indices.size());
    }
    out << "\"regions\":[";

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
            << "\"containing_regions\":";
        serialize_region_containing_regions(out, data, idx);
        out << ','
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
        << "\"nearest_category_intent\":" << (result.nearest_category_intent ? "true" : "false") << ','
        << "\"nearest_category\":";
    if (result.nearest_category.has_value()) out << '"' << poi_category_json(*result.nearest_category) << '"';
    else out << "null";
    out << ",\"viewport\":";
    serialize_optional_bbox(out, result.viewport);
    out << ",\"result_bounds\":";
    serialize_optional_bbox(out, result.result_bounds);
    out << ','
        << "\"reference_resolved\":" << (result.reference_resolved ? "true" : "false") << ','
        << "\"reference_query\":\"" << escape_json(result.reference_query) << "\","
        << "\"reference_label\":\"" << escape_json(result.reference_label) << "\","
        << "\"reference_lat\":" << result.reference_lat << ','
        << "\"reference_lon\":" << result.reference_lon << ','
        << "\"failure_reason\":\"" << escape_json(result.failure_reason) << "\","
        << "\"spatial_cells_examined\":" << result.spatial_cells_examined << ','
        << "\"spatial_pois_tested\":" << result.spatial_pois_tested << ','
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
            << "\"match_strategy\":\"" << search::queryMatchStrategyName(interpretation.match_strategy) << "\","
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
            << "\"category\":";
        if (candidate.ref.type == SearchObjectType::Poi && candidate.ref.index < data.pois.size()) {
            out << '"' << poi_category_json(data.pois[candidate.ref.index].category) << '"';
        } else {
            out << "null";
        }
        out << ','
            << "\"lat\":" << point.lat << ','
            << "\"lon\":" << point.lon << ','
            << "\"exact_address\":" << (candidate.exact_address_match ? "true" : "false") << ','
            << "\"exact_name\":" << (candidate.exact_name_match ? "true" : "false") << ','
            << "\"match_strategy\":\"" << search::queryMatchStrategyName(candidate.match_strategy) << "\","
            << "\"locality_recognized\":" << (candidate.locality_recognized ? "true" : "false") << ','
            << "\"in_viewport\":" << (candidate.in_viewport ? "true" : "false") << ','
            << "\"distance_to_viewport_center_m\":";
        if (std::isfinite(candidate.distance_to_viewport_center_m)) out << candidate.distance_to_viewport_center_m;
        else out << "null";
        out << ",\"nearest_distance_m\":";
        if (std::isfinite(candidate.nearest_distance_m)) out << candidate.nearest_distance_m;
        else out << "null";
        out << ','
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

    out << "],\"clusters\":[";
    bool first_cluster = true;
    for (const auto& cluster : result.clusters) {
        if (cluster.representative_candidate_index >= result.ranked_candidates.size()) continue;
        if (!first_cluster) out << ',';
        first_cluster = false;
        out << '{'
            << "\"representative_index\":" << cluster.representative_candidate_index << ','
            << "\"lat\":" << cluster.lat << ','
            << "\"lon\":" << cluster.lon << ','
            << "\"member_count\":" << cluster.member_candidate_indices.size() << ','
            << "\"member_indices\":[";
        for (std::size_t i = 0; i < cluster.member_candidate_indices.size(); ++i) {
            if (i > 0) out << ',';
            out << cluster.member_candidate_indices[i];
        }
        out << "]}";
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
    const std::vector<std::size_t>& clicked_region_indices,
    const std::optional<std::size_t> nearest_poi_index,
    const double nearest_poi_distance_m,
    const std::optional<std::size_t> nearest_street_index,
    const double nearest_street_lat,
    const double nearest_street_lon,
    const double nearest_street_distance_m,
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
    out << ",\"clicked_containing_regions\":";
    serialize_region_indices(out, data, clicked_region_indices);
    out << ",\"nearest_poi\":";
    serialize_nearest_poi(out, data, nearest_poi_index, nearest_poi_distance_m);
    out << ",\"nearest_street\":";
    if (nearest_street_index.has_value()) {
        serialize_street_object(out, data, *nearest_street_index, nearest_street_lat, nearest_street_lon, nearest_street_distance_m);
    } else {
        out << "null";
    }
    out << '}';
    return out.str();
}

std::string serialize_reverse_street_json(
    const DataStore& data,
    const std::size_t street_index,
    const std::vector<std::size_t>& clicked_region_indices,
    const std::optional<std::size_t> nearest_poi_index,
    const double nearest_poi_distance_m,
    const double query_lat,
    const double query_lon,
    const double result_lat,
    const double result_lon,
    const double distance_m) {
    std::ostringstream out;
    out << '{'
        << "\"query\":{\"lat\":" << query_lat << ",\"lon\":" << query_lon << "},"
        << "\"nearest\":";
    serialize_street_object(out, data, street_index, result_lat, result_lon, distance_m);
    out << ",\"containing_regions\":";
    serialize_street_containing_regions(out, data, street_index);
    out << ",\"clicked_containing_regions\":";
    serialize_region_indices(out, data, clicked_region_indices);
    out << ",\"nearest_poi\":";
    serialize_nearest_poi(out, data, nearest_poi_index, nearest_poi_distance_m);
    out << ",\"nearest_street\":";
    serialize_street_object(out, data, street_index, result_lat, result_lon, distance_m);
    out << '}';
    return out.str();
}

std::string serialize_reverse_region_json(
    const DataStore& data,
    const std::size_t region_index,
    const std::vector<std::size_t>& clicked_region_indices,
    const std::optional<std::size_t> nearest_poi_index,
    const double nearest_poi_distance_m,
    const double query_lat,
    const double query_lon) {
    std::ostringstream out;
    out << '{'
        << "\"query\":{\"lat\":" << query_lat << ",\"lon\":" << query_lon << "},"
        << "\"nearest\":";
    serialize_region_object(out, data, region_index, query_lat, query_lon);
    out << ",\"containing_regions\":";
    serialize_region_containing_regions(out, data, region_index);
    out << ",\"clicked_containing_regions\":";
    serialize_region_indices(out, data, clicked_region_indices);
    out << ",\"nearest_poi\":";
    serialize_nearest_poi(out, data, nearest_poi_index, nearest_poi_distance_m);
    out << ",\"nearest_street\":null";
    out << '}';
    return out.str();
}

} // namespace osm

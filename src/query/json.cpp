#include "query/json.hpp"

#include <array>
#include <charconv>
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
        << "\"houses_from_address_nodes\":" << stats.houses_from_address_nodes << ','
        << "\"houses_from_polygon_centroid\":" << stats.houses_from_polygon_centroid << ','
        << "\"houses_from_polygon_bbox_fallback\":" << stats.houses_from_polygon_bbox_fallback << ','
        << "\"houses_skipped_invalid_geometry\":" << stats.houses_skipped_invalid_geometry << ','
        << "\"unnamed_streets\":" << stats.unnamed_streets << ','
        << "\"regions_skipped_complex_relations\":" << stats.regions_skipped_complex_relations << ','
        << "\"parse_seconds\":" << stats.parse_seconds << ','
        << "\"estimated_memory_bytes\":" << stats.estimated_memory_bytes
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
        const auto postcode = escape_json(resolve_string_or_empty(data.strings, h.postcode_id));

        out << '{'
            << "\"lat\":" << h.lat << ','
            << "\"lon\":" << h.lon << ','
            << "\"street\":\"" << street << "\"," 
            << "\"house_number\":\"" << house_no << "\"," 
            << "\"city\":\"" << city << "\"," 
            << "\"postcode\":\"" << postcode << "\""
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

std::string serialize_error_json(std::string_view message) {
    return std::string{"{\"error\":\""} + escape_json(message) + "\"}";
}

} // namespace osm

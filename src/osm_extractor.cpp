#include "osm_extractor.hpp"

#include <osmium/handler.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/index/map/sparse_file_array.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/visitor.hpp>

#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace sheet1 {
namespace {

[[nodiscard]] std::string_view tag(const osmium::TagList& tags, const char* key) {
    const char* value = tags.get_value_by_key(key);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

[[nodiscard]] std::string_view selected_name(const osmium::TagList& tags) {
    const auto german = tag(tags, "name:de");
    return german.empty() ? tag(tags, "name") : german;
}

[[nodiscard]] bool has_address(const osmium::TagList& tags) {
    return tags.has_key("addr:housenumber") || tags.has_key("addr:street") ||
           tags.has_key("addr:place") || tags.has_key("addr:postcode") ||
           tags.has_key("addr:city");
}

[[nodiscard]] Coordinate coordinate_from_degrees(const double latitude,
                                                 const double longitude) {
    constexpr double scale = 10'000'000.0;
    return Coordinate{
        .latitude_e7 = static_cast<std::int32_t>(std::llround(latitude * scale)),
        .longitude_e7 = static_cast<std::int32_t>(std::llround(longitude * scale)),
    };
}

[[nodiscard]] double latitude(const Coordinate coordinate) {
    return static_cast<double>(coordinate.latitude_e7) / 10'000'000.0;
}

[[nodiscard]] double longitude(const Coordinate coordinate) {
    return static_cast<double>(coordinate.longitude_e7) / 10'000'000.0;
}

[[nodiscard]] BoundingBox bounds_for(const std::vector<Coordinate>& points) {
    BoundingBox bounds{.minimum = points.front(), .maximum = points.front()};
    for (const auto point : points) {
        bounds.minimum.latitude_e7 = std::min(bounds.minimum.latitude_e7, point.latitude_e7);
        bounds.minimum.longitude_e7 = std::min(bounds.minimum.longitude_e7, point.longitude_e7);
        bounds.maximum.latitude_e7 = std::max(bounds.maximum.latitude_e7, point.latitude_e7);
        bounds.maximum.longitude_e7 = std::max(bounds.maximum.longitude_e7, point.longitude_e7);
    }
    return bounds;
}

[[nodiscard]] Coordinate bounds_center(const BoundingBox bounds) {
    const auto latitude_sum = static_cast<std::int64_t>(bounds.minimum.latitude_e7) +
                              static_cast<std::int64_t>(bounds.maximum.latitude_e7);
    const auto longitude_sum = static_cast<std::int64_t>(bounds.minimum.longitude_e7) +
                               static_cast<std::int64_t>(bounds.maximum.longitude_e7);
    return Coordinate{
        .latitude_e7 = static_cast<std::int32_t>(latitude_sum / 2),
        .longitude_e7 = static_cast<std::int32_t>(longitude_sum / 2),
    };
}

[[nodiscard]] Coordinate representative_point(const std::vector<Coordinate>& points) {
    const auto bounds = bounds_for(points);
    if (points.size() < 4 || points.front().latitude_e7 != points.back().latitude_e7 ||
        points.front().longitude_e7 != points.back().longitude_e7) {
        return bounds_center(bounds);
    }

    double twice_area = 0.0;
    double longitude_sum = 0.0;
    double latitude_sum = 0.0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const double x0 = longitude(points[i]);
        const double y0 = latitude(points[i]);
        const double x1 = longitude(points[i + 1]);
        const double y1 = latitude(points[i + 1]);
        const double cross = x0 * y1 - x1 * y0;
        twice_area += cross;
        longitude_sum += (x0 + x1) * cross;
        latitude_sum += (y0 + y1) * cross;
    }

    if (std::abs(twice_area) < 1e-12) {
        return bounds_center(bounds);
    }

    return coordinate_from_degrees(latitude_sum / (3.0 * twice_area),
                                   longitude_sum / (3.0 * twice_area));
}

[[nodiscard]] std::int16_t admin_level(const osmium::TagList& tags) {
    const auto raw = tag(tags, "admin_level");
    if (raw.empty()) {
        return -1;
    }
    try {
        const auto parsed = std::stoi(std::string{raw});
        if (parsed < std::numeric_limits<std::int16_t>::min() ||
            parsed > std::numeric_limits<std::int16_t>::max()) {
            return -1;
        }
        return static_cast<std::int16_t>(parsed);
    } catch (...) {
        return -1;
    }
}

[[nodiscard]] double peak_memory_mib() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
}

[[nodiscard]] double process_cpu_seconds() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
    const auto seconds = static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec);
    const auto microseconds = static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
    return seconds + microseconds / 1'000'000.0;
}

class ExtractionHandler final : public osmium::handler::Handler {
public:
    ExtractionHandler(DataStore& data, ExtractionStats& stats) : data_(data), stats_(stats) {}

    void node(const osmium::Node& node) {
        ++stats_.processed_nodes;
        const auto& tags = node.tags();
        if ((!tags.has_key("building") && !has_address(tags)) || !node.location().valid()) {
            return;
        }

        Building building;
        building.id = OsmId{OsmObjectType::node, node.id()};
        building.point = coordinate_from_degrees(node.location().lat_without_check(),
                                                 node.location().lon_without_check());
        building.has_point = true;
        fill_building_tags(building, tags);
        add_building(std::move(building));
    }

    void way(const osmium::Way& way) {
        ++stats_.processed_ways;
        const auto& tags = way.tags();
        const bool is_building = tags.has_key("building");
        const bool is_street = tags.has_key("highway");
        const bool is_administrative = tags.has_tag("boundary", "administrative");
        if (!is_building && !is_street && !is_administrative) {
            return;
        }

        std::vector<Coordinate> points;
        points.reserve(way.nodes().size());
        for (const auto& node_ref : way.nodes()) {
            if (!node_ref.location().valid()) {
                ++stats_.invalid_way_node_locations;
                continue;
            }
            points.push_back(coordinate_from_degrees(node_ref.location().lat_without_check(),
                                                     node_ref.location().lon_without_check()));
        }

        if (is_building) {
            Building building;
            building.id = OsmId{OsmObjectType::way, way.id()};
            if (!points.empty()) {
                building.point = representative_point(points);
                building.has_point = true;
            }
            fill_building_tags(building, tags);
            add_building(std::move(building));
        }

        if (is_street && points.size() >= 2) {
            if (data_.street_points.size() + points.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                throw std::runtime_error{"Street geometry exceeds 32-bit offsets"};
            }
            Street street;
            street.id = OsmId{OsmObjectType::way, way.id()};
            street.name = data_.strings.intern(selected_name(tags));
            street.highway_class = data_.strings.intern(tag(tags, "highway"));
            street.bounds = bounds_for(points);
            street.points_begin = static_cast<std::uint32_t>(data_.street_points.size());
            street.points_count = static_cast<std::uint32_t>(points.size());
            data_.street_points.insert(data_.street_points.end(), points.begin(), points.end());
            data_.streets.push_back(street);
            ++stats_.extracted_streets;
        }

        if (is_administrative && points.size() >= 3) {
            if (data_.administrative_points.size() + points.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                throw std::runtime_error{"Administrative geometry exceeds 32-bit offsets"};
            }
            AdministrativeArea area;
            area.id = OsmId{OsmObjectType::way, way.id()};
            area.name = data_.strings.intern(selected_name(tags));
            area.admin_level = admin_level(tags);
            area.bounds = bounds_for(points);
            area.points_begin = static_cast<std::uint32_t>(data_.administrative_points.size());
            area.points_count = static_cast<std::uint32_t>(points.size());
            area.has_geometry = true;
            data_.administrative_points.insert(data_.administrative_points.end(),
                                               points.begin(),
                                               points.end());
            data_.administrative_areas.push_back(area);
            ++stats_.extracted_administrative_areas;
        }
    }

    void relation(const osmium::Relation& relation) {
        ++stats_.processed_relations;
        const auto& tags = relation.tags();

        if (tags.has_key("building")) {
            Building building;
            building.id = OsmId{OsmObjectType::relation, relation.id()};
            fill_building_tags(building, tags);
            add_building(std::move(building));
        }

        if (tags.has_tag("boundary", "administrative")) {
            AdministrativeArea area;
            area.id = OsmId{OsmObjectType::relation, relation.id()};
            area.name = data_.strings.intern(selected_name(tags));
            area.admin_level = admin_level(tags);
            data_.administrative_areas.push_back(area);
            ++stats_.extracted_administrative_areas;
        }
    }

private:
    void fill_building_tags(Building& building, const osmium::TagList& tags) {
        building.name = data_.strings.intern(selected_name(tags));
        building.building_type = data_.strings.intern(tag(tags, "building"));
        building.street = data_.strings.intern(tag(tags, "addr:street"));
        building.house_number = data_.strings.intern(tag(tags, "addr:housenumber"));
        building.postcode = data_.strings.intern(tag(tags, "addr:postcode"));
        building.city = data_.strings.intern(tag(tags, "addr:city"));
    }

    void add_building(Building building) {
        if (building.name == kInvalidStringId) {
            ++stats_.buildings_without_names;
        }
        if (!building.has_point) {
            ++stats_.buildings_without_points;
        }
        data_.buildings.push_back(std::move(building));
        ++stats_.extracted_buildings;
    }

    DataStore& data_;
    ExtractionStats& stats_;
};

} // namespace

ExtractionResult extract_pbf(const std::filesystem::path& input_path) {
    if (!std::filesystem::is_regular_file(input_path)) {
        throw std::runtime_error{"Input is not a regular file: " + input_path.string()};
    }

    ExtractionResult result;
    using NodeIndex = osmium::index::map::SparseFileArray<
        osmium::unsigned_object_id_type,
        osmium::Location>;

    NodeIndex node_index;
    osmium::handler::NodeLocationsForWays<NodeIndex> location_handler{node_index};
    location_handler.ignore_errors();
    ExtractionHandler extraction_handler{result.data, result.stats};

    const auto started = std::chrono::steady_clock::now();
    osmium::io::Reader reader{input_path.string(), osmium::osm_entity_bits::all};
    osmium::apply(reader, location_handler, extraction_handler);
    reader.close();

    result.stats.elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    result.stats.cpu_seconds = process_cpu_seconds();
    result.stats.peak_memory_mib = peak_memory_mib();
    result.stats.temporary_node_index_bytes = node_index.used_memory();
    result.stats.stored_data_bytes = result.data.estimated_bytes();
    return result;
}

void print_extraction_report(const std::filesystem::path& input_path,
                             const ExtractionResult& result) {
    constexpr double bytes_per_mib = 1024.0 * 1024.0;
    const auto& stats = result.stats;
    std::cout << "\nOSM extraction report\n"
              << "Input: " << input_path << '\n'
              << "Processed nodes: " << stats.processed_nodes << '\n'
              << "Processed ways: " << stats.processed_ways << '\n'
              << "Processed relations: " << stats.processed_relations << '\n'
              << "Extracted buildings/address points: " << stats.extracted_buildings << '\n'
              << "Extracted streets: " << stats.extracted_streets << '\n'
              << "Extracted administrative areas: " << stats.extracted_administrative_areas << '\n'
              << "Buildings without names: " << stats.buildings_without_names << '\n'
              << "Buildings without representative points: " << stats.buildings_without_points << '\n'
              << "Invalid way-node locations: " << stats.invalid_way_node_locations << '\n'
              << std::fixed << std::setprecision(3)
              << "Extraction wall seconds: " << stats.elapsed_seconds << '\n'
              << "Extraction CPU seconds: " << stats.cpu_seconds << '\n'
              << "Peak process memory MiB: " << stats.peak_memory_mib << '\n'
              << "Temporary node index MiB: "
              << static_cast<double>(stats.temporary_node_index_bytes) / bytes_per_mib << '\n'
              << "Estimated stored data MiB: "
              << static_cast<double>(stats.stored_data_bytes) / bytes_per_mib << '\n';
}

} // namespace sheet1

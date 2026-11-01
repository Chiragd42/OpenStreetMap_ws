#include "osm_extractor.hpp"

#include <osmium/area/assembler.hpp>
#include <osmium/area/multipolygon_manager.hpp>
#include <osmium/handler.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/index/map/sparse_file_array.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/io/file.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/area.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/relations/manager_util.hpp>
#include <osmium/tags/tags_filter.hpp>
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
#include <unordered_map>
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

void extend_bounds(BoundingBox& bounds, const Coordinate point) {
    bounds.minimum.latitude_e7 = std::min(bounds.minimum.latitude_e7, point.latitude_e7);
    bounds.minimum.longitude_e7 = std::min(bounds.minimum.longitude_e7, point.longitude_e7);
    bounds.maximum.latitude_e7 = std::max(bounds.maximum.latitude_e7, point.latitude_e7);
    bounds.maximum.longitude_e7 = std::max(bounds.maximum.longitude_e7, point.longitude_e7);
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

class AdministrativeAreaStore {
public:
    AdministrativeAreaStore(DataStore& data, ExtractionStats& stats)
        : data_(data), stats_(stats) {}

    void add_relation_metadata(const osmium::Relation& relation) {
        const auto existing = relation_indices_.find(relation.id());
        if (existing != relation_indices_.end()) {
            auto& area = data_.administrative_areas[existing->second];
            area.name = data_.strings.intern(selected_name(relation.tags()));
            area.admin_level = admin_level(relation.tags());
            return;
        }

        AdministrativeArea area;
        area.id = OsmId{OsmObjectType::relation, relation.id()};
        area.name = data_.strings.intern(selected_name(relation.tags()));
        area.admin_level = admin_level(relation.tags());
        relation_indices_.emplace(relation.id(), data_.administrative_areas.size());
        data_.administrative_areas.push_back(area);
        ++stats_.extracted_administrative_areas;
    }

    void area(const osmium::Area& source) {
        const auto type = source.from_way() ? OsmObjectType::way : OsmObjectType::relation;
        auto& indices = source.from_way() ? way_indices_ : relation_indices_;
        const auto existing = indices.find(source.orig_id());

        std::size_t area_index = 0;
        if (existing == indices.end()) {
            AdministrativeArea area;
            area.id = OsmId{type, source.orig_id()};
            area.name = data_.strings.intern(selected_name(source.tags()));
            area.admin_level = admin_level(source.tags());
            area_index = data_.administrative_areas.size();
            indices.emplace(source.orig_id(), area_index);
            data_.administrative_areas.push_back(area);
            ++stats_.extracted_administrative_areas;
        } else {
            area_index = existing->second;
        }

        auto& area = data_.administrative_areas[area_index];
        if (area.name == kInvalidStringId) {
            area.name = data_.strings.intern(selected_name(source.tags()));
        }
        if (area.admin_level < 0) {
            area.admin_level = admin_level(source.tags());
        }

        const auto rings_begin = data_.administrative_rings.size();
        bool has_bounds = false;
        BoundingBox bounds{};
        std::uint32_t outer_count = 0;
        std::uint32_t inner_count = 0;

        for (const auto& outer : source.outer_rings()) {
            if (!append_ring(outer, false, bounds, has_bounds)) {
                continue;
            }
            ++outer_count;
            for (const auto& inner : source.inner_rings(outer)) {
                if (append_ring(inner, true, bounds, has_bounds)) {
                    ++inner_count;
                }
            }
        }

        if (outer_count == 0 || !has_bounds) {
            return;
        }
        if (rings_begin > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
            data_.administrative_rings.size() - rings_begin >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error{"Administrative rings exceed 32-bit offsets"};
        }

        area.bounds = bounds;
        area.rings_begin = static_cast<std::uint32_t>(rings_begin);
        area.rings_count = static_cast<std::uint32_t>(
            data_.administrative_rings.size() - rings_begin);
        area.outer_ring_count = outer_count;
        area.inner_ring_count = inner_count;
        area.has_geometry = true;
        stats_.administrative_outer_rings += outer_count;
        stats_.administrative_inner_rings += inner_count;
    }

private:
    template <typename TRing>
    bool append_ring(const TRing& source,
                     const bool is_inner,
                     BoundingBox& bounds,
                     bool& has_bounds) {
        if (data_.administrative_points.size() + source.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error{"Administrative geometry exceeds 32-bit offsets"};
        }

        const auto points_begin = static_cast<std::uint32_t>(data_.administrative_points.size());
        for (const auto& node_ref : source) {
            if (!node_ref.location().valid()) {
                continue;
            }
            const auto point = coordinate_from_degrees(node_ref.location().lat_without_check(),
                                                       node_ref.location().lon_without_check());
            data_.administrative_points.push_back(point);
            if (!is_inner) {
                if (!has_bounds) {
                    bounds = BoundingBox{.minimum = point, .maximum = point};
                    has_bounds = true;
                } else {
                    extend_bounds(bounds, point);
                }
            }
        }

        const auto points_count = data_.administrative_points.size() - points_begin;
        if (points_count < 4) {
            data_.administrative_points.resize(points_begin);
            return false;
        }
        data_.administrative_rings.push_back(AdministrativeRing{
            .points_begin = points_begin,
            .points_count = static_cast<std::uint32_t>(points_count),
            .is_inner = is_inner,
        });
        return true;
    }

    DataStore& data_;
    ExtractionStats& stats_;
    std::unordered_map<std::int64_t, std::size_t> way_indices_;
    std::unordered_map<std::int64_t, std::size_t> relation_indices_;
};

class AreaHandler final : public osmium::handler::Handler {
public:
    explicit AreaHandler(AdministrativeAreaStore& areas) : areas_(areas) {}

    void area(const osmium::Area& area) {
        areas_.area(area);
    }

private:
    AdministrativeAreaStore& areas_;
};

class ExtractionHandler final : public osmium::handler::Handler {
public:
    ExtractionHandler(DataStore& data,
                      ExtractionStats& stats,
                      AdministrativeAreaStore& administrative_areas)
        : data_(data), stats_(stats), administrative_areas_(administrative_areas) {}

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
        if (!is_building && !is_street) {
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
            administrative_areas_.add_relation_metadata(relation);
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
    AdministrativeAreaStore& administrative_areas_;
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
    AdministrativeAreaStore administrative_areas{result.data, result.stats};
    ExtractionHandler extraction_handler{result.data, result.stats, administrative_areas};

    osmium::area::Assembler::config_type assembler_config;
    assembler_config.create_empty_areas = true;
    assembler_config.ignore_invalid_locations = false;
    osmium::TagsFilter administrative_filter{false};
    administrative_filter.add_rule(true, "boundary", "administrative");
    osmium::area::MultipolygonManager<osmium::area::Assembler> multipolygon_manager{
        assembler_config,
        std::move(administrative_filter),
    };

    const osmium::io::File input_file{input_path.string()};

    const auto started = std::chrono::steady_clock::now();
    osmium::relations::read_relations(input_file, multipolygon_manager);

    AreaHandler area_handler{administrative_areas};
    auto& multipolygon_handler = multipolygon_manager.handler(
        [&](osmium::memory::Buffer&& buffer) {
            osmium::apply(buffer, area_handler);
        });

    osmium::io::Reader reader{input_file, osmium::osm_entity_bits::all};
    osmium::apply(reader, location_handler, extraction_handler, multipolygon_handler);
    reader.close();

    multipolygon_manager.for_each_incomplete_relation([&](const auto&) {
        ++result.stats.incomplete_administrative_relations;
    });
    result.stats.open_administrative_rings = multipolygon_manager.stats().open_rings;
    result.stats.administrative_areas_without_geometry =
        static_cast<std::uint64_t>(std::count_if(
            result.data.administrative_areas.begin(),
            result.data.administrative_areas.end(),
            [](const AdministrativeArea& area) { return !area.has_geometry; }));

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
              << "Administrative outer rings: " << stats.administrative_outer_rings << '\n'
              << "Administrative inner rings: " << stats.administrative_inner_rings << '\n'
              << "Administrative areas without geometry: "
              << stats.administrative_areas_without_geometry << '\n'
              << "Incomplete administrative relations: "
              << stats.incomplete_administrative_relations << '\n'
              << "Open administrative rings: " << stats.open_administrative_rings << '\n'
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

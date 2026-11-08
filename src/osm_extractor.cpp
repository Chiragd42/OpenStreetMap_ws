#include "osm_extractor.hpp"

#include <osmium/area/assembler.hpp>
#include <osmium/area/multipolygon_manager.hpp>
#include <osmium/handler.hpp>
#include <osmium/index/map/flex_mem.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/io/file.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/area.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/relations/manager_util.hpp>
#include <osmium/relations/relations_manager.hpp>
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

template <typename TValue>
void sort_and_deduplicate(std::vector<TValue>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

template <typename TValue>
[[nodiscard]] bool contains_sorted(const std::vector<TValue>& values, const TValue value) {
    return std::binary_search(values.begin(), values.end(), value);
}

[[nodiscard]] bool way_has_directly_required_geometry(const osmium::Way& way) {
    const auto& tags = way.tags();
    return tags.has_key("building") || tags.has_key("highway") ||
           tags.has_tag("boundary", "administrative");
}

[[nodiscard]] bool way_requires_locations(
    const osmium::Way& way,
    const std::vector<osmium::object_id_type>& relation_way_ids) {
    return way_has_directly_required_geometry(way) ||
           contains_sorted(relation_way_ids, way.id());
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

[[nodiscard]] long double absolute_twice_area(const std::vector<Coordinate>& points) {
    if (points.size() < 4) {
        return 0.0L;
    }

    long double twice_area = 0.0L;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const auto x0 = static_cast<long double>(points[i].longitude_e7);
        const auto y0 = static_cast<long double>(points[i].latitude_e7);
        const auto x1 = static_cast<long double>(points[i + 1].longitude_e7);
        const auto y1 = static_cast<long double>(points[i + 1].latitude_e7);
        twice_area += x0 * y1 - x1 * y0;
    }
    return std::abs(twice_area);
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

class BuildingStore {
public:
    BuildingStore(DataStore& data, ExtractionStats& stats)
        : data_(data), stats_(stats) {}

    void add(Building building) {
        data_.buildings.push_back(std::move(building));
        ++stats_.extracted_buildings;
    }

    void add_relation_metadata(const osmium::Relation& relation) {
        auto& building = find_or_create_relation(relation.id());
        fill_tags(building, relation.tags());
    }

    void area(const osmium::Area& source) {
        if (source.from_way() || !source.tags().has_key("building")) {
            return;
        }

        auto& building = find_or_create_relation(source.orig_id());
        fill_tags(building, source.tags());

        std::vector<Coordinate> largest_outer_ring;
        long double largest_area = 0.0L;
        for (const auto& outer : source.outer_rings()) {
            std::vector<Coordinate> points;
            points.reserve(outer.size());
            for (const auto& node_ref : outer) {
                if (!node_ref.location().valid()) {
                    continue;
                }
                points.push_back(coordinate_from_degrees(
                    node_ref.location().lat_without_check(),
                    node_ref.location().lon_without_check()));
            }

            const auto ring_area = absolute_twice_area(points);
            if (ring_area > largest_area) {
                largest_area = ring_area;
                largest_outer_ring = std::move(points);
            }
        }

        if (!largest_outer_ring.empty()) {
            building.point = representative_point(largest_outer_ring);
            building.has_point = true;
        }
    }

    void complete_relation(const osmium::Relation& relation,
                           const std::vector<const osmium::Way*>& ways) {
        auto& building = find_or_create_relation(relation.id());
        fill_tags(building, relation.tags());

        std::vector<Coordinate> largest_closed_way;
        std::vector<Coordinate> all_points;
        long double largest_area = 0.0L;
        for (const auto* way : ways) {
            std::vector<Coordinate> points;
            points.reserve(way->nodes().size());
            for (const auto& node_ref : way->nodes()) {
                if (!node_ref.location().valid()) {
                    continue;
                }
                const auto point = coordinate_from_degrees(
                    node_ref.location().lat_without_check(),
                    node_ref.location().lon_without_check());
                points.push_back(point);
                all_points.push_back(point);
            }

            const bool is_closed = points.size() >= 4 &&
                                   points.front().latitude_e7 == points.back().latitude_e7 &&
                                   points.front().longitude_e7 == points.back().longitude_e7;
            const auto way_area = is_closed ? absolute_twice_area(points) : 0.0L;
            if (way_area > largest_area) {
                largest_area = way_area;
                largest_closed_way = std::move(points);
            }
        }

        if (!largest_closed_way.empty()) {
            building.point = representative_point(largest_closed_way);
            building.has_point = true;
        } else if (!all_points.empty()) {
            // Split multipolygon members are not closed individually. This fallback is
            // replaced by the assembled-area centroid when assembly succeeds.
            building.point = bounds_center(bounds_for(all_points));
            building.has_point = true;
        }
    }

private:
    Building& find_or_create_relation(const std::int64_t id) {
        const auto existing = relation_indices_.find(id);
        if (existing != relation_indices_.end()) {
            return data_.buildings[existing->second];
        }

        Building building;
        building.id = OsmId{OsmObjectType::relation, id};
        const auto index = data_.buildings.size();
        relation_indices_.emplace(id, index);
        data_.buildings.push_back(building);
        ++stats_.extracted_buildings;
        return data_.buildings.back();
    }

    void fill_tags(Building& building, const osmium::TagList& tags) {
        building.name = data_.strings.intern(selected_name(tags));
        building.building_type = data_.strings.intern(tag(tags, "building"));
        building.street = data_.strings.intern(tag(tags, "addr:street"));
        building.house_number = data_.strings.intern(tag(tags, "addr:housenumber"));
        building.postcode = data_.strings.intern(tag(tags, "addr:postcode"));
        building.city = data_.strings.intern(tag(tags, "addr:city"));
    }

    DataStore& data_;
    ExtractionStats& stats_;
    std::unordered_map<std::int64_t, std::size_t> relation_indices_;
};

class BuildingRelationManager final
    : public osmium::relations::RelationsManager<BuildingRelationManager, false, true, false> {
public:
    BuildingRelationManager(const osmium::area::Assembler::config_type& config,
                            BuildingStore& buildings,
                            std::vector<osmium::object_id_type>& relation_way_ids)
        : assembler_config_(config),
          buildings_(buildings),
          relation_way_ids_(relation_way_ids) {}

    [[nodiscard]] bool new_relation(const osmium::Relation& relation) const {
        for (const auto& member : relation.members()) {
            if (member.type() == osmium::item_type::way && member.ref() != 0) {
                relation_way_ids_.push_back(member.ref());
            }
        }
        if (!relation.tags().has_key("building")) {
            return false;
        }
        return std::any_of(relation.members().cbegin(), relation.members().cend(),
                           [](const osmium::RelationMember& member) {
                               return member.type() == osmium::item_type::way;
                           });
    }

    [[nodiscard]] bool new_member(const osmium::Relation&,
                                  const osmium::RelationMember& member,
                                  const std::size_t) const noexcept {
        return member.type() == osmium::item_type::way;
    }

    void complete_relation(const osmium::Relation& relation) {
        std::vector<const osmium::Way*> ways;
        ways.reserve(relation.members().size());
        for (const auto& member : relation.members()) {
            if (member.type() != osmium::item_type::way || member.ref() == 0) {
                continue;
            }
            const auto* way = get_member_way(member.ref());
            if (way != nullptr) {
                ways.push_back(way);
            }
        }

        buildings_.complete_relation(relation, ways);

        if (!relation.tags().has_tag("type", "multipolygon") &&
            !relation.tags().has_tag("type", "boundary")) {
            return;
        }

        try {
            osmium::area::Assembler assembler{assembler_config_};
            assembler(relation, ways, buffer());
        } catch (const osmium::invalid_location&) {
            // The relation is retained without a point and reported in the final statistics.
        }
    }

private:
    osmium::area::Assembler::config_type assembler_config_;
    BuildingStore& buildings_;
    std::vector<osmium::object_id_type>& relation_way_ids_;
};

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

class AdministrativeAreaHandler final : public osmium::handler::Handler {
public:
    explicit AdministrativeAreaHandler(AdministrativeAreaStore& areas) : areas_(areas) {}

    void area(const osmium::Area& area) {
        areas_.area(area);
    }

private:
    AdministrativeAreaStore& areas_;
};

class BuildingAreaHandler final : public osmium::handler::Handler {
public:
    explicit BuildingAreaHandler(BuildingStore& buildings) : buildings_(buildings) {}

    void area(const osmium::Area& area) {
        buildings_.area(area);
    }

private:
    BuildingStore& buildings_;
};

class RequiredNodeCollector final : public osmium::handler::Handler {
public:
    RequiredNodeCollector(
        const std::vector<osmium::object_id_type>& relation_way_ids,
        std::vector<osmium::unsigned_object_id_type>& required_node_ids)
        : relation_way_ids_(relation_way_ids), required_node_ids_(required_node_ids) {}

    void way(const osmium::Way& way) {
        if (!way_requires_locations(way, relation_way_ids_)) {
            return;
        }
        for (const auto& node_ref : way.nodes()) {
            required_node_ids_.push_back(node_ref.positive_ref());
        }
    }

private:
    const std::vector<osmium::object_id_type>& relation_way_ids_;
    std::vector<osmium::unsigned_object_id_type>& required_node_ids_;
};

using SelectiveNodeIndex = osmium::index::map::FlexMem<
    osmium::unsigned_object_id_type,
    osmium::Location>;

class SelectiveNodeLocationsForWays final : public osmium::handler::Handler {
public:
    SelectiveNodeLocationsForWays(
        SelectiveNodeIndex& node_index,
        const std::vector<osmium::unsigned_object_id_type>& required_node_ids,
        const std::vector<osmium::object_id_type>& relation_way_ids)
        : node_index_(node_index),
          required_node_ids_(required_node_ids),
          relation_way_ids_(relation_way_ids) {}

    void node(const osmium::Node& node) {
        const auto id = node.positive_id();
        bool is_required = false;

        if (nodes_are_ordered_ && id >= last_node_id_) {
            while (next_required_node_ < required_node_ids_.size() &&
                   required_node_ids_[next_required_node_] < id) {
                ++next_required_node_;
            }
            is_required = next_required_node_ < required_node_ids_.size() &&
                          required_node_ids_[next_required_node_] == id;
        } else {
            nodes_are_ordered_ = false;
            is_required = contains_sorted(required_node_ids_, id);
        }

        if (is_required) {
            node_index_.set(id, node.location());
        }
        last_node_id_ = id;
    }

    void way(osmium::Way& way) {
        if (!way_requires_locations(way, relation_way_ids_)) {
            return;
        }
        if (!index_is_ready_) {
            if (!nodes_are_ordered_) {
                node_index_.sort();
            }
            index_is_ready_ = true;
        }
        for (auto& node_ref : way.nodes()) {
            node_ref.set_location(node_index_.get_noexcept(node_ref.positive_ref()));
        }
    }

private:
    SelectiveNodeIndex& node_index_;
    const std::vector<osmium::unsigned_object_id_type>& required_node_ids_;
    const std::vector<osmium::object_id_type>& relation_way_ids_;
    std::size_t next_required_node_{0};
    osmium::unsigned_object_id_type last_node_id_{0};
    bool nodes_are_ordered_{true};
    bool index_is_ready_{false};
};

class ExtractionHandler final : public osmium::handler::Handler {
public:
    ExtractionHandler(DataStore& data,
                      ExtractionStats& stats,
                      BuildingStore& buildings,
                      AdministrativeAreaStore& administrative_areas)
        : data_(data),
          stats_(stats),
          buildings_(buildings),
          administrative_areas_(administrative_areas) {}

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
        buildings_.add(std::move(building));
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
            buildings_.add(std::move(building));
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
            buildings_.add_relation_metadata(relation);
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

    DataStore& data_;
    ExtractionStats& stats_;
    BuildingStore& buildings_;
    AdministrativeAreaStore& administrative_areas_;
};

} // namespace

ExtractionResult extract_pbf(const std::filesystem::path& input_path) {
    if (!std::filesystem::is_regular_file(input_path)) {
        throw std::runtime_error{"Input is not a regular file: " + input_path.string()};
    }

    ExtractionResult result;
    SelectiveNodeIndex node_index;
    std::vector<osmium::object_id_type> relation_way_ids;
    std::vector<osmium::unsigned_object_id_type> required_node_ids;
    BuildingStore buildings{result.data, result.stats};
    AdministrativeAreaStore administrative_areas{result.data, result.stats};
    ExtractionHandler extraction_handler{
        result.data,
        result.stats,
        buildings,
        administrative_areas,
    };

    osmium::area::Assembler::config_type assembler_config;
    assembler_config.create_empty_areas = true;
    assembler_config.ignore_invalid_locations = false;
    osmium::TagsFilter administrative_filter{false};
    administrative_filter.add_rule(true, "boundary", "administrative");
    osmium::area::MultipolygonManager<osmium::area::Assembler> multipolygon_manager{
        assembler_config,
        std::move(administrative_filter),
    };
    BuildingRelationManager building_relation_manager{
        assembler_config,
        buildings,
        relation_way_ids,
    };

    const osmium::io::File input_file{input_path.string()};

    const auto started = std::chrono::steady_clock::now();
    osmium::relations::read_relations(
        input_file,
        multipolygon_manager,
        building_relation_manager);
    sort_and_deduplicate(relation_way_ids);

    RequiredNodeCollector required_node_collector{relation_way_ids, required_node_ids};
    osmium::io::Reader way_reader{input_file, osmium::osm_entity_bits::way};
    osmium::apply(way_reader, required_node_collector);
    way_reader.close();
    sort_and_deduplicate(required_node_ids);

    SelectiveNodeLocationsForWays location_handler{
        node_index,
        required_node_ids,
        relation_way_ids,
    };

    AdministrativeAreaHandler area_handler{administrative_areas};
    auto& multipolygon_handler = multipolygon_manager.handler(
        [&](osmium::memory::Buffer&& buffer) {
            osmium::apply(buffer, area_handler);
        });
    BuildingAreaHandler building_area_handler{buildings};
    auto& building_relation_handler = building_relation_manager.handler(
        [&](osmium::memory::Buffer&& buffer) {
            osmium::apply(buffer, building_area_handler);
        });

    osmium::io::Reader reader{input_file, osmium::osm_entity_bits::all};
    osmium::apply(reader,
                  location_handler,
                  extraction_handler,
                  multipolygon_handler,
                  building_relation_handler);
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
    result.stats.buildings_without_names = static_cast<std::uint64_t>(std::count_if(
        result.data.buildings.begin(),
        result.data.buildings.end(),
        [](const Building& building) { return building.name == kInvalidStringId; }));
    result.stats.buildings_without_points = static_cast<std::uint64_t>(std::count_if(
        result.data.buildings.begin(),
        result.data.buildings.end(),
        [](const Building& building) { return !building.has_point; }));

    result.stats.elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    result.stats.cpu_seconds = process_cpu_seconds();
    result.stats.peak_memory_mib = peak_memory_mib();
    result.stats.temporary_node_index_bytes =
        node_index.used_memory() +
        required_node_ids.capacity() * sizeof(osmium::unsigned_object_id_type) +
        relation_way_ids.capacity() * sizeof(osmium::object_id_type);
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

#include "ingest/pbf_extractor.hpp"

#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/visitor.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace osm {

namespace {

[[nodiscard]] StringId intern_if_non_empty(StringPool& pool, std::string_view value) {
    if (value.empty()) {
        return kInvalidStringId;
    }
    return pool.intern(value);
}

[[nodiscard]] std::string_view tag_value_or_empty(const osmium::TagList& tags, const char* key) {
    if (const char* value = tags.get_value_by_key(key)) {
        return value;
    }
    return {};
}

[[nodiscard]] std::string pick_preferred_name(const osmium::TagList& tags) {
    if (const char* v = tags.get_value_by_key("name:de")) {
        return v;
    }
    if (const char* v = tags.get_value_by_key("name")) {
        return v;
    }
    return {};
}

[[nodiscard]] bool has_address_tags(const osmium::TagList& tags) {
    return tags.has_key("addr:housenumber");
}

[[nodiscard]] std::optional<std::int32_t> parse_admin_level(const osmium::TagList& tags) {
    const char* value = tags.get_value_by_key("admin_level");
    if (value == nullptr) {
        return std::nullopt;
    }

    try {
        return std::stoi(std::string(value));
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] BBox bbox_from_points(const std::vector<GeoPoint>& points) {
    BBox bbox;
    if (points.empty()) {
        return bbox;
    }

    bbox.min_lon = points.front().lon;
    bbox.max_lon = points.front().lon;
    bbox.min_lat = points.front().lat;
    bbox.max_lat = points.front().lat;

    for (const auto& p : points) {
        bbox.min_lon = std::min(bbox.min_lon, static_cast<double>(p.lon));
        bbox.max_lon = std::max(bbox.max_lon, static_cast<double>(p.lon));
        bbox.min_lat = std::min(bbox.min_lat, static_cast<double>(p.lat));
        bbox.max_lat = std::max(bbox.max_lat, static_cast<double>(p.lat));
    }

    return bbox;
}

[[nodiscard]] std::optional<GeoPoint> polygon_centroid(const std::vector<GeoPoint>& points) {
    if (points.size() < 3) {
        return std::nullopt;
    }

    double cross_sum = 0.0;
    double cx_acc = 0.0;
    double cy_acc = 0.0;

    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];

        const double ax = static_cast<double>(a.lon);
        const double ay = static_cast<double>(a.lat);
        const double bx = static_cast<double>(b.lon);
        const double by = static_cast<double>(b.lat);

        const double cross = (ax * by) - (bx * ay);
        cross_sum += cross;
        cx_acc += (ax + bx) * cross;
        cy_acc += (ay + by) * cross;
    }

    if (std::abs(cross_sum) < 1e-12) {
        return std::nullopt;
    }

    const double cx = cx_acc / (3.0 * cross_sum);
    const double cy = cy_acc / (3.0 * cross_sum);
    return GeoPoint{.lat = static_cast<float>(cy), .lon = static_cast<float>(cx)};
}

void add_house_to_grid(SpatialGridIndex& grid, const HousePoint& house, const std::size_t idx) {
    const auto key = to_grid_cell(house.lon, house.lat, grid.cell_size_deg);
    grid.house_cells[key].push_back(idx);
}

void add_bbox_to_grid(
    std::unordered_map<GridCellKey, std::vector<std::size_t>, GridCellKeyHash>& cells,
    const BBox& bbox,
    const float cell_size_deg,
    const std::size_t idx) {
    for (const auto& key : grid_cells_for_bbox(bbox, cell_size_deg)) {
        cells[key].push_back(idx);
    }
}

[[nodiscard]] std::size_t estimate_string_pool_bytes(const StringPool& pool) {
    std::size_t bytes = 0;
    for (StringId id = 0; id < pool.size(); ++id) {
        bytes += pool.resolve(id).size();
    }
    return bytes;
}

class PbfSheet1Handler final : public osmium::handler::Handler {
public:
    explicit PbfSheet1Handler(DataStore& data, ParseStats& stats, const bool include_regions)
        : data_(data), stats_(stats), include_regions_(include_regions) {}

    void node(const osmium::Node& node) {
        ++stats_.processed_nodes;
        if (!node.location().valid()) {
            return;
        }

        const auto& tags = node.tags();
        if (!has_address_tags(tags)) {
            return;
        }

        HousePoint house;
        house.lat = static_cast<float>(node.location().lat_without_check());
        house.lon = static_cast<float>(node.location().lon_without_check());
        house.street_name_id = intern_if_non_empty(data_.strings, tag_value_or_empty(tags, "addr:street"));
        house.house_number_id = intern_if_non_empty(data_.strings, tag_value_or_empty(tags, "addr:housenumber"));
        house.city_id = intern_if_non_empty(data_.strings, tag_value_or_empty(tags, "addr:city"));
        house.postcode_id = intern_if_non_empty(data_.strings, tag_value_or_empty(tags, "addr:postcode"));
        data_.houses.push_back(house);

        ++stats_.houses_from_address_nodes;
        ++stats_.extracted_houses;
    }

    void way(const osmium::Way& way) {
        ++stats_.processed_ways;

        const auto& tags = way.tags();
        const bool is_street = tags.has_key("highway");
        const bool is_address_building = tags.has_key("building") && has_address_tags(tags);
        const bool is_region = include_regions_ && tags.has_tag("boundary", "administrative");

        if (!is_street && !is_address_building && !is_region) {
            return;
        }

        std::vector<GeoPoint> points;
        points.reserve(way.nodes().size());
        for (const auto& nr : way.nodes()) {
            if (!nr.location().valid()) {
                continue;
            }

            points.push_back(GeoPoint{
                .lat = static_cast<float>(nr.location().lat_without_check()),
                .lon = static_cast<float>(nr.location().lon_without_check()),
            });
        }

        if (is_street) {
            extract_street(tags, points);
        }

        if (is_address_building) {
            extract_building_house(tags, points);
        }

        if (is_region) {
            extract_region(tags, points);
        }
    }

    void relation(const osmium::Relation& relation) {
        ++stats_.processed_relations;
        if (!include_regions_) {
            return;
        }

        if (relation.tags().has_tag("boundary", "administrative")) {
            ++stats_.regions_skipped_complex_relations;
        }
    }

private:
    void extract_street(const osmium::TagList& tags, const std::vector<GeoPoint>& points) {
        if (points.size() < 2) {
            return;
        }

        StreetPolyline street;
        const std::string name = pick_preferred_name(tags);
        street.is_unnamed = name.empty();
        if (street.is_unnamed) {
            ++stats_.unnamed_streets;
        } else {
            street.name_id = intern_if_non_empty(data_.strings, name);
        }

        street.highway_class_id = intern_if_non_empty(data_.strings, tag_value_or_empty(tags, "highway"));
        street.points_begin = static_cast<std::uint32_t>(data_.street_points.size());
        street.points_count = static_cast<std::uint32_t>(points.size());
        street.bbox = bbox_from_points(points);

        data_.street_points.insert(data_.street_points.end(), points.begin(), points.end());
        data_.streets.push_back(street);
        ++stats_.extracted_streets;
    }

    void extract_building_house(const osmium::TagList& tags, const std::vector<GeoPoint>& points) {
        if (points.empty()) {
            ++stats_.houses_skipped_invalid_geometry;
            return;
        }

        GeoPoint representative{};
        if (points.size() < 3) {
            const auto bbox = bbox_from_points(points);
            representative.lat = static_cast<float>((bbox.min_lat + bbox.max_lat) * 0.5);
            representative.lon = static_cast<float>((bbox.min_lon + bbox.max_lon) * 0.5);
            ++stats_.houses_from_polygon_bbox_fallback;
        } else {
            const auto centroid = polygon_centroid(points);
            if (centroid.has_value()) {
                representative = *centroid;
                ++stats_.houses_from_polygon_centroid;
            } else {
                const auto bbox = bbox_from_points(points);
                representative.lat = static_cast<float>((bbox.min_lat + bbox.max_lat) * 0.5);
                representative.lon = static_cast<float>((bbox.min_lon + bbox.max_lon) * 0.5);
                ++stats_.houses_from_polygon_bbox_fallback;
            }
        }

        HousePoint house;
        house.lat = representative.lat;
        house.lon = representative.lon;
        house.street_name_id = intern_if_non_empty(data_.strings, tag_value_or_empty(tags, "addr:street"));
        house.house_number_id = intern_if_non_empty(data_.strings, tag_value_or_empty(tags, "addr:housenumber"));
        house.city_id = intern_if_non_empty(data_.strings, tag_value_or_empty(tags, "addr:city"));
        house.postcode_id = intern_if_non_empty(data_.strings, tag_value_or_empty(tags, "addr:postcode"));
        data_.houses.push_back(house);
        ++stats_.extracted_houses;
    }

    void extract_region(const osmium::TagList& tags, const std::vector<GeoPoint>& points) {
        if (points.size() < 3) {
            return;
        }

        RegionPolygon region;
        region.name_id = intern_if_non_empty(data_.strings, pick_preferred_name(tags));
        region.admin_level = parse_admin_level(tags).value_or(-1);
        region.points_begin = static_cast<std::uint32_t>(data_.region_points.size());
        region.points_count = static_cast<std::uint32_t>(points.size());
        region.bbox = bbox_from_points(points);
        data_.region_points.insert(data_.region_points.end(), points.begin(), points.end());
        data_.regions.push_back(region);

        ++stats_.extracted_regions;
    }

    DataStore& data_;
    ParseStats& stats_;
    bool include_regions_{true};
};

} // namespace

ExtractionResult PbfExtractor::extract(const ExtractionConfig& config) const {
    ExtractionResult result;
    result.data.grid.cell_size_deg = config.grid_cell_size_deg;

    Stopwatch stopwatch;
    stopwatch.start();

    using index_type = osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
    using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

    index_type index;
    location_handler_type location_handler(index);
    location_handler.ignore_errors();

    osmium::io::Reader reader(config.input_pbf_path, osmium::osm_entity_bits::all);
    PbfSheet1Handler handler(result.data, result.stats, config.include_regions);
    osmium::apply(reader, location_handler, handler);
    reader.close();

    for (std::size_t i = 0; i < result.data.houses.size(); ++i) {
        add_house_to_grid(result.data.grid, result.data.houses[i], i);
    }
    for (std::size_t i = 0; i < result.data.streets.size(); ++i) {
        add_bbox_to_grid(
            result.data.grid.street_cells,
            result.data.streets[i].bbox,
            result.data.grid.cell_size_deg,
            i);
    }
    for (std::size_t i = 0; i < result.data.regions.size(); ++i) {
        add_bbox_to_grid(
            result.data.grid.region_cells,
            result.data.regions[i].bbox,
            result.data.grid.cell_size_deg,
            i);
    }

    result.stats.estimated_memory_bytes =
        (result.data.houses.size() * sizeof(HousePoint)) +
        (result.data.streets.size() * sizeof(StreetPolyline)) +
        (result.data.street_points.size() * sizeof(GeoPoint)) +
        (result.data.regions.size() * sizeof(RegionPolygon)) +
        (result.data.region_points.size() * sizeof(GeoPoint)) +
        estimate_string_pool_bytes(result.data.strings);

    result.stats.parse_seconds = stopwatch.elapsed_seconds();
    return result;
}

} // namespace osm

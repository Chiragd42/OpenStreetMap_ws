#include "ingest/pbf_extractor.hpp"

#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/visitor.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <limits>
#include <optional>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <map>
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

[[nodiscard]] const char* first_non_empty_tag(const osmium::TagList& tags, const std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (const char* value = tags.get_value_by_key(key)) {
            if (*value != '\0') {
                return value;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<PoiCategory> classify_poi(const osmium::TagList& tags) {
    if (tags.has_tag("amenity", "restaurant")) return PoiCategory::Restaurant;
    if (tags.has_tag("amenity", "cafe")) return PoiCategory::Cafe;
    if (tags.has_tag("amenity", "fast_food")) return PoiCategory::FastFood;
    if (tags.has_tag("amenity", "hospital")) return PoiCategory::Hospital;
    if (tags.has_tag("amenity", "school")) return PoiCategory::School;
    if (tags.has_tag("railway", "station")) return PoiCategory::Station;
    if (tags.has_tag("public_transport", "station")) return PoiCategory::Station;
    if (tags.has_tag("tourism", "hotel")) return PoiCategory::Hotel;
    if (tags.has_tag("leisure", "park")) return PoiCategory::Park;
    if (const char* shop = tags.get_value_by_key("shop")) {
        if (std::string_view(shop) != "no") return PoiCategory::Shop;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view poi_subtype_value(const osmium::TagList& tags, const PoiCategory category) {
    switch (category) {
        case PoiCategory::Shop:
            return tag_value_or_empty(tags, "shop");
        case PoiCategory::Restaurant:
        case PoiCategory::Cafe:
        case PoiCategory::FastFood:
        case PoiCategory::Hospital:
        case PoiCategory::School:
            return tag_value_or_empty(tags, "amenity");
        case PoiCategory::Park:
            return tag_value_or_empty(tags, "leisure");
        case PoiCategory::Hotel:
            return tag_value_or_empty(tags, "tourism");
        case PoiCategory::Station:
            if (const auto railway = tag_value_or_empty(tags, "railway"); !railway.empty()) return railway;
            return tag_value_or_empty(tags, "public_transport");
        case PoiCategory::Other:
            return {};
    }
    return {};
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

[[nodiscard]] bool is_postal_region(const osmium::TagList& tags) {
    if (tags.has_tag("boundary", "postal_code")) {
        return true;
    }
    return tags.has_key("postal_code") || tags.has_key("addr:postcode") || tags.has_key("postcode");
}

[[nodiscard]] bool is_germany_name(std::string_view name) {
    return name == "Deutschland" || name == "Germany" || name == "Bundesrepublik Deutschland";
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

[[nodiscard]] std::optional<GeoPoint> representative_point(const std::vector<GeoPoint>& points) {
    if (points.empty()) {
        return std::nullopt;
    }
    if (points.size() < 3) {
        const auto bbox = bbox_from_points(points);
        return GeoPoint{
            .lat = static_cast<float>((bbox.min_lat + bbox.max_lat) * 0.5),
            .lon = static_cast<float>((bbox.min_lon + bbox.max_lon) * 0.5),
        };
    }
    if (const auto centroid = polygon_centroid(points); centroid.has_value()) {
        return centroid;
    }
    const auto bbox = bbox_from_points(points);
    return GeoPoint{
        .lat = static_cast<float>((bbox.min_lat + bbox.max_lat) * 0.5),
        .lon = static_cast<float>((bbox.min_lon + bbox.max_lon) * 0.5),
    };
}

[[nodiscard]] double polygon_area_approx(const std::vector<GeoPoint>& points) {
    if (points.size() < 3) {
        return 0.0;
    }
    double area2 = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];
        area2 += static_cast<double>(a.lon) * static_cast<double>(b.lat) -
                 static_cast<double>(b.lon) * static_cast<double>(a.lat);
    }
    return std::abs(area2) * 0.5;
}

[[nodiscard]] bool point_in_polygon(const GeoPoint& point, const GeoPoint* polygon, const std::size_t n) {
    if (n < 3) {
        return false;
    }
    const double x = static_cast<double>(point.lon);
    const double y = static_cast<double>(point.lat);
    bool inside = false;

    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = static_cast<double>(polygon[i].lon);
        const double yi = static_cast<double>(polygon[i].lat);
        const double xj = static_cast<double>(polygon[j].lon);
        const double yj = static_cast<double>(polygon[j].lat);

        const bool intersect = ((yi > y) != (yj > y)) &&
                               (x < (xj - xi) * (y - yi) / ((yj - yi) + 1e-15) + xi);
        if (intersect) {
            inside = !inside;
        }
    }
    return inside;
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

struct TargetRelation {
    std::int64_t relation_id{0};
    std::int32_t admin_level{-1};
    std::string name;
    std::vector<std::int64_t> outer_way_ids;
};

class TargetAdminRelationCollector final : public osmium::handler::Handler {
public:
    void relation(const osmium::Relation& rel) {
        if (!rel.tags().has_tag("boundary", "administrative")) {
            return;
        }
        const auto level = parse_admin_level(rel.tags()).value_or(-1);
        if (level != 2 && level != 4) {
            return;
        }

        TargetRelation tr;
        tr.relation_id = rel.id();
        tr.admin_level = level;
        tr.name = pick_preferred_name(rel.tags());
        for (const auto& m : rel.members()) {
            if (m.type() == osmium::item_type::way && std::string_view(m.role()) == "outer") {
                tr.outer_way_ids.push_back(m.ref());
            }
        }
        if (!tr.outer_way_ids.empty()) {
            relations.push_back(std::move(tr));
        }
    }

    std::vector<TargetRelation> relations;
};

class WayGeometryCollector final : public osmium::handler::Handler {
public:
    explicit WayGeometryCollector(const std::unordered_set<std::int64_t>& target_ids)
        : target_way_ids(target_ids) {}

    void way(const osmium::Way& way) {
        if (target_way_ids.find(way.id()) == target_way_ids.end()) {
            return;
        }
        std::vector<GeoPoint> points;
        points.reserve(way.nodes().size());
        for (const auto& nr : way.nodes()) {
            if (!nr.location().valid()) continue;
            points.push_back(GeoPoint{.lat = static_cast<float>(nr.location().lat_without_check()),
                                      .lon = static_cast<float>(nr.location().lon_without_check())});
        }
        if (points.size() >= 2) {
            ways.emplace(way.id(), std::move(points));
        }
    }

    const std::unordered_set<std::int64_t>& target_way_ids;
    std::unordered_map<std::int64_t, std::vector<GeoPoint>> ways;
};

[[nodiscard]] bool same_point(const GeoPoint& a, const GeoPoint& b) {
    return std::abs(static_cast<double>(a.lat - b.lat)) < 1e-6 &&
           std::abs(static_cast<double>(a.lon - b.lon)) < 1e-6;
}

[[nodiscard]] std::vector<std::vector<GeoPoint>> assemble_outer_rings(const std::vector<std::vector<GeoPoint>>& segments) {
    std::vector<std::vector<GeoPoint>> rings;
    if (segments.empty()) return rings;

    std::vector<bool> used(segments.size(), false);
    for (std::size_t seed = 0; seed < segments.size(); ++seed) {
        if (used[seed] || segments[seed].empty()) continue;

        std::vector<GeoPoint> ring = segments[seed];
        used[seed] = true;

        bool progress = true;
        while (progress) {
            progress = false;
            for (std::size_t i = 0; i < segments.size(); ++i) {
                if (used[i] || segments[i].empty()) continue;
                const auto& seg = segments[i];
                if (same_point(ring.back(), seg.front())) {
                    ring.insert(ring.end(), seg.begin() + 1, seg.end());
                    used[i] = true;
                    progress = true;
                } else if (same_point(ring.back(), seg.back())) {
                    for (std::size_t j = seg.size() - 1; j > 0; --j) ring.push_back(seg[j - 1]);
                    used[i] = true;
                    progress = true;
                }
            }
        }

        const bool naturally_closed = !ring.empty() && same_point(ring.front(), ring.back());
        if (naturally_closed && ring.size() >= 4) {
            rings.push_back(std::move(ring));
        }
    }
    return rings;
}

void reconstruct_admin_relations_l2_l4(const std::string& input_path, DataStore& data, ParseStats& stats) {
    using index_type = osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
    using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

    TargetAdminRelationCollector rel_collector;
    {
        osmium::io::Reader rel_reader(input_path, osmium::osm_entity_bits::relation);
        osmium::apply(rel_reader, rel_collector);
        rel_reader.close();
    }

    std::unordered_set<std::int64_t> needed_way_ids;
    for (const auto& rel : rel_collector.relations) {
        for (const auto wid : rel.outer_way_ids) needed_way_ids.insert(wid);
    }
    if (needed_way_ids.empty()) return;

    index_type index;
    location_handler_type location_handler(index);
    location_handler.ignore_errors();
    WayGeometryCollector way_collector(needed_way_ids);

    {
        osmium::io::Reader way_reader(input_path, osmium::osm_entity_bits::node | osmium::osm_entity_bits::way);
        osmium::apply(way_reader, location_handler, way_collector);
        way_reader.close();
    }

    std::size_t added = 0;
    std::size_t unclosed_or_invalid_rings = 0;
    for (const auto& rel : rel_collector.relations) {
        std::vector<std::vector<GeoPoint>> segments;
        for (const auto wid : rel.outer_way_ids) {
            const auto it = way_collector.ways.find(wid);
            if (it != way_collector.ways.end()) segments.push_back(it->second);
        }
        const auto relation_segment_count = segments.size();
        auto rings = assemble_outer_rings(segments);
        if (rings.size() < relation_segment_count) {
            unclosed_or_invalid_rings += (relation_segment_count - rings.size());
        }
        if (rings.empty()) continue;

        for (const auto& ring : rings) {
            RegionPolygon region;
            region.name_id = intern_if_non_empty(data.strings, rel.name);
            region.admin_level = rel.admin_level;
            region.is_postal_region = false;
            region.points_begin = static_cast<std::uint32_t>(data.region_points.size());
            region.points_count = static_cast<std::uint32_t>(ring.size());
            region.bbox = bbox_from_points(ring);
            region.approx_area = polygon_area_approx(ring);
            data.region_points.insert(data.region_points.end(), ring.begin(), ring.end());
            data.regions.push_back(region);
            ++stats.extracted_regions;
            ++added;
        }
    }

    std::cout << "[AdminRelationReconstruction] added_regions_l2_l4: " << added
              << " skipped_unclosed_or_invalid_rings: " << unclosed_or_invalid_rings << '\n';
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
        extract_poi_node(node, tags);

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
        const bool is_poi = first_non_empty_tag(tags, {"name", "name:de"}) != nullptr && classify_poi(tags).has_value();

        if (!is_street && !is_address_building && !is_region && !is_poi) {
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

        if (is_poi) {
            extract_poi_way(way, tags, points);
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
            const auto lvl = parse_admin_level(relation.tags()).value_or(-1);
            ++skipped_admin_relations_by_level_[lvl];
        }
    }

    [[nodiscard]] const std::unordered_map<int, std::uint64_t>& extracted_admin_regions_by_level() const {
        return extracted_admin_regions_by_level_;
    }

    [[nodiscard]] const std::unordered_map<int, std::uint64_t>& skipped_admin_relations_by_level() const {
        return skipped_admin_relations_by_level_;
    }

    [[nodiscard]] std::uint64_t postal_regions_extracted() const {
        return postal_regions_extracted_;
    }

    [[nodiscard]] const std::unordered_set<std::string>& extracted_region_names() const {
        return extracted_region_names_;
    }

private:
    void extract_poi_node(const osmium::Node& node, const osmium::TagList& tags) {
        const char* name = first_non_empty_tag(tags, {"name", "name:de"});
        const auto category = classify_poi(tags);
        if (!category.has_value()) {
            return;
        }
        if (name == nullptr) {
            ++stats_.skipped_unnamed_pois;
            return;
        }

        PoiPoint poi;
        poi.osm_id = static_cast<std::uint64_t>(node.id() < 0 ? -node.id() : node.id());
        poi.osm_type = OsmElementType::Node;
        poi.lat = static_cast<float>(node.location().lat_without_check());
        poi.lon = static_cast<float>(node.location().lon_without_check());
        poi.name_id = intern_if_non_empty(data_.strings, name);
        poi.category = *category;
        poi.subtype_id = intern_if_non_empty(data_.strings, poi_subtype_value(tags, poi.category));
        data_.pois.push_back(poi);

        ++stats_.extracted_pois_total;
        ++stats_.extracted_poi_nodes;
        ++stats_.extracted_pois_by_category[static_cast<std::size_t>(poi.category)];
    }

    void extract_poi_way(const osmium::Way& way, const osmium::TagList& tags, const std::vector<GeoPoint>& points) {
        const char* name = first_non_empty_tag(tags, {"name", "name:de"});
        const auto category = classify_poi(tags);
        if (!category.has_value()) {
            return;
        }
        if (name == nullptr) {
            ++stats_.skipped_unnamed_pois;
            return;
        }

        const auto point = representative_point(points);
        if (!point.has_value()) {
            ++stats_.skipped_invalid_poi_geometry;
            return;
        }

        PoiPoint poi;
        poi.osm_id = static_cast<std::uint64_t>(way.id() < 0 ? -way.id() : way.id());
        poi.osm_type = OsmElementType::Way;
        poi.lat = point->lat;
        poi.lon = point->lon;
        poi.name_id = intern_if_non_empty(data_.strings, name);
        poi.category = *category;
        poi.subtype_id = intern_if_non_empty(data_.strings, poi_subtype_value(tags, poi.category));
        data_.pois.push_back(poi);

        ++stats_.extracted_pois_total;
        ++stats_.extracted_poi_ways;
        ++stats_.extracted_pois_by_category[static_cast<std::size_t>(poi.category)];
    }

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
        region.is_postal_region = is_postal_region(tags);
        region.points_begin = static_cast<std::uint32_t>(data_.region_points.size());
        region.points_count = static_cast<std::uint32_t>(points.size());
        region.bbox = bbox_from_points(points);
        region.approx_area = polygon_area_approx(points);
        data_.region_points.insert(data_.region_points.end(), points.begin(), points.end());
        data_.regions.push_back(region);

        ++stats_.extracted_regions;
        ++extracted_admin_regions_by_level_[region.admin_level];
        if (region.is_postal_region) {
            ++postal_regions_extracted_;
        }
        if (region.name_id != kInvalidStringId) {
            extracted_region_names_.insert(data_.strings.resolve(region.name_id));
        }
    }

    DataStore& data_;
    ParseStats& stats_;
    bool include_regions_{true};
    std::unordered_map<int, std::uint64_t> extracted_admin_regions_by_level_;
    std::unordered_map<int, std::uint64_t> skipped_admin_relations_by_level_;
    std::uint64_t postal_regions_extracted_{0};
    std::unordered_set<std::string> extracted_region_names_;
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

    reconstruct_admin_relations_l2_l4(config.input_pbf_path, result.data, result.stats);

    const auto print_level_count = [&](const std::unordered_map<int, std::uint64_t>& m, const int level) {
        const auto it = m.find(level);
        return it == m.end() ? 0ULL : it->second;
    };

    std::cout << "\n[AdminDiagnostics]\n"
              << "  extracted_admin_level_2: " << print_level_count(handler.extracted_admin_regions_by_level(), 2) << '\n'
              << "  extracted_admin_level_4: " << print_level_count(handler.extracted_admin_regions_by_level(), 4) << '\n'
              << "  extracted_admin_level_6: " << print_level_count(handler.extracted_admin_regions_by_level(), 6) << '\n'
              << "  extracted_admin_level_8: " << print_level_count(handler.extracted_admin_regions_by_level(), 8) << '\n'
              << "  postal_regions_extracted: " << handler.postal_regions_extracted() << '\n'
              << "  skipped_admin_rel_level_2: " << print_level_count(handler.skipped_admin_relations_by_level(), 2) << '\n'
              << "  skipped_admin_rel_level_4: " << print_level_count(handler.skipped_admin_relations_by_level(), 4) << '\n'
              << "  skipped_admin_rel_level_6: " << print_level_count(handler.skipped_admin_relations_by_level(), 6) << '\n'
              << "  skipped_admin_rel_level_8: " << print_level_count(handler.skipped_admin_relations_by_level(), 8) << '\n';

    // Keep diagnostics concise: per-level extraction/skip counts are sufficient for routine runs.

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

    const auto assign_pois_start = std::chrono::steady_clock::now();
    result.data.poi_containing_region_ids.clear();
    result.data.poi_containing_region_ids.reserve(result.data.pois.size() * 2);
    for (std::size_t pi = 0; pi < result.data.pois.size(); ++pi) {
        auto& poi = result.data.pois[pi];
        poi.containing_regions_begin = static_cast<std::uint32_t>(result.data.poi_containing_region_ids.size());
        poi.containing_regions_count = 0;

        const auto key = to_grid_cell(poi.lon, poi.lat, result.data.grid.cell_size_deg);
        const auto it = result.data.grid.region_cells.find(key);
        if (it == result.data.grid.region_cells.end()) {
            ++result.stats.pois_without_region;
            continue;
        }

        std::unordered_set<std::size_t> dedup;
        dedup.reserve(it->second.size());
        const GeoPoint pp{.lat = poi.lat, .lon = poi.lon};
        for (const auto ridx : it->second) {
            if (!dedup.insert(ridx).second || ridx >= result.data.regions.size()) {
                continue;
            }
            const auto& region = result.data.regions[ridx];
            if (!region.bbox.contains(poi.lon, poi.lat)) {
                continue;
            }
            const auto begin = static_cast<std::size_t>(region.points_begin);
            const auto count = static_cast<std::size_t>(region.points_count);
            if (begin + count > result.data.region_points.size() || count < 3) {
                continue;
            }
            if (!point_in_polygon(pp, result.data.region_points.data() + begin, count)) {
                continue;
            }
            result.data.poi_containing_region_ids.push_back(static_cast<std::uint32_t>(ridx));
            ++poi.containing_regions_count;
        }

        if (poi.containing_regions_count > 0) {
            ++result.stats.pois_assigned_to_region;
        } else {
            ++result.stats.pois_without_region;
        }
    }
    result.stats.poi_region_assignment_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - assign_pois_start).count();

    const auto assign_start = std::chrono::steady_clock::now();
    result.data.house_containing_region_ids.clear();
    result.data.house_containing_region_ids.reserve(result.data.houses.size() * 2);
    std::size_t pip_candidate_checks = 0;
    bool debug_house_logged = false;
    std::size_t state_without_country_fallback_count = 0;

    for (std::size_t hi = 0; hi < result.data.houses.size(); ++hi) {
        auto& house = result.data.houses[hi];
        const auto key = to_grid_cell(house.lon, house.lat, result.data.grid.cell_size_deg);

        house.containing_regions_begin = static_cast<std::uint32_t>(result.data.house_containing_region_ids.size());
        house.containing_regions_count = 0;

        const auto it = result.data.grid.region_cells.find(key);
        const bool is_debug_house = false;
        std::size_t debug_bbox_pass = 0;
        std::size_t debug_pip_pass = 0;
        if (it == result.data.grid.region_cells.end()) {
            continue;
        }

        std::unordered_set<std::size_t> dedup;
        dedup.reserve(it->second.size());

        struct CandidatePick {
            StringId id{kInvalidStringId};
            double area{std::numeric_limits<double>::infinity()};
        };
        CandidatePick city_pick{};
        CandidatePick state_pick{};
        CandidatePick country_pick{};
        CandidatePick postcode_pick{};

        const GeoPoint hp{.lat = house.lat, .lon = house.lon};
        for (const auto ridx : it->second) {
            if (!dedup.insert(ridx).second || ridx >= result.data.regions.size()) {
                continue;
            }
            ++pip_candidate_checks;
            const auto& region = result.data.regions[ridx];
            if (!region.bbox.contains(house.lon, house.lat)) {
                continue;
            }
            ++debug_bbox_pass;

            const auto begin = static_cast<std::size_t>(region.points_begin);
            const auto count = static_cast<std::size_t>(region.points_count);
            if (begin + count > result.data.region_points.size() || count < 3) {
                continue;
            }

            if (!point_in_polygon(hp, result.data.region_points.data() + begin, count)) {
                continue;
            }
            ++debug_pip_pass;

            result.data.house_containing_region_ids.push_back(static_cast<std::uint32_t>(ridx));
            ++house.containing_regions_count;

            const auto choose = [&](CandidatePick& pick) {
                if (region.name_id == kInvalidStringId) {
                    return;
                }
                if (region.approx_area < pick.area) {
                    pick.id = region.name_id;
                    pick.area = region.approx_area;
                }
            };

            if (region.admin_level == 8) choose(city_pick);
            if (region.admin_level == 4) choose(state_pick);
            if (region.admin_level == 2) choose(country_pick);
            if (region.is_postal_region) choose(postcode_pick);
        }

        if (house.city_id == kInvalidStringId && city_pick.id != kInvalidStringId) {
            house.city_id = city_pick.id;
        }
        if (house.state_id == kInvalidStringId && state_pick.id != kInvalidStringId) {
            house.state_id = state_pick.id;
        }
        bool assigned_country_from_pip = false;
        if (house.country_id == kInvalidStringId && country_pick.id != kInvalidStringId) {
            house.country_id = country_pick.id;
            assigned_country_from_pip = true;
        }
        if (house.postcode_id == kInvalidStringId && postcode_pick.id != kInvalidStringId) {
            house.postcode_id = postcode_pick.id;
        }

        if (house.country_id == kInvalidStringId && country_pick.id != kInvalidStringId) {
            const auto& picked_country = result.data.strings.resolve(country_pick.id);
            if (is_germany_name(picked_country)) {
                house.country_id = result.data.strings.intern("Deutschland");
            } else {
                house.country_id = country_pick.id;
            }
        }

        if (house.country_id == kInvalidStringId && house.state_id != kInvalidStringId) {
            const auto& state_name = result.data.strings.resolve(house.state_id);
            if (state_name == "Baden-Württemberg") {
                house.country_id = result.data.strings.intern("Deutschland");
                ++state_without_country_fallback_count;
                ++result.stats.country_assigned_by_fallback;
            }
        }

        (void)debug_house_logged;
        (void)debug_bbox_pass;
        (void)debug_pip_pass;

        if (house.city_id != kInvalidStringId) ++result.stats.houses_with_assigned_city;
        if (house.state_id != kInvalidStringId) ++result.stats.houses_with_assigned_state;
        if (house.country_id != kInvalidStringId) ++result.stats.houses_with_assigned_country;
        if (assigned_country_from_pip) ++result.stats.country_assigned_by_pip;
        if (house.postcode_id != kInvalidStringId) ++result.stats.houses_with_assigned_postcode;
    }

    const auto assign_end = std::chrono::steady_clock::now();
    result.stats.region_assignment_seconds =
        std::chrono::duration<double>(assign_end - assign_start).count();
    result.stats.avg_pip_candidates_per_house =
        result.data.houses.empty()
            ? 0.0
            : static_cast<double>(pip_candidate_checks) / static_cast<double>(result.data.houses.size());
    result.stats.spatial_index_cells = static_cast<std::uint64_t>(
        result.data.grid.house_cells.size() + result.data.grid.street_cells.size() + result.data.grid.region_cells.size());
    result.stats.reverse_index_build_seconds = 0.0;

    std::cout << "[CountryFallback] state_without_country_assigned: " << state_without_country_fallback_count << '\n';

    result.stats.estimated_memory_bytes =
        (result.data.houses.size() * sizeof(HousePoint)) +
        (result.data.streets.size() * sizeof(StreetPolyline)) +
        (result.data.street_points.size() * sizeof(GeoPoint)) +
        (result.data.pois.size() * sizeof(PoiPoint)) +
        (result.data.regions.size() * sizeof(RegionPolygon)) +
        (result.data.region_points.size() * sizeof(GeoPoint)) +
        (result.data.poi_containing_region_ids.size() * sizeof(std::uint32_t)) +
        estimate_string_pool_bytes(result.data.strings);

    result.stats.parse_seconds = stopwatch.elapsed_seconds();
    return result;
}

} // namespace osm

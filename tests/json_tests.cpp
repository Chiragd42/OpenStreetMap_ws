#include "model.hpp"
#include "cache/datastore_cache.hpp"
#include "query/json.hpp"
#include "search/geocode_query.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect_contains(const std::string& haystack, const std::string& needle, const std::string& label) {
    if (haystack.find(needle) == std::string::npos) {
        ++failures;
        std::cerr << "FAIL " << label << " missing: " << needle << '\n';
    }
}

void expect_not_contains(const std::string& haystack, const std::string& needle, const std::string& label) {
    if (haystack.find(needle) != std::string::npos) {
        ++failures;
        std::cerr << "FAIL " << label << " unexpectedly found: " << needle << '\n';
    }
}

std::uint32_t add_region(osm::DataStore& data, const std::string& name, const std::uint64_t relation_id, const std::int32_t admin_level) {
    osm::RegionPolygon region;
    region.name_id = data.strings.intern(name);
    region.source_relation_id = relation_id;
    region.admin_level = admin_level;
    data.regions.push_back(region);
    return static_cast<std::uint32_t>(data.regions.size() - 1);
}

void set_region_parents(osm::DataStore& data, const std::uint32_t region_index, std::initializer_list<std::uint32_t> parent_regions) {
    if (region_index >= data.regions.size()) {
        return;
    }
    auto& region = data.regions[region_index];
    region.containing_regions_begin = static_cast<std::uint32_t>(data.region_containing_region_ids.size());
    region.containing_regions_count = static_cast<std::uint32_t>(parent_regions.size());
    for (const auto parent_index : parent_regions) {
        data.region_containing_region_ids.push_back(parent_index);
    }
}

void add_house(osm::DataStore& data, std::initializer_list<std::uint32_t> containing_regions) {
    osm::HousePoint house;
    house.lat = 48.841F;
    house.lon = 10.091F;
    house.street_name_id = data.strings.intern("Bahnhofstraße");
    house.house_number_id = data.strings.intern("10");
    house.city_id = data.strings.intern("Aalen");
    house.state_id = data.strings.intern("Baden-Württemberg");
    house.postcode_id = data.strings.intern("73430");
    house.country_id = data.strings.intern("Deutschland");
    house.containing_regions_begin = static_cast<std::uint32_t>(data.house_containing_region_ids.size());
    house.containing_regions_count = static_cast<std::uint32_t>(containing_regions.size());
    for (const auto region_index : containing_regions) {
        data.house_containing_region_ids.push_back(region_index);
    }
    data.houses.push_back(house);
}

void add_poi(osm::DataStore& data, std::initializer_list<std::uint32_t> containing_regions) {
    osm::PoiPoint poi;
    poi.osm_id = 12345;
    poi.osm_type = osm::OsmElementType::Node;
    poi.lat = 48.8405F;
    poi.lon = 10.0905F;
    poi.name_id = data.strings.intern("Café Test");
    poi.subtype_id = data.strings.intern("cafe");
    poi.category = osm::PoiCategory::Cafe;
    poi.containing_regions_begin = static_cast<std::uint32_t>(data.poi_containing_region_ids.size());
    poi.containing_regions_count = static_cast<std::uint32_t>(containing_regions.size());
    for (const auto region_index : containing_regions) {
        data.poi_containing_region_ids.push_back(region_index);
    }
    data.pois.push_back(poi);
}

void add_street(osm::DataStore& data, std::initializer_list<std::uint32_t> containing_regions) {
    osm::StreetPolyline street;
    street.name_id = data.strings.intern("Bahnhofstraße");
    street.highway_class_id = data.strings.intern("residential");
    street.points_begin = static_cast<std::uint32_t>(data.street_points.size());
    street.points_count = 2;
    street.containing_regions_begin = static_cast<std::uint32_t>(data.street_containing_region_ids.size());
    street.containing_regions_count = static_cast<std::uint32_t>(containing_regions.size());
    for (const auto region_index : containing_regions) {
        data.street_containing_region_ids.push_back(region_index);
    }
    data.street_points.push_back(osm::GeoPoint{.lat = 48.84F, .lon = 10.09F});
    data.street_points.push_back(osm::GeoPoint{.lat = 48.85F, .lon = 10.10F});
    data.streets.push_back(street);
}

void reverse_json_includes_house_and_clicked_regions() {
    osm::DataStore data;
    const auto state = add_region(data, "Baden-Württemberg", 100, 4);
    const auto city = add_region(data, "Aalen", 200, 8);
    const auto district = add_region(data, "Ostalbkreis", 300, 6);
    add_house(data, {city, state});
    add_poi(data, {city, state});

    const auto json = osm::serialize_reverse_json(
        data,
        0,
        std::vector<std::size_t>{city, district, state},
        0,
        42.5,
        0,
        48.8402,
        10.0902,
        12.0,
        48.84,
        10.09,
        48.841,
        10.091,
        123.4,
        "Bahnhofstraße",
        "10",
        "Aalen",
        "Baden-Württemberg",
        "73430",
        "Deutschland");

    expect_contains(json, "\"containing_regions\":[{\"name\":\"Aalen\"", "nearest house regions stay available");
    expect_contains(json, "\"clicked_containing_regions\":[{\"name\":\"Aalen\",\"admin_level\":8,\"relation_id\":200},{\"name\":\"Ostalbkreis\",\"admin_level\":6,\"relation_id\":300},{\"name\":\"Baden-Württemberg\",\"admin_level\":4,\"relation_id\":100}]", "clicked regions are serialized in provided order");
    expect_contains(json, "\"nearest_poi\":{\"type\":\"poi\",\"name\":\"Café Test\",\"category\":\"cafe\",\"subtype\":\"cafe\",\"osm_type\":\"node\",\"osm_id\":12345", "nearest POI is serialized");
    expect_contains(json, "\"distance_m\":42.5,\"containing_regions\":[{\"name\":\"Aalen\"", "nearest POI distance and regions are serialized");
    expect_contains(json, "\"nearest_street\":null", "invalid nearest street index is serialized as null");
}

void reverse_json_skips_invalid_clicked_regions() {
    osm::DataStore data;
    const auto city = add_region(data, "Aalen", 200, 8);
    add_house(data, {city});

    const auto json = osm::serialize_reverse_json(
        data,
        0,
        std::vector<std::size_t>{static_cast<std::size_t>(9999)},
        std::nullopt,
        std::numeric_limits<double>::infinity(),
        std::nullopt,
        0.0,
        0.0,
        std::numeric_limits<double>::infinity(),
        48.84,
        10.09,
        48.841,
        10.091,
        123.4,
        "Bahnhofstraße",
        "10",
        "Aalen",
        "Baden-Württemberg",
        "73430",
        "Deutschland");

    expect_contains(json, "\"clicked_containing_regions\":[]", "invalid clicked regions are ignored");
    expect_contains(json, "\"nearest_poi\":null", "missing nearest POI is serialized as null");
    expect_contains(json, "\"nearest_street\":null", "missing nearest street is serialized as null");
    expect_not_contains(json, "9999", "invalid region index is not emitted");
}

void streets_json_includes_containing_regions() {
    osm::DataStore data;
    const auto state = add_region(data, "Baden-Württemberg", 100, 4);
    const auto city = add_region(data, "Aalen", 200, 8);
    add_street(data, {city, state});

    const auto json = osm::serialize_streets_json(data, std::vector<std::size_t>{0}, 1, true);

    expect_contains(json, "\"matched\":1,\"returned\":1,\"limited\":false", "street metadata remains available");
    expect_contains(json, "\"name\":\"Bahnhofstraße\",\"highway\":\"residential\",\"is_unnamed\":false", "street basics are serialized");
    expect_contains(json, "\"containing_regions\":[{\"name\":\"Aalen\",\"admin_level\":8,\"relation_id\":200},{\"name\":\"Baden-Württemberg\",\"admin_level\":4,\"relation_id\":100}]", "street containing regions are serialized");
}

void reverse_street_json_serializes_fallback_result() {
    osm::DataStore data;
    const auto state = add_region(data, "Baden-Württemberg", 100, 4);
    const auto city = add_region(data, "Aalen", 200, 8);
    add_street(data, {city, state});

    const auto json = osm::serialize_reverse_street_json(
        data,
        0,
        std::vector<std::size_t>{city, state},
        std::nullopt,
        std::numeric_limits<double>::infinity(),
        48.84,
        10.09,
        48.8401,
        10.0901,
        8.5);

    expect_contains(json, "\"nearest\":{\"type\":\"street\",\"name\":\"Bahnhofstraße\",\"highway\":\"residential\"", "street fallback nearest object is serialized");
    expect_contains(json, "\"distance_m\":8.5,\"containing_regions\":[{\"name\":\"Aalen\"", "street fallback distance and regions are serialized");
    expect_contains(json, "\"nearest_street\":{\"type\":\"street\",\"name\":\"Bahnhofstraße\"", "street fallback also exposes nearest_street context");
}

void regions_json_includes_parent_hierarchy() {
    osm::DataStore data;
    const auto country = add_region(data, "Deutschland", 10, 2);
    const auto state = add_region(data, "Baden-Württemberg", 100, 4);
    const auto district = add_region(data, "Ostalbkreis", 300, 6);
    const auto city = add_region(data, "Aalen", 200, 8);
    set_region_parents(data, city, {district, state, country});

    const auto json = osm::serialize_regions_json(data, std::vector<std::size_t>{city}, 1, true);

    expect_contains(json, "\"matched\":1,\"returned\":1,\"limited\":false", "region metadata remains available");
    expect_contains(json, "\"name\":\"Aalen\",\"admin_level\":8", "region basics are serialized");
    expect_contains(json, "\"containing_regions\":[{\"name\":\"Ostalbkreis\",\"admin_level\":6,\"relation_id\":300},{\"name\":\"Baden-Württemberg\",\"admin_level\":4,\"relation_id\":100},{\"name\":\"Deutschland\",\"admin_level\":2,\"relation_id\":10}]", "region parent hierarchy is serialized");
}

void reverse_region_json_serializes_fallback_result() {
    osm::DataStore data;
    const auto country = add_region(data, "Deutschland", 10, 2);
    const auto state = add_region(data, "Baden-Württemberg", 100, 4);
    const auto district = add_region(data, "Ostalbkreis", 300, 6);
    const auto city = add_region(data, "Aalen", 200, 8);
    set_region_parents(data, city, {district, state, country});

    const auto json = osm::serialize_reverse_region_json(
        data,
        city,
        std::vector<std::size_t>{city, district, state, country},
        std::nullopt,
        std::numeric_limits<double>::infinity(),
        48.84,
        10.09);

    expect_contains(json, "\"nearest\":{\"type\":\"region\",\"name\":\"Aalen\",\"admin_level\":8,\"relation_id\":200", "region fallback nearest object is serialized");
    expect_contains(json, "\"distance_m\":0,\"containing_regions\":[{\"name\":\"Ostalbkreis\"", "region fallback distance and hierarchy are serialized");
    expect_contains(json, "\"clicked_containing_regions\":[{\"name\":\"Aalen\",\"admin_level\":8", "region fallback keeps clicked context");
    expect_contains(json, "\"nearest_street\":null", "region fallback has no nearest street context");
}

void old_cache_version_is_rejected() {
    const std::string path = "/tmp/osm_geocoder_old_v5_cache.bin";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const std::uint32_t magic = 0x4F534D43;
        const std::uint32_t old_version = 5;
        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&old_version), sizeof(old_version));
    }

    osm::DataStore data;
    osm::ParseStats stats;
    std::string error;
    const bool loaded = osm::load_datastore_cache(path, data, stats, error);

    if (loaded) {
        ++failures;
        std::cerr << "FAIL old cache version should be rejected\n";
    }
    expect_contains(error, "Unsupported cache version: 5", "old v5 cache is rejected after v6 bump");
}

void geocode_json_includes_sheet3_metadata() {
    osm::DataStore data;
    add_poi(data, {});
    osm::search::GeocodeQueryResult result;
    result.input = "closest cafe to test";
    result.normalized_query = "closest cafe to test";
    result.nearest_category_intent = true;
    result.nearest_category = osm::PoiCategory::Cafe;
    result.viewport = osm::BBox{.min_lon = 8.0, .min_lat = 47.0, .max_lon = 10.0, .max_lat = 49.0};
    result.result_bounds = osm::BBox{.min_lon = 9.0, .min_lat = 48.0, .max_lon = 9.0, .max_lat = 48.0};
    result.reference_resolved = true;
    result.reference_label = "Test 1";
    result.reference_lat = 48.0;
    result.reference_lon = 9.0;
    result.spatial_cells_examined = 2;
    result.spatial_pois_tested = 1;
    osm::search::GeocodeCandidate candidate;
    candidate.ref = osm::SearchObjectRef{.type = osm::SearchObjectType::Poi, .index = 0};
    candidate.in_viewport = true;
    candidate.distance_to_viewport_center_m = 15.0;
    candidate.nearest_distance_m = 42.0;
    result.ranked_candidates.push_back(candidate);
    result.clusters.push_back(osm::search::GeocodeCluster{
        .representative_candidate_index = 0,
        .member_candidate_indices = {0},
        .lat = 48.0,
        .lon = 9.0,
    });

    const auto json = osm::serialize_geocode_json(data, result);
    expect_contains(json, "\"nearest_category_intent\":true", "nearest intent metadata serialized");
    expect_contains(json, "\"nearest_category\":\"cafe\"", "nearest category serialized");
    expect_contains(json, "\"viewport\":{\"min_lon\":8", "viewport bounds serialized");
    expect_contains(json, "\"result_bounds\":{\"min_lon\":9", "result bounds serialized");
    expect_contains(json, "\"category\":\"cafe\"", "POI category serialized");
    expect_contains(json, "\"in_viewport\":true", "viewport evidence serialized");
    expect_contains(json, "\"nearest_distance_m\":42", "nearest distance serialized");
    expect_contains(json, "\"clusters\":[{\"representative_index\":0", "clusters serialized");
}

} // namespace

int main() {
    reverse_json_includes_house_and_clicked_regions();
    reverse_json_skips_invalid_clicked_regions();
    streets_json_includes_containing_regions();
    reverse_street_json_serializes_fallback_result();
    regions_json_includes_parent_hierarchy();
    reverse_region_json_serializes_fallback_result();
    old_cache_version_is_rejected();
    geocode_json_includes_sheet3_metadata();

    if (failures != 0) {
        return EXIT_FAILURE;
    }

    std::cout << "All JSON tests passed\n";
    return EXIT_SUCCESS;
}
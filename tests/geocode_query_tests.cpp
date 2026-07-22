#include "model.hpp"
#include "search/geocode_query.hpp"
#include "search/search_index.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_true(const bool value, const std::string& label) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL " << label << '\n';
    }
}

void expect_eq_u32(const std::uint32_t actual, const std::uint32_t expected, const std::string& label) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL " << label << " expected " << expected << " actual " << actual << '\n';
    }
}

void expect_eq_i32(const std::int32_t actual, const std::int32_t expected, const std::string& label) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL " << label << " expected " << expected << " actual " << actual << '\n';
    }
}

void expect_eq_string(const std::string& actual, const std::string& expected, const std::string& label) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL " << label << " expected " << expected << " actual " << actual << '\n';
    }
}

std::uint32_t add_region(osm::DataStore& data, const std::string& name, const std::uint64_t relation_id, const std::int32_t admin_level, const bool postal = false) {
    osm::RegionPolygon region;
    region.name_id = data.strings.intern(name);
    region.source_relation_id = relation_id;
    region.admin_level = admin_level;
    region.is_postal_region = postal;
    data.regions.push_back(region);
    return static_cast<std::uint32_t>(data.regions.size() - 1);
}

std::uint32_t add_locality(osm::DataStore& data, const std::string& name, const float lat, const float lon, std::initializer_list<std::uint32_t> regions) {
    osm::LocalityPoint locality;
    locality.name_id = data.strings.intern(name);
    locality.lat = lat;
    locality.lon = lon;
    locality.containing_regions_begin = static_cast<std::uint32_t>(data.locality_containing_region_ids.size());
    locality.containing_regions_count = static_cast<std::uint32_t>(regions.size());
    for (const auto idx : regions) {
        data.locality_containing_region_ids.push_back(idx);
    }
    data.localities.push_back(locality);
    return static_cast<std::uint32_t>(data.localities.size() - 1);
}

std::uint32_t add_house(osm::DataStore& data, const std::string& street, const std::string& number, const std::string& city, const float lat, const float lon, std::initializer_list<std::uint32_t> regions) {
    osm::HousePoint house;
    house.street_name_id = data.strings.intern(street);
    house.house_number_id = data.strings.intern(number);
    house.city_id = data.strings.intern(city);
    house.lat = lat;
    house.lon = lon;
    house.containing_regions_begin = static_cast<std::uint32_t>(data.house_containing_region_ids.size());
    house.containing_regions_count = static_cast<std::uint32_t>(regions.size());
    for (const auto idx : regions) {
        data.house_containing_region_ids.push_back(idx);
    }
    data.houses.push_back(house);
    return static_cast<std::uint32_t>(data.houses.size() - 1);
}

std::uint32_t add_poi(osm::DataStore& data, const std::string& name, const float lat, const float lon, std::initializer_list<std::uint32_t> regions) {
    osm::PoiPoint poi;
    poi.name_id = data.strings.intern(name);
    poi.category = osm::PoiCategory::FastFood;
    poi.lat = lat;
    poi.lon = lon;
    poi.containing_regions_begin = static_cast<std::uint32_t>(data.poi_containing_region_ids.size());
    poi.containing_regions_count = static_cast<std::uint32_t>(regions.size());
    for (const auto idx : regions) {
        data.poi_containing_region_ids.push_back(idx);
    }
    data.pois.push_back(poi);
    return static_cast<std::uint32_t>(data.pois.size() - 1);
}

std::uint32_t add_street(osm::DataStore& data, const std::string& name, const float lat, const float lon, std::initializer_list<std::uint32_t> regions) {
    osm::StreetPolyline street;
    street.name_id = data.strings.intern(name);
    street.highway_class_id = data.strings.intern("residential");
    street.points_begin = static_cast<std::uint32_t>(data.street_points.size());
    street.points_count = 2;
    street.containing_regions_begin = static_cast<std::uint32_t>(data.street_containing_region_ids.size());
    street.containing_regions_count = static_cast<std::uint32_t>(regions.size());
    street.bbox.min_lat = static_cast<double>(lat) - 0.001;
    street.bbox.max_lat = static_cast<double>(lat) + 0.001;
    street.bbox.min_lon = static_cast<double>(lon) - 0.001;
    street.bbox.max_lon = static_cast<double>(lon) + 0.001;
    for (const auto idx : regions) {
        data.street_containing_region_ids.push_back(idx);
    }
    data.street_points.push_back(osm::GeoPoint{.lat = lat, .lon = lon});
    data.street_points.push_back(osm::GeoPoint{.lat = static_cast<float>(lat + 0.001F), .lon = static_cast<float>(lon + 0.001F)});
    data.streets.push_back(street);
    return static_cast<std::uint32_t>(data.streets.size() - 1);
}

osm::DataStore make_test_data() {
    osm::DataStore data;
    const auto bw = add_region(data, "Baden-Württemberg", 100, 4);
    const auto aalen = add_region(data, "Aalen", 200, 8);
    const auto stuttgart = add_region(data, "Stuttgart", 300, 6);
    const auto bad_homburg = add_region(data, "Bad Homburg", 400, 8);
    const auto schopfheim = add_region(data, "Schopfheim", 500, 8);
    const auto neustadt_a = add_region(data, "Neustadt", 600, 8);
    const auto neustadt_b = add_region(data, "Neustadt", 700, 8);
    const auto postal = add_region(data, "70173", 800, 8, true);
    const auto fragment_1 = add_region(data, "Fragmentstadt", 900, 8);
    const auto fragment_2 = add_region(data, "Fragmentstadt", 900, 8);

    add_locality(data, "Aalen", 48.84F, 10.09F, {aalen, bw});
    add_locality(data, "Stuttgart", 48.78F, 9.18F, {stuttgart, bw});
    add_locality(data, "Bad Homburg", 50.22F, 8.61F, {bad_homburg, bw});
    add_locality(data, "Schopfheim", 47.65F, 7.82F, {schopfheim, bw});
    add_locality(data, "Neustadt", 48.00F, 8.00F, {neustadt_a, bw});
    add_locality(data, "Neustadt", 49.00F, 9.00F, {neustadt_b, bw});
    add_locality(data, "Fragmentstadt", 48.50F, 8.50F, {fragment_1, fragment_2, bw});

    add_house(data, "Bahnhofstraße", "10", "Aalen", 48.841F, 10.091F, {aalen, bw});
    add_house(data, "Bahnhofstraße", "10", "Stuttgart", 48.781F, 9.181F, {stuttgart, bw, postal});
    add_house(data, "Königstraße", "1", "Stuttgart", 48.782F, 9.182F, {stuttgart, bw, postal});
    add_house(data, "Hauptstraße", "10", "Bad Homburg", 50.221F, 8.611F, {bad_homburg, bw});
    add_house(data, "Bahnhofstraße", "10 A", "Schopfheim", 47.651F, 7.821F, {schopfheim, bw});
    add_house(data, "Bahnhofstraße", "10", "Neustadt A", 48.001F, 8.001F, {neustadt_a, bw});
    add_house(data, "Bahnhofstraße", "10", "Neustadt B", 49.001F, 9.001F, {neustadt_b, bw});
    add_house(data, "Bahnhofstraße", "10", "Fragmentstadt", 48.501F, 8.501F, {fragment_1, fragment_2, bw});

    add_poi(data, "Burger King", 48.783F, 9.183F, {stuttgart, bw});
    add_poi(data, "Burger King", 48.843F, 10.093F, {aalen, bw});
    add_poi(data, "Studio 54", 48.784F, 9.184F, {stuttgart, bw});

    add_street(data, "Marktstraße", 48.845F, 10.095F, {stuttgart, bw});
    add_street(data, "Marktstraße", 48.780F, 9.180F, {aalen, bw});
    return data;
}

} // namespace

int main() {
    auto data = make_test_data();
    const auto built = osm::search::buildSearchIndex(data);
    const auto& index = built.index;

    const auto aalen_first = osm::search::runGeocodeQuery(data, index, "Aalen Bahnhofstrasse 10");
    expect_true(!aalen_first.ranked_candidates.empty(), "aalen first has candidates");
    expect_eq_u32(aalen_first.ranked_candidates.front().ref.index, 0, "aalen first top house");
    expect_eq_i32(aalen_first.ranked_candidates.front().shared_admin_level, 8, "aalen first level 8 match");

    const auto aalen_last = osm::search::runGeocodeQuery(data, index, "Bahnhofstrasse 10 Aalen");
    expect_true(!aalen_last.ranked_candidates.empty(), "aalen last has candidates");
    expect_eq_u32(aalen_last.ranked_candidates.front().ref.index, 0, "aalen last top house");

    const auto stuttgart = osm::search::runGeocodeQuery(data, index, "Stuttgart Königstrasse 1");
    expect_true(!stuttgart.ranked_candidates.empty(), "stuttgart has candidates");
    expect_eq_u32(stuttgart.ranked_candidates.front().ref.index, 2, "stuttgart top house");
    expect_eq_i32(stuttgart.ranked_candidates.front().shared_admin_level, 6, "stuttgart level 6 match");

    const auto bad_homburg = osm::search::runGeocodeQuery(data, index, "Bad Homburg Hauptstrasse 10");
    expect_true(!bad_homburg.ranked_candidates.empty(), "bad homburg has candidates");
    expect_eq_u32(bad_homburg.ranked_candidates.front().ref.index, 3, "bad homburg top house");

    const auto suffix_space = osm::search::runGeocodeQuery(data, index, "Schopfheim Bahnhofstrasse 10 A");
    const auto suffix_joined = osm::search::runGeocodeQuery(data, index, "Bahnhofstrasse 10A Schopfheim");
    expect_true(!suffix_space.ranked_candidates.empty(), "suffix space has candidates");
    expect_true(!suffix_joined.ranked_candidates.empty(), "suffix joined has candidates");
    expect_eq_u32(suffix_space.ranked_candidates.front().ref.index, 4, "suffix space top house");
    expect_eq_u32(suffix_joined.ranked_candidates.front().ref.index, 4, "suffix joined top house");

    const auto unknown = osm::search::runGeocodeQuery(data, index, "CompletelyUnknownTown Bahnhofstrasse 10");
    expect_true(!unknown.ranked_candidates.empty(), "unknown locality still returns global address candidates");
    expect_eq_string(unknown.interpretations.front().entity_name, "bahnhofstrasse", "unknown fallback preferred entity");
    expect_true(unknown.interpretations.front().unexplained_token_count == 1, "unknown fallback has one unexplained token");

    const auto burger = osm::search::runGeocodeQuery(data, index, "Stuttgart Burger King");
    expect_true(!burger.ranked_candidates.empty(), "burger has candidates");
    expect_eq_u32(burger.ranked_candidates.front().ref.index, 0, "stuttgart burger top poi");
    expect_eq_i32(burger.ranked_candidates.front().shared_admin_level, 6, "stuttgart burger level 6 match");

    const auto studio = osm::search::runGeocodeQuery(data, index, "Studio 54");
    expect_true(!studio.ranked_candidates.empty(), "studio has candidates");
    expect_true(studio.ranked_candidates.front().ref.type == osm::SearchObjectType::Poi, "studio exact named object beats weak address");
    expect_eq_u32(studio.ranked_candidates.front().ref.index, 2, "studio top poi");

    const auto typo_address = osm::search::runGeocodeQuery(data, index, "Aalen Bahnofstrasse 10");
    expect_true(!typo_address.ranked_candidates.empty(), "typo address has candidates");
    expect_eq_u32(typo_address.ranked_candidates.front().ref.index, 0, "typo address top house");
    expect_true(typo_address.ranked_candidates.front().match_strategy == osm::search::QueryMatchStrategy::Fuzzy, "typo address marked fuzzy");
    expect_eq_string(typo_address.interpretations.front().entity_name, "bahnhofstrasse", "typo address corrected street interpretation");

    const auto typo_poi = osm::search::runGeocodeQuery(data, index, "Stutgart Burgar King");
    expect_true(!typo_poi.ranked_candidates.empty(), "typo poi has candidates");
    expect_true(typo_poi.ranked_candidates.front().ref.type == osm::SearchObjectType::Poi, "typo poi returns poi");
    expect_eq_u32(typo_poi.ranked_candidates.front().ref.index, 0, "typo poi top stuttgart burger king");
    expect_eq_i32(typo_poi.ranked_candidates.front().shared_admin_level, 6, "typo poi level 6 locality match");
    expect_true(typo_poi.ranked_candidates.front().match_strategy == osm::search::QueryMatchStrategy::Fuzzy, "typo poi marked fuzzy");

    const auto partial_address = osm::search::runGeocodeQuery(data, index, "Aalen Bahn 10");
    expect_true(!partial_address.ranked_candidates.empty(), "partial address has candidates");
    expect_eq_u32(partial_address.ranked_candidates.front().ref.index, 0, "partial address top house");
    expect_true(partial_address.ranked_candidates.front().match_strategy == osm::search::QueryMatchStrategy::Partial, "partial address marked partial");
    expect_eq_string(partial_address.interpretations.front().entity_name, "bahnhofstrasse", "partial address completed street interpretation");

    const auto partial_poi = osm::search::runGeocodeQuery(data, index, "Stuttgart Burger Ki");
    expect_true(!partial_poi.ranked_candidates.empty(), "partial poi has candidates");
    expect_true(partial_poi.ranked_candidates.front().ref.type == osm::SearchObjectType::Poi, "partial poi returns poi");
    expect_eq_u32(partial_poi.ranked_candidates.front().ref.index, 0, "partial poi top stuttgart burger king");
    expect_true(partial_poi.ranked_candidates.front().match_strategy == osm::search::QueryMatchStrategy::Partial, "partial poi marked partial");

    const auto neustadt = osm::search::runGeocodeQuery(data, index, "Neustadt Bahnhofstrasse 10");
    expect_true(!neustadt.ranked_candidates.empty(), "duplicate locality has candidates");
    expect_eq_i32(neustadt.ranked_candidates.front().shared_admin_level, 8, "duplicate locality level 8 match");

    const auto fragment = osm::search::runGeocodeQuery(data, index, "Fragmentstadt Bahnhofstrasse 10");
    expect_true(!fragment.ranked_candidates.empty(), "fragmentstadt has candidates");
    expect_eq_u32(fragment.ranked_candidates.front().ref.index, 7, "fragmentstadt top house");
    expect_eq_i32(fragment.ranked_candidates.front().shared_admin_level, 8, "fragmentstadt duplicate relation level 8 match");

    const auto street = osm::search::runGeocodeQuery(data, index, "Aalen Marktstrasse");
    expect_true(!street.ranked_candidates.empty(), "street locality has candidates");
    expect_true(street.ranked_candidates.front().ref.type == osm::SearchObjectType::Street, "street locality returns street");
    expect_eq_u32(street.ranked_candidates.front().ref.index, 1, "street locality top street uses street regions");
    expect_eq_i32(street.ranked_candidates.front().shared_admin_level, 8, "street locality level 8 match");

    const auto region = osm::search::runGeocodeQuery(data, index, "Baden Wuerttemberg");
    expect_true(!region.ranked_candidates.empty(), "region name has candidates");
    expect_true(region.ranked_candidates.front().ref.type == osm::SearchObjectType::Region, "region name returns region candidate");
    expect_eq_u32(region.ranked_candidates.front().ref.index, 0, "region name top region");
    expect_true(region.ranked_candidates.front().exact_name_match, "region name exact match is recorded");

    if (failures != 0) {
        std::cerr << failures << " geocode query test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All geocode query tests passed\n";
    return EXIT_SUCCESS;
}
#include "model.hpp"
#include "search/search_index.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect_true(const bool value, const std::string& label) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL " << label << '\n';
    }
}

void expect_eq_size(const std::size_t actual, const std::size_t expected, const std::string& label) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL " << label << " expected " << expected << " actual " << actual << '\n';
    }
}

} // namespace

int main() {
    osm::DataStore data;

    const auto burger = data.strings.intern("Burger King");
    const auto burger2 = data.strings.intern("BURGER KING");
    const auto koenig = data.strings.intern("Königstraße");
    const auto oberer = data.strings.intern("Oberer Grundweg");
    const auto stuttgart = data.strings.intern("Stuttgart");
    const auto bahnhof = data.strings.intern("Bahnhofstraße");
    const auto house10 = data.strings.intern("10 A");

    osm::StreetPolyline unnamed;
    unnamed.is_unnamed = true;
    data.streets.push_back(unnamed);

    osm::StreetPolyline s1;
    s1.name_id = koenig;
    data.streets.push_back(s1);

    osm::StreetPolyline s2;
    s2.name_id = oberer;
    data.streets.push_back(s2);

    osm::PoiPoint p1;
    p1.name_id = burger;
    p1.category = osm::PoiCategory::FastFood;
    data.pois.push_back(p1);

    osm::PoiPoint p2;
    p2.name_id = burger2;
    p2.category = osm::PoiCategory::FastFood;
    data.pois.push_back(p2);

    osm::RegionPolygon r1;
    r1.name_id = stuttgart;
    data.regions.push_back(r1);

    osm::HousePoint h1;
    h1.street_name_id = bahnhof;
    h1.house_number_id = house10;
    data.houses.push_back(h1);

    auto built = osm::search::buildSearchIndex(data);
    const auto& index = built.index;
    const auto& metrics = built.metrics;

    expect_eq_size(metrics.indexed_streets, 2, "indexed named streets");
    expect_eq_size(metrics.skipped_unnamed_streets, 1, "skipped unnamed street");
    expect_eq_size(metrics.indexed_pois, 2, "indexed pois");
    expect_eq_size(metrics.indexed_regions, 1, "indexed regions");
    expect_eq_size(metrics.indexed_addresses, 1, "indexed address");

    expect_eq_size(index.exact_name_index.at("burger king").size(), 2, "exact burger king postings");
    expect_eq_size(index.exact_name_index.at("koenigstrasse").size(), 1, "exact koenigstrasse postings");
    expect_eq_size(index.token_index.at("burger").size(), 2, "burger token postings");
    expect_eq_size(index.token_index.at("king").size(), 2, "king token postings");
    expect_eq_size(index.token_index.at("oberer").size(), 1, "oberer token postings");
    expect_eq_size(index.token_index.at("grundweg").size(), 1, "grundweg token postings");
    expect_eq_size(index.region_name_index.at("stuttgart").size(), 1, "stuttgart region postings");
    const auto address_key = osm::search::makeAddressKey("bahnhofstrasse", "10a");
    expect_eq_size(index.address_index.at(address_key).size(), 1, "address postings");

    const auto intersection = osm::search::intersectPostingLists({index.token_index.at("king"), index.token_index.at("burger")});
    expect_eq_size(intersection.size(), 2, "token intersection order independent");
    expect_true(intersection[0] < intersection[1], "intersection sorted");

    if (failures != 0) {
        std::cerr << failures << " search index test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All search index tests passed\n";
    return EXIT_SUCCESS;
}

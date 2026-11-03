#include "osm_extractor.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

const sheet1::Building* find_building(const sheet1::DataStore& data,
                                      const sheet1::OsmObjectType type,
                                      const std::int64_t id) {
    const auto found = std::find_if(
        data.buildings.begin(),
        data.buildings.end(),
        [&](const sheet1::Building& building) {
            return building.id.type == type && building.id.value == id;
        });
    return found == data.buildings.end() ? nullptr : &*found;
}

bool expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool close_to(const std::int32_t actual, const std::int32_t expected) {
    return std::abs(static_cast<std::int64_t>(actual) - expected) <= 1;
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Expected one OSM fixture path\n";
        return 2;
    }

    const auto result = sheet1::extract_pbf(std::filesystem::path{argv[1]});
    const auto* complete = find_building(
        result.data, sheet1::OsmObjectType::relation, 200);
    const auto* incomplete = find_building(
        result.data, sheet1::OsmObjectType::relation, 201);
    const auto* building_relation = find_building(
        result.data, sheet1::OsmObjectType::relation, 202);
    const auto* unaddressed = find_building(
        result.data, sheet1::OsmObjectType::way, 300);

    bool okay = true;
    okay &= expect(result.data.buildings.size() == 4,
                   "Expected exactly four building records without duplicates");
    okay &= expect(complete != nullptr, "Missing complete building relation");
    okay &= expect(incomplete != nullptr, "Missing incomplete building relation");
    okay &= expect(building_relation != nullptr, "Missing type=building relation");
    okay &= expect(unaddressed != nullptr, "Missing unaddressed building way");

    if (complete != nullptr) {
        okay &= expect(complete->has_point,
                       "Complete building relation has no representative point");
        okay &= expect(close_to(complete->point.latitude_e7, 480'050'000),
                       "Unexpected building-relation latitude");
        okay &= expect(close_to(complete->point.longitude_e7, 90'050'000),
                       "Unexpected building-relation longitude");
        okay &= expect(result.data.strings.resolve(complete->name) == "Testgebäude",
                       "German building-relation name was not selected");
        okay &= expect(result.data.strings.resolve(complete->street) == "Teststraße",
                       "Building-relation address was not retained");
    }
    if (incomplete != nullptr) {
        okay &= expect(!incomplete->has_point,
                       "Incomplete building relation unexpectedly has a point");
    }
    if (building_relation != nullptr) {
        okay &= expect(building_relation->has_point,
                       "type=building relation has no representative point");
        okay &= expect(close_to(building_relation->point.latitude_e7, 500'020'000),
                       "Unexpected type=building relation latitude");
        okay &= expect(close_to(building_relation->point.longitude_e7, 110'020'000),
                       "Unexpected type=building relation longitude");
    }
    if (unaddressed != nullptr) {
        okay &= expect(unaddressed->has_point,
                       "Unaddressed building way has no representative point");
        okay &= expect(unaddressed->house_number == sheet1::kInvalidStringId,
                       "Unaddressed building unexpectedly has a house number");
    }

    okay &= expect(result.stats.extracted_buildings == 4,
                   "Unexpected extracted-building count");
    okay &= expect(result.stats.buildings_without_points == 1,
                   "Expected one building without a representative point");
    okay &= expect(result.stats.buildings_without_names == 1,
                   "Expected one unnamed building");
    return okay ? 0 : 1;
}

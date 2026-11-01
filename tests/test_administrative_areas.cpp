#include "osm_extractor.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {

const sheet1::AdministrativeArea* find_area(const sheet1::DataStore& data,
                                            const sheet1::OsmObjectType type,
                                            const std::int64_t id) {
    const auto found = std::find_if(
        data.administrative_areas.begin(),
        data.administrative_areas.end(),
        [&](const sheet1::AdministrativeArea& area) {
            return area.id.type == type && area.id.value == id;
        });
    return found == data.administrative_areas.end() ? nullptr : &*found;
}

bool expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Expected one OSM fixture path\n";
        return 2;
    }

    const auto result = sheet1::extract_pbf(std::filesystem::path{argv[1]});
    const auto* relation = find_area(result.data, sheet1::OsmObjectType::relation, 200);
    const auto* incomplete = find_area(result.data, sheet1::OsmObjectType::relation, 201);
    const auto* standalone = find_area(result.data, sheet1::OsmObjectType::way, 300);

    bool okay = true;
    okay &= expect(relation != nullptr, "Missing complete administrative relation");
    okay &= expect(incomplete != nullptr, "Missing incomplete administrative relation metadata");
    okay &= expect(standalone != nullptr, "Missing standalone administrative way");
    if (relation != nullptr) {
        okay &= expect(relation->has_geometry, "Complete relation has no geometry");
        okay &= expect(relation->outer_ring_count == 1, "Expected one assembled outer ring");
        okay &= expect(relation->inner_ring_count == 1, "Expected one inner ring");
        okay &= expect(relation->rings_count == 2, "Expected two stored rings");
        okay &= expect(result.data.strings.resolve(relation->name) == "Testbezirk",
                       "German relation name was not selected");
    }
    if (incomplete != nullptr) {
        okay &= expect(!incomplete->has_geometry,
                       "Incomplete relation unexpectedly has geometry");
    }
    if (standalone != nullptr) {
        okay &= expect(standalone->has_geometry,
                       "Standalone administrative way has no geometry");
        okay &= expect(standalone->outer_ring_count == 1,
                       "Standalone way should have one outer ring");
    }

    okay &= expect(result.stats.incomplete_administrative_relations == 1,
                   "Expected one incomplete administrative relation");
    okay &= expect(result.stats.administrative_outer_rings == 2,
                   "Expected two outer rings in total");
    okay &= expect(result.stats.administrative_inner_rings == 1,
                   "Expected one inner ring in total");
    return okay ? 0 : 1;
}

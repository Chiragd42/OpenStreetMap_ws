#include "viewport_query.hpp"

#include <algorithm>
#include <chrono>

namespace sheet1 {

ViewportQueryResult query_buildings_in_viewport(const DataStore& data,
                                                const BoundingBox& viewport,
                                                const std::size_t threshold) {
    const auto started = std::chrono::steady_clock::now();

    ViewportQueryResult result;
    result.building_indices.reserve(std::min(threshold, data.buildings.size()));

    for (std::size_t index = 0; index < data.buildings.size(); ++index) {
        const auto& building = data.buildings[index];
        if (!building.has_point) {
            continue;
        }

        const auto point = building.point;
        const bool inside =
            point.longitude_e7 >= viewport.minimum.longitude_e7 &&
            point.longitude_e7 <= viewport.maximum.longitude_e7 &&
            point.latitude_e7 >= viewport.minimum.latitude_e7 &&
            point.latitude_e7 <= viewport.maximum.latitude_e7;
        if (!inside) {
            continue;
        }

        ++result.matched;
        if (result.building_indices.size() < threshold) {
            result.building_indices.push_back(index);
        }
    }

    result.threshold_reached = result.matched > result.building_indices.size();
    result.scan_milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return result;
}

} // namespace sheet1

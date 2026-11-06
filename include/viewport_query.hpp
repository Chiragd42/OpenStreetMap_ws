#pragma once

#include "osm_model.hpp"

#include <cstddef>
#include <vector>

namespace sheet1 {

struct ViewportQueryResult {
    std::vector<std::size_t> building_indices;
    std::size_t matched{0};
    bool threshold_reached{false};
    double scan_milliseconds{0.0};
};

[[nodiscard]] ViewportQueryResult query_buildings_in_viewport(
    const DataStore& data,
    const BoundingBox& viewport,
    std::size_t threshold);

} // namespace sheet1

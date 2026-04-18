#pragma once

#include "model.hpp"

#include <cstddef>
#include <vector>

namespace osm {

struct BBoxQueryResult {
    std::vector<std::size_t> house_indices;
    std::vector<std::size_t> street_indices;
};

[[nodiscard]] std::vector<std::size_t> query_houses_in_bbox(const DataStore& data, const BBox& bbox);
[[nodiscard]] std::vector<std::size_t> query_streets_in_bbox(const DataStore& data, const BBox& bbox);
[[nodiscard]] BBoxQueryResult query_all_in_bbox(const DataStore& data, const BBox& bbox);

} // namespace osm

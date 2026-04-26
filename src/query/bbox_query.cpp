#include "query/bbox_query.hpp"

#include <unordered_set>

namespace osm {

namespace {

template <typename Predicate>
std::vector<std::size_t> query_from_grid(
    const std::unordered_map<GridCellKey, std::vector<std::size_t>, GridCellKeyHash>& grid_cells,
    const BBox& bbox,
    const float cell_size_deg,
    Predicate&& predicate) {
    std::vector<std::size_t> result;
    std::unordered_set<std::size_t> seen;

    const auto cells = grid_cells_for_bbox(bbox, cell_size_deg);
    for (const auto& cell : cells) {
        const auto it = grid_cells.find(cell);
        if (it == grid_cells.end()) {
            continue;
        }

        for (const auto idx : it->second) {
            if (!seen.insert(idx).second) {
                continue;
            }
            if (predicate(idx)) {
                result.push_back(idx);
            }
        }
    }

    return result;
}

[[nodiscard]] bool bbox_intersects(const BBox& a, const BBox& b) {
    if (a.max_lon < b.min_lon || b.max_lon < a.min_lon) {
        return false;
    }
    if (a.max_lat < b.min_lat || b.max_lat < a.min_lat) {
        return false;
    }
    return true;
}

} // namespace

std::vector<std::size_t> query_houses_in_bbox(const DataStore& data, const BBox& bbox) {
    return query_from_grid(
        data.grid.house_cells,
        bbox,
        data.grid.cell_size_deg,
        [&](const std::size_t idx) {
            if (idx >= data.houses.size()) {
                return false;
            }
            const auto& h = data.houses[idx];
            return bbox.contains(h.lon, h.lat);
        });
}

std::vector<std::size_t> query_streets_in_bbox(const DataStore& data, const BBox& bbox) {
    return query_from_grid(
        data.grid.street_cells,
        bbox,
        data.grid.cell_size_deg,
        [&](const std::size_t idx) {
            if (idx >= data.streets.size()) {
                return false;
            }
            return bbox_intersects(data.streets[idx].bbox, bbox);
        });
}

std::vector<std::size_t> query_regions_in_bbox(const DataStore& data, const BBox& bbox) {
    return query_from_grid(
        data.grid.region_cells,
        bbox,
        data.grid.cell_size_deg,
        [&](const std::size_t idx) {
            if (idx >= data.regions.size()) {
                return false;
            }
            return bbox_intersects(data.regions[idx].bbox, bbox);
        });
}

BBoxQueryResult query_all_in_bbox(const DataStore& data, const BBox& bbox) {
    BBoxQueryResult result;
    result.house_indices = query_houses_in_bbox(data, bbox);
    result.street_indices = query_streets_in_bbox(data, bbox);
    result.region_indices = query_regions_in_bbox(data, bbox);
    return result;
}

} // namespace osm

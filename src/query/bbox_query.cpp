#include "query/bbox_query.hpp"

namespace osm {

std::vector<std::size_t> query_houses_in_bbox(const DataStore& data, const BBox& bbox) {
    std::vector<std::size_t> matches;
    matches.reserve(data.houses.size());

    for (std::size_t i = 0; i < data.houses.size(); ++i) {
        const auto& house = data.houses[i];
        if (bbox.contains(house.lon, house.lat)) {
            matches.push_back(i);
        }
    }

    return matches;
}

std::vector<std::size_t> query_streets_in_bbox(const DataStore& data, const BBox& bbox) {
    std::vector<std::size_t> matches;
    matches.reserve(data.streets.size());

    for (std::size_t i = 0; i < data.streets.size(); ++i) {
        const auto& street = data.streets[i];

        bool intersects = false;
        for (std::uint32_t j = 0; j < street.points_count; ++j) {
            const auto idx = static_cast<std::size_t>(street.points_begin + j);
            if (idx >= data.street_points.size()) {
                break;
            }

            const auto& p = data.street_points[idx];
            if (bbox.contains(p.lon, p.lat)) {
                intersects = true;
                break;
            }
        }

        if (intersects) {
            matches.push_back(i);
        }
    }

    return matches;
}

BBoxQueryResult query_all_in_bbox(const DataStore& data, const BBox& bbox) {
    BBoxQueryResult result;
    result.house_indices = query_houses_in_bbox(data, bbox);
    result.street_indices = query_streets_in_bbox(data, bbox);
    return result;
}

} // namespace osm

#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace osm {

namespace {

[[nodiscard]] bool point_in_polygon(const GeoPoint& point, const GeoPoint* polygon, const std::size_t n) {
    if (n < 3) {
        return false;
    }

    const double x = static_cast<double>(point.lon);
    const double y = static_cast<double>(point.lat);
    bool inside = false;

    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = static_cast<double>(polygon[i].lon);
        const double yi = static_cast<double>(polygon[i].lat);
        const double xj = static_cast<double>(polygon[j].lon);
        const double yj = static_cast<double>(polygon[j].lat);

        const bool intersect = ((yi > y) != (yj > y)) &&
                               (x < (xj - xi) * (y - yi) / ((yj - yi) + 1e-15) + xi);
        if (intersect) {
            inside = !inside;
        }
    }

    return inside;
}

} // namespace

bool BBox::contains(double lon, double lat) const noexcept {
    return lon >= min_lon && lon <= max_lon && lat >= min_lat && lat <= max_lat;
}

StringId StringPool::intern(std::string_view value) {
    const auto existing = index_by_value_.find(std::string(value));
    if (existing != index_by_value_.end()) {
        return existing->second;
    }

    const auto id = static_cast<StringId>(values_.size());
    values_.emplace_back(value);
    index_by_value_.emplace(values_.back(), id);
    return id;
}

const std::string& StringPool::resolve(StringId id) const {
    if (id >= values_.size()) {
        throw std::out_of_range("Invalid StringId");
    }
    return values_[id];
}

std::size_t StringPool::size() const noexcept {
    return values_.size();
}

const std::vector<std::string>& StringPool::values() const noexcept {
    return values_;
}

void StringPool::reset_from_values(std::vector<std::string> values) {
    values_ = std::move(values);
    index_by_value_.clear();
    index_by_value_.reserve(values_.size());
    for (std::size_t i = 0; i < values_.size(); ++i) {
        index_by_value_.emplace(values_[i], static_cast<StringId>(i));
    }
}

GridCellKey to_grid_cell(const double lon, const double lat, const float cell_size_deg) {
    const auto scale = static_cast<double>(cell_size_deg);
    GridCellKey key;
    key.x = static_cast<std::int32_t>(std::floor(lon / scale));
    key.y = static_cast<std::int32_t>(std::floor(lat / scale));
    return key;
}

std::vector<GridCellKey> grid_cells_for_bbox(const BBox& bbox, const float cell_size_deg) {
    const auto min_cell = to_grid_cell(bbox.min_lon, bbox.min_lat, cell_size_deg);
    const auto max_cell = to_grid_cell(bbox.max_lon, bbox.max_lat, cell_size_deg);

    std::vector<GridCellKey> cells;
    const auto width = static_cast<std::size_t>(max_cell.x - min_cell.x + 1);
    const auto height = static_cast<std::size_t>(max_cell.y - min_cell.y + 1);
    cells.reserve(width * height);

    for (std::int32_t x = min_cell.x; x <= max_cell.x; ++x) {
        for (std::int32_t y = min_cell.y; y <= max_cell.y; ++y) {
            cells.push_back(GridCellKey{.x = x, .y = y});
        }
    }
    return cells;
}

bool is_useful_admin_level(const std::int32_t admin_level) noexcept {
    return admin_level == 8 || admin_level == 6 || admin_level == 4 || admin_level == 2;
}

std::vector<std::size_t> find_containing_regions_for_point(
    const DataStore& data,
    const double lat,
    const double lon,
    const RegionLookupOptions& options) {
    std::vector<std::size_t> region_indices;
    const auto cell = to_grid_cell(lon, lat, data.grid.cell_size_deg);
    const auto it = data.grid.region_cells.find(cell);
    if (it == data.grid.region_cells.end()) {
        return region_indices;
    }

    const GeoPoint point{.lat = static_cast<float>(lat), .lon = static_cast<float>(lon)};
    std::unordered_set<std::uint64_t> seen_relation_ids;
    std::unordered_set<std::size_t> seen_region_indices;

    for (const auto region_index : it->second) {
        if (region_index >= data.regions.size() || region_index == options.skip_region_index) {
            continue;
        }
        if (!seen_region_indices.insert(region_index).second) {
            continue;
        }

        const auto& region = data.regions[region_index];
        if (options.useful_admin_levels_only && !is_useful_admin_level(region.admin_level)) {
            continue;
        }
        if (options.exclude_postal_regions && region.is_postal_region) {
            continue;
        }
        if (options.broader_than_admin_level >= 0 && region.admin_level >= options.broader_than_admin_level) {
            continue;
        }
        if (options.require_name) {
            if (region.name_id == kInvalidStringId || region.name_id >= data.strings.size()) {
                continue;
            }
            if (data.strings.resolve(region.name_id).empty()) {
                continue;
            }
        }
        if (!region.bbox.contains(lon, lat)) {
            continue;
        }

        const auto begin = static_cast<std::size_t>(region.points_begin);
        const auto count = static_cast<std::size_t>(region.points_count);
        if (count < 3 || begin + count > data.region_points.size()) {
            continue;
        }
        if (!point_in_polygon(point, data.region_points.data() + begin, count)) {
            continue;
        }
        if (region.source_relation_id != 0 && !seen_relation_ids.insert(region.source_relation_id).second) {
            continue;
        }

        region_indices.push_back(region_index);
    }

    std::sort(region_indices.begin(), region_indices.end(), [&data](const std::size_t lhs, const std::size_t rhs) {
        const auto& a = data.regions[lhs];
        const auto& b = data.regions[rhs];
        if (a.admin_level != b.admin_level) {
            return a.admin_level > b.admin_level;
        }
        if (a.approx_area != b.approx_area) {
            return a.approx_area < b.approx_area;
        }
        const auto an = (a.name_id != kInvalidStringId && a.name_id < data.strings.size()) ? data.strings.resolve(a.name_id) : std::string{};
        const auto bn = (b.name_id != kInvalidStringId && b.name_id < data.strings.size()) ? data.strings.resolve(b.name_id) : std::string{};
        if (an != bn) {
            return an < bn;
        }
        return lhs < rhs;
    });

    return region_indices;
}

} // namespace osm

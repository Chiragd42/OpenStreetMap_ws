#include "model.hpp"

#include <cmath>
#include <stdexcept>

namespace osm {

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

} // namespace osm

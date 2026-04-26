#pragma once

#include <cstdint>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace osm {

using StringId = std::uint32_t;
constexpr StringId kInvalidStringId = std::numeric_limits<StringId>::max();

struct GeoPoint {
    float lat{0.0F};
    float lon{0.0F};
};

struct BBox {
    double min_lon{0.0};
    double min_lat{0.0};
    double max_lon{0.0};
    double max_lat{0.0};

    [[nodiscard]] bool contains(double lon, double lat) const noexcept;
};

class StringPool {
public:
    [[nodiscard]] StringId intern(std::string_view value);
    [[nodiscard]] const std::string& resolve(StringId id) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<std::string> values_;
    std::unordered_map<std::string, StringId> index_by_value_;
};

struct HousePoint {
    float lat{0.0F};
    float lon{0.0F};
    StringId street_name_id{kInvalidStringId};
    StringId house_number_id{kInvalidStringId};
    StringId city_id{kInvalidStringId};
    StringId postcode_id{kInvalidStringId};
};

struct StreetPolyline {
    StringId name_id{kInvalidStringId};
    StringId highway_class_id{kInvalidStringId};
    bool is_unnamed{false};
    std::uint32_t points_begin{0};
    std::uint32_t points_count{0};
    BBox bbox{};
};

struct RegionPolygon {
    StringId name_id{kInvalidStringId};
    std::int32_t admin_level{-1};
    std::uint32_t points_begin{0};
    std::uint32_t points_count{0};
    BBox bbox{};
};

struct GridCellKey {
    std::int32_t x{0};
    std::int32_t y{0};

    [[nodiscard]] bool operator==(const GridCellKey& other) const noexcept {
        return x == other.x && y == other.y;
    }
};

struct GridCellKeyHash {
    [[nodiscard]] std::size_t operator()(const GridCellKey& key) const noexcept {
        const auto hx = static_cast<std::size_t>(static_cast<std::uint32_t>(key.x));
        const auto hy = static_cast<std::size_t>(static_cast<std::uint32_t>(key.y));
        return (hx * 73856093U) ^ (hy * 19349663U);
    }
};

struct SpatialGridIndex {
    float cell_size_deg{0.01F};
    std::unordered_map<GridCellKey, std::vector<std::size_t>, GridCellKeyHash> house_cells;
    std::unordered_map<GridCellKey, std::vector<std::size_t>, GridCellKeyHash> street_cells;
    std::unordered_map<GridCellKey, std::vector<std::size_t>, GridCellKeyHash> region_cells;
};

struct DataStore {
    StringPool strings;
    std::vector<HousePoint> houses;
    std::vector<StreetPolyline> streets;
    std::vector<GeoPoint> street_points;
    std::vector<RegionPolygon> regions;
    std::vector<GeoPoint> region_points;
    SpatialGridIndex grid;
};

[[nodiscard]] GridCellKey to_grid_cell(double lon, double lat, float cell_size_deg);
[[nodiscard]] std::vector<GridCellKey> grid_cells_for_bbox(const BBox& bbox, float cell_size_deg);

} // namespace osm

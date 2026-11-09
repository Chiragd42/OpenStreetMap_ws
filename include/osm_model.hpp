#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sheet1 {

using StringId = std::uint32_t;
inline constexpr StringId kInvalidStringId = std::numeric_limits<StringId>::max();

enum class OsmObjectType : std::uint8_t {
    node,
    way,
    relation,
};

struct OsmId {
    OsmObjectType type{OsmObjectType::node};
    std::int64_t value{0};

    [[nodiscard]] bool operator==(const OsmId&) const noexcept = default;
};

struct Coordinate {
    std::int32_t latitude_e7{0};
    std::int32_t longitude_e7{0};
};

struct BoundingBox {
    Coordinate minimum{};
    Coordinate maximum{};
};

class StringPool {
public:
    [[nodiscard]] StringId intern(std::string_view value);
    [[nodiscard]] const std::string& resolve(StringId id) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t estimated_bytes() const noexcept;

private:
    std::vector<std::string> values_;
    std::unordered_map<std::string, StringId> ids_;
};

struct Building {
    OsmId id{};
    Coordinate point{};
    StringId name{kInvalidStringId};
    StringId building_type{kInvalidStringId};
    StringId street{kInvalidStringId};
    StringId house_number{kInvalidStringId};
    StringId postcode{kInvalidStringId};
    StringId city{kInvalidStringId};
    StringId state{kInvalidStringId};
    StringId country{kInvalidStringId};
    bool has_point{false};
    std::uint64_t regions_begin{0};
    std::uint32_t regions_count{0};
};

struct Street {
    OsmId id{};
    BoundingBox bounds{};
    StringId name{kInvalidStringId};
    StringId highway_class{kInvalidStringId};
    std::uint32_t points_begin{0};
    std::uint32_t points_count{0};
    std::uint64_t regions_begin{0};
    std::uint32_t regions_count{0};
};

struct AdministrativeRing {
    std::uint32_t points_begin{0};
    std::uint32_t points_count{0};
    bool is_inner{false};
};

struct AdministrativeArea {
    OsmId id{};
    BoundingBox bounds{};
    StringId name{kInvalidStringId};
    std::int16_t admin_level{-1};
    std::uint32_t rings_begin{0};
    std::uint32_t rings_count{0};
    std::uint32_t outer_ring_count{0};
    std::uint32_t inner_ring_count{0};
    bool has_geometry{false};
    StringId postcode{kInvalidStringId};
    std::uint64_t regions_begin{0};
    std::uint32_t regions_count{0};
};

struct DataStore {
    StringPool strings;
    std::vector<Building> buildings;
    std::vector<Street> streets;
    std::vector<AdministrativeArea> administrative_areas;
    std::vector<Coordinate> street_points;
    std::vector<AdministrativeRing> administrative_rings;
    std::vector<Coordinate> administrative_points;
    std::vector<std::uint32_t> region_memberships;

    [[nodiscard]] std::size_t estimated_bytes() const noexcept;
};

[[nodiscard]] std::string format_osm_id(const OsmId& id);

} // namespace sheet1

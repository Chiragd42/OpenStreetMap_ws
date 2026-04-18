#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace osm {

using StringId = std::uint32_t;
constexpr StringId kInvalidStringId = std::numeric_limits<StringId>::max();

struct GeoPoint {
    double lat{0.0};
    double lon{0.0};
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
    double lat{0.0};
    double lon{0.0};
    StringId street_name_id{kInvalidStringId};
    StringId house_number_id{kInvalidStringId};
    StringId city_id{kInvalidStringId};
    StringId postcode_id{kInvalidStringId};
};

struct StreetPolyline {
    StringId name_id{kInvalidStringId};
    StringId highway_class_id{kInvalidStringId};
    std::uint32_t points_begin{0};
    std::uint32_t points_count{0};
};

struct DataStore {
    StringPool strings;
    std::vector<HousePoint> houses;
    std::vector<StreetPolyline> streets;
    std::vector<GeoPoint> street_points;
};

} // namespace osm

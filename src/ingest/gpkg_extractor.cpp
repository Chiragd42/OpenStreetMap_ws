#include "ingest/gpkg_extractor.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace osm {

namespace {

class ByteReader {
public:
    ByteReader(const std::uint8_t* data, const std::size_t size) : data_(data), size_(size) {}

    [[nodiscard]] bool read_u8(std::uint8_t& value) {
        if (pos_ + 1 > size_) {
            return false;
        }
        value = data_[pos_++];
        return true;
    }

    [[nodiscard]] bool read_u32(const bool little_endian, std::uint32_t& value) {
        if (pos_ + 4 > size_) {
            return false;
        }

        const auto* p = data_ + pos_;
        if (little_endian) {
            value =
                static_cast<std::uint32_t>(p[0]) |
                (static_cast<std::uint32_t>(p[1]) << 8U) |
                (static_cast<std::uint32_t>(p[2]) << 16U) |
                (static_cast<std::uint32_t>(p[3]) << 24U);
        } else {
            value =
                static_cast<std::uint32_t>(p[3]) |
                (static_cast<std::uint32_t>(p[2]) << 8U) |
                (static_cast<std::uint32_t>(p[1]) << 16U) |
                (static_cast<std::uint32_t>(p[0]) << 24U);
        }

        pos_ += 4;
        return true;
    }

    [[nodiscard]] bool read_u64(const bool little_endian, std::uint64_t& value) {
        if (pos_ + 8 > size_) {
            return false;
        }

        const auto* p = data_ + pos_;
        if (little_endian) {
            value =
                static_cast<std::uint64_t>(p[0]) |
                (static_cast<std::uint64_t>(p[1]) << 8U) |
                (static_cast<std::uint64_t>(p[2]) << 16U) |
                (static_cast<std::uint64_t>(p[3]) << 24U) |
                (static_cast<std::uint64_t>(p[4]) << 32U) |
                (static_cast<std::uint64_t>(p[5]) << 40U) |
                (static_cast<std::uint64_t>(p[6]) << 48U) |
                (static_cast<std::uint64_t>(p[7]) << 56U);
        } else {
            value =
                static_cast<std::uint64_t>(p[7]) |
                (static_cast<std::uint64_t>(p[6]) << 8U) |
                (static_cast<std::uint64_t>(p[5]) << 16U) |
                (static_cast<std::uint64_t>(p[4]) << 24U) |
                (static_cast<std::uint64_t>(p[3]) << 32U) |
                (static_cast<std::uint64_t>(p[2]) << 40U) |
                (static_cast<std::uint64_t>(p[1]) << 48U) |
                (static_cast<std::uint64_t>(p[0]) << 56U);
        }

        pos_ += 8;
        return true;
    }

    [[nodiscard]] bool read_double(const bool little_endian, double& value) {
        std::uint64_t bits = 0;
        if (!read_u64(little_endian, bits)) {
            return false;
        }

        std::memcpy(&value, &bits, sizeof(double));
        return true;
    }

private:
    const std::uint8_t* data_{nullptr};
    std::size_t size_{0};
    std::size_t pos_{0};
};

struct WkbTypeInfo {
    std::uint32_t base_type{0};
    int dimensions{2};
};

[[nodiscard]] WkbTypeInfo decode_wkb_type(std::uint32_t type) {
    WkbTypeInfo info;

    // Extended WKB flags.
    const bool ewkb_z = (type & 0x80000000U) != 0;
    const bool ewkb_m = (type & 0x40000000U) != 0;
    if ((type & 0xE0000000U) != 0) {
        info.base_type = type & 0xFFU;
        info.dimensions = 2 + (ewkb_z ? 1 : 0) + (ewkb_m ? 1 : 0);
        return info;
    }

    // ISO WKB 1000/2000/3000 offsets.
    if (type >= 3000 && type < 4000) {
        info.base_type = type - 3000;
        info.dimensions = 4;
        return info;
    }
    if (type >= 2000 && type < 3000) {
        info.base_type = type - 2000;
        info.dimensions = 3;
        return info;
    }
    if (type >= 1000 && type < 2000) {
        info.base_type = type - 1000;
        info.dimensions = 3;
        return info;
    }

    info.base_type = type;
    info.dimensions = 2;
    return info;
}

[[nodiscard]] bool read_geometry_header(ByteReader& reader, bool& little_endian, WkbTypeInfo& type_info) {
    std::uint8_t byte_order = 0;
    if (!reader.read_u8(byte_order)) {
        return false;
    }

    if (byte_order != 0 && byte_order != 1) {
        return false;
    }
    little_endian = (byte_order == 1);

    std::uint32_t raw_type = 0;
    if (!reader.read_u32(little_endian, raw_type)) {
        return false;
    }

    type_info = decode_wkb_type(raw_type);
    return true;
}

[[nodiscard]] bool read_xy(ByteReader& reader, const bool little_endian, const int dimensions, double& x, double& y) {
    if (!reader.read_double(little_endian, x)) {
        return false;
    }
    if (!reader.read_double(little_endian, y)) {
        return false;
    }

    for (int d = 2; d < dimensions; ++d) {
        double discard = 0.0;
        if (!reader.read_double(little_endian, discard)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool parse_lines_geometry(
    ByteReader& reader,
    std::vector<std::vector<GeoPoint>>& out_lines,
    std::uint64_t& processed_point_count);

[[nodiscard]] bool parse_polygon_points_geometry(
    ByteReader& reader,
    std::vector<GeoPoint>& out_points,
    std::uint64_t& processed_point_count);

[[nodiscard]] bool parse_lines_geometry(
    ByteReader& reader,
    std::vector<std::vector<GeoPoint>>& out_lines,
    std::uint64_t& processed_point_count) {
    bool little_endian = true;
    WkbTypeInfo info;
    if (!read_geometry_header(reader, little_endian, info)) {
        return false;
    }

    switch (info.base_type) {
        case 2: { // LINESTRING
            std::uint32_t count = 0;
            if (!reader.read_u32(little_endian, count)) {
                return false;
            }

            std::vector<GeoPoint> line;
            line.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                double x = 0.0;
                double y = 0.0;
                if (!read_xy(reader, little_endian, info.dimensions, x, y)) {
                    return false;
                }

                line.push_back(GeoPoint{.lat = y, .lon = x});
                ++processed_point_count;
            }

            if (line.size() >= 2) {
                out_lines.push_back(std::move(line));
            }
            return true;
        }

        case 5:   // MULTILINESTRING
        case 7: { // GEOMETRYCOLLECTION
            std::uint32_t count = 0;
            if (!reader.read_u32(little_endian, count)) {
                return false;
            }

            for (std::uint32_t i = 0; i < count; ++i) {
                if (!parse_lines_geometry(reader, out_lines, processed_point_count)) {
                    return false;
                }
            }

            return true;
        }

        default:
            return false;
    }
}

[[nodiscard]] bool parse_polygon_points_geometry(
    ByteReader& reader,
    std::vector<GeoPoint>& out_points,
    std::uint64_t& processed_point_count) {
    bool little_endian = true;
    WkbTypeInfo info;
    if (!read_geometry_header(reader, little_endian, info)) {
        return false;
    }

    switch (info.base_type) {
        case 3: { // POLYGON
            std::uint32_t ring_count = 0;
            if (!reader.read_u32(little_endian, ring_count)) {
                return false;
            }

            for (std::uint32_t r = 0; r < ring_count; ++r) {
                std::uint32_t point_count = 0;
                if (!reader.read_u32(little_endian, point_count)) {
                    return false;
                }

                for (std::uint32_t p = 0; p < point_count; ++p) {
                    double x = 0.0;
                    double y = 0.0;
                    if (!read_xy(reader, little_endian, info.dimensions, x, y)) {
                        return false;
                    }
                    out_points.push_back(GeoPoint{.lat = y, .lon = x});
                    ++processed_point_count;
                }
            }

            return true;
        }

        case 6:   // MULTIPOLYGON
        case 7: { // GEOMETRYCOLLECTION
            std::uint32_t count = 0;
            if (!reader.read_u32(little_endian, count)) {
                return false;
            }

            for (std::uint32_t i = 0; i < count; ++i) {
                if (!parse_polygon_points_geometry(reader, out_points, processed_point_count)) {
                    return false;
                }
            }

            return true;
        }

        default:
            return false;
    }
}

[[nodiscard]] std::size_t envelope_size_bytes_from_flags(const std::uint8_t flags) {
    const std::uint8_t envelope_code = static_cast<std::uint8_t>((flags >> 1U) & 0x07U);
    switch (envelope_code) {
        case 0: return 0;
        case 1: return 32; // minx, maxx, miny, maxy
        case 2:
        case 3: return 48; // +z or +m
        case 4: return 64; // +z and +m
        default: return 0;
    }
}

[[nodiscard]] bool gpkg_blob_to_wkb_view(
    const void* blob,
    const int blob_size,
    const std::uint8_t*& wkb_data,
    std::size_t& wkb_size) {
    if (blob == nullptr || blob_size < 8) {
        return false;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(blob);
    if (bytes[0] != 'G' || bytes[1] != 'P') {
        return false;
    }

    const std::uint8_t flags = bytes[3];
    const std::size_t header_size = 8 + envelope_size_bytes_from_flags(flags);
    if (static_cast<std::size_t>(blob_size) <= header_size) {
        return false;
    }

    wkb_data = bytes + header_size;
    wkb_size = static_cast<std::size_t>(blob_size) - header_size;
    return true;
}

[[nodiscard]] std::string read_text_column(sqlite3_stmt* stmt, const int idx) {
    const auto* txt = sqlite3_column_text(stmt, idx);
    if (txt == nullptr) {
        return {};
    }
    return reinterpret_cast<const char*>(txt);
}

[[nodiscard]] std::string limit_clause_from_max_rows(const std::size_t max_rows) {
    if (max_rows == 0) {
        return {};
    }
    return " LIMIT " + std::to_string(max_rows);
}

[[nodiscard]] StringId intern_if_non_empty(StringPool& pool, std::string_view value) {
    if (value.empty()) {
        return kInvalidStringId;
    }
    return pool.intern(value);
}

[[nodiscard]] std::size_t estimate_string_pool_bytes(const StringPool& pool) {
    std::size_t bytes = 0;
    for (StringId id = 0;; ++id) {
        try {
            bytes += pool.resolve(id).size();
        } catch (...) {
            break;
        }
    }
    return bytes;
}

using StatementPtr = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

[[nodiscard]] StatementPtr prepare_statement(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return StatementPtr(nullptr, sqlite3_finalize);
    }
    return StatementPtr(stmt, sqlite3_finalize);
}

} // namespace

ExtractionResult GpkgExtractor::extract(const ExtractionConfig& config) const {
    ExtractionResult result;

    Stopwatch timer;
    timer.start();

    sqlite3* raw_db = nullptr;
    if (sqlite3_open_v2(config.input_gpkg_path.c_str(), &raw_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (raw_db != nullptr) {
            sqlite3_close(raw_db);
        }
        result.stats.parse_seconds = timer.elapsed_seconds();
        return result;
    }

    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw_db, sqlite3_close);

    // ---- Roads -> Street polylines
    {
        const std::string sql =
            "SELECT name, fclass, geom FROM gis_osm_roads_free WHERE geom IS NOT NULL" +
            limit_clause_from_max_rows(config.max_road_rows);

        auto stmt = prepare_statement(db.get(), sql);
        if (stmt) {
            while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
                const std::string name = read_text_column(stmt.get(), 0);
                const std::string fclass = read_text_column(stmt.get(), 1);

                const void* blob = sqlite3_column_blob(stmt.get(), 2);
                const int blob_size = sqlite3_column_bytes(stmt.get(), 2);

                const std::uint8_t* wkb = nullptr;
                std::size_t wkb_size = 0;
                if (!gpkg_blob_to_wkb_view(blob, blob_size, wkb, wkb_size)) {
                    continue;
                }

                ByteReader reader(wkb, wkb_size);
                std::vector<std::vector<GeoPoint>> lines;
                if (!parse_lines_geometry(reader, lines, result.stats.processed_nodes)) {
                    continue;
                }

                ++result.stats.processed_ways;

                for (auto& line : lines) {
                    const auto begin = static_cast<std::uint32_t>(result.data.street_points.size());
                    const auto count = static_cast<std::uint32_t>(line.size());
                    if (count < 2) {
                        continue;
                    }

                    result.data.street_points.insert(result.data.street_points.end(), line.begin(), line.end());

                    StreetPolyline street;
                    street.name_id = intern_if_non_empty(result.data.strings, name);
                    street.highway_class_id = intern_if_non_empty(result.data.strings, fclass);
                    street.points_begin = begin;
                    street.points_count = count;
                    result.data.streets.push_back(street);
                }
            }
        }
    }

    // ---- Building polygons -> representative house points
    {
        const std::string sql =
            "SELECT name, type, geom FROM gis_osm_buildings_a_free WHERE geom IS NOT NULL" +
            limit_clause_from_max_rows(config.max_building_rows);

        auto stmt = prepare_statement(db.get(), sql);
        if (stmt) {
            const StringId city_id = result.data.strings.intern("Stuttgart");

            while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
                const std::string name = read_text_column(stmt.get(), 0);
                const std::string type = read_text_column(stmt.get(), 1);

                const void* blob = sqlite3_column_blob(stmt.get(), 2);
                const int blob_size = sqlite3_column_bytes(stmt.get(), 2);

                const std::uint8_t* wkb = nullptr;
                std::size_t wkb_size = 0;
                if (!gpkg_blob_to_wkb_view(blob, blob_size, wkb, wkb_size)) {
                    continue;
                }

                ByteReader reader(wkb, wkb_size);
                std::vector<GeoPoint> points;
                if (!parse_polygon_points_geometry(reader, points, result.stats.processed_nodes)) {
                    continue;
                }

                if (points.empty()) {
                    continue;
                }

                ++result.stats.processed_ways;

                double min_lon = std::numeric_limits<double>::infinity();
                double max_lon = -std::numeric_limits<double>::infinity();
                double min_lat = std::numeric_limits<double>::infinity();
                double max_lat = -std::numeric_limits<double>::infinity();

                for (const auto& p : points) {
                    min_lon = std::min(min_lon, p.lon);
                    max_lon = std::max(max_lon, p.lon);
                    min_lat = std::min(min_lat, p.lat);
                    max_lat = std::max(max_lat, p.lat);
                }

                HousePoint house;
                house.lon = (min_lon + max_lon) * 0.5;
                house.lat = (min_lat + max_lat) * 0.5;

                const std::string label = !name.empty() ? name : (!type.empty() ? type : "building");
                house.street_name_id = intern_if_non_empty(result.data.strings, label);
                house.house_number_id = kInvalidStringId;
                house.city_id = city_id;
                house.postcode_id = kInvalidStringId;
                result.data.houses.push_back(house);
            }
        }
    }

    // ---- Optional boundaries (count-only for Sheet-1 foundation)
    if (config.include_admin_boundaries) {
        const std::string sql = "SELECT geom FROM gis_osm_adminareas_a_free WHERE geom IS NOT NULL";
        auto stmt = prepare_statement(db.get(), sql);
        if (stmt) {
            while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
                const void* blob = sqlite3_column_blob(stmt.get(), 0);
                const int blob_size = sqlite3_column_bytes(stmt.get(), 0);

                const std::uint8_t* wkb = nullptr;
                std::size_t wkb_size = 0;
                if (!gpkg_blob_to_wkb_view(blob, blob_size, wkb, wkb_size)) {
                    continue;
                }

                ByteReader reader(wkb, wkb_size);
                std::vector<GeoPoint> points;
                if (!parse_polygon_points_geometry(reader, points, result.stats.processed_nodes)) {
                    continue;
                }

                ++result.stats.processed_relations;
                ++result.stats.extracted_admin_areas;
            }
        }
    }

    result.stats.extracted_houses = result.data.houses.size();
    result.stats.extracted_streets = result.data.streets.size();

    result.stats.estimated_memory_bytes =
        (result.data.houses.size() * sizeof(HousePoint)) +
        (result.data.streets.size() * sizeof(StreetPolyline)) +
        (result.data.street_points.size() * sizeof(GeoPoint)) +
        estimate_string_pool_bytes(result.data.strings);

    result.stats.parse_seconds = timer.elapsed_seconds();
    return result;
}

} // namespace osm

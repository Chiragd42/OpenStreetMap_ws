#include "server/http_server.hpp"

#include "query/bbox_query.hpp"
#include "query/json.hpp"
#include "search/geocode_query.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <charconv>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace osm {

namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
};

struct ApiProfile {
    std::string route;
    std::size_t matched{0};
    std::size_t returned{0};
};

std::optional<int> hex_value(const char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return std::nullopt;
}

std::optional<std::string> url_decode(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '%') {
            if (i + 2 >= input.size()) {
                return std::nullopt;
            }
            const auto hi = hex_value(input[i + 1]);
            const auto lo = hex_value(input[i + 2]);
            if (!hi.has_value() || !lo.has_value()) {
                return std::nullopt;
            }
            out.push_back(static_cast<char>(((*hi) << 4) | (*lo)));
            i += 2;
            continue;
        }
        if (c == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }

    return out;
}

std::optional<HttpRequest> parse_request_line(std::string_view raw) {
    const auto line_end = raw.find("\r\n");
    const auto line = raw.substr(0, line_end == std::string_view::npos ? raw.size() : line_end);

    const auto method_end = line.find(' ');
    if (method_end == std::string_view::npos) {
        return std::nullopt;
    }

    const auto target_end = line.find(' ', method_end + 1);
    if (target_end == std::string_view::npos) {
        return std::nullopt;
    }

    HttpRequest request;
    request.method = std::string(line.substr(0, method_end));

    const auto target = line.substr(method_end + 1, target_end - (method_end + 1));
    const auto query_pos = target.find('?');
    if (query_pos == std::string_view::npos) {
        request.path = std::string(target);
        request.query.clear();
    } else {
        request.path = std::string(target.substr(0, query_pos));
        request.query = std::string(target.substr(query_pos + 1));
    }

    return request;
}

std::optional<std::string> get_query_param(std::string_view query, std::string_view key) {
    std::size_t start = 0;
    while (start <= query.size()) {
        const auto end = query.find('&', start);
        const std::size_t token_end = (end == std::string_view::npos) ? query.size() : end;
        const auto token = query.substr(start, token_end - start);

        const auto eq = token.find('=');
        if (eq != std::string_view::npos) {
            const auto k = token.substr(0, eq);
            const auto v = token.substr(eq + 1);
            if (k == key) {
                return url_decode(v);
            }
        }

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    return std::nullopt;
}

std::optional<std::size_t> parse_size_t_query_param(std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }
    std::size_t value = 0;
    const auto* begin = raw.data();
    const auto* end = raw.data() + raw.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> parse_double_query_param(std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }
    double value = 0.0;
    const auto* begin = raw.data();
    const auto* end = raw.data() + raw.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

double haversine_meters(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
    constexpr double kEarthRadiusM = 6371000.0;
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const auto lat1 = lat1_deg * kDegToRad;
    const auto lon1 = lon1_deg * kDegToRad;
    const auto lat2 = lat2_deg * kDegToRad;
    const auto lon2 = lon2_deg * kDegToRad;
    const auto dlat = lat2 - lat1;
    const auto dlon = lon2 - lon1;
    const auto a = std::sin(dlat * 0.5) * std::sin(dlat * 0.5) +
                   std::cos(lat1) * std::cos(lat2) * std::sin(dlon * 0.5) * std::sin(dlon * 0.5);
    const auto c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthRadiusM * c;
}

double min_ring_distance_meters(const GridCellKey& center, const GridCellKey& cell, float cell_size_deg) {
    const auto dx_cells = std::max(0, std::abs(cell.x - center.x) - 1);
    const auto dy_cells = std::max(0, std::abs(cell.y - center.y) - 1);
    const auto min_deg = static_cast<double>(std::max(dx_cells, dy_cells)) * static_cast<double>(cell_size_deg);
    return min_deg * 111320.0;
}

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

[[nodiscard]] bool is_reverse_context_admin_level(const std::int32_t admin_level) {
    return admin_level == 8 || admin_level == 6 || admin_level == 4 || admin_level == 2;
}

[[nodiscard]] std::vector<std::size_t> find_clicked_containing_regions(
    const DataStore& data,
    const double lat,
    const double lon) {
    RegionLookupOptions options;
    options.require_name = true;
    options.exclude_postal_regions = true;
    options.useful_admin_levels_only = true;
    return find_containing_regions_for_point(data, lat, lon, options);
}

[[nodiscard]] std::optional<std::size_t> find_nearest_poi(
    const DataStore& data,
    const double lat,
    const double lon,
    double& best_distance_m) {
    constexpr double kMaxNearestPoiDistanceM = 250.0;
    const double max_delta_deg = kMaxNearestPoiDistanceM / 111320.0;

    best_distance_m = std::numeric_limits<double>::infinity();
    std::optional<std::size_t> best_idx;

    for (std::size_t i = 0; i < data.pois.size(); ++i) {
        const auto& poi = data.pois[i];
        if (poi.name_id == kInvalidStringId || poi.name_id >= data.strings.size()) {
            continue;
        }
        if (data.strings.resolve(poi.name_id).empty()) {
            continue;
        }
        if (std::abs(static_cast<double>(poi.lat) - lat) > max_delta_deg ||
            std::abs(static_cast<double>(poi.lon) - lon) > max_delta_deg) {
            continue;
        }

        const auto dist = haversine_meters(lat, lon, poi.lat, poi.lon);
        if (dist <= kMaxNearestPoiDistanceM && dist < best_distance_m) {
            best_distance_m = dist;
            best_idx = i;
        }
    }

    return best_idx;
}

struct NearestStreetResult {
    std::size_t index{std::numeric_limits<std::size_t>::max()};
    double lat{0.0};
    double lon{0.0};
    double distance_m{std::numeric_limits<double>::infinity()};
};

struct SegmentProjection {
    double lat{0.0};
    double lon{0.0};
    double distance_m{std::numeric_limits<double>::infinity()};
};

[[nodiscard]] SegmentProjection project_point_to_street_segment(
    const double query_lat,
    const double query_lon,
    const GeoPoint& a,
    const GeoPoint& b) {
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    constexpr double kMetersPerDegreeLat = 111320.0;
    const double meters_per_degree_lon = std::max(1e-6, kMetersPerDegreeLat * std::cos(query_lat * kDegToRad));

    const double ax = (static_cast<double>(a.lon) - query_lon) * meters_per_degree_lon;
    const double ay = (static_cast<double>(a.lat) - query_lat) * kMetersPerDegreeLat;
    const double bx = (static_cast<double>(b.lon) - query_lon) * meters_per_degree_lon;
    const double by = (static_cast<double>(b.lat) - query_lat) * kMetersPerDegreeLat;
    const double vx = bx - ax;
    const double vy = by - ay;
    const double len_sq = vx * vx + vy * vy;

    double t = 0.0;
    if (len_sq > 0.0) {
        t = std::clamp(-(ax * vx + ay * vy) / len_sq, 0.0, 1.0);
    }

    const double px = ax + (vx * t);
    const double py = ay + (vy * t);
    return SegmentProjection{
        .lat = query_lat + (py / kMetersPerDegreeLat),
        .lon = query_lon + (px / meters_per_degree_lon),
        .distance_m = std::sqrt((px * px) + (py * py)),
    };
}

[[nodiscard]] NearestStreetResult find_nearest_street(
    const DataStore& data,
    const double lat,
    const double lon) {
    constexpr double kMaxNearestStreetDistanceM = 250.0;
    constexpr int kMaxStreetRing = 64;

    NearestStreetResult best;
    const auto center = to_grid_cell(lon, lat, data.grid.cell_size_deg);
    std::unordered_set<std::size_t> seen;

    for (int ring = 0; ring <= kMaxStreetRing; ++ring) {
        for (int dx = -ring; dx <= ring; ++dx) {
            for (int dy = -ring; dy <= ring; ++dy) {
                if (ring > 0 && std::abs(dx) != ring && std::abs(dy) != ring) {
                    continue;
                }

                const GridCellKey cell{.x = center.x + dx, .y = center.y + dy};
                const auto it = data.grid.street_cells.find(cell);
                if (it == data.grid.street_cells.end()) {
                    continue;
                }

                for (const auto idx : it->second) {
                    if (idx >= data.streets.size() || !seen.insert(idx).second) {
                        continue;
                    }

                    const auto& street = data.streets[idx];
                    const auto begin = static_cast<std::size_t>(street.points_begin);
                    const auto count = static_cast<std::size_t>(street.points_count);
                    if (count == 0 || begin >= data.street_points.size()) {
                        continue;
                    }

                    if (count == 1 || begin + 1 >= data.street_points.size()) {
                        const auto& point = data.street_points[begin];
                        const auto dist = haversine_meters(lat, lon, point.lat, point.lon);
                        if (dist < best.distance_m) {
                            best = NearestStreetResult{
                                .index = idx,
                                .lat = point.lat,
                                .lon = point.lon,
                                .distance_m = dist,
                            };
                        }
                        continue;
                    }

                    const auto usable_count = std::min(count, data.street_points.size() - begin);
                    for (std::size_t point_offset = 1; point_offset < usable_count; ++point_offset) {
                        const auto projection = project_point_to_street_segment(
                            lat,
                            lon,
                            data.street_points[begin + point_offset - 1],
                            data.street_points[begin + point_offset]);
                        if (projection.distance_m < best.distance_m) {
                            best = NearestStreetResult{
                                .index = idx,
                                .lat = projection.lat,
                                .lon = projection.lon,
                                .distance_m = projection.distance_m,
                            };
                        }
                    }
                }
            }
        }

        const GridCellKey next_ring_probe{.x = center.x + ring + 1, .y = center.y};
        const auto min_next = min_ring_distance_meters(center, next_ring_probe, data.grid.cell_size_deg);
        if (min_next > std::min(best.distance_m, kMaxNearestStreetDistanceM)) {
            break;
        }
    }

    if (best.distance_m > kMaxNearestStreetDistanceM) {
        return {};
    }
    return best;
}

std::vector<std::size_t> apply_stride_and_limit(
    const std::vector<std::size_t>& indices,
    const std::size_t stride,
    const std::size_t limit) {
    std::vector<std::size_t> filtered;
    filtered.reserve(limit > 0 ? std::min(limit, indices.size()) : indices.size());

    for (std::size_t i = 0; i < indices.size(); i += stride) {
        filtered.push_back(indices[i]);
        if (limit > 0 && filtered.size() >= limit) {
            break;
        }
    }
    return filtered;
}

std::string make_response(
    const int status_code,
    std::string_view status_text,
    std::string_view content_type,
    std::string_view body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status_code << ' ' << status_text << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n"
        << body;
    return out.str();
}

std::string handle_api_request(
    const HttpRequest& request,
    const DataStore& data,
    const search::SearchIndex& search_index,
    const ParseStats& stats,
    ApiProfile& profile,
    int& status_code,
    std::string& status_text) {
    if (request.method != "GET") {
        status_code = 405;
        status_text = "Method Not Allowed";
        return serialize_error_json("Only GET is supported");
    }

    if (request.path == "/stats") {
        profile.route = "/stats";
        status_code = 200;
        status_text = "OK";
        return serialize_stats_json(stats);
    }

    if (request.path == "/geocode") {
        profile.route = "/geocode";
        const auto query_param = get_query_param(request.query, "q");
        if (!query_param.has_value()) {
            status_code = 400;
            status_text = "Bad Request";
            return serialize_error_json("Missing q query parameter");
        }

        search::GeocodeQueryOptions options;
        if (const auto bbox_param = get_query_param(request.query, "bbox"); bbox_param.has_value()) {
            options.viewport = parse_bbox_csv(*bbox_param);
            if (!options.viewport.has_value()) {
                status_code = 400;
                status_text = "Bad Request";
                return serialize_error_json("Invalid bbox format. Expected minLon,minLat,maxLon,maxLat");
            }
        }
        const auto result = search::runGeocodeQuery(data, search_index, *query_param, options);
        profile.matched = result.ranked_candidates.size();
        profile.returned = result.ranked_candidates.size();
        status_code = 200;
        status_text = "OK";
        return serialize_geocode_json(data, result);
    }

    if (request.path == "/reverse") {
        constexpr double kMaxReverseHouseDistanceM = 500.0;
        profile.route = "/reverse";
        const auto lat_param = get_query_param(request.query, "lat");
        const auto lon_param = get_query_param(request.query, "lon");
        if (!lat_param.has_value() || !lon_param.has_value()) {
            status_code = 400;
            status_text = "Bad Request";
            return serialize_error_json("Missing lat/lon query parameters");
        }

        const auto lat = parse_double_query_param(*lat_param);
        const auto lon = parse_double_query_param(*lon_param);
        if (!lat.has_value() || !lon.has_value()) {
            status_code = 400;
            status_text = "Bad Request";
            return serialize_error_json("Invalid lat/lon values");
        }

        const auto clicked_regions = find_clicked_containing_regions(data, *lat, *lon);
        double nearest_poi_distance_m = std::numeric_limits<double>::infinity();
        const auto nearest_poi_index = find_nearest_poi(data, *lat, *lon, nearest_poi_distance_m);
        const auto nearest_street = find_nearest_street(data, *lat, *lon);
        const auto center = to_grid_cell(*lon, *lat, data.grid.cell_size_deg);
        constexpr int kMaxRing = 64;
        double best_distance = std::numeric_limits<double>::infinity();
        std::size_t best_idx = data.houses.size();
        std::unordered_set<std::size_t> seen;

        for (int ring = 0; ring <= kMaxRing; ++ring) {
            for (int dx = -ring; dx <= ring; ++dx) {
                for (int dy = -ring; dy <= ring; ++dy) {
                    if (ring > 0 && std::abs(dx) != ring && std::abs(dy) != ring) {
                        continue;
                    }
                    GridCellKey cell{.x = center.x + dx, .y = center.y + dy};
                    const auto it = data.grid.house_cells.find(cell);
                    if (it == data.grid.house_cells.end()) {
                        continue;
                    }
                    for (const auto idx : it->second) {
                        if (idx >= data.houses.size() || !seen.insert(idx).second) {
                            continue;
                        }
                        const auto& h = data.houses[idx];
                        const auto dist = haversine_meters(*lat, *lon, h.lat, h.lon);
                        if (dist < best_distance) {
                            best_distance = dist;
                            best_idx = idx;
                        }
                    }
                }
            }

            if (best_idx < data.houses.size()) {
                const GridCellKey next_ring_probe{.x = center.x + ring + 1, .y = center.y};
                const auto min_next = min_ring_distance_meters(center, next_ring_probe, data.grid.cell_size_deg);
                if (min_next > best_distance) {
                    break;
                }
            }
        }

        if (best_idx >= data.houses.size() || best_distance > kMaxReverseHouseDistanceM) {
            if (nearest_street.index < data.streets.size()) {
                profile.matched = seen.size();
                profile.returned = 1;
                status_code = 200;
                status_text = "OK";
                return serialize_reverse_street_json(
                    data,
                    nearest_street.index,
                    clicked_regions,
                    nearest_poi_index,
                    nearest_poi_distance_m,
                    *lat,
                    *lon,
                    nearest_street.lat,
                    nearest_street.lon,
                    nearest_street.distance_m);
            }

            if (!clicked_regions.empty()) {
                profile.matched = clicked_regions.size();
                profile.returned = 1;
                status_code = 200;
                status_text = "OK";
                return serialize_reverse_region_json(
                    data,
                    clicked_regions.front(),
                    clicked_regions,
                    nearest_poi_index,
                    nearest_poi_distance_m,
                    *lat,
                    *lon);
            }

            status_code = 404;
            status_text = "Not Found";
            return serialize_error_json("No nearby house, street, or region found");
        }

        const auto& h = data.houses[best_idx];
        profile.matched = seen.size();
        profile.returned = 1;
        status_code = 200;
        status_text = "OK";
        return serialize_reverse_json(
            data,
            best_idx,
            clicked_regions,
            nearest_poi_index,
            nearest_poi_distance_m,
            nearest_street.index < data.streets.size() ? std::optional<std::size_t>{nearest_street.index} : std::nullopt,
            nearest_street.lat,
            nearest_street.lon,
            nearest_street.distance_m,
            *lat,
            *lon,
            h.lat,
            h.lon,
            best_distance,
            h.street_name_id == kInvalidStringId ? std::string_view{} : data.strings.resolve(h.street_name_id),
            h.house_number_id == kInvalidStringId ? std::string_view{} : data.strings.resolve(h.house_number_id),
            h.city_id == kInvalidStringId ? std::string_view{} : data.strings.resolve(h.city_id),
            h.state_id == kInvalidStringId ? std::string_view{} : data.strings.resolve(h.state_id),
            h.postcode_id == kInvalidStringId ? std::string_view{} : data.strings.resolve(h.postcode_id),
            h.country_id == kInvalidStringId ? std::string_view{} : data.strings.resolve(h.country_id));
    }

    if (request.path == "/houses" || request.path == "/streets" || request.path == "/regions") {
        const auto bbox_param = get_query_param(request.query, "bbox");
        if (!bbox_param.has_value()) {
            status_code = 400;
            status_text = "Bad Request";
            return serialize_error_json("Missing bbox query parameter");
        }

        const auto bbox = parse_bbox_csv(*bbox_param);
        if (!bbox.has_value()) {
            status_code = 400;
            status_text = "Bad Request";
            return serialize_error_json("Invalid bbox format. Expected minLon,minLat,maxLon,maxLat");
        }

        status_code = 200;
        status_text = "OK";

        if (request.path == "/houses") {
            profile.route = "/houses";
            std::size_t stride = 1;
            std::size_t limit = 0;

            if (const auto stride_param = get_query_param(request.query, "stride"); stride_param.has_value()) {
                const auto parsed = parse_size_t_query_param(*stride_param);
                if (!parsed.has_value() || *parsed == 0) {
                    status_code = 400;
                    status_text = "Bad Request";
                    return serialize_error_json("Invalid stride parameter. Expected positive integer");
                }
                stride = *parsed;
            }

            if (const auto limit_param = get_query_param(request.query, "limit"); limit_param.has_value()) {
                const auto parsed = parse_size_t_query_param(*limit_param);
                if (!parsed.has_value()) {
                    status_code = 400;
                    status_text = "Bad Request";
                    return serialize_error_json("Invalid limit parameter. Expected non-negative integer");
                }
                limit = *parsed;
            }

            const auto matched = query_houses_in_bbox(data, *bbox);
            const auto indices = apply_stride_and_limit(matched, stride, limit);
            profile.matched = matched.size();
            profile.returned = indices.size();
            return serialize_houses_json(data, indices, matched.size(), true);
        }

        if (request.path == "/regions") {
            profile.route = "/regions";
            std::size_t stride = 1;
            std::size_t limit = 0;
            std::optional<std::int32_t> max_admin_level;

            if (const auto stride_param = get_query_param(request.query, "stride"); stride_param.has_value()) {
                const auto parsed = parse_size_t_query_param(*stride_param);
                if (!parsed.has_value() || *parsed == 0) {
                    status_code = 400;
                    status_text = "Bad Request";
                    return serialize_error_json("Invalid stride parameter. Expected positive integer");
                }
                stride = *parsed;
            }

            if (const auto limit_param = get_query_param(request.query, "limit"); limit_param.has_value()) {
                const auto parsed = parse_size_t_query_param(*limit_param);
                if (!parsed.has_value()) {
                    status_code = 400;
                    status_text = "Bad Request";
                    return serialize_error_json("Invalid limit parameter. Expected non-negative integer");
                }
                limit = *parsed;
            }

            if (const auto max_admin_level_param = get_query_param(request.query, "max_admin_level");
                max_admin_level_param.has_value()) {
                const auto parsed = parse_size_t_query_param(*max_admin_level_param);
                if (!parsed.has_value()) {
                    status_code = 400;
                    status_text = "Bad Request";
                    return serialize_error_json("Invalid max_admin_level parameter. Expected non-negative integer");
                }
                max_admin_level = static_cast<std::int32_t>(*parsed);
            }

            const auto matched = query_regions_in_bbox(data, *bbox);
            std::vector<std::size_t> filtered = matched;
            if (max_admin_level.has_value()) {
                filtered.clear();
                filtered.reserve(matched.size());
                for (const auto idx : matched) {
                    if (idx >= data.regions.size()) continue;
                    const auto& region = data.regions[idx];
                    if (region.admin_level >= 0 && region.admin_level <= *max_admin_level) {
                        filtered.push_back(idx);
                    }
                }
            }

            const auto indices = apply_stride_and_limit(filtered, stride, limit);
            profile.matched = matched.size();
            profile.returned = indices.size();
            return serialize_regions_json(data, indices, filtered.size(), true);
        }

        profile.route = "/streets";
        std::size_t stride = 1;
        std::size_t limit = 0;

        if (const auto stride_param = get_query_param(request.query, "stride"); stride_param.has_value()) {
            const auto parsed = parse_size_t_query_param(*stride_param);
            if (!parsed.has_value() || *parsed == 0) {
                status_code = 400;
                status_text = "Bad Request";
                return serialize_error_json("Invalid stride parameter. Expected positive integer");
            }
            stride = *parsed;
        }

        if (const auto limit_param = get_query_param(request.query, "limit"); limit_param.has_value()) {
            const auto parsed = parse_size_t_query_param(*limit_param);
            if (!parsed.has_value()) {
                status_code = 400;
                status_text = "Bad Request";
                return serialize_error_json("Invalid limit parameter. Expected non-negative integer");
            }
            limit = *parsed;
        }

        const auto matched = query_streets_in_bbox(data, *bbox);
        const auto indices = apply_stride_and_limit(matched, stride, limit);
        profile.matched = matched.size();
        profile.returned = indices.size();
        return serialize_streets_json(data, indices, matched.size(), true);
    }

    profile.route = request.path;
    status_code = 404;
    status_text = "Not Found";
    return serialize_error_json("Route not found");
}

bool send_all(const int socket_fd, const std::string& payload) {
    std::size_t sent = 0;
    while (sent < payload.size()) {
        const auto* buffer = payload.data() + sent;
        const auto remaining = payload.size() - sent;
        const auto written = ::send(socket_fd, buffer, remaining, 0);
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

} // namespace

int run_http_server(
    const DataStore& data,
    const search::SearchIndex& search_index,
    const ParseStats& stats,
    const std::uint16_t port,
    const std::size_t max_requests) {
    std::signal(SIGPIPE, SIG_IGN);

    const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket: " << std::strerror(errno) << '\n';
        return 1;
    }

    int reuse = 1;
    if (::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR: " << std::strerror(errno) << '\n';
        ::close(server_fd);
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind server socket on port " << port << ": " << std::strerror(errno) << '\n';
        ::close(server_fd);
        return 1;
    }

    if (::listen(server_fd, 16) < 0) {
        std::cerr << "Failed to listen on socket: " << std::strerror(errno) << '\n';
        ::close(server_fd);
        return 1;
    }

    std::cout << "HTTP server listening on http://127.0.0.1:" << port << '\n';
    if (max_requests > 0) {
        std::cout << "Server will stop after " << max_requests << " request(s)." << '\n';
    }

    std::size_t served_requests = 0;
    while (max_requests == 0 || served_requests < max_requests) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_fd < 0) {
            std::cerr << "Accept failed: " << std::strerror(errno) << '\n';
            continue;
        }

        char buffer[8192];
        const auto bytes_read = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) {
            ::close(client_fd);
            continue;
        }

        const std::string_view request_view{buffer, static_cast<std::size_t>(bytes_read)};
        int status_code = 400;
        std::string status_text = "Bad Request";
        std::string body = serialize_error_json("Malformed HTTP request");
        ApiProfile profile;
        profile.route = "<invalid>";

        const auto request_start = std::chrono::steady_clock::now();
        if (const auto request = parse_request_line(request_view)) {
            body = handle_api_request(*request, data, search_index, stats, profile, status_code, status_text);
        }
        const auto elapsed_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - request_start).count();

        const auto response = make_response(status_code, status_text, "application/json; charset=utf-8", body);
        (void)send_all(client_fd, response);

        std::cout << "HTTP " << status_code
                  << " route=" << profile.route
                  << " matched=" << profile.matched
                  << " returned=" << profile.returned
                  << " ms=" << elapsed_ms
                  << '\n';

        ::close(client_fd);
        ++served_requests;
    }

    ::close(server_fd);
    return 0;
}

} // namespace osm

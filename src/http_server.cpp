#include "http_server.hpp"

#include "viewport_query.hpp"
#include "spatial_json.hpp"
#include <chrono>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace sheet1 {
namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
};

[[nodiscard]] std::optional<int> hex_value(const char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return 10 + character - 'a';
    }
    if (character >= 'A' && character <= 'F') {
        return 10 + character - 'A';
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> url_decode(const std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        const char character = input[index];
        if (character == '%') {
            if (index + 2 >= input.size()) {
                return std::nullopt;
            }
            const auto high = hex_value(input[index + 1]);
            const auto low = hex_value(input[index + 2]);
            if (!high.has_value() || !low.has_value()) {
                return std::nullopt;
            }
            output.push_back(static_cast<char>((*high << 4) | *low));
            index += 2;
        } else if (character == '+') {
            output.push_back(' ');
        } else {
            output.push_back(character);
        }
    }
    return output;
}

[[nodiscard]] std::optional<HttpRequest> parse_request_line(const std::string_view raw) {
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
    request.method = line.substr(0, method_end);
    const auto target = line.substr(method_end + 1, target_end - method_end - 1);
    const auto query_begin = target.find('?');
    if (query_begin == std::string_view::npos) {
        request.path = target;
    } else {
        request.path = target.substr(0, query_begin);
        request.query = target.substr(query_begin + 1);
    }
    return request;
}

[[nodiscard]] std::optional<std::string> query_parameter(const std::string_view query,
                                                         const std::string_view key) {
    std::size_t begin = 0;
    while (begin <= query.size()) {
        const auto end = query.find('&', begin);
        const auto token_end = end == std::string_view::npos ? query.size() : end;
        const auto token = query.substr(begin, token_end - begin);
        const auto equals = token.find('=');
        if (equals != std::string_view::npos && token.substr(0, equals) == key) {
            return url_decode(token.substr(equals + 1));
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> parse_double(const std::string_view text) {
    try {
        std::size_t parsed = 0;
        const auto value = std::stod(std::string{text}, &parsed);
        if (parsed != text.size() || !std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<BoundingBox> parse_viewport(const std::string_view csv) {
    std::array<double, 4> values{};
    std::size_t begin = 0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto end = csv.find(',', begin);
        if ((index + 1 < values.size() && end == std::string_view::npos) ||
            (index + 1 == values.size() && end != std::string_view::npos)) {
            return std::nullopt;
        }
        const auto token_end = end == std::string_view::npos ? csv.size() : end;
        const auto value = parse_double(csv.substr(begin, token_end - begin));
        if (!value.has_value()) {
            return std::nullopt;
        }
        values[index] = *value;
        begin = token_end + 1;
    }

    const double minimum_longitude = values[0];
    const double minimum_latitude = values[1];
    const double maximum_longitude = values[2];
    const double maximum_latitude = values[3];
    if (minimum_longitude < -180.0 || maximum_longitude > 180.0 ||
        minimum_latitude < -90.0 || maximum_latitude > 90.0 ||
        minimum_longitude > maximum_longitude || minimum_latitude > maximum_latitude) {
        return std::nullopt;
    }

    constexpr double scale = 10'000'000.0;
    return BoundingBox{
        .minimum = Coordinate{
            .latitude_e7 = static_cast<std::int32_t>(std::llround(minimum_latitude * scale)),
            .longitude_e7 = static_cast<std::int32_t>(std::llround(minimum_longitude * scale)),
        },
        .maximum = Coordinate{
            .latitude_e7 = static_cast<std::int32_t>(std::llround(maximum_latitude * scale)),
            .longitude_e7 = static_cast<std::int32_t>(std::llround(maximum_longitude * scale)),
        },
    };
}

void append_json_string(std::ostringstream& output, const std::string_view value) {
    output << '"';
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) {
                output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<int>(character) << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    output << '"';
}

void append_optional_string(std::ostringstream& output,
                            const DataStore& data,
                            const StringId id) {
    if (id == kInvalidStringId) {
        output << "null";
    } else {
        append_json_string(output, data.strings.resolve(id));
    }
}

[[nodiscard]] std::string serialize_houses(const DataStore& data,
                                           const ViewportQueryResult& query) {
    std::ostringstream output;
    output << "{\"matched\":" << query.matched
           << ",\"returned\":" << query.building_indices.size()
           << ",\"threshold\":" << kViewportBuildingThreshold
           << ",\"threshold_reached\":"
           << (query.threshold_reached ? "true" : "false")
           << ",\"matched_exact\":" << (query.threshold_reached ? "false" : "true")
           << ",\"scan_ms\":" << std::fixed << std::setprecision(3)
           << query.scan_milliseconds
           << ",\"houses\":[";

    bool first = true;
    for (const auto index : query.building_indices) {
        if (index >= data.buildings.size()) {
            continue;
        }
        const auto& building = data.buildings[index];
        if (!first) {
            output << ',';
        }
        first = false;
        output << "{\"id\":";
        append_json_string(output, format_osm_id(building.id));
        output << ",\"lat\":" << std::setprecision(7)
               << static_cast<double>(building.point.latitude_e7) / 10'000'000.0
               << ",\"lon\":"
               << static_cast<double>(building.point.longitude_e7) / 10'000'000.0
               << ",\"name\":";
        append_optional_string(output, data, building.name);
        output << ",\"building\":";
        append_optional_string(output, data, building.building_type);
        output << ",\"street\":";
        append_optional_string(output, data, building.street);
        output << ",\"house_number\":";
        append_optional_string(output, data, building.house_number);
        output << ",\"postcode\":";
        append_optional_string(output, data, building.postcode);
        output << ",\"city\":";
        append_optional_string(output, data, building.city);
        output << ",\"regions\":";
        append_regions_json(output,data,building.regions_begin,building.regions_count);
        output << '}';
    }
    output << "]}";
    return output.str();
}

[[nodiscard]] std::string error_json(const std::string_view message) {
    std::ostringstream output;
    output << "{\"error\":";
    append_json_string(output, message);
    output << '}';
    return output.str();
}

[[nodiscard]] std::string http_response(const int status,
                                        const std::string_view reason,
                                        const std::string_view body) {
    std::ostringstream output;
    output << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
           << "Content-Type: application/json; charset=utf-8\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Connection: close\r\n\r\n"
           << body;
    return output.str();
}

[[nodiscard]] std::string handle_request(const HttpRequest& request,
                                         const DataStore& data,
                                         const SpatialIndex& spatial,
                                         int& status,
                                         std::string_view& reason) {
    if (request.method != "GET") {
        status = 405;
        reason = "Method Not Allowed";
        return error_json("Only GET is supported");
    }
    if (request.path == "/health") {
        status = 200;
        reason = "OK";
        return "{\"status\":\"ok\"}";
    }
    if(request.path=="/reverse") {
        const auto lat=query_parameter(request.query,"lat"),lon=query_parameter(request.query,"lon");
        const auto y=lat?parse_double(*lat):std::nullopt,x=lon?parse_double(*lon):std::nullopt;
        if(!y||!x||std::abs(*y)>90||std::abs(*x)>180) {status=400;reason="Bad Request";return error_json("Expected valid lat and lon");}
        try {auto body=reverse_json(data,spatial,{static_cast<std::int32_t>(std::llround(*y*1e7)),static_cast<std::int32_t>(std::llround(*x*1e7))});status=200;reason="OK";return body;}
        catch(const std::exception& e){status=400;reason="Bad Request";return error_json(e.what());}
    }
    if (request.path != "/houses" && request.path!="/streets" && request.path!="/regions") {
        status = 404;
        reason = "Not Found";
        return error_json("Route not found");
    }

    const auto bbox_parameter = query_parameter(request.query, "bbox");
    if (!bbox_parameter.has_value()) {
        status = 400;
        reason = "Bad Request";
        return error_json("Missing bbox parameter");
    }
    const auto viewport = parse_viewport(*bbox_parameter);
    if (!viewport.has_value()) {
        status = 400;
        reason = "Bad Request";
        return error_json("Expected bbox=minLon,minLat,maxLon,maxLat");
    }

    status=200;reason="OK";
    if(request.path!="/houses") {
        try {return geometry_json(data,spatial,viewport->minimum,viewport->maximum,request.path=="/streets"?1:2);}
        catch(const std::exception& e){status=400;reason="Bad Request";return error_json(e.what());}
    }
    const auto start=std::chrono::steady_clock::now();
    ViewportQueryResult result;
    result.building_indices=spatial.viewport(viewport->minimum,viewport->maximum,0,kViewportBuildingThreshold+1);
    result.matched=result.building_indices.size();
    result.threshold_reached=result.matched>kViewportBuildingThreshold;
    if(result.threshold_reached)result.building_indices.resize(kViewportBuildingThreshold);
    result.scan_milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout << "Viewport houses matched=" << result.matched
              << " returned=" << result.building_indices.size()
              << " threshold_reached=" << (result.threshold_reached ? "yes" : "no")
              << " scan_ms=" << result.scan_milliseconds << '\n';
    status = 200;
    reason = "OK";
    return serialize_houses(data, result);
}

[[nodiscard]] bool send_all(const int socket, const std::string& response) {
    std::size_t sent = 0;
    while (sent < response.size()) {
        const auto written = ::send(socket,
                                    response.data() + sent,
                                    response.size() - sent,
                                    0);
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

} // namespace

int run_http_server(const DataStore& data,
                    const SpatialIndex& spatial,
                    const std::uint16_t port,
                    const std::size_t max_requests) {
    std::signal(SIGPIPE, SIG_IGN);
    const int server = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::cerr << "Could not create server socket: " << std::strerror(errno) << '\n';
        return 1;
    }

    int reuse_address = 1;
    if (::setsockopt(server,
                     SOL_SOCKET,
                     SO_REUSEADDR,
                     &reuse_address,
                     sizeof(reuse_address)) < 0) {
        std::cerr << "Could not configure server socket: " << std::strerror(errno) << '\n';
        ::close(server);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Could not bind server to port " << port << ": "
                  << std::strerror(errno) << '\n';
        ::close(server);
        return 1;
    }
    if (::listen(server, 16) < 0) {
        std::cerr << "Could not listen on server socket: " << std::strerror(errno) << '\n';
        ::close(server);
        return 1;
    }

    std::cout << "Viewport backend listening on http://127.0.0.1:" << port << '\n'
              << "Route: /houses?bbox=minLon,minLat,maxLon,maxLat\n"
              << "House threshold: " << kViewportBuildingThreshold << '\n';

    std::size_t served_requests = 0;
    while (max_requests == 0 || served_requests < max_requests) {
        sockaddr_in client_address{};
        socklen_t client_length = sizeof(client_address);
        const int client = ::accept(
            server, reinterpret_cast<sockaddr*>(&client_address), &client_length);
        if (client < 0) {
            std::cerr << "Could not accept request: " << std::strerror(errno) << '\n';
            continue;
        }

        std::array<char, 8192> request_buffer{};
        const auto received = ::recv(client, request_buffer.data(), request_buffer.size(), 0);
        int status = 400;
        std::string_view reason = "Bad Request";
        std::string body = error_json("Malformed HTTP request");
        if (received > 0) {
            const std::string_view raw_request{
                request_buffer.data(), static_cast<std::size_t>(received)};
            if (const auto request = parse_request_line(raw_request); request.has_value()) {
                body = handle_request(*request, data, spatial, status, reason);
            }
        }

        const auto response = http_response(status, reason, body);
        (void)send_all(client, response);
        ::close(client);
        ++served_requests;
    }

    ::close(server);
    return 0;
}

} // namespace sheet1

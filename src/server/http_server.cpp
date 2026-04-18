#include "server/http_server.hpp"

#include "query/bbox_query.hpp"
#include "query/json.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace osm {

namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
};

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
                return std::string(v);
            }
        }

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    return std::nullopt;
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
    const ParseStats& stats,
    int& status_code,
    std::string& status_text) {
    if (request.method != "GET") {
        status_code = 405;
        status_text = "Method Not Allowed";
        return serialize_error_json("Only GET is supported");
    }

    if (request.path == "/stats") {
        status_code = 200;
        status_text = "OK";
        return serialize_stats_json(stats);
    }

    if (request.path == "/houses" || request.path == "/streets") {
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
            const auto indices = query_houses_in_bbox(data, *bbox);
            return serialize_houses_json(data, indices);
        }

        const auto indices = query_streets_in_bbox(data, *bbox);
        return serialize_streets_json(data, indices);
    }

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
    const ParseStats& stats,
    const std::uint16_t port,
    const std::size_t max_requests) {
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

        if (const auto request = parse_request_line(request_view)) {
            body = handle_api_request(*request, data, stats, status_code, status_text);
        }

        const auto response = make_response(status_code, status_text, "application/json; charset=utf-8", body);
        (void)send_all(client_fd, response);

        ::close(client_fd);
        ++served_requests;
    }

    ::close(server_fd);
    return 0;
}

} // namespace osm

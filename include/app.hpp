#pragma once

#include <cstddef>
#include <cstdint>

namespace osm {

struct AppOptions {
    bool serve_http{false};
    std::uint16_t port{8080};
    std::size_t max_requests{0};
};

class App {
public:
    int run(const AppOptions& options = {}) const;
};

} // namespace osm

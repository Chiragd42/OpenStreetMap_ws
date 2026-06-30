#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace osm {

struct AppOptions {
    bool serve_http{false};
    bool merge_streets{true};
    std::uint16_t port{8080};
    std::size_t max_requests{0};
    std::string pbf_path;
    std::string save_cache_path;
    std::string load_cache_path;
    std::string test_search_query;
};

class App {
public:
    int run(const AppOptions& options = {}) const;
};

} // namespace osm

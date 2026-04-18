#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace osm {

struct ParseStats {
    std::uint64_t processed_nodes{0};
    std::uint64_t processed_ways{0};
    std::uint64_t processed_relations{0};

    std::uint64_t extracted_houses{0};
    std::uint64_t extracted_streets{0};
    std::uint64_t extracted_admin_areas{0};

    double parse_seconds{0.0};
    std::size_t estimated_memory_bytes{0};
};

class Stopwatch {
public:
    void start();
    [[nodiscard]] double elapsed_seconds() const;

private:
    std::chrono::steady_clock::time_point start_time_{};
    bool started_{false};
};

} // namespace osm

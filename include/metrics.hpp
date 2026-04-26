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
    std::uint64_t extracted_regions{0};

    std::uint64_t houses_from_address_nodes{0};
    std::uint64_t houses_from_polygon_centroid{0};
    std::uint64_t houses_from_polygon_bbox_fallback{0};
    std::uint64_t houses_skipped_invalid_geometry{0};

    std::uint64_t unnamed_streets{0};
    std::uint64_t regions_skipped_complex_relations{0};

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

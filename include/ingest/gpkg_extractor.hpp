#pragma once

#include "metrics.hpp"
#include "model.hpp"

#include <cstddef>
#include <string>

namespace osm {

struct ExtractionConfig {
    // User requested fixed file naming/path; this is the default production input.
    std::string input_gpkg_path{"data/gpkg/stuttgart-regbez.gpkg"};

    bool include_admin_boundaries{false};

    // Optional row limits (0 means unlimited).
    std::size_t max_road_rows{0};
    std::size_t max_building_rows{0};
};

struct ExtractionResult {
    DataStore data;
    ParseStats stats;
};

class GpkgExtractor {
public:
    [[nodiscard]] ExtractionResult extract(const ExtractionConfig& config) const;
};

} // namespace osm

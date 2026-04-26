#pragma once

#include "metrics.hpp"
#include "model.hpp"

#include <string>

namespace osm {

struct ExtractionConfig {
    std::string input_pbf_path{"data/pbf/stuttgart-regbez-260416.osm.pbf"};
    bool include_regions{true};
    float grid_cell_size_deg{0.01F};
};

struct ExtractionResult {
    DataStore data;
    ParseStats stats;
};

class PbfExtractor {
public:
    [[nodiscard]] ExtractionResult extract(const ExtractionConfig& config) const;
};

} // namespace osm

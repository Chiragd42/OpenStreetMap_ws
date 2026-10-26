#pragma once

#include "osm_model.hpp"

#include <cstdint>
#include <filesystem>

namespace sheet1 {

struct ExtractionStats {
    std::uint64_t processed_nodes{0};
    std::uint64_t processed_ways{0};
    std::uint64_t processed_relations{0};
    std::uint64_t extracted_buildings{0};
    std::uint64_t extracted_streets{0};
    std::uint64_t extracted_administrative_areas{0};
    std::uint64_t buildings_without_names{0};
    std::uint64_t buildings_without_points{0};
    std::uint64_t invalid_way_node_locations{0};
    std::uint64_t temporary_node_index_bytes{0};
    double elapsed_seconds{0.0};
    double cpu_seconds{0.0};
    double peak_memory_mib{0.0};
    std::uint64_t stored_data_bytes{0};
};

struct ExtractionResult {
    DataStore data;
    ExtractionStats stats;
};

[[nodiscard]] ExtractionResult extract_pbf(const std::filesystem::path& input_path);
void print_extraction_report(const std::filesystem::path& input_path,
                             const ExtractionResult& result);

} // namespace sheet1

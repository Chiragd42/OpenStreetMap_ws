#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace sheet1 {

struct ObjectSample {
    std::string typed_id;
    std::string description;
};

struct InspectionStats {
    std::uint64_t nodes{0};
    std::uint64_t ways{0};
    std::uint64_t relations{0};

    std::uint64_t address_nodes{0};
    std::uint64_t address_only_nodes{0};
    std::uint64_t building_nodes{0};
    std::uint64_t building_nodes_with_address{0};
    std::uint64_t building_ways{0};
    std::uint64_t building_relations{0};
    std::uint64_t building_way_node_references{0};
    std::uint64_t buildings_without_address{0};
    std::uint64_t buildings_without_name{0};

    std::uint64_t street_ways{0};
    std::uint64_t named_street_ways{0};
    std::uint64_t unnamed_street_ways{0};
    std::uint64_t street_node_references{0};

    std::uint64_t administrative_ways{0};
    std::uint64_t administrative_way_node_references{0};
    std::uint64_t administrative_relations{0};
    std::uint64_t administrative_relations_without_name{0};
    std::uint64_t administrative_outer_members{0};
    std::uint64_t administrative_inner_members{0};
    std::uint64_t administrative_other_members{0};

    std::uint64_t multipolygon_relations{0};
    std::uint64_t relation_members{0};
    std::uint64_t invalid_node_locations{0};

    std::map<std::string, std::uint64_t> building_type_counts;
    std::map<std::string, std::uint64_t> highway_class_counts;
    std::map<std::string, std::uint64_t> administrative_level_counts;

    std::vector<ObjectSample> node_samples;
    std::vector<ObjectSample> building_samples;
    std::vector<ObjectSample> street_samples;
    std::vector<ObjectSample> administrative_samples;

    double elapsed_seconds{0.0};
    double cpu_seconds{0.0};
    double peak_memory_mib{0.0};
    std::uintmax_t input_bytes{0};
};

[[nodiscard]] InspectionStats inspect_pbf(const std::filesystem::path& input_path,
                                          std::size_t sample_limit = 3);
void print_report(const std::filesystem::path& input_path, const InspectionStats& stats);

} // namespace sheet1

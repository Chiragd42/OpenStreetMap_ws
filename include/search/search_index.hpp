#pragma once

#include "model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace osm::search {

struct SearchIndex {
    std::unordered_map<std::string, std::vector<SearchObjectRef>> exact_name_index;
    std::unordered_map<std::string, std::vector<SearchObjectRef>> token_index;
    std::unordered_map<std::string, std::vector<std::uint32_t>> region_name_index;
    std::unordered_map<std::string, std::vector<std::uint32_t>> locality_name_index;
};

struct SearchIndexMetrics {
    std::size_t indexed_streets{0};
    std::size_t skipped_unnamed_streets{0};
    std::size_t indexed_pois{0};
    std::size_t indexed_regions{0};
    std::size_t indexed_localities{0};
    std::size_t skipped_pois_invalid_name_id{0};
    std::size_t skipped_pois_empty_normalized_name{0};
    std::size_t regions_seen{0};
    std::size_t regions_skipped_invalid_name_id{0};
    std::size_t regions_skipped_empty_normalized_name{0};
    std::size_t exact_name_keys{0};
    std::size_t exact_name_postings{0};
    std::size_t token_keys{0};
    std::size_t token_postings{0};
    std::size_t region_name_keys{0};
    std::size_t region_name_postings{0};
    std::size_t locality_name_keys{0};
    std::size_t locality_name_postings{0};
    std::string longest_posting_token;
    std::size_t longest_posting_list{0};
    std::vector<std::pair<std::string, std::size_t>> largest_token_postings;
    std::vector<std::string> skipped_poi_examples;
    std::vector<std::string> indexed_region_examples;
    std::vector<std::string> skipped_region_examples;
    double build_seconds{0.0};
};

struct SearchIndexBuildResult {
    SearchIndex index;
    SearchIndexMetrics metrics;
};

[[nodiscard]] SearchIndexBuildResult buildSearchIndex(const DataStore& data);
[[nodiscard]] std::vector<SearchObjectRef> intersectPostingLists(const std::vector<std::vector<SearchObjectRef>>& postings);

} // namespace osm::search

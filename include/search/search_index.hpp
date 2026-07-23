#pragma once

#include "model.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace osm::search {

enum class IndexedNameKind : std::uint8_t {
    Street,
    Locality,
    Region,
    Poi
};

struct IndexedName {
    std::string normalized_name;
    IndexedNameKind kind{IndexedNameKind::Street};
    std::vector<SearchObjectRef> objects;
};

struct SuffixRef {
    std::uint32_t name_id{0};
    std::uint32_t offset{0};
};

struct SubstringNameMatch {
    std::uint32_t name_id{0};
    bool starts_at_name_begin{false};
};

struct FuzzyTokenMatch {
    std::string token;
    IndexedNameKind kind{IndexedNameKind::Street};
    std::size_t distance{0};
};

struct BkTreeNode {
    std::string token;
    std::unordered_map<std::size_t, std::uint32_t> children;
};

struct BkTree {
    std::vector<BkTreeNode> nodes;
};

struct SearchIndex {
    std::unordered_map<std::string, std::vector<SearchObjectRef>> exact_name_index;
    std::unordered_map<std::string, std::vector<SearchObjectRef>> token_index;
    std::unordered_map<std::string, std::vector<std::uint32_t>> region_name_index;
    std::unordered_map<std::string, std::vector<std::uint32_t>> locality_name_index;
    std::unordered_map<std::string, std::vector<SearchObjectRef>> address_index;
    std::vector<IndexedName> names;
    std::vector<SuffixRef> suffix_array;
    std::array<BkTree, 4> fuzzy_trees;
    std::array<
        std::unordered_map<GridCellKey, std::vector<std::uint32_t>, GridCellKeyHash>,
        static_cast<std::size_t>(PoiCategory::Other) + 1>
        poi_cells_by_category;
};

struct SearchIndexMetrics {
    std::size_t indexed_streets{0};
    std::size_t skipped_unnamed_streets{0};
    std::size_t indexed_pois{0};
    std::size_t indexed_regions{0};
    std::size_t indexed_localities{0};
    std::size_t indexed_addresses{0};
    std::size_t skipped_addresses_missing_street{0};
    std::size_t skipped_addresses_missing_house_number{0};
    std::size_t skipped_addresses_empty_normalized_key{0};
    std::size_t skipped_pois_invalid_name_id{0};
    std::size_t skipped_pois_empty_normalized_name{0};
    std::size_t regions_seen{0};
    std::size_t regions_skipped_invalid_name_id{0};
    std::size_t regions_skipped_empty_normalized_name{0};
    std::size_t address_keys{0};
    std::size_t address_postings{0};
    std::size_t exact_name_keys{0};
    std::size_t exact_name_postings{0};
    std::size_t token_keys{0};
    std::size_t token_postings{0};
    std::size_t region_name_keys{0};
    std::size_t region_name_postings{0};
    std::size_t locality_name_keys{0};
    std::size_t locality_name_postings{0};
    std::size_t indexed_full_names{0};
    std::size_t suffix_count{0};
    std::size_t estimated_suffix_bytes{0};
    std::size_t fuzzy_vocabulary_tokens{0};
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

[[nodiscard]] std::string makeAddressKey(std::string_view normalized_street, std::string_view normalized_house_number);
[[nodiscard]] SearchIndexBuildResult buildSearchIndex(const DataStore& data);
[[nodiscard]] std::vector<SearchObjectRef> intersectPostingLists(const std::vector<std::vector<SearchObjectRef>>& postings);
[[nodiscard]] std::vector<SubstringNameMatch> findSubstringNameMatches(
    const SearchIndex& index,
    std::string_view normalized_fragment,
    std::size_t limit = 32,
    std::optional<IndexedNameKind> required_kind = std::nullopt);
[[nodiscard]] std::vector<FuzzyTokenMatch> findFuzzyTokenMatches(
    const SearchIndex& index,
    std::string_view token,
    IndexedNameKind kind,
    std::size_t max_distance,
    std::size_t limit = 4);

} // namespace osm::search

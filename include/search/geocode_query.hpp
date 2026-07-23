#pragma once

#include "model.hpp"
#include "search/search_index.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace osm::search {

enum class QueryIntent : std::uint8_t {
    Address,
    NamedObject,
    NearestCategory,
    Unknown
};

enum class QueryMatchStrategy : std::uint8_t {
    Original,
    Fuzzy,
    Partial
};

struct QueryInterpretation {
    QueryIntent intent{QueryIntent::Unknown};
    QueryMatchStrategy match_strategy{QueryMatchStrategy::Original};
    std::string normalized_query;
    std::vector<std::string> tokens;
    std::string entity_name;
    std::string normalized_house_number;
    std::size_t house_token_begin{0};
    std::size_t house_token_end{0};
    std::vector<std::uint32_t> locality_indices;
    std::size_t locality_token_begin{0};
    std::size_t locality_token_end{0};
    std::size_t unexplained_token_count{0};
    bool exact_entity_name_match{false};
    bool exact_address_key_match{false};
    std::size_t raw_candidate_count{0};
    std::size_t edit_cost{0};
};

struct GeocodeCandidate {
    SearchObjectRef ref{};
    std::size_t interpretation_index{0};
    QueryMatchStrategy match_strategy{QueryMatchStrategy::Original};
    std::uint64_t shared_relation_id{0};
    StringId shared_relation_name_id{kInvalidStringId};
    std::int32_t shared_admin_level{-1};
    double distance_to_locality_m{std::numeric_limits<double>::infinity()};
    bool exact_name_match{false};
    bool exact_address_match{false};
    bool locality_recognized{false};
    std::size_t unexplained_token_count{std::numeric_limits<std::size_t>::max()};
    std::size_t matched_entity_token_count{0};
    std::size_t edit_cost{0};
    std::size_t source_name_postings{0};
    double nearest_distance_m{std::numeric_limits<double>::infinity()};
    bool in_viewport{false};
    double distance_to_viewport_center_m{std::numeric_limits<double>::infinity()};
};

struct GeocodeCluster {
    std::size_t representative_candidate_index{0};
    std::vector<std::size_t> member_candidate_indices;
    double lat{0.0};
    double lon{0.0};
};

struct GeocodeQueryTimings {
    double normalization_ms{0.0};
    double interpretation_ms{0.0};
    double candidate_lookup_ms{0.0};
    double region_matching_ms{0.0};
    double ranking_ms{0.0};
    double total_ms{0.0};
};

struct GeocodeQueryResult {
    std::string input;
    std::string normalized_query;
    std::string corrected_query;
    std::vector<QueryInterpretation> interpretations;
    std::vector<GeocodeCandidate> ranked_candidates;
    std::vector<GeocodeCluster> clusters;
    std::optional<BBox> viewport;
    std::optional<BBox> result_bounds;
    bool viewport_applied{false};
    bool viewport_filtered{false};
    bool viewport_fallback{false};
    std::size_t global_candidate_count{0};
    std::size_t in_viewport_candidate_count{0};
    bool nearest_category_intent{false};
    std::optional<PoiCategory> nearest_category;
    std::string reference_query;
    bool reference_resolved{false};
    std::string reference_label;
    double reference_lat{0.0};
    double reference_lon{0.0};
    std::string failure_reason;
    std::size_t spatial_cells_examined{0};
    std::size_t spatial_pois_tested{0};
    GeocodeQueryTimings timings;
};

struct GeocodeQueryOptions {
    std::size_t max_ranked_candidates{200};
    std::optional<BBox> viewport;
    double cluster_threshold_m{20.0};
};

[[nodiscard]] const char* queryIntentName(QueryIntent intent);
[[nodiscard]] const char* queryMatchStrategyName(QueryMatchStrategy strategy);
[[nodiscard]] int regionSpecificity(std::int32_t admin_level);
[[nodiscard]] GeocodeQueryResult runGeocodeQuery(
    const DataStore& data,
    const SearchIndex& index,
    const std::string& input,
    const GeocodeQueryOptions& options = {});

} // namespace osm::search
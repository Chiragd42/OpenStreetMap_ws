#pragma once

#include "model.hpp"
#include "search/search_index.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace osm::search {

enum class QueryIntent : std::uint8_t {
    Address,
    NamedObject,
    Unknown
};

struct QueryInterpretation {
    QueryIntent intent{QueryIntent::Unknown};
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
};

struct GeocodeCandidate {
    SearchObjectRef ref{};
    std::size_t interpretation_index{0};
    std::uint64_t shared_relation_id{0};
    StringId shared_relation_name_id{kInvalidStringId};
    std::int32_t shared_admin_level{-1};
    double distance_to_locality_m{std::numeric_limits<double>::infinity()};
    bool exact_name_match{false};
    bool exact_address_match{false};
    bool locality_recognized{false};
    std::size_t unexplained_token_count{std::numeric_limits<std::size_t>::max()};
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
    std::vector<QueryInterpretation> interpretations;
    std::vector<GeocodeCandidate> ranked_candidates;
    GeocodeQueryTimings timings;
};

struct GeocodeQueryOptions {
    std::size_t max_ranked_candidates{200};
};

[[nodiscard]] const char* queryIntentName(QueryIntent intent);
[[nodiscard]] int regionSpecificity(std::int32_t admin_level);
[[nodiscard]] GeocodeQueryResult runGeocodeQuery(
    const DataStore& data,
    const SearchIndex& index,
    const std::string& input,
    const GeocodeQueryOptions& options = {});

} // namespace osm::search
#include "app.hpp"

#include "cache/datastore_cache.hpp"
#include "ingest/pbf_extractor.hpp"
#include "search/geocode_query.hpp"
#include "search/search_index.hpp"
#include "search/text_normalizer.hpp"
#include "server/http_server.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace osm {

namespace {

[[nodiscard]] const char* poi_category_name(const PoiCategory category) {
    switch (category) {
        case PoiCategory::Shop: return "Shops";
        case PoiCategory::Restaurant: return "Restaurants";
        case PoiCategory::Cafe: return "Cafes";
        case PoiCategory::FastFood: return "Fast food";
        case PoiCategory::Park: return "Parks";
        case PoiCategory::Hotel: return "Hotels";
        case PoiCategory::School: return "Schools";
        case PoiCategory::Hospital: return "Hospitals";
        case PoiCategory::Station: return "Stations";
        case PoiCategory::Other: return "Other";
    }
    return "Other";
}


[[nodiscard]] const char* poi_category_label(const PoiCategory category) {
    switch (category) {
        case PoiCategory::Shop: return "Shop";
        case PoiCategory::Restaurant: return "Restaurant";
        case PoiCategory::Cafe: return "Cafe";
        case PoiCategory::FastFood: return "FastFood";
        case PoiCategory::Park: return "Park";
        case PoiCategory::Hotel: return "Hotel";
        case PoiCategory::School: return "School";
        case PoiCategory::Hospital: return "Hospital";
        case PoiCategory::Station: return "Station";
        case PoiCategory::Other: return "Other";
    }
    return "Other";
}

[[nodiscard]] const char* osm_element_type_label(const OsmElementType type) {
    switch (type) {
        case OsmElementType::Node: return "node";
        case OsmElementType::Way: return "way";
    }
    return "unknown";
}

[[nodiscard]] const char* locality_type_label(const LocalityType type) {
    switch (type) {
        case LocalityType::City: return "City";
        case LocalityType::Town: return "Town";
        case LocalityType::Village: return "Village";
        case LocalityType::Municipality: return "Municipality";
        case LocalityType::Suburb: return "Suburb";
        case LocalityType::Borough: return "Borough";
        case LocalityType::Quarter: return "Quarter";
        case LocalityType::Neighbourhood: return "Neighbourhood";
        case LocalityType::Hamlet: return "Hamlet";
        case LocalityType::Other: return "Other";
    }
    return "Other";
}

[[nodiscard]] std::string resolve_string_or_empty(const DataStore& data, const StringId id) {
    if (id == kInvalidStringId || id >= data.strings.size()) {
        return {};
    }
    return data.strings.resolve(id);
}

void print_search_object(const DataStore& data, const SearchObjectRef& ref) {
    switch (ref.type) {
        case SearchObjectType::Street: {
            if (ref.index >= data.streets.size()) {
                std::cout << "    [Street] <invalid index " << ref.index << ">\n";
                return;
            }
            const auto& street = data.streets[ref.index];
            std::cout << "    [Street] " << resolve_string_or_empty(data, street.name_id)
                      << " | index " << ref.index
                      << " | points " << street.points_count << "\n";
            return;
        }
        case SearchObjectType::Poi: {
            if (ref.index >= data.pois.size()) {
                std::cout << "    [POI] <invalid index " << ref.index << ">\n";
                return;
            }
            const auto& poi = data.pois[ref.index];
            std::cout << "    [POI] " << resolve_string_or_empty(data, poi.name_id) << "\n"
                      << "      category: " << poi_category_label(poi.category) << "\n"
                      << "      subtype: " << resolve_string_or_empty(data, poi.subtype_id) << "\n"
                      << "      osm_type: " << osm_element_type_label(poi.osm_type) << "\n"
                      << "      osm_id: " << poi.osm_id << "\n"
                      << "      lat: " << poi.lat << "\n"
                      << "      lon: " << poi.lon << "\n";
            return;
        }
        case SearchObjectType::Region: {
            if (ref.index >= data.regions.size()) {
                std::cout << "    [Region] <invalid index " << ref.index << ">\n";
                return;
            }
            const auto& region = data.regions[ref.index];
            std::cout << "    [Region] " << resolve_string_or_empty(data, region.name_id)
                      << " | admin_level " << region.admin_level
                      << " | index " << ref.index << "\n";
            return;
        }
        case SearchObjectType::Locality: {
            if (ref.index >= data.localities.size()) {
                std::cout << "    [Locality] <invalid index " << ref.index << ">\n";
                return;
            }
            const auto& locality = data.localities[ref.index];
            std::cout << "    [Locality] " << resolve_string_or_empty(data, locality.name_id) << "\n"
                      << "      type: " << locality_type_label(locality.type) << "\n"
                      << "      osm_id: " << locality.osm_id << "\n"
                      << "      lat: " << locality.lat << "\n"
                      << "      lon: " << locality.lon << "\n"
                      << "      containing_regions: " << locality.containing_regions_count << "\n";
            const auto begin = static_cast<std::size_t>(locality.containing_regions_begin);
            const auto count = static_cast<std::size_t>(locality.containing_regions_count);
            for (std::size_t i = 0; i < std::min<std::size_t>(count, 5); ++i) {
                const auto ridx = static_cast<std::size_t>(data.locality_containing_region_ids[begin + i]);
                if (ridx < data.regions.size()) {
                    const auto& region = data.regions[ridx];
                    std::cout << "        polygon_index: " << ridx
                              << " relation_id: " << region.source_relation_id
                              << " name: " << resolve_string_or_empty(data, region.name_id)
                              << " admin_level: " << region.admin_level << "\n";
                }
            }
            return;
        }
        case SearchObjectType::House: {
            if (ref.index >= data.houses.size()) {
                std::cout << "    [House] <invalid index " << ref.index << ">\n";
                return;
            }
            const auto& house = data.houses[ref.index];
            std::cout << "    [House] " << resolve_string_or_empty(data, house.street_name_id)
                      << " " << resolve_string_or_empty(data, house.house_number_id) << "\n"
                      << "      city: " << resolve_string_or_empty(data, house.city_id) << "\n"
                      << "      postcode: " << resolve_string_or_empty(data, house.postcode_id) << "\n"
                      << "      lat: " << house.lat << "\n"
                      << "      lon: " << house.lon << "\n"
                      << "      containing_regions: " << house.containing_regions_count << "\n";
            return;
        }
    }
}

void print_result_sample(const DataStore& data, const std::vector<SearchObjectRef>& refs, const std::size_t limit) {
    const auto shown = std::min(limit, refs.size());
    for (std::size_t i = 0; i < shown; ++i) {
        print_search_object(data, refs[i]);
    }
    std::cout << "  showing: " << shown << " of " << refs.size() << '\n';
}

[[nodiscard]] std::string normalized_token_span(const std::vector<std::string>& tokens, const std::size_t begin, const std::size_t end) {
    std::string out;
    for (std::size_t i = begin; i < end && i < tokens.size(); ++i) {
        if (!out.empty()) out.push_back(' ');
        out += tokens[i];
    }
    return out;
}

[[nodiscard]] std::string first_locality_name(const DataStore& data, const std::vector<std::uint32_t>& locality_indices) {
    if (locality_indices.empty() || locality_indices.front() >= data.localities.size()) {
        return {};
    }
    return resolve_string_or_empty(data, data.localities[locality_indices.front()].name_id);
}

void print_geocode_object_line(const DataStore& data, const SearchObjectRef& ref) {
    switch (ref.type) {
        case SearchObjectType::House: {
            if (ref.index >= data.houses.size()) {
                std::cout << "[House] <invalid>";
                return;
            }
            const auto& house = data.houses[ref.index];
            std::cout << "[House] " << resolve_string_or_empty(data, house.street_name_id)
                      << " " << resolve_string_or_empty(data, house.house_number_id);
            return;
        }
        case SearchObjectType::Street: {
            if (ref.index >= data.streets.size()) {
                std::cout << "[Street] <invalid>";
                return;
            }
            std::cout << "[Street] " << resolve_string_or_empty(data, data.streets[ref.index].name_id);
            return;
        }
        case SearchObjectType::Poi: {
            if (ref.index >= data.pois.size()) {
                std::cout << "[POI] <invalid>";
                return;
            }
            std::cout << "[POI] " << resolve_string_or_empty(data, data.pois[ref.index].name_id);
            return;
        }
        case SearchObjectType::Region: {
            if (ref.index >= data.regions.size()) {
                std::cout << "[Region] <invalid>";
                return;
            }
            std::cout << "[Region] " << resolve_string_or_empty(data, data.regions[ref.index].name_id);
            return;
        }
        case SearchObjectType::Locality: {
            if (ref.index >= data.localities.size()) {
                std::cout << "[Locality] <invalid>";
                return;
            }
            std::cout << "[Locality] " << resolve_string_or_empty(data, data.localities[ref.index].name_id);
            return;
        }
    }
}

[[nodiscard]] std::string rank_reason(const search::GeocodeCandidate& candidate) {
    std::string reason;
    if (candidate.exact_address_match) reason += "exact address";
    if (candidate.exact_name_match) {
        if (!reason.empty()) reason += " + ";
        reason += "exact name";
    }
    if (candidate.shared_admin_level != -1) {
        if (!reason.empty()) reason += " + ";
        reason += "shared level " + std::to_string(candidate.shared_admin_level) + " relation";
    } else if (candidate.locality_recognized) {
        if (!reason.empty()) reason += " + ";
        reason += "recognized locality";
    }
    if (candidate.unexplained_token_count > 0 && candidate.unexplained_token_count != std::numeric_limits<std::size_t>::max()) {
        if (!reason.empty()) reason += " + ";
        reason += std::to_string(candidate.unexplained_token_count) + " unexplained token(s)";
    }
    if (reason.empty()) reason = "stable fallback ordering";
    return reason;
}

void run_test_geocode_query(const DataStore& data, const search::SearchIndex& index, const std::string& input) {
    const auto result = search::runGeocodeQuery(data, index, input);
    std::cout << "\n[TestGeocodeQuery]\n"
              << "  input: " << result.input << '\n'
              << "  normalized: " << result.normalized_query << '\n'
              << "  query_time_ms: " << result.timings.total_ms << '\n'
              << "  normalization_ms: " << result.timings.normalization_ms << '\n'
              << "  interpretation_ms: " << result.timings.interpretation_ms << '\n'
              << "  candidate_lookup_ms: " << result.timings.candidate_lookup_ms << '\n'
              << "  region_matching_ms: " << result.timings.region_matching_ms << '\n'
              << "  ranking_ms: " << result.timings.ranking_ms << "\n\n";

    std::cout << "  interpretations:\n";
    const auto interpretation_limit = std::min<std::size_t>(result.interpretations.size(), 10);
    for (std::size_t i = 0; i < interpretation_limit; ++i) {
        const auto& interpretation = result.interpretations[i];
        std::cout << "    " << (i + 1) << ".\n"
                  << "      intent: " << search::queryIntentName(interpretation.intent) << '\n';
        if (!interpretation.locality_indices.empty()) {
            std::cout << "      locality_span: " << normalized_token_span(interpretation.tokens, interpretation.locality_token_begin, interpretation.locality_token_end) << '\n'
                      << "      locality_candidates: " << interpretation.locality_indices.size() << '\n'
                      << "      first_locality: " << first_locality_name(data, interpretation.locality_indices) << '\n';
        } else {
            std::cout << "      locality_span: <none>\n"
                      << "      locality_candidates: 0\n";
        }
        std::cout << "      entity: " << interpretation.entity_name << '\n';
        if (interpretation.intent == search::QueryIntent::Address) {
            std::cout << "      house_number: " << interpretation.normalized_house_number << '\n'
                      << "      house_token_span: [" << interpretation.house_token_begin << ", " << interpretation.house_token_end << ")\n"
                      << "      address_key_found: " << (interpretation.exact_address_key_match ? "true" : "false") << '\n';
        } else {
            std::cout << "      exact_entity_name_match: " << (interpretation.exact_entity_name_match ? "true" : "false") << '\n';
        }
        std::cout << "      unexplained_tokens: " << interpretation.unexplained_token_count << '\n'
                  << "      raw_candidates: " << interpretation.raw_candidate_count << '\n';
    }
    std::cout << "  interpretations_shown: " << interpretation_limit << " of " << result.interpretations.size() << "\n\n";

    std::cout << "  ranked_results:\n";
    const auto result_limit = std::min<std::size_t>(result.ranked_candidates.size(), 10);
    for (std::size_t i = 0; i < result_limit; ++i) {
        const auto& candidate = result.ranked_candidates[i];
        std::cout << "    " << (i + 1) << ". ";
        print_geocode_object_line(data, candidate.ref);
        std::cout << '\n';
        if (candidate.ref.type == SearchObjectType::House && candidate.ref.index < data.houses.size()) {
            const auto& house = data.houses[candidate.ref.index];
            std::cout << "       city: " << resolve_string_or_empty(data, house.city_id) << '\n'
                      << "       postcode: " << resolve_string_or_empty(data, house.postcode_id) << '\n';
        }
        std::cout << "       exact_address: " << (candidate.exact_address_match ? "true" : "false") << '\n'
                  << "       exact_name: " << (candidate.exact_name_match ? "true" : "false") << '\n'
                  << "       locality_recognized: " << (candidate.locality_recognized ? "true" : "false") << '\n'
                  << "       shared_relation: " << resolve_string_or_empty(data, candidate.shared_relation_name_id) << '\n'
                  << "       shared_relation_id: " << candidate.shared_relation_id << '\n'
                  << "       shared_admin_level: " << candidate.shared_admin_level << '\n'
                  << "       distance_to_locality_m: " << candidate.distance_to_locality_m << '\n'
                  << "       rank_reason: " << rank_reason(candidate) << '\n';
    }
    std::cout << "  ranked_results_shown: " << result_limit << " of " << result.ranked_candidates.size() << '\n';
}

void run_test_search(const DataStore& data, const search::SearchIndex& index, const std::string& input) {
    const auto normalized = search::normalizeSearchText(input);
    const auto tokens = search::tokenizeNormalizedText(normalized);

    std::cout << "\n[TestSearch]\n"
              << "  input: " << input << '\n'
              << "  normalized: " << normalized << '\n';

    if (normalized.empty()) {
        std::cout << "  normalized query is empty; no search performed.\n";
        return;
    }

    std::cout << "  tokens:\n";
    for (const auto& token : tokens) {
        std::cout << "    - " << token << '\n';
    }

    std::vector<SearchObjectRef> address_matches;
    if (!tokens.empty()) {
        for (std::size_t i = tokens.size(); i > 0; --i) {
            const auto& candidate = tokens[i - 1];
            const bool has_digit = std::any_of(candidate.begin(), candidate.end(), [](const char c) {
                return c >= '0' && c <= '9';
            });
            if (!has_digit) continue;

            std::string house_number_raw = candidate;
            std::size_t extra_house_token = tokens.size();
            if (i < tokens.size() && tokens[i].size() == 1 && tokens[i][0] >= 'a' && tokens[i][0] <= 'z') {
                house_number_raw += tokens[i];
                extra_house_token = i;
            }
            const auto house_number = search::normalizeHouseNumber(house_number_raw);
            std::string street_candidate;
            for (std::size_t j = 0; j < tokens.size(); ++j) {
                if (j == i - 1 || j == extra_house_token) continue;
                if (!street_candidate.empty()) street_candidate.push_back(' ');
                street_candidate += tokens[j];
            }
            const auto address_key = search::makeAddressKey(street_candidate, house_number);
            if (!address_key.empty()) {
                if (const auto it = index.address_index.find(address_key); it != index.address_index.end()) {
                    address_matches = it->second;
                }
                std::cout << "  address_lookup:\n"
                          << "    street: " << street_candidate << "\n"
                          << "    house_number: " << house_number << "\n"
                          << "    matches: " << address_matches.size() << "\n\n";
            }
            break;
        }
    }

    std::vector<SearchObjectRef> exact_matches;
    if (const auto it = index.exact_name_index.find(normalized); it != index.exact_name_index.end()) {
        exact_matches = it->second;
    }
    std::cout << "  exact_name_matches: " << exact_matches.size() << "\n\n";

    std::vector<std::vector<SearchObjectRef>> posting_lists;
    posting_lists.reserve(tokens.size());
    std::cout << "  token_postings:\n";
    bool missing_token = false;
    for (const auto& token : tokens) {
        const auto it = index.token_index.find(token);
        const auto count = it == index.token_index.end() ? 0 : it->second.size();
        std::cout << "    " << token << ": " << count << '\n';
        if (it == index.token_index.end()) {
            missing_token = true;
        } else {
            posting_lists.push_back(it->second);
        }
    }

    std::vector<SearchObjectRef> token_intersection;
    if (!missing_token && !posting_lists.empty()) {
        token_intersection = search::intersectPostingLists(posting_lists);
    }

    std::cout << "\n  token_intersection_matches: " << token_intersection.size() << "\n";

    if (!address_matches.empty()) {
        std::cout << "\n  first_address_results:\n";
        print_result_sample(data, address_matches, 5);
    }

    std::cout << "\n  first_exact_results:\n";
    print_result_sample(data, exact_matches, 5);
    std::cout << "\n  first_token_intersection_results:\n";
    print_result_sample(data, token_intersection, 5);
}

struct EndpointKey {
    std::int32_t lat_q{0};
    std::int32_t lon_q{0};

    [[nodiscard]] bool operator==(const EndpointKey& other) const noexcept {
        return lat_q == other.lat_q && lon_q == other.lon_q;
    }
};

struct EndpointKeyHash {
    [[nodiscard]] std::size_t operator()(const EndpointKey& key) const noexcept {
        const auto h1 = static_cast<std::size_t>(static_cast<std::uint32_t>(key.lat_q));
        const auto h2 = static_cast<std::size_t>(static_cast<std::uint32_t>(key.lon_q));
        return (h1 * 73856093U) ^ (h2 * 19349663U);
    }
};

struct StreetMergeStats {
    std::size_t raw_streets{0};
    std::size_t merged_streets{0};
    double merge_time_ms{0.0};
};

[[nodiscard]] EndpointKey point_to_key(const GeoPoint& p) {
    constexpr double scale = 1'000'000.0;
    return EndpointKey{
        .lat_q = static_cast<std::int32_t>(p.lat * scale),
        .lon_q = static_cast<std::int32_t>(p.lon * scale),
    };
}

[[nodiscard]] BBox bbox_from_points(const std::vector<GeoPoint>& points) {
    BBox bbox;
    if (points.empty()) {
        return bbox;
    }

    bbox.min_lon = points.front().lon;
    bbox.max_lon = points.front().lon;
    bbox.min_lat = points.front().lat;
    bbox.max_lat = points.front().lat;
    for (const auto& p : points) {
        bbox.min_lon = std::min(bbox.min_lon, static_cast<double>(p.lon));
        bbox.max_lon = std::max(bbox.max_lon, static_cast<double>(p.lon));
        bbox.min_lat = std::min(bbox.min_lat, static_cast<double>(p.lat));
        bbox.max_lat = std::max(bbox.max_lat, static_cast<double>(p.lat));
    }
    return bbox;
}

void add_bbox_to_grid(
    std::unordered_map<GridCellKey, std::vector<std::size_t>, GridCellKeyHash>& cells,
    const BBox& bbox,
    const float cell_size_deg,
    const std::size_t idx) {
    for (const auto& key : grid_cells_for_bbox(bbox, cell_size_deg)) {
        cells[key].push_back(idx);
    }
}

struct Dsu {
    std::vector<std::size_t> parent;
    std::vector<std::uint8_t> rank;

    explicit Dsu(const std::size_t n) : parent(n), rank(n, 0) {
        for (std::size_t i = 0; i < n; ++i) {
            parent[i] = i;
        }
    }

    std::size_t find(std::size_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }

    void unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a == b) {
            return;
        }
        if (rank[a] < rank[b]) {
            std::swap(a, b);
        }
        parent[b] = a;
        if (rank[a] == rank[b]) {
            ++rank[a];
        }
    }
};

StreetMergeStats merge_streets_in_place(DataStore& data) {
    StreetMergeStats out;
    out.raw_streets = data.streets.size();
    const auto t0 = std::chrono::steady_clock::now();

    if (data.streets.empty()) {
        out.merged_streets = 0;
        return out;
    }

    std::unordered_map<std::uint64_t, std::vector<std::size_t>> groups;
    groups.reserve(data.streets.size());
    for (std::size_t i = 0; i < data.streets.size(); ++i) {
        const auto& s = data.streets[i];
        if (s.points_count < 2) {
            continue;
        }
        const std::uint64_t key = (static_cast<std::uint64_t>(s.name_id) << 32U) |
                                  static_cast<std::uint64_t>(s.highway_class_id);
        groups[key].push_back(i);
    }

    std::vector<StreetPolyline> merged_streets;
    std::vector<GeoPoint> merged_points;
    std::vector<std::uint32_t> merged_street_region_ids;
    merged_streets.reserve(data.streets.size());
    merged_points.reserve(data.street_points.size());
    merged_street_region_ids.reserve(data.street_containing_region_ids.size());

    for (auto& [_, indices] : groups) {
        Dsu dsu(indices.size());
        std::unordered_map<EndpointKey, std::size_t, EndpointKeyHash> endpoint_owner;

        for (std::size_t local_idx = 0; local_idx < indices.size(); ++local_idx) {
            const auto& s = data.streets[indices[local_idx]];
            const auto begin = static_cast<std::size_t>(s.points_begin);
            const auto end = begin + static_cast<std::size_t>(s.points_count) - 1;
            const auto k0 = point_to_key(data.street_points[begin]);
            const auto k1 = point_to_key(data.street_points[end]);
            for (const auto key : std::array<EndpointKey, 2>{k0, k1}) {
                const auto it = endpoint_owner.find(key);
                if (it == endpoint_owner.end()) {
                    endpoint_owner.emplace(key, local_idx);
                } else {
                    dsu.unite(local_idx, it->second);
                }
            }
        }

        std::unordered_map<std::size_t, std::vector<std::size_t>> components;
        for (std::size_t local_idx = 0; local_idx < indices.size(); ++local_idx) {
            components[dsu.find(local_idx)].push_back(local_idx);
        }

        for (auto& [__, comp] : components) {
            std::unordered_map<EndpointKey, std::vector<std::size_t>, EndpointKeyHash> adj;
            adj.reserve(comp.size() * 2);

            std::vector<EndpointKey> seg_start(comp.size());
            std::vector<EndpointKey> seg_end(comp.size());
            std::vector<bool> used(comp.size(), false);

            for (std::size_t ci = 0; ci < comp.size(); ++ci) {
                const auto& s = data.streets[indices[comp[ci]]];
                const auto begin = static_cast<std::size_t>(s.points_begin);
                const auto end = begin + static_cast<std::size_t>(s.points_count) - 1;
                seg_start[ci] = point_to_key(data.street_points[begin]);
                seg_end[ci] = point_to_key(data.street_points[end]);
                adj[seg_start[ci]].push_back(ci);
                adj[seg_end[ci]].push_back(ci);
            }

            auto emit_trail = [&](const EndpointKey start_key) {
                std::vector<GeoPoint> poly;
                std::vector<std::uint32_t> region_ids;
                std::unordered_set<std::uint32_t> seen_region_ids;
                EndpointKey current = start_key;
                bool has_segment = false;

                while (true) {
                    auto it = adj.find(current);
                    if (it == adj.end()) {
                        break;
                    }
                    std::size_t chosen = comp.size();
                    for (const auto idx : it->second) {
                        if (!used[idx]) {
                            chosen = idx;
                            break;
                        }
                    }
                    if (chosen == comp.size()) {
                        break;
                    }

                    used[chosen] = true;
                    has_segment = true;

                    const auto global_street_idx = indices[comp[chosen]];
                    const auto& s = data.streets[global_street_idx];
                    const auto begin = static_cast<std::size_t>(s.points_begin);
                    const auto count = static_cast<std::size_t>(s.points_count);
                    const bool forward = (current == seg_start[chosen]);

                    const auto regions_begin = static_cast<std::size_t>(s.containing_regions_begin);
                    const auto regions_count = static_cast<std::size_t>(s.containing_regions_count);
                    if (regions_begin + regions_count <= data.street_containing_region_ids.size()) {
                        for (std::size_t ri = 0; ri < regions_count; ++ri) {
                            const auto region_index = data.street_containing_region_ids[regions_begin + ri];
                            if (seen_region_ids.insert(region_index).second) {
                                region_ids.push_back(region_index);
                            }
                        }
                    }

                    if (forward) {
                        for (std::size_t i = 0; i < count; ++i) {
                            if (!poly.empty() && i == 0) {
                                continue;
                            }
                            poly.push_back(data.street_points[begin + i]);
                        }
                        current = seg_end[chosen];
                    } else {
                        for (std::size_t i = 0; i < count; ++i) {
                            const auto rev_i = count - 1 - i;
                            if (!poly.empty() && i == 0) {
                                continue;
                            }
                            poly.push_back(data.street_points[begin + rev_i]);
                        }
                        current = seg_start[chosen];
                    }
                }

                if (has_segment && poly.size() >= 2) {
                    StreetPolyline out_street;
                    out_street.name_id = data.streets[indices[comp.front()]].name_id;
                    out_street.highway_class_id = data.streets[indices[comp.front()]].highway_class_id;
                    out_street.is_unnamed = data.streets[indices[comp.front()]].is_unnamed;
                    out_street.points_begin = static_cast<std::uint32_t>(merged_points.size());
                    out_street.points_count = static_cast<std::uint32_t>(poly.size());
                    out_street.containing_regions_begin = static_cast<std::uint32_t>(merged_street_region_ids.size());
                    out_street.containing_regions_count = static_cast<std::uint32_t>(region_ids.size());
                    out_street.bbox = bbox_from_points(poly);
                    merged_points.insert(merged_points.end(), poly.begin(), poly.end());
                    merged_street_region_ids.insert(merged_street_region_ids.end(), region_ids.begin(), region_ids.end());
                    merged_streets.push_back(out_street);
                }
            };

            for (const auto& [node, edges] : adj) {
                std::size_t degree = 0;
                for (const auto e : edges) {
                    if (!used[e]) {
                        ++degree;
                    }
                }
                if (degree % 2 == 1) {
                    emit_trail(node);
                }
            }

            for (std::size_t ci = 0; ci < comp.size(); ++ci) {
                if (!used[ci]) {
                    emit_trail(seg_start[ci]);
                }
            }
        }
    }

    data.streets = std::move(merged_streets);
    data.street_points = std::move(merged_points);
    data.street_containing_region_ids = std::move(merged_street_region_ids);

    data.grid.street_cells.clear();
    for (std::size_t i = 0; i < data.streets.size(); ++i) {
        add_bbox_to_grid(data.grid.street_cells, data.streets[i].bbox, data.grid.cell_size_deg, i);
    }

    out.merged_streets = data.streets.size();
    const auto t1 = std::chrono::steady_clock::now();
    out.merge_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return out;
}

void print_sheet_header(const int sheet) {
    std::cout << "\n==============================\n"
              << "[Sheet " << sheet << "]\n"
              << "==============================\n";
}

} // namespace

int App::run(const AppOptions& options) const {
    const auto app_start = std::chrono::steady_clock::now();
    DataStore data;
    ParseStats stats;
    std::string source_description;
    bool loaded_from_cache = false;

    if (!options.load_cache_path.empty()) {
        const auto t0 = std::chrono::steady_clock::now();
        std::string cache_error;
        if (!load_datastore_cache(options.load_cache_path, data, stats, cache_error)) {
            std::cerr << "Failed to load cache: " << cache_error << '\n';
            return 1;
        }
        const auto t1 = std::chrono::steady_clock::now();
        stats.parse_seconds = std::chrono::duration<double>(t1 - t0).count();
        loaded_from_cache = true;
        source_description = "cache: " + options.load_cache_path;
    } else {
        PbfExtractor extractor;
        ExtractionConfig config;
        if (!options.pbf_path.empty()) {
            config.input_pbf_path = options.pbf_path;
        }

        auto extracted = extractor.extract(config);
        data = std::move(extracted.data);
        stats = extracted.stats;
        source_description = "PBF: " + config.input_pbf_path;

        if (!options.save_cache_path.empty()) {
            std::string cache_error;
            if (!save_datastore_cache(options.save_cache_path, data, stats, cache_error)) {
                std::cerr << "Failed to save cache: " << cache_error << '\n';
                return 1;
            }
            std::cout << "Saved cache to: " << options.save_cache_path << '\n';
        }
    }

    StreetMergeStats merge_stats;
    merge_stats.raw_streets = data.streets.size();
    merge_stats.merged_streets = data.streets.size();
    if (options.merge_streets) {
        merge_stats = merge_streets_in_place(data);
    }

    const auto search_index_build = search::buildSearchIndex(data);
    const auto& search_metrics = search_index_build.metrics;

    std::cout << "OSM geocoder pipeline initialized (PBF-first, target: Baden-Wuerttemberg)." << '\n';
    std::cout << "Input source: " << source_description << '\n';

    const auto reduced =
        (merge_stats.raw_streets > merge_stats.merged_streets)
            ? (merge_stats.raw_streets - merge_stats.merged_streets)
            : 0;
    const double reduced_pct =
        merge_stats.raw_streets == 0
            ? 0.0
            : (static_cast<double>(reduced) * 100.0 / static_cast<double>(merge_stats.raw_streets));

    print_sheet_header(1);
    std::cout << "Processed nodes: " << stats.processed_nodes << '\n';
    std::cout << "Processed ways: " << stats.processed_ways << '\n';
    std::cout << "Processed relations: " << stats.processed_relations << '\n';
    std::cout << "Extracted houses: " << stats.extracted_houses << '\n';
    std::cout << "Extracted streets: " << stats.extracted_streets << '\n';
    std::cout << "Extracted regions: " << stats.extracted_regions << '\n';
    std::cout << "Houses from address nodes: " << stats.houses_from_address_nodes << '\n';
    std::cout << "Houses from polygon centroid: " << stats.houses_from_polygon_centroid << '\n';
    std::cout << "Houses from bbox fallback: " << stats.houses_from_polygon_bbox_fallback << '\n';
    std::cout << "Skipped invalid house geometries: " << stats.houses_skipped_invalid_geometry << '\n';
    std::cout << "Unnamed streets: " << stats.unnamed_streets << '\n';
    std::cout << "Skipped complex region relations: " << stats.regions_skipped_complex_relations << '\n';
    std::cout << "Street merge enabled: " << (options.merge_streets ? "yes" : "no") << '\n'
              << "Raw streets: " << merge_stats.raw_streets << '\n'
              << "Merged streets: " << merge_stats.merged_streets << '\n'
              << "Street reduction: " << reduced << " (" << reduced_pct << "%)\n"
              << "Street merge time ms: " << merge_stats.merge_time_ms << '\n';
    if (loaded_from_cache) {
        const auto startup_seconds = std::chrono::duration<double>(
                                         std::chrono::steady_clock::now() - app_start)
                                         .count();
        std::cout << "Load seconds: " << startup_seconds << '\n';
    } else {
        std::cout << "Parse seconds: " << stats.parse_seconds << '\n';
    }
    std::cout << "Estimated memory bytes: " << stats.estimated_memory_bytes << '\n';

    print_sheet_header(2);
    std::cout << "Houses with assigned city: " << stats.houses_with_assigned_city << '\n'
              << "Houses with assigned state: " << stats.houses_with_assigned_state << '\n'
              << "Houses with assigned postcode: " << stats.houses_with_assigned_postcode << '\n'
              << "Houses with assigned country: " << stats.houses_with_assigned_country << '\n'
              << "Country assigned by PIP: " << stats.country_assigned_by_pip << '\n'
              << "Country assigned by fallback: " << stats.country_assigned_by_fallback << '\n'
              << "Region assignment seconds: " << stats.region_assignment_seconds << '\n'
              << "Average PIP candidates per house: " << stats.avg_pip_candidates_per_house << '\n'
              << "Spatial index cells: " << stats.spatial_index_cells << '\n'
              << "Reverse index build seconds: " << stats.reverse_index_build_seconds << '\n';
    std::cout << "POI nodes: " << stats.extracted_poi_nodes << '\n'
              << "POI ways: " << stats.extracted_poi_ways << '\n'
              << "POIs total: " << stats.extracted_pois_total << '\n';
    for (std::size_t i = 0; i < kPoiCategoryCount; ++i) {
        std::cout << poi_category_name(static_cast<PoiCategory>(i)) << ": "
                  << stats.extracted_pois_by_category[i] << '\n';
    }
    std::cout << "Skipped unnamed POIs: " << stats.skipped_unnamed_pois << '\n'
              << "Invalid POI geometries: " << stats.skipped_invalid_poi_geometry << '\n'
              << "POIs assigned to region: " << stats.pois_assigned_to_region << '\n'
              << "POIs without region: " << stats.pois_without_region << '\n'
              << "POI-region assignment seconds: " << stats.poi_region_assignment_seconds << '\n';
    std::cout << "Localities total: " << stats.extracted_localities_total << '\n';
    for (std::size_t i = 0; i < kLocalityTypeCount; ++i) {
        std::cout << locality_type_label(static_cast<LocalityType>(i)) << ": "
                  << stats.extracted_localities_by_type[i] << '\n';
    }
    std::cout << "Localities skipped unnamed: " << stats.skipped_unnamed_localities << '\n'
              << "Localities assigned to region: " << stats.localities_assigned_to_region << '\n'
              << "Localities without region: " << stats.localities_without_region << '\n'
              << "Locality assignment seconds: " << stats.locality_region_assignment_seconds << '\n';

    print_sheet_header(3);
    std::cout << "indexed_streets: " << search_metrics.indexed_streets << '\n'
              << "  skipped_unnamed_streets: " << search_metrics.skipped_unnamed_streets << '\n'
              << "  indexed_pois: " << search_metrics.indexed_pois << '\n'
              << "  indexed_regions: " << search_metrics.indexed_regions << '\n'
              << "  indexed_localities: " << search_metrics.indexed_localities << '\n'
              << "  indexed_addresses: " << search_metrics.indexed_addresses << '\n'
              << "  skipped_addresses_missing_street: " << search_metrics.skipped_addresses_missing_street << '\n'
              << "  skipped_addresses_missing_house_number: " << search_metrics.skipped_addresses_missing_house_number << '\n'
              << "  skipped_addresses_empty_normalized_key: " << search_metrics.skipped_addresses_empty_normalized_key << '\n'
              << "  skipped_pois_invalid_name_id: " << search_metrics.skipped_pois_invalid_name_id << '\n'
              << "  skipped_pois_empty_normalized_name: " << search_metrics.skipped_pois_empty_normalized_name << '\n'
              << "  regions_seen: " << search_metrics.regions_seen << '\n'
              << "  regions_skipped_invalid_name_id: " << search_metrics.regions_skipped_invalid_name_id << '\n'
              << "  regions_skipped_empty_normalized_name: " << search_metrics.regions_skipped_empty_normalized_name << '\n'
              << "  address_keys: " << search_metrics.address_keys << '\n'
              << "  address_postings: " << search_metrics.address_postings << '\n'
              << "  exact_name_keys: " << search_metrics.exact_name_keys << '\n'
              << "  exact_name_postings: " << search_metrics.exact_name_postings << '\n'
              << "  token_keys: " << search_metrics.token_keys << '\n'
              << "  token_postings: " << search_metrics.token_postings << '\n'
              << "  region_name_keys: " << search_metrics.region_name_keys << '\n'
              << "  region_name_postings: " << search_metrics.region_name_postings << '\n'
              << "  locality_name_keys: " << search_metrics.locality_name_keys << '\n'
              << "  locality_name_postings: " << search_metrics.locality_name_postings << '\n'
              << "  indexed_full_names: " << search_metrics.indexed_full_names << '\n'
              << "  suffix_count: " << search_metrics.suffix_count << '\n'
              << "  estimated_suffix_bytes: " << search_metrics.estimated_suffix_bytes << '\n'
              << "  fuzzy_vocabulary_tokens: " << search_metrics.fuzzy_vocabulary_tokens << '\n'
              << "  longest_posting_token: " << search_metrics.longest_posting_token << '\n'
              << "  longest_posting_list: " << search_metrics.longest_posting_list << '\n'
              << "  build_seconds: " << search_metrics.build_seconds << '\n';

    if (!search_metrics.largest_token_postings.empty()) {
        std::cout << "  largest_token_postings:\n";
        for (const auto& [token, count] : search_metrics.largest_token_postings) {
            std::cout << "    " << token << ": " << count << '\n';
        }
    }
    if (!search_metrics.skipped_poi_examples.empty()) {
        std::cout << "  skipped_poi_examples:\n";
        for (const auto& example : search_metrics.skipped_poi_examples) {
            std::cout << "    " << example << '\n';
        }
    }
    if (!search_metrics.indexed_region_examples.empty()) {
        std::cout << "  indexed_region_examples:\n";
        for (const auto& example : search_metrics.indexed_region_examples) {
            std::cout << "    " << example << '\n';
        }
    }
    if (!search_metrics.skipped_region_examples.empty()) {
        std::cout << "  skipped_region_examples:\n";
        for (const auto& example : search_metrics.skipped_region_examples) {
            std::cout << "    " << example << '\n';
        }
    }

    if (!options.test_geocode_query.empty()) {
        run_test_geocode_query(data, search_index_build.index, options.test_geocode_query);
        return 0;
    }

    if (!options.test_search_query.empty()) {
        run_test_search(data, search_index_build.index, options.test_search_query);
        return 0;
    }

    std::cout << "\nAvailable API routes (when --serve is enabled):\n"
              << "  /stats\n"
              << "  /houses?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /streets?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /regions?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /geocode?q=<address-or-place>\n"
              << "  /reverse?lat=<lat>&lon=<lon>\n";

    if (options.serve_http) {
        std::cout << "\nStarting local HTTP server on port " << options.port << "..." << '\n';
        return run_http_server(data, search_index_build.index, stats, options.port, options.max_requests);
    }

    std::cout << "\nRun with --serve to expose local bbox query endpoints." << '\n';
    return 0;
}

} // namespace osm

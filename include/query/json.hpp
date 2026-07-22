#pragma once

#include "metrics.hpp"
#include "model.hpp"
#include "search/geocode_query.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace osm {

[[nodiscard]] std::optional<BBox> parse_bbox_csv(std::string_view csv);

[[nodiscard]] std::string serialize_stats_json(const ParseStats& stats);
[[nodiscard]] std::string serialize_houses_json(
    const DataStore& data,
    const std::vector<std::size_t>& indices,
    std::size_t matched_count = 0,
    bool include_metadata = false);
[[nodiscard]] std::string serialize_streets_json(
    const DataStore& data,
    const std::vector<std::size_t>& indices,
    std::size_t matched_count = 0,
    bool include_metadata = false);
[[nodiscard]] std::string serialize_regions_json(
    const DataStore& data,
    const std::vector<std::size_t>& indices,
    std::size_t matched_count = 0,
    bool include_metadata = false);
[[nodiscard]] std::string serialize_geocode_json(
    const DataStore& data,
    const search::GeocodeQueryResult& result);
[[nodiscard]] std::string serialize_reverse_json(
    const DataStore& data,
    std::size_t house_index,
    const std::vector<std::size_t>& clicked_region_indices,
    std::optional<std::size_t> nearest_poi_index,
    double nearest_poi_distance_m,
    std::optional<std::size_t> nearest_street_index,
    double nearest_street_lat,
    double nearest_street_lon,
    double nearest_street_distance_m,
    double query_lat,
    double query_lon,
    double result_lat,
    double result_lon,
    double distance_m,
    std::string_view street,
    std::string_view house_number,
    std::string_view city,
    std::string_view state,
    std::string_view postcode,
    std::string_view country);
[[nodiscard]] std::string serialize_reverse_street_json(
    const DataStore& data,
    std::size_t street_index,
    const std::vector<std::size_t>& clicked_region_indices,
    std::optional<std::size_t> nearest_poi_index,
    double nearest_poi_distance_m,
    double query_lat,
    double query_lon,
    double result_lat,
    double result_lon,
    double distance_m);
[[nodiscard]] std::string serialize_reverse_region_json(
    const DataStore& data,
    std::size_t region_index,
    const std::vector<std::size_t>& clicked_region_indices,
    std::optional<std::size_t> nearest_poi_index,
    double nearest_poi_distance_m,
    double query_lat,
    double query_lon);
[[nodiscard]] std::string serialize_error_json(std::string_view message);

} // namespace osm

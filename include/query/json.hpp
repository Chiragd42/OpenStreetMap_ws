#pragma once

#include "metrics.hpp"
#include "model.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace osm {

[[nodiscard]] std::optional<BBox> parse_bbox_csv(std::string_view csv);

[[nodiscard]] std::string serialize_stats_json(const ParseStats& stats);
[[nodiscard]] std::string serialize_houses_json(const DataStore& data, const std::vector<std::size_t>& indices);
[[nodiscard]] std::string serialize_streets_json(const DataStore& data, const std::vector<std::size_t>& indices);
[[nodiscard]] std::string serialize_error_json(std::string_view message);

} // namespace osm

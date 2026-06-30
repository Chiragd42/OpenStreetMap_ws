#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace osm::search {

[[nodiscard]] std::string normalizeSearchText(std::string_view input);
[[nodiscard]] std::string normalizeHouseNumber(std::string_view input);
[[nodiscard]] std::vector<std::string> tokenizeNormalizedText(std::string_view normalized_text);

} // namespace osm::search

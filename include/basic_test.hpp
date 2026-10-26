#pragma once

#include "osm_model.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace sheet1 {

[[nodiscard]] std::vector<OsmId> linear_name_search(const DataStore& data,
                                                    std::string_view query);
void run_basic_test(const DataStore& data,
                    const std::filesystem::path& input_path,
                    const std::filesystem::path& output_path);

} // namespace sheet1

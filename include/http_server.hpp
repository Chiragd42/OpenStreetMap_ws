#pragma once

#include "osm_model.hpp"

#include <cstddef>
#include <cstdint>

namespace sheet1 {

inline constexpr std::size_t kViewportBuildingThreshold = 10'000;

int run_http_server(const DataStore& data,
                    std::uint16_t port,
                    std::size_t max_requests = 0);

} // namespace sheet1

#pragma once

#include "metrics.hpp"
#include "model.hpp"
#include "search/search_index.hpp"

#include <cstddef>
#include <cstdint>

namespace osm {

int run_http_server(
    const DataStore& data,
    const search::SearchIndex& search_index,
    const ParseStats& stats,
    std::uint16_t port,
    std::size_t max_requests = 0);

} // namespace osm

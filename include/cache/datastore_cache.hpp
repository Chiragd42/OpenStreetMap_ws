#pragma once

#include "metrics.hpp"
#include "model.hpp"

#include <string>

namespace osm {

bool save_datastore_cache(const std::string& path, const DataStore& data, const ParseStats& stats, std::string& error_message);
bool load_datastore_cache(const std::string& path, DataStore& data, ParseStats& stats, std::string& error_message);

} // namespace osm

#include "model.hpp"

#include <stdexcept>

namespace osm {

bool BBox::contains(double lon, double lat) const noexcept {
    return lon >= min_lon && lon <= max_lon && lat >= min_lat && lat <= max_lat;
}

StringId StringPool::intern(std::string_view value) {
    const auto existing = index_by_value_.find(std::string(value));
    if (existing != index_by_value_.end()) {
        return existing->second;
    }

    const auto id = static_cast<StringId>(values_.size());
    values_.emplace_back(value);
    index_by_value_.emplace(values_.back(), id);
    return id;
}

const std::string& StringPool::resolve(StringId id) const {
    if (id >= values_.size()) {
        throw std::out_of_range("Invalid StringId");
    }
    return values_[id];
}

std::size_t StringPool::size() const noexcept {
    return values_.size();
}

} // namespace osm

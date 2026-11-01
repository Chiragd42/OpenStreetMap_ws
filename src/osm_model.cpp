#include "osm_model.hpp"

#include <stdexcept>

namespace sheet1 {

StringId StringPool::intern(const std::string_view value) {
    if (value.empty()) {
        return kInvalidStringId;
    }

    const auto existing = ids_.find(std::string{value});
    if (existing != ids_.end()) {
        return existing->second;
    }

    if (values_.size() >= static_cast<std::size_t>(kInvalidStringId)) {
        throw std::runtime_error{"String pool exhausted its 32-bit IDs"};
    }

    const auto id = static_cast<StringId>(values_.size());
    values_.emplace_back(value);
    ids_.emplace(values_.back(), id);
    return id;
}

const std::string& StringPool::resolve(const StringId id) const {
    if (id == kInvalidStringId || static_cast<std::size_t>(id) >= values_.size()) {
        throw std::out_of_range{"Invalid string-pool ID"};
    }
    return values_[id];
}

std::size_t StringPool::size() const noexcept {
    return values_.size();
}

std::size_t StringPool::estimated_bytes() const noexcept {
    std::size_t bytes = values_.capacity() * sizeof(std::string);
    for (const auto& value : values_) {
        bytes += value.capacity() + 1;
    }
    return bytes;
}

std::size_t DataStore::estimated_bytes() const noexcept {
    return strings.estimated_bytes() +
           buildings.capacity() * sizeof(Building) +
           streets.capacity() * sizeof(Street) +
           administrative_areas.capacity() * sizeof(AdministrativeArea) +
           street_points.capacity() * sizeof(Coordinate) +
           administrative_rings.capacity() * sizeof(AdministrativeRing) +
           administrative_points.capacity() * sizeof(Coordinate);
}

std::string format_osm_id(const OsmId& id) {
    const char* type = "node";
    switch (id.type) {
    case OsmObjectType::node:
        type = "node";
        break;
    case OsmObjectType::way:
        type = "way";
        break;
    case OsmObjectType::relation:
        type = "relation";
        break;
    }
    return std::string{type} + '/' + std::to_string(id.value);
}

} // namespace sheet1

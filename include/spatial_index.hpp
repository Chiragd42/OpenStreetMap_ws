#pragma once
#include "osm_model.hpp"
#include <filesystem>
#include <memory>

namespace sheet1 {
struct NearestObject {
    enum class Kind { building, street, area } kind;
    std::size_t index;
    double distance_metres;
    Coordinate point;
};

// Immutable packed spatial trees; the referenced DataStore must outlive this object.
class SpatialIndex {
public:
    explicit SpatialIndex(DataStore& data);
    ~SpatialIndex();
    SpatialIndex(const SpatialIndex&) = delete;
    SpatialIndex& operator=(const SpatialIndex&) = delete;
    [[nodiscard]] std::vector<std::uint32_t> regions_at(Coordinate point) const;
    [[nodiscard]] bool covers(std::size_t area, Coordinate point) const;
    [[nodiscard]] std::vector<NearestObject> nearest_buildings(Coordinate point, std::size_t count = 10) const;
    [[nodiscard]] std::vector<NearestObject> reverse(Coordinate point) const;
    [[nodiscard]] std::vector<std::size_t> viewport(Coordinate minimum, Coordinate maximum, int kind, std::size_t limit) const;
    void assign_regions(DataStore& data) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
void run_reverse_file(const SpatialIndex& index, const DataStore& data,
                      const std::filesystem::path& input, const std::filesystem::path& output);
} // namespace sheet1

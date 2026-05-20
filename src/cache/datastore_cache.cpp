#include "cache/datastore_cache.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <vector>

namespace osm {
namespace {

constexpr std::uint32_t kCacheMagic = 0x4F534D43; // OSMC
constexpr std::uint32_t kCacheVersion = 2;

template <typename T>
bool write_pod(std::ofstream& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(out);
}

template <typename T>
bool read_pod(std::ifstream& in, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

template <typename T>
bool write_vector_pod(std::ofstream& out, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto size = static_cast<std::uint64_t>(values.size());
    if (!write_pod(out, size)) return false;
    if (size == 0) return true;
    out.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(sizeof(T) * values.size()));
    return static_cast<bool>(out);
}

template <typename T>
bool read_vector_pod(std::ifstream& in, std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::uint64_t size = 0;
    if (!read_pod(in, size)) return false;
    values.resize(static_cast<std::size_t>(size));
    if (size == 0) return true;
    in.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(sizeof(T) * values.size()));
    return static_cast<bool>(in);
}

bool write_string_pool(std::ofstream& out, const StringPool& pool) {
    const auto& values = pool.values();
    const auto size = static_cast<std::uint64_t>(values.size());
    if (!write_pod(out, size)) return false;
    for (const auto& s : values) {
        const auto len = static_cast<std::uint64_t>(s.size());
        if (!write_pod(out, len)) return false;
        out.write(s.data(), static_cast<std::streamsize>(len));
        if (!out) return false;
    }
    return true;
}

bool read_string_pool(std::ifstream& in, StringPool& pool) {
    std::uint64_t size = 0;
    if (!read_pod(in, size)) return false;
    std::vector<std::string> values;
    values.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t i = 0; i < size; ++i) {
        std::uint64_t len = 0;
        if (!read_pod(in, len)) return false;
        std::string s(static_cast<std::size_t>(len), '\0');
        in.read(s.data(), static_cast<std::streamsize>(len));
        if (!in) return false;
        values.push_back(std::move(s));
    }
    pool.reset_from_values(std::move(values));
    return true;
}

bool write_grid_map(std::ofstream& out,
                    const std::unordered_map<GridCellKey, std::vector<std::size_t>, GridCellKeyHash>& grid_map) {
    const auto entries = static_cast<std::uint64_t>(grid_map.size());
    if (!write_pod(out, entries)) return false;
    for (const auto& [key, indices] : grid_map) {
        if (!write_pod(out, key)) return false;
        std::vector<std::uint64_t> casted;
        casted.reserve(indices.size());
        for (const auto idx : indices) casted.push_back(static_cast<std::uint64_t>(idx));
        if (!write_vector_pod(out, casted)) return false;
    }
    return true;
}

bool read_grid_map(std::ifstream& in,
                   std::unordered_map<GridCellKey, std::vector<std::size_t>, GridCellKeyHash>& grid_map) {
    std::uint64_t entries = 0;
    if (!read_pod(in, entries)) return false;
    grid_map.clear();
    grid_map.reserve(static_cast<std::size_t>(entries));
    for (std::uint64_t i = 0; i < entries; ++i) {
        GridCellKey key{};
        if (!read_pod(in, key)) return false;
        std::vector<std::uint64_t> casted;
        if (!read_vector_pod(in, casted)) return false;
        std::vector<std::size_t> indices;
        indices.reserve(casted.size());
        for (const auto v : casted) indices.push_back(static_cast<std::size_t>(v));
        grid_map.emplace(key, std::move(indices));
    }
    return true;
}

} // namespace

bool save_datastore_cache(const std::string& path,
                          const DataStore& data,
                          const ParseStats& stats,
                          std::string& error_message) {
    error_message.clear();
    std::error_code ec;
    const std::filesystem::path out_path(path);
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path(), ec);
        if (ec) {
            error_message = "Failed to create cache directory: " + ec.message();
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error_message = "Failed to open cache file for writing: " + path;
        return false;
    }

    if (!write_pod(out, kCacheMagic) ||
        !write_pod(out, kCacheVersion) ||
        !write_pod(out, stats) ||
        !write_string_pool(out, data.strings) ||
        !write_vector_pod(out, data.houses) ||
        !write_vector_pod(out, data.streets) ||
        !write_vector_pod(out, data.street_points) ||
        !write_vector_pod(out, data.regions) ||
        !write_vector_pod(out, data.region_points) ||
        !write_vector_pod(out, data.house_containing_region_ids) ||
        !write_pod(out, data.grid.cell_size_deg) ||
        !write_grid_map(out, data.grid.house_cells) ||
        !write_grid_map(out, data.grid.street_cells) ||
        !write_grid_map(out, data.grid.region_cells)) {
        error_message = "Failed while writing cache file: " + path;
        return false;
    }

    return true;
}

bool load_datastore_cache(const std::string& path,
                          DataStore& data,
                          ParseStats& stats,
                          std::string& error_message) {
    error_message.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error_message = "Failed to open cache file for reading: " + path;
        return false;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!read_pod(in, magic) || !read_pod(in, version)) {
        error_message = "Failed to read cache header: " + path;
        return false;
    }
    if (magic != kCacheMagic) {
        error_message = "Invalid cache magic: " + path;
        return false;
    }
    if (version != kCacheVersion) {
        error_message = "Unsupported cache version: " + std::to_string(version);
        return false;
    }

    DataStore loaded_data;
    ParseStats loaded_stats;
    if (!read_pod(in, loaded_stats) ||
        !read_string_pool(in, loaded_data.strings) ||
        !read_vector_pod(in, loaded_data.houses) ||
        !read_vector_pod(in, loaded_data.streets) ||
        !read_vector_pod(in, loaded_data.street_points) ||
        !read_vector_pod(in, loaded_data.regions) ||
        !read_vector_pod(in, loaded_data.region_points) ||
        !read_vector_pod(in, loaded_data.house_containing_region_ids) ||
        !read_pod(in, loaded_data.grid.cell_size_deg) ||
        !read_grid_map(in, loaded_data.grid.house_cells) ||
        !read_grid_map(in, loaded_data.grid.street_cells) ||
        !read_grid_map(in, loaded_data.grid.region_cells)) {
        error_message = "Failed while reading cache data: " + path;
        return false;
    }

    data = std::move(loaded_data);
    stats = loaded_stats;
    return true;
}

} // namespace osm

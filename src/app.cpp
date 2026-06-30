#include "app.hpp"

#include "cache/datastore_cache.hpp"
#include "ingest/pbf_extractor.hpp"
#include "server/http_server.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace osm {

namespace {

[[nodiscard]] const char* poi_category_name(const PoiCategory category) {
    switch (category) {
        case PoiCategory::Shop: return "Shops";
        case PoiCategory::Restaurant: return "Restaurants";
        case PoiCategory::Cafe: return "Cafes";
        case PoiCategory::FastFood: return "Fast food";
        case PoiCategory::Park: return "Parks";
        case PoiCategory::Hotel: return "Hotels";
        case PoiCategory::School: return "Schools";
        case PoiCategory::Hospital: return "Hospitals";
        case PoiCategory::Station: return "Stations";
        case PoiCategory::Other: return "Other";
    }
    return "Other";
}

struct EndpointKey {
    std::int32_t lat_q{0};
    std::int32_t lon_q{0};

    [[nodiscard]] bool operator==(const EndpointKey& other) const noexcept {
        return lat_q == other.lat_q && lon_q == other.lon_q;
    }
};

struct EndpointKeyHash {
    [[nodiscard]] std::size_t operator()(const EndpointKey& key) const noexcept {
        const auto h1 = static_cast<std::size_t>(static_cast<std::uint32_t>(key.lat_q));
        const auto h2 = static_cast<std::size_t>(static_cast<std::uint32_t>(key.lon_q));
        return (h1 * 73856093U) ^ (h2 * 19349663U);
    }
};

struct StreetMergeStats {
    std::size_t raw_streets{0};
    std::size_t merged_streets{0};
    double merge_time_ms{0.0};
};

[[nodiscard]] EndpointKey point_to_key(const GeoPoint& p) {
    constexpr double scale = 1'000'000.0;
    return EndpointKey{
        .lat_q = static_cast<std::int32_t>(p.lat * scale),
        .lon_q = static_cast<std::int32_t>(p.lon * scale),
    };
}

[[nodiscard]] BBox bbox_from_points(const std::vector<GeoPoint>& points) {
    BBox bbox;
    if (points.empty()) {
        return bbox;
    }

    bbox.min_lon = points.front().lon;
    bbox.max_lon = points.front().lon;
    bbox.min_lat = points.front().lat;
    bbox.max_lat = points.front().lat;
    for (const auto& p : points) {
        bbox.min_lon = std::min(bbox.min_lon, static_cast<double>(p.lon));
        bbox.max_lon = std::max(bbox.max_lon, static_cast<double>(p.lon));
        bbox.min_lat = std::min(bbox.min_lat, static_cast<double>(p.lat));
        bbox.max_lat = std::max(bbox.max_lat, static_cast<double>(p.lat));
    }
    return bbox;
}

void add_bbox_to_grid(
    std::unordered_map<GridCellKey, std::vector<std::size_t>, GridCellKeyHash>& cells,
    const BBox& bbox,
    const float cell_size_deg,
    const std::size_t idx) {
    for (const auto& key : grid_cells_for_bbox(bbox, cell_size_deg)) {
        cells[key].push_back(idx);
    }
}

struct Dsu {
    std::vector<std::size_t> parent;
    std::vector<std::uint8_t> rank;

    explicit Dsu(const std::size_t n) : parent(n), rank(n, 0) {
        for (std::size_t i = 0; i < n; ++i) {
            parent[i] = i;
        }
    }

    std::size_t find(std::size_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }

    void unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a == b) {
            return;
        }
        if (rank[a] < rank[b]) {
            std::swap(a, b);
        }
        parent[b] = a;
        if (rank[a] == rank[b]) {
            ++rank[a];
        }
    }
};

StreetMergeStats merge_streets_in_place(DataStore& data) {
    StreetMergeStats out;
    out.raw_streets = data.streets.size();
    const auto t0 = std::chrono::steady_clock::now();

    if (data.streets.empty()) {
        out.merged_streets = 0;
        return out;
    }

    std::unordered_map<std::uint64_t, std::vector<std::size_t>> groups;
    groups.reserve(data.streets.size());
    for (std::size_t i = 0; i < data.streets.size(); ++i) {
        const auto& s = data.streets[i];
        if (s.points_count < 2) {
            continue;
        }
        const std::uint64_t key = (static_cast<std::uint64_t>(s.name_id) << 32U) |
                                  static_cast<std::uint64_t>(s.highway_class_id);
        groups[key].push_back(i);
    }

    std::vector<StreetPolyline> merged_streets;
    std::vector<GeoPoint> merged_points;
    merged_streets.reserve(data.streets.size());
    merged_points.reserve(data.street_points.size());

    for (auto& [_, indices] : groups) {
        Dsu dsu(indices.size());
        std::unordered_map<EndpointKey, std::size_t, EndpointKeyHash> endpoint_owner;

        for (std::size_t local_idx = 0; local_idx < indices.size(); ++local_idx) {
            const auto& s = data.streets[indices[local_idx]];
            const auto begin = static_cast<std::size_t>(s.points_begin);
            const auto end = begin + static_cast<std::size_t>(s.points_count) - 1;
            const auto k0 = point_to_key(data.street_points[begin]);
            const auto k1 = point_to_key(data.street_points[end]);
            for (const auto key : std::array<EndpointKey, 2>{k0, k1}) {
                const auto it = endpoint_owner.find(key);
                if (it == endpoint_owner.end()) {
                    endpoint_owner.emplace(key, local_idx);
                } else {
                    dsu.unite(local_idx, it->second);
                }
            }
        }

        std::unordered_map<std::size_t, std::vector<std::size_t>> components;
        for (std::size_t local_idx = 0; local_idx < indices.size(); ++local_idx) {
            components[dsu.find(local_idx)].push_back(local_idx);
        }

        for (auto& [__, comp] : components) {
            std::unordered_map<EndpointKey, std::vector<std::size_t>, EndpointKeyHash> adj;
            adj.reserve(comp.size() * 2);

            std::vector<EndpointKey> seg_start(comp.size());
            std::vector<EndpointKey> seg_end(comp.size());
            std::vector<bool> used(comp.size(), false);

            for (std::size_t ci = 0; ci < comp.size(); ++ci) {
                const auto& s = data.streets[indices[comp[ci]]];
                const auto begin = static_cast<std::size_t>(s.points_begin);
                const auto end = begin + static_cast<std::size_t>(s.points_count) - 1;
                seg_start[ci] = point_to_key(data.street_points[begin]);
                seg_end[ci] = point_to_key(data.street_points[end]);
                adj[seg_start[ci]].push_back(ci);
                adj[seg_end[ci]].push_back(ci);
            }

            auto emit_trail = [&](const EndpointKey start_key) {
                std::vector<GeoPoint> poly;
                EndpointKey current = start_key;
                bool has_segment = false;

                while (true) {
                    auto it = adj.find(current);
                    if (it == adj.end()) {
                        break;
                    }
                    std::size_t chosen = comp.size();
                    for (const auto idx : it->second) {
                        if (!used[idx]) {
                            chosen = idx;
                            break;
                        }
                    }
                    if (chosen == comp.size()) {
                        break;
                    }

                    used[chosen] = true;
                    has_segment = true;

                    const auto global_street_idx = indices[comp[chosen]];
                    const auto& s = data.streets[global_street_idx];
                    const auto begin = static_cast<std::size_t>(s.points_begin);
                    const auto count = static_cast<std::size_t>(s.points_count);
                    const bool forward = (current == seg_start[chosen]);

                    if (forward) {
                        for (std::size_t i = 0; i < count; ++i) {
                            if (!poly.empty() && i == 0) {
                                continue;
                            }
                            poly.push_back(data.street_points[begin + i]);
                        }
                        current = seg_end[chosen];
                    } else {
                        for (std::size_t i = 0; i < count; ++i) {
                            const auto rev_i = count - 1 - i;
                            if (!poly.empty() && i == 0) {
                                continue;
                            }
                            poly.push_back(data.street_points[begin + rev_i]);
                        }
                        current = seg_start[chosen];
                    }
                }

                if (has_segment && poly.size() >= 2) {
                    StreetPolyline out_street;
                    out_street.name_id = data.streets[indices[comp.front()]].name_id;
                    out_street.highway_class_id = data.streets[indices[comp.front()]].highway_class_id;
                    out_street.is_unnamed = data.streets[indices[comp.front()]].is_unnamed;
                    out_street.points_begin = static_cast<std::uint32_t>(merged_points.size());
                    out_street.points_count = static_cast<std::uint32_t>(poly.size());
                    out_street.bbox = bbox_from_points(poly);
                    merged_points.insert(merged_points.end(), poly.begin(), poly.end());
                    merged_streets.push_back(out_street);
                }
            };

            for (const auto& [node, edges] : adj) {
                std::size_t degree = 0;
                for (const auto e : edges) {
                    if (!used[e]) {
                        ++degree;
                    }
                }
                if (degree % 2 == 1) {
                    emit_trail(node);
                }
            }

            for (std::size_t ci = 0; ci < comp.size(); ++ci) {
                if (!used[ci]) {
                    emit_trail(seg_start[ci]);
                }
            }
        }
    }

    data.streets = std::move(merged_streets);
    data.street_points = std::move(merged_points);

    data.grid.street_cells.clear();
    for (std::size_t i = 0; i < data.streets.size(); ++i) {
        add_bbox_to_grid(data.grid.street_cells, data.streets[i].bbox, data.grid.cell_size_deg, i);
    }

    out.merged_streets = data.streets.size();
    const auto t1 = std::chrono::steady_clock::now();
    out.merge_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return out;
}

} // namespace

int App::run(const AppOptions& options) const {
    const auto app_start = std::chrono::steady_clock::now();
    DataStore data;
    ParseStats stats;
    std::string source_description;
    bool loaded_from_cache = false;

    if (!options.load_cache_path.empty()) {
        const auto t0 = std::chrono::steady_clock::now();
        std::string cache_error;
        if (!load_datastore_cache(options.load_cache_path, data, stats, cache_error)) {
            std::cerr << "Failed to load cache: " << cache_error << '\n';
            return 1;
        }
        const auto t1 = std::chrono::steady_clock::now();
        stats.parse_seconds = std::chrono::duration<double>(t1 - t0).count();
        loaded_from_cache = true;
        source_description = "cache: " + options.load_cache_path;
    } else {
        PbfExtractor extractor;
        ExtractionConfig config;
        if (!options.pbf_path.empty()) {
            config.input_pbf_path = options.pbf_path;
        }

        auto extracted = extractor.extract(config);
        data = std::move(extracted.data);
        stats = extracted.stats;
        source_description = "PBF: " + config.input_pbf_path;

        if (!options.save_cache_path.empty()) {
            std::string cache_error;
            if (!save_datastore_cache(options.save_cache_path, data, stats, cache_error)) {
                std::cerr << "Failed to save cache: " << cache_error << '\n';
                return 1;
            }
            std::cout << "Saved cache to: " << options.save_cache_path << '\n';
        }
    }

    StreetMergeStats merge_stats;
    merge_stats.raw_streets = data.streets.size();
    merge_stats.merged_streets = data.streets.size();
    if (options.merge_streets) {
        merge_stats = merge_streets_in_place(data);
    }

    std::cout << "OSM geocoder Sheet-1 pipeline initialized (PBF-first, target: Baden-Wuerttemberg)." << '\n';
    std::cout << "Input source: " << source_description << '\n';
    std::cout << "Processed nodes: " << stats.processed_nodes << '\n';
    std::cout << "Processed ways: " << stats.processed_ways << '\n';
    std::cout << "Processed relations: " << stats.processed_relations << '\n';
    std::cout << "Extracted houses: " << stats.extracted_houses << '\n';
    std::cout << "Extracted streets: " << stats.extracted_streets << '\n';
    std::cout << "Extracted regions: " << stats.extracted_regions << '\n';
    std::cout << "\n[POI extraction]\n"
              << "  POI nodes: " << stats.extracted_poi_nodes << '\n'
              << "  POI ways: " << stats.extracted_poi_ways << '\n'
              << "  POIs total: " << stats.extracted_pois_total << '\n';
    for (std::size_t i = 0; i < kPoiCategoryCount; ++i) {
        std::cout << "  " << poi_category_name(static_cast<PoiCategory>(i)) << ": "
                  << stats.extracted_pois_by_category[i] << '\n';
    }
    std::cout << "  Skipped unnamed POIs: " << stats.skipped_unnamed_pois << '\n'
              << "  Invalid POI geometries: " << stats.skipped_invalid_poi_geometry << '\n'
              << "  POIs assigned to region: " << stats.pois_assigned_to_region << '\n'
              << "  POIs without region: " << stats.pois_without_region << '\n'
              << "  POI-region assignment seconds: " << stats.poi_region_assignment_seconds << '\n';
    std::cout << "Houses from address nodes: " << stats.houses_from_address_nodes << '\n';
    std::cout << "Houses from polygon centroid: " << stats.houses_from_polygon_centroid << '\n';
    std::cout << "Houses from bbox fallback: " << stats.houses_from_polygon_bbox_fallback << '\n';
    std::cout << "Skipped invalid house geometries: " << stats.houses_skipped_invalid_geometry << '\n';
    std::cout << "Unnamed streets: " << stats.unnamed_streets << '\n';
    std::cout << "Skipped complex region relations: " << stats.regions_skipped_complex_relations << '\n';
    if (loaded_from_cache) {
        const auto startup_seconds = std::chrono::duration<double>(
                                         std::chrono::steady_clock::now() - app_start)
                                         .count();
        std::cout << "Load seconds: " << startup_seconds << '\n';
    } else {
        std::cout << "Parse seconds: " << stats.parse_seconds << '\n';
    }
    std::cout << "Estimated memory bytes: " << stats.estimated_memory_bytes << '\n';

    const auto reduced =
        (merge_stats.raw_streets > merge_stats.merged_streets)
            ? (merge_stats.raw_streets - merge_stats.merged_streets)
            : 0;
    const double reduced_pct =
        merge_stats.raw_streets == 0
            ? 0.0
            : (static_cast<double>(reduced) * 100.0 / static_cast<double>(merge_stats.raw_streets));

    std::cout << "\n[StreetMerge]\n"
              << "  enabled: " << (options.merge_streets ? "yes" : "no") << '\n'
              << "  raw_streets: " << merge_stats.raw_streets << '\n'
              << "  merged_streets: " << merge_stats.merged_streets << '\n'
              << "  reduced: " << reduced << " (" << reduced_pct << "%)\n"
              << "  merge_time_ms: " << merge_stats.merge_time_ms << '\n';

    std::cout << "\nAvailable API routes (when --serve is enabled):\n"
              << "  /stats\n"
              << "  /houses?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /streets?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /regions?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /reverse?lat=<lat>&lon=<lon>\n";

    if (options.serve_http) {
        std::cout << "\nStarting local HTTP server on port " << options.port << "..." << '\n';
        return run_http_server(data, stats, options.port, options.max_requests);
    }

    std::cout << "\nRun with --serve to expose local bbox query endpoints." << '\n';
    return 0;
}

} // namespace osm

#include "app.hpp"

#include "cache/datastore_cache.hpp"
#include "ingest/pbf_extractor.hpp"
#include "server/http_server.hpp"

#include <iostream>

namespace osm {

int App::run(const AppOptions& options) const {
    DataStore data;
    ParseStats stats;
    std::string source_description;

    if (!options.load_cache_path.empty()) {
        std::string cache_error;
        if (!load_datastore_cache(options.load_cache_path, data, stats, cache_error)) {
            std::cerr << "Failed to load cache: " << cache_error << '\n';
            return 1;
        }
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

    std::cout << "OSM geocoder Sheet-1 pipeline initialized (PBF-first, target: Stuttgart)." << '\n';
    std::cout << "Input source: " << source_description << '\n';
    std::cout << "Processed nodes: " << stats.processed_nodes << '\n';
    std::cout << "Processed ways: " << stats.processed_ways << '\n';
    std::cout << "Processed relations: " << stats.processed_relations << '\n';
    std::cout << "Extracted houses: " << stats.extracted_houses << '\n';
    std::cout << "Extracted streets: " << stats.extracted_streets << '\n';
    std::cout << "Extracted regions: " << stats.extracted_regions << '\n';
    std::cout << "Houses from address nodes: " << stats.houses_from_address_nodes << '\n';
    std::cout << "Houses from polygon centroid: " << stats.houses_from_polygon_centroid << '\n';
    std::cout << "Houses from bbox fallback: " << stats.houses_from_polygon_bbox_fallback << '\n';
    std::cout << "Skipped invalid house geometries: " << stats.houses_skipped_invalid_geometry << '\n';
    std::cout << "Unnamed streets: " << stats.unnamed_streets << '\n';
    std::cout << "Skipped complex region relations: " << stats.regions_skipped_complex_relations << '\n';
    std::cout << "Parse seconds: " << stats.parse_seconds << '\n';
    std::cout << "Estimated memory bytes: " << stats.estimated_memory_bytes << '\n';

    std::cout << "\nAvailable API routes (when --serve is enabled):\n"
              << "  /stats\n"
              << "  /houses?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /streets?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /regions?bbox=minLon,minLat,maxLon,maxLat\n";

    if (options.serve_http) {
        std::cout << "\nStarting local HTTP server on port " << options.port << "..." << '\n';
        return run_http_server(data, stats, options.port, options.max_requests);
    }

    std::cout << "\nRun with --serve to expose local bbox query endpoints." << '\n';
    return 0;
}

} // namespace osm

#include "app.hpp"

#include "ingest/pbf_extractor.hpp"
#include "server/http_server.hpp"

#include <iostream>

namespace osm {

int App::run(const AppOptions& options) const {
    PbfExtractor extractor;
    ExtractionConfig config;
    if (!options.pbf_path.empty()) {
        config.input_pbf_path = options.pbf_path;
    }

    auto extracted = extractor.extract(config);

    std::cout << "OSM geocoder Sheet-1 pipeline initialized (PBF-first, target: Stuttgart)." << '\n';
    std::cout << "Input PBF path: " << config.input_pbf_path << '\n';
    std::cout << "Processed nodes: " << extracted.stats.processed_nodes << '\n';
    std::cout << "Processed ways: " << extracted.stats.processed_ways << '\n';
    std::cout << "Processed relations: " << extracted.stats.processed_relations << '\n';
    std::cout << "Extracted houses: " << extracted.stats.extracted_houses << '\n';
    std::cout << "Extracted streets: " << extracted.stats.extracted_streets << '\n';
    std::cout << "Extracted regions: " << extracted.stats.extracted_regions << '\n';
    std::cout << "Houses from address nodes: " << extracted.stats.houses_from_address_nodes << '\n';
    std::cout << "Houses from polygon centroid: " << extracted.stats.houses_from_polygon_centroid << '\n';
    std::cout << "Houses from bbox fallback: " << extracted.stats.houses_from_polygon_bbox_fallback << '\n';
    std::cout << "Skipped invalid house geometries: " << extracted.stats.houses_skipped_invalid_geometry << '\n';
    std::cout << "Unnamed streets: " << extracted.stats.unnamed_streets << '\n';
    std::cout << "Skipped complex region relations: " << extracted.stats.regions_skipped_complex_relations << '\n';
    std::cout << "Parse seconds: " << extracted.stats.parse_seconds << '\n';
    std::cout << "Estimated memory bytes: " << extracted.stats.estimated_memory_bytes << '\n';

    std::cout << "\nAvailable API routes (when --serve is enabled):\n"
              << "  /stats\n"
              << "  /houses?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /streets?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /regions?bbox=minLon,minLat,maxLon,maxLat\n";

    if (options.serve_http) {
        std::cout << "\nStarting local HTTP server on port " << options.port << "..." << '\n';
        return run_http_server(extracted.data, extracted.stats, options.port, options.max_requests);
    }

    std::cout << "\nRun with --serve to expose local bbox query endpoints." << '\n';
    return 0;
}

} // namespace osm

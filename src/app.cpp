#include "app.hpp"

#include "ingest/gpkg_extractor.hpp"
#include "server/http_server.hpp"

#include <iostream>

namespace osm {

int App::run(const AppOptions& options) const {
    GpkgExtractor extractor;
    ExtractionConfig config;

    auto extracted = extractor.extract(config);

    std::cout << "OSM geocoder scaffold initialized (target dataset: Stuttgart)." << '\n';
    std::cout << "Input GPKG path: " << config.input_gpkg_path << '\n';
    std::cout << "Processed nodes: " << extracted.stats.processed_nodes << '\n';
    std::cout << "Processed ways: " << extracted.stats.processed_ways << '\n';
    std::cout << "Processed relations: " << extracted.stats.processed_relations << '\n';
    std::cout << "Extracted houses: " << extracted.stats.extracted_houses << '\n';
    std::cout << "Extracted streets: " << extracted.stats.extracted_streets << '\n';
    std::cout << "Extracted admin areas: " << extracted.stats.extracted_admin_areas << '\n';
    std::cout << "Parse seconds: " << extracted.stats.parse_seconds << '\n';
    std::cout << "Estimated memory bytes: " << extracted.stats.estimated_memory_bytes << '\n';

    std::cout << "\nAvailable API routes (when --serve is enabled):\n"
              << "  /stats\n"
              << "  /houses?bbox=minLon,minLat,maxLon,maxLat\n"
              << "  /streets?bbox=minLon,minLat,maxLon,maxLat\n";

    if (options.serve_http) {
        std::cout << "\nStarting local HTTP server on port " << options.port << "..." << '\n';
        return run_http_server(extracted.data, extracted.stats, options.port, options.max_requests);
    }

    std::cout << "\nRun with --serve to expose local bbox query endpoints." << '\n';
    return 0;
}

} // namespace osm

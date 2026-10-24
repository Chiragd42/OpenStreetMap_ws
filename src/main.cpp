#include "osm_inspector.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    const std::filesystem::path input_path =
        argc >= 2 ? argv[1] : "data/pbf/germany-latest.osm.pbf";

    if (argc > 2) {
        std::cerr << "Usage: osm_inspect [path/to/input.osm.pbf]\n";
        return 2;
    }

    try {
        const auto stats = sheet1::inspect_pbf(input_path);
        sheet1::print_report(input_path, stats);
    } catch (const std::exception& error) {
        std::cerr << "Inspection failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}

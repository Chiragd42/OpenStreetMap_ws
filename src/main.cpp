#include "app.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cout << "Usage: osm_geocoder [--serve] [--port=<port>] [--max-requests=<n>] [--pbf=<path>] [--save-cache=<path>] [--load-cache=<path>] [--test-search=<query>] [--merge-streets] [--no-merge-streets]\n";
}

bool parse_u16_arg(std::string_view raw, std::uint16_t& out) {
    try {
        std::size_t parsed_chars = 0;
        const auto value = std::stoull(std::string(raw), &parsed_chars, 10);
        if (parsed_chars != raw.size() || value > std::numeric_limits<std::uint16_t>::max()) {
            return false;
        }
        out = static_cast<std::uint16_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_size_t_arg(std::string_view raw, std::size_t& out) {
    try {
        std::size_t parsed_chars = 0;
        const auto value = std::stoull(std::string(raw), &parsed_chars, 10);
        if (parsed_chars != raw.size() || value > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        out = static_cast<std::size_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

int main(int argc, char** argv) {
    osm::AppOptions options;

    // Minimal CLI parsing for local development.
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};

        if (arg == "--serve") {
            options.serve_http = true;
            continue;
        }

        if (arg == "--merge-streets") {
            options.merge_streets = true;
            continue;
        }

        if (arg == "--no-merge-streets") {
            options.merge_streets = false;
            continue;
        }

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }

        if (arg.rfind("--port=", 0) == 0) {
            std::uint16_t parsed_port = 0;
            if (!parse_u16_arg(arg.substr(7), parsed_port)) {
                std::cerr << "Invalid --port value: " << arg.substr(7) << '\n';
                print_usage();
                return 1;
            }
            options.port = parsed_port;
            continue;
        }

        if (arg.rfind("--max-requests=", 0) == 0) {
            std::size_t parsed_max_requests = 0;
            if (!parse_size_t_arg(arg.substr(15), parsed_max_requests)) {
                std::cerr << "Invalid --max-requests value: " << arg.substr(15) << '\n';
                print_usage();
                return 1;
            }
            options.max_requests = parsed_max_requests;
            continue;
        }

        if (arg.rfind("--pbf=", 0) == 0) {
            options.pbf_path = std::string(arg.substr(6));
            continue;
        }

        if (arg.rfind("--save-cache=", 0) == 0) {
            options.save_cache_path = std::string(arg.substr(13));
            continue;
        }

        if (arg.rfind("--load-cache=", 0) == 0) {
            options.load_cache_path = std::string(arg.substr(13));
            continue;
        }

        if (arg == "--load-cache" && i + 1 < argc) {
            options.load_cache_path = argv[++i];
            continue;
        }

        if (arg.rfind("--test-search=", 0) == 0) {
            options.test_search_query = std::string(arg.substr(14));
            continue;
        }

        if (arg == "--test-search" && i + 1 < argc) {
            options.test_search_query = argv[++i];
            continue;
        }

        std::cerr << "Unknown argument: " << arg << '\n';
        print_usage();
        return 1;
    }

    const osm::App app;
    return app.run(options);
}

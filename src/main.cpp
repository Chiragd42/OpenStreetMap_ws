#include "app.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cout << "Usage: osm_geocoder [--serve] [--port=<port>] [--max-requests=<n>]\n";
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

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }

        if (arg.rfind("--port=", 0) == 0) {
            const auto value = std::string(arg.substr(7));
            options.port = static_cast<std::uint16_t>(std::stoul(value));
            continue;
        }

        if (arg.rfind("--max-requests=", 0) == 0) {
            const auto value = std::string(arg.substr(15));
            options.max_requests = static_cast<std::size_t>(std::stoull(value));
            continue;
        }

        std::cerr << "Unknown argument: " << arg << '\n';
        print_usage();
        return 1;
    }

    const osm::App app;
    return app.run(options);
}

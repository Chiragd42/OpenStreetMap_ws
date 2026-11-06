#include "basic_test.hpp"
#include "http_server.hpp"
#include "osm_extractor.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path pbf{"data/pbf/germany-latest.osm.pbf"};
    std::filesystem::path basic_test_input;
    std::filesystem::path basic_test_output;
    bool run_basic_test{false};
    bool serve{false};
    std::uint16_t port{8080};
    std::size_t max_requests{0};
};

void print_usage() {
    std::cout << "Usage:\n"
              << "  osm_sheet1 [--pbf <file.osm.pbf>] --basicTest <input_file> <output_file>\n"
              << "  osm_sheet1 [--pbf <file.osm.pbf>] --serve [--port <port>]\n";
}

[[nodiscard]] std::uint64_t parse_unsigned(const std::string_view text,
                                           const std::uint64_t maximum,
                                           const char* option) {
    if (text.empty() || text.front() == '-') {
        throw std::runtime_error{std::string{"Invalid "} + option + ": " +
                                 std::string{text}};
    }
    try {
        std::size_t parsed = 0;
        const auto value = std::stoull(std::string{text}, &parsed, 10);
        if (parsed != text.size() || value > maximum) {
            throw std::runtime_error{"out of range"};
        }
        return value;
    } catch (...) {
        throw std::runtime_error{std::string{"Invalid "} + option + ": " +
                                 std::string{text}};
    }
}

[[nodiscard]] Options parse_options(const int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--help" || argument == "-h") {
            print_usage();
            std::exit(0);
        }
        if (argument == "--pbf") {
            if (i + 1 >= argc) {
                throw std::runtime_error{"--pbf requires a path"};
            }
            options.pbf = argv[++i];
            continue;
        }
        if (argument.starts_with("--pbf=")) {
            options.pbf = std::string{argument.substr(6)};
            continue;
        }
        if (argument == "--basicTest") {
            if (i + 2 >= argc) {
                throw std::runtime_error{"--basicTest requires input and output paths"};
            }
            options.basic_test_input = argv[++i];
            options.basic_test_output = argv[++i];
            options.run_basic_test = true;
            continue;
        }
        if (argument == "--serve") {
            options.serve = true;
            continue;
        }
        if (argument == "--port") {
            if (i + 1 >= argc) {
                throw std::runtime_error{"--port requires a number"};
            }
            options.port = static_cast<std::uint16_t>(parse_unsigned(
                argv[++i], std::numeric_limits<std::uint16_t>::max(), "--port"));
            if (options.port == 0) {
                throw std::runtime_error{"--port must be greater than zero"};
            }
            continue;
        }
        if (argument == "--max-requests") {
            if (i + 1 >= argc) {
                throw std::runtime_error{"--max-requests requires a number"};
            }
            options.max_requests = static_cast<std::size_t>(parse_unsigned(
                argv[++i],
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()),
                "--max-requests"));
            continue;
        }
        throw std::runtime_error{"Unknown argument: " + std::string{argument}};
    }
    return options;
}

} // namespace

int main(const int argc, char* argv[]) {
    try {
        const auto options = parse_options(argc, argv);
        auto result = sheet1::extract_pbf(options.pbf);
        sheet1::print_extraction_report(options.pbf, result);

        if (options.run_basic_test) {
            sheet1::run_basic_test(result.data,
                                   options.basic_test_input,
                                   options.basic_test_output);
            std::cout << "Basic-test results written to: " << options.basic_test_output << '\n';
        }
        if (options.serve) {
            return sheet1::run_http_server(result.data, options.port, options.max_requests);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        print_usage();
        return 1;
    }
}

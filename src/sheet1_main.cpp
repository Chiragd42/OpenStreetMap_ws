#include "basic_test.hpp"
#include "osm_extractor.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path pbf{"data/pbf/germany-latest.osm.pbf"};
    std::filesystem::path basic_test_input;
    std::filesystem::path basic_test_output;
    bool run_basic_test{false};
};

void print_usage() {
    std::cout << "Usage:\n"
              << "  osm_sheet1 [--pbf <file.osm.pbf>] --basicTest <input_file> <output_file>\n"
              << "  osm_sheet1 [--pbf <file.osm.pbf>]\n";
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        print_usage();
        return 1;
    }
}

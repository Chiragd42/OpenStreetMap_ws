#include "basic_test.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace sheet1 {
namespace {

[[nodiscard]] unsigned char ascii_lower(const unsigned char character) {
    if (character >= static_cast<unsigned char>('A') &&
        character <= static_cast<unsigned char>('Z')) {
        return static_cast<unsigned char>(character + ('a' - 'A'));
    }
    return character;
}

[[nodiscard]] bool contains_ascii_case_insensitive(const std::string_view text,
                                                   const std::string_view query) {
    if (query.empty() || query.size() > text.size()) {
        return false;
    }

    return std::search(
               text.begin(),
               text.end(),
               query.begin(),
               query.end(),
               [](const char left, const char right) {
                   return ascii_lower(static_cast<unsigned char>(left)) ==
                          ascii_lower(static_cast<unsigned char>(right));
               }) != text.end();
}

[[nodiscard]] bool named_object_matches(const DataStore& data,
                                        const StringId name,
                                        const std::string_view query) {
    return name != kInvalidStringId &&
           contains_ascii_case_insensitive(data.strings.resolve(name), query);
}

[[nodiscard]] int type_rank(const OsmObjectType type) {
    switch (type) {
    case OsmObjectType::node:
        return 0;
    case OsmObjectType::way:
        return 1;
    case OsmObjectType::relation:
        return 2;
    }
    return 3;
}

} // namespace

std::vector<OsmId> linear_name_search(const DataStore& data, const std::string_view query) {
    std::vector<OsmId> matches;
    if (query.empty()) {
        return matches;
    }

    for (const auto& building : data.buildings) {
        if (named_object_matches(data, building.name, query)) {
            matches.push_back(building.id);
        }
    }
    for (const auto& street : data.streets) {
        if (named_object_matches(data, street.name, query)) {
            matches.push_back(street.id);
        }
    }
    for (const auto& area : data.administrative_areas) {
        if (named_object_matches(data, area.name, query)) {
            matches.push_back(area.id);
        }
    }

    std::sort(matches.begin(), matches.end(), [](const OsmId& left, const OsmId& right) {
        const int left_rank = type_rank(left.type);
        const int right_rank = type_rank(right.type);
        return left_rank == right_rank ? left.value < right.value : left_rank < right_rank;
    });
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

void run_basic_test(const DataStore& data,
                    const std::filesystem::path& input_path,
                    const std::filesystem::path& output_path) {
    std::ifstream input{input_path};
    if (!input) {
        throw std::runtime_error{"Could not open basic-test input: " + input_path.string()};
    }

    std::ofstream output{output_path};
    if (!output) {
        throw std::runtime_error{"Could not open basic-test output: " + output_path.string()};
    }

    std::string query;
    while (std::getline(input, query)) {
        if (!query.empty() && query.back() == '\r') {
            query.pop_back();
        }

        const auto matches = linear_name_search(data, query);
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (i != 0) {
                output << ", ";
            }
            output << format_osm_id(matches[i]);
        }
        output << '\n';
    }

    if (!output) {
        throw std::runtime_error{"Failed while writing basic-test output: " + output_path.string()};
    }
}

} // namespace sheet1

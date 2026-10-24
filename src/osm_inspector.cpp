#include "osm_inspector.hpp"

#include <osmium/handler.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/visitor.hpp>

#include <sys/resource.h>

#include <chrono>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace sheet1 {
namespace {

[[nodiscard]] std::string_view tag(const osmium::TagList& tags, const char* key) {
    const char* value = tags.get_value_by_key(key);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

[[nodiscard]] std::string_view selected_name(const osmium::TagList& tags) {
    const auto german = tag(tags, "name:de");
    return german.empty() ? tag(tags, "name") : german;
}

[[nodiscard]] bool has_address(const osmium::TagList& tags) {
    return tags.has_key("addr:housenumber") || tags.has_key("addr:street") ||
           tags.has_key("addr:place") || tags.has_key("addr:postcode") ||
           tags.has_key("addr:city");
}

[[nodiscard]] std::string printable(std::string_view value) {
    return value.empty() ? "<missing>" : std::string{value};
}

void add_sample(std::vector<ObjectSample>& samples,
                std::size_t sample_limit,
                std::string typed_id_value,
                std::string description) {
    if (samples.size() < sample_limit) {
        samples.push_back(ObjectSample{
            .typed_id = std::move(typed_id_value),
            .description = std::move(description),
        });
    }
}

[[nodiscard]] std::string typed_id(const char* type, osmium::object_id_type id) {
    return std::string{type} + '/' + std::to_string(id);
}

[[nodiscard]] double peak_memory_mib() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
#if defined(__APPLE__)
    constexpr double bytes_per_mib = 1024.0 * 1024.0;
    return static_cast<double>(usage.ru_maxrss) / bytes_per_mib;
#else
    constexpr double kib_per_mib = 1024.0;
    return static_cast<double>(usage.ru_maxrss) / kib_per_mib;
#endif
}

[[nodiscard]] double process_cpu_seconds() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
    const auto seconds = static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec);
    const auto microseconds = static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
    return seconds + microseconds / 1'000'000.0;
}

class InspectionHandler final : public osmium::handler::Handler {
public:
    InspectionHandler(InspectionStats& stats, std::size_t sample_limit)
        : stats_(stats), sample_limit_(sample_limit) {}

    void node(const osmium::Node& node) {
        ++stats_.nodes;
        if (!node.location().valid()) {
            ++stats_.invalid_node_locations;
        }

        const auto& tags = node.tags();
        const bool address = has_address(tags);
        const bool building = tags.has_key("building");
        if (address) {
            ++stats_.address_nodes;
        }
        if (address && !building) {
            ++stats_.address_only_nodes;
        }
        if (building) {
            ++stats_.building_nodes;
            if (address) {
                ++stats_.building_nodes_with_address;
            }
            count_building_metadata(tags);
        }

        if ((address || building) && node.location().valid()) {
            std::ostringstream details;
            details << "lat=" << std::fixed << std::setprecision(7) << node.location().lat_without_check()
                    << ", lon=" << node.location().lon_without_check()
                    << ", building=" << printable(tag(tags, "building"))
                    << ", housenumber=" << printable(tag(tags, "addr:housenumber"));
            add_sample(stats_.node_samples, sample_limit_, typed_id("node", node.id()), details.str());
        }
    }

    void way(const osmium::Way& way) {
        ++stats_.ways;
        const auto& tags = way.tags();

        if (tags.has_key("building")) {
            ++stats_.building_ways;
            stats_.building_way_node_references += static_cast<std::uint64_t>(way.nodes().size());
            count_building_metadata(tags);

            std::ostringstream details;
            details << "name=" << printable(selected_name(tags))
                    << ", building=" << printable(tag(tags, "building"))
                    << ", address=" << (has_address(tags) ? "yes" : "no")
                    << ", node_refs=" << way.nodes().size();
            add_sample(stats_.building_samples, sample_limit_, typed_id("way", way.id()), details.str());
        }

        if (tags.has_key("highway")) {
            ++stats_.street_ways;
            ++stats_.highway_class_counts[std::string{tag(tags, "highway")}];
            stats_.street_node_references += static_cast<std::uint64_t>(way.nodes().size());
            const auto name = selected_name(tags);
            if (name.empty()) {
                ++stats_.unnamed_street_ways;
            } else {
                ++stats_.named_street_ways;
            }

            std::ostringstream details;
            details << "name=" << printable(name)
                    << ", highway=" << printable(tag(tags, "highway"))
                    << ", node_refs=" << way.nodes().size();
            add_sample(stats_.street_samples, sample_limit_, typed_id("way", way.id()), details.str());
        }

        if (tags.has_tag("boundary", "administrative")) {
            ++stats_.administrative_ways;
            stats_.administrative_way_node_references += static_cast<std::uint64_t>(way.nodes().size());
        }
    }

    void relation(const osmium::Relation& relation) {
        ++stats_.relations;
        stats_.relation_members += static_cast<std::uint64_t>(relation.members().size());
        const auto& tags = relation.tags();

        if (tags.has_key("building")) {
            ++stats_.building_relations;
            count_building_metadata(tags);
        }
        if (tags.has_tag("type", "multipolygon")) {
            ++stats_.multipolygon_relations;
        }
        if (!tags.has_tag("boundary", "administrative")) {
            return;
        }

        ++stats_.administrative_relations;
        ++stats_.administrative_level_counts[printable(tag(tags, "admin_level"))];
        const auto name = selected_name(tags);
        if (name.empty()) {
            ++stats_.administrative_relations_without_name;
        }

        std::uint64_t outer = 0;
        std::uint64_t inner = 0;
        std::uint64_t other = 0;
        for (const auto& member : relation.members()) {
            const std::string_view role{member.role()};
            if (role == "outer") {
                ++outer;
            } else if (role == "inner") {
                ++inner;
            } else {
                ++other;
            }
        }
        stats_.administrative_outer_members += outer;
        stats_.administrative_inner_members += inner;
        stats_.administrative_other_members += other;

        std::ostringstream details;
        details << "name=" << printable(name)
                << ", admin_level=" << printable(tag(tags, "admin_level"))
                << ", members=" << relation.members().size()
                << ", outer=" << outer
                << ", inner=" << inner
                << ", other_roles=" << other;
        add_sample(stats_.administrative_samples,
                   sample_limit_,
                   typed_id("relation", relation.id()),
                   details.str());
    }

private:
    void count_building_metadata(const osmium::TagList& tags) {
        ++stats_.building_type_counts[std::string{tag(tags, "building")}];
        if (!has_address(tags)) {
            ++stats_.buildings_without_address;
        }
        if (selected_name(tags).empty()) {
            ++stats_.buildings_without_name;
        }
    }

    InspectionStats& stats_;
    std::size_t sample_limit_;
};

void print_count(const char* label, std::uint64_t value) {
    std::cout << "  " << std::left << std::setw(43) << label << std::right << value << '\n';
}

void print_samples(const char* heading, const std::vector<ObjectSample>& samples) {
    std::cout << '\n' << heading << '\n';
    if (samples.empty()) {
        std::cout << "  <none found>\n";
        return;
    }
    for (const auto& sample : samples) {
        std::cout << "  " << sample.typed_id << ": " << sample.description << '\n';
    }
}

void print_top_counts(const char* heading,
                      const std::map<std::string, std::uint64_t>& counts,
                      std::size_t limit = 12) {
    std::vector<std::pair<std::string, std::uint64_t>> sorted{counts.begin(), counts.end()};
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        return left.second > right.second;
    });

    std::cout << '\n' << heading << '\n';
    const auto displayed = std::min(limit, sorted.size());
    for (std::size_t i = 0; i < displayed; ++i) {
        print_count(sorted[i].first.c_str(), sorted[i].second);
    }
    if (sorted.size() > displayed) {
        std::cout << "  ... " << (sorted.size() - displayed) << " more values\n";
    }
}

} // namespace

InspectionStats inspect_pbf(const std::filesystem::path& input_path, std::size_t sample_limit) {
    if (!std::filesystem::exists(input_path)) {
        throw std::runtime_error{"PBF file does not exist: " + input_path.string()};
    }
    if (!std::filesystem::is_regular_file(input_path)) {
        throw std::runtime_error{"PBF path is not a regular file: " + input_path.string()};
    }

    InspectionStats stats;
    stats.input_bytes = std::filesystem::file_size(input_path);
    InspectionHandler handler{stats, sample_limit};

    const auto started = std::chrono::steady_clock::now();
    osmium::io::Reader reader{input_path.string(), osmium::osm_entity_bits::all};
    osmium::apply(reader, handler);
    reader.close();
    stats.elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    stats.cpu_seconds = process_cpu_seconds();
    stats.peak_memory_mib = peak_memory_mib();
    return stats;
}

void print_report(const std::filesystem::path& input_path, const InspectionStats& stats) {
    constexpr double bytes_per_gib = 1024.0 * 1024.0 * 1024.0;
    std::cout << "\nOSM PBF inspection report\n"
              << "Input: " << input_path << '\n'
              << "Input size: " << std::fixed << std::setprecision(2)
              << static_cast<double>(stats.input_bytes) / bytes_per_gib << " GiB\n"
              << "Wall elapsed (includes host sleep): " << stats.elapsed_seconds << " seconds\n"
              << "Process CPU time: " << stats.cpu_seconds << " seconds\n"
              << "Peak process memory: " << stats.peak_memory_mib << " MiB\n";

    std::cout << "\nOSM primitives\n";
    print_count("Nodes", stats.nodes);
    print_count("Ways", stats.ways);
    print_count("Relations", stats.relations);
    print_count("Invalid node locations", stats.invalid_node_locations);

    std::cout << "\nBuilding/address candidates\n";
    print_count("Address nodes", stats.address_nodes);
    print_count("Address-only nodes", stats.address_only_nodes);
    print_count("Building-tagged nodes", stats.building_nodes);
    print_count("Building nodes with address tags", stats.building_nodes_with_address);
    print_count("Building ways", stats.building_ways);
    print_count("Building relations", stats.building_relations);
    print_count("Building-way node references", stats.building_way_node_references);
    print_count("Buildings without address tags", stats.buildings_without_address);
    print_count("Buildings without selected name", stats.buildings_without_name);

    std::cout << "\nStreet candidates\n";
    print_count("Highway ways", stats.street_ways);
    print_count("Named highway ways", stats.named_street_ways);
    print_count("Unnamed highway ways", stats.unnamed_street_ways);
    print_count("Street node references", stats.street_node_references);

    std::cout << "\nAdministrative-area candidates\n";
    print_count("Administrative ways", stats.administrative_ways);
    print_count("Administrative-way node references", stats.administrative_way_node_references);
    print_count("Administrative relations", stats.administrative_relations);
    print_count("Administrative relations without name", stats.administrative_relations_without_name);
    print_count("Administrative outer members", stats.administrative_outer_members);
    print_count("Administrative inner members", stats.administrative_inner_members);
    print_count("Administrative members with other roles", stats.administrative_other_members);
    print_count("All multipolygon relations", stats.multipolygon_relations);
    print_count("All relation members", stats.relation_members);

    print_top_counts("Most common building tag values", stats.building_type_counts);
    print_top_counts("Most common highway tag values", stats.highway_class_counts);
    print_top_counts("Administrative relation levels", stats.administrative_level_counts, 20);

    print_samples("Address/building node samples", stats.node_samples);
    print_samples("Building way samples", stats.building_samples);
    print_samples("Street way samples", stats.street_samples);
    print_samples("Administrative relation samples", stats.administrative_samples);
}

} // namespace sheet1

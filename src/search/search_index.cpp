#include "search/search_index.hpp"

#include "search/text_normalizer.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <unordered_set>

namespace osm::search {
namespace {

void add_exact_name(SearchIndex& index, const std::string& normalized_name, const SearchObjectRef ref) {
    if (normalized_name.empty()) {
        return;
    }
    index.exact_name_index[normalized_name].push_back(ref);
}

void add_tokens(SearchIndex& index, const std::string& normalized_name, const SearchObjectRef ref) {
    if (normalized_name.empty()) {
        return;
    }

    auto tokens = tokenizeNormalizedText(normalized_name);
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());

    for (const auto& token : tokens) {
        if (!token.empty()) {
            index.token_index[token].push_back(ref);
        }
    }
}

template <typename T>
void sort_and_dedup(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

void finalize_index(SearchIndex& index) {
    for (auto& [_, postings] : index.exact_name_index) {
        sort_and_dedup(postings);
    }
    for (auto& [_, postings] : index.token_index) {
        sort_and_dedup(postings);
    }
    for (auto& [_, regions] : index.region_name_index) {
        sort_and_dedup(regions);
    }
    for (auto& [_, localities] : index.locality_name_index) {
        sort_and_dedup(localities);
    }
}

[[nodiscard]] std::size_t posting_count(const std::unordered_map<std::string, std::vector<SearchObjectRef>>& map) {
    std::size_t total = 0;
    for (const auto& [_, postings] : map) {
        total += postings.size();
    }
    return total;
}

[[nodiscard]] std::size_t region_posting_count(const std::unordered_map<std::string, std::vector<std::uint32_t>>& map) {
    std::size_t total = 0;
    for (const auto& [_, postings] : map) {
        total += postings.size();
    }
    return total;
}

[[nodiscard]] std::vector<std::pair<std::string, std::size_t>> largest_token_postings(const SearchIndex& index, const std::size_t limit) {
    std::vector<std::pair<std::string, std::size_t>> values;
    values.reserve(index.token_index.size());
    for (const auto& [token, postings] : index.token_index) {
        values.emplace_back(token, postings.size());
    }
    std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) return lhs.second > rhs.second;
        return lhs.first < rhs.first;
    });
    if (values.size() > limit) {
        values.resize(limit);
    }
    return values;
}

[[nodiscard]] std::string object_type_name(const SearchObjectType type) {
    switch (type) {
        case SearchObjectType::House: return "House";
        case SearchObjectType::Street: return "Street";
        case SearchObjectType::Poi: return "POI";
        case SearchObjectType::Region: return "Region";
        case SearchObjectType::Locality: return "Locality";
    }
    return "Unknown";
}

void add_limited_example(std::vector<std::string>& examples, std::string value, const std::size_t limit = 20) {
    if (examples.size() < limit) {
        examples.push_back(std::move(value));
    }
}

} // namespace

SearchIndexBuildResult buildSearchIndex(const DataStore& data) {
    const auto start = std::chrono::steady_clock::now();

    SearchIndexBuildResult result;
    auto& index = result.index;
    auto& metrics = result.metrics;

    for (std::uint32_t i = 0; i < data.streets.size(); ++i) {
        const auto& street = data.streets[i];
        if (street.name_id == kInvalidStringId || street.name_id >= data.strings.size()) {
            ++metrics.skipped_unnamed_streets;
            continue;
        }

        const auto normalized = normalizeSearchText(data.strings.resolve(street.name_id));
        if (normalized.empty()) {
            ++metrics.skipped_unnamed_streets;
            continue;
        }

        const SearchObjectRef ref{.type = SearchObjectType::Street, .index = i};
        add_exact_name(index, normalized, ref);
        add_tokens(index, normalized, ref);
        ++metrics.indexed_streets;
    }

    for (std::uint32_t i = 0; i < data.pois.size(); ++i) {
        const auto& poi = data.pois[i];
        if (poi.name_id == kInvalidStringId || poi.name_id >= data.strings.size()) {
            ++metrics.skipped_pois_invalid_name_id;
            std::ostringstream example;
            example << "osm_id=" << poi.osm_id << " name_id=" << poi.name_id << " reason=invalid_name_id";
            add_limited_example(metrics.skipped_poi_examples, example.str());
            continue;
        }

        const auto& original_name = data.strings.resolve(poi.name_id);
        const auto normalized = normalizeSearchText(original_name);
        if (normalized.empty()) {
            ++metrics.skipped_pois_empty_normalized_name;
            std::ostringstream example;
            example << "osm_id=" << poi.osm_id << " original_name=\"" << original_name << "\" reason=empty_normalized_name";
            add_limited_example(metrics.skipped_poi_examples, example.str());
            continue;
        }

        const SearchObjectRef ref{.type = SearchObjectType::Poi, .index = i};
        add_exact_name(index, normalized, ref);
        add_tokens(index, normalized, ref);
        ++metrics.indexed_pois;
    }

    for (std::uint32_t i = 0; i < data.localities.size(); ++i) {
        const auto& locality = data.localities[i];
        if (locality.name_id == kInvalidStringId || locality.name_id >= data.strings.size()) continue;
        const auto normalized = normalizeSearchText(data.strings.resolve(locality.name_id));
        if (normalized.empty()) continue;
        const SearchObjectRef ref{.type = SearchObjectType::Locality, .index = i};
        add_exact_name(index, normalized, ref);
        add_tokens(index, normalized, ref);
        index.locality_name_index[normalized].push_back(i);
        ++metrics.indexed_localities;
    }

    metrics.regions_seen = data.regions.size();
    for (std::uint32_t i = 0; i < data.regions.size(); ++i) {
        const auto& region = data.regions[i];
        if (region.name_id == kInvalidStringId || region.name_id >= data.strings.size()) {
            ++metrics.regions_skipped_invalid_name_id;
            std::ostringstream example;
            example << "region_index=" << i << " admin_level=" << region.admin_level << " reason=invalid_name_id";
            add_limited_example(metrics.skipped_region_examples, example.str());
            continue;
        }

        const auto& original_name = data.strings.resolve(region.name_id);
        const auto normalized = normalizeSearchText(original_name);
        if (normalized.empty()) {
            ++metrics.regions_skipped_empty_normalized_name;
            std::ostringstream example;
            example << "region_index=" << i << " admin_level=" << region.admin_level << " original_name=\"" << original_name << "\" reason=empty_normalized_name";
            add_limited_example(metrics.skipped_region_examples, example.str());
            continue;
        }

        index.region_name_index[normalized].push_back(i);
        ++metrics.indexed_regions;
        std::ostringstream example;
        example << "region_index=" << i << " admin_level=" << region.admin_level << " name=\"" << original_name << "\" normalized=\"" << normalized << "\"";
        add_limited_example(metrics.indexed_region_examples, example.str());
    }

    finalize_index(index);

    metrics.exact_name_keys = index.exact_name_index.size();
    metrics.exact_name_postings = posting_count(index.exact_name_index);
    metrics.token_keys = index.token_index.size();
    metrics.token_postings = posting_count(index.token_index);
    metrics.region_name_keys = index.region_name_index.size();
    metrics.region_name_postings = region_posting_count(index.region_name_index);
    metrics.locality_name_keys = index.locality_name_index.size();
    metrics.locality_name_postings = region_posting_count(index.locality_name_index);
    metrics.largest_token_postings = largest_token_postings(index, 5);
    if (!metrics.largest_token_postings.empty()) {
        metrics.longest_posting_token = metrics.largest_token_postings.front().first;
        metrics.longest_posting_list = metrics.largest_token_postings.front().second;
    }
    metrics.build_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    return result;
}

std::vector<SearchObjectRef> intersectPostingLists(const std::vector<std::vector<SearchObjectRef>>& postings) {
    if (postings.empty()) {
        return {};
    }

    std::vector<const std::vector<SearchObjectRef>*> ordered;
    ordered.reserve(postings.size());
    for (const auto& posting : postings) {
        if (posting.empty()) {
            return {};
        }
        ordered.push_back(&posting);
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->size() < rhs->size();
    });

    std::vector<SearchObjectRef> result = *ordered.front();
    for (std::size_t i = 1; i < ordered.size(); ++i) {
        std::vector<SearchObjectRef> next;
        std::set_intersection(
            result.begin(),
            result.end(),
            ordered[i]->begin(),
            ordered[i]->end(),
            std::back_inserter(next));
        result = std::move(next);
        if (result.empty()) {
            break;
        }
    }
    return result;
}

} // namespace osm::search

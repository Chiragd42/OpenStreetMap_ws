#include "search/text_normalizer.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace osm::search {
namespace {

void append_ascii_lower_or_space(std::string& out, const unsigned char c) {
    if (std::isalnum(c)) {
        out.push_back(static_cast<char>(std::tolower(c)));
        return;
    }

    switch (c) {
        case '-':
        case '/':
            out.push_back(static_cast<char>(c));
            break;
        default:
            out.push_back(' ');
            break;
    }
}

bool starts_with_at(std::string_view input, const std::size_t pos, std::string_view token) {
    return pos + token.size() <= input.size() && input.substr(pos, token.size()) == token;
}

void append_mapped_utf8(std::string& out, std::string_view input, std::size_t& i) {
    if (starts_with_at(input, i, "Ä")) { out += "ae"; i += 2; return; }
    if (starts_with_at(input, i, "ä")) { out += "ae"; i += 2; return; }
    if (starts_with_at(input, i, "Ö")) { out += "oe"; i += 2; return; }
    if (starts_with_at(input, i, "ö")) { out += "oe"; i += 2; return; }
    if (starts_with_at(input, i, "Ü")) { out += "ue"; i += 2; return; }
    if (starts_with_at(input, i, "ü")) { out += "ue"; i += 2; return; }
    if (starts_with_at(input, i, "ß")) { out += "ss"; i += 2; return; }

    // Common apostrophe variants and acute accent: remove for search.
    if (starts_with_at(input, i, "’") || starts_with_at(input, i, "‘")) {
        i += 3;
        return;
    }
    if (starts_with_at(input, i, "´")) {
        i += 2;
        return;
    }
    if (starts_with_at(input, i, "`")) {
        i += 1;
        return;
    }

    // A small accent-stripping set for common POI words such as Café.
    if (starts_with_at(input, i, "É") || starts_with_at(input, i, "é") ||
        starts_with_at(input, i, "È") || starts_with_at(input, i, "è") ||
        starts_with_at(input, i, "Ê") || starts_with_at(input, i, "ê")) {
        out.push_back('e');
        i += 2;
        return;
    }
    if (starts_with_at(input, i, "Á") || starts_with_at(input, i, "á") ||
        starts_with_at(input, i, "À") || starts_with_at(input, i, "à") ||
        starts_with_at(input, i, "Â") || starts_with_at(input, i, "â")) {
        out.push_back('a');
        i += 2;
        return;
    }
    if (starts_with_at(input, i, "Í") || starts_with_at(input, i, "í") ||
        starts_with_at(input, i, "Ì") || starts_with_at(input, i, "ì") ||
        starts_with_at(input, i, "Î") || starts_with_at(input, i, "î")) {
        out.push_back('i');
        i += 2;
        return;
    }
    if (starts_with_at(input, i, "Ó") || starts_with_at(input, i, "ó") ||
        starts_with_at(input, i, "Ò") || starts_with_at(input, i, "ò") ||
        starts_with_at(input, i, "Ô") || starts_with_at(input, i, "ô")) {
        out.push_back('o');
        i += 2;
        return;
    }
    if (starts_with_at(input, i, "Ú") || starts_with_at(input, i, "ú") ||
        starts_with_at(input, i, "Ù") || starts_with_at(input, i, "ù") ||
        starts_with_at(input, i, "Û") || starts_with_at(input, i, "û")) {
        out.push_back('u');
        i += 2;
        return;
    }

    // Unknown non-ASCII bytes become a separator instead of corrupting tokens.
    out.push_back(' ');
    ++i;
}

std::string collapse_spaces(std::string input) {
    std::string out;
    out.reserve(input.size());
    bool previous_space = true;
    for (const char c : input) {
        if (c == ' ') {
            if (!previous_space) {
                out.push_back(' ');
            }
            previous_space = true;
        } else {
            out.push_back(c);
            previous_space = false;
        }
    }
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string normalize_street_abbreviations(const std::string& input) {
    auto tokens = tokenizeNormalizedText(input);
    for (auto& token : tokens) {
        if (token == "str") {
            token = "strasse";
            continue;
        }
        constexpr std::string_view suffix = "str";
        if (token.size() > suffix.size() && token.ends_with(suffix)) {
            token += "asse";
        }
    }

    std::string out;
    for (const auto& token : tokens) {
        if (!out.empty()) out.push_back(' ');
        out += token;
    }
    return out;
}

} // namespace

std::string normalizeSearchText(std::string_view input) {
    std::string mapped;
    mapped.reserve(input.size());

    for (std::size_t i = 0; i < input.size();) {
        const auto c = static_cast<unsigned char>(input[i]);
        if (c < 0x80) {
            if (c == '\'' || c == '`') {
                ++i;
                continue;
            }
            append_ascii_lower_or_space(mapped, c);
            ++i;
        } else {
            append_mapped_utf8(mapped, input, i);
        }
    }

    return normalize_street_abbreviations(collapse_spaces(std::move(mapped)));
}

std::string normalizeHouseNumber(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (const auto raw : input) {
        const auto c = static_cast<unsigned char>(raw);
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (raw == '-' || raw == '/') {
            out.push_back(raw);
        }
    }
    return out;
}

std::vector<std::string> tokenizeNormalizedText(std::string_view normalized_text) {
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (start < normalized_text.size()) {
        while (start < normalized_text.size() && normalized_text[start] == ' ') {
            ++start;
        }
        if (start >= normalized_text.size()) break;
        auto end = start;
        while (end < normalized_text.size() && normalized_text[end] != ' ') {
            ++end;
        }
        tokens.emplace_back(normalized_text.substr(start, end - start));
        start = end;
    }
    return tokens;
}

} // namespace osm::search

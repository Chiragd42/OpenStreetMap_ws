#include "search/text_normalizer.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect_eq(const std::string& actual, const std::string& expected, const std::string& label) {
    if (actual == expected) {
        return;
    }
    ++failures;
    std::cerr << "FAIL " << label << "\n  expected: [" << expected << "]\n  actual  : [" << actual << "]\n";
}

} // namespace

int main() {
    using osm::search::normalizeHouseNumber;
    using osm::search::normalizeSearchText;
    using osm::search::tokenizeNormalizedText;

    expect_eq(normalizeSearchText("Burger King"), "burger king", "basic case");
    expect_eq(normalizeSearchText("BURGER KING"), "burger king", "uppercase ascii");
    expect_eq(normalizeSearchText("Königstraße"), "koenigstrasse", "german umlaut sharp-s");
    expect_eq(normalizeSearchText("Koenigstrasse"), "koenigstrasse", "ascii german spelling");
    expect_eq(normalizeSearchText("KÖNIGSTRASSE"), "koenigstrasse", "uppercase umlaut");
    expect_eq(normalizeSearchText("Hauptstr."), "hauptstrasse", "street abbreviation suffix");
    expect_eq(normalizeSearchText("Hauptstraße"), "hauptstrasse", "street full form");
    expect_eq(normalizeSearchText("Stuttgart, Burger King"), "stuttgart burger king", "punctuation to spaces");
    expect_eq(normalizeSearchText("Karl’s Café"), "karls cafe", "apostrophe and accent");
    expect_eq(normalizeSearchText("  Burger   King  "), "burger king", "collapse whitespace");
    expect_eq(normalizeSearchText(""), "", "empty input");
    expect_eq(normalizeSearchText("   "), "", "space input");
    expect_eq(normalizeSearchText("!!!"), "", "punctuation only");
    expect_eq(normalizeSearchText("Str."), "strasse", "standalone street abbreviation");

    expect_eq(normalizeHouseNumber("10 A"), "10a", "house number separated suffix");
    expect_eq(normalizeHouseNumber("10A"), "10a", "house number uppercase suffix");
    expect_eq(normalizeHouseNumber("10a"), "10a", "house number lowercase suffix");
    expect_eq(normalizeHouseNumber("10-12"), "10-12", "house number range");
    expect_eq(normalizeHouseNumber("5/1"), "5/1", "house number slash");

    const auto tokens = tokenizeNormalizedText("burger king stuttgart");
    if (tokens.size() != 3 || tokens[0] != "burger" || tokens[1] != "king" || tokens[2] != "stuttgart") {
        ++failures;
        std::cerr << "FAIL tokenization\n";
    }

    if (failures != 0) {
        std::cerr << failures << " normalizer test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All normalizer tests passed\n";
    return EXIT_SUCCESS;
}

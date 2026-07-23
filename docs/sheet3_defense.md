# Sheet 3 — Demo and Oral Defense Guide

This document maps every Sheet 3 sentence to the implementation, a test, and a concise oral answer. The implementation is **dataset-neutral**: names are read from the runtime datastore and are not compiled into production search logic.

## One-minute architecture

1. Load the PBF or versioned binary cache into compact vectors and a string pool.
2. Normalize searchable names and build exact-name, token, address, locality, region, suffix-array, typed BK-tree, and POI spatial indexes.
3. Normalize and tokenize each query.
4. Generate multiple contiguous interpretations for locality, entity, and house-number spans rather than committing to one word order.
5. Retrieve candidates and rank them deterministically by exactness, unexplained tokens, strategy, administrative containment, edit cost, rarity, viewport evidence, distance, and stable object id.
6. Apply strict viewport filtering when possible; otherwise expose an explicit global fallback.
7. Limit results, aggregate nearby results with DSU single-linkage clustering, serialize JSON, and show list entries and map markers.

## Mandatory Task 1 — String preprocessing

Implementation: `src/search/text_normalizer.cpp`.

- Unicode-aware lowercase handling for supported Latin/German text.
- `ä/ö/ü/ß` and transliterated spellings converge to stable keys.
- punctuation, apostrophe variants, whitespace, and street abbreviations are standardized.
- display strings are preserved; only index/query keys are normalized.

**Why:** one canonical key avoids storing every spelling variant and keeps lookup deterministic.

**Evidence:** `tests/normalizer_tests.cpp`.

## Mandatory Task 2 — Geocoder

Implementation: `src/search/search_index.cpp`, `src/search/geocode_query.cpp`, `/geocode` in `src/server/http_server.cpp`, and `frontend/index.html`.

- Inverted token index for streets, POIs, regions, and localities.
- Exact full-name index.
- Secondary locality/region indexes and stored containing-region relations.
- Address index keyed by normalized street plus normalized house number.
- Multiple locality/entity/house spans resolve spacing and order ambiguity.
- GUI has a text input, timed result list, and map markers.

**Ambiguous example answer:** for `Oberer Grundweg Vaihingen`, generate plausible contiguous spans and rank only interpretations supported by indexes and administrative evidence. This is heuristic natural-language interpretation, not unrestricted language understanding.

## Mandatory Task 3 — Heuristics

Ranking order is deterministic:

1. exact address;
2. exact name;
3. fewer unexplained tokens;
4. original query before literal substring completion before fuzzy correction;
5. longer exact entity evidence;
6. recognized locality and most specific shared administrative relation (8 before 6 before 4);
7. lower edit cost;
8. rarer posting list;
9. viewport evidence and distance for remaining ties;
10. stable object reference.

**Why:** lexical certainty must beat geographic convenience. A valid exact Stuttgart result should not silently become an unrelated Aalen result merely because Aalen is visible. The separate viewport policy then implements the optional “only show results in view” task explicitly.

## Mandatory Task 4 — timings, measurements, cache

Reported or exposed timings include:

- PBF parse or cache load;
- region/point-in-polygon assignment for houses, POIs, and localities;
- reverse-index and search-index build;
- geocoder normalization, interpretation, lookup/region matching, ranking, and total;
- reverse-geocoder/API request profiles.

Counts include nodes, ways, relations, buildings/houses, streets, regions, POIs, and localities. `/stats` exposes the ingest measurements.

Binary persistence uses a magic number and explicit version. Commands:

```sh
./build-release/osm_geocoder --pbf=data/pbf/baden-wuerttemberg-260602.osm.pbf --save-cache=data/cache/demo.bin
./build-release/osm_geocoder --load-cache=data/cache/demo.bin
```

Old cache versions fail explicitly instead of being misread (`tests/json_tests.cpp`).

## Optional Task 1 — substring search

All normalized full names contribute suffix references. The sorted suffix array is searched by binary search; returned names then use their existing object postings.

- Build: approximately `O(S log S)` comparisons for `S` suffixes.
- Lookup: `O(log S + matches)` before bounded result sorting.
- One-character expansion is disabled to avoid enormous low-quality ranges.

**Why not remove the inverted index:** the suffix array discovers matching full names; exact/token/address indexes still provide efficient object postings. The structures complement each other.

## Optional Task 2 — aggregating

After ranking and result limiting, coordinate-bearing results within the configured threshold (20 m in the GUI) are connected. Disjoint-set union computes deterministic transitive single-linkage components. The best-ranked member is the representative and the marker uses the member centroid.

**Why after limiting:** it bounds the current quadratic pair check and guarantees cluster indices refer to actual response results.

## Optional Task 3 — fuzzy correction

Each name kind has a BK-tree vocabulary. Query tokens receive adaptive edit budgets: short tokens stay strict; longer tokens may tolerate more edits. Numbers are never corrected. Original tokens remain available, partial matches take precedence over fuzzy guesses, alternatives are bounded, and exact/original results rank above corrections.

The API returns `corrected_query` only when the top result actually came from a fuzzy interpretation.

**Honest limitation:** no algorithm can infer a unique intended word from arbitrary noise. This is bounded candidate correction, not a promise to correct every possible input.

**Proof it is generic:** tests insert unrelated invented runtime names (`Zeraphine Boulevard`, `Copper Lantern Cafe`) and correct deletion/substitution/multiple-token errors without changing production code.

## Optional Task 4 — results in view

With a bbox:

- if at least one candidate is in view, only in-view candidates are returned;
- if none is in view, globally ranked candidates are returned and `viewport_fallback=true`;
- JSON also exposes `viewport_applied`, `viewport_filtered`, `global_candidate_count`, and `in_viewport_candidate_count`;
- the GUI explicitly says either “current-view results only” or “no matches in view; showing global results”.

## Optional Task 5 — official examples

- `Aalen Bahnhofstrasse 10`: real-cache address query.
- `Stuttgart Burger King`: real-cache locality + POI query.
- `Closest Park to Kaistrasse 5, Kiel`: deterministic synthetic acceptance fixture because the bundled dataset is Baden-Württemberg; use a Germany/Kiel cache for a live Kiel demonstration.

The nearest query first resolves the reference using the ordinary geocoder, then searches only the requested POI category and orders by haversine distance. A closer non-park in the fixture proves category filtering.

## Recommended live demo (5–7 minutes)

Start the server:

```sh
./build-release/osm_geocoder --load-cache=data/cache/baden-wuerttemberg-260602.bin --serve --port=8080
open frontend/index.html
```

Show, in order:

1. `Aalen Bahnhofstrasse 10` — normalization, exact address, timing, marker.
2. `Stuttgart Burger King` — locality interpretation and shared relation.
3. Remove characters from a known displayed name — generic fuzzy correction and visible corrected query.
4. Enter a prefix/interior fragment of a known name — suffix-array partial match.
5. Pan away and repeat a query — explicit global viewport fallback.
6. Pan to an area containing one of several same-name results — only in-view results.
7. A common POI name — aggregated marker count.
8. Reverse-geocode a map click — reverse timing/context.
9. `/stats` — counts and preprocessing measurements.

## Likely questions and short answers

### Is this “natural language”?

It implements the sheet’s requested interpretation choices with deterministic parsing heuristics: normalization, tokenization, span generation, intent recognition, and evidence-based ranking. It is deliberately not an LLM or universal grammar.

### Why BK-tree?

It indexes a static vocabulary under edit distance and prunes branches using metric bounds, avoiding a full vocabulary scan for every typo.

### Why suffix array?

It is compact, deterministic, binary-searchable, and simpler than a dynamic suffix tree because the search index is static after startup.

### How do duplicate city or region polygons work?

Candidate containment compares stable source relation ids, not only polygon-vector indices. Multiple fragments of one OSM relation therefore represent one logical administrative region.

### Why does exact beat fuzzy?

Fuzzy output is uncertain. Exact lexical evidence must not be displaced by a nearby guess. Match strategy and edit cost are explicit ranking dimensions.

### What happens outside the viewport?

If any matches are inside, outside candidates are removed. If none are inside, global results are returned with explicit fallback metadata so the user is never shown an unexplained empty list.

### What is not claimed?

- perfect correction of arbitrary text;
- universal conversational language understanding;
- real Kiel coverage from a Baden-Württemberg extract;
- globally optimal clustering for an unbounded result set.

These are explicit scope boundaries, while every mandatory Sheet 3 requirement and optional Tasks 1–4 are implemented and tested.

## Validation commands

```sh
cmake --build build-release -j2
ctest --test-dir build-release --output-on-failure
./build-release/osm_geocoder --load-cache=data/cache/baden-wuerttemberg-260602.bin --test-geocode-query="Aalen Bahnhofstrasse 10"
./build-release/osm_geocoder --load-cache=data/cache/baden-wuerttemberg-260602.bin --test-geocode-query="Stuttgart Burger King"
```
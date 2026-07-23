# Sheet 3 Search Baseline

## Final Sheet 3 implementation

- Partial names: suffix array over full normalized names (spaces retained), with prefix-first/posting/completion ordering.
- Typos: typed Street/Locality/Region/POI BK-trees and a bounded query beam.
- Nearest intent: `closest|nearest <category> to|near|from <reference>`; reference is geocoded first, then category-specific occupied grid cells are traversed in admissible lower-bound order.
- Viewport: optional `/geocode` `bbox`; in-view is a soft tie-break after stronger semantic evidence, with out-of-view results retained.
- Clustering: deterministic DSU single-linkage at 20 m after final limiting; representative is the best-ranked member and cluster coordinate is the member centroid.
- Cache remains version 6; all new Sheet 3 indexes are runtime-derived.
- Germany/Kiel real-data validation is opt-in (`make prep-germany`, `make validate-kiel`) and was not run because no Germany PBF/cache was present. Synthetic Kiel coverage is part of CTest.

Captured before implementing Sheet 3 substring indexing, aggregation, viewport-aware results, or nearest-category queries.

## Environment

- Build type: CMake `Release`
- Dataset: `baden-wuerttemberg-260602`
- Cache: binary cache version 6
- Measurements: median of seven `/geocode` requests against one already-loaded local server
- Baseline commit parent: `798a8f8 Fix reverse click marker position`

## Existing validation

All existing executables passed before the Sheet 3 changes:

```text
normalizer_tests: passed
search_index_tests: passed
geocode_query_tests: passed
json_tests: passed
```

## Search-index baseline

```text
indexed_streets: 332734
indexed_pois: 96878
indexed_regions: 1559
indexed_localities: 10604
indexed_addresses: 2880021
exact_name_keys: 164968
exact_name_postings: 441775
token_keys: 124870
token_postings: 636510
build_seconds: 2.11975
```

## Query baseline

| Query | Median total query time | Current first result | Current result count |
|---|---:|---|---:|
| `Aalen Bahnhofstrasse 10` | 0.402416 ms | house: `Bahnhofstraße 10` | 200 |
| `Stuttgart Burger King` | 0.087209 ms | POI: `Burger King` | 114 |
| `Aalen Bahn 10` | 0.021625 ms | street: `Bahn 10` | 5 |
| `Aalen Bahnofstrasse 10` | 381.957 ms | house: `Bahnhofstraße 10` | 200 |
| `Closest Park to Kaistrasse 5, Kiel` | 715.372 ms | unrelated street: `5` | 118 |

The typo and unsupported nearest-category queries are slow because the current fallback scans large hash-map vocabularies. `Aalen Bahn 10` also demonstrates that the current token-prefix fallback can prefer a literal weak match instead of completing the intended street name.

## Acceptance fixtures

`tests/geocode_query_tests.cpp` contains:

- regression checks for the two currently working assignment examples;
- a synthetic Kiel reference address at `Kaistraße 5`;
- two park POIs and a geographically closer non-park POI.

The synthetic fixture allows the nearest-category behavior to be implemented and tested without hard-coding Kiel or requiring a Germany PBF in CI.

## Phase 2 suffix-array scale check

The first real-cache run after adding the compact full-name suffix array reported:

```text
indexed_full_names: 171708
suffix_count: 2536434
estimated_suffix_bytes: 20291472
build_seconds: 2.80983
maximum resident set size: 867205120 bytes
```

Suffix references begin only at non-whitespace positions. The measured array itself is about 20.3 MB; full startup memory includes the complete cached OSM datastore and all pre-existing indexes.

## Phase 3 fuzzy-index check

The typed BK-trees contain 135,570 vocabulary entries across street, locality, region, and POI-name classes. Representative real-cache query times fell to approximately 5.4 ms for `Aalen Bahnofstrasse 10` and 7.1 ms for `Stutgart Burgar King`, instead of scanning the full vocabulary for hundreds of milliseconds.

## Final real-cache validation

Measured against one already-loaded BW server process after all Sheet 3 features, using seven warm requests per query (median shown):

| Query | Median total time | First result | Returned |
|---|---:|---|---:|
| `Aalen Bahnhofstrasse 10` | 3.73 ms | house `Bahnhofstraße 10` | 200 |
| `Stuttgart Burger King` | 20.24 ms | POI `Burger King` | 200 |
| `Aalen Bahn 10` | 1.78 ms | house `Bahnhofstraße 10` | 200 |
| `Aalen Bahnofstrasse 10` | 5.19 ms | house `Bahnhofstraße 10` | 200 |

Final startup/index observations on `data/cache/baden-wuerttemberg-260602.bin`:

```text
Load seconds: 3.82194
search index build_seconds: 3.24573
indexed_full_names: 171708
suffix_count: 2536434
estimated_suffix_bytes: 20291472
fuzzy_vocabulary_tokens: 135570
live resident set: 282528 KiB (about 289 MB decimal)
```

The 200-result POI query includes bounded O(k²) post-limit clustering, which explains its higher final timing. Germany/Kiel real-data validation remains explicitly unexecuted because no Germany PBF/cache is present; synthetic Kiel acceptance coverage passes in CTest.
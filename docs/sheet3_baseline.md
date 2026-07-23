# Sheet 3 Search Baseline

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
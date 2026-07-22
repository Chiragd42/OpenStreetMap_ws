Local C++ geocoder for the Baden-Wuerttemberg OpenStreetMap extract. The app loads OSM data from a PBF or a binary cache, builds search indexes, serves map data over HTTP, supports reverse geocoding, and exposes a forward geocoder through both `/geocode` and the Leaflet UI.

## Quick Start

Place a `.osm.pbf` file under `data/pbf/`, then build and prepare a cache:

```sh
make prep
```

Start the backend:

```sh
make server CACHE=data/cache/baden-wuerttemberg.bin
```

Start the frontend from a second terminal:

```sh
make ui
```

Open:

```text
http://127.0.0.1:5500/frontend/index.html
```

## Command Line Checks

Build and run the unit tests:

```sh
cmake --build build-release -j2
./build-release/normalizer_tests
./build-release/search_index_tests
./build-release/geocode_query_tests
./build-release/json_tests
```

Run a forward-geocode query directly from the cache:

```sh
./build-release/osm_geocoder \
  --load-cache=data/cache/baden-wuerttemberg.bin \
  --no-merge-streets \
  --test-geocode-query="Aalen Bahnhofstrasse 10"
```

Run the HTTP server:

```sh
./build-release/osm_geocoder \
  --load-cache=data/cache/baden-wuerttemberg.bin \
  --serve \
  --port=8080
```

Example HTTP queries:

```sh
curl "http://localhost:8080/geocode?q=Aalen%20Bahnhofstrasse%2010"
curl "http://localhost:8080/geocode?q=Bahnhofstrasse%2010%20Aalen"
curl "http://localhost:8080/geocode?q=Stuttgart%20Burger%20King"
curl "http://localhost:8080/geocode?q=Burger%20King%20Stuttgart"
curl "http://localhost:8080/geocode?q=Baden%20Wuerttemberg"
curl "http://localhost:8080/reverse?lat=48.8391&lon=10.0948"
```

## Data Pipeline

The PBF extractor reads nodes, ways, and relations with libosmium. It extracts:

- house address points from address nodes and building polygons
- street polylines
- POIs such as shops, restaurants, cafes, fast food, parks, hotels, schools, hospitals, and stations
- named localities such as cities, towns, villages, suburbs, quarters, neighbourhoods, and hamlets
- administrative region polygons

Level 2 and level 4 administrative regions are reconstructed where possible from relation geometry. Postal regions are excluded from locality/ranking relation matching so postal boundaries do not outrank administrative containment.

The extractor precomputes containment lists:

- house -> containing administrative regions
- street -> containing administrative regions, using multiple sampled points along the street geometry
- POI -> containing administrative regions
- locality -> containing administrative regions
- administrative region -> broader parent administrative regions

These lists make forward geocoder ranking deterministic without doing point-in-polygon work per query.

## Normalization

Search normalization is intentionally conservative and deterministic:

- lowercases ASCII letters
- strips common accents and German variants
- maps `ß` to `ss`
- normalizes `ä`, `ö`, `ü`-style text into searchable forms such as `ae`, `oe`, `ue`
- treats punctuation and repeated whitespace as separators
- preserves useful house-number separators such as `/` and `-`
- supports common street abbreviations through normalized tokens, for example `str.` and `strasse`

Examples:

- `Königstraße 1 Stuttgart` -> `koenigstrasse 1 stuttgart`
- `Bahnhofstrasse 10 Aalen` -> `bahnhofstrasse 10 aalen`

## Search Indexes

The backend builds several indexes from the loaded `DataStore`:

- exact-name index for POIs, streets, regions, and localities
- token inverted index for broad named-object lookup, including region candidates
- region-name index
- locality-name index
- exact address index keyed by normalized street name plus normalized house number

The exact address index is what makes `Aalen Bahnhofstrasse 10` and `Bahnhofstrasse 10 Aalen` resolve quickly to global `Bahnhofstraße 10` candidates before locality-aware ranking is applied.

## Query Interpretation

`osm::search::runGeocodeQuery(data, search_index, input)` separates interpretation from ranking. It generates candidate interpretations such as:

- address with recognized locality
- address without recognized locality
- named object with recognized locality
- named object without recognized locality
- fallback spans when some tokens are unexplained

For example, `CompletelyUnknownTown Bahnhofstrasse 10` keeps a fallback interpretation for `bahnhofstrasse 10`, records one unexplained token, and still returns global exact-address candidates.

## Ranking

Ranking is deterministic and prioritizes:

- exact address matches
- exact name matches
- candidates sharing the most specific administrative relation with the recognized locality
- recognized locality over unrecognized locality
- fewer unexplained tokens
- distance to the locality when applicable
- stable object ordering as a final tie-breaker

Expected examples:

- `Aalen Bahnhofstrasse 10`: first result is `Bahnhofstraße 10`, city `Aalen`, shared relation `Aalen`, admin level 8.
- `Stuttgart Burger King`: first results are `Burger King` POIs sharing relation `Stuttgart`, admin level 6.
- `Aalen Marktstrasse`: street candidates can be ranked by their containing administrative region.
- `Baden Wuerttemberg`: administrative regions are returned as named-object candidates.
- `CompletelyUnknownTown Bahnhofstrasse 10`: returns global `Bahnhofstraße 10` candidates with `locality_recognized: false`.

## HTTP API

### `GET /geocode?q=<query>`

Returns normalized query text, timing data, interpretations, and ranked results:

```json
{
  "query": "Aalen Bahnhofstrasse 10",
  "normalized_query": "aalen bahnhofstrasse 10",
  "timing": {
    "normalization_ms": 0.01,
    "interpretation_ms": 0.05,
    "candidate_lookup_ms": 0.08,
    "region_matching_ms": 0.04,
    "ranking_ms": 0.02,
    "total_ms": 0.20
  },
  "interpretations": [],
  "results": []
}
```

Missing `q` returns `400`. Empty `q` and punctuation-only queries return the normal empty-result JSON shape.

### `GET /reverse?lat=<lat>&lon=<lon>`

Returns the clicked query point, administrative context for the clicked point, nearest POI context, nearest street context, and the best reverse result. If a nearby house is available, `nearest.type` is `house`; otherwise the endpoint falls back to a nearby street result with `nearest.type` set to `street`. If no nearby house or street is available but the click is inside administrative regions, it returns the most specific region as `nearest.type = "region"`. The reverse geocoder uses the spatial grids over house points, street polylines, and regions, and is independent from the forward geocoder's `geocodeLayer` in the UI.

### BBox Endpoints

The frontend uses:

- `/houses?bbox=minLon,minLat,maxLon,maxLat`
- `/streets?bbox=minLon,minLat,maxLon,maxLat`
- `/regions?bbox=minLon,minLat,maxLon,maxLat`
- `/stats`

## Frontend

The Leaflet UI is in `frontend/index.html`.

Forward search:

- input placeholder: `Search address or place...`
- calls `/geocode?q=...`
- shows ranked result cards and query timing
- draws markers on a dedicated `geocodeLayer`
- fits or zooms to forward-search results

Reverse mode:

- remains controlled by `Reverse Mode: ON/OFF`
- draws clicked point, nearest address, and connecting line on a separate `reverseLayer`
- displays house results, street fallback results, and region fallback results distinctly
- is not mixed with forward geocoder markers

## Binary Cache

The cache format uses magic `OSMC` and the current supported cache version is 6. Version 6 stores the data required by the forward and reverse geocoder, including POIs, localities, administrative regions, street containment, region-parent containment, and precomputed containment arrays.

Older cache versions are rejected. For example, loading a version 2 cache fails cleanly with:

```text
Failed to load cache: Unsupported cache version: 2
```

Regenerate a current cache with:

```sh
./build-release/osm_geocoder \
  --pbf=data/pbf/baden-wuerttemberg-260602.osm.pbf \
  --save-cache=data/cache/baden-wuerttemberg.bin \
  --no-merge-streets \
  --test-geocode-query="Aalen Bahnhofstrasse 10"
```

## Timings and Metrics

Startup logs and `/stats` expose the main operational metrics:

- processed node, way, and relation counts
- extracted house, street, region, POI, and locality counts
- POI-region assignment time
- locality-region assignment time
- house-region assignment time as `region_assignment_seconds`
- PBF parse time as `Parse seconds`
- cache-backed startup as `Load seconds`
- street merge time
- search-index build time and index counts
- `/geocode` query timings in the JSON response
- `/reverse` request timing in the HTTP request log
- cache save confirmation when `--save-cache` succeeds

- Representative Baden-Wuerttemberg cache validation:

- nodes: 56,144,314
- ways: 9,164,239
- relations: 151,029
- houses: 2,920,961
- raw streets: 2,359,316
- POIs: 96,879
- localities: 10,604
- regions: 14,954

## Validation Queries

Use these before final submission:

```text
Aalen Bahnhofstrasse 10
Bahnhofstrasse 10 Aalen
Stuttgart Burger King
Burger King Stuttgart
Königstraße 1 Stuttgart
Hauptstr. 10 Aalen
CompletelyUnknownTown Bahnhofstrasse 10
Baden Wuerttemberg
Aalen Marktstrasse
```

Required checks:

- `Aalen Bahnhofstrasse 10` ranks the Aalen `Bahnhofstraße 10` address first.
- `Bahnhofstrasse 10 Aalen` ranks the same address first.
- `Stuttgart Burger King` and `Burger King Stuttgart` rank Stuttgart Burger King POIs first.
- unknown locality fallback still returns global exact-address candidates.
- region-name queries return administrative region candidates.
- street-name queries with localities prefer street candidates sharing that locality.
- `/reverse` returns the nearest house when available, falls back to nearest street when no nearby house is available, and falls back to the clicked administrative region in empty areas.
- GUI search shows results and markers.
- GUI reverse mode still works independently.
- old cache versions are rejected cleanly.

## Known Limitations

- No fuzzy spelling correction yet; misspellings such as `Bahnhofstrase` are not guaranteed to match.
- Ranking does not yet prioritize current map viewport.
- Street geometry is represented in the forward API by a representative point, not a returned polyline.
- Duplicate OSM-derived address points may appear when multiple source geometries represent the same address.
- Street-to-region assignment is still an approximation: it samples capped points along each street rather than intersecting the full street geometry with every administrative polygon.
- Full polygon intersection detection, constructing overlap polygons, hierarchy-based point-in-polygon acceleration, and robust multipolygon repair are future work.
- The frontend is intentionally lightweight and served separately with `python3 -m http.server`.

## Generated Files

Generated files are ignored:

- `build-release/`
- `data/cache/`
- `data/pbf/*.osm.pbf`
- `*.bin`
- `.DS_Store`

Keep `data/pbf/.gitkeep` and `data/gpkg/.gitkeep` tracked so the expected data directories exist after checkout.

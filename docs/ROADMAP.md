# End-to-End Roadmap (Stuttgart First)

This roadmap is optimized for grading criteria: correctness, algorithmic quality, runtime, memory efficiency, and demonstrable results.

## Phase 0 — Foundation (0.5–1 day)

**Goal:** establish a clean C++20/CMake baseline.

- Buildable skeleton (`src/`, `include/`, CMake targets)
- Basic app entrypoint and module boundaries
- Data folders for `.osm.pbf` and future `.gpkg`

## Phase 1 — Sheet 1: Extraction, Preprocessing, Visualization (3–7 days)

**Goal:** parse OSM data and expose map-ready outputs.

### 1.1 PBF Ingestion (libosmium)
- Streaming parse of nodes, ways, relations
- Filter to critical tags:
  - `addr:housenumber`, `addr:street`, `addr:city`, `addr:postcode`
  - `highway`, `name`
  - `building`
  - `boundary=administrative`, `admin_level`

### 1.2 Preprocessing
- Houses: derive representative points (direct node or building centroid)
- Streets: retain geometry + name + hierarchy info
- Admin areas: extract polygons/multipolygons + bbox precompute
- Normalize strings for later forward geocoding

### 1.3 Performance counters (critical)
- Extraction wall-clock time (`std::chrono`)
- Object counts by type/tag class
- Memory footprint snapshots

### 1.4 GUI + backend endpoint
- Leaflet map with viewport rendering (naive acceptable initially)
- Layers: houses, streets, boundaries

## Phase 2 — Sheet 2: Reverse Geocoding (3–5 days)

**Goal:** coordinate → structured address.

### 2.1 Spatial algorithms
- Point-in-polygon with bbox prefilter
- Spatial lookup for nearest street/house candidates

### 2.2 Reverse pipeline
- Input `(lat, lon)`
- Determine containing admin area
- Retrieve nearest address candidates
- Return structured output (street, housenumber, city, postcode, confidence)

### 2.3 Validation
- Test set for central + suburban Stuttgart points
- Accuracy and latency report

## Phase 3 — Sheet 3: Forward Geocoding (4–7 days)

**Goal:** text query → ranked coordinates.

### 3.1 Text indexing
- Normalization/tokenization pipeline
- Inverted token index for candidate retrieval

### 3.2 Ranking
- Field match quality (street/housenumber/city/postcode)
- Exact vs partial token match
- Locality consistency boosts

### 3.3 Output
- Top-k ranked candidates
- Coordinate + confidence + matched fields

## Phase 4 — Optimization & Final Evaluation (2–4 days)

**Goal:** lock in performance and grading artifacts.

- Profile with `perf` and/or Valgrind
- Reduce memory overhead in hot structures
- Benchmark reverse/forward p50/p95 latency
- Produce final evidence pack for presentation

## Suggested milestone order

1. Scaffold complete
2. PBF parser + extraction counters
3. Preprocessing + serialized in-memory model
4. Leaflet viewport rendering
5. Reverse geocoder + point-in-polygon
6. Forward geocoder + ranking
7. Performance tuning + final report

# Active Context

## Current focus
Stabilize Sheet-1 pipeline with optional street-segment merging, safe demo toggles, and clear professor-friendly run flow.

## Current implementation snapshot
- Offline monolith in C++20 with CMake build.
- Active ingestion source: OpenStreetMap PBF (`data/pbf/stuttgart-regbez-260416.osm.pbf` by default in extraction config).
- PBF parser uses libosmium streaming with filter-on-read conversion into internal data structures.
- Extraction currently builds:
  - houses (address nodes + addressed building ways to representative points),
  - streets (`highway=*` ways, language normalization `name:de` -> `name`),
  - regions (simple administrative boundary ways + bounded handling for complex relations).
- Spatial grid index is built after ingest for bbox query acceleration.
- API routes available when started with `--serve`:
  - `GET /stats`
  - `GET /houses?bbox=minLon,minLat,maxLon,maxLat`
  - `GET /streets?bbox=minLon,minLat,maxLon,maxLat`
  - `GET /regions?bbox=minLon,minLat,maxLon,maxLat`
- Optional street merge toggle added:
  - `--merge-streets` (default on)
  - `--no-merge-streets`
- New console block added after normal stats:
  - `[StreetMerge]` with raw/merged/reduced/merge_time summary.

## Recent decisions
- Commit to PBF-first Sheet-1 pipeline; GPKG path is now legacy/de-emphasized.
- Use deterministic house representative-point strategy and expose quality counters.
- Add lightweight fixed-grid index (`cell_id -> object indices`) instead of naive full scans.
- Implement street merge as post-processing (not during raw extraction) to reduce breakage risk.
- Keep existing API and existing stats output unchanged; append merge stats block only.
- Keep merge grouping pragmatic and simple for Sheet 1 (`name_id + highway_class_id` with endpoint connectivity).

## Immediate next steps
1. Add README limitation note and defense line for street merge heuristic.
2. Add one extra optional merge metric (avg segments per merged street) for stronger demo defense.
3. Optionally refactor merge logic out of `app.cpp` into dedicated preprocess module for cleanliness.
4. Continue toward Sheet 2 (PIP + reverse geocoder).

## Risks / watch items
- Build artifacts can be accidentally tracked if ignore/tracking rules are not kept clean.
- HTTP server is intentionally minimal and needs hardening for broader usage.
- Region extraction deliberately bounds relation complexity; edge-case completeness remains future work.
- Geometry correctness should still be validated with broader test fixtures.

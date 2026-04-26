# Active Context

## Current focus
Deliver Sheet-1-aligned PBF-first architecture with robust extraction counters, region visualization support, and lightweight indexed bbox querying.

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

## Recent decisions
- Commit to PBF-first Sheet-1 pipeline; GPKG path is now legacy/de-emphasized.
- Use deterministic house representative-point strategy and expose quality counters.
- Add lightweight fixed-grid index (`cell_id -> object indices`) instead of naive full scans.

## Immediate next steps
1. Add optional GeoJSON debug export CLI path for validation artifacts.
2. Harden PBF region handling incrementally where needed (without overengineering Sheet 1).
3. Begin Sheet 2 work (PIP and reverse geocoder) on top of new region model/bbox precompute.
4. Continue memory-bank hygiene after each significant change.

## Risks / watch items
- Build artifacts can be accidentally tracked if ignore/tracking rules are not kept clean.
- HTTP server is intentionally minimal and needs hardening for broader usage.
- Region extraction deliberately bounds relation complexity; edge-case completeness remains future work.
- Geometry correctness should still be validated with broader test fixtures.

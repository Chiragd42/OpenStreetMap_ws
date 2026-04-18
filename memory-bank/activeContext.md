# Active Context

## Current focus
Initialize and stabilize foundational project documentation (memory bank) for long-term development continuity.

## Current implementation snapshot
- Offline monolith in C++20 with CMake build.
- Active ingestion source: GeoPackage (`data/gpkg/stuttgart-regbez.gpkg` by default in extraction config).
- Extractor currently pulls:
  - roads from `gis_osm_roads_free` (line geometries),
  - building polygons from `gis_osm_buildings_a_free` (reduced to representative points),
  - optional admin boundaries count from `gis_osm_adminareas_a_free`.
- API routes available when started with `--serve`:
  - `GET /stats`
  - `GET /houses?bbox=minLon,minLat,maxLon,maxLat`
  - `GET /streets?bbox=minLon,minLat,maxLon,maxLat`

## Recent decisions
- Prioritize durable memory bank setup now because project is expected to run for a long time.
- Keep repository lean using `.gitignore` (notably for `build/` and large `.gpkg` artifacts).

## Immediate next steps
1. Keep memory bank updated after every significant code/architecture change.
2. Implement Sheet 2 reverse geocoding pipeline using current data model foundation.
3. Introduce spatial indexing to replace naive bbox linear scans.
4. Progress toward Sheet 3 forward geocoding with token index + ranking.

## Risks / watch items
- Build artifacts can be accidentally tracked if ignore/tracking rules are not kept clean.
- Current HTTP and query layers are intentionally minimal and need hardening for larger datasets.
- Geometry parsing correctness/performance should be validated with broader test cases.

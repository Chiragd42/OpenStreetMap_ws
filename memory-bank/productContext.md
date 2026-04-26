# Product Context

## Why this project exists
The project exists to deliver a practical, high-performance geocoding system over OpenStreetMap data for academic evaluation and real engineering practice. It is intentionally scoped to emphasize algorithmic quality, spatial processing correctness, and measurable performance.

## Problems it solves
- Converts raw geospatial OSM-derived data into queryable structures.
- Provides machine-usable APIs for map viewport rendering and later geocoding features.
- Creates a foundation for:
  - reverse geocoding (coordinate → address),
  - forward geocoding (text → ranked coordinate candidates).

## Current user workflow
1. Build C++ project with CMake.
2. Run extractor/application against Stuttgart PBF dataset (`data/pbf/stuttgart-regbez-260416.osm.pbf`).
3. Optionally start local HTTP API (`--serve`) on port 8080.
4. Query routes (`/stats`, `/houses`, `/streets`, `/regions`) manually or through the frontend.

## UX and product goals
- Fast local iteration for development and benchmarking.
- Predictable API responses for frontend integration.
- Easy-to-understand operational flow: ingest once, query many.
- Documentation-first approach so long-term development remains coherent.

## Sheet-1 alignment notes (current)
- PBF streaming ingestion is active (libosmium-based).
- Frontend now visualizes all required primitives for Sheet 1:
  - houses as points,
  - streets as lines,
  - regions as polygons.
- Runtime exposes extraction quality/performance counters for grading evidence.

## Non-goals (current stage)
- Full production deployment infrastructure.
- Global-scale dataset handling and distributed processing.
- Advanced auth/multitenant concerns.

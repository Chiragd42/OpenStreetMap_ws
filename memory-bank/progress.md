# Progress

## Current status summary
Project has a strong Sheet-1-oriented foundation and a running local API/visualization loop, with major geocoding features still pending.

## What works now
- Buildable C++20/CMake scaffold.
- `geocoder_core` library and `osm_geocoder` executable.
- GPKG ingestion via SQLite from Stuttgart dataset path.
- Extraction of roads and building-derived representative house points.
- Basic parse/memory metrics collection (`ParseStats`).
- Lightweight HTTP server with bbox-filtered JSON endpoints:
  - `/stats`
  - `/houses`
  - `/streets`
- Frontend scaffold available for map rendering.

## In progress / partially implemented
- Admin boundary handling exists as optional count-oriented extraction, not yet exposed as full query/result feature.
- BBox query uses linear scans (functional but not scalable enough for final performance goals).

## Not implemented yet (major)
- Full reverse geocoding pipeline (Sheet 2).
- Full forward geocoding pipeline and ranking model (Sheet 3).
- Dedicated benchmarking harness for p50/p95 and reproducible evaluation outputs.
- Comprehensive tests for geometry parsing and endpoint correctness.

## Known issues / project risks
- Push hygiene issues can recur if generated files are re-tracked accidentally.
- Very large dataset artifacts must remain untracked to avoid remote push failures.
- Performance ceilings likely without spatial indexing.

## Next milestones (recommended order)
1. Stabilize memory bank maintenance cadence.
2. Add spatial indexing foundation.
3. Implement and validate reverse geocoder.
4. Implement forward geocoder + ranking.
5. Run profiling/optimization and finalize evaluation artifacts.

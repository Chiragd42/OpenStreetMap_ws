# Progress

## Current status summary
Project now has a Sheet-1-aligned PBF-first ingestion + preprocessing + visualization pipeline, with Sheet 2/3 geocoding features still pending.

## What works now
- Buildable C++20/CMake scaffold.
- `geocoder_core` library and `osm_geocoder` executable.
- PBF ingestion via libosmium from Stuttgart dataset path (`data/pbf/stuttgart-regbez-260416.osm.pbf`).
- Extraction of:
  - houses (address nodes + addressed building representative points),
  - streets (with unnamed tracking + preferred-language naming),
  - regions (bounded administrative extraction).
- Parse/memory + extraction-quality metrics collection (`ParseStats`).
- Lightweight HTTP server with indexed bbox-filtered JSON endpoints:
  - `/stats`
  - `/houses`
  - `/streets`
  - `/regions`
- Frontend renders houses/streets/regions with layer toggles.
- Lightweight grid index integrated for bbox query acceleration.

## In progress / partially implemented
- Region relation handling is intentionally bounded (complex multipolygon completeness deferred).
- Optional GeoJSON export path for debugging/validation not yet added.

## Not implemented yet (major)
- Full reverse geocoding pipeline (Sheet 2).
- Full forward geocoding pipeline and ranking model (Sheet 3).
- Dedicated benchmarking harness for p50/p95 and reproducible evaluation outputs.
- Comprehensive tests for geometry parsing and endpoint correctness.

## Known issues / project risks
- Push hygiene issues can recur if generated files are re-tracked accidentally.
- Very large dataset artifacts must remain untracked to avoid remote push failures.
- HTTP server remains intentionally minimal.
- Relation-heavy region edge cases can still require careful handling in later phases.

## Next milestones (recommended order)
1. Stabilize memory bank maintenance cadence.
2. Add optional GeoJSON debug export + core correctness tests.
3. Implement and validate reverse geocoder (Sheet 2) on new region model foundation.
4. Implement forward geocoder + ranking (Sheet 3).
5. Run profiling/optimization and finalize evaluation artifacts.

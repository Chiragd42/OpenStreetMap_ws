# Project Brief

## Project
OpenStreetMap_ws — a high-performance geocoder and reverse-geocoder built on OpenStreetMap data, currently focused on Stuttgart as the first target region.

## Core Objective
Build an offline, performance-oriented C++ geocoding system that can:
- ingest OSM-derived geospatial data,
- preprocess it into efficient in-memory structures,
- serve map/query endpoints for visualization,
- implement reverse geocoding (Sheet 2),
- implement forward geocoding with ranking (Sheet 3),
- provide measurable runtime and memory evidence for grading/presentation.

## Current Scope (Implemented Foundation)
- C++20 + CMake scaffold is in place.
- **PBF-first ingestion path is active** (`data/pbf/stuttgart-regbez-260416.osm.pbf` default in config).
- In-memory data model includes houses, streets, regions, pooled strings, and grid index metadata.
- BBox query layer uses a lightweight grid index (`cell_id -> object indices`) for retrieval.
- Lightweight HTTP API routes: `/stats`, `/houses`, `/streets`, `/regions`.
- Leaflet frontend renders houses (points), streets (lines), and regions (polygons) with layer toggles.

## Constraints and Preferences
- Project is long-running and should be documented continuously via memory bank files.
- Keep repository lightweight: ignore generated/build artifacts and large raw datasets where possible.
- Prioritize correctness + performance + traceable milestones aligned with roadmap/backlog.

## Success Criteria
1. End-to-end geocoder flows (reverse + forward) are functional.
2. Performance metrics are captured and reportable (latency, memory, extraction timing).
3. Architecture and progress are maintainable through clear memory bank updates.

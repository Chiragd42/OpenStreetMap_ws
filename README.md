# OpenStreetMap_ws

High-performance geocoder and reverse-geocoder project based on OpenStreetMap data.

## Current scaffold

- `data/gpkg/` → active input location for GeoPackage (`.gpkg`) files
- `data/pbf/` → reserved for future direct `.osm.pbf` ingestion
- `docs/ROADMAP.md` → end-to-end milestone roadmap (Sheets 1–3)
- `docs/BACKLOG.md` → implementation backlog with grading-focused priorities
- `src/` and `include/` → C++20 project skeleton

## Active dataset path (current implementation)

The backend currently reads Stuttgart data from:

- `data/gpkg/stuttgart-regbez.gpkg`

This path is wired as the default ingestion input in the extractor config.

## Build (Ubuntu / Linux)

```bash
cmake -S . -B build
cmake --build build -j
./build/osm_geocoder
```

## Run backend API

Start the local in-memory API server:

```bash
./build/osm_geocoder --serve --port=8080
```

Available routes:

- `GET /stats`
- `GET /houses?bbox=minLon,minLat,maxLon,maxLat`
- `GET /streets?bbox=minLon,minLat,maxLon,maxLat`

Example bbox request (Stuttgart center):

```bash
curl "http://127.0.0.1:8080/houses?bbox=9.15,48.75,9.23,48.81"
```

## Run frontend (Leaflet viewer)

In a second terminal, serve the static frontend:

```bash
python3 -m http.server 5500
```

Open:

- `http://127.0.0.1:5500/frontend/index.html`

The frontend fetches viewport data from `http://127.0.0.1:8080` and displays houses (points) + streets (polylines).

## Notes

- Tech baseline: **C++20 + CMake**
- Initial target dataset: **Stuttgart**
- Architecture intent: **offline monolith, in-memory performance-oriented pipeline**
- Current extractor is **GPKG-first** (SQLite-based)
- Streets are read from `gis_osm_roads_free` (lines)
- Building geometries from `gis_osm_buildings_a_free` are reduced to representative points for house visualization

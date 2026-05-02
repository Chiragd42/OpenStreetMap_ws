# OpenStreetMap_ws

High-performance geocoder and reverse-geocoder project based on OpenStreetMap data.

## Sheet 1 status (current)

- **PBF-first ingestion is active** (libosmium streaming parser)
- Data is processed into compact in-memory structures (houses, streets, regions)
- Lightweight HTTP backend serves viewport bbox queries
- Leaflet frontend visualizes points/lines/polygons

## Project structure

- `data/pbf/` → `.osm.pbf` inputs (active)
- `data/gpkg/` → legacy path (currently de-emphasized)
- `docs/ROADMAP.md` → end-to-end milestone roadmap (Sheets 1–3)
- `docs/BACKLOG.md` → implementation backlog with grading-focused priorities
- `src/` and `include/` → C++20 project skeleton

## Active dataset path

Default extractor input:


- `data/pbf/stuttgart-regbez-260416.osm.pbf`

You can override via CLI:

```bash
./build/osm_geocoder --pbf=data/pbf/your-file.osm.pbf
```

## Dependencies

### Ubuntu 22.04 (recommended submission target)

```bash
sudo apt update
sudo apt install -y build-essential cmake zlib1g-dev libbz2-dev libexpat1-dev
```

Install **libosmium + protozero** headers (header-only libs), for example via package manager or vendor setup.

### macOS (Homebrew)

```bash
brew install cmake libosmium protozero
```

## Build (Ubuntu / Linux)

```bash
cmake -S . -B build
cmake --build build -j
./build/osm_geocoder
```

## Demo quick start (short commands)

After cloning and installing dependencies, use:

```bash
make prep
make server
```

In another terminal:

```bash
make ui
```

Open:

- `http://127.0.0.1:5500/frontend/index.html`

What these do:
- `make prep` → builds release binary and preprocesses PBF into cache (`data/cache/stuttgart.bin`)
- `make server` → starts backend from cache (fast startup)
- `make ui` → starts static frontend server

Optional overrides:

```bash
make prep PBF=data/pbf/your-file.osm.pbf CACHE=data/cache/your-file.bin
make server CACHE=data/cache/your-file.bin PORT=8080
make ui UI_PORT=5500
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
- `GET /regions?bbox=minLon,minLat,maxLon,maxLat`

Example bbox request (Stuttgart center):

```bash
curl "http://127.0.0.1:8080/houses?bbox=9.15,48.75,9.23,48.81"
curl "http://127.0.0.1:8080/regions?bbox=9.15,48.75,9.23,48.81"
```

## Run frontend (Leaflet viewer)

In a second terminal, serve the static frontend:

```bash
python3 -m http.server 5500
```

Open:

- `http://127.0.0.1:5500/frontend/index.html`

The frontend fetches viewport data from `http://127.0.0.1:8080` and displays:
- houses (points)
- streets (polylines)
- regions (polygons)

Layer toggles are available in the map panel.

## Notes

- Tech baseline: **C++20 + CMake**
- Initial target dataset: **Stuttgart**
- Architecture intent: **offline monolith, in-memory performance-oriented pipeline**
- Current extractor is **PBF-first** (libosmium streaming)
- Language policy: prefer `name:de`, fallback to `name`
- House representative point policy:
  - address node → direct point
  - addressed building way → centroid (bbox fallback if needed)
- Query path uses a simple **grid-based index** (`cell_id -> object indices`) for bbox retrieval

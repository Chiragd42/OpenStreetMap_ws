# Tech Context

## Language and build stack
- **Language:** C++20
- **Build system:** CMake (minimum 3.16)
- **Primary targets:**
  - `geocoder_core` (library)
  - `osm_geocoder` (executable)

## Core dependencies
- **SQLite3** (required by CMake): used for reading `.gpkg` (GeoPackage) content.
- **POSIX sockets** (`arpa/inet.h`, `sys/socket.h`, etc.) for in-process HTTP server.

## Project structure (current)
- `include/` headers for app/model/metrics/ingest/query/server modules
- `src/` implementation files matching module boundaries
- `frontend/index.html` Leaflet-based visualization entry
- `docs/ROADMAP.md` and `docs/BACKLOG.md` for milestone and task planning
- `data/gpkg/` and `data/pbf/` for dataset inputs/placeholders

## Runtime behavior
- Default input path in extractor config: `data/gpkg/stuttgart-regbez.gpkg`
- CLI options:
  - `--serve`
  - `--port=<port>`
  - `--max-requests=<n>`
- API JSON routes when serving:
  - `/stats`
  - `/houses?bbox=...`
  - `/streets?bbox=...`

## Repository hygiene
- `.gitignore` currently excludes:
  - `build/`
  - `data/gpkg/*.gpkg`
- `.gitkeep` files preserve required directory structure.

## Operational commands
```bash
cmake -S . -B build
cmake --build build -j
./build/osm_geocoder
./build/osm_geocoder --serve --port=8080
python3 -m http.server 5500
```

## Technical debt to track
- Introduce robust tests for geometry parsing and query correctness.
- Replace naive query scans with spatial index structures.
- Harden HTTP handling and error paths for broader usage.

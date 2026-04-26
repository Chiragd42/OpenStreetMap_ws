# Tech Context

## Language and build stack
- **Language:** C++20
- **Build system:** CMake (minimum 3.16)
- **Primary targets:**
  - `geocoder_core` (library)
  - `osm_geocoder` (executable)

## Core dependencies
- **libosmium** (header-only): PBF parsing and OSM object traversal.
- **protozero** (header-only): protobuf foundation used by libosmium.
- **zlib / bzip2 / expat / threads**: supporting libs required by libosmium stack.
- **POSIX sockets** (`arpa/inet.h`, `sys/socket.h`, etc.) for in-process HTTP server.

## Project structure (current)
- `include/` headers for app/model/metrics/ingest/query/server modules
- `src/` implementation files matching module boundaries
- `frontend/index.html` Leaflet-based visualization entry
- `docs/ROADMAP.md` and `docs/BACKLOG.md` for milestone and task planning
- `data/pbf/` active input directory (`stuttgart-regbez-260416.osm.pbf`)
- `data/gpkg/` legacy/de-emphasized path

## Runtime behavior
- Default input path in extractor config: `data/pbf/stuttgart-regbez-260416.osm.pbf`
- CLI options:
  - `--serve`
  - `--port=<port>`
  - `--max-requests=<n>`
  - `--pbf=<path>`
- API JSON routes when serving:
  - `/stats`
  - `/houses?bbox=...`
  - `/streets?bbox=...`
  - `/regions?bbox=...`

## Repository hygiene
- `.gitignore` currently excludes:
  - `build/`
  - large local datasets/artifacts as configured
- `.gitkeep` files preserve required directory structure.

## Operational commands
```bash
cmake -S . -B build
cmake --build build -j
./build/osm_geocoder
./build/osm_geocoder --pbf=data/pbf/stuttgart-regbez-260416.osm.pbf
./build/osm_geocoder --serve --port=8080
python3 -m http.server 5500
```

## Technical debt to track
- Introduce robust tests for geometry parsing and query correctness.
- Add optional GeoJSON export helpers for debugging/validation.
- Harden bounded region relation handling incrementally for Sheet 2 accuracy work.
- Harden HTTP handling and error paths for broader usage.

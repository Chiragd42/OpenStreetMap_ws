# OpenStreetMap Geocoder (Sheet 1)

This project builds a fast in-memory geodata pipeline from OpenStreetMap PBF data.

It includes:
- PBF extraction (houses, streets, regions)
- local HTTP backend with bbox endpoints
- Leaflet frontend map viewer
- optional street merge optimization for cleaner road data

---

## 1) Quick start (easy flow)

### Step 1: open terminal in this project folder

### Step 2: prepare data + build cache
```bash
make prep
```

### Step 3: start backend
```bash
make server
```

### Step 4: start frontend (new terminal)
```bash
make ui
```

### Step 5: open GUI in browser
```text
http://127.0.0.1:5500/frontend/index.html
```

That’s it.

---

## 2) Where to put your PBF file

Put your `.osm.pbf` file in:

```text
data/pbf/
```

Current default file used by short commands:

```text
data/pbf/stuttgart-regbez-260416.osm.pbf
```

---

## 3) What the make commands do

- `make prep`
  - builds release binary
  - parses PBF
  - writes cache file (default: `data/cache/stuttgart.bin`)

- `make server`
  - loads cache file
  - starts backend server (default port 8080)

- `make ui`
  - starts static frontend server (default port 5500)

---

## 4) Backend API routes

When backend is running (`make server`), available routes are:

- `GET /stats`
- `GET /houses?bbox=minLon,minLat,maxLon,maxLat`
- `GET /streets?bbox=minLon,minLat,maxLon,maxLat`
- `GET /regions?bbox=minLon,minLat,maxLon,maxLat`

Example:

```bash
curl "http://127.0.0.1:8080/houses?bbox=9.15,48.75,9.23,48.81"
```

---

## 5) Street merge optimization (optional task)

You can run with merge ON or OFF:

- ON: `--merge-streets`
- OFF: `--no-merge-streets`

If not set, default is ON.

The program prints normal stats first, then a small block:

```text
[StreetMerge]
  enabled: yes/no
  raw_streets: X
  merged_streets: Y
  reduced: Z (% )
  merge_time_ms: T
```

So demo comparison is easy:
- run once with OFF (baseline)
- run once with ON (optimized)

---

## 6) Important limitation note (for presentation)

Street merging is heuristic and connectivity-based.
It reduces fragmentation, but may not perfectly preserve semantic street boundaries in all OSM edge cases.

---

## 7) One clean defense line (for viva/demo)

“We only merge within spatially connected components, so disconnected streets sharing a name are not merged.”

---

## 8) Dependencies

### Ubuntu 22.04
```bash
sudo apt update
sudo apt install -y build-essential cmake zlib1g-dev libbz2-dev libexpat1-dev
```

Also install header-only libs:
- libosmium
- protozero

### macOS (Homebrew)
```bash
brew install cmake libosmium protozero
```

---

## 9) Manual build/run (without make shortcuts)

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
./build-release/osm_geocoder --help
```

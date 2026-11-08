# OpenStreetMap Sheet 1

This is a fresh workspace for implementing Exercise Sheet 1 one checkpoint at
a time. It is intentionally independent from the previous project history.

The complete implementation plan and progress are tracked in
[`CHECKPOINTS.md`](CHECKPOINTS.md).

## Ubuntu 22.04 dependencies

Install the compiler, build tools, libosmium headers, Protozero headers, and
compression/XML libraries:

```sh
sudo apt update
sudo apt install -y \
  build-essential cmake make python3 \
  libosmium2-dev libprotozero-dev \
  zlib1g-dev libbz2-dev libexpat1-dev
```

## Required Sheet 1 commands

Build the optimized executable:

```sh
make release
```

Run normal extraction and print timing, memory, and object statistics:

```sh
make run PBF=data/pbf/region-latest.osm.pbf
```

Run the mandatory linear file test:

```sh
make basicTest \
  PBF=data/pbf/region-latest.osm.pbf \
  INPUT=data/basic-test/input/queries.txt \
  OUTPUT=data/basic-test/output/results.txt
```

All three commands validate their required paths and print a useful error when
an argument is missing. PBF files and generated output files are intentionally
excluded from Git.

Place an OSM PBF extract under `data/pbf/`, for example:

```text
data/pbf/baden-wuerttemberg-latest.osm.pbf
```

Large datasets, generated caches, test outputs, and build directories are not
tracked by Git.

## Optional PBF inspection

Requirements on macOS with Homebrew:

```sh
brew install cmake libosmium protozero
```

Run the streaming inspector:

```sh
./build-release/osm_inspect data/pbf/germany-latest.osm.pbf
```

The inspector does not load the full dataset into memory. It counts relevant
objects and prints a few real examples that help us design the permanent data
structures in the next checkpoint.

## Extraction design

The first extraction version keeps buildings/address points, streets, and
administrative objects in memory. Before resolving geometry, it collects the
node IDs used by relevant building, street, administrative, and relation-member
ways. Only those coordinates enter libosmium's temporary `FlexMem` lookup. The
lookup is discarded when extraction finishes; it is not a persistent cache.

Each input line is matched case-insensitively as a substring of the selected
German/fallback name. The corresponding output line contains original typed
OSM IDs such as `way/455293178, relation/105184`. The implementation performs
the required linear sweep over the extracted object vectors; it does not use a
name index.

The revised Sheet 1 does not require a persistent binary cache. The mandatory
file test always performs the specified linear sweep over the extracted vectors.

The extraction has been completed on the 652,827,459-byte Baden-Württemberg
extract in 31.505 seconds. Full measurements are recorded in
[`docs/checkpoint-10-baden-wuerttemberg-benchmark.md`](docs/checkpoint-10-baden-wuerttemberg-benchmark.md).

## Run the viewport backend

The Sheet 1 backend performs the required naive linear scan over building
representative points and returns at most 10,000 houses from the requested map
viewport:

```sh
make serve PBF=data/pbf/region-latest.osm.pbf PORT=8080
```

Example request:

```text
http://127.0.0.1:8080/houses?bbox=9.10,48.70,9.25,48.82
```

The JSON response reports the exact number of matches, the number returned,
whether the threshold was reached, and the linear scan time. The query logic is
kept separate from HTTP handling so a spatial index can replace the scan later
without changing the frontend API.

## Open the map visualization

Keep the viewport backend running, then serve the static frontend from a second
terminal:

```sh
make frontend FRONTEND_PORT=5500
```

Open `http://127.0.0.1:5500` in a browser. The map starts in Stuttgart and
requests the visible bounding box from the local backend whenever the view
changes. Addressed buildings are red, buildings without an address are gold,
and no more than 10,000 results are drawn for one request. If the limit is
reached, zoom in to request a smaller area.

The frontend loads Leaflet and OpenStreetMap map tiles from their public web
services, so the browser needs an internet connection for the base map. The
extracted building data continues to come only from the local Sheet 1 backend.

Administrative boundary relations are assembled with libosmium's multipolygon
assembler. Outer and inner rings are retained in flat point/ring arrays, and
incomplete relations remain visible in the statistics instead of being silently
dropped. The representation and verification are documented in
[`docs/checkpoint-07-administrative-areas.md`](docs/checkpoint-07-administrative-areas.md).

The measured Germany results and their storage implications are documented in
[`docs/checkpoint-02-osm-inspection.md`](docs/checkpoint-02-osm-inspection.md).

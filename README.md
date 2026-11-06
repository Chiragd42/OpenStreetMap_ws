# OpenStreetMap Sheet 1

This is a fresh workspace for implementing Exercise Sheet 1 one checkpoint at
a time. It is intentionally independent from the previous project history.

The complete implementation plan and progress are tracked in
[`CHECKPOINTS.md`](CHECKPOINTS.md).

Place the Germany PBF at:

```text
data/pbf/germany-latest.osm.pbf
```

Large datasets, generated caches, test outputs, and build directories are not
tracked by Git.

## Checkpoint 2: inspect the Germany PBF

Requirements on macOS with Homebrew:

```sh
brew install cmake libosmium protozero
```

Configure and build:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
```

Run the streaming inspector:

```sh
./build-release/osm_inspect data/pbf/germany-latest.osm.pbf
```

The inspector does not load the full dataset into memory. It counts relevant
objects and prints a few real examples that help us design the permanent data
structures in the next checkpoint.

## Extract and run the mandatory basic file test

The first extraction version keeps buildings/address points, streets, and
administrative objects in memory. It uses libosmium's temporary, disk-backed
`SparseFileArray` to resolve the node references in ways without requiring the
complete Germany node lookup to fit in RAM. The temporary file is not a
persistent application cache.

```sh
./build-release/osm_sheet1 \
  --pbf data/pbf/germany-latest.osm.pbf \
  --basicTest data/basic-test/input/queries.txt \
  data/basic-test/output/results.txt
```

Each input line is matched case-insensitively as a substring of the selected
German/fallback name. The corresponding output line contains original typed
OSM IDs such as `way/455293178, relation/105184`. The implementation performs
the required linear sweep over the extracted object vectors; it does not use a
name index.

The binary cache and map frontend are deliberately not part of this checkpoint.

## Run the viewport backend

The Sheet 1 backend performs the required naive linear scan over building
representative points and returns at most 10,000 houses from the requested map
viewport:

```sh
./build-release/osm_sheet1 \
  --pbf data/pbf/germany-latest.osm.pbf \
  --serve --port 8080
```

Example request:

```text
http://127.0.0.1:8080/houses?bbox=9.10,48.70,9.25,48.82
```

The JSON response reports the exact number of matches, the number returned,
whether the threshold was reached, and the linear scan time. The query logic is
kept separate from HTTP handling so a spatial index can replace the scan later
without changing the frontend API.

Administrative boundary relations are assembled with libosmium's multipolygon
assembler. Outer and inner rings are retained in flat point/ring arrays, and
incomplete relations remain visible in the statistics instead of being silently
dropped. The representation and verification are documented in
[`docs/checkpoint-07-administrative-areas.md`](docs/checkpoint-07-administrative-areas.md).

The measured Germany results and their storage implications are documented in
[`docs/checkpoint-02-osm-inspection.md`](docs/checkpoint-02-osm-inspection.md).

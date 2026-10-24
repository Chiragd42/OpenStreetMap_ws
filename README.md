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

The measured Germany results and their storage implications are documented in
[`docs/checkpoint-02-osm-inspection.md`](docs/checkpoint-02-osm-inspection.md).

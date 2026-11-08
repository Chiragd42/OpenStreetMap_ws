# Local data

Use this directory for local input and generated data:

```text
data/
├── pbf/                 Original OSM PBF inputs
├── cache/               Generated binary caches
└── basic-test/
    ├── input/           Query files
    └── output/          Generated result files
```

Place the selected regional dataset at, for example:

```text
data/pbf/baden-wuerttemberg-latest.osm.pbf
```

PBF files, binary caches, and generated basic-test outputs are ignored by Git.

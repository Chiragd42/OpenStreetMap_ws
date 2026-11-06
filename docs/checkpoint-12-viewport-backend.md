# Checkpoint 12: viewport backend

The backend exposes the mandatory naive Sheet 1 viewport routine:

```text
GET /houses?bbox=minLongitude,minLatitude,maxLongitude,maxLatitude
```

For each request it linearly scans the extracted building vector and tests the
stored representative point against the inclusive viewport bounds. Buildings
without a representative point are ignored. The response returns at most
10,000 buildings and reports the exact matched count, returned count, whether
the threshold was reached, and scan time.

Each returned building contains its typed OSM ID, coordinate, and available
name, building type, street, house number, postcode, and city. Missing metadata
is represented as JSON `null` rather than an invented value.

The implementation takes the useful API shape from the previous project but
deliberately postpones its spatial grid and binary cache. The query function is
separate from HTTP handling, so a later spatial index can replace the linear
scan without changing the frontend API.

Run the backend with:

```sh
./build-release/osm_sheet1 \
  --pbf data/pbf/germany-latest.osm.pbf \
  --serve --port 8080
```

The server binds only to `127.0.0.1` and permits cross-origin requests from the
local frontend.

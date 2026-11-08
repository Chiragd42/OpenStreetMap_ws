# Checkpoint 13: map visualization

## Purpose

The browser frontend visualizes the buildings returned by the mandatory
viewport backend. It deliberately uses the same simple API that a later spatial
index can serve, so improving the query implementation will not require a new
frontend.

## Behavior

- The initial map view is Stuttgart, Germany.
- Moving or zooming the map sends the current WGS 84 bounding box to
  `GET /houses`.
- Every returned building with a representative point is drawn as a circle.
- Red circles identify buildings with address information; gold circles identify
  buildings without address information.
- Selecting a circle displays its typed OSM ID and available metadata.
- The panel reports returned and matched counts, backend scan time, browser
  round-trip time, and backend connection state.
- A clear warning asks the user to zoom in when the backend's 10,000-house
  response threshold is reached.
- A new viewport request cancels the previous request, and large responses are
  drawn in batches to keep the page responsive.

## Running it

Start the backend after extraction:

```sh
./build-release/osm_sheet1 \
  --pbf data/pbf/germany-latest.osm.pbf \
  --serve --port 8080
```

Serve the frontend from another terminal:

```sh
python3 -m http.server 5500 --bind 127.0.0.1 --directory frontend
```

Then open `http://127.0.0.1:5500`.

## Verification

The frontend was loaded in a desktop browser against the real backend using the
small building-relation fixture. The browser sent the viewport bounding box,
the backend matched and returned the expected building, and the interface drew
one marker with the same returned and matched counts. A broad fixture query also
returned one addressed and two unaddressed buildings with their original typed
OSM IDs. The JavaScript syntax, release build, and project test suite were
checked again before the checkpoint was committed.

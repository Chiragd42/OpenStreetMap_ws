# System Patterns

## High-level architecture
Current architecture is an offline-first monolith with clear module boundaries:

1. **Ingestion layer** (`ingest/pbf_extractor`)
   - Reads `.osm.pbf` via libosmium in streaming mode.
   - Filters relevant entities during parse (`addr:*`, `highway`, `boundary=administrative`).
   - Converts directly into internal model (no retention of raw OSM objects).
   - Tracks extraction quality counters (house derivation paths, skipped complex relations, unnamed streets).

2. **Core model layer** (`model`)
   - Stores compact in-memory structures:
     - `HousePoint`
     - `StreetPolyline` (+ shared `street_points`, bbox, unnamed flag)
     - `RegionPolygon` (+ shared `region_points`, bbox, admin level)
      - pooled strings via `StringPool`
      - grid index maps (`cell_id -> object indices`)
   - Provides `BBox` helper + grid-cell helpers for spatial filtering.

3. **Query layer** (`query/bbox_query`)
   - Performs bbox queries through fixed-grid index buckets.
   - De-duplicates candidate indices and confirms bbox overlap.
   - Returns index vectors to avoid immediate data copying.

4. **Serialization layer** (`query/json`)
   - Parses bbox query params.
   - Serializes stats/houses/streets/regions/error payloads into JSON.

5. **Transport layer** (`server/http_server`)
   - Minimal socket-based HTTP server.
   - Routes: `/stats`, `/houses`, `/streets`, `/regions`.
   - CORS enabled (`Access-Control-Allow-Origin: *`).

6. **Application orchestration** (`app`, `main`)
   - CLI parsing (`--serve`, `--port`, `--max-requests`, `--pbf=<path>`).
   - Runs extraction and optional HTTP serving.

## Key design choices
- **String interning** to reduce duplicated textual memory.
- **Append-only vectors** for cache-friendly storage.
- **Fixed-grid spatial indexing** for low-complexity query acceleration.
- **Language normalization rule:** prefer `name:de`, fallback `name`.
- **Deterministic house representative point policy:** address node direct; addressed building polygon centroid with bbox fallback.
- **Explicit metrics capture** (`ParseStats`, `Stopwatch`) for grading evidence.

## Known architectural limitations (current)
- HTTP parser is intentionally minimal and not production hardening-focused.
- Region relation handling is intentionally bounded for Sheet 1 (complex multipolygon completeness deferred).
- Forward/reverse geocoding pipelines not yet fully implemented.

## Expected evolution path
1. Harden region geometry handling as needed for Sheet 2 point-in-polygon accuracy.
2. Implement robust reverse geocoder pipeline (Sheet 2).
3. Add forward query tokenizer/index/ranker (Sheet 3).
4. Extend metrics + profiling-driven optimization loops.

# System Patterns

## High-level architecture
Current architecture is an offline-first monolith with clear module boundaries:

1. **Ingestion layer** (`ingest/gpkg_extractor`)
   - Reads GeoPackage via SQLite.
   - Parses GeoPackage geometry blobs into WKB views.
   - Decodes line/polygon geometries into internal points.

2. **Core model layer** (`model`)
   - Stores compact in-memory structures:
     - `HousePoint`
     - `StreetPolyline` (+ shared `street_points`)
     - pooled strings via `StringPool`
   - Provides `BBox` helper for spatial filtering.

3. **Query layer** (`query/bbox_query`)
   - Performs naive linear scans for houses/streets in bbox.
   - Returns index vectors to avoid immediate data copying.

4. **Serialization layer** (`query/json`)
   - Parses bbox query params.
   - Serializes stats/houses/streets/error payloads into JSON.

5. **Transport layer** (`server/http_server`)
   - Minimal socket-based HTTP server.
   - Routes: `/stats`, `/houses`, `/streets`.
   - CORS enabled (`Access-Control-Allow-Origin: *`).

6. **Application orchestration** (`app`, `main`)
   - CLI parsing (`--serve`, `--port`, `--max-requests`).
   - Runs extraction and optional HTTP serving.

## Key design choices
- **String interning** to reduce duplicated textual memory.
- **Append-only vectors** for cache-friendly storage.
- **Simple query strategy first** (linear scan) before adding spatial indexes.
- **Explicit metrics capture** (`ParseStats`, `Stopwatch`) for grading evidence.

## Known architectural limitations (current)
- No spatial index yet (R-tree/grid) for fast bbox lookups.
- HTTP parser is intentionally minimal and not production hardening-focused.
- Boundary extraction currently count-oriented and not yet integrated into query API.
- Forward/reverse geocoding pipelines not yet fully implemented.

## Expected evolution path
1. Introduce spatial indexing for houses/streets/boundaries.
2. Implement robust reverse geocoder pipeline (Sheet 2).
3. Add forward query tokenizer/index/ranker (Sheet 3).
4. Extend metrics + profiling-driven optimization loops.

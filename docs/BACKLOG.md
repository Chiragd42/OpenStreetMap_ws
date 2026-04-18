# Implementation Backlog (Grading-Focused)

## Epic A — Infrastructure & Build

### A1. Project skeleton
**Acceptance criteria**
- CMake config builds on Ubuntu with C++20
- `osm_geocoder` executable runs

### A2. Third-party dependencies
**Acceptance criteria**
- libosmium integrated via documented setup
- Build instructions include dependency install commands

## Epic B — OSM Extraction (Sheet 1 critical)

### B1. Tag-driven entity extraction
**Acceptance criteria**
- Extract nodes/ways/relations relevant to address, street, boundary semantics
- Counters emitted for each extracted entity category

### B2. House representation unification
**Acceptance criteria**
- Address nodes mapped directly
- Building polygons mapped to centroids
- Unified `HouseCandidate` model produced

### B3. Street and boundary models
**Acceptance criteria**
- Street ways with `highway` + optional `name` retained
- Administrative boundaries parsed with level and bbox

## Epic C — Preprocessing & Performance Evidence

### C1. Normalization pipeline
**Acceptance criteria**
- Canonicalized strings (case, whitespace, punctuation handling)
- IDs and references compacted for efficient storage

### C2. Metrics instrumentation
**Acceptance criteria**
- Extraction/preprocessing timing report
- Memory usage snapshots
- Persisted run summary for evaluation

## Epic D — Visualization (Sheet 1)

### D1. Backend endpoints for viewport queries
**Acceptance criteria**
- Endpoint returns houses/streets/boundaries in bbox
- Payload sizes bounded and paginated if needed

### D2. Leaflet integration
**Acceptance criteria**
- Layer toggles for houses/streets/boundaries
- Stable rendering over Stuttgart dataset

## Epic E — Reverse Geocoding (Sheet 2 critical)

### E1. Point-in-polygon core
**Acceptance criteria**
- Correct containment results on curated polygon tests
- Bbox prefilter integrated for speed

### E2. Reverse geocoder pipeline
**Acceptance criteria**
- `(lat, lon)` returns best structured address candidate
- Includes confidence and matched components

### E3. Reverse benchmark suite
**Acceptance criteria**
- Stuttgart test coordinates with expected outputs
- p50/p95 latency measured

## Epic F — Forward Geocoding (Sheet 3 critical)

### F1. Query parser + tokenizer
**Acceptance criteria**
- Parses common patterns: street + housenumber + city/postcode
- Handles partial and noisy input safely

### F2. Candidate index
**Acceptance criteria**
- Inverted index maps tokens to candidate sets
- Query retrieval avoids full scan

### F3. Ranking model
**Acceptance criteria**
- Score combines token match, field alignment, locality consistency
- Top-k results deterministic for identical input

### F4. Forward benchmark suite
**Acceptance criteria**
- Query corpus for Stuttgart addresses
- Accuracy + latency metrics generated

## Epic G — Final Hardening & Presentation

### G1. Profiling and optimization
**Acceptance criteria**
- Identify top hotspots via profiler output
- Apply targeted optimizations with before/after numbers

### G2. Final report materials
**Acceptance criteria**
- Complexity + design rationale documented
- Performance tables + demo script prepared

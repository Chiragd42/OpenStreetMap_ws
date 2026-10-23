# Sheet 1 checkpoints

We complete these checkpoints in order. A checkpoint is marked complete only
after its approach is understood, implemented, tested, and committed.

Status legend:

- `[ ]` Not started
- `[~]` In progress
- `[x]` Completed and verified

## Mandatory foundation

### 1. Set up the fresh branch and local data layout

- Status: `[x]`
- Intention: Use a standalone `sheet1` branch containing only the new Sheet 1
  workspace, with safe locations for PBF inputs, caches, and test files.
- Verification: Confirm that no old project source files or history are
  inherited and that large local data files are ignored by Git.

### 2. Understand and inspect OSM data

- Status: `[ ]`
- Intention: Inspect real nodes, ways, relations, tags, IDs, and references in
  the Germany PBF before writing the extractor.
- Verification: Explain how a building, street, and administrative boundary
  are represented in OSM.

### 3. Define the extracted object data structures

- Status: `[ ]`
- Intention: Decide which IDs, names, address fields, geometry, and offsets are
  required for buildings, streets, and administrative areas.
- Verification: Review the structures for correctness, memory use, and the
  required linear file test.

### 4. Parse nodes and resolve way-node coordinates

- Status: `[ ]`
- Intention: Use libosmium to parse the PBF and map node references in ways to
  geographic coordinates.
- Verification: Resolve and inspect known way geometries from a small extract.

### 5. Extract every required building

- Status: `[ ]`
- Intention: Extract all relevant `building=*` objects, including buildings
  without address attributes, preserve typed OSM IDs, and calculate a
  representative point.
- Verification: Confirm that addressed and unaddressed buildings are stored.

### 6. Extract original street segments

- Status: `[ ]`
- Intention: Extract relevant `highway=*` ways with original IDs, selected
  names, road classes, bounding boxes, and geometry before optional merging.
- Verification: Inspect named and unnamed streets and their original way IDs.

### 7. Extract administrative areas

- Status: `[ ]`
- Intention: Extract administrative ways and relations with names, IDs,
  levels, outer rings, and inner rings.
- Verification: Validate representative German boundaries and report
  incomplete relations.

### 8. Apply one consistent language policy

- Status: `[ ]`
- Intention: Use one shared rule for selecting a German object name, with a
  documented fallback when `name:de` is unavailable.
- Verification: Apply the same function to every extracted object type.

### 9. Implement the linear `--basicTest` mode

- Status: `[ ]`
- Intention: Read queries from a file, linearly scan all original extracted
  object names, and write matching typed OSM IDs to an output file.
- Verification: Test substring matches, no matches, empty input, multiple
  matches, and different OSM object types without using a search index.

### 10. Record timings, counts, and memory statistics

- Status: `[ ]`
- Intention: Report processing time, object counts, skipped objects, cache
  timings, and meaningful memory measurements.
- Verification: Produce a concise statistics report for a complete run.

### 11. Implement and validate the binary cache

- Status: `[ ]`
- Intention: Store extracted records in a versioned binary cache so later runs
  do not need to parse the PBF again.
- Verification: Confirm equivalent data after cache loading and reject
  incompatible cache versions.

### 12. Build the viewport backend and spatial index

- Status: `[ ]`
- Intention: Retrieve objects inside a map viewport without scanning all of
  Germany for every request.
- Verification: Compare indexed results with a linear reference on test data.

### 13. Complete the mandatory map visualization

- Status: `[ ]`
- Intention: Display extracted buildings in the current viewport up to a safe
  threshold through a frontend and server backend.
- Verification: Confirm that addressed and unaddressed buildings appear while
  panning and zooming.

## Optional tasks

### 14. Connect compatible street segments

- Status: `[ ]`
- Intention: After the mandatory file test, combine connected street ways with
  the same selected name and retain the lowest contributing OSM ID.
- Verification: Test chains, reversed segments, branches, disconnected roads,
  and same-name roads in different locations.

### 15. Add missing object information

- Status: `[ ]`
- Intention: Add defensible context such as administrative containment or
  inherited addresses without inventing uncertain data.
- Verification: Document and test every enrichment rule.

## Final validation and submission

### 16. Validate the complete Germany dataset

- Status: `[ ]`
- Intention: Run the complete pipeline on Germany after validating it on
  smaller extracts.
- Verification: Record runtime, peak memory, cache size, extracted counts,
  skipped objects, and test results.

### 17. Complete Ubuntu 22.04 submission documentation

- Status: `[ ]`
- Intention: Document dependency installation, compilation, extraction,
  caching, basic testing, server startup, and frontend usage.
- Verification: Follow the instructions in a clean Ubuntu 22.04 environment
  or equivalent container.

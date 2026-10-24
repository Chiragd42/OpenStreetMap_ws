# Checkpoint 2: Germany PBF inspection

This checkpoint inspects the real input before we choose permanent in-memory
structures. The inspector streams through the PBF, so it does not retain all
OSM objects.

## Pseudocode

```text
for every object in the PBF:
    count whether it is a node, way, or relation
    inspect only tags relevant to buildings, streets, and boundaries
    count geometry references and relation member roles
    retain only a few human-readable examples

print counts, examples, elapsed time, CPU time, and peak process memory
```

## Reproduce the scan

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
./build-release/osm_inspect data/pbf/germany-latest.osm.pbf
```

## Measured local dataset

Input size: 4.53 GiB

| Category | Count |
|---|---:|
| Nodes | 441,550,388 |
| Ways | 71,352,134 |
| Relations | 913,191 |
| Invalid node locations | 0 |
| Address-only nodes | 4,084,057 |
| Building-tagged nodes | 19,494 |
| Building ways | 39,408,626 |
| Building relations | 28,112 |
| Building-way node references | 265,469,710 |
| Buildings without address tags | 22,579,643 |
| Buildings without a selected name | 38,838,713 |
| Highway ways | 16,358,731 |
| Named highway ways | 4,105,412 |
| Unnamed highway ways | 12,253,319 |
| Street node references | 108,954,191 |
| Administrative ways | 89,719 |
| Administrative-way node references | 4,925,952 |
| Administrative relations | 35,310 |
| Administrative outer members | 414,909 |
| Administrative inner members | 4,047 |

The first uninterrupted scan completed in about 80 seconds with a peak process
memory measurement of about 229 MiB. A later run crossed a host sleep/suspend
period, so its wall-clock value was deliberately not used as a benchmark. The
program reports CPU time separately to make such cases visible.

## What the samples demonstrate

- A house address can be stored directly on a node. Such a node already has a
  coordinate and does not need way geometry.
- A building is usually a closed way. Most building ways have no name, and more
  than 22 million building objects have no address tags, but the sheet requires
  us to keep them.
- A street is a way with ordered node references. Its `highway` tag describes
  its class, and its selected name may be missing.
- An administrative boundary is commonly a relation. Member ways use roles such
  as `outer` and `inner`; ignoring inner members produces incorrect polygons.
- A regional extract can include objects just outside the target country when
  they are needed to complete relations or features near the extract boundary.

## Storage consequences

The scan gives an upper-bound candidate count of 43,540,289 building/address
records before relation-member deduplication:

```text
4,084,057 address-only nodes
+ 19,494 building nodes
+ 39,408,626 building ways
+ 28,112 building relations
```

At 32 bytes per compact record, these records alone need about 1.30 GiB.

Building ways contain 265,469,710 node references. Storing one 8-byte coordinate
for every reference would need about 1.98 GiB, before vector and object overhead.
This supports reducing each building to one representative point and retaining
full building geometry only as an optional visualization feature.

If every `highway=*` way is retained, 32-byte street records require about
499 MiB and their 8-byte coordinates require about 831 MiB. Most highway ways
are unnamed, and the largest classes include `service`, `track`, `footway`, and
`path`. Checkpoint 3 must therefore define explicitly what counts as a street;
we should not silently treat every `highway=*` object as equivalent.

## Decision carried into Checkpoint 3

Permanent storage should use typed OSM IDs, interned strings, compact fixed-point
coordinates, flat geometry arrays, and offsets/counts. The temporary
node-ID-to-coordinate index used to resolve way geometry must be measured and
discarded after extraction. Spatial query indexes and street merging should be
added only after the required linear file test works with original OSM IDs.

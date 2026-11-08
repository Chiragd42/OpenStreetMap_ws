# Checkpoint 10: Baden-Württemberg benchmark

## Input

- Geofabrik extract: `baden-wuerttemberg-261107.osm.pbf`
- File size: 652,827,459 bytes
- Build: Release
- Command: `make run PBF=data/pbf/baden-wuerttemberg-261107.osm.pbf`

## Completed result

| Measurement | Value |
| --- | ---: |
| Extraction wall time | 31.505 seconds |
| Extraction CPU time | 43.685 seconds |
| Peak process memory | 1,068.016 MiB |
| Temporary node lookup estimate | 1,189.959 MiB |
| Estimated stored data | 908.501 MiB |
| Processed nodes | 57,103,879 |
| Processed ways | 9,320,275 |
| Processed relations | 154,479 |
| Buildings/address points | 5,309,163 |
| Streets | 2,408,324 |
| Administrative areas | 5,106 |
| Invalid way-node locations | 0 |

The run completed the full extraction and printed all mandatory timing and
statistics fields. It is faster than the revised sheet's approximate one-minute
reference for Baden-Württemberg.

The temporary lookup figure is a conservative capacity estimate that includes
the required-node and relation-way ID vectors. Peak process memory is the
operating system's measured resident-process peak, so the two values are not
expected to be identical.

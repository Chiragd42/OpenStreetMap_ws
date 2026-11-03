# Checkpoint 5: building extraction

The extractor stores every object tagged with `building=*`, even when it has
no name or address. Address-tagged nodes are also retained as point records.
Every stored ID includes its OSM object type, so a node, way, and relation with
the same numeric ID remain distinct.

## Representative points

- A building node uses its own coordinate.
- A closed building way uses its polygon centroid. Open or degenerate ways use
  the centre of their bounding box.
- A `type=multipolygon` building relation is assembled from its member ways.
  Its point is calculated from the largest assembled outer ring.
- Other building relations, including `type=building`, use the largest closed
  member way. If their complete geometry is split across open member ways, the
  member-geometry bounding-box centre is used as a fallback.
- An incomplete relation is still stored with its tags and typed ID, but it is
  counted as a building without a representative point.

Building relations have a dedicated relation manager. It tracks only member
ways needed by building relations; it does not assemble every ordinary
building way a second time. This avoids adding multipolygon-assembly overhead
to the roughly 39 million building ways in the Germany dataset.

## Verification

`tests/fixtures/building-relations.osm` covers:

- a multipolygon building split across two outer ways;
- a `type=building` relation with an outline way;
- an incomplete building relation;
- an unnamed and unaddressed building way;
- German-name selection and address retention; and
- exact building and missing-point statistics without duplicate relation
  records.

The fixture passes in both the Release and UndefinedBehaviorSanitizer builds.

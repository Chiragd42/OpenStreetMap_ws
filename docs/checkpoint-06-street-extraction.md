# Checkpoint 6: original street extraction

Every usable `highway=*` way is stored as its original OSM way. Street merging
is deliberately postponed until after the mandatory linear file test.

Each street record contains:

- its typed original OSM ID;
- the selected `name:de`, falling back to `name`;
- the `highway` class;
- a bounding box; and
- an offset and count into the flat street-point vector.

Unnamed streets remain in the street vector with an invalid name ID. They can
therefore be visualized and used by later geographic queries without appearing
in name-search results.

The implementation was verified locally with named and unnamed street samples.
The checks covered original IDs, German-name selection, fallback-name
selection, road classes, ordered geometry points, flat-vector offsets, and
bounding boxes.

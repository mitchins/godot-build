# Synthetic atlas acceptance fixture (M4 slice 4)

All files in this directory are **generated, original content** (fixed
formulas, no extracted bytes). Regenerate with:

    python3 fixtures/atlas/generate.py

`generate.py` is the spec: distinctive per-tile index formulas (asymmetric
in x/y so transposition bugs are loud), a gap range, empty tiles, an
animation anchor, shade tables, lookup swaps, and an alternate palette.
The C++ unit suite, the fbtool contract gate, and the Godot
consumer-boundary test all consume these bytes.

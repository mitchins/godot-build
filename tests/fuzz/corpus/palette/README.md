# Fuzz corpus: palette

The files in this directory are **generated, layout-matched, synthetic** —
built by `tests/unit/palette.test.cpp`-style builders (counting patterns over
6-bit components) and a one-off generator script. They are NOT extracted from
any game data.

`valid_palette.bin` (82,690 bytes) and `valid_lookup.bin` (10,266 bytes)
deliberately match the *layout arithmetic* of commercial PALETTE.DAT /
LOOKUP.DAT (including 32 declared + 32 extra shade tables, D0013) because
that is exactly what a seed corpus is for. Byte-level comparison against real
content shows ~0.7% positional coincidence — independent generated data.

No proprietary bytes, hashes, or derived values are committed (PROVENANCE
rule 3). `ci/check_corpus.py` and `tests/fuzz/fuzz_main.cpp` skip `README*`
by name so this note never runs as a fuzz input.

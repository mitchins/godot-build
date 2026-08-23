# Compatibility scope

## In scope (format compatibility only)

Initial dialect: the data layout needed to open a Duke shareware-era MAP v7 world.

- directory and GRP-mounted files;
- MAP v7 (read, validate, byte-preserving and canonical write);
- sequential ART tile archives;
- base palette;
- palette lookups/shade data required for recognizable presentation;
- tile animation and pivot metadata present in the asset format.

Tile numbers and tags carry **no** Duke meaning inside `fauxbuild_core`. `lotag`, `hitag`,
and `extra` are preserved as raw data only.

## Out of scope

- CON scripting, effectors, or any Duke game semantics;
- Blood, Shadow Warrior, Exhumed, or other Build-engine dialects;
- Duke save-file or demo compatibility;
- network play;
- exact Build bugs (unless they materially affect maps or player feel);
- frame-exact software-renderer output;
- mirrors, voxels, HRP, Polymer-style lighting, or source-port extensions unless the original
  game later requires them.

## Freeze rule

After the M12 engine conformance tag `fauxbuild-core-v0.1`, any further compatibility work
requires a demonstrated need from the original game, recorded as a decision in
`DECISIONS.md`.

## Known incompatibilities

Incompatibilities discovered so far are listed below. Every entry must be bounded
explicitly — what is rejected, why, and what would change it — before the M12 gate.

| # | Item | Status | Bound |
|---|---|---|---|
| 0 | MAP v7 layout is int32 version==7 (no string signature), int16 sprite count, section sizes 40/32/44 bytes, no trailing data | Locked by black-box verification of untouched E1L1.MAP (2026-08-23); section arithmetic is exact | Verified in-repo; no external code consulted. Any future map that disagrees reopens this row. |
| 1 | GRP containers declaring more than 65,536 files are rejected | Accepted, bounded (D0011) | Parser resource limit `kMaxEntryCount` (D0011), not a format claim. Real archives hold low thousands of files. Raising it is a decision record. |

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

None recorded yet. This table is populated as they are discovered and must be bounded
explicitly before the M12 gate.

| # | Item | Status | Bound |
|---|---|---|---|
| 1 | GRP containers declaring more than 65,536 files are rejected | Accepted, bounded | Parser resource limit `kMaxEntryCount` (D0011), not a format claim. Real archives hold low thousands of files. Raising it is a decision record. |

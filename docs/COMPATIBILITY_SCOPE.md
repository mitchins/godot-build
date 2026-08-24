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
| 0 | MAP v7 layout is int32 version==7 (no string signature), uint16 record counts, section sizes 40/32/44 bytes, no trailing data | Locked by black-box verification of six legally owned maps (2026-08-23); section arithmetic is exact on all six, and Mapster32 r9598 re-saved our fixture as v7 with no trailer | Verified in-repo; published format description recorded as PROVENANCE row 9; no engine or source-port code consulted. Any future map that disagrees reopens this row. |
| 0b | PALETTE.DAT: the declared int16 table count (32 in real content) covers only the palette-0 shade ramp; 32 further 256-byte tables follow before the translucency table, undeclared in-band and implied by total size. The published size equation is therefore incomplete for real files | Accepted, bounded (D0013, accepted) | FauxBuild preserves the extra tables verbatim (byte-identical round-trip). Property evidence: tables 0-31 form a monotone ramp (identity 255/256 at t0, collapsing distinct values), table 32 breaks the pattern; 768+2+64*256+65536 = 82,690 exactly. No interpretation of the extra tables until a demonstrated need. BOUND: because the table region is sized from the file, truncation of up to (tables − declared) × 256 bytes is undetectable and silently shifts the translucency window; GRP-mounted content is immune (container validates entry lengths) — prefer mount-based reads for loose files of unknown provenance (D0013 consequence) |
| 0c | LOOKUP.DAT closes exactly as 1 + swaps*257 + alt_palettes*768 (real content: 25 swaps with indices a permutation of 1..25, 5 six-bit alt palettes) | Locked by black-box verification | Structural only; swap semantics (which palette index does what) are game-layer, never core |
| 0d | ART: version==1; the numtiles header field has no established meaning (published description says unused; observation shows only that it is not the per-file count, and 2816 in every one of the 13 shipped files while ranges reach 3327 rules out both bounds readings — see D0015). Range end-start+1 is authoritative; ranges chain contiguously (0..255/256..511/512..767); closure 16 + n*8 + sum(w*h) is exact; picanm bit layout (type 7-6, signed centers 15-8/23-16) corroborated structurally (type distribution dominated by 0, small signed centers); frames/speed widths are PROVENANCE-sourced, NOT observation-corroborated: real content maxes frames at 15, which satisfies both a 4-bit and 6-bit mask, so the width claim is unverifiable black-box | Locked by black-box verification of three legally owned TILES###.ART | Bit-layout semantics beyond structure (animation behavior, speed tick meaning) unverified until a consumer exists. Pixel ORDERING (column-major per the published description) cannot be distinguished by size arithmetic: pixels are stored verbatim in file order with zero conversion; interpretation belongs to the presentation boundary |
| 0e | Atlas composition over the shipped GRP (13 ART files): declared ranges chain contiguously 0..255 through 3072..3327 with no overlaps; the numtiles field is 2816 in every file while range ends reach 3327, so numtiles is NOT a per-file bound and NOT the namespace size (3328); 1605 of 3328 picnums are populated, 1723 are zero-dimension tiles, 0 are unclaimed gaps; palette/lookup load through the same mount (32 declared shades, 5 alt palettes, 25 swaps) | Locked by black-box verification (inspect-atlas first run, 2026-08-24) | The published description calls numtiles unused and derives the namespace from localtilestart/localtileend (PROVENANCE 10). Evidence here establishes only that numtiles is not an upper bound; nothing establishes it as a lower bound either, so the atlas ignores it entirely and preserves it raw (D0015 rule 2, amended). An intermediate implementation treated it as a namespace floor -- an invented semantic that never changed the answer on real content (2816 < 3328) while letting a 24-byte file size a 2e9-entry namespace. Any future content where max(end+1) is not the namespace reopens this row. |
| 1 | GRP containers declaring more than 65,536 files are rejected | Accepted, bounded (D0011) | Parser resource limit `kMaxEntryCount` (D0011), not a format claim. Real archives hold low thousands of files. Raising it is a decision record. |

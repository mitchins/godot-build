# Provenance

Engineering provenance policy for FauxBuild. This is not a substitute for commercial legal
advice.

## Forbidden inputs

Coding agents and contributors must not copy, translate, or adapt code from:

- Ken Silverman's Build source;
- EDuke32;
- JFBuild / JFDuke;
- Chocolate Duke;
- any game source release;
- decompilations or disassemblies;
- leaked or proprietary source;
- code snippets whose provenance cannot be established.

Do not ask an agent to "look at how EDuke32 implements clipmove."

## Approved inputs

- published binary-format descriptions;
- Godot's official documentation and APIs;
- general computational geometry references;
- original synthetic maps and assets;
- black-box observation of legally owned binaries/content;
- measurements and traces produced by our own runtime;
- permissively licensed general-purpose libraries after review;
- our own design decisions.

## Proprietary local stress data

`local_reference/` is gitignored. No proprietary map, tile, palette, audio, screenshot,
hash-derived asset cache, or extracted content enters CI or release artifacts. The runtime may
load that directory in a developer build only. Release builds of the original game must not
expose Duke-specific UI or assumptions.

## Provenance log

For every external technical reference or dependency record: title, owner/project, license or
usage classification, what was learned or imported, whether code was copied (must be `no` for
compatibility references), reviewer, date.

| # | Title | Owner/Project | License | Learned/Imported | Code copied | Reviewer | Date |
|---|---|---|---|---|---|---|---|
| 1 | doctest v2.4.11 (`third_party/doctest/doctest.h`) | Viktor Kirilov / doctest | MIT | Vendored single-header test framework for `fauxbuild_tests` | yes (verbatim, permitted: permissive license) | mitchellcurrie | 2026-08-21 |
| 2 | Godot 4.7.2 stable (editor binary, `/Applications/Godot.app`) | Godot Engine project | MIT | Pinned engine binary for M1+ integration; not linked yet | no | mitchellcurrie | 2026-08-21 |
| 3 | SCons 4.10.1 (system install via Homebrew) | SCons Foundation | MIT | Build system driving all native targets; invoked as a tool, not linked | no | mitchellcurrie | 2026-08-21 |
| 4 | Apple clang 21.0.0 / LLVM libc++ (Xcode 26.6 toolchain) | Apple / LLVM | Apache-2.0 with LLVM exception | Compiler and C++ standard library for all native targets | no | mitchellcurrie | 2026-08-21 |
| 5 | godot-cpp @ `9c8aeff0f58ad030f3d1030e8262de1322cd0ccd` (submodule `third_party/godot-cpp`, built `api_version=4.7`) | Godot Engine project | MIT | GDExtension C++ bindings; links the extension layer against Godot 4.7 APIs | yes (verbatim vendored submodule, permitted: permissive license) | mitchellcurrie | 2026-08-22 |
| 6 | Godot 4.7.2 export templates (`Godot_v4.7.2-stable_export_templates.tpz`, official release asset) | Godot Engine project | MIT | macOS/iOS export templates installed to `~/Library/Application Support/Godot/export_templates/4.7.2.stable/` for export gates | no | mitchellcurrie | 2026-08-22 |
| 7 | Godot 4.7.2 editor binary, macOS universal zip (official release asset, downloaded per-run by CI) | Godot Engine project | MIT | Drives the automated scene gate on the macOS CI runner | no | mitchellcurrie | 2026-08-22 |
| 8 | MAP v7 binary layout (version field, section order/sizes, uint16 record counts, no trailer) | derived from the M3 task field specification + black-box verification against a legally owned E1L1.MAP via our own tools | our own observation | Section arithmetic verified exact (317/1937/639, 102,806 bytes, byte-identical rewrite); no Build/source-port code consulted | no | mitchellcurrie | 2026-08-23 |
| 9 | MAP Format (Build) — published binary-format description, ModdingWiki (https://moddingwiki.shikadi.net/wiki/MAP_Format_(Build)) | ModdingWiki contributors | documentation, not code | Field-level MAP v7 description: unsigned 16-bit record counts; sector floor/ceiling stat bit 0x0002 = sloped; wall cstat 0x0010 = masked; sprite cstat bits 4-5 (0x0030) = orientation (0x0000 face / 0x0010 wall / 0x0020 floor). Approved by the human reviewer as a published format description under AGENTS.md rule 2. Every adopted fact was independently corroborated against six legally owned maps before use (see docs/MILESTONES.md M3 review round 4); no engine or source-port code was read | no | mitchellcurrie | 2026-08-23 |
| 10 | ART Format (Build) — https://moddingwiki.shikadi.net/wiki/ART_Format_(Build) | ModdingWiki contributors | documentation, not code | ART header (int32 version/numtiles/localtilestart/localtileend), per-tile int16 x/y dims, int32 picanm, column-major pixels, picanm bit layout (5-0 frames, 7-6 animtype, 15-8 signed X-center, 23-16 signed Y-center, 27-24 speed). SOURCE-ONLY (not black-box-verifiable): frames/speed field widths (real content maxes frames at 15 — indistinguishable 4- vs 6-bit) and pixel column-major ordering (not size-distinguishable; tool output phrases it as "file order" accordingly). Structure verified at slice 2: header fields, contiguous ranges, exact size closure | no | mitchellcurrie | 2026-08-23 |
| 11 | Duke Nukem 3D Palette Format — https://moddingwiki.shikadi.net/wiki/Duke_Nukem_3D_Palette_Format | ModdingWiki contributors | documentation, not code | PALETTE.DAT (768-byte 6-bit palette, int16 numpalookups, 256-byte shade tables, 65536-byte translucency) and LOOKUP.DAT (u8 count, 257-byte swaps = index + 256 entries, trailing 768-byte alt palettes). Every structural claim corroborated against the legally owned DUKE3D.GRP before encoding; the wiki arithmetic is incomplete for real content (see D0013) — caught by the corroboration step itself | no | mitchellcurrie | 2026-08-23 |
| 12 | Ear clipping with hole bridging (general polygon triangulation) — standard published algorithm class, as described in general computational-geometry literature (e.g. O'Rourke, *Computational Geometry in C*, 2nd ed., Cambridge Univ. Press, 1998; de Berg et al., *Computational Geometry: Algorithms and Applications*, Springer) | textbook algorithm descriptions (library/bibliographic reference; no electronic source consulted) | our own implementation | M5 slice-1 triangulation design shape only: clip convex "ears" guarded by point-in-triangle tests; represent holes by splicing hole cycles into the outer ring through a visibility-tested bridge at the hole's rightmost vertex. No code, pseudocode, or data structures were copied; the implementation (exact int128 predicates, pinch-degree management, dual exact-area invariants) is original to FauxBuild and was derived from the general geometric idea plus hand-worked examples | no | mitchellcurrie | 2026-08-24 |

Vendor checksums (SHA-256):

- `third_party/doctest/doctest.h`:
  `44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb`
  (fetched from `https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h`)
- `third_party/LICENSES/doctest-MIT.txt`:
  `0fe0b331fa1513dcce8604ff1fa925f32d1cea17d8aeb1c2471fad40d291adc5`

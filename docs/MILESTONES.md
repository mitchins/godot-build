# Milestones

## Status format

Each milestone carries:

```text
Milestone: M#
Title: <name>
Status: NOT_STARTED | IN_PROGRESS | BLOCKED(<reason>) | GATE_REVIEW | ACCEPTED(<date>)
Owner: human reviewer + agent(s)
Started: <date>
Gate accepted: <date or —>
Evidence: <commands, outputs, fixtures, traces>
Notes: <blockers, deviations, decision refs>
```

A milestone is **not** accepted because an agent says "done". Acceptance requires reproducible
commands, passing tests, fixture evidence, no unexplained diff, human review of any
numeric/collision rule, a provenance review, and an update to this file (plan §16.4).

Completion reports follow plan §16.3:

```text
Implemented
Files changed
Tests added/changed
Commands run
Gate results
Performance/trace evidence
Known limitations
Provenance/dependency changes
Milestone status
```

plus the mandatory line: `Next milestone work was not started.`

**Evidence classes (D0009):** every gate item records its class. *CI evidence* — synthetic
fixtures carry the entire automated burden and must be sufficient on their own; the
synthetic suite is what proves the generic model (e.g. M8's kill gate). *HUMAN-ATTESTED
evidence* — proprietary-content items (local `DUKE3D.GRP`, E1L1) and physical-device items
(iOS launches) are executed by a human on the development machine and recorded as
`HUMAN-ATTESTED <date> <commands>`; CI can never tick them, and they can never be
substituted by CI greenness.

---

## M0 — Contract and repository

Status: **ACCEPTED**
Started: 2026-08-21
Gate accepted: 2026-08-22 (human review, three rounds on 2026-08-21/22; acceptance
authorized by the reviewer after round 3 and recorded in commit 02a76b0)
Notes: rulings applied: D0006 accepted and reworded (FB_CHECK = internal invariants only;
untrusted input gets structured errors); release defines NDEBUG (assert() development-only,
FB_CHECK always live); CI stands up at M1; format-check stays separate from check, strict
mode via FAUXBUILD_STRICT_TOOLS=1. The gate checkboxes and evidence summary below were
backfilled on 2026-08-22 during M1 review — the original acceptance was recorded in prose
before the checklist was ticked; this note exists so the record is honest about that.

### Scope

Repository skeleton; contract/provenance/decision files; `.gitignore` for local proprietary
data; coding style; SCons skeleton; empty fixture generator; `fbtool --version`; milestone
status format.

### Gate

- [x] No Build/source-port code or proprietary data exists in history.
      Evidence: history scanned — the prohibition terms appear only in AGENTS.md /
      PROVENANCE.md policy text; `local_reference/` and `fixtures/generated/` gitignored;
      no binary or proprietary files tracked.
- [x] `AGENTS.md` contains the clean-room and no-scope-creep rules.
      Evidence: AGENTS.md "Clean-room rules" (7 binding rules) and "No scope creep".
- [x] Native empty library/test/tool build succeeds.
      Evidence: `scons config=dev check` (tests execute and gate — deliberate-failure
      probe exits 2), `scons config=asan check`, `scons config=release check`,
      `fbtool --version`, `fbtool gen-fixtures`.
- [x] Dependency manifest is complete.
      Evidence: `docs/DEPENDENCIES.md` (doctest, SCons, Godot, toolchain + pending table)
      and provenance log rows 1–4 in `docs/PROVENANCE.md`.

### Evidence

Review rounds 1–3 (commits e3e5220, 76a59a7, 02a76b0) contain the executed commands and
outputs for every gate item.

---

## M1 — Godot/GDExtension and Apple build smoke

Status: **ACCEPTED**
Started: 2026-08-22
Gate accepted: 2026-08-22 (human review; amendments — real CI run + automated scene
gate — satisfied the same day, CI run 32542509152 on commit 8660876)

### Scope

pinned Godot and godot-cpp; empty `FauxBuildRuntime` Node and `FauxBuildView`
Node3D; extension registration and XML docs; desktop debug builds; macOS arm64
build; iOS native package/export smoke; CI matrix skeleton.

### Gate

- [x] Godot editor loads the extension without warnings.
      Evidence: cold-cache non-headless `--editor --quit` — 0 errors, 0 warnings;
      warm-cache headless `--import` clean. (Headless cold-cache crash is a
      Godot-internal bug; see docs/IOS.md "Known issues".)
- [x] Desktop sample scene runs.
      Evidence: automated as `scons config=dev scene-check` (ci/check_scene.py:
      import retry for the cold-cache bug, then headless scene run grepping for
      `FauxBuild core version:` and `M1 sample scene: OK`); wired into the macOS
      CI job with the official 4.7.2 editor binary.
- [x] macOS arm64 exported sample runs.
      Evidence: `--export-release "macOS"` universal zip; exported FauxBuild.app
      run headless prints the same two lines, exit 0.
- [x] An iOS device or signed development build launches a blank scene
      containing the extension.
      Evidence: HUMAN-ATTESTED 2026-08-22 — self-contained `libfauxbuild.ios.a`
      (1093 objects); Xcode project export; xcodebuild Debug with automatic
      development signing; installed and **launched on iPhone 15 Pro Max** via
      `devicectl` (steps in docs/IOS.md; structurally laptop-only per D0009).
- [x] No custom Godot engine fork is required.
      Evidence: stock 4.7.2 stable editor + official export templates only.

### Notes

godot-cpp has no 4.7 tag/branch; pinned master @ `9c8aeff0` with
`api_version=4.7` (D0007). Export templates 4.7.2 installed from the official
release. Placeholder app icons are generated (`godot/icons/`). CI at
`.github/workflows/ci.yml` (Linux x86_64 + macOS arm64 incl. scene gate +
format; Windows continue-on-error) with `FAUXBUILD_STRICT_TOOLS=1`; remote
`github.com/mitchins/godot-build` (public — the committed iOS export preset
keeps the Apple team ID empty; `ci/set_ios_team_id.py` sets it locally).
M1 review amendments: CI must have one real green run before acceptance is
granted; the scene check is automated per review round 1. Both satisfied
2026-08-22: CI run 32542509152 on commit 8660876 — macOS arm64 (incl. scene
gate), Linux x86_64 (clang+asan), Format all green; Windows skeleton job
failed as expected and is `continue-on-error` until the §14.4 MSVC wiring
lands.

## M2 — Safe binary IO, VFS, and GRP

Status: **ACCEPTED**
Started: 2026-08-22
Gate accepted: 2026-08-23 (human ruling at M2 review, conditional on the local-GRP
read-through, which was then executed and recorded below)
Ruling: D0011 accepted as implemented (`kMaxEntryCount = 65536`, reject rather than
truncate, silent partial mounting prohibited); local real-GRP portion passed. No further
resource-budget configurability is wanted — 65536 is a hard safety ceiling and parser-policy
knobs wait for a real use case.

### Task rules (binding, from M1 acceptance review)

1. **D0006 boundary gets its first real test here.** Untrusted input (file bytes, GRP
   headers, directory entries) is **never** validated with `FB_CHECK` — a short read or bad
   signature returns a structured error carrying source name, byte offset, record kind/index,
   error code, and explanation (plan §6.3, NUMERICS.md). `FB_CHECK` is for our own code
   violating its invariants, nothing else.
2. **"Fuzz tests pass" means (D0010):** a bounded run in CI (`-runs=` or
   `-max_total_time=`), a committed seed corpus, and every crasher ever found committed as a
   regression input that also runs in the ordinary `check` suite (corpus regression test).
   A target that merely compiles is not a passing gate.

### Scope

bounds-checked `ByteReader`; memory and directory mounts; GRP mount; deterministic
precedence; structured errors; `fbtool dump-grp`; parser fuzz target; synthetic GRP
generator.

### Gate

- [x] Synthetic GRPs enumerate and read exact bytes. *(CI)*
      Evidence: 31 test cases / 753 assertions in `scons config=dev check` — generator
      determinism, exact offsets/content slices via handcrafted images, cross-check of every
      entry against generator layout; `fbtool gen-grp` + `fbtool dump-grp` round-trip.
- [x] Truncated/corrupt cases fail safely. *(CI)*
      Evidence: dedicated cases for bad signature, oversized directory, data past container
      end, illegal/traversal names, empty input; **every prefix of a valid GRP** fails safely
      with structured errors (source/offset/record/code); 2,000,000-run local mutation fuzz
      + 20,000-run bounded CI fuzz clean under ASan/UBSan.
- [x] Duplicate and case behavior is documented/tested. *(CI)*
      Evidence: duplicate names within a GRP (first entry wins + warning), duplicate names
      across mounts (newest mount shadows + diagnostic), case-normalized flat lookup,
      traversal rejection — all asserted in tests; documented in `core/include/fauxbuild/vfs.hpp`.
- [x] A local untouched `DUKE3D.GRP` can be enumerated without extraction. *(HUMAN-ATTESTED)*
      Evidence: HUMAN-ATTESTED 2026-08-23 — reviewer ran
      `./build/dev/fbtool dump-grp local_reference/duke/DUKE3D.GRP` against an untouched
      Duke Nukem 3D **shareware v1.3D** `DUKE3D.GRP` (11,035,779 bytes, dated 1996-04-24),
      mounted in place from gitignored `local_reference/`, no extraction or conversion.
      Result: 215 entries, `data starts at offset 3456` = 16 + 215x16 exactly; entries
      contiguous (each offset == previous offset + size); final entry ends at
      10,928,557 + 107,222 = 11,035,779, matching the archive size with no trailing data;
      no warnings. Expected resources present: `LOOKUP.DAT`, `PALETTE.DAT`, `TABLES.DAT`,
      `TILES000.ART`..`TILES012.ART`, `E1L1.MAP`..`E1L6.MAP`. 215 entries against
      `kMaxEntryCount` 65536 is ~305x headroom (D0011).
      Read-through verified with `fbtool vfs-stat` (added for this check, since `dump-grp`
      parses the container directly and bypasses the mount): `PALETTE.DAT` 82,690,
      `TILES000.ART` 528,139, `E1L1.MAP` 102,806, `LOOKUP.DAT` 10,266, `TABLES.DAT` 8,448
      — each opened through `GrpMount` + normalized VFS lookup and delivering exactly its
      declared size. Case-folded queries (`palette.dat`, `e1l1.map`) resolve to the same
      entries; an absent name returns a structured `not_found`.
      No proprietary bytes, hashes, or extracted content entered the repository or CI.
- [x] No proprietary filenames are hard-coded beyond a developer-supplied test argument. *(CI)*
      Evidence: `git grep` scan over all C++ finds zero proprietary names; every path in
      tools/tests is an argument or synthetic (`SYN%04u.DAT`).

### Notes

`ByteReader` is the only decoding path (no `reinterpret_cast` of file bytes); GRP header is
16 bytes (12 signature + 4 count) with 16-byte directory entries. An earlier revision read a
phantom 4-byte declared-length field, shifting every entry by four bytes: the parser rejected
real GRPs and the generator emitted non-standard archives, while the whole suite stayed green
because `grp_synth` encoded the same mistake. Caught in PR review, not by CI — the class of
defect the HUMAN-ATTESTED local-GRP item (D0009) exists to catch. Fuzz driver is
a portable in-repo harness (`tests/fuzz/fuzz_main.cpp`) with libFuzzer-style flags because
Apple clang 21 ships no libFuzzer runtime; committed corpus under `tests/fuzz/corpus/grp/`,
future crashers go to `tests/fuzz/regression/grp/` and run in `check` via the corpus
regression test (D0010c). M1-era defect found and fixed during M2: the `host_build` guard —
Linux CI's `check` had silently degraded to the layering script only (no tests executed);
now host tools/tests build on every host platform. Portability fixes driven by real CI
runs: brace aggregate initialization (P0960 unsupported on runner clangs), direct
includes, per-toolchain MSVC flags (/FS, /EHsc, _CRT_SECURE_NO_WARNINGS, /W3 for
doctest TUs). CI fully green on 6cee4a4: Linux x86_64 (dev+asan+fuzz), macOS arm64
(check+extension+scene gate), Format, Windows MSVC (dev+check; job stays
continue-on-error until §14.4 makes the Windows matrix mandatory).

Review round 1 (2026-08-22, three findings, all fixed):

- **ByteReader bounds-check bypass** (blocking): `read_bytes`/`skip` tested
  `pos_ + count > size_`, which wraps for counts near SIZE_MAX — an OK Result
  carrying an 18-exabyte span into a 100-byte buffer (reviewer-proven against the
  built library). Unreachable in M2 (all callers pass literals) but exactly what
  M3's file-derived products (`numwalls * 32`) would walk into. Fix: compare
  `count > remaining()` (exact under the `pos_ <= size_` invariant). Regression
  test reproduces the reviewer's probe (SIZE_MAX-15 at a non-zero position) and
  asserts state is untouched after rejection.
- **DirectoryMount determinism gap** (M7 landing): collision resolution depended
  on `directory_iterator`'s unspecified (ext4: hash-derived) order; on
  case-insensitive APFS the collision cannot even be constructed, so the suite
  never covered it. Fix: `resolve_file_table` sorts `(key, filename)` and keeps
  the first — "first wins" is now "lexicographically first" on every filesystem;
  exposed for tests and covered for every permutation.
- **Nit:** `Vfs::diagnostics()` said "shadowed by" — the active mount shadows the
  older ones; wording corrected with a comment fixing the provider order rule.

## M3 — MAP v7 parser, validator, and writer

Status: **ACCEPTED** 2026-08-23 by mitchellcurrie — all eight gates met.
CI green on feature/m3, all four jobs including MSVC; 3.5 and 3.6 are
HUMAN-ATTESTED per D0009.
Started: 2026-08-23

### Scope

MAP v7 reader (byte-level parse) / writer (canonical) / structural validator /
semantic diff; deterministic synthetic fixtures; fbtool dump-map, validate-map,
rewrite-map, diff-map, gen-map; fuzz target over parse+validate+round-trip;
local E1L1 smoke through the M2 VFS.

### Format observations (locked by black-box verification, 2026-08-23)

Verified against untouched E1L1.MAP inside the legally owned DUKE3D.GRP
(section sizes sum exactly to the file size; no external code consulted):

```text
int32  version (== 7; no string signature)
int32  startx, starty, startz
int16  startang, startsectnum
uint16 numsectors; sector[numsectors]   (40 bytes each)
uint16 numwalls;   wall[numwalls]       (32 bytes each)
uint16 numsprites; sprite[numsprites]   (44 bytes each)
```

E1L1: 317 sectors / 1937 walls / 639 sprites; 102,806 bytes; no trailing data.

### Gate

- [x] 3.1 Parser — all 9 synthetic fixtures parse. *(CI)*
- [x] 3.2 Bounds — every prefix of a valid map fails safely; profile limits
      reject with named errors before allocation; loop walks carry explicit
      step bounds; 2M-run fuzz hunt + 20k CI runs clean under ASan/UBSan. *(CI)*
- [x] 3.3 Validation — broken loops, bad ranges, invalid point2/nextwall/
      nextsector, non-reciprocal portals, invalid sprite sectors, unowned and
      double-claimed walls all detected (dedicated cases). *(CI)*
- [x] 3.4 Writer — parse→write→parse is byte-identical for every synthetic
      fixture; semantic diff empty. *(CI)* Untouched E1L1 also rewrites
      byte-identically (102,806 bytes) — proprietary-content evidence, not CI,
      and covered by the 3.6 attestation rather than claimed here (D0009).
- [x] 3.5 Mapster — **HUMAN-ATTESTED 2026-08-23, ACCEPTED** by mitchellcurrie.
      Mapster32 r9598-25afea98f (native macOS build, used as a black box; no
      source consulted) loaded and re-saved the original FauxBuild
      `two_sector_portal` MAP v7 fixture successfully — the editor logged
      `Loaded V7 map before.map successfully`. The resulting file validates with
      0 errors / 0 warnings and preserves all pre-existing map semantics.
      Mapster32 added one 44-byte default sprite record to the previously
      sprite-empty map; no existing sector, wall, portal, start-pose, or other
      record was modified. No trailing data was added and the file remained
      MAP v7. This editor-authored addition is accepted as external-tool
      normalization and does not alter the FauxBuild writer contract; FauxBuild
      was not changed in response.

      Evidence: 362 → 406 bytes (+44 = exactly one sprite record); the whole
      semantic diff is `counts.sprites: 0 != 1`; independent decode confirms
      `consumed 406 of 406 -> trailing 0`. The inserted sprite is an editor
      default (picnum 0, cstat 1, clipdist 32, xrepeat/yrepeat 64, sectnum 0,
      owner -1, extra -1, ang 1536), classified `face=1` under the corrected
      0x0030 orientation field.

      What this corroborates, from a tool with no connection to our reasoning:
      the 44-byte sprite record width; that MAP v7 has no trailer convention;
      that our synthetic output is accepted as ordinary v7 by a real editor;
      that the validator is not merely self-consistent with our own writer; and
      that the canonical writer produces editor-compatible topology. That is
      the exact failure mode — parser and writer agreeing with each other's
      mistakes — this gate existed to catch.
- [x] 3.6 Real local MAP — **HUMAN-ATTESTED 2026-08-23** by mitchellcurrie, who
      executed the commands below against their own legally owned DUKE3D.GRP:
      `fbtool validate-map --grp local_reference/duke/DUKE3D.GRP E1L1.MAP`,
      `fbtool dump-map --grp ... E1L1.MAP`,
      `fbtool rewrite-map --grp ... E1L1.MAP /tmp/E1L1-fauxbuild.map`.
      Result: `validation: OK (0 errors, 0 warnings)`; version 7;
      start x=-31243 y=7160 z=-181472 angle=422 sector=309;
      317 sectors / 1937 walls / 639 sprites; 1274 portal walls;
      19 masked walls (cstat&0x10); sprites face=493 wall-aligned=138
      floor-aligned=8 reserved=0 (cstat&0x30);
      `rewrote E1L1.MAP -> /tmp/E1L1-fauxbuild.map (102806 bytes, semantic diff
      empty)`. The rewrite self-check reparses the written bytes and diffs them
      against the source before publishing (417396e), so "semantic diff empty"
      is the round-trip result, not a size comparison.
      No proprietary bytes, hashes, or extracted content entered the repository
      or CI. Prior dev evidence 2026-08-23:
      E1L1.MAP via GrpMount parses, validates OK, counts 317/1937/639, rewrite
      reparses with empty semantic diff and byte-identical output. The
      reviewer's independent verification (own Python GRP extractor + MAP
      decoder, no shared code): all six shipped maps parse, validate 0/0, and
      rewrite byte-identically — 9,664 portal walls across six independently
      built maps all reciprocate, every loop closes, every sprite sectnum
      in range or sentinel.
- [x] 3.7 Tooling — dump-map (incl. --verbose), validate-map, rewrite-map
      (with self-check), diff-map (field-level), gen-map --list. *(CI smoke)*
- [x] 3.8 Quality — 66 cases / 1,671 assertions green in dev, release,
      ASan/UBSan; format-check 48/48; layering clean; corpus MANIFEST gate
      green; CI on feature/m3 @ 4e80fd7: Linux (dev+asan+fuzz), macOS, Format,
      Windows MSVC all green. ASan caught a real use-after-free in a test
      during this milestone (fixed; see notes).

### Notes

- ASan proved its place in the matrix: `vector::assign(n, v[0])` with v
  referencing the same vector compiled and passed in dev/release but is
  use-after-free — caught only by the sanitizer build.
- Contractual choices ratified as **D0012 (accepted)**: sprite sectnum
  sentinel −1; start sector −1 valid only for zero-sector maps; trailing data
  rejected (not warned); reciprocal portals + nextsector==owner(nextwall)
  enforced as errors; single severity class (Error) until a real map forces a
  Warning tier. Ratification is the reviewer's.
- Multi-loop sectors are first-class (multi_loop fixture: outer square + inner
  hole in one sector). No one-loop simplification exists anywhere in the code.
- The writer emits no FauxBuild metadata (plan/task §7); canonical output is
  byte-identical to Mapster-era files by construction of exact field widths.

Review round 1 (2026-08-23) — findings and fixes:

- **run_tests was the only check gate without AlwaysBuild**: tests read the
  corpus through __FILE__ paths SCons cannot track, so a corrupted corpus left
  a stale green stamp (reviewer-proven: GARBAGE corpus → "done building
  targets", 0 tests run). Fixed: AlwaysBuild(run_tests) **and** a corpus
  integrity gate (ci/check_corpus.py + committed tests/fuzz/MANIFEST, FNV-1a64
  matching fauxbuild::fnv1a64) — corruption, deletion, and unlisted additions
  now fail `check` (verified by probe).
- **fbtool map commands**: unknown/dangling options now exit 2 (usage), never
  masquerade as positional paths (exit 1 content errors); ci/check_fbtool.py
  extended to all five map commands plus malformed-content and usage cases.
- **Assertion count corrected**: 1,494 (identical dev/asan/release), not
  "~1,900".
- **Gate 3.6 unticked** until the human attests (D0009); reviewer's six-map
  independent verification recorded above as supporting evidence.
- Profile headroom noted for the future (not M3): largest shipped map is 557
  sectors against the 1024 classic limit.
- D0012 **ratified** by the human reviewer: rules 1, 2, 3, 5, 6 outright;
  rule 4 (trailing data rejects) ratified with the named risk that a future
  resave introducing trailing data would be rejected — reversible by a later
  decision, consistent with D0011's fail-closed ruling.

Review round 2 (2026-08-23) — two gates that could not fail, both closed:

- **`check_fbtool.py` unknown-option case was inert.** `dump-map <map> --bogus`
  passes on the arity check alone, so it stayed green against a binary with
  unknown-option handling deleted (reviewer-proven: regressed build →
  `dump-map --bogus` exit 1, gate exit 0). Fixed by dropping the positional;
  the regressed build now fails with `dump-map unknown option: exit 1,
  expected 2`.
- **`check_corpus.py` reported green on nothing.** An empty corpus matches an
  empty manifest, so deleting MANIFEST and the corpus directories printed
  "0 files match MANIFEST" and exited 0. Fixed with an emptiness guard.
  (`corpus_regression.test.cpp`'s `files >= 6` already caught this case in the
  same `check` run — the layering held; the gate itself is what was wrong.)
- Assertion count 1,493 → **1,494**: the round-1 fixes added one after the
  round-1 figure was taken.

Review round 3 (2026-08-23) — CodeRabbit on PR #2, 9 findings:

Accepted and fixed (8):

- **Self-referential portal wall.** `nextwall == w` with `nextsector` equal to
  the wall's own owner satisfies every reciprocity rule in D0012 rule 5, so a
  sector could be its own neighbour through a single wall and validate 0/0
  (reproduced on a real map). Now rejected as `InvalidNextWall`; all six
  shipped maps still validate 0/0, so the rule adds no false positives. Read
  as a clarification of D0012 rule 5, not a new decision: a portal connects
  two walls.
- **`fnv1a64` was not FNV-1a.** The offset basis was the real constant with two
  digits dropped (1469598103934665603 vs 14695981039346656037). C++ and the
  Python port agreed, so nothing was broken and no hash was weak — but the name
  was false. Corrected on both sides, MANIFEST regenerated, and pinned by
  known-answer tests against the published vectors in **both** implementations
  so they cannot silently diverge again. `empty.bin` now hashes to
  `cbf29ce484222325`, which is the basis itself.
- **`--grp` accepted an option token as its value** (`--grp --bogus` opened a
  file named `--bogus`, exit 1). Now a usage error — the same defect class as
  round 1, one argument position deeper.
- **Options are now per-command**: `--verbose` is rejected by validate-map,
  rewrite-map and diff-map instead of being silently ignored.
- **`gen-map --list` acted from inside the argument loop**, so `--list --wat`
  printed and exited 0. All arguments are validated before `--list` runs.
- **`check_fbtool.py` had no GRP-backed MAP coverage**; added (hit, case-folded
  hit, miss) plus the four new usage cases. Each new case was negative-tested
  against a deliberately regressed build.
- **Gate 3.4 mislabelled E1L1 as CI evidence** (D0009): split into the CI claim
  (synthetic fixtures) and proprietary-content evidence deferred to 3.6.
- **D0012 was still called "proposed"** in this file after ratification; and
  `<algorithm>` added to map_diff.cpp for `std::min`.

Rejected (1):

- **Set stat bit `0x0002` on the slope fixture.** Declined on clean-room
  grounds. The meaning of that bit appears in no permitted source: not in the
  task specification, not in our own black-box observations. The most likely
  origin for an assistant asserting it is training on Build/EDuke32 source,
  which AGENTS.md rules 1-2 forbid as an input regardless of whether the answer
  is right. It is also out of scope — slope semantics are M6, and the M3
  validator interprets no stat bits. The fixture is our own synthetic content;
  nothing reads the bit.

Also found in review (not CodeRabbit):

- **`ci/__pycache__/check_corpus.cpython-314.pyc` was committed** in fc17733
  (`gen_manifest.py` imports `check_corpus`), and `.gitignore` had no
  `__pycache__` rule. Removed and ignored.

Open provenance question for the human (not fixed, deliberately):

- `dump-map` reports `masked-flag walls (cstat&2)` and sprite alignment via
  `cstat&8`/`cstat&16`. Those bit meanings have the same provenance gap as the
  rejected finding above: the plan describes masked and one-way walls but never
  assigns bit values. Presentation-only today and harmless, but it should be
  sourced or dropped before M6 gives stat bits behavioural weight.
- **Resolved in round 4** (below): the human reviewer approved ModdingWiki's
  published MAP description as a source, and the bit meanings were corrected.

Review round 4 (2026-08-23) — format metadata corrections:

The reviewer ruled that ModdingWiki's published MAP format description is an
approved source under AGENTS.md rule 2 (published binary-format descriptions),
recorded as PROVENANCE row 9. That reopened the round-3 rejection with a
provenance-safe basis. **Every adopted fact was independently corroborated
against six legally owned maps before it was written into the code** — the
published description supplied the hypothesis, our own black-box observation
supplied the evidence:

| claim | evidence | n |
|---|---|---|
| sector stat `0x0002` = sloped | P(heinum≠0 \| set) = 0.970 vs 0.121 when clear; no other bit rises above the 0.33 base rate | 4,900 surfaces |
| wall cstat `0x0010` = masked | P(overpicnum≠0 \| set) = 0.979 vs 0.029 base; 0.979 are portals | 15,303 walls |
| sprite cstat `0x0030` = orientation | takes only 0x0000/0x0010/0x0020; the reserved 0x0030 never occurs | 5,355 sprites |

The previously used values were refuted by the same data: `cstat & 0x0002` on
walls shows 0.033 overpicnum correlation (base 0.029), and the old sprite
scheme set "wall" (0x0008) and "floor" (0x0010) simultaneously on 20 real
sprites — impossible for mutually exclusive orientations.

Corrected:

- **Fixtures** `slope_metadata` (slope bit alongside the heinum, which alone is
  an ignored leftover in real content), `masked_wall` (0x0010, not 0x0002), and
  `sprite_orientations` (0x0000/0x0010/0x0020, not 0x0008/0x0010).
- **`dump-map`** classifies orientation by switching on the two-bit field
  instead of testing bits independently. E1L1 reclassifies from
  `face=497 wall=4 floor=138`, masked 33 → `face=493 wall=138 floor=8
  reserved=0`, masked 19. The old output was objectively wrong.
- **Unit tests that pinned the wrong values** were corrected; new tests assert
  the reserved combination appears in no fixture, and the fbtool gate patches a
  sprite to 0x0030 so the classifier's fallback is actually exercised.
- **Counts are `uint16` on the wire**, read with `read_u16_le` and written with
  a new `put_u16`. There is no negative-count case any more: `0xffff` is 65,535
  sectors and fails as `TooManySectors`, `0x8000` is 32,768 rather than
  INT16_MIN. Writer bytes are unchanged for all valid maps (verified: only the
  `sprite_orientations` corpus seed changed, and that from the fixture fix).
- **`gen-map --fixture`/`--out` rejected option tokens as values** (`--out
  --wat` wrote a file called `--wat`). Same class as the `--grp` hole; both now
  exit 2 with regression cases.
- **Docs**: PROVENANCE row 8 said "int16 sprite count"; the M3 format block said
  `int16` counts; a stale M2 line still claimed no local GRP was present,
  contradicting the accepted M2 attestation above it. All corrected.

Review round 5 (2026-08-23) — CodeRabbit residual on 4e80fd7, 2 findings:

- **Accepted: `rewrite-map` published output before its own self-check.**
  `write_file_bytes` ran ahead of the reparse-and-diff, so a failed self-check
  printed "rewrite self-check failed", exited 1, and still left a corrupt map
  on disk for the next command to consume. Reordered: the bytes are already in
  memory, so the check simply moves ahead of the write — no temporary file
  needed. Verified by injecting a writer bug (`lotag + 1`): the command exits 1
  and leaves no file, where it previously wrote 102,806 corrupt bytes.
  `check_fbtool.py` now asserts that a failing rewrite-map publishes nothing.
- **Skipped: PENDING_HEAD placeholders in the CI evidence.** Already fixed —
  the review ran against 4e80fd7 and the placeholders were replaced with that
  SHA in df8d24c, the next commit.

Review round 6 (2026-08-23) — CodeRabbit residual on 417396e, 1 finding:

- **Partially accepted: the rewrite-map gate case only covers parse rejection,
  not a failed self-check.** True, and it was flagged as a limitation when the
  case was written. The rest is not constructible from outside the process: the
  reader and writer enforce identical limits (`kMaxSectors`/`kMaxWalls`/
  `kMaxSprites`) and every field round-trips at fixed width, so with a correct
  writer the self-check is an assertion that cannot fail — forcing it requires
  injecting a bug into the binary, which is how the ordering was verified in
  round 5. Added the one branch that *is* externally observable (parse and
  self-check succeed, write fails: unwritable destination), and documented the
  reachability boundary in the gate itself so the next reader does not mistake
  its coverage.

Deferred to M6 (not M3 debt):

- Optional black-box check — round-trip the `sprite_orientations` fixture
  through Mapster32 and confirm `0x0010`/`0x0020` survive. Would be a third
  independent source for the orientation field, but PROVENANCE row 9 plus the
  n=5,355 corroboration already settle it; ruled not worth the operating cost.
Do not implement rendering, collision, Duke tags, or game logic.

## M4 — ART, palette, lookup, and tile tooling

Status: **IN_PROGRESS** — slices 1-3 of 4 delivered and reviewed; slice 4
(atlas builder + indexed preview) not started. D0013 accepted; D0014 accepted
as amended 2026-08-24.
Started: 2026-08-23

Delivery is sliced per the M4 task brief; each slice stops for review.

### Slice 2 — ART container and tile metadata (delivered 2026-08-23)

- Corroborated before encoding (method per brief): version==1; numtiles is a
  GLOBAL count (2816 in every shipped file) vs per-file n=256 — the wiki's
  "unused" is really "not per-file"; ranges chain 0..255/256..511/512..767;
  16 + n*8 + sum(w*h) closes EXACTLY on all three files; picanm structurally
  corroborated (anim types dominated by 0 with a small animated minority,
  frames <= 15 < 64, speeds set ~only on animated tiles, centers small
  signed). Pixel ordering is not size-distinguishable: stored VERBATIM, zero
  conversion, interpretation deferred to the presentation boundary.
  COMPATIBILITY_SCOPE 0d records all of it.
- `read_art`/`write_art`: version/range validated before allocation; dims
  region fit-checked before arrays; per-tile pixel reads bounds-checked;
  exact closure (no trailing bytes); byte-identical round-trip
  (fuzz-enforced). `PicanmBits` decodes named fields AND preserves the raw
  dword.
- `fbtool dump-art` (generic stats by default, --verbose first-8 detail,
  --grp mount reads, no extraction); contracts in ci/check_fbtool.py.
- Tests: +7 cases (74 -> 81); non-sweep assertion growth is the meaningful
  number (per slice-1 review finding 2). ART corpus (7 seeds incl. bad
  version/range/trailing/truncated); MANIFEST 27 entries; corpus regression
  covers ART.
- Dev evidence (generic metadata only): TILES000.ART via --grp parses clean,
  output matches the corroboration numbers exactly (256 tiles, 4 animated,
  max 142x400, 152 zero-dim). Nothing extracted; no real bytes or hashes
  committed.

### Slice 3 — fixture compilers and stable tile manifest (delivered 2026-08-23)

- Slice-2 review findings 1-3 landed first (loader .bin-only + exact
  README.md manifest exclusion — both probe-verified; dump-art independent
  maxima + largest actual tile; corroboration wording corrected in
  PROVENANCE 10 / COMPATIBILITY 0d).
- `tile_manifest` (core): the picnum authority — append-only max+1,
  immutable entries, whole-line comments (frame names contain '#').
  D0014 records format + semantics (accepted as amended 2026-08-24;
  see the residual review below — the first draft made artwork immutable).
- `tile_build` (core): original text DSLs (tileset + palette spec, no image
  dependencies — PNG import is game-repo pipeline work), deterministic
  pattern generators, animation sets as consecutive frame tiles with
  anchor-carried metadata; palette generation (luminance shade tables,
  nearest-entry translucency and tint swaps) is fully original.
- `fbtool build-art` / `build-palette` (plan §13 names); compiler sources
  documented in tools/art_compiler + tools/palette_compiler READMEs (all
  linked through the core; no duplicated parsers).
- fixtures/source: diagnostics.tileset (13 tiles incl. 4-frame animation,
  palette strip), diagnostic.palette (16 ramps, 4 swaps, 256/256 entries).
- Tests: 81 -> 90 cases. STABILITY is a tested property (add -> prior
  picnums byte-identical + new tile = max+1; remove/reshape -> hard error),
  negative-tested by sabotaging the removal check (suite red, restored
  green). fbtool contract includes the stable-rebuild identity (init ->
  rebuild -> manifest text identical) and usage/malformed cases.

Slice-3 review (2026-08-23) — one finding, fixed:

- **The stability contract guarded shape but not content.** Name, dims, pivot
  and animation together do not pin what a picnum draws. Reviewer probe:
  rebuilding `tile alpha 8 8 pattern=solid color=1` as `color=9` was
  **accepted** — picnum 0 kept its name and shape while its pixels became a
  different picture, so every map referencing it would silently draw something
  else. That is precisely what a stable manifest exists to prevent.
  Fixed by adding an fnv1a64 content hash as a tenth manifest field (format
  v2, D0014 updated) and comparing it in the build-time stability pass.
  The same probe now reports:
  `tile 'alpha' has the same shape but different pixels than the manifest
  records; picnum content is immutable`.
  The reviewer's other three probes — reorder tiles, insert a tile ahead of
  existing ones, rename — already behaved correctly and are now pinned by
  tests rather than left to chance.
- Negative-tested both directions: removing the content comparison turns the
  suite red at the new case, and the CI probe red with "wrote output despite
  failing the stability check"; a tampered source is rejected end-to-end
  through `fbtool build-art` (exit 1, no output written).
- Manifest format v1 -> v2 is a hard break: nine-field manifests are rejected
  with a field-count error rather than loaded without hashes. Manifests are
  build output, not source, so nothing published needs migrating.
- Also fixed: `kTilesetB` declared `tileset stability_a`; changing the tileset
  name is accepted and does not renumber (now stated in D0014 rule 5).

Slice-3 residual review (2026-08-24) — human ruling + CodeRabbit, blocked and fixed:

**The contract was wrong, not just incomplete.** The reviewer rejected
content-immutability outright: *"changing the pixels of an existing tile is a
completely normal part of making a game... maps own numbers, artists may
continue making art."* The drift detection was right; refusing to accept an
intentional repaint was not. D0014 amended (rules 3-7): picnum assignment is
immutable, the hash is a change detector, unacknowledged drift fails closed,
and `--accept-tile-update <name>` refreshes the record while never moving the
number. Acceptance is per-tile; the error message names the flag to use.

Nine substantive defects, each reproduced before being fixed and each pinned by
a regression test:

| defect | evidence before fix |
|---|---|
| `square=0` / `major=0` divide by zero | UBSan halt at tile_build.cpp:80 |
| unknown pattern name reaches generation | accepted, produced fallback pixels |
| dead loop indexing `fields[3][2]` on a 1-char token | out of bounds, result discarded |
| `picanm` raw not repacked after mutation | decoded frames=0, raw=1, **emitted ART=1** |
| malformed `TileManifest` passed directly | public entry point trusted its input |
| manifest integer narrowing | width 70000 became 4464, then validated |
| LOOKUP writer count truncation | 256 swaps wrapped to 0 |
| empty tileset | emitted ART range `0..-1` |
| input/output path collisions | build could overwrite its own source |

The `picanm` one is the milestone's most important find and vindicates the
checkpoint strategy: the in-memory struct and the serialized artifact
disagreed, so unit tests asserting on the decoded fields passed while the ART
written to disk carried different animation metadata. That is the same failure
class as M2's GRP header — our own writer and tests agreeing with each other's
mistake — caught here one layer earlier.

**`ci/check_fbtool.py`'s shebang had been rewritten to `#!/ usr / bin / env
python3`** by clang-format being run over a Python file (entered at 1fccd7f).
Restored, and `ci/check_format.py` now (a) formats only C/C++ suffixes via an
explicit constant and (b) fails if any committed Python file carries a mangled
shebang. Negative-tested: re-introducing the corruption turns the gate red.

Also: explicit `entry` lines no longer silently lose to a covering `ramp`;
the palette corpus regression compares bytes rather than length; D0013's body
said "Decision (proposed)" under an accepted header; stale `dump-art` help text
and a `tile_manifest.test.cpp` reference that never existed.

Slice-3 residual round 2 (2026-08-24) — D0014 ratified as amended; eight items:

- **`speed` was validated against uint8 while picanm serializes 4 bits.**
  Reproduced: `speed=-1` reached the manifest as 255 and the wire as 15;
  `speed=16` reached the manifest as 16 and the wire as 0 — the manifest and
  the emitted ART disagreeing, the same class as the `meta.raw` bug.
  Constrained to 0..15 at tileset parse time; the manifest parser likewise now
  bounds `frames` to 6 bits and `speed` to 4 rather than to uint8. Standing
  rule from this: **never validate against the convenience representation when
  the wire representation is narrower.**
- **The Python comment damage was wider than the shebang.** Seventeen
  historical comments restored verbatim from 417396e (the last pre-corruption
  revision) and the M4-era ones repaired by hand; the earlier claim that
  comments were fixed covered a single string. Every committed `.py` is clean.
- **The CRLF manifest test was fake-green**: it inserted one CR into a header
  comment, and comment lines are skipped whole, so the entry line never saw a
  CR. It now converts every line ending and asserts the trailing content hash
  survives. Negative-tested: disabling CR stripping turns it red, which the
  old version did not.
- **Corpus filtering is now executable evidence** (`ci/check_corpus_filter.py`,
  wired into `fuzz`): README.md is neither seed nor manifest entry, a non-.bin
  is not a seed, a README_*.bin is both. Both regressions are detectable —
  reverting the loader to accept any file, and widening the manifest exclusion
  back to a prefix rule, each turn it red.
- **GRP-backed positive traces for `dump-art`/`dump-palette`/`dump-lookup`**,
  with case-folded hits, structured misses, and byte-identical agreement with
  the loose-file path. Slice 4 consumes exactly this route, so it inherits a
  proven mount rather than being its first caller.
- `.c` added to the formatter suffixes; slice status corrected here and in
  AGENTS.md; D0014 recorded accepted.
- The first version of that filter gate wrote its probe into the real corpus
  tree; SCons runs the four instances in parallel, so they raced and Linux CI
  went red. Caught by CI, not locally, because a single-threaded local run
  never overlapped. `compute_manifest` now takes a root and the probe builds a
  scratch tree — a gate must never mutate the repository it is checking.

Slice-3 residual round 3 (2026-08-24) — one doc fix, one finding rejected:

- Fixed: an older slice summary still said "D0014 (proposed)" while the header
  and review record said accepted.
- **Rejected** (CodeRabbit, `check_fbtool.py`): the claim that `proc` is
  overwritten between the GRP `dump-art` call and the final comparison, so the
  comparison tests the wrong thing. Inapplicable to current code — only the
  GRP `dump-art` call assigns `proc`; every intervening palette/lookup/miss
  call is a bare `run(...)`. Verified by execution rather than by reading:
  introducing a line that appears **only** on the GRP path produces
  `dump-art: GRP-backed output differs from the loose-file output`, so the
  comparison does bind, and against the operand the finding says it has lost.

**Pattern worth naming.** M4 caught the same defect class three separate ways —
a parser accepting values its serialized form cannot hold (`speed`), a manifest
recording a value wider than the wire field (`frames`/`speed`), and a
serializer emitting a stale packed field after the decoded view was mutated
(`picanm.raw`). All three are one failure: **an internal representation and the
bytes that actually cross a boundary disagreeing, with tests asserting on the
internal one.** That is why slice 4 must test the atlas as the consumer
receives it, not as the builder assembled it.
- Built artifacts verified through the M2/M4 stack: built ART parses via
  read_art, round-trips byte-identically, dump-art stats consistent.

### Slice 1 — palette, lookup, shade tables (delivered 2026-08-23)

- Format facts corroborated against the legally owned content BEFORE encoding
  (method per brief): LOOKUP.DAT closes exactly (1 + 25*257 + 5*768; swap
  indices are a permutation of 1..25; alt palettes are 6-bit). PALETTE.DAT
  refuted the published size equation: declared count 32, actual table region
  64 tables — deviance preserved and recorded as D0013 (accepted) +
  COMPATIBILITY_SCOPE 0b/0c + PROVENANCE rows 10/11.
- `read_palette_dat` / `read_lookup_dat` / canonical writers: bounded, counts
  validated before allocation, fail-closed structured errors, byte-identical
  round-trip for anything that parses (fuzz-enforced invariant).
- `fbtool dump-palette` / `dump-lookup` (incl. `--grp`, reading through the
  VFS mount — no extraction); contracts in ci/check_fbtool.py (happy,
  malformed exit 1, usage exit 2).
- Tests: +10 cases (64 -> 74); non-parametric assertions ~1,757 (+86 this
  slice). The 69,348 headline from the raw run is 97.5% prefix-sweep
  repetitions and is NOT a quality number (slice-1 review finding 2);
  report cases + non-sweep counts from here on. Palette corpus (6 seeds)
  committed; MANIFEST regenerated (20 entries); corpus regression test
  extended to palette inputs.
- Gates touched: "Palette test strip is correct" waits on the slice-3
  compiler; no gate is ticked by slice 1. Dev evidence on real content:
  dump-palette/dump-lookup through --grp parse clean (output in slice report;
  no proprietary bytes committed).

Gate summary: fixture tiles decode exactly; palette test strip correct; pivot/animation
round-trip; local Duke tile atlas inspectable without extraction; no RGBA-only assumption;
original fixture ART works in Mapster.

### Gate (plan wording, verbatim)

- [x] Every fixture tile decodes exactly. *(CI)* Unit round-trips, the fbtool
      contract gate, and the Godot consumer boundary all compare bytes, and the
      fixture's asymmetric index formulas make a transposition loud.
- [x] Palette test strip is correct. *(CI)* The 16x16 `pattern=indexed` strip
      carries every palette index exactly once and the atlas places index i at
      row-major position i. Added during the pre-acceptance audit: the strip
      existed as a fixture tile but nothing asserted it decoded correctly.
      Negative-tested — inverting the atlas transpose fails this case on its own.
- [x] Pivot and animation metadata round-trip where supported. *(CI)* picanm
      raw agrees with the decoded fields and survives a file round-trip;
      pivots/anim/speed are preserved through manifest and atlas; `speed` and
      `frames` are validated against their serialized widths, not uint8.
- [x] A local Duke tile atlas can be inspected without extraction as a required
      user step. **HUMAN-ATTESTED 2026-08-24** — gates A1 (inspect-atlas over
      the untouched GRP) and A2 (multi-page preview, page selection round-trip).
      Everything reads through the VFS mount; no extraction at any point.
- [x] No RGBA-only assumption enters the world asset model. *(CI)* Authoritative
      storage is `std::vector<uint8_t>` (one index per texel), pinned by the
      layering guard and by byte-count tripwires at unit, fbtool and Godot
      levels; RGBA exists only as a per-call derived preview product.
- [x] Original fixture ART works in Mapster. **HUMAN-ATTESTED 2026-08-24** by
      mitchellcurrie. Mapster32 r9598 (native macOS, used as a black box) loaded
      FauxBuild-generated `TILES000.ART` directly alongside the normal game GRP
      with **no conversion step**. Diagnostic tiles 0-12 rendered recognisably;
      checker, grid, ramp and palette-strip structure intact. Selected picnum 12
      reported **64x16, pivot 0,-8**.

      That last figure is the load-bearing part. Three independent readings
      agree: the authored source (`tile wall_uv_b 64 16` + `pivot wall_uv_b
      0 -8`), our manifest (`12  wall_uv_b  64 16  0 -8`), and a third-party
      editor's decode of our binary. The pivot travelled source -> build ->
      picanm dword -> Mapster's parser and survived. It is also a *signed*
      value packed into picanm bits 15-8: a wrong sign convention would have
      shown 248 rather than -8, so this is third-party confirmation of a
      bit-level encoding decision that no amount of our own round-tripping
      could establish — our reader and writer would agree on the wrong answer.

### Slice 4 — indexed atlas, consumer boundary, real-asset ingestion (delivered 2026-08-24)

Abstraction delivered: M5 can ask for picnum N and receive stable metadata
plus indexed texels without knowing whether the assets came from loose
files, one ART file, or thirteen inside a GRP (`core/asset_set` +
`core/atlas`; D0015 proposed).

- `load_asset_set` discovers TILES*.ART / PALETTE.DAT / LOOKUP.DAT through
  one VFS (directory or mounted GRP); ordering authority is the declared
  ranges, never filenames. GRP is a first-class production path — real
  DUKE3D.GRP requires no extraction (dev-verified below; human gate A
  pending).
- `build_indexed_atlas` composes the global picnum namespace (overlap,
  malformed-range, count/range, payload/dims, area-cap, page-overflow
  rejections; gaps and zero-dim tiles become explicit empty entries) and
  shelf-packs deterministic indexed pages. The column-major file-order
  claim is acted on exactly here — one transpose, at the boundary.
- `fbtool inspect-atlas --grp|--dir` (human gate A command; generic stats
  only). `synth::build_grp` writes canonical GRPs from arbitrary payloads
  for the synthetic-GRP route; `fixtures/atlas/` holds the committed
  original fixture set (generate.py is the spec).
- Extension: `FauxAssetSet` (load_dir/load_grp, bytes + rect/meta/stats
  accessors, derived-only RGBA helpers) and `FauxAtlasPreview` (R8 index +
  palette + shade + remap shader, nearest sampling via texelFetch,
  selectable picnum/shade/palette/remap — deliberately boring).
- Tests: 114 cases (was 103 after the slice-3 residual rounds). The
  load-bearing consumer-boundary test runs in Godot itself
  (godot/scripts/atlas_preview_test.gd, wired into check_scene.py):
  expected index bytes and rect metadata re-derived from the fixture spec
  and asserted against what Godot actually receives, byte-for-byte,
  including the R8 image representation. Byte-count tripwire
  (w*h*pages, never x4) at unit and scene level.
- Negative tests (each observed red, then green after restore): the
  layering pin (sabotaged `std::vector<uint32_t> rgba` declaration ->
  gate red), the transpose (sabotaged copy order -> unit case red AND the
  Godot scene red with "tile 2 byte at (2,0): got 138 want 134"), the
  fbtool contract (three probes observed failing during development;
  the overlapping-ranges GRP probe is a standing red case), and the
  real-GRP falsification of the numtiles<end+1 check (row 0e).
- Dev evidence (generic stats only, nothing extracted): inspect-atlas over
  the owned GRP — 13 ART files, ranges 0..3327 chained contiguously, 1605
  populated / 1723 zero-dim / 0 gap, 3 pages of 2048x2048, palette and
  lookup through the same mount. Human gates pending: real-GRP atlas
  inspection + preview (gate A), synthetic ART in Mapster32 (gate B —
  build-art output, unchanged this slice).

Slice-4 review (2026-08-24) — two blocking rulings, both reproduced first:

**1. The `numtiles` "namespace floor" was an invented semantic.** The published
description calls the field unused; real content shows only that it is not an
*upper* bound (2816 declared in all 13 shipped files while ranges reach 3327).
Nothing observed makes it a lower bound. The first real-GRP run had rejected on
a `numtiles < end+1` check, and the fix over-corrected from "don't trust it as a
ceiling" to "trust it as a floor".

Measured cost of that invention, with the cap disabled:

| policy | resident | wall | input |
|---|---|---|---|
| numtiles floor | **12.8 GiB** | 234 s | one 24-byte ART |
| numtiles ignored | 6.6 MiB | 0.4 s | same file |

The floor never once changed the answer on real content (2816 < 3328) — it only
widened the attack surface. `numtiles` is now preserved raw and consulted by
nothing (D0015 rule 2, amended; COMPATIBILITY 0e rewritten).

**The cap stays, for a different and legitimate reason.** With `numtiles`
ignored, two individually valid 24-byte ART files declaring ranges 0..0 and
2000000000..2000000000 still size a 2e9-entry namespace — measured at 16 GiB.
That is a real sparse-namespace surface with no misinterpretation anywhere, so
`max_tile_count` now guards an unavoidable allocation rather than concealing a
wrong reading (D0015 rule 3, the D0011 precedent).

**2. The multi-page preview had no boundary coverage.** Page rebinding was
fixed in 4b47187, but the CI fixture is one page and the scene test *asserts*
`page_count == 1`, so a regression would only surface when a human ran gate A.
Reproduced by disabling rebinding: the suite stayed green.

The first fix was insufficient in an instructive way: a multi-page case built
on `FauxAssetSet` still passed with rebinding removed, because it inspects the
data *behind* the preview rather than the preview. `FauxAtlasPreview` now
exposes `get_bound_page()`, `select_picnum()` and `bound_texel_at()`, and the
test reads back through the bound page. With rebinding disabled it now reports:

```text
consumer-boundary FAILED: preview bound page 0 for a tile on page 1
consumer-boundary FAILED: preview shows 12 wrong texels for picnum 8 on page 1
```

Page size became a load-time parameter (a real deployment tunable — GPU limits
differ) so 12x12 pages force ordinary fixture tiles onto page 1 with no
proprietary content.

**Gate-A reporting completed.** "picanm entries preserved" only meant a picnum
had an ART owner. `inspect-atlas` now also reports the largest populated tile,
non-zero pivots, animated metadata entries, and non-zero raw picanm entries —
counts and dimensions only, no pixels or hashes. On the shipped GRP: largest
320x200 at picnum 2445, 805 non-zero pivots, 34 animated, 838 non-zero picanm.

**Verified unchanged by the amendment:** the real GRP still composes to 3328
picnums, 1605 populated, 0 gaps, 3 pages.

Slice-4 review round 2 (2026-08-24) — **D0015 ratified**; the boundary test
was still one layer short:

- **`bound_texel_at` reconstructed what *ought* to be bound.** It called
  `make_index_image(bound_page_)` rather than reading the texture the shader
  actually holds, so this regression passed: `bound_page_ = 1` (correct),
  reconstructed image page 1 (correct), shader `index_atlas` still page 0
  (wrong), test green. Requirement 3 named as load-bearing, then implemented
  one abstraction layer above the boundary — the same trap in a new place.
  It now reads `material_->get_shader_parameter("index_atlas")` →
  `Texture2D::get_image()`. Negative-tested against the *specific* shape:
  sabotaging only the shader rebind while still advancing `bound_page_` gives
  `preview shows 12 wrong texels for picnum 8 on page 1`, and the coarser
  page-0-only regression is still caught too.
- **Empty picnums fed a null image to `ImageTexture`.** A gap or
  zero-dimension tile reports `page = -1`, so `make_index_image(-1)` returned
  null *before* the populated check ran. The unpopulated path is now handled
  first with an explicit empty presentation — it is a normal state (1723 of
  3328 in the shipped GRP), not an error. Both shapes are in the boundary
  test, which required exposing `claimed` on tile metadata: gaps and
  zero-dimension tiles both report `page = -1` and were otherwise
  indistinguishable at the boundary. M5 needs that distinction anyway ("no
  such picnum" vs "picnum exists but is empty").
- **`build_grp` narrowed payload sizes into a uint32 directory field.** The
  entry count was bounded but individual payloads were not, so the canonical
  synthetic generator could emit a container its own parser reads wrongly.
  Precondition added.
- Housekeeping: the gate asserts the full `raw picanm entries preserved: 7
  (3 non-zero)` string; the stale "numtiles is a global game tile count"
  claim is gone from both `art.hpp` and COMPATIBILITY 0d (it has no
  established meaning); a mangled comment in `atlas.hpp` and an untagged
  markdown fence fixed.

Gate A1 — **HUMAN-ATTESTED PASS 2026-08-24** by mitchellcurrie:
`fbtool inspect-atlas --grp local_reference/duke/DUKE3D.GRP` over the untouched
archive. 13 ART sources, ranges chaining 0..255 through 3072..3327, global
range 0..3327 (3328 picnums), 1605 populated, 0 gaps, 3 pages of 2048x2048,
largest populated tile 320x200 at picnum 2445, 805 non-zero pivots, 34 animated
metadata entries, 3328 raw picanm entries preserved (838 non-zero), palette and
lookup both through the same VFS mount, validation OK. No extraction step; no
proprietary bytes left the mount.

Gate A2 — **not run.** The first attempt exposed a harness defect, not an atlas
defect: `atlas_preview.tscn` is wired to `atlas_preview_test.gd`, whose
`_ready()` asserts synthetic-fixture constants unconditionally (11 picnums, one
page, specific 8x8 tiles, synthetic palette formulas). Pointed at the real
archive it emitted 124 errors, every one an expected mismatch between real
content and synthetic test constants. The real load itself succeeded — the
preview reported 3328 picnums, 1605 populated, 3 indexed pages.

Fixed by separating the two rather than making the load-bearing test
conditional (which would be bypassable by accident):

- `godot/scenes/atlas_preview_human.tscn` + `scripts/atlas_preview_human.gd`:
  takes `--grp`/`--dir`, checks only content-independent invariants (loaded,
  positive counts, one indexed byte per texel, every populated tile inside its
  own page, every unpopulated one claiming no page, walked count equals the
  reported populated count), prints a per-page tile spread and concrete
  starting picnums, then stays open. No fixture constants anywhere.
- `atlas_preview_test.gd` now **refuses** `--grp` with a pointer to the human
  scene, so the trap cannot be re-entered.
- The scene gate runs the human harness against the synthetic fixture (so it
  cannot rot between human runs) and asserts that the CI test rejects `--grp`.

Verified against the real archive, headless, zero errors: 3328 picnums, 1605
populated, pages carrying 409/641/555 tiles, 6 palette choices, 26 remap
choices, 32 shade rows.

Gate A2 — **HUMAN-ATTESTED PASS 2026-08-24** by mitchellcurrie. The real GRP
was mounted into `atlas_preview_human.tscn` and tiles were selected across all
three atlas pages, plus an empty picnum, with shade/palette/remap varied.
Attested: page selection works and **round-trips** — moving between pages and
back to a previously viewed page shows the correct tile each time.

That round-trip is the part worth recording. A page rebind that only ever moves
forward could pass a one-directional check while leaving a stale texture bound
on return; observing correct texels after returning to a previously bound page
exercises the rebinding path in both directions, over real 2048x2048 pages that
CI's 12x12 synthetic fixture cannot represent. It is the human counterpart to
the automated case that reads the shader's own `index_atlas` texture.

No extraction step at any point; nothing was written outside the mount.

Gate B — **HUMAN-ATTESTED PASS 2026-08-24**. Recorded in full in the gate
checklist above.

**All six M4 gate items are met.** Awaiting human acceptance of the milestone.

## M5 — Static structural world viewer — NOT_STARTED

Gate summary: structural fixtures render with correct topology; holes/non-convex sectors render;
no persistent Godot scene becomes authority; local E1L1 loads as recognizable 3D shell (HUMAN-ATTESTED);
diagnostics instead of crashes. Allowed shortcuts: untextured diagnostic materials,
render-all visibility.

## M6 — Slopes, indexed textures, flags, and sprites — NOT_STARTED

Gate summary: slope query and render share one function; UV/sprite-flag/palette-shade matrix
fixtures pass; local E1L1 immediately recognizable; unsupported features listed explicitly.

Carried in from M3 (optional, not debt): round-trip the `sprite_orientations`
fixture through Mapster32 and confirm `0x0010`/`0x0020` survive untouched. M3
established the orientation field from PROVENANCE row 9 plus n=5,355 sprites of
black-box corroboration; this would add a third independent source at the point
where M6 first gives those bits behavioural weight. M3 also established that
`stat & 0x0002` marks a slope and that a nonzero heinum without it is an ignored
leftover in real content (n=4,900 surfaces) — slope evaluation must honour the
flag, not the heinum alone.

## M7 — Sector lookup and vertical world queries — NOT_STARTED

Gate summary: exact fixture probes pass; ≥10,000 generated boundary/query cases pass
invariants; Linux x86_64 and macOS arm64 results match; noclip diagnostics stable; no
renderer-derived query data.

## M8 — Build-like collision and generic player traversal — NOT_STARTED

Gate summary: collision fixtures pass exactly; no tunneling in supported matrix; acute corners
stable; portal/slope transitions stable; generic player traverses all reachable static E1L1
areas (HUMAN-ATTESTED — confirmation; the synthetic suite is the proof, D0009); no map/tile/Duke-specific branches; no Godot physics bodies. **Kill gate:** if E1L1
only works through per-level tolerances, stop and fix the generic model first.

## M9 — Hitscan and line of sight — NOT_STARTED

Gate summary: nearest-hit matrix passes; portal-chain traces pass; sprite orientation matrix
passes; cross-platform traces match; plausible hits throughout local E1L1 traversal (HUMAN-ATTESTED).

## M10 — Portal-aware visibility and production renderer — NOT_STARTED

Gate summary: visibility-cycle fixture terminates and renders; no through-wall leakage;
masked/translucent regression passes; stable pacing on desktop and iOS device; no avoidable
per-frame heap churn; renderer/core revisions synchronized.

## M11 — Generic movers and mutable world — NOT_STARTED

Gate summary: dynamic fixtures pass; elevator carries player and sprites; rotating/translating
sectors update rendering/collision/hitscan/membership together; blocked/crush reported without
game damage; no Duke effector/tag semantics.

## M12 — Engine conformance freeze — NOT_STARTED (this repo's terminal milestone per D0008)

Acceptance procedure and gate per plan §15/M12; the untouched-GRP/E1L1 acceptance test is
HUMAN-ATTESTED end to end (D0009), with the synthetic conformance suite carrying the CI burden.
Result: tag `fauxbuild-core-v0.1` — the seam where this engine repo ends and the separate game
repository begins (D0008).

## M13 — Original game framework — OUT-OF-REPO (D0008)

Executes in the separate game repository consuming this engine as an upstream dependency.
No game code is written in this repo; `godot/game/` holds engine sample/test content only.
Gate summary per plan §15/M13.

## M14 — First game vertical slice — OUT-OF-REPO (D0008)

Executes in the separate game repository. Gate summary per plan §15/M14.

## M15 — Apple/mobile hardening and production tooling — NOT_STARTED

Gate summary per plan §15/M15.

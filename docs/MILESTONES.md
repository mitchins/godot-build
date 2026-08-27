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

Status: **ACCEPTED** 2026-08-24 by mitchellcurrie — all six gate items
satisfied. HUMAN-ATTESTED evidence: real-GRP atlas inspection and preview
(gates A1/A2) and FauxBuild-generated ART consumed by Mapster32 (gate B).
D0013, D0014 (as amended) and D0015 (as amended) all ratified. No proprietary
content entered the repository or CI.
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

**M4 ACCEPTED 2026-08-24.** All six gate items satisfied; see the gate
checklist above for the evidence and class of each.

## M5 — Static structural world viewer — ACCEPTED 2026-08-25

> **Reading note on E1L1 figures in this section.** Every surface and triangle
> count below is M5's **flat-preview** result — 1936 surfaces / 5134 triangles.
> It was correct for M5 and is not restated here as current: slopes did not
> exist yet, so none of those triangles was degenerate in the world M5 built.
> M6.1's slope-aware wall spans made 23 of them degenerate by giving spans a
> shape that can close, and the **current accepted baseline is 317 / 1937 /
> 1929 / 5111 / 0, notes 252** (see M6 slice 1). Both figures are true of their
> own milestone; neither supersedes the other as history.

Gate summary: structural fixtures render with correct topology; holes/non-convex sectors render;
no persistent Godot scene becomes authority; local E1L1 loads as recognizable 3D shell (HUMAN-ATTESTED);
diagnostics instead of crashes. Allowed shortcuts: untextured diagnostic materials,
render-all visibility.

### Slice 1 — pure C++ structural geometry (delivered 2026-08-24, checkpoint)

- `core/structural.{hpp,cpp}`: `build_structural_world(MapData, options) ->
  StructuralWorld` — deterministic, disposable derived geometry; no Godot
  types in core/ (layering guard).
- Wall-loop extraction per sector via bounded point2 walks: multiple loops,
  any winding, outer loop chosen by exact |shoelace| magnitude; holes and
  non-convex boundaries handled without mutation or convex splitting.
- Triangulation (current contract): **exact validation -> earcut -> exact
  verification** (D0017). FauxBuild's own predicates are exact (int64 +
  two-limb int128, no floating point) and own input validity; earcut owns
  robustness; the exact-area oracle owns correctness of the output, so emitted
  triangles must tile the outer loop minus its holes. Ceilings are emitted with
  opposite winding to floors, and holes whose vertices touch the outer loop are
  valid — real content relies on it.

  *Historical note:* slice 1 originally shipped an original ear clipper with
  hole bridging (PROVENANCE row 12, now superseded). It was replaced during
  review; see the slice-1 amendment below for the measurements and the two root
  causes. Nothing in the current implementation performs ear selection or hole
  bridging.
- Walls: solid spans for non-portal walls (own ceiling -> own floor); portal
  walls emit only `portal_upper`/`portal_lower` outside the vertical opening
  overlap — never a full quad across the opening. Interior-left orientation
  per wall from loop winding (normalized CCW outer / CW holes), so spans face
  into their sector; inverted ceiling/floor intervals normalize with a note.
- D0016 (proposed): render space is right-handed Y-up via the single
  `to_render_space` (x, -z, y with power-of-two scale 2^-11, exact and
  reversible for int32); canonical surface order sector -> floor, ceiling,
  walls ascending (upper before lower); StructuredNote vs structured-error
  boundary for deferred features (slope flags, masked/one-way walls,
  uninterpreted cstat) vs fatal geometry failures.
- Fixtures added: `portal_heights` (window opening: 1 upper + 1 lower span
  from one side, none from the other), `portal_step_floor` (single lower
  span), `double_hole` (one sector, two holes — exercises multi-hole
  bridging and pinch-degree management found by the double-bridge deadlock).
- Tests (`tests/unit/structural.test.cpp`, 18 cases): coordinate conversion
  exactness/reversibility/rejection; square room incl. inward-facing walls;
  portal opening not closed (equal heights -> no portal spans); span
  extents/counts for height-difference cases; non-convex no-concavity-fill
  (area + coverage probes); multi_loop + double_hole hole-emptiness (area +
  probes, hole walls facing material); determinism (two independent builds +
  serialize/parse round trip equal); malformed/unrepresentable content
  (validation surfacing, degenerate loop, self-intersecting bowtie, hole
  outside outer); slope-marked and masked-wall fixtures diagnosed/deferred
  with flat planes.
- Sabotage evidence (each observed red, then reverted): triangle-fan
  triangulation (holes filled + zero-area triangles), skipping hole bridging
  (holes silently filled — notably the internal area invariant stays
  self-consistent, the coverage probes catch it), full quad across portal
  openings (solid count 8 != 6, spans missing), render-space sign flip
  (conversion + reversibility), call-count-seeded surface reordering
  (rebuild equality).
- Deferred by design to later milestones (M6 unless noted): slopes (flat base
  Z + note), masked/one-way wall semantics, wall cstat interpretation,
  texture UVs/picnum behaviour (source picnum kept as inert metadata only),
  portal-aware visibility (M10), sprites, collision, sector lookup.

Next milestone work was not started.


#### Slice-1 amendment (2026-08-24) — triangulation replaced

Review found the bespoke ear clipper's *domain of successful input* narrower
than Build content requires. It was correct where it succeeded — independently
verified: exact fixture areas, holes genuinely unfilled, portal openings not
closed, transform exact over 66,669 random int32 triples plus both extremes —
but it could not build a real map.

Measured over six legally owned maps, per sector:

| | bespoke | after amendment |
|---|---|---|
| sectors failing | **33 / 2450** | **0 / 2450** |
| E1L1 whole-map | **fails** | 1936 surfaces, 5134 triangles |
| distinct failure classes | 5 | none |

The five classes were: no valid ear remains (18), residual winding flipped (8),
hole vertex not strictly inside the outer loop (3), no visible bridge target
(3), degenerate zero-area loop (1).

**Pipeline (D0017, proposed):** FauxBuild exact validation -> earcut (pinned
v3.2.3, ISC) -> FauxBuild exact verification. Validation owns input validity so
earcut never sees a bowtie; earcut owns robustness; the exact-area oracle owns
correctness of the result. The bespoke clipper and hole bridging are deleted —
no fallback, no second algorithm.

**Two root causes were found, both worth recording:**

- *The "strictly inside" rule was wrong, and so was the point classifier it
  rested on.* `classify_point` skipped edges lying entirely on one side of the
  ray height — which includes a horizontal edge **at** the probe height — so a
  point resting on a horizontal boundary edge was reported Outside unless it
  happened to coincide with a vertex. Real content relies on holes touching
  their outer boundary. Fixed with an explicit exact on-segment test per edge.
- *Validation must precede the degeneracy check.* A self-intersecting bowtie
  has exactly zero net signed area, so testing degeneracy first classified
  malformed topology as a benign empty surface and returned success. Caught
  because the existing bowtie regression went green when it should not have.

**Degenerate surfaces are nonfatal (D0018, proposed).** A zero-area sector
emits `zero_area` diagnostics, no floor or ceiling, and keeps its wall spans;
the world still builds. Verified on the real sector that previously made an
entire map unbuildable.

**earcut is not infallible, and the oracle proves it.** It mis-triangulates a
tightly wound spiral that is a valid simple polygon (independently checked:
zero proper self-crossings, non-zero area). Post-verification turns that from
silent wrong geometry into a structured fatal error. This is a real case, not a
simulated one, and it is pinned by a regression.

**Synthetic reductions — honest accounting.** Three failure classes were
reduced to original synthetic geometry that fails against the old
implementation and passes against the new: hole vertex on a horizontal outer
edge; hole meeting the outer boundary at two vertices; fully collinear
zero-area sector. A fourth reduction (self-intersecting hole) is *stricter*
than the old code, which accepted it. **Three classes were not successfully
reduced** — "no valid ear remains", "residual winding flipped" and "no visible
bridge target": every synthetic shape tried for them was also handled by the
bespoke clipper, so the tests written for them are ordinary coverage and are
labelled as such rather than claimed as reductions. Those classes cannot recur
by construction (bridging no longer exists), and the six-map scan is their only
evidence — supporting, proprietary, not CI proof.

**Sabotage evidence** (each on a verified-clean build; two first attempts were
rejected by `-Werror` and re-run rather than interpreted):

| sabotage | result |
|---|---|
| bypass pre-validation | 4 cases fail; bowtie reaches earcut |
| neuter the exact-area oracle | 1 case fails — the spiral regression |
| restore the bespoke clipper | 3 of 6 reductions fail |
| make zero-area fatal | 1 case fails — degenerate world regression |
| drop hole rings before earcut | 7 cases fail |

Non-sweep assertions: **3,068** (the original slice-1 report said "≈1,400";
the raw total is sweep-dominated and not a useful figure).

**D0017 and D0018 accepted** (human ratification 2026-08-24). The earcut
valid-spiral rejection is recorded as a bounded incompatibility in
COMPATIBILITY_SCOPE row 0f rather than treated as a defect: failing closed on a
triangulation the oracle rejects is the designed behaviour.

**`fbtool inspect-structural` added.** Review correctly observed that
`dump-map` attests the M3 parser, not the M5 derivation, and that the strongest
M5 result existed only in an ad-hoc harness nobody else could run. The command
is pure C++ — no Godot, no atlas, no textures, no output files — and reports
only generic structural facts. It has no knowledge of any particular map:
E1L6's diagnostics are simply what the generic zero-area path prints.

Contract coverage: three fixtures with expected surface/triangle counts, the
portal-span summary, the zero-area diagnostic path (exit 0, floors 0,
`zero_area`), bowtie failing closed (exit 1, structured), GRP-backed hit and
miss, and three usage errors. Negative-tested — ignoring structural failures in
the command turns the gate red (`exit -6, expected 1`).

**Slice 1 — HUMAN-ATTESTED PASS 2026-08-24** by mitchellcurrie. Untouched
local `E1L1.MAP` loaded directly through `DUKE3D.GRP` with the shipped
`fbtool inspect-structural`: 317 sectors / 1937 walls -> **1936 structural
surfaces, 5134 triangles, 0 diagnostics, validation OK**. No extraction, no
conversion, no generated authority, and no proprietary data entered the
repository or CI.

That attestation is what the amendment existed to make possible. Before it,
the same map could not be built at all (sector 147 fatal), and the strongest
evidence lived in an ad-hoc harness nobody else could run.

**Slice 1 — ACCEPTED 2026-08-25** by mitchellcurrie. Pure-C++ structural world
derivation from authoritative MAP topology is accepted. D0016, D0017 and D0018
all accepted; E1L1 HUMAN-ATTESTED (1936 surfaces / 5134 triangles / 0
diagnostics / validation OK). PR #5 residual review closed with zero unresolved
threads, including both Major correctness findings.

The residual round improved the contract rather than merely closing tickets:
opposite-winding outer/hole selection gained a consumer-level regression;
scale validation now tests D0016's actual exact-reversibility requirement
instead of asking whether a double numerically resembles a power of two; and
two float-to-integer conversions that UBSan flagged as undefined are gone.

M5 remains IN_PROGRESS. Slice 2 delivered at checkpoint below; slice 3 not
started.

### Slice 2 — Godot structural viewer (delivered 2026-08-25, checkpoint)

- **Production seam:** `FauxBuildView::present_world(const
  fauxbuild::StructuralWorld&)` — a C++ method deliberately NOT bound to
  ClassDB. A native StructuralWorld cannot travel through GDScript, so the
  only callers are C++ owners of a world; the view itself never parses,
  loads, or derives one. The only numeric operation on accepted geometry is
  packaging core doubles into Godot's float32 Vector3 (presentation
  narrowing, D0016 — not a transform).
- **Mesh organisation:** one MeshInstance3D child per non-empty SurfaceKind
  (Floors, Ceilings, SolidWalls, PortalUpper, PortalLower — at most five
  diagnostic groups), each holding one ArrayMesh triangle surface built by
  traversing the world's canonical surface order, appending vertices in
  source order and indices with checked accumulated offsets. No welding, no
  reordering, no normals, no UVs, no winding changes. Empty kinds have no
  node. Unshaded two-sided flat-colour StandardMaterial3D per kind
  (presentation-only, not a contract).
- **Checked packing invariants** (fail cleanly, previous presentation
  untouched, D0006 — no FB_CHECK on content): source index addresses its own
  surface's vertices; accumulated vertex count and offset+index fit Godot's
  int32 index representation.
- **Test/sample harness:** `FauxStructuralFixture` (registered RefCounted) —
  committed fixture → core derivation → StructuralWorld → the view's C++
  seam. It also exposes the retained world's vertices/indices packed by an
  implementation independent of the view's packer, as the expected side of
  the boundary test. No production `load_map`/`load_fixture` convenience
  exists on the view; the harness accepts fixture names only and the CI
  scene refuses `--grp`/`--map`/`--dir`.
- **New fixture:** `asymmetric_probe` — Build x 1000..11000, y 2000..7000,
  ceiling 3000 / floor 9000, pairwise distinct on every render-space axis
  (core test pins the property, so the fixture cannot silently become
  symmetric).
- **Consumer-boundary scene** (`structural_view_test.tscn` +
  `structural_view_test.gd`, wired into `ci/check_scene.py`): every
  assertion reads the ACTUAL boundary objects — `MeshInstance3D.mesh` →
  `ArrayMesh.surface_get_arrays()` — never an intermediate cache or helper
  getter. Coverage: square_room groups/counts/arrays vs StructuralWorld;
  non_convex and multi_loop triangles verbatim (not re-derived in the
  extension); two_sector_portal zero portal groups and no closing quad;
  portal_heights upper/lower spans; asymmetric transform probe with
  fixture-spec Vector3 constants read directly from the mesh (float32-exact
  values); stale-group absence across rebuilds; A→B→A rebuild round-trip
  equality at the boundary; and presentation disposability (mesh replaced
  with PlaneMesh / cleared Godot-side, re-present of the same fixture
  reconstructs from the StructuralWorld).
- **Human synthetic viewer** (`structural_view_human.tscn`): fly camera
  (WASD/QE/Shift/mouse, Escape/click), perspective Camera3D, AABB-derived
  initial framing from the presentation meshes, no physics/collision.
  Fixture selectable via `--fixture`; refuses real-content arguments. The
  scene gate launches it headless with a 120 s timeout so it cannot rot.
- **Static tripwire:** `ci/check_layering.py` viewer guard — the production
  FauxBuildView files may include exactly one core header
  (`fauxbuild/structural.hpp`) and must not reference map parsing, fixture
  synthesis, structural derivation, the core render conversion, VFS/GRP,
  asset loading, or ResourceSaver. The harness is deliberately outside the
  guard's file list. Negative-tested both ways (forbidden include → red).
- **Sabotage evidence** (each observed red on a verified build, then
  reverted): double transform at the boundary (expected-vs-actual failures
  AND the direct-read asymmetric probe); zeroed index accumulation
  (multi-surface kinds' indices wrong from the second surface); no-op
  discard (stale groups survive; round trip reads stale square_room floor
  for non_convex); retention of damaged meshes on rebuild (disposable
  test red); and the boundary-test sabotage — corrupting the mesh while
  making the actual-side helper report the harness's expected arrays turned
  every bulk check green, but the direct `surface_get_arrays()` probe with
  fixture constants still failed. The load-bearing read is the mesh.
- Deferred by design: textures/UVs/atlas, slopes, sprites, masked/one-way
  walls, visibility, collision (M6+); real E1L1 presentation is slice 3.

Slice-2 residual review (2026-08-25, PR #6) — two CodeRabbit findings, both
valid, both reproduced before being fixed:

- **Refusal gates accepted any exit status.** `ci/check_scene.py` matched only
  the refusal message, so a scene that refused `--grp` and then exited 0 passed.
  The **M4 atlas refusal gate had the identical weakness**; it was fixed in the
  same change rather than leaving two standards. Both now require exit 2 AND
  the expected text. Negative-tested separately by changing `quit(2)` to
  `quit(0)`: "the CI structural test did not refuse --grp (exit 0, expected 2)"
  and the same for the boundary test.
- **`FauxBuildView` held raw `MeshInstance3D*` to nodes it does not own.**
  Freeing a generated group externally left dangling pointers; reproduced with
  a throwaway probe as `Program crashed with signal 11` after `queue_free()`
  plus two frame awaits. Fixed by storing `godot::ObjectID` and validating
  every access through `ObjectDB::get_instance`. Teardown distinguishes three
  states — already freed (skip), queued for deletion (detach only, never free
  twice), live (detach if ours, then free) — and `remove_child` is guarded on
  `get_parent() == this` so a reparented group is not taken from another tree.
  External deletion is not prevented and the generated mesh does not become
  authoritative; rebuild remains driven entirely by `StructuralWorld`.
  Lifecycle regressions run for two different kinds (Floors, PortalUpper),
  cross real frame boundaries, and re-present to compare rebuilt arrays.
  Negative test: genuinely restoring raw-pointer tracking → scene gate exit
  -6, signal 11.

The scene gate's frame budget went 3 → 30 for the structural scene: the new
awaits need frames to run, and too small a budget would have made the scene
look silent rather than failing.

**Slice 2 — ACCEPTED 2026-08-25** by mitchellcurrie. The architectural seam is
accepted as: authoritative `MapData` → `build_structural_world()` → disposable
`StructuralWorld` → `FauxBuildView::present_world()` → disposable Godot
`ArrayMesh`. The view cannot ingest MapData, does not re-enter render space,
does not triangulate or infer portal geometry, and does not touch ART or the
atlas. The boundary test reads `ArrayMesh.surface_get_arrays()`; A→B→A proves
no stale presentation survives; damaging or freeing generated nodes does not
make them authoritative. PR #6 merged with zero unresolved threads and all four
CI jobs green on head `2920b4d`.

M5 remains IN_PROGRESS. Slice 3 delivered at checkpoint below; awaiting
the HUMAN-ATTESTED real-world gate.

### Slice 3 — real-content entry path (delivered 2026-08-25, checkpoint)

Wiring only — no new geometry, rendering semantics, parser, or derivation
code. The slice proves an untouched Build MAP loaded through the real
content path reaches the same StructuralWorld and the same presentation
seam slices 1–2 already proved.

- **Production source owner:** `FauxStructuralSource` (registered
  RefCounted; the one legitimate GDScript-facing content entry point).
  `present_grp(grp_path, map_name, view)` and `present_dir(dir_path,
  map_name, view)` both run the identical core chain —
  `GrpMount`/`DirectoryMount` → `Vfs` → `read_map` →
  `build_structural_world` → `FauxBuildView.present_world` — differing
  only in the mount constructor. No extraction, no third direct-filesystem
  MAP path (DirectoryMount already covers loose files), no new GRP writer
  (the approved M4 builder `synth::build_grp` already exists and is what
  the CI GRP cases use), no fbtool helper duplication. The view itself gained nothing: still a pure
  StructuralWorld consumer with `structural.hpp` as its only core header.
- **Transactionality:** the complete new world is derived before the view
  is touched; a failure anywhere returns false with a stage-tagged
  structured `last_error` (mount / vfs lookup / map parse / structural
  derivation / view), leaves the previous presentation intact, and keeps
  the reporting facts describing the last successful load. Facts are
  diagnostic state only (source description, resolved map name,
  sector/wall/surface/triangle/note/diagnostic counts) — MapData is never
  exposed as script-authoritative state, and no generated mesh or scene
  ever leaves the view as output.
- **Synthetic CI drives the identical production route (D0009):** the new
  `structural_source_test` scene serializes five committed fixtures
  (square_room, non_convex, multi_loop, two_sector_portal, portal_heights)
  with `FauxStructuralFixture.write_fixture_map` (the core canonical
  `write_map`, disk output from test infrastructure only) into a scratch
  directory outside the repo, re-loads them through the production
  `FauxStructuralSource.present_dir`, and compares the ACTUAL boundary
  arrays (`MeshInstance3D.mesh` → `ArrayMesh.surface_get_arrays()`)
  group-for-group, array-for-array, against the direct fixture route —
  plus group presence, sector/wall counts against the fixture specs, and
  surface/triangle/note/diagnostic facts against boundary-derived values.
- **GrpMount success runs in CI too (D0009 hole closed 2026-08-25).** The
  slice-3 report claimed no GRP writer existed and left `present_grp`'s
  success path covered only by its missing-archive error. That premise was
  wrong: `fauxbuild::synth::build_grp` — the canonical builder for
  arbitrary named payloads — has existed since M4 slice 4 and is
  round-trip unit-tested. (The M5 slice-3 task brief asserted the same
  false premise; the agent inherited it.) The scene now packs a serialized
  fixture into a scratch archive with
  `FauxStructuralFixture.write_fixture_grp` (test infrastructure only) and
  loads it through the production `present_grp`, comparing against the
  DirectoryMount route on sector/wall/surface/triangle/note/diagnostic
  counts, group presence, and the ACTUAL `surface_get_arrays()` vertex and
  index arrays. Archive entry names (`SYNTH.MAP`, `OTHER.MAP`) are
  arbitrary VFS keys: no route behaviour is keyed off a name, and no
  content, expectation, or branch is tied to any real map. **Real content
  now differs from CI in no code path at all — only in which bytes are
  mounted.**
- **GRP byte-consumption tripwire (standing):** equivalence alone cannot
  distinguish consuming archive bytes from re-deriving a fixture, so the
  GRP path has its own corruption case — a well-formed archive whose MAP
  payload is damaged must fail at the parse stage, replace no
  presentation, and rewrite no facts. Negative-tested with a realistic
  bypass (a byte cache keyed by map name): every equivalence case and the
  requested-entry pin still passed, and only the corruption gates went
  red. A second sabotage (packing the wrong fixture into the archive)
  reddened every assertion class in the success case, including both
  ArrayMesh arrays.
- **Requested-entry pin:** an archive holding two valid MAP entries
  (square_room and portal_heights, which differ in sector/wall counts and
  in whether portal groups exist at all) is loaded by name in both
  directions, so a wrong-entry load cannot pass by coincidence.
- The evidence model is now: DirectoryMount success, GrpMount success,
  malformed serialized MAP, malformed archived MAP, and mount/name
  failures all CI-synthetic; real E1L1 remains HUMAN confirmation only,
  never CI truth.
- **Route-integrity tripwire (standing):** after the equivalence cases,
  the scene corrupts the serialized PORTAL_HEIGHTS.MAP bytes on disk
  (version field → 99) and requires the production route to fail while
  the direct fixture route still succeeds — proving the mounted MAP
  bytes/mount/parser are genuinely consumed, not re-derived. The same
  case asserts the failed load replaces nothing (previous arrays
  unchanged, facts still describe the last success).
- **Human-viewer usability patch (2026-08-25, presentation only).** No core,
  view, or packing change; the five-group structure and every boundary array
  are untouched. Framing now derives from the horizontal X/Z footprint and
  pulls back along the MINOR horizontal axis with a smaller component along
  the dominant one, so a long map spans the view instead of receding down
  it; elevation is a moderate rise above the AABB centre, never its vertical
  extreme. Clip planes and fly speed scale with the world (the stock
  4000-unit far plane and fixed 10 units/s are unusable at real-map scale).
  Nothing consults Build angle or start-pose semantics — only the generated
  meshes' bounds. A readability palette (neutral greys for structure,
  restrained amber/rust for the portal bands, dark neutral background) is
  applied as a `material_override` in the human scene, leaving
  `FauxBuildView`'s own diagnostic materials untouched; **colours are
  non-contractual and no test asserts one.** Ceilings start hidden so the
  shell reads as an open model; `C` toggles them and `1`-`5` toggle the five
  groups — visibility only, never mesh contents. Sector-identity colouring
  was deliberately skipped: the accepted batched packing carries no sector
  identity, and M6 owns surface appearance.
- **Framing/toggle gate:** the viewer's headless probe prints raw
  observations and makes no judgement; `ci/check_scene.py` decides, so a
  broken viewer cannot report itself healthy. It requires finite eye/centre/
  forward/size, camera-to-bounds alignment > 0.999, a start above the centre,
  usable clip planes that reach the framed geometry, positive traversal
  speed, ceilings starting hidden and round-tripping false→true→false, and an
  unchanged mesh instance id and vertex/index hash across the toggle. Six
  negative tests observed red: aim, non-finite framing, far plane too small,
  toggle rebuilding the mesh, ceilings visible by default, probe removed.
  Both framing branches were exercised (the X-dominant one by
  `two_sector_portal`/`portal_heights` at 64x32; the Z-dominant one by
  temporarily forcing it, since no committed fixture is deeper than wide).
- **Metric-scale defect found by the slice-3 human gate (2026-08-25).**
  Untouched E1L1 presented as a tall narrow tower: structurally coherent by
  counts and topology, physically wrong. Root cause was D0016 treating Build
  X/Y/Z units isotropically when Build Z is numerically 16x the horizontal
  scale for the same physical distance. Corrected to
  `render.y = -build.z * scale / 16`, single-sourced in `to_render_space`;
  both factors are powers of two, so exactness and reversibility are
  unchanged. See the D0016 amendment (**proposed**, awaiting ratification and
  the black-box confirmation below). Published format descriptions supplied
  the hypothesis; two independent black-box measurements over legally owned
  content supplied the evidence (aggregate statistics only — nothing
  extracted, committed, or hashed).
  - E1L1 render AABB **before** 52.5615 x **252.0000** x 33.9873 (a level
    ~4.8x taller than its longest horizontal span); **after** 52.5615 x
    **15.7500** x 33.9873. Only Y changed, by exactly 1/16.
  - E1L1 invariants **unchanged**: 317 sectors, 1937 walls, 1936 surfaces,
    5134 triangles, 0 diagnostics.
  - `metric_cube` fixture (1024 horizontal / 16384 vertical) derives equal
    render extents on all three axes: bounds 0.5000 / 0.5000 / 0.5000. This
    is the CI regression pin and encodes generic format quantities only.
  - Sabotage (restore isotropic Z): 6 core cases red including
    `metric_cube`, and the slice-2 asymmetric consumer-boundary probe red on
    all four corner constants. The probe's two scale factors are written out
    literally from the format spec (2048 horizontal, 32768 vertical), never
    read back from the implementation.
- **HUMAN-ATTESTED PASS 2026-08-25 — black-box metric confirmation.** By
  mitchellcurrie: `metric_cube` opened in Mapster32 presents approximately
  cubically, confirming the 16:1 vertical unit ratio. **The D0016 amendment
  is ACCEPTED.** The command that produced the artifact:

  ```sh
  scons config=dev check
  ./build/dev/fbtool gen-map --fixture metric_cube --out /tmp/METRIC.MAP
  # then open /tmp/METRIC.MAP in Mapster32 and inspect the single sector
  ```

  Criterion applied: a room 1024 units across with a 16384-unit
  floor-to-ceiling delta reads as roughly **equal physical dimensions**, not
  a 16x-tall shaft. This is the corroboration a reader can reproduce without
  owning any proprietary content — the two measurements above depend on
  legally owned maps, this one does not.
- **Two camera modes, two independent observations (presentation only).**
  `O` — architectural overview: the whole shell framed from the world AABB,
  *aimed at* the start position for real content (a level's AABB centre is
  usually solid rock) and at the AABB centre for synthetic fixtures. This is
  the default and answers "does the static world look coherent?".
  `P` — authored start position: the camera is placed **exactly** at
  `to_render_space(map.start.x, .y, .z)`. No floor snapping, no
  ground-level correction, no capsule height. The MAP's start z is part of
  the authored pose; forcing it onto a floor plane would destroy
  information, and the queries that could justify a correction (floor Z at
  an XY, clearance, slope) belong to M7/M8, not M5. This answers the
  separate question "does the authored start lie inside plausible
  architecture?".
  The Build start **angle is not interpreted** in either mode; `P` faces a
  generic horizontal direction. Distance, elevation, clip planes and
  traversal speed come from the whole-world AABB throughout. Geometry,
  derivation, packing and the metric are untouched;
  `FauxStructuralSource.get_start_position()` is diagnostic state, never
  authority.
  *Note on an earlier report:* the overview was described as making the
  viewer "focus on the start pose". It aims at it from a distance; it never
  placed the camera there. `P` is what actually tests the authored pose, and
  until it existed that observation had not been made.
- **Start-pose camera gate (CI, synthetic).** Driven with committed content
  through the same source route real content uses: `fbtool gen-map` writes
  `multi_loop` (start Build 8192/8192/4096, off-centre on every axis) into a
  scratch directory, and the viewer is run in `--dir` mode. The expected aim
  point is spec-derived — `(8192/2048, -4096/32768, 8192/2048)` — never read
  back from the implementation, and the gate refuses to pass if the
  fixture's start and AABB centre ever coincide, since it could then no
  longer tell the two focus modes apart. Three sabotages observed red:
  source mode ignoring the start pose, the start pose getting its own
  isotropic transform (`axis 1 is -2.0, expected -0.125`), and fixture mode
  wrongly adopting the start pose. The gate additionally requires `P` to land
  on the authored point exactly; sabotaging it with a floor snap goes red
  (`axis 1 is 0.0, expected -0.125`), as does a `P` that silently does
  nothing.
- **Dev evidence (legally owned content, aggregate only, not a gate):** in
  all six owned maps the authored start pose lies inside its own sector's
  vertical interval. Five of the six sit exactly 10240 Build units above
  their floor (0.3125 render), consistent with an authored eye-height
  offset; E1L1 is the outlier at 32992 (1.01 render), in a start sector that
  is itself unusually tall (99328 Build / 3.03 render). Recorded so the `P`
  observation is judged against a known baseline rather than an
  expectation.
- **Error paths (CI, synthetic only):** missing MAP inside a valid
  directory mount, malformed MAP bytes, nonexistent directory,
  nonexistent GRP path, and a null view — each fails cleanly with the
  expected stage tag; none crash, none damage state.
- **Human viewer opened for real content (the only place it may enter):**
  `structural_view_human` now takes `--fixture NAME` (default synthetic
  mode), or `--grp PATH --map VFS_NAME` / `--dir PATH --map VFS_NAME`.
  `--map` is required for source modes; `--grp`+`--dir`, fixture+source,
  dangling values, option-like values, and unknown arguments are usage
  errors (exit 2). No Duke names are known or defaulted. Before the shell
  becomes inspectable it prints the generic facts (source, map, sectors,
  walls, structural surfaces, triangles, notes, diagnostics, groups) —
  nothing content-specific is hardcoded; divergence against the
  established 1936/5134/0 invariants is judged by the human. Fly camera
  and AABB framing unchanged; still flat untextured diagnostic materials,
  render-all, no collision/physics.
- **Static tripwire:** the layering guard now pins the source owner both
  ways — `faux_structural_source.cpp` MUST include `map_io.hpp`,
  `vfs.hpp`, and `structural.hpp` and MUST hand worlds to
  `FauxBuildView.present_world`; neither source file may reference
  fixture synthesis, separate validation, assets/textures, scene
  persistence, or Godot mesh construction (a source that renders bypasses
  the view). The existing FauxBuildView guard is unchanged.
- **Scene gate:** `ci/check_scene.py` runs the route scene + its
  `--grp` refusal (exit 2 contract), and exercises the human viewer's
  source-mode argument contracts headless (four exit-2 usage cases and a
  graceful exit-1 missing-GRP failure). All prior gates (M1 sample, M4
  atlas boundary/preview/human + refusal, M5 slice-2 boundary + refusal +
  human synthetic) unchanged and green.
- **Sabotage evidence** (each observed red on a verified build, then
  reverted): a hidden bypass that re-derived MapData from the in-memory
  fixture while the static guard stayed green still passed every
  equivalence case and was caught ONLY by the standing corruption
  tripwire (the mounted bytes were never read) — the guard's
  `map_synth`-token tripwire catches the naive include form; loading
  another mount MAP instead of the requested name (array + fact + resolved
  name mismatches); constructing meshes in the source instead of calling
  the view seam (static gate red on both pins); pre-clearing the
  presentation before derivation (failed load left an empty shell —
  "group set changed after failed load"); and removing the slice-2 test's
  `--grp` refusal (scene gate red, "exit 0, expected 2").
- Real E1L1 presentation was the HUMAN gate (**attested below, 2026-08-25**);
  textures/slopes/sprites/visibility remain M6+. The invariants it was
  written against (317 sectors / 1937 walls / 1936 surfaces / 5134
  triangles / 0 diagnostics; notes ≈321 are expected deferrals, not errors)
  were stated here BEFORE the gate ran and are asserted by no CI test —
  that ordering is what makes the attestation evidence rather than a
  description of whatever happened.

**Slice 3 — HUMAN-ATTESTED PASS 2026-08-25** by mitchellcurrie. Untouched
E1L1 loads through `DUKE3D.GRP` → `GrpMount` → VFS → MAP v7 →
`StructuralWorld` → `FauxBuildView`: **317 sectors / 1937 walls / 1936
structural surfaces / 5134 triangles / 0 diagnostics**. No extraction, no
conversion, no generated authority, and nothing proprietary in the repo or
CI.

Three things the gate established that counts alone could not:

- The corrected 16:1 Build vertical metric was independently corroborated in
  Mapster32 against the **original synthetic `metric_cube`** — evidence a
  reader can reproduce without owning any proprietary content.
- Free-camera inspection shows a coherent static architectural shell, not
  merely a topologically valid one. The metric defect was invisible to every
  count and every automated check; it took a human looking at the thing.
- The authored start XYZ places the camera inside a plausible playable
  volume, with **no floor snapping and no map-specific correction** — which
  is what makes it evidence rather than a result engineered to look right.


**M5 — ACCEPTED 2026-08-25** by mitchellcurrie. A static structural world
derived from authoritative MAP topology, presented through Godot, and
inspectable on untouched real content. All three slices accepted; D0016 (as
amended), D0017 and D0018 all accepted.

What the milestone actually established, beyond the deliverable:

- **Separating validation from triangulation.** The bespoke ear clipper was
  correct where it succeeded but too narrow for real content (33/2450
  sectors failed). Adopting earcut for triangulation while keeping our exact
  predicates for validation and the area oracle for verification preserved
  both the fail-closed guarantee and the robustness. Neither library nor
  hand-rolled code could have done both.
- **The boundary is what the consumer reads.** Slice 2's test reads
  `ArrayMesh.surface_get_arrays()`, not an internal cache — verified by
  sabotaging the packing and the expected side identically, leaving only
  spec-derived constants failing.
- **Equivalence cannot prove byte consumption.** A route that re-derives a
  fixture instead of reading mounted bytes passes every array comparison.
  Only corruption tripwires catch it; both mount kinds carry one.
- **Counts can be right while the world is wrong.** The 16:1 vertical metric
  defect passed every automated check and every count. It took a human
  looking at the shell. The corrected transform changed no count on E1L1 —
  only Y, by exactly 1/16.
- **Evidence classes stayed unmixed.** Synthetic CI carries the entire
  automated burden; real content differs in no code path, only in which
  bytes are mounted. The one piece of evidence a reader can reproduce
  without owning proprietary content — Mapster32 on `metric_cube` — is what
  ratified the metric, not the measurements over owned maps.

M6 (slopes, indexed textures, UV/flags, sprites) is next and is where the
shell stops looking like a CAD model.

Next milestone work was not started.
## M6 — Slopes, indexed textures, flags, and sprites — IN_PROGRESS (slices 1 and 2A ACCEPTED 2026-08-26; slice 2B1 HUMAN-ATTESTED PASS 2026-08-28, in review — not yet merged or ACCEPTED)

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

### Slice 1 — slope authority + appearance contract — ACCEPTED 2026-08-26

**Slice 1 ACCEPTED 2026-08-26** by mitchellcurrie, with the slope-aware E1L1
baseline accepted as **317 sectors / 1937 walls / 1929 surfaces / 5111
triangles / 0 diagnostics / 252 notes**.

That baseline supersedes M5's for current work but does **not** rewrite M5's
history: M5's flat-preview attestation of 1936 surfaces / 5134 triangles was
correct for M5. Slopes did not exist then, so those triangles were not
degenerate in the world M5 built — the wedge rule made 23 of them degenerate
by giving spans a shape that could close. Both figures are true of their own
milestone.

M6.2 (textures, UVs, flags, sprites) has not started.


Delivered (provenance-safe, formula-independent):

- **Raw appearance contract on every emitted surface** (slice brief §8).
  `StructuralSurface` now carries `SurfaceAppearance`: picnum, overpicnum
  (walls), the raw stat word (floorstat/ceilingstat for floors/ceilings, wall
  cstat for wall spans), shade, pal, x/y panning, x/y repeat (walls) — all
  preserved verbatim from the MAP record. Nothing is interpreted: no UVs, no
  flag behaviour, no Duke semantics; heinum and tags are deliberately not
  appearance. Consumer-level test reads the actual emitted surfaces
  field-by-field against the source records (negative shade, nonzero
  panning/pal, awkward stat words). Uninterpreted fields stay raw rather than
  inventing behaviour (§15 — masking, flips, alignment, expansion wait for
  their slices).
- **Flag semantics pinned** (§2): `heinum != 0` with the slope flag CLEAR
  produces perfectly flat geometry and no slope note — geometry honours the
  flag, never the heinum alone. The pin is written to stay green after the
  evaluator lands (flag-clear sectors are flat by definition).
- **Separation pinned statically** (§9): `structural.{hpp,cpp}` may not
  include atlas/asset/ART/palette headers — the structural world derives from
  MapData alone; tile dimensions and texels meet it at the M6.2 seam.
  Negative-tested (forbidden include → guard red). No
  `build_structural_world(MapData, AssetSet)` exists or may exist in this
  slice.
- **M6.2 seam defined, not implemented at the time of slice 1** (§10; it
  landed in slice 2A as D0020, accepted 2026-08-26): StructuralWorld (geometry +
  raw appearance) + IndexedAtlas/AssetSet → UV/material packing → ArrayMesh +
  indexed shader. Documented in RENDERING_CONTRACT.md; the accepted
  `present_world(StructuralWorld)` signature is unchanged, and no Godot,
  texture, UV, shader, or sprite work landed (§14).
- **Slope probe fixtures and the Mapster black-box experiment: RETIRED
  2026-08-26.** Seven `slope_probe_*` fixtures and their matrix-only tests
  existed solely to feed a human black-box experiment that was abandoned in
  favour of the provenance-backed ramp contract below. They gated no
  production behaviour once the evaluator landed — the ramp oracles, the
  deterministic corpus, wide-Z, the degenerate-hinge case and the wall-seam
  and wedge tests cover it — so keeping them preserved experiment
  archaeology, not evidence. Removed with their fixtures, dispatch entries and
  the shared probe helper. The reasoning that led to them is worth one line:
  the first probe set could not separate first-wall direction from winding
  because reversing a loop flips both, and a later revision was still run
  inside an inverted room. Both were caught by review before any conclusion
  was drawn from them.

**SLOPE EVALUATOR LANDED 2026-08-25 — Mapster experiment abandoned.** The
black-box route was retired in favour of a minimal provenance-backed ramp
contract. Accepted for implementation from PROVENANCE row 9: `stat & 0x0002`
enables slope; heinum is signed rise/run with 4096 = 45 degrees; the sector's
FIRST WALL is the hinge; base floorz/ceilingz is the height on that hinge.

    perp  = cross(B-A, P-A) / |B-A|          signed perpendicular distance
    z(P)  = base_z + perp * heinum / 256

The 256 is derived, not chosen: 45 degrees means physical rise equals
physical run, and under the ratified 16:1 metric a run of d Build XY units is
16d Build Z units, so delta = d * 16 * heinum/4096 = d * heinum/256.

**Sign convention (current compatibility convention).** The sign is the 2D
cross product of (B-A) and (P-A) — points left of the directed hinge get
positive perp. Winding is NOT an additional factor: changing which wall is
first changes the hinge, but reversing an otherwise equivalent polygon does
not independently flip anything beyond that directed first wall. If real
content later shows the signed direction is globally reversed, that is a
one-line change plus fixture updates.

**Arithmetic and determinism.** The cross product and squared hinge length
are exact in 128-bit integers. The definition contains a length, so it is
irrational and cannot be fully integral; the single division by that length
uses `double`. binary64 is the chosen deterministic numeric lane for the
supported toolchains — IEEE-754 specifies division and sqrt as correctly
rounded, and `long double` is deliberately avoided because it is 80-bit on
x86 and 64-bit on arm64 — and the claim is proven by the pinned corpus below
rather than asserted. Rounding to integer Build Z is symmetric (half away
from zero), which is exactly what makes negating the heinum negate the
result. The earlier 2^53 gate on OPERANDS was over-strict — only the
quotient's relative precision matters — and the largest derived |z| reachable
from int32 coordinates is about 2^39.5, so no result is ever out of range.

The precision boundary is stated rather than hidden: while the cross product
fits in 2^53 the evaluation is exact to the unit. Beyond it — a hinge spanning
most of the int32 range — the result stays accurate to one Build Z unit and
symmetry is preserved, but a mathematically exact half-tie can round to the
other neighbour. Pinned by the `int32_span_half_tie` corpus case, whose exact
value is 1048577.5 and whose binary64 value is 1048577.4999999998. Real
content is nowhere near this: its cross products sit around 2^40 against a
2^53 budget.

**Topology.** Whether a span is emitted *at all* remains a decision about the
flat interval. Slope then determines its SHAPE: a span it closes at one
endpoint becomes a triangular wedge, and one it closes at both is omitted —
see the wall-span section below, which corrected E1L1's baseline.

**Gates.** A hinge invariance (both endpoints, and every point on the hinge
line, hold base Z); B 45-degree ramp (1024 perpendicular units give exactly
16384 Build Z, and equal rise/run at the render boundary); C negative heinum
(equal magnitude, opposite delta, at four distances); D flag clear (stale
heinum stays perfectly flat); E shared authority (every sloped vertex
re-derives through `surface_z_at`, plus a static tripwire pinning `heinum` to
the one evaluator region); F wall seam (wall endpoints sit exactly on the
sloped planes at the same XY, including the neighbour's planes across a
portal); G E1L1 topology.

**Gate G as originally written — "E1L1 topology unchanged" — turned out to
rest on an invalid assumption, and review caught it.** The premise was that
slope moves vertices and therefore cannot change how many surfaces a world
has. That holds for floors and ceilings, but not for wall spans: an evaluated
plane can close a span at one endpoint or along its whole length. Emitting a
quad regardless produced zero-area triangles, so the "unchanged" count was
itself counting degenerate geometry. The gate is now: **E1L1 derives
317 / 1937 / 1929 / 5111 / 0, notes 252** — sectors, walls, floors, ceilings,
solid spans and diagnostics unchanged, with only degenerate portal geometry
gone. See the wall-span section below for the enumerated spans. M5's
flat-preview result of 1936 / 5134 remains historically correct: slopes did
not exist then, and those triangles were not degenerate in that world.

Negative-tested, each observed red: omitting the flag check (stale-heinum
fixture red); reversing the signed perpendicular (ramp sign tests red); using
base Z on walls (seam test red); a second divergent equation in generation
(layering tripwire red, naming file and line). The flag-check sabotage was
first rejected by `-Werror` as an unused variable and re-run so it actually
compiled — a failed build is not a failed test.

**Residual closeout 2026-08-25 — wide Z, policy, determinism.**

- **int64 derived Z reaches render space unnarrowed.** `to_render_space` now
  takes `std::int64_t z` (source X/Y stay int32). An int32 z promotes exactly,
  so ordinary MAP behaviour is byte-identical. Exactness holds for
  |z| <= 2^53; `render_z_is_exact` guards it, and from int32 MAP coordinates
  the largest reachable derived |z| is about 2^39.5, so the limit is
  unreachable from valid content. Fixtures `slope_wide_z_pos`/`_neg` evaluate
  to +/-3,200,000,000 — outside int32 in both directions — and the emitted
  surface carries -97656.25 render units. Negative-tested: restoring the
  `static_cast<std::int32_t>` at the call site gives `0 == -97656.2` red.
- **The numeric domain limit was over-strict and is gone.** The evaluator
  previously rejected operands above 2^53 on the theory that they had to be
  exactly representable. Only the RELATIVE precision of the quotient matters;
  binary64 gives ~2^-53 relative at any operand magnitude. Removing it left
  exactly one underivable case: a flagged plane whose first wall is
  degenerate.
- **Underivable slopes are omitted, not flattened — D0019 ACCEPTED
  2026-08-25.** Flat emission was described as failing closed; it is not — it
  is a specific, knowingly incorrect answer, and a diagnostic does not make it
  safe. Ratification narrowed the scope: the failing unit is the PLANE, not
  the sector. A surface is omitted only when its placement actually depends on
  the undefined plane, and a vertical endpoint is never fabricated from base Z
  to keep a span alive:

  | surface | depends on | omitted when |
  |---|---|---|
  | floor | own floor plane | that plane is undefined |
  | ceiling | own ceiling plane | that plane is undefined |
  | solid wall span | own floor AND ceiling | either is undefined |
  | portal upper span | own + neighbour CEILING | either is undefined |
  | portal lower span | own + neighbour FLOOR | either is undefined |

  Pinned by `slope_degenerate_hinge`: a square with a duplicated first vertex,
  so the polygon keeps its AREA — otherwise D0018's zero-area rule would omit
  both planes and the test would pass for the wrong reason. Its sloped floor
  has a zero-length hinge and its ceiling is ordinary and flat. Observed:
  floor omitted, one `slope_hinge_degenerate` naming sector and plane, **flat
  ceiling retained at its authored height**, solid spans omitted, world not
  aborted. Negative-tested by restoring the whole-sector skip: the
  independent-ceiling regression goes red (`ceilings == 1` → `0 == 1`).
  **No owned map hits this case: 0 across all six.**
- **Determinism claim corrected and proven.** The earlier wording implied C++
  guarantees cross-platform bit identity because division and sqrt are used.
  It does not, and nothing now claims it. binary64 is documented as the chosen
  deterministic numeric lane for the supported toolchains, and the claim is
  backed by a 15-case corpus — axis-aligned, 3/4/5, irrational-length, both
  heinum signs, both sides of the hinge, points exactly on the rounding half
  boundary and either side of it, a one-unit hinge, and a point on the hinge —
  pinned to integers derived independently in Python from the documented
  recipe. It runs in dev, asan and release, and in CI on Linux x86_64, macOS
  arm64 and Windows MSVC. No platform tolerance is permitted.
  **The corpus immediately earned its place:** it caught a real bug in the
  128-bit-to-double conversion, which converted the two's complement limbs
  directly and so destroyed small negative values (-2000000 is hi = -1,
  lo = 2^64 - 2000000; lo loses its low bits at that magnitude and the limbs
  very nearly cancel). Fixed by converting the magnitude and reapplying the
  sign.

**Wall spans that slope closes (2026-08-26) — and an E1L1 baseline
correction.** An evaluated slope can close a span at one endpoint. The derived
shape is then a triangular WEDGE:

| endpoints | emitted |
|---|---|
| open at both | quad, two triangles |
| closed at exactly one | wedge, one triangle |
| closed at both | nothing |

Both failure modes are real and opposite: emitting a quad anyway staples a
zero-area triangle onto it, and dropping the span entirely throws away valid
geometry. Applied identically to SolidWall, PortalUpper and PortalLower.

Covered by `portal_slope_collapse`: a sloped central sector with three flat
neighbours whose BASE intervals are all open, so every case is produced by
slope evaluation rather than the flat decision. Its wall 0 stays an ordinary
quad; walls 1 and 3 run perpendicular to the hinge from opposite sides and
give upper and lower wedges closing at opposite endpoints; wall 2 runs
PARALLEL to the hinge, so both endpoints share a perpendicular distance and
its spans are omitted. Which endpoint closed is derived from the evaluator in
the test, not read off the emitted vertices. Negative-tested: restoring
unconditional quad emission trips the zero-area gate (`dot(n, n) > 0.0` →
`0 > 0`).

**This changed E1L1, and the previous baseline was the thing that was wrong.**
1936 → 1929 surfaces, 5134 → 5111 triangles. Enumerated exactly: **7 portal
spans closed at both endpoints** (sector[108] wall[499], sector[110]
wall[507], sector[127] wall[591], sector[263] walls[1442..1444], sector[269]
wall[1499]) and **9 spans closed at one endpoint**, now wedges. 7 × 2 + 9 = 23
triangles, and 5134 − 23 = 5111 exactly. So the accepted baseline had been
counting **23 zero-area triangles** — 14 from wholly degenerate spans, 9 from
spans carrying one degenerate face. Sector/wall counts, floors, ceilings,
solid spans and diagnostics are all unchanged; only degenerate portal geometry
went. The baseline is corrected rather than defended.

*Real-content scan (dev evidence, aggregate only):*

| map | sloped planes | stale heinum | max \|z−base\| | max negative Δz | surfaces | tris | diags |
|---|---|---|---|---|---|---|---|
| E1L1 | 69 | 22 | 24576 | 15360 | 1929 | 5111 | 0 |
| E1L2 | 67 | 7 | 44544 | 24576 | 1709 | 4649 | 0 |
| E1L3 | 252 | 57 | 40960 | 40960 | 3026 | 8190 | 0 |
| E1L4 | 278 | 287 | 60407 | 60407 | 3435 | 9189 | 0 |
| E1L5 | 302 | 49 | 152056 | 152056 | 3078 | 8723 | 0 |
| E1L6 | 206 | 27 | 78200 | 78200 | 1845 | 4741 | 2 |

Surface and triangle counts are post-wedge-rule; every map shed degenerate
portal geometry it had previously been counting.

Every map exercises substantial NEGATIVE deltas, which is why the scan is
meaningful evidence that the sign-conversion bug is gone: the same aggregates
were produced independently by the earlier exact-integer conversion path and
by the current magnitude-plus-sign one.

E1L1's note count fell 321 → 252 — exactly the 69 slope-deferral notes that
slope support retired. Its surface and triangle counts were 1936 / 5134 at
this point in the slice and moved to **1929 / 5111** once the wall-span wedge
rule landed (below); the accepted slope-aware baseline is
**317 / 1937 / 1929 / 5111 / 0, notes 252**. E1L6's 2 diagnostics are the
pre-existing zero-area sector, confirmed by building the same map on the
parent commit and getting the identical 1917/4894/2. **287 stale-heinum
planes in E1L4 alone** is why gate D is load-bearing rather than theoretical.

### [SUPERSEDED HISTORY] Slice 2A — the provenance stop, 2026-08-26

> **This section is HISTORY, not current state.** It records why slice 2A
> initially stopped before writing any code, and every claim in it about what
> is unimplemented — no `prepare_world`, no UVs, no shader, no D0020 — is
> **obsolete**. The stop was later lifted by ruling: isolating undocumented
> rendering constants cost more than the risk it controlled, and the
> compatibility gate became the rendered level. **Slice 2A was delivered,
> HUMAN-ATTESTED on E1L1, and ACCEPTED 2026-08-26; D0020 is accepted.** The
> binding description is in the accepted slice-2A section above and in
> AGENTS.md.
>
> It is kept because the reasoning is the evidence: the implementation waited
> for provenance, refused to compensate with a familiar formula, and only
> proceeded when a human ruling changed what counted as the gate.

**[HISTORICAL] STOPPED before any code.** The slice's own brief orders provenance first
for UV semantics and forbids compensating with a familiar formula. The
approved documentation search is exhausted and does not establish the exact
baseline scaling equations, so no `prepare_world` layer, no UVs, no shader,
no fixtures, and no D0020 landed. Delivered: the pre-flight documentation
fix (the stale "M6.1 evaluator deliberately not implemented" sentence in
RENDERING_CONTRACT.md — the surrounding text already recorded the accepted
evaluator), three new provenance rows for the documentation actually
consulted, and this record.

**Established from published, approved sources** (rows 9/10/11 previously;
row 16 added by this stop — rows 14/15 were withdrawn, see below):

- Field meanings and flag bits, including floor/ceiling swap-XY,
  expansion/smoosh, flips, first-wall-relative alignment and x/y panning,
  and wall x/y repeat, x/y panning, alignment and flips (ModdingWiki row 9).
**Provenance re-sourced 2026-08-26.** Rows 14/15 were reached by navigating a
forbidden-source repository. No code was read, but the boundary must be
mechanically checkable rather than resting on an agent's account of which
files it opened, so both rows are **withdrawn**. Re-sourcing followed the
ruling's A/B/C order, and the outcome splits the fact set in two.

Established, from clean sources only:

- Repeat is a pixel-size control — "Change pixel size to stretch/shrink
  textures" (**ModdingWiki MAP page, row 9, already approved**), and keypad
  2/4/6/8 "adjusts the repeats of the pixel columns/rows (effectively
  smooshes them together, or spreads them apart)" (**InfoSuite, row 16**).
- Panning is an alignment offset — "Offset for aligning textures" (row 9).
- A reset key exists: "/ - Resets a sprite's pixel repeats (size). Resets a
  wall's panning, pixel repeats (size), and cstat flags" (row 16). **It does
  not state the value reset to.**
- Wall orientation is a documented control: "O - Wall orientation. Changes
  whether the texture aligns to the ceiling or floor" (row 16). **Which
  alignment is the DEFAULT is not stated.**
- Floors/ceilings have an expand/compress control: "E - Expand/compress a
  ceiling or floor texture" (row 16), **with no dimensions given**.
- Floor/ceiling textures are anchored to ABSOLUTE world coordinates by
  default — "ceiling/floor textures remain stationary while sectors are
  moved", except under relative alignment (row 16).
- "Everything is measured in Z-units of 1024" (row 16) — a granularity fact,
  not a scale relation.

Withdrawn with rows 14/15 and **NOT usable**: the numeric default repeat = 64;
the "normal 64*64 area"; the by-2 E smoosh; square-aspect paste; and "Z
coordinates are all shifted up 4". Ken's canonical page publishes BUILDINF.TXT
only inside `BUILDSRC.ZIP`, so rule C applied and the archive was not opened.

> **The D0016 corroboration is withdrawn, not kept.** The 2^4 statement was
> genuinely useful, but its only source is a row we have just rejected, and
> keeping a fact while retiring its provenance is exactly the move this
> project does not make. **D0016 is unaffected**: it was ratified on our own
> black-box measurements over legally owned maps plus the Mapster32
> `metric_cube` attestation, none of which touched these rows. Nothing is
> reopened or redefined; the milestone simply loses a corroboration it never
> depended on. If the statement later appears in a cleanly published
> document, it can be re-added as corroboration then.

**NOT established by any approved source — the exact missing facts
(stop conditions, verbatim class):**

1. **Baseline world-coordinate → floor/ceiling texel scale.** How many
   world XY units one texel — or one whole tile — spans at default flags.
   No clean source states a texel size in world units at all.
2. **Wall world-length → texture-U relationship / xrepeat scaling.** The
   constant linking a wall's horizontal world length to texel count at a
   given xrepeat. Clean sources establish only that repeat changes pixel
   size; the equation and the constant are absent.
3. **Wall vertical coordinate → texture-V relationship / yrepeat
   scaling.** The same constant for Build Z at a given yrepeat, including
   how the ratified 16:1 vertical metric combines with it. No clean source
   addresses the interaction.
4. **Baseline orientation conventions.** Which direction U runs along a
   wall's directed A→B span; whether V counts down from the top (suggested
   by the `O`-key text, not stated as an equation); which world axes floor
   U/V follow and with which signs (world-anchoring is published; the axis
   assignment is not).

5. **The default repeat value.** Row 16 documents that a reset key exists
   but not the value it resets to. The number 64 came only from the
   withdrawn rows, so it is no longer established — and it was load-bearing
   for the whole hypothesis. **This is a regression in what we know**, and
   the experiment below must now measure the default rather than assume it.
6. **The default wall orientation.** Row 16 documents the `O` key and that
   it switches between ceiling and floor alignment, but not which is the
   default. Previously taken from the withdrawn rows.


Panning units were not needed: M6.2A is a zero-panning baseline slice.

**Why no formula was used.** Specific constants for units-per-texel at a
given repeat are engine-internal knowledge whose provenance is the forbidden
class (source ports, engine source, remembered implementations). Adopting
any of them unattested would be exactly the compensation the brief forbids;
the clean-room rule holds even where memory claims to agree.

*Hypothesis — WEAKENED 2026-08-26, and deliberately left weakened.* The
earlier reading (a 1:1 baseline: one texel per XY unit on 64-pixel floor
tiles; one XY unit horizontally and 16 Build Z units vertically at repeat 64)
rested on two facts that the provenance withdrawal removed — the "normal
64*64 area" wording and the numeric default repeat of 64. Without them it is
no longer "the most natural reading of the collected facts"; it is one
candidate among several, and the normalise-to-a-canonical-area model is
another that the surviving evidence does not exclude.
It is recorded here only so the experiment has something to falsify, and it
is **not** to be treated as a leading answer. Resolution is measurement, and
the measurements must now settle the default repeat as well as the scale.

*Candidate black-box experiment (HUMAN-ATTESTED; original synthetic content
only, needs no proprietary data):*

1. Generate at least two deliberately asymmetric original tiles (every
   corner/row distinct — no checkerboards) with the existing M4 tile
   tooling, and a flat synthetic room: floorz 0, ceilingz −16384
   (= 1024 XY-equivalent), one wall exactly 1024 XY units long,
   floorstat/ceilingstat = 0, wall cstat = 0, panning = 0, xrepeat/yrepeat
   = R = 64, a **fixed experimental repeat**, plus one wall variant each at repeat
   32 and 128.
2. Open it in Mapster32 (8-bit classic renderer) and observe.

> **On the number 64 in this experiment.** Wherever the boards below fix a
> repeat of 64, that is an **arbitrary controlled test value (R = 64)**, not
> an asserted default. The numeric default repeat was withdrawn with rows
> 14/15 and is now missing fact 5 — something the default-state oracle
> below *measures*. Nothing in the design may assume 64 is special.

*Controlled tile set (amended 2026-08-26 — the 64-only design was
confounded).* The original matrix measured a 64-pixel tile only, where two
incompatible models predict the **same** count:

| tile width | fixed texel/world scale | normalise every tile to the 64×64 area |
|---|---|---|
| **64 px** | 16 copies over 1024 units | 16 copies — **indistinguishable** |
| 128 px | 8 copies | 16 copies |
| 32 px | 32 copies | 16 copies |

And the second model is not a strawman: it is what the "smoosh any tile into
the normal 64*64 area" phrasing points at. Tile SIZE is therefore an
independent variable, not a constant:

| tile | pixels | isolates |
|---|---|---|
| **A** | 64 × 64 | baseline |
| **B** | 128 × 64 | HORIZONTAL model (A vs B) |
| **C** | 64 × 128 | VERTICAL model (A vs C) |

Every tile uses deliberately asymmetric indexed pixels — no pattern whose
halves, axes, or rotations can be confused, so a transpose or a U/V swap is
visible rather than inferred. **Record the exact pixel dimensions beside every
observation**; a copy count means nothing without them.

Across each comparison, hold fixed: wall geometry, wall length and height,
xrepeat and yrepeat, cstat, x/y panning = 0, camera and view, palette and
shade, and every other MAP field. **Only tile dimensions change.**

Repeat variation is retained as a *separate* axis: tile-size tests answer
normalisation, repeat tests answer repeat scaling. They are different
questions and neither substitutes for the other.

*Exact quantities to record,* with tile dimensions noted each time:

1. Tile copies spanning the 1024-unit wall horizontally, for **A, B and C**
   at a fixed xrepeat → the horizontal model AND the U texel XY size.
2. Tile copies spanning the 16384-Z height, for **A, B and C** at a fixed
   yrepeat → the vertical model AND the V texel Z size, including how it
   interacts with the 16:1 metric.
3. Tile copies spanning 1024 XY units across the floor, for **A, B and C**
   at default flags → whether floors normalise to a canonical area or scale
   with tile size.
4. Which tile edge leads at the wall's first (A) end → U direction.
5. Which tile row sits at the top of the wall span → V direction and anchor,
   and hence the default wall orientation (missing fact 6).
6. Which tile corner sits at the world origin on the floor → floor axis
   assignment.
7. The copy-count change at repeat 32 and 128, for tile A → linearity in
   repeat.
8. **The repeat value a freshly placed wall/sprite actually carries**, read
   from the editor before any adjustment → the default repeat (missing fact
   5), which is no longer established by any clean source and must now be
   measured rather than assumed.

### Slice 2A — textured presentation (D0020) — ACCEPTED 2026-08-26

The Mapster UV calibration programme was **abandoned by ruling**: isolating
undocumented rendering constants cost more than the risk it controlled. The
compatibility gate is now the rendered real level itself. The boards below are
retained as history only.

**D0020 prepared-world API** (accepted 2026-08-26) (`core/include/fauxbuild/prepared.hpp`):

    prepare_world(StructuralWorld, IndexedAtlas, PaletteData, UvConventions)
        -> Result<PreparedWorld>

Pure C++, no Godot type. `PreparedSurface` carries the structural vertices and
indices **verbatim**, one `PreparedUV` per vertex, the atlas page, and the
tile's page-normalised rect. UVs are **tile-local** (1.0 = one tile repeat,
values outside [0,1] are normal): the consumer wraps inside the tile's rect,
because wrapping across a whole atlas page would bleed neighbouring tiles.
That is why grouping is by (kind, picnum) — a rect is a per-group uniform.

**The one UV authority** is `core/src/prepared.cpp`, pinned by
`ci/check_layering.py`: the tokens `units_per_texel`, `units_per_tile`,
`wall_z_per_texel_v`, `reference_repeat` and `repeat_factor` may appear in no
other core or extension source, and `FauxBuildView` may not name
`prepare_world` or `UvConventions`.

**Provisional conventions — not proven, and not presented as proven.** All in
`UvConventions`, one edit each: floor 16 world XY units per texel; wall U 16
XY units per texel; wall V 256 Build Z per texel; reference repeat 64; U along
the wall's own A->B direction; V increasing downward in Build Z; floor U/V =
world X/Y unswapped. The 16 and 256 are consistent with each other under the
ratified 16:1 metric — 256 Z is 16 XY-equivalent — so texels are square by
construction; that consistency is a design choice, not evidence. The numeric
default repeat remains missing fact 5; `reference_repeat` is the point the
constants are stated at, not a claim about defaults.

**Indexed Godot path.** `FauxBuildView::present_prepared_world` uploads the
prepared arrays unchanged and computes nothing. The atlas page is
`Image::FORMAT_R8` — one palette index per byte, authoritative — sampled
`filter_nearest, repeat_disable`, with a 256x1 base-palette LUT. The fragment
stage is `fract(UV)` into the tile rect, index lookup, palette lookup.
`present_world(StructuralWorld)` is untouched and still available.

`atlas_page` carries **DATA and is deliberately NOT `source_color`**: that hint
asks the renderer to apply an sRGB transfer to what are palette INDICES,
producing almost-right ones — the worst failure mode, because the result still
looks like a texture and would survive a visual inspection. `palette_lut` IS
colour and keeps the hint. A gate pins the split in both directions.

**Resource reuse:** one `ImageTexture` per atlas PAGE and one shared `Shader`
for the whole presentation; only the per-group `ShaderMaterial` differs,
because `tile_rect` does. E1L1 is 173 groups over 3 pages, so per-group uploads
meant 173 copies of the same megabytes. A gate requires distinct page textures
to be fewer than groups and the shader count to be exactly one.

> The sharing fix did not take on the first attempt: the edit that was supposed
> to remove the per-group creation silently failed to match (clang-format had
> reflowed the target), so the shared objects were built and then ignored while
> the per-group site survived. The new gate caught it — `3 textures for 3
> groups`, `3 distinct shaders` — which is the whole point of adding it.

**Checked index packing**, at parity with `present_world`: a source index must
address its own `PreparedSurface`'s vertices, and `base + index` is validated
against Godot's index representation **before** narrowing to int32. Both checks
run while nothing in the scene has been touched, so a rejection leaves the
previous presentation intact.

**Production route.** `present_grp_textured` / `present_dir_textured` load
assets from the **same Vfs** the map came from, and remain transactional: the
view is handed a prepared world only after mount, parse, derivation, asset
load, atlas and preparation all succeed.

**Synthetic CI gates** (159 cases, was 152). Core: geometry passes through
preparation untouched; picnum resolves to the exact atlas tile (two fixture
tiles of different width, so a mixed-up resolution cannot pass by chance);
exactly one UV per vertex; UVs actually depend on the conventions; an unusable
picnum fails deliberately with no placeholder; the payload stays R8; wall U
spans the wall and V its height. Scene (`textured_boundary_test`): the
ArrayMesh (vertex, uv) pairs equal the prepared ones verbatim, the uploaded
texture is FORMAT_R8 at one byte per texel, sampling is nearest with no linear
filtering, and a failed textured load preserves the previous presentation.

**Sabotages observed red:** duplicate UV computation in the view → "the
ArrayMesh UVs are not the prepared UVs verbatim"; RGBA-authoritative atlas →
"atlas page must be FORMAT_R8, got 5"; `source_color` on the atlas sampler →
"atlas_page is indexed DATA and must not be source_color"; per-group page
upload → "each group uploaded its own atlas page (3 textures for 3 groups)".

> **Where sabotage 12 is caught, and why it matters.** Reordering structural
> vertices during preparation goes red in the CORE gate (159 → 158) and NOT in
> the scene gate. That is the correct division, not a hole: the scene's
> expected side is itself produced by `prepare_world`, so both sides move
> together and it structurally cannot detect a core defect. Only the core gate
> compares against the `StructuralWorld` directly. A scene gate that appeared
> to catch it would be the more worrying result.

**Human viewer.** One invocation, no auxiliary tooling:

```sh
scons config=dev extension
/Applications/Godot.app/Contents/MacOS/Godot --path godot \
  res://scenes/structural_view_human.tscn -- \
  --grp "$PWD/local_reference/duke/DUKE3D.GRP" --map E1L1.MAP --textured
```

Framing had to be fixed for this: `_bounds()` looked up fixed group names, and
textured groups are named `Floors_123_0`, so the AABB came back empty and
parked the camera at the origin. It now walks the view's actual children. In
textured mode the readability palette override and the 1-5 toggles are
disabled — overriding the material would hide the very thing under inspection.

**E1L1 (dev evidence):** 317 sectors / 1937 walls / 1929 surfaces / 5111
triangles / 0 diagnostics, presented as **173 textured groups**; bounds
52.5615 / 15.7500 / 33.9873, identical to the untextured baseline. Untextured
mode still presents its 5 groups.

**Slice 2A — HUMAN-ATTESTED PASS 2026-08-26** by mitchellcurrie, on untouched
E1L1 through `DUKE3D.GRP`. Baseline indexed presentation accepted: the level
is **immediately recognisable**, and global texture scale, orientation and the
indexed palette path are coherent.

What that attestation is worth, and what it is not:

- It settles the **global** conventions the M6.2A provenance stop could not.
  The scale constants, the U/V directions and the floor axis assignment in
  `UvConventions` produced a coherent real level on the first visual pass, on
  content with 173 distinct tiles and no map-, tile- or level-specific branch
  anywhere. That is a far stronger check than the abandoned Mapster
  measurement would have been: a wrong constant or a swapped axis cannot
  produce a recognisable level by accident.
- It does **not** promote those constants to *proven*. They remain
  provisional, still centralised in the one authority, and still labelled as
  such. What changed is that they are now corroborated by the only evidence
  the project ever accepted for rendering semantics — black-box observation of
  legally owned content — rather than resting on a plausible reading.
- **The residual is diagnostic, not a defect.** Visible local misalignment on
  authored multi-surface details (the vent being the clear case) is exactly
  what a baseline with panning, alignment and flips deliberately unimplemented
  should look like. That the errors are *local to authored detail* and not
  global drift is positive evidence that the deferral boundary was drawn in
  the right place: if the scale or orientation constants were wrong, the
  failure would be everywhere, not on the details that carry the unsupported
  flags.

**Slice 2A — ACCEPTED 2026-08-26** by mitchellcurrie; PR #9 merged with zero
unresolved threads and all ten CI checks green. D0020 ratified.

What the slice settled, and what it deliberately did not:

- The **global** UV model is right. A wrong scale constant or a swapped axis
  cannot render a recognisable level across 173 distinct tiles by accident, so
  the visual gate is stronger evidence than the abandoned per-constant
  measurement would have been.
- The constants are still **provisional**, still centralised, still a one-line
  change. Accepting D0020 accepted the architecture and the seam, not a claim
  that the historical constants are exact.
- **No more UV-scale archaeology.** The route that produced this slice — a
  provenance stop, a confounded experiment, withdrawn sourcing, and finally a
  ruling that replaced measurement with the rendered level — is closed. The
  remaining texture work is about authored controls, not about rediscovering
  engine constants.

### Slice 2B1 — authored texture placement — HUMAN-ATTESTED PASS 2026-08-28 (in review; not merged, not yet ACCEPTED)

Scope: **make authored texture placement behave properly — panning, flips, and
alignment flags.** These are the fields M6.2A preserved verbatim in
`SurfaceAppearance` and deliberately did not interpret.

The vent and similar authored multi-surface detail on E1L1 are the **human
regression symptom**, not a target. The rule is unchanged and matters here more
than anywhere: implement the generic authored controls, then reopen E1L1 and
see whether such details snap into alignment. If making one of them look right
needs a map-specific branch, a tile-ID exception, or a per-level tolerance, the
generic model is wrong and the slice stops.

**Aggregate real-content inventory (decides priority; AGGREGATE counts only —
no per-map proprietary extracts exist anywhere in the repo).** Scanned all six
owned maps through the core reader against the bits of PROVENANCE row 9
(floor/ceiling stat: swap-XY 0x0004, smoosh 0x0008, X-flip 0x0010, Y-flip
0x0020, relative 0x0040; wall cstat: bottom-align 0x0004, X-flip 0x0008,
Y-flip 0x0100), plus nonzero pan counts.

Read the columns carefully: every FLAG row counts the population with that bit
set, and those rows overlap freely. The PANNING row is different — it is a
**partition of the panned population into x-only / y-only / both**, so its
three numbers sum to the count with any panning at all and must not be read as
three independent "has x pan" / "has y pan" totals. (E1L1 planes: 39 + 59 + 71
= 169 panned of 634; E1L1 walls: 254 + 180 + 240 = 674 panned of 1937. The
independent totals are larger — 110 with x pan and 130 with y pan on planes —
because "both" belongs to each.)

| floors/ceilings (4900 planes; E1L1: 634) | six maps | E1L1 |
|---|---|---|
| swap XY | 1048 | 130 |
| smoosh (unsupported ledger) | 2621 | 378 |
| X flip | 1086 | 149 |
| Y flip | 1344 | 178 |
| relative alignment | 1254 | 119 |
| panning (x-only / y-only / both) | 436 / 255 / 538 | 39 / 59 / 71 |

| walls (15303; E1L1: 1937) | six maps | E1L1 |
|---|---|---|
| bottom alignment | 6452 | 796 |
| X flip | 312 | 62 |
| Y flip | 182 | 26 |
| bottom-align + Y-flip together | 15 | 0 |
| panning (x-only / y-only / both) | 2067 / 1014 / 2272 | 254 / 180 / 240 |

Every implemented control is materially exercised; nothing was implemented
merely because a bit exists.

**Implemented (all in the ONE UV authority, `core/src/prepared.cpp`):**

1. **Panning** — provisional generic convention per the brief: xpanning/
   ypanning are texel offsets within the selected tile, converted to
   tile-local phase (pan_u = xpanning/tile_width, pan_v = ypanning/tile_height);
   wrapping stays tile-local; ONE global sign choice
   (`UvConventions::panning_adds_phase`, true = adds phase) for every surface.
   Applied after flips, so a flip can never erase or double a pan.
2. **Flips** — floorstat/ceilingstat 0x0010/0x0020 and wall cstat 0x0008/0x0100
   NEGATE the position-derived tile-local coordinate (a mirror; the shader's
   `fract()` makes −u the exact mirror of u). Structural vertices and indices
   are never reordered — the verbatim geometry gate keeps teeth here.
3. **Floor/ceiling swap XY** (stat 0x0004) — exchanges the base (a,b) axes,
   applied to the world-anchored axes and to the relative frame alike.
4. **Wall top/bottom alignment** (cstat 0x0004) — V is anchored at the span's
   lowest edge instead of its upper edge, SAME vertical texel scale. On sloped
   and triangular-wedge spans the anchor is the span's own extreme vertex
   (one generic vertical-coordinate model; the exact historic phase at a
   sloped endpoint is undocumented, so the anchor convention is marked
   provisional). No flattening, no re-derivation.
5. **Floor/ceiling relative alignment** (stat 0x0040) — materially exercised
   (26% of planes), supported properly: U along the sector's first wall A→B,
   V along the left perpendicular of the U direction (the frame stays
   right-handed; both toggles live in `UvConventions`), origin at A.

**StructuralWorld widening.** Relative alignment needed the sector's exact
texture reference frame; `StructuralWorld::sector_frames` now carries it —
one entry per source sector: the first-wall index and the Build-space A/B
endpoints copied VERBATIM from MapData. It is a MAP geometry/reference fact,
not an asset and not a UV: the structural core still derives from MapData
alone and computes nothing about textures. A static layering gate forbids
`prepared.cpp` from reconstructing the first wall (`wallptr`/`wallnum`/
`point2` are banned there) and pins `sector_frames` filling to
`structural.cpp`. A sector whose relative flag is set but whose first wall is
missing/zero-length gets a `relative_alignment_no_frame` note from the build,
and the UV authority falls back to world axes — observed zero times across
the six owned maps (synthetic-only reachability, pinned by test).

**Zero/default equivalence.** With all placement fields zero/default,
`compute_uvs` executes the exact M6.2A arithmetic (the transforms are
bit-guarded on the authored values): 178 core cases including a hand-derived
M6.2A regression pin pass unchanged in dev, asan and release. The textured
scene boundary now exercises placement-carrying content (`write_placement_map`:
the same two_sector_portal geometry with pans, flips, swap-XY, relative and
bottom-align set; the view's consumer gate proved verbatim on those UVs).

**Provisional conventions introduced** (all centralised in `UvConventions`,
single-site edits, documented as provisional):
- `panning_adds_phase` — the global pan sign. HUMAN E1L1 review may reverse it
  if obviously wrong; that is a one-site edit.
- `floor_relative_u_follows_first_wall` and `floor_relative_v_is_left_perp` —
  the relative frame's orientation.
- Bottom-align anchor = the span's own extreme vertex on sloped spans.

**Unsupported placement bits (explicit ledger):**
- floorstat/ceilingstat bit 3, "double smooshiness" (0x0008): the approved
  provenance names the bit but establishes NO factor; the brief forbids
  guessing one. Inventory: 2621 of 4900 planes set across the six maps (378
  of 634 on E1L1), and non-64×64 tiles occur on both set (1671) and clear
  (1580) planes — so IF the HUMAN E1L1 gate shows an obvious systematic
  defect attributable to this flag, ONE explicit provisional factor lands in
  the one authority with human ratification (M6.2B2). Otherwise the ledger
  entry stands as-is.
- Wall cstat bit 1 ("bottoms of invisible walls swapped"), parallax (stat
  bit 0), masked/one-way/translucency bits: not texture placement of visible
  baseline surfaces — later M6 slices.

**Synthetic matrix and sabotages.** Core: zero/default = exact M6.2A values
(hand-derived oracle); floor X/Y/both pans on an asymmetric (128×64) tile;
axis independence; flip X/Y/both as exact negations; swap-XY as exact
transpose; flip+pan composition (pan independent of flip); wall pan; wall
X/Y flips; bottom alignment (bottom edge at phase 0, top at −4, U untouched)
and bottom-align + Y-flip; relative alignment against a hand-computed
45°-rotated room with convention toggles; degenerate-frame fallback with the
build note; sloped (ramp) and wedge (portal_slope_collapse) spans — UVs
change only, vertices/indices verbatim, zero diagnostics. Boundary:
ArrayMesh UVs == prepared UVs verbatim on placement-carrying content.
**Sabotages observed red:** (1) panning ignored → floor/wall pan gates;
(2) xpanning/ypanning transposed → asymmetric-tile phase gate;
(3) flip implemented by reordering vertices → the flip test's verbatim
vertex check; (4) bottom alignment ignored → bottom-edge phase gate;
(5) placement arithmetic computed in FauxBuildView → "the ArrayMesh UVs are
not the prepared UVs verbatim" (the scene boundary runs placement content
precisely so this has teeth); (6) M6.2A global scale mutated while
implementing a control → the regression pin. A stale-extension rebuild
(5) and a dangling-temporary use-after-free caught by asan were the two
real defects the red runs surfaced; both fixed.

**E1L1 before/after visual result: PENDING — the HUMAN gate has not run.**
Same command as M6.2A:

```sh
scons config=dev extension
/Applications/Godot.app/Contents/MacOS/Godot --path godot \
  res://scenes/structural_view_human.tscn -- \
  --grp "$PWD/local_reference/duke/DUKE3D.GRP" --map E1L1.MAP --textured
```

Success criteria (unchanged from the brief): global scale coherent, no
regression of ordinary surfaces, improved registration of authored local
detail, vent/panel-type details more coherent where MAP placement fields
explain the discrepancy, no map/tile-specific fix exists.

**Review closeout, 2026-08-28 (pre-human-gate).** Three findings from the
checkpoint review, all closed; no placement semantics were added, no global
UV scale touched, no smoosh support started.

1. **`prepare_world` now validates the per-sector tables before indexing
   them.** They are seam CONTRACT over caller-provided input, so an
   incoherent world returns a structured error (`invalid_topology` for a
   `sector_frames`/`sector_appearance` domain mismatch, `invalid_range` for a
   surface whose `sector` is negative or out of domain) and produces NO
   partial world — never an `FB_CHECK`, never a read past the end. The bug
   this closes failed SILENT-WRONG, not loudly: the unchecked `[]` read a
   null `sector_frames` (UBSan: "reference binding to null pointer of type
   'const StructuralSectorFrame'") and still returned ok with UVs built from
   garbage, which is why an error and not an assertion is the right shape.
   Seven gates, one per condition (grouping them under SUBCASEs would let the
   first `REQUIRE` failure mask the rest). Negative-tested by deleting the
   guard and running the ORDINARY non-sanitised dev suite: all six rejection
   gates fail as clean test failures — not sanitiser crashes, which would be
   evidence of the bug rather than of a working gate — and the valid
   boundary-index gate stays green, so the sabotage is discriminating.
2. **Usage-table columns disambiguated** (numbers unchanged): the flag rows
   overlap freely, the panning row is a partition into x-only/y-only/both.
3. **The layering grep's limit is now written down where it is used** — it
   sees named constants only, not a raw `0x0008`; the behavioural boundary
   test is the actual authority guarantee.

**Checkpoint reached.** Panning + flips + wall alignment + swap-XY + relative
alignment are delivered; the slice STOPS here per the brief, before the
ambiguous smoosh flag. Gate results: 178 core cases green on dev/asan/release,
fuzz corpus clean, layering + corpus + corpus-filter + fbtool + format + scene
gates green. Verified on real content: with every authored placement field
cleared, all 9046 E1L1 prepared UVs are bit-for-bit identical to the accepted
M6.2A output (raw float bits, not a tolerance); with placement honoured, 5118
of them move across 1166 of 1929 surfaces, no non-finite UV, and max |UV|
stays at the M6.2A value of 240.0 — so relative alignment introduced no
runaway frame origin. M6 remains IN_PROGRESS.

**Named attribution risks for the human gate** (so a defect is not blamed on
the wrong thing): the unsupported smoosh bit is set on 378 of 634 E1L1 planes,
though those same planes passed the M6.2A gate with it ignored; and 432 of the
796 bottom-aligned E1L1 walls are PORTAL walls, where the span is not the
whole wall, so the provisional "anchor at the span's own extreme" convention
is most visible there.

**Slice 2B1 — HUMAN-ATTESTED PASS 2026-08-28** by mitchellcurrie, on untouched
E1L1 through `DUKE3D.GRP`. Attested evidence:

- untouched E1L1 remains globally coherent;
- previously misregistered vent / multi-surface detail is **visibly much
  better** after generic panning/flip/alignment interpretation;
- ordinary floor and wall presentation did not globally regress.

What the attestation settles, and what it does not:

- It settles that the generic authored-placement model is **right in kind**.
  The M6.2A residual was predicted to be authored placement, and implementing
  the documented controls generically — no map branch, no tile-ID exception,
  no per-level tolerance — is what moved it. That the *same* details the
  M6.2A gate flagged are the ones that improved is the check: a wrong model
  would have improved nothing, or improved detail while breaking the bulk.
- It does **not** promote the provisional constants to proven.
  `panning_adds_phase`, `floor_relative_u_follows_first_wall`,
  `floor_relative_v_is_left_perp` and the bottom-align anchor convention
  remain PROVISIONAL, centralised in `UvConventions`, one-site edits. Human
  corroboration on owned content is the evidence class this project accepts;
  it is not a proof of the historic arithmetic.
- Neither named attribution risk above produced an attributable defect. The
  smoosh deferral and the span-relative portal anchor both survived the gate,
  so both ledger entries stand as written rather than being promoted to work.

**Named residual visual symptom — the cinema entrance.** One anomaly remains
and is recorded, NOT fixed: at the cinema entrance, original E1L1 shows a
bounded marquee/sign surface; FauxBuild instead repeats an unrelated-looking
base wall texture across the same rectangle.

- **Not attributed to M6.2B1 placement semantics.** Its failure shape is
  wrong for a panning/flip/alignment regression: the texture is *coherently*
  repeated, correctly registered as a repeat — it simply appears to be the
  wrong texture for that surface. A placement defect misregisters a texture;
  it does not substitute one.
- **Strong candidate: deferred semantics, not this slice.** Masked/one-way
  wall presentation and `overpicnum` selection are the first things to test.
  M6.2A preserves `overpicnum` verbatim and nothing consumes it, so a surface
  that should present its masked/over texture presents its base `picnum`
  instead — which is exactly this shape.
- **Binding constraint on whoever picks this up:** do NOT alter the global UV
  or panning conventions to make this look right, and do NOT add a map, wall
  or tile exception. Test the generic deferred semantics first. If closing it
  ever appears to need a per-surface exception, the model is wrong and the
  work stops — the same rule that governed B1.

Still deferred: masked/one-way walls, translucency, sprites, non-zero pal and
shade, visibility — M6.2B2 and later own those. Nothing here branches on a
map, a tile ID, or a level.

### [SUPERSEDED — the Mapster UV programme was abandoned 2026-08-26] M6.2A black-box boards — generated 2026-08-26, awaiting HUMAN attestation

All three boards and their assets are ORIGINAL synthetic content in `/tmp/uv/`,
produced with the existing M4 ART/palette tooling and scratch generators. No
production feature was added for the experiment, and no proprietary asset is
involved. Nothing is committed: these are inputs to a human observation.

    /tmp/uv/TILES000.ART   3 calibration tiles   /tmp/uv/PALETTE.DAT
    /tmp/uv/DEFPROBE.MAP   default-state oracle
    /tmp/uv/UVCALIB.MAP    tile-size board (A/B/C)
    /tmp/uv/REPCALIB.MAP   repeat board (tile A only)
    /tmp/uv/readwall       one-off field reader (not an fbtool feature)

**Calibration tiles.** picnum 0 = `calib_a` 64×64, 1 = `calib_b` 128×64,
2 = `calib_c` 64×128. Asymmetry comes from a checker whose square (24) does
NOT divide either dimension: the final band along +X and +Y is truncated
(64 = 24+24+16), so the narrow band marks the RIGHT and BOTTOM edges and
doubles as a visible tile-boundary marker for counting copies. Verified
numerically, not by eye — every tile is distinguishable under a horizontal
flip and under a vertical flip.

> **Stated limitation.** Tile A is transpose-symmetric: a square checker on a
> square tile always is. **A therefore cannot answer the axis-assignment
> question (observation 6), and is not asked to** — B and C answer it, because
> their differing aspect makes a U/V swap visible. A still answers the counting
> questions.

**Board 1 — default-state oracle (answers missing facts 5 and 6).** Exact
fields, not visual interpretation. `DEFPROBE.MAP` wall 0 is authored:

| field | authored |
|---|---|
| cstat | `0x0005` (the alignment bit `0x0004` + one other flag) |
| xrepeat | 37 |
| yrepeat | 91 |
| xpanning | 17 |
| ypanning | 23 |

*Human protocol:* open `DEFPROBE.MAP` in Mapster32; highlight that wall (the
south wall, running +X from the origin, and the only one using the 128×64
tile); press `/` **exactly once**; save to a NEW file in `/tmp`; exit. Then:

```sh
/tmp/uv/readwall /tmp/uv/<saved>.MAP 0
```

The reported `cstat`, `xrepeat`, `yrepeat`, `xpanning`, `ypanning` ARE the
answer — reset/default repeats, reset panning, and the default wall
orientation read from bit `0x0004`. No visual interpretation is permitted for
these facts.

**Board 2 — UV scale/orientation (`UVCALIB.MAP`).** Three adjacent 256×256
bays, floor-to-ceiling 4096 Z, viewable in one pass. **256 XY and 4096 Z are
equal physical extents under the ratified D0016 metric**, so equal copy counts
horizontally and vertically mean an isotropic texel and unequal counts
quantify the anisotropy directly. Every wall is identical except picnum:
cstat 0, panning 0, shade 0, pal 0, and xrepeat = yrepeat = **R = 64, an
arbitrary controlled test value**. Floors: identical 256×256 geometry,
floorstat 0, panning 0, only the tile changes.

Record — WALL: (1) horizontal copies on A/B/C; (2) vertical copies on A/B/C;
(3) which edge of the tile sits at the wall's first (A) end; (4) which row
sits at the wall's top. FLOOR: (5) copies over 256 XY on A/B/C; (6) which tile
corner/axes correspond to known world directions — **from B and C**.

**Board 3 — repeat scaling (`REPCALIB.MAP`), run separately.** Same three
bays, **tile A everywhere**, repeats 32 / 64 / 128. Tile size is constant here;
only repeat varies. Do not mix this with board 2: tile-size answers
normalisation, repeat answers repeat scaling.

**Report raw observations first.** No `prepare_world`, no UV function, no
shader, and no promotion into oracles until the observations have been
reviewed. **STOP if any observation remains compatible with more than one
model** — do not interpolate a familiar Build formula to close a gap.

*Do not promote any UV formula unless the measurements distinguish the
candidate models.* If A, B and C give counts consistent with more than one
model, that is a result to report, not to resolve by picking the reading that
looks familiar. Once attested, these numbers become exact integer CI oracles
in synthetic fixtures, and only then do the UV layer, the D0020 presentation
seam, the R8/palette shader path, and the negative/sabotage matrix land.

---

## SUPERSEDED HISTORICAL RECORD — M6.1 provenance STOP (2026-08-25)

> **Everything from here to the end of this M6 slice-1 section is HISTORY, not
> instructions.** It records the state of knowledge *before* the evaluator
> landed, and every claim in it about what is unimplemented, pending, or
> unresolved is **obsolete**. The evaluator was delivered and accepted on
> 2026-08-26; the binding description is above and in AGENTS.md. The Mapster
> black-box experiment described below was **abandoned**, its probe fixtures
> **removed**, and nothing here should be acted on. It is kept because how a
> question was settled — including a stop that turned out to be unnecessary —
> is evidence about the process, and deleting it would hide that the
> implementation waited for provenance rather than guessing.

**[HISTORICAL] STOPPED — slope evaluator not implemented (slice brief §3).**
At the time of writing, the exact evaluation equation was not provenance-safe
in the repository:

- *Published, approved (PROVENANCE row 9):* heinum is "rise/run; 0 =
  parallel to floor, 4096 = 45 degrees"; sector stat bit 0x0002 = sloped
  (n=4,900 corroboration, M3); floorz/ceilingz is the height "at first point
  of sector"; the CONVMAP7 note confirms heinum is zeroed when the slope bit
  is clear.
- *Not established by any approved source:* **(a)** the tilt axis/direction
  — world X, world Y, or first-wall-relative; **(b)** the sign convention —
  which heinum sign moves Z which way along it; **(c)** the evaluation
  equation and its rounding as an executable statement. The natural reading
  of "4096 = 45°" is the ratio heinum/4096 against horizontal Build units
  (z_delta = heinum·d/4096, exact integer arithmetic, 4096 = 2^12), but the
  page never states the equation, and without (a) the distance d has no
  defined axis. Source-port memory of the answer is exactly the input the
  clean-room rules forbid, so it is not used even where it agrees.

*Published hypothesis (hypothesis only):* for a flagged surface,
z(x,y) = z_anchor + heinum·d/4096 where z_anchor is the surface's base Z at
the sector's first point, d is signed horizontal Build distance along the
unknown tilt direction, int64 arithmetic widened before multiply, explicit
rounding at the divide (NUMERICS requires the rounding policy be named and
tested — itself an open black-box item).


**Visual protocol (no M32 scripting, no height readout).** For each file:
open it, enter 3D mode, and *without moving*, record which way the flagged
surface tilts relative to your spawn facing — rises ahead / behind / left /
right. Then note whether it tilts along the room's long axis or its short
one. That is the entire observation; four words per probe.

| record per probe | |
|---|---|
| high side | ahead / behind / left / right |
| axis | long (1024) or short (512) |

Read the four comparisons off the table afterwards:

| compare | holds fixed | tells you |
|---|---|---|
| `px` vs `py_ccw` | winding (CCW) | does the FIRST WALL set the direction? |
| `px` vs `px_cw` | first wall (+X) | does WINDING set it? |
| `px` vs `rx` | neither (both flip) | consistency check against the two above |
| `px` vs `neg` | everything but heinum sign | the sign convention |
| `px` vs `ceiling_px` | everything but which surface | does the ceiling match or oppose the floor? |

Only the first comparison changes → first-wall-relative. Only the second →
winding or surface normal. Neither → world-axis-aligned. **Both → none of the
simple candidates hold**, which is a result to report, not to resolve by
picking the reading that looks familiar.

*The four open questions,* and nothing more: (1) which direction the surface
tilts relative to the first wall; (2) whether winding changes that
relationship; (3) the heinum sign convention; (4) whether the ceiling uses
the same or the opposite convention as the floor. The rise magnitude is NOT
an open question — the published description already fixes 4096 as 45°
rise/run, and that becomes an exact integer CI oracle once the direction and
sign are attested.

*Do not promote a formula until the observations distinguish the
candidates.* Visual observations are
HUMAN-ATTESTED; once recorded, the numbers become exact integer CI oracles in
the probe fixtures (e.g. heinum 4096 ⇒ z delta exactly equal to horizontal
distance along the attested axis), and only then do the evaluator, the
geometry consumption, the shared-function tripwire, and sabotages 1–5 land.

M6 remains IN_PROGRESS. Slice 2 (textured surfaces + UV) is not started.

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

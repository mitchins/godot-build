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
      PENDING: no `local_reference/duke/DUKE3D.GRP` on this machine. Command to run:
      `./build/dev/fbtool dump-grp local_reference/duke/DUKE3D.GRP | head` (must list the
      container contents; nothing is extracted or committed).
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

## M3 — MAP v7 parser, validator, and writer — NOT_STARTED

Gate summary: all synthetic maps parse; parse→write→parse semantically identical; byte-identical
round-trip where promised; generated maps open in Mapster and Mapster saves re-parse (HUMAN-ATTESTED);
fuzz and malformed tests pass; local E1L1 reports plausible counts/start pose without fatal errors
(HUMAN-ATTESTED).
Do not implement rendering, collision, Duke tags, or game logic.

## M4 — ART, palette, lookup, and tile tooling — NOT_STARTED

Gate summary: fixture tiles decode exactly; palette test strip correct; pivot/animation
round-trip; local Duke tile atlas inspectable without extraction; no RGBA-only assumption;
original fixture ART works in Mapster.

## M5 — Static structural world viewer — NOT_STARTED

Gate summary: structural fixtures render with correct topology; holes/non-convex sectors render;
no persistent Godot scene becomes authority; local E1L1 loads as recognizable 3D shell (HUMAN-ATTESTED);
diagnostics instead of crashes. Allowed shortcuts: untextured diagnostic materials,
render-all visibility.

## M6 — Slopes, indexed textures, flags, and sprites — NOT_STARTED

Gate summary: slope query and render share one function; UV/sprite-flag/palette-shade matrix
fixtures pass; local E1L1 immediately recognizable; unsupported features listed explicitly.

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

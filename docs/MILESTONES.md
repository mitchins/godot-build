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

## M2 — Safe binary IO, VFS, and GRP — NEXT (active once a task names it)

Gate summary: synthetic GRPs enumerate/read exact bytes; truncated/corrupt cases fail safely;
duplicate/case behavior documented and tested; local untouched `DUKE3D.GRP` enumerates without
extraction (HUMAN-ATTESTED per D0009); no hard-coded proprietary filenames.

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

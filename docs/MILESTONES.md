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

---

## M0 — Contract and repository

Status: **IN_PROGRESS**
Started: 2026-08-21

### Scope

Repository skeleton; contract/provenance/decision files; `.gitignore` for local proprietary
data; coding style; SCons skeleton; empty fixture generator; `fbtool --version`; milestone
status format.

### Gate

- [ ] No Build/source-port code or proprietary data exists in history.
- [ ] `AGENTS.md` contains the clean-room and no-scope-creep rules.
- [ ] Native empty library/test/tool build succeeds.
- [ ] Dependency manifest is complete.

### Evidence

Tree listing, build commands, dependency list, first provenance review — attached at gate
review.

---

## M1 — Godot/GDExtension and Apple build smoke — NOT_STARTED

Gate summary: editor loads extension without warnings; desktop sample scene runs; macOS arm64
exported sample runs; iOS device/signed dev build launches blank scene with extension; no
engine fork required. **Stop condition:** if GDExtension packaging blocks iOS, solve it or
switch to a statically compiled Godot module before any engine code depends on the boundary.

## M2 — Safe binary IO, VFS, and GRP — NOT_STARTED

Gate summary: synthetic GRPs enumerate/read exact bytes; truncated/corrupt cases fail safely;
duplicate/case behavior documented and tested; local untouched `DUKE3D.GRP` enumerates without
extraction; no hard-coded proprietary filenames.

## M3 — MAP v7 parser, validator, and writer — NOT_STARTED

Gate summary: all synthetic maps parse; parse→write→parse semantically identical; byte-identical
round-trip where promised; generated maps open in Mapster and Mapster saves re-parse; fuzz and
malformed tests pass; local E1L1 reports plausible counts/start pose without fatal errors.
Do not implement rendering, collision, Duke tags, or game logic.

## M4 — ART, palette, lookup, and tile tooling — NOT_STARTED

Gate summary: fixture tiles decode exactly; palette test strip correct; pivot/animation
round-trip; local Duke tile atlas inspectable without extraction; no RGBA-only assumption;
original fixture ART works in Mapster.

## M5 — Static structural world viewer — NOT_STARTED

Gate summary: structural fixtures render with correct topology; holes/non-convex sectors render;
no persistent Godot scene becomes authority; local E1L1 loads as recognizable 3D shell;
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
areas; no map/tile/Duke-specific branches; no Godot physics bodies. **Kill gate:** if E1L1
only works through per-level tolerances, stop and fix the generic model first.

## M9 — Hitscan and line of sight — NOT_STARTED

Gate summary: nearest-hit matrix passes; portal-chain traces pass; sprite orientation matrix
passes; cross-platform traces match; plausible hits throughout local E1L1 traversal.

## M10 — Portal-aware visibility and production renderer — NOT_STARTED

Gate summary: visibility-cycle fixture terminates and renders; no through-wall leakage;
masked/translucent regression passes; stable pacing on desktop and iOS device; no avoidable
per-frame heap churn; renderer/core revisions synchronized.

## M11 — Generic movers and mutable world — NOT_STARTED

Gate summary: dynamic fixtures pass; elevator carries player and sprites; rotating/translating
sectors update rendering/collision/hitscan/membership together; blocked/crush reported without
game damage; no Duke effector/tag semantics.

## M12 — Engine conformance freeze — NOT_STARTED

Acceptance procedure and gate per plan §15/M12. Result: tag `fauxbuild-core-v0.1`.

## M13 — Original game framework — NOT_STARTED

Gate summary per plan §15/M13.

## M14 — First game vertical slice — NOT_STARTED

Gate summary per plan §15/M14.

## M15 — Apple/mobile hardening and production tooling — NOT_STARTED

Gate summary per plan §15/M15.

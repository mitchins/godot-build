# AGENTS.md — Coding-agent operating rules (FauxBuild)

This file is binding for every coding agent (Codex, Qwen Coder, or other) working in this repository.
The full implementation contract is `docs/PROJECT_CONTRACT.md` and the source plan
`FauxBuild_BUILD_to_Godot_Implementation_Plan.md` (kept outside the repo).

## Active milestone

**M5 — Static structural world viewer: IN_PROGRESS (slice 1 delivered at checkpoint;
slices 2–3 pending).**

M4 (ART, palette, lookup, tile tooling and the indexed atlas) was **ACCEPTED
2026-08-24**: all six gate items met, D0013/D0014/D0015 ratified, and two
HUMAN-ATTESTED results that automated evidence could not have produced — real
`DUKE3D.GRP` traversing GRP -> VFS -> 13 ART files + palette + lookup ->
indexed atlas with no extraction step, and FauxBuild-generated ART consumed by
Mapster32 (which independently decoded picnum 12 as 64x16, pivot 0,-8,
confirming our signed picanm encoding rather than agreeing with our own
reader).

M5's asset-side contract is settled and narrow. It asks the atlas for a picnum
and receives page + rect, dimensions, pivot, picanm and indexed texels. It does
**not** rediscover assets, decode palettes, reason about ART file ordering, or
care which atlas page a tile landed on. The question M5 answers: can untouched
E1L1 sector/wall/sprite topology be made visibly recognisable in Godot with no
Duke-specific rendering hack?

## Repository boundary and evidence classes

- This is the **engine repo** (public tech demo). The original game lives in a separate
  repository consuming this one; no game code is written here, and `godot/game/` holds
  engine sample/test content only (D0008). This repo's scope ends at the M12 tag
  `fauxbuild-core-v0.1`.
- Gate evidence is either **CI** (synthetic fixtures; must be sufficient alone) or
  **HUMAN-ATTESTED** (local proprietary GRP/E1L1 items, physical-device launches) — recorded
  as such in `docs/MILESTONES.md`; never mixed, never substituted (D0009).

## Clean-room rules (non-negotiable)

1. Do **not** fetch, read, summarize, or paraphrase code from: Ken Silverman's Build source,
   EDuke32, JFBuild/JFDuke, Chocolate Duke, any game source release, decompilations,
   disassemblies, leaked source, or any code whose provenance cannot be established.
2. Do **not** ask "how does X source port implement Y". Derive behavior from published
   binary-format descriptions, our own black-box observations, and general geometry.
3. Do **not** commit proprietary assets (maps, tiles, palettes, audio, screenshots, extracted
   content, hash-derived caches). `local_reference/` is gitignored and stays that way.
4. Do **not** implement Duke Nukem 3D semantics: no CON, no effectors, no Duke tags, no
   map-name or tile-ID special cases.
5. Do **not** add a map-specific branch, tolerance, or patch to make a particular map work.
6. Every behavior change requires a fixture, test, or trace in the same change set.
7. If the specification is ambiguous: **stop and report**. Do not guess silently.

## No scope creep

- Implement only the active milestone and the bounded subtask named in your task.
- Do not pre-build future systems (rendering before M5, collision before M8, game logic before M13).
- Do not add dependencies without a license entry in `docs/DEPENDENCIES.md` and a provenance
  entry in `docs/PROVENANCE.md`.
- Do not add fallback Godot physics, navmeshes, or scene-authoritative world state at any point.
- Update documentation in the same change where behavior becomes contractual.

## Layering

- `core/` must never include Godot headers or types.
- `extension/` is the only place godot-cpp appears (from M1).
- All tools link `libfauxbuild_core`; never duplicate parsers.
- Game logic does not belong in this repository (D0008).

## Build and verify commands

```sh
git submodule update --init --recursive   # once, after clone (godot-cpp)

scons config=dev          # build libfauxbuild_core, fbtool, fauxbuild_tests
scons config=dev check    # run headless tests + fbtool smoke + layering guard
scons config=asan check   # sanitizer build + tests (where supported)
scons config=dev format-check   # clang-format dry-run (skips with warning if not installed)
scons config=fuzz fuzz          # bounded fuzz run over committed corpus (D0010)
./build/dev/fbtool --version

# GDExtension (installs into godot/bin/):
scons config=dev extension                           # macOS debug (editor/dev runs)
scons config=dev extension target=template_release   # macOS release (exports)
scons config=release extension platform=ios target=template_release  # iOS static package
```

There is no separate lint command yet; `-Wall -Wextra -Werror` is enforced by the build.
Formatting follows `.clang-format`: run `clang-format -i <file>` before submitting
(install with `brew install clang-format`), and `scons config=dev format-check` verifies.

## Task protocol (plan §16)

Every task must: name exactly one milestone and a bounded subtask; read this file; read the
active milestone; inspect current tests/status; state the smallest implementation slice; flag
provenance/dependency concerns before coding. Every completion report must end with the
structured report format in `docs/MILESTONES.md`, including the line:

```text
Next milestone work was not started.
```

A milestone is accepted only by human review against its gate checklist, never by agent
self-declaration.

# FauxBuild

A clean-room, Build-format-compatible world runtime hosted by Godot.

FauxBuild consumes Build-style content (MAP v7, ART tiles, palette/lookups, GRP or directory
mounts) in a pure C++ core that owns all world topology, collision, traces, and mutations.
Godot handles presentation, input, audio, UI, and platform export through a GDExtension.

## Status

Milestone **M0 — Contract and repository** (see `docs/MILESTONES.md`).

## Repository map

- `core/` — `libfauxbuild_core`, pure C++20, no Godot dependency.
- `extension/` — godot-cpp GDExtension (populated from M1).
- `tools/fbtool/` — CLI validator/dumper/probe tool.
- `tests/` — unit, property, fuzz, trace, and render-regression suites.
- `fixtures/` — original synthetic maps/art/palettes; `fixtures/generated/` is tool output.
- `godot/` — Godot project (runtime host, game scripts).
- `docs/` — contract, provenance, milestones, and subsystem contracts.
- `local_reference/` — gitignored, developer-only proprietary stress data. Never committed.

## Build

Requirements: Xcode command-line tools (clang), SCons 4.x, Python 3.

```sh
scons config=dev          # build everything
scons config=dev check    # run headless tests
./build/dev/fbtool --version
```

Godot 4.7.2 stable is the pinned engine (from M1).

## Rules

Read `AGENTS.md` before touching anything. Clean-room policy: `docs/PROVENANCE.md`.

# FauxBuild Project Contract

Condensed binding contract. Authoritative detail lives in the implementation plan sections
noted inline. Changes to this contract require a decision record (`DECISIONS.md`).

## Product definition

FauxBuild is a clean-room world runtime that consumes Build-compatible content and preserves
the world representation, constraints, and observable behavior that cause designers to think
like Build designers. It recovers the creative machine — sectors, walls as linked 2D edges,
derived floors/ceilings, height-and-slope verticality, portal adjacency, sprites as the
universal fake-3D object, mutation-driven moving geometry, palette/shade lighting, and strict
budgets — not nostalgia through shaders.

## Strict initial profile: `FAUXBUILD_CLASSIC_V7`

| Property | Value |
|---|---|
| map format | MAP v7 |
| max sectors | 1024 |
| max walls | 8192 |
| max sprites | 4096 |
| world primitives | sector / wall / sprite |
| room over room | representation-level illusions only |
| world textures | indexed tiles |
| world lighting | palette / lookup / shade / visibility |
| collision, traces | FauxBuild custom |
| world authority | FauxBuild core |

No "Plus" profile during the engine proof. A future profile may raise limits but must not
contaminate the strict profile.

## Authority

FauxBuild owns: topology, native coordinates/angles, mutable sector/wall/sprite arrays, sector
membership, floor/ceiling queries, slopes, collision, movement resolution, vertical clearance,
sprite blocking, hitscan, line of sight, portal visibility input, mover mutations, stable IDs,
world serialization.

Godot owns: lifecycle, window/display, input collection, audio, UI/HUD, game-state
orchestration, gameplay scripts, platform APIs, rendering resources/shader execution,
debug/editor presentation. Godot may display FauxBuild data; it may not silently modify a
parallel copy.

## Allowed modern liberties (plan §1.3)

GPU rendering; resolution/presentation modes; 60 Hz fixed simulation with render
interpolation; modern input/accessibility; modern positional audio; safe C++ parsing; wider
intermediate arithmetic; threaded loading/prep; modern save files; editor overlays and tooling;
high-res HUD/menus; startup caches derived from content; platform-native export via Godot.

## Prohibited in the strict profile (plan §1.4)

Godot physics bodies/PhysicsServer as world authority; navmeshes as primary AI spatial model;
imported 3D meshes as world geometry; unrestricted stacked geometry or arbitrary true
room-over-room; PBR as default visual model; dynamic lights replacing palette/shade;
hand-authored Godot nodes altering topology; invisible map/tile/level compatibility patches;
duplicated world state between FauxBuild and Godot; `.tscn`-converted maps treated as
canonical.

## Non-goals for engine v0.1 (plan §1.5)

Duke actors/weapons/inventory/switches/effectors/tags; CON compatibility; Duke save/demo
compatibility; other Build-engine game dialects; network play; exact Build bugs unless they
materially affect maps or feel; exact software-renderer output; a full Mapster replacement;
mirrors/voxels/HRP/Polymer unless the original game later requires them.

## Engine acceptance test (decisive)

Mount an untouched legally owned `DUKE3D.GRP`, load `E1L1.MAP`, use the map's original start
pose, and traverse the static world shell with a generic player — without conversion,
map-specific patches, Duke code, or tile-specific compatibility logic. Dynamic doors,
elevators, and rotating sectors are proved separately in original synthetic fixtures. After
this passes (M12), engine archaeology stops.

## Layering

```text
fauxbuild_core     pure C++20, no Godot headers, deterministic headless tests
fauxbuild_godot    godot-cpp GDExtension adapters + rendering backend
game               GDScript, original rules/UI/audio, commands FauxBuild
tools              CLI validators/dumpers/probes linking fauxbuild_core
```

No public C ABI until a second genuine consumer requires it.

# AGENTS.md — Coding-agent operating rules (FauxBuild)

This file is binding for every coding agent (Codex, Qwen Coder, or other) working in this repository.
The full implementation contract is `docs/PROJECT_CONTRACT.md` and the source plan
`FauxBuild_BUILD_to_Godot_Implementation_Plan.md` (kept outside the repo).

## Active milestone

**M6 — Slopes, indexed textures, flags, and sprites: IN_PROGRESS.**

**Slice 1 (slope authority + appearance contract) delivered at checkpoint.**
The raw renderer-facing appearance contract landed on `StructuralSurface`
(picnum/overpicnum/raw stat/shade/pal/panning/repeat, preserved verbatim,
nothing interpreted — no UVs yet), the flag rule is pinned (heinum without
the sector slope bit is an ignored leftover, geometry stays perfectly
flat), and the structural core is pinned away from the atlas/asset headers.
**M6 slice 1 is DELIVERED and ACCEPTED (2026-08-26).** `surface_z_at` is the
one authoritative slope evaluator; nothing else in the derivation may compute
a slope, and a static tripwire pins `heinum` to its region. The sector's FIRST
WALL A->B is the hinge, the plane passes through it at the surface's base
floorz/ceilingz, and `stat & 0x0002` — never the heinum alone — activates it:

    perp = cross(B-A, P-A) / |B-A|
    z(P) = base_z + perp * heinum / 256

The sign is the 2D cross product of the directed first wall with (P-A). That
is the **current documented compatibility convention**, and a global reversal
would be a one-line change plus fixture updates, not an architectural one.
Winding is not an additional factor. The `/256` is derived: 4096 is a 45
degree rise/run, and under the ratified 16:1 metric a run of d Build XY units
is 16d Build Z units.

A flagged plane whose first-wall hinge has zero length has no defined slope
plane. It is never flattened as a substitute (**D0019**): a
`slope_hinge_degenerate` diagnostic is recorded, geometry whose placement
depends on that plane is omitted, and independently derivable geometry is
retained — an ordinary flat ceiling above an undefined sloped floor survives.

Slope changes wall spans, not just vertices: a span an evaluated plane closes
at ONE endpoint becomes a triangular wedge (one triangle), and one closed at
BOTH endpoints is omitted entirely. No zero-area triangle reaches a consumer.

**M6.2A — baseline indexed-texture presentation: DELIVERED and ACCEPTED
2026-08-26.** **D0020 is accepted.**

- `prepare_world(StructuralWorld, IndexedAtlas, PaletteData, UvConventions)`
  is the pure-C++ geometry+asset seam. No Godot type appears in it.
- `StructuralWorld` remains **asset-free**; the dependency runs one way.
- `PreparedWorld` owns the prepared UV and material facts: geometry verbatim,
  one UV per vertex, atlas page and tile rect per surface.
- `FauxBuildView` **consumes** those facts rather than interpreting any Build
  UV semantics — it computes no UV, resolves no picnum, reads no appearance
  field. `ci/check_layering.py` pins that.
- The indexed atlas stays **R8 data**: one palette index per byte, uploaded as
  `FORMAT_R8`, sampled nearest. The atlas sampler is deliberately NOT
  `source_color` (that hint would apply an sRGB transfer to indices).
- The **base palette path is live** (256×1 LUT); pal and shade stay at their
  baseline for this slice.
- **E1L1 is HUMAN-ATTESTED recognisable** through the production GRP/VFS
  textured route, with coherent global scale, orientation and palette.

**The world↔texel scale and orientation constants remain PROVISIONAL generic
compatibility conventions.** Accepting D0020 accepted the architecture and the
seam — not a claim that those historical constants are proven exactly. They
live in `UvConventions`, are applied only in `core/src/prepared.cpp`, and a
change is a single-site edit. No map-, tile- or level-specific value is
permitted anywhere.

**M6.2B1 (panning, flips, swap-XY, relative and wall top/bottom alignment) is
ACCEPTED 2026-08-28**, HUMAN-ATTESTED on untouched E1L1 and merged as PR #10.
All interpretation stays in the
one UV authority (`core/src/prepared.cpp`); `UvConventions` gained three
provisional toggles (pan sign, relative-frame orientation x2). Relative
alignment consumes `StructuralWorld::sector_frames` (first-wall A/B copied
verbatim by the derivation; `prepared.cpp` may not reconstruct it). The
smoosh bit (floorstat/ceilingstat 0x0008) stays UNSUPPORTED — no approved
factor exists; it lands only with a human-ratified provisional factor.
Zero/default placement remains byte-equivalent to the accepted M6.2A UVs —
verified on real content, not only fixtures: with placement cleared, all 9046
E1L1 prepared UVs are bit-for-bit identical to the M6.2A baseline.

**Named residual after the B1 human gate — the cinema entrance.** Original
E1L1 shows a bounded marquee/sign surface there; FauxBuild repeats an
unrelated-looking base wall texture across the same rectangle. It is NOT
attributed to B1 placement semantics: the texture is coherently repeated and
correctly registered, it simply looks like the wrong texture for the surface —
a placement defect misregisters a texture, it does not substitute one. Strong
candidate: deferred masked/one-way wall presentation and `overpicnum`
selection (preserved verbatim since M6.2A, consumed by nothing). Whoever picks
it up: test those generic deferred semantics FIRST, and never alter the global
UV/panning conventions or add a map, wall or tile exception to make it look
right.

**M6.2C1 (masked portal layers, `overpicnum` selection and the D0021 indexed
cutout) is HUMAN-ATTESTED PASS IN SCOPE 2026-08-28 on untouched E1L1 and is IN
REVIEW — not merged, not yet ACCEPTED. The cinema marquee artwork is now
visibly present; that is the oracle.** Residual, NOT this slice's: the marquee
repeats far too many times — a generic wall repeat-scale question for M6.2C1c.
Do NOT change PortalMasked geometry, overpicnum selection, the cutout
sentinel, culling or B1 placement to fix repetition; those are attested
correct. `SurfaceKind::PortalMasked` is a distinct sixth kind spanning
the portal opening (same plane evaluations and zero-area protections as
upper/lower; quad/wedge/omit), derived only in the structural core from the
masked bit (cstat 0x0010). `prepare_world` owns THE effective-tile selection:
`PortalMasked -> overpicnum`, everything else `picnum`; **overpicnum == 0 is
tile 0**, never a sentinel, and a nonzero overpicnum alone selects nothing
(60 of 79 nonzero carriers on E1L1 are not masked). The view reads no
appearance field (layering pin). E1L1 counts moved 1929/5111/252 →
1948/5149/233, exactly +19 masked quads (all 19 masked walls open at both
endpoints) and −19 obsolete deferral notes — full ledger in MILESTONES.
DEFERRED: one-way (0x0020; no positive rule in approved provenance —
inventoried, pinned uninterpreted), cstat translucency, sprites, smoosh.
(Transparent-texel discard is NO LONGER deferred — slice 2C1b implements it
under D0021; the text above describes C1 before that landed.) The B1-era
cinema paragraph above was the oracle for the gate, never a target, and the
gate has since PASSED. **Pre-gate correction (accepted):** paired
masked sides stay TWO coincident opposite-wound surfaces with their own
placement — never deduplicated — and their TEXTURED presentation uses a
second shared `cull_back` shader variant so the pair cannot z-fight;
ordinary groups keep `cull_disabled`; two shared shaders total, never per
group (sharing gate pins it).

Binary indexed CUTOUT landed in M6.2C1b (D0021, sentinel index 255):
cutout-enabled surfaces discard sentinel texels, decided on the R8 INDEX
before any palette lookup — never on RGB, since 245 shares the colour and is
NOT transparent. Sprites, one-way walls, cstat translucency and smoosh are
later M6 slices; visibility is M10.

**M5 — Static structural world viewer: ACCEPTED 2026-08-25.** All three
slices accepted. Slice 1: pure-C++ structural derivation from authoritative
MAP topology; D0016/D0017/D0018 accepted (D0016 as amended — Build Z is 16x
the horizontal unit scale, `render.y = -build.z * scale / 16`); E1L1
HUMAN-ATTESTED (1936 surfaces / 5134 triangles / 0 diagnostics — the
flat-preview figures, correct for M5; see the slope-aware baseline below).
Slice 2:
`FauxBuildView.present_world(const StructuralWorld&)` is the production seam
(C++-only, not ClassDB-bound): the view packs accepted surfaces into five
diagnostic ArrayMesh groups — a copier/packer that never re-derives,
reorders, or transforms geometry; the boundary test reads the actual
`MeshInstance3D.mesh -> ArrayMesh.surface_get_arrays()` arrays; the layering
guard pins the view to `structural.hpp` alone. Slice 3: `FauxStructuralSource`
owns the real-content route (mount → Vfs → MAP parser →
`build_structural_world` → view seam, transactional stage-tagged failures);
synthetic CI drives the identical route through both mount kinds with
corruption tripwires; the human viewer is the only place real content may
enter. **Slice 3 HUMAN-ATTESTED PASS 2026-08-25**: untouched E1L1 through
`DUKE3D.GRP` presented 317 sectors / 1937 walls / 1936 surfaces / 5134
triangles / 0 diagnostics; the 16:1 vertical metric was independently
corroborated in Mapster32 against the original synthetic `metric_cube`.
Those surface/triangle figures are M5's flat-preview result and remain
correct for M5 — slopes did not exist yet. The **current slope-aware
baseline is 317 / 1937 / 1929 / 5111 / 0, notes 252** (M6.1, accepted
2026-08-26).
Slopes landed in M6 slice 1: one authoritative evaluator (`surface_z_at`),
first-wall hinge, activated by stat 0x0002, sign from the directed hinge's 2D
cross product, `heinum/256` from the 16:1 metric. A plane with no usable hinge
is diagnosed and its dependent geometry omitted, never flattened (D0019).
Baseline indexed textures landed in M6.2A (accepted; see above).
Panning/alignment/flips (M6.2B1) are ACCEPTED. Masked portal layers and
`overpicnum` selection (M6.2C1) plus the binary indexed CUTOUT (M6.2C1b) are
HUMAN-ATTESTED PASS IN SCOPE 2026-08-28 and in review (see above).
**M6.2C1c is next: generic wall REPEAT semantics** — the marquee repeats far
too many times, which is a repeat-scale question about the provisional UV
conventions, not a masked-layer one. Do not start it before C1 merges. The cutout sentinel is palette index **255**, ratified as
D0021 from black-box measurement over owned content: it is an INDEX semantic
decided on the authoritative R8 index BEFORE any palette lookup, never on RGB,
because entries 245 and 255 are both exact full magenta and **245 is NOT
transparent**. Cutout is a property of the SURFACE, never the tile — owned
content uses the same tile as a masked overlay and as an ordinary opaque wall.
Sprites, one-way walls, cstat translucency and the smoosh bit are not
implemented; visibility (M10) is not started. The 79/19 overpicnum trap is now load-bearing and pinned: a nonzero
`overpicnum` is not by itself a licence to present it — the masked bit
selects the layer.

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

   **1a. ACCEPTED — human ratification 2026-08-26.**
   Forbidden-source repositories are forbidden **navigation surfaces**, not just forbidden
   code inputs. Do not clone, browse, fetch, or open files from Build/EDuke32/JFBuild/
   JFDuke/Chocolate Duke/game-source repositories — **including documentation files stored
   inside them**. Published documentation must be obtained from the author's or publisher's
   canonical documentation location, or from an approved independent documentation source.

   If the only available copy of a document requires entering or downloading a mixed
   source repository or archive, **stop and mark the fact unapproved**. Do not weaken the
   boundary to obtain it. An author's official pages may be cited to establish authorship
   or origin, but bundled source must not be inspected to recover missing prose.

   Ratified 2026-08-26 by mitchellcurrie, on the M6.2A closeout that exposed the gap.
   Rationale: rule 1 as written prohibits reading *code*, which left "documentation stored
   inside a forbidden repository" to an agent's judgement. That makes the boundary depend
   on an assertion a reviewer cannot check, when the same facts are usually obtainable from
   a clean origin. This is a policy clarification, **not** a finding that any agent copied
   code — M6.2A's rows 14/15 disclosed their route accurately and were withdrawn on the
   route alone (docs/PROVENANCE.md).
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

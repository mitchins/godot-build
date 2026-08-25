# Rendering contract

Becomes binding at M5/M10 (first binding content at M5 slice 1). Rules fixed by plan §9:

- The renderer consumes FauxBuild topology; it never converts a map into an independently
  editable Godot level, and no generated scene becomes world authority.
- Pipeline: camera pose in Build space → portal-aware visibility → visible
  sectors/wall-spans/sprites → cached static geometry + dirty dynamic rebuilds → indexed tile
  sampling → palette/lookup/shade/visibility shader → Godot viewport.
- Structural geometry (M5 slices 1–2, D0016 accepted): MapData → deterministic
  `StructuralWorld` in core/ (pure C++, exact integer predicates) → the Godot
  adapter consumes render-space vertices verbatim. Rebuilding the world from
  MapData alone is always possible; meshes/scenes built from it are disposable
  presentation. Surfaces: floor, ceiling, solid_wall, portal_upper,
  portal_lower — portal walls never emit a full quad across the opening.
  Ceilings wind opposite floors for back-face culling. The single
  Build→render conversion is `fauxbuild::to_render_space` (x, -z, y;
  power-of-two scale); no consumer may invent its own transform.
- Structural presentation seam (M5 slice 2, proven): `StructuralWorld` →
  `FauxBuildView.present_world` (C++ seam; the world never travels through
  GDScript) → one MeshInstance3D + ArrayMesh triangle surface per non-empty
  SurfaceKind (Floors, Ceilings, SolidWalls, PortalUpper, PortalLower) with
  vertices appended in canonical order, indices with checked accumulated
  offsets, no welding/reordering/normals/UVs, and only float32 packaging of
  the core doubles. Flat unshaded two-sided diagnostic materials. Presenting
  a world discards and deterministically recreates the presentation; the
  scene gate reads the actual `MeshInstance3D.mesh` →
  `ArrayMesh.surface_get_arrays()` arrays to prove it. Textures, UVs and
  lighting semantics are M6; visibility is M10.
- Real-content entry seam (M5 slice 3, proven): `FauxStructuralSource` owns
  source-side operation only — GRP path or loose directory (core mounts, no
  extraction) → VFS lookup of the MAP name → core MAP reader → core
  structural derivation → `FauxBuildView.present_world`. The source never
  renders, never exposes MapData as script-authoritative state, and fails
  transactionally (stage-tagged errors; a failed load replaces nothing).
  The source path is never authority: generated Godot state remains
  disposable and re-presenting from the source recreates the shell.
- One authoritative slope evaluator feeds floor/ceiling rendering, grounding, clearance,
  hitscan, sprite placement, movers (plan §8.3). **M6 slice 1 checkpoint (2026-08-25): the
  evaluator is deliberately NOT implemented.** The approved published description fixes the
  heinum scale (rise/run, 0 = flat, 4096 = 45°) and the slope flag (sector stat 0x0002), but
  not the tilt axis/direction, the sign convention, or the evaluation equation — implementing
  those from memory would violate the clean-room rules. Sloped-flag sectors therefore still
  render flat at their base Z with the explicit deferral note (M5 allowance, carried); a
  nonzero heinum with the flag clear remains an ignored leftover. Resolution awaits the
  human black-box Mapster32 experiment on the original `slope_probe_*` fixtures recorded in
  MILESTONES.md. When the evaluator lands: structural geometry places every sloped
  floor/ceiling vertex — and every wall endpoint adjacent to a sloped surface — through the
  same Build-space evaluation, converting to render space only afterwards; a static tripwire
  pins the evaluator as the sole slope call site.
- Appearance data contract (M6 slice 1, delivered): every emitted StructuralSurface carries
  raw MAP appearance facts verbatim — picnum, overpicnum (walls), the raw stat word
  (floorstat/ceilingstat or wall cstat), shade, pal, x/y panning, x/y repeat (walls). No UVs,
  no flag behaviour, no interpretation; heinum and tags are not appearance. The M6.2 seam is
  defined but NOT implemented: StructuralWorld (geometry + raw appearance) + IndexedAtlas /
  AssetSet (tile dimensions, indexed texels) → UV/material packing → Godot ArrayMesh +
  indexed shader. Geometry and appearance derive from MapData alone; the asset side meets
  them only at that seam (pinned by the layering guard), and the seam must not change the
  accepted `present_world(StructuralWorld)` signature without a reported API decision.
- Wall spans are generated only where visible (solid, upper, lower, masked, one-way) — never
  full portal quads clipped later. (M5 generates structural upper/lower spans only;
  masked/one-way semantics arrive at M6 and are diagnosed-not-interpreted before that.)
- Preferred texture path: R8 tile-index texture + palette/lookup texture + per-surface
  palette/shade/visibility = final unshaded color. Nearest sampling, no PBR, transparent-index
  discard, alpha scissor where possible; true translucency only when required.
  (M5 uses flat diagnostic materials; this path becomes binding at M6+.)
- Presentation modes: Authentic (virtual low-res, nearest upscale) and Clean (native
  resolution). Same engine; Authentic is not a separate software renderer.
- Portal traversal is required before engine acceptance (M10); cycles handled by covered
  screen interval, not a simple visited bit. (M5 renders all structural geometry — the
  allowed render-all visibility shortcut.)
- Begin with `ArrayMesh`/normal resources; move to direct `RenderingServer` RIDs only after
  profiling demonstrates a real bottleneck.

# Rendering contract

Stub — becomes binding at M5/M6/M10. Rules fixed by plan §9:

- The renderer consumes FauxBuild topology; it never converts a map into an independently
  editable Godot level, and no generated scene becomes world authority.
- Pipeline: camera pose in Build space → portal-aware visibility → visible
  sectors/wall-spans/sprites → cached static geometry + dirty dynamic rebuilds → indexed tile
  sampling → palette/lookup/shade/visibility shader → Godot viewport.
- One authoritative slope evaluator feeds floor/ceiling rendering, grounding, clearance,
  hitscan, sprite placement, movers (plan §8.3).
- Wall spans are generated only where visible (solid, upper, lower, masked, one-way) — never
  full portal quads clipped later.
- Preferred texture path: R8 tile-index texture + palette/lookup texture + per-surface
  palette/shade/visibility = final unshaded color. Nearest sampling, no PBR, transparent-index
  discard, alpha scissor where possible; true translucency only when required.
- Presentation modes: Authentic (virtual low-res, nearest upscale) and Clean (native
  resolution). Same engine; Authentic is not a separate software renderer.
- Portal traversal is required before engine acceptance (M10); cycles handled by covered
  screen interval, not a simple visited bit.
- Begin with `ArrayMesh`/normal resources; move to direct `RenderingServer` RIDs only after
  profiling demonstrates a real bottleneck.

# Collision contract

Stub — becomes binding at M7/M8/M9. Rules fixed by plan §8:

- Actor model: vertical body with XY radius, top clearance, bottom clearance/eye
  relationship, step height, movement mask, current sector hint.
- Pipeline: swept XY bounds → collect walls/blocking sprites via nearby sectors → classify
  passable portals by vertical span → sweep body against blockers → move to earliest contact
  → project remaining motion along blocking surface → iterate up to a fixed documented
  maximum contact count → resolve final sector and floor/ceiling → return collision
  references and flags.
- Required behavior: wall sliding; stable acute corners; no tunneling at supported speeds;
  oblique portal crossing; step-up/rejection by configured step height; slope grounding;
  ceiling rejection; sprite blocking; deterministic tie-breaking (time of impact, then stable
  ID); no NaN/Inf path.
- Gravity/jump/crouch/acceleration/friction live in the generic player/game layer; collision
  resolution lives in FauxBuild.
- Hitscan classifies nearest valid hit (wall, masked wall, sprite, floor, ceiling, none) and
  returns hit position, object ID, start/end sector context, surface normal/tangent, tile
  reference, flags. Line of sight shares the same portal and vertical-intersection code path —
  no second unrelated ray implementation.
- No Godot physics body participates, ever.

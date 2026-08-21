# Tag schema

Stub — the original game's schema is defined at M13 (plan §12.3).

Fixed rules already in force:

- `fauxbuild_core` preserves raw `lotag`, `hitag`, `extra` fields and assigns them **no**
  Duke meaning, ever.
- The original game defines its own schema: marker sprites with type tile/picnum,
  `lotag` = behavior category, `hitag` = link/group ID, `extra` = parameter or stable
  metadata ID.
- Editor-only marker tiles cover: player starts, enemy spawns, pickups, trigger
  volumes/lines, mover controllers, camera/attention nodes, scripted events, sound emitters.
- A sidecar file is permitted for strings/dialogue/large parameter sets/localization; spatial
  truth and topology remain in MAP.

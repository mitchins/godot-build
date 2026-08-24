# Numerics contract

Stub — becomes contractual as implementations land (first binding content at M2/M3, slope
quantization at M6/M7). Policy is fixed by plan §6 and must not be violated by any
implementation:

- `Coord = std::int32_t`, `AngleRaw = std::uint16_t` normalized to 0..2047; strong ID
  wrappers for `SectorId`, `WallId`, `SpriteId`, `TileId`, `PaletteId`, `WorldRevision`.
  No naked integers where two ID types could be confused.
- Native integer coordinates and angle units at all external core interfaces.
- 64-bit integers for products, squared distances, cross products, range checks.
- `double` only for robust intermediate geometry where exact integer arithmetic is not
  required. M5 structural derivation (D0016 proposed): every geometric predicate
  (orientation, area, containment) is exact integer math on int64 coordinates with
  two-limb signed 128-bit accumulators (Build coordinates are int32; products of
  differences overflow 64 bits). `double` appears only in final render-space vertex
  values, exact by the power-of-two render scale — never in a predicate.
- Explicit, tested rounding/quantization at every query output; never platform default
  rounding modes; never undefined signed overflow.
- Build-space → Godot-space conversion in exactly one place (the Godot adapter).
- Build's native vertical sign convention preserved inside the core; axes flipped only in the
  adapter. XY/Z display scale centralized — no scattered magic constants. M5 (D0016
  proposed): that one place is `fauxbuild::to_render_space` in core/ (x, -z, y;
  power-of-two scale, default 2^-11) — the adapter forwards its output verbatim, and the
  power-of-two rule makes the mapping exact and reversible for every int32 input.
- Binary parsing only through the bounds-checked little-endian `ByteReader`; never
  `reinterpret_cast` of file bytes into packed structs. Structured parse errors carry source
  name, byte offset, record kind/index, error code, explanation. Malformed input fails
  atomically.
- Error-handling boundary (D0006): `FB_CHECK` guards internal invariant violations only —
  bugs in our own code. Untrusted or external input (files, GRP contents, map data) is never
  validated with `FB_CHECK`; it must produce the structured errors above and fail atomically.
  Plain `assert()` is development-only (`NDEBUG` in release) and never a content-safety
  mechanism.

Deterministic observable behavior is the target — not imitation of old implementation
techniques.

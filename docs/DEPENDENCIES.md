# Dependencies

Every dependency needs a license entry and a written reason (plan §3.1). Provenance details
in `PROVENANCE.md`.

## Runtime / build dependencies

| Dependency | Version/Pin | License | Purpose | Written reason |
|---|---|---|---|---|
| doctest | v2.4.11, vendored `third_party/doctest/doctest.h` | MIT | Headless unit/property test framework for `fauxbuild_tests` | Single header, zero build integration cost, permissive license (D0002) |
| SCons | 4.10.1 (system, Homebrew) | MIT | Top-level native build system | Pinned by plan §3.1 (D0003) |
| Godot | 4.7.2 stable, `/Applications/Godot.app` | MIT | Engine host; not linked until M1 | Pinned by plan §3.1 (D0001) |
| C++ standard library (libc++) | Apple clang 21 toolchain | Apache-2.0 with LLVM exception | Core language runtime | Toolchain baseline, no choice to make |

| godot-cpp | submodule `third_party/godot-cpp` @ `9c8aeff0f58ad030f3d1030e8262de1322cd0ccd`, built with `api_version=4.7` | MIT | GDExtension C++ bindings for the extension layer | Only upstream artifact supporting the 4.7 extension API; no 4.7 tag exists yet (D0007) |
| Godot export templates | 4.7.2 stable (official `tpz`, installed under `~/Library/Application Support/Godot/export_templates/`) | MIT | macOS/iOS export gates and future release pipelines | Official release artifact matching the pinned engine (M1) |
| Fuzz harness | Custom deterministic driver `tests/fuzz/fuzz_main.cpp` + ASan/UBSan from the pinned clang toolchains | In-repo (MIT, ours) / Apache-2.0 with LLVM exception | Bounded mutation runs over committed corpus (D0010) | Apple clang ships no libFuzzer runtime; one portable codepath on all platforms; Linux CI may add libFuzzer against the same entry point later |
| earcut.hpp | v3.2.3, commit `c68c8835ccff2b7532d31d8fa8dfcf398f629498`, vendored `third_party/earcut/earcut.hpp` | ISC | Polygon triangulation of derived structural floor/ceiling loops (D0017) | Resolves the M5 triangulation dependency. Adopted after a measured A/B spike: the bespoke ear clipper failed 33 of 2450 sectors across six legally owned maps and could not build a real map; earcut fails none. Isolated behind FauxBuild's own exact validation and exact-area verification, so it is a mechanism with no authority over `MapData` (PROVENANCE row 13) |

## Pending (must be resolved in the milestone that introduces them)

| Dependency | Introduced at | Notes |
|---|---|---|
| _(none outstanding)_ | | The M5 triangulation decision was resolved in favour of vendored earcut.hpp (D0017); see the table above |

## Platform support notes

`config=fuzz` is supported on Linux and macOS only. It requires
`-fsanitize=address,undefined`, which MSVC does not provide, so `SConstruct`
rejects the configuration on Windows with a clear message rather than emitting
flags the toolchain silently mishandles. Windows fuzzing is not required by any
milestone through M15; if it becomes required, it needs a decision record.

## Rejected / avoided

| Candidate | Reason |
|---|---|
| Any Build-engine source port code | Clean-room prohibition (`PROVENANCE.md`) |
| Catch2 / GoogleTest | Heavier than needed for the current suite; revisit with a decision record if needed |

## earcut.hpp — details

Pinned, licence and provenance are in the dependency table above and in
`PROVENANCE.md` row 13. Vendored files:
`third_party/earcut/earcut.hpp` (SHA-256
`cc0913ab9e3a0903a1e826cd8f0a6445705dc5471c78e4dec6934f6f647a8a54`) and its ISC
`LICENSE`. Included in exactly one place, `third_party/earcut/earcut_adapt.hpp`,
so no earcut type reaches a FauxBuild header or public API.

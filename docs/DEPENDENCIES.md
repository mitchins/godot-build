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

## Pending (must be resolved in the milestone that introduces them)

| Dependency | Introduced at | Notes |
|---|---|---|
| Triangulation library for sector loops (or tested internal implementation) | M5 | Plan §9.3: a dependency decision, not an incidental code paste |

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

## earcut.hpp

- **Version**: v3.2.3, commit `c68c8835ccff2b7532d31d8fa8dfcf398f629498`
- **Licence**: ISC (vendored verbatim at `third_party/earcut/LICENSE`)
- **Vendored**: `third_party/earcut/earcut.hpp`
  (SHA-256 `cc0913ab9e3a0903a1e826cd8f0a6445705dc5471c78e4dec6934f6f647a8a54`)
- **Used for**: polygon triangulation of derived structural floor/ceiling loops
  only (D0017). Not linked into any parser, not used for MAP/ART/GRP/palette
  handling, and no earcut type appears in a FauxBuild header or public API.
- **Authority**: none. `MapData` remains canonical; earcut output is verified
  against the exact expected polygon area before any surface is emitted.
- **Provenance**: general-purpose computational-geometry library with no
  relationship to Build, any source port, or any game. See PROVENANCE row 13.

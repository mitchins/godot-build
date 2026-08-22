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

## Pending (must be resolved in the milestone that introduces them)

| Dependency | Introduced at | Notes |
|---|---|---|
| Triangulation library for sector loops (or tested internal implementation) | M5 | Plan §9.3: a dependency decision, not an incidental code paste |
| Fuzz harness (e.g. libFuzzer via clang) | M2 | Toolchain-provided, record usage when wired |

## Rejected / avoided

| Candidate | Reason |
|---|---|
| Any Build-engine source port code | Clean-room prohibition (`PROVENANCE.md`) |
| Catch2 / GoogleTest | Heavier than needed for the current suite; revisit with a decision record if needed |

# Decisions

Format per entry:

```text
### D#### — Title
Status: proposed | accepted | superseded by D####
Date: YYYY-MM-DD
Context: why this decision is needed
Decision: what was decided
Consequences: what follows
```

---

### D0001 — Pinned engine: Godot 4.7.2 stable

Status: accepted
Date: 2026-08-21
Context: plan §3.1 pins the engine; the machine originally had Godot 4.5.1 installed.
Decision: Godot **4.7.2 stable** is the pinned engine binary
(`/Applications/Godot.app`, reports `4.7.2.stable.official.ed1daf0bf`). 4.5.1 remains
installed under an alternate name for legacy purposes only. No Godot development snapshots
are used during the engine proof; upgrades happen only on a dedicated branch after the
complete gate suite passes.
Consequences: godot-cpp must be pinned to the matching branch/tag and an exact commit at M1.

### D0002 — Test framework: doctest v2.4.11

Status: accepted
Date: 2026-08-21
Context: plan §3.1 requires a small permissively licensed test framework, vendored or pinned,
with a license entry and written reason.
Decision: vendor doctest v2.4.11 (MIT) as the single header
`third_party/doctest/doctest.h`. Reason: single header, no build-system dependency, MIT
license, widely used, supports the unit/property-style tests needed by `fauxbuild_tests`.
Checksums and fetch URL recorded in `PROVENANCE.md`.
Consequences: any future test-framework change requires a new decision record.

### D0003 — Build system: SCons with clang on macOS

Status: accepted
Date: 2026-08-21
Context: plan §3.1 pins SCons as the initial top-level native build system and clang on macOS.
Decision: one top-level `SConstruct`; outputs under `build/<config>/`; configurations
`dev` (default), `release`, `asan` added as milestones need them (`determinism`, `fuzz` later).
Consequences: all native targets (core lib, tests, fbtool, later the GDExtension) build via
`scons config=<cfg>`.

### D0004 — Repository root layout per plan §5

Status: accepted
Date: 2026-08-21
Context: plan §5 defines the tree.
Decision: adopt it as-is; `fixtures/generated/` content is gitignored and reproducible;
`local_reference/` is gitignored.
Consequences: CI and tooling paths assume this layout.

### D0005 — No public C ABI yet

Status: accepted
Date: 2026-08-21
Context: plan §2.2.
Decision: link the pure C++ core directly into the GDExtension and CLI tools. A stable C ABI
is added only when a second genuine consumer requires it.
Consequences: core headers are C++20-only.

### D0006 — Content-safety checks independent of NDEBUG (`FB_CHECK`)

Status: accepted
Date: 2026-08-21 (accepted by human review 2026-08-21)
Context: plan §3.3 requires release builds to retain "assertions that protect content
safety", while conventional `NDEBUG` disables `assert()`. Parser/validator code arrives at
M2/M3 and would otherwise default to `assert()`.
Decision: `core/include/fauxbuild/check.hpp` provides `FB_CHECK(cond)`, which reports
expression/file/line and aborts, enabled in every configuration regardless of NDEBUG. The
`release` configuration defines `NDEBUG`, so plain `assert()` remains development-only:
"deliberate content-safety invariant" (`FB_CHECK`) stays distinct from "debug aid"
(`assert()`) in shipping builds.
Boundary with structured errors (see `NUMERICS.md`): `FB_CHECK` is for internal invariant
violations — bugs in our own code. Untrusted or external input is **never** validated with
`FB_CHECK`; malformed content must return structured errors (source name, byte offset,
record kind, error code) and fail atomically.
Consequences: content-safety invariants in core use `FB_CHECK`, never bare `assert()`;
parsers return structured errors for all bad input; behavior relying on `FB_CHECK` needs
tests (the fork/SIGABRT death test in `tests/unit/check.test.cpp` is the reference).

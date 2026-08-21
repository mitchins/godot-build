# Provenance

Engineering provenance policy for FauxBuild. This is not a substitute for commercial legal
advice.

## Forbidden inputs

Coding agents and contributors must not copy, translate, or adapt code from:

- Ken Silverman's Build source;
- EDuke32;
- JFBuild / JFDuke;
- Chocolate Duke;
- any game source release;
- decompilations or disassemblies;
- leaked or proprietary source;
- code snippets whose provenance cannot be established.

Do not ask an agent to "look at how EDuke32 implements clipmove."

## Approved inputs

- published binary-format descriptions;
- Godot's official documentation and APIs;
- general computational geometry references;
- original synthetic maps and assets;
- black-box observation of legally owned binaries/content;
- measurements and traces produced by our own runtime;
- permissively licensed general-purpose libraries after review;
- our own design decisions.

## Proprietary local stress data

`local_reference/` is gitignored. No proprietary map, tile, palette, audio, screenshot,
hash-derived asset cache, or extracted content enters CI or release artifacts. The runtime may
load that directory in a developer build only. Release builds of the original game must not
expose Duke-specific UI or assumptions.

## Provenance log

For every external technical reference or dependency record: title, owner/project, license or
usage classification, what was learned or imported, whether code was copied (must be `no` for
compatibility references), reviewer, date.

| # | Title | Owner/Project | License | Learned/Imported | Code copied | Reviewer | Date |
|---|---|---|---|---|---|---|---|
| 1 | doctest v2.4.11 (`third_party/doctest/doctest.h`) | Viktor Kirilov / doctest | MIT | Vendored single-header test framework for `fauxbuild_tests` | yes (verbatim, permitted: permissive license) | mitchellcurrie | 2026-08-21 |
| 2 | Godot 4.7.2 stable (editor binary, `/Applications/Godot.app`) | Godot Engine project | MIT | Pinned engine binary for M1+ integration; not linked yet | no | mitchellcurrie | 2026-08-21 |
| 3 | SCons 4.10.1 (system install via Homebrew) | SCons Foundation | MIT | Build system driving all native targets; invoked as a tool, not linked | no | mitchellcurrie | 2026-08-21 |
| 4 | Apple clang 21.0.0 / LLVM libc++ (Xcode 26.6 toolchain) | Apple / LLVM | Apache-2.0 with LLVM exception | Compiler and C++ standard library for all native targets | no | mitchellcurrie | 2026-08-21 |

Vendor checksums (SHA-256):

- `third_party/doctest/doctest.h`:
  `44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb`
  (fetched from `https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h`)
- `third_party/LICENSES/doctest-MIT.txt`:
  `0fe0b331fa1513dcce8604ff1fa925f32d1cea17d8aeb1c2471fad40d291adc5`

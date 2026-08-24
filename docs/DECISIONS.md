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

### D0007 — godot-cpp pin: master @ 9c8aeff0 with api_version=4.7

Status: accepted
Date: 2026-08-22
Context: D0001 expected a godot-cpp branch/tag matching Godot 4.7.2. Upstream has no 4.7
tag and no 4.7 branch; the newest stable tag is `godot-4.5-stable`. godot-cpp `master`
(HEAD `9c8aeff0f58ad030f3d1030e8262de1322cd0ccd`) ships `extension_api-4-7.json` and
declares `supported_api_versions = ["4.3", "4.4", "4.5", "4.6", "4.7"]`, so it is the only
upstream artifact that can target the pinned engine.
Decision: vendor godot-cpp as a git submodule at `third_party/godot-cpp`, pinned to commit
`9c8aeff0f58ad030f3d1030e8262de1322cd0ccd`, built with `api_version=4.7`. Re-pin to the
official `godot-4.7.2-stable` tag (or equivalent) when upstream creates one; that re-pin is
a follow-up decision record, not optional cleanup.
Consequences: the pin tracks a moving upstream lineage until a tag exists — the pinned commit
in `.gitmodules`/submodule state is authoritative, never "master". Provenance entry records
the commit and license.

### D0008 — Engine/game repository boundary at the M12 seam

Status: accepted
Date: 2026-08-22 (human ruling at M1 acceptance)
Context: the plan (§5 layout, §11 integration; M13/M14 milestones) places the original game
inside this repository. This repository is public and functions as the engine/tech demo for
an upstream game that will consume it as a dependency with free public CI/CD. Writing game
content here would also collide with the M12 freeze: `fauxbuild-core-v0.1` is exactly the
seam where the engine repo stops and the game repo starts.
Decision: this repository's scope ends at the engine conformance freeze (M12, tag
`fauxbuild-core-v0.1`). M13 (game framework) and M14 (vertical slice) execute in a separate
game repository consuming this one as an upstream dependency. `godot/game/` in this repo is
demoted to engine sample/test content only. Plan §5/§11 "game" layer references are
superseded for this repo by this decision.
Consequences: MILESTONES.md M13/M14 are annotated out-of-repo; no agent writes game code
here. The game repository becomes the "second genuine consumer" that reopens D0005 (public
API/ABI question) when it starts consuming the engine; the GDScript extension surface is the
game-facing API in the meantime.

### D0009 — Evidence classes: CI-carried vs human-attested

Status: accepted
Date: 2026-08-22 (human ruling at M1 acceptance)
Context: `local_reference/` is gitignored and must never enter CI (plan §4.4), and no CI
runner has an iPhone. Every gate item resting on the local `DUKE3D.GRP` (M3 E1L1 counts,
M5 recognizable shell, M8 full traversal, the M12 acceptance test) and every iOS
device-launch item are structurally laptop-only. Left implicit, these become gates that
report green without executing — the exact failure mode of M0/M1 review rounds 1–3.
Decision: two evidence classes, recorded per item in MILESTONES.md:
(a) **CI evidence** — synthetic fixtures carry the entire automated burden and must be
sufficient on their own. The synthetic suite is what proves the generic model; e.g. M8's
kill gate ("if E1L1 only works through per-level tolerances, stop") is judged against the
synthetic collision suite, with E1L1 as confirmation only.
(b) **HUMAN-ATTESTED evidence** — proprietary-content and physical-device gates are executed
by a human on the development machine, recorded as `HUMAN-ATTESTED <date> <commands>` in the
milestone entry, and can never be ticked by CI.
Consequences: gate items in M3/M5/M8/M11(device)/M12 are annotated with their class; CI
greenness never substitutes for the attested items and vice versa.

### D0010 — Definition of "fuzz tests pass"

Status: accepted
Date: 2026-08-22 (carried in from the M1 acceptance review)
Context: plan §3.3 and the M2 gate require parser fuzz targets. A built-but-never-run fuzz
target is the same failure shape as every gate defect found in M0/M1 review rounds 1–3 —
it reports capability without executing it. Without a definition settled before the target
exists, the gate item silently degrades to "it compiled."
Decision: a fuzz gate item is satisfied only by all of:
(a) a **bounded run in CI** (`-runs=N` or `-max_total_time=`) that exits zero;
(b) a **committed seed corpus** covering at least: valid containers, empty input, truncated
headers/directories/data, and bad signatures;
(c) **every crasher ever found is committed as a regression input** and additionally
exercised by the ordinary `check` suite (corpus regression test), so regressions fail even
on platforms/configurations without the fuzz runtime.
Consequences: `tests/fuzz/corpus/` and `tests/fuzz/regression/` are gate artifacts, not
scratch space; CI green without the bounded fuzz run does not tick any fuzz item.

### D0011 — GRP parser entry-count resource limit

Status: accepted
Date: 2026-08-23 (human ruling 2026-08-23, at M2 review)
Ruling: "FauxBuild GRP parsing is bounded to 65,536 directory entries. Archives
declaring more entries are rejected with a structured `TooLarge` error rather
than truncated. This is a defensive parser/runtime limit, not a claim about the
GRP format itself. Silent partial mounting is prohibited." Parse-first-N-and-warn
was considered and rejected: an archive that appears to mount while its namespace
is silently incomplete turns a parser bound into distant, bizarre failures in map,
art and audio lookup. Fail-closed is the correct behaviour for a container layer.
Resource-budget configurability is explicitly *not* wanted: 65,536 is a hard
safety ceiling, and parser-policy knobs wait for a real use case.
Context: the GRP format states no maximum file count. The directory costs 16
bytes per entry while a parsed `GrpEntry` costs 64, so an unbounded count lets
untrusted input amplify ~4x into process memory: a 1 GiB container (the
`read_file_bytes` default cap) materializes ~4 GiB of entries, plus reallocation
peak. Measured at 4.2x on a 15 MiB directory-only container. Clamping the
initial `reserve` only defers the allocation; it does not bound it.
Decision: `fauxbuild::grp::kMaxEntryCount = 65536`. A file count above it is
rejected with a structured `TooLarge` error on the header alone, before the
directory is read or any per-entry allocation occurs. This is a **parser
resource limit, not a format limit** — it makes no claim about what the GRP
format permits, and it is deliberately distinct from the
`FAUXBUILD_CLASSIC_V7` profile limits, which are world-representation limits.
Real archives hold low thousands of files, so this is roughly 40x headroom.
Consequences: recorded as a bounded known incompatibility in
`COMPATIBILITY_SCOPE.md`. Raising the limit is a new decision record, not an
incidental code change. Any future format parser that allocates per record from
an untrusted count needs an equivalent bound.
### D0012 — MAP v7 sentinel and strictness rules (M3)

Status: accepted (human ratification 2026-08-23)
Date: 2026-08-23
Ratification: rules 1, 2, 3, 5, 6 ratified outright — corroborated by the
reviewer's independent decode of all six shipped maps (9,664 portal walls, all
reciprocated; every loop closed; every sprite sectnum valid or sentinel).
Rule 4 (trailing data rejects) ratified with the risk named: a legitimate
resave that introduces trailing bytes would be rejected; zero shipped maps
have trailing data today, and the rule is reversible by a later decision.
Also noted for future profile discussions: largest shipped map is 557 sectors
against the 1024 classic limit (~1.8x headroom) — full-game and user maps can
exceed it; that is a future decision, not an M3 change.
Context: task §16 requires explicit rulings for format ambiguities encountered at M3.
All choices below were implemented to be strict and fail-closed; real-world exceptions
discovered by later gates (Mapster, more maps) reopen the specific item.
Decision:
1. Version encoding is int32 == 7 with no string signature (observed on E1L1;
   locked, not merely proposed — recorded here for completeness).
2. Sprite `sectnum` -1 is a valid "no sector" sentinel; anything else out of
   [0, numsectors) is an error.
3. Start sector -1 is valid only for zero-sector maps; otherwise it must be in
   [0, numsectors).
4. Trailing bytes after the sprite table are rejected (TrailingData), not
   warned. E1L1 and canonical writes have none; if a legitimate source map with
   trailing data appears, downgrade to Warning via a new decision.
5. Portals: nextwall/nextsector are both -1 or both set; nextwall must be
   reciprocated and nextsector must equal the sector owning nextwall. All
   violations are errors.
6. Validation severity is a single Error tier at M3; a Warning tier is added
   only when a real, legally-owned map is rejected by an Error that genuine
   content exhibits.
Consequences: validators stay deterministic and fail-closed; every rule above
is covered by a named test case in tests/unit/map_validate.test.cpp.

### D0013 — PALETTE.DAT table region beyond the declared count (M4)

Status: accepted (human ratification 2026-08-23, slice-1 review: "ratify it
as written"; refutation independently reproduced by the reviewer)
Date: 2026-08-23
Context: the published PALETTE.DAT description (ModdingWiki, PROVENANCE row 11)
models the file as 768-byte palette + int16 numpalookups + numpalookups*256
shade tables + 65536 translucency. Corroborated against the legally owned
DUKE3D.GRP: the declared count (32) is honored as the palette-0 shade ramp,
but the file contains 64 tables' worth of bytes before the translucency
region; the size equation closes only with 64 tables. The extra 32 tables are
undeclared in-band. Property evidence: tables 0..31 form a monotone shade ramp
(table 0 is 255/256 identity; distinct-value count collapses monotonically to
18); table 32 breaks every property of that ramp (identity hits drop to 3,
distinct values jump to 139) — the boundary is real, not an artifact.
Decision:
1. `read_palette_dat` accepts the file iff the total arithmetic closes exactly
   (770 + 256*k + 65536 == size, k >= declared count). The bytes between the
   declared tables and the translucency region are preserved verbatim as
   `extra_tables` — parsed structure, no interpretation.
2. Canonical write emits the region verbatim; round-trip is byte-identical
   (unit-tested, fuzz-enforced).
3. No attempt is made to name the extra tables (candidate hypotheses —
   per-palette shade tables, etc. — are not encoded) until a genuine consumer
   exists; guessing semantics ahead of need is the pre-building AGENTS.md
   forbids.
Consequences: real PALETTE.DAT files load and rewrite byte-identically; other
Build games' palettes that pack a different number of tables still parse under
the same closure rule; the deviation from the published description is bounded
and recorded here and in COMPATIBILITY_SCOPE row 0b.

Additional consequence (slice-1 review finding 1), inherent to sizing the
table region from the file: **truncation inside the extra region is
undetectable**. Removing k*256 bytes (k <= 32 on real content) still closes
arithmetically, and the translucency window slides by the removed bytes —
wrong colour data with no error. This is a property of the format, not a
parser choice: the declared count cannot size the region without rejecting
real content. Mitigation, adopted as policy: content read through a GRP mount
has an authoritative entry length (the container validates offset+size), so
truncation is caught at the container layer; the exposure is loose files
only. Tooling therefore defaults to mount-based reads (fbtool --grp), and the
slice-1 record notes that plain-file palette loads of unknown provenance
should not be trusted for rendering.

### D0014 — Tile manifest format and stability semantics (M4)

Status: accepted — human ratification 2026-08-24 (amended from the content-immutable draft the same day)
Date: 2026-08-23 (amended 2026-08-24)
Context: plan §7.5 requires a stable tile manifest ("never casually renumber
tiles after maps exist"). The M4 brief demands the stability property as a
test, not a comment. This record proposes the format and the semantics the
tests enforce.
Decision:
1. Format (v2): deterministic text, one entry per tile — `picnum name w h xc
   yc anim frames speed content` with whole-line `#` comments (frame entries
   legitimately contain `#` in names, so trailing comments are not supported).
   `content` is fnv1a64 of the tile's pixel bytes, written as exactly 16
   lowercase hex digits and parsed strictly. Canonical write is stable across
   rewrites.
2. The manifest is the picnum authority. Builds assign new tiles as max+1;
   animation sets occupy `frames` consecutive entries named `name#k`, with
   only the anchor (`#0`) recording frame count and animation type.
3. **Picnum assignment is immutable; artwork is not.** Existing logical tiles
   never move because source files were reordered or new tiles were inserted.
   New logical tiles receive max(picnum)+1. The manifest is the picnum
   authority; source order is never consulted. Tested.
4. **The content hash is a change detector, not the tile's identity.** Name,
   dims, pivot and animation together do not pin what a picnum draws — a tile
   can keep all of them and change its pattern — so the hash exists to catch
   that drift. It does *not* make artwork immutable. FNV-1a64 is adequate: this
   is a drift detector, not an adversarial boundary.
5. **Unacknowledged change fails closed.** If pixels, dims, pivot or animation
   differ from the manifest, the ordinary build fails and names exactly what
   changed and which picnum is affected.
6. **Intentional updates have an explicit acceptance path that preserves the
   picnum**: `fbtool build-art ... --accept-tile-update <name>` (core:
   `TileUpdateAcceptance`). It refreshes the stored hash and metadata and
   **never** the number. Acceptance is per-tile — accepting one tile does not
   excuse drift in another — and `name` accepts a whole animation set while
   `name#k` accepts a single frame. Redrawing an enemy twenty times while it
   stays picnum 417 is ordinary authoring and must not require ceremony beyond
   saying so.
7. **Rename and removal are not automatic.** Removal is a hard error today;
   picnums are never compacted or recycled. A future intentional rename or
   retire must preserve or tombstone the number rather than freeing it for
   reuse — the one thing that must never happen is a later tile inheriting a
   retired picnum.
Consequences: `fbtool build-art --init-manifest` bootstraps; later builds
pass `--manifest`. The stable-rebuild contract (identical manifest text) and
the add/remove/reshape unit properties live in
tests/unit/tile_build.test.cpp; the removal check was negative-tested
(sabotaged to tolerate removals -> suite red), as was the content check
(removed -> suite red at the same-shape-different-pixels case; CI probe red
with "wrote output despite failing the stability check").

**Amendment history.** The first implementation made content *immutable*:
any pixel change was a hard error with no way to say "yes, I meant that". The
human reviewer rejected that contract — "changing the pixels of an existing
tile is a completely normal part of making a game" — and it is now rules 3-6
above. The correct invariant is **maps own numbers; artists may continue
making art.** The drift detection was kept; only the refusal to accept an
intentional change was wrong.

Manifests written before v2 have nine fields and are rejected with a field
count error rather than silently loaded without content hashes. No such
manifest has been published outside this branch: `fixtures/` carries source
DSLs, and manifests are build output. Regenerate with `--init-manifest`.


### D0015 — Indexed atlas: authoritative representation and namespace policy (M4)

Status: proposed (implemented, awaiting human ratification)
Date: 2026-08-24
Context: M4 slice 4 builds the atlas M5 will render from. Two policies had
to be fixed before code: where RGBA may exist, and how multiple ART files
form one picnum namespace.
Decision:
1. **The atlas is indexed, terminally.** Authoritative storage is
   `std::vector<uint8_t> pixels` — one palette index per texel, row-major
   pages. Every RGBA form anywhere is derived at a presentation boundary,
   recomputed per call, never stored. `ci/check_layering.py` pins the
   declaration; unit + scene tests pin the byte count at
   page_width*page_height*page_count.
2. **Namespace = union of declared ranges + numtiles floor.** Each ART file
   claims [localtilestart, localtileend]; overlapping claims are rejected;
   gaps become explicit empty picnums (page = -1). The global namespace size
   is max(end+1, numtiles) over all files. numtiles is a FLOOR, never a
   per-file bound: the shipped GRP declares 2816 globally while its last
   files claim picnums through 3327 (black-box observation; the first
   real-GRP run of inspect-atlas rejected on a numtiles<end+1 check and was
   wrong to — see COMPATIBILITY_SCOPE 0e).
3. **Placement is deterministic and disposable.** Same assets -> same atlas
   byte-for-byte (shelf packing in picnum order; pure function of dims and
   options). Atlas coordinates are runtime products and may change when
   packing changes; picnum identity is owned by the stable tile manifest
   (D0014) and never by the atlas.
4. **The column-major file-order claim is acted on exactly once**, at this
   boundary: ART bytes are copied verbatim into the model (no
   interpretation), and the atlas transpose (file column-major -> page
   row-major) is the first and only place the published ordering claim
   affects output. The synthetic fixture's asymmetric index formulas make a
   transposition or off-by-one loud at the unit, fbtool, and Godot
   consumer-boundary levels (each negative-tested by sabotage).
5. **Consumer boundary is tested at the consumer.** The Godot scene reads
   bytes/rects/metadata through the GDExtension API and re-derives expected
   values from the fixture spec (never from the atlas itself). RGBA exists
   only in `compute_tile_rgba` / preview images, recomputed each call.
Consequences: M5 asks "give me picnum N" and receives stable metadata plus
indexed texels without knowing whether assets came from loose files, one
ART file, or thirteen inside a GRP (`load_asset_set` + `tile_bytes`).
Rejection classes: overlap, malformed range, count/range mismatch, payload
vs dims, area cap, page-overflow — all InvalidRange/TooLarge ParseErrors,
never partial atlases.

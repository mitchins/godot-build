#!/usr/bin/env python3
"""fbtool command-contract gate: exercises dump-grp/gen-grp end to end.

Command behaviour is a contract like any other (AGENTS.md rule 6). These are
process-level checks because exit codes and stdout are the contract, and the
unit suite cannot observe either.
"""

import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
fbtool = sys.argv[1] if len(sys.argv) > 1 else str(root / "build/dev/fbtool")

failures = []


def run(args, expect_rc, label, expect_out=None, expect_err=None):
    proc = subprocess.run([fbtool, *args], capture_output=True, text=True, cwd=root)
    if proc.returncode != expect_rc:
        failures.append(f"{label}: exit {proc.returncode}, expected {expect_rc}")
    if expect_out and expect_out not in proc.stdout:
        failures.append(f"{label}: stdout missing {expect_out!r}")
    if expect_err and expect_err not in proc.stderr:
        failures.append(f"{label}: stderr missing {expect_err!r}")
    return proc


with tempfile.TemporaryDirectory() as tmp:
    good = str(pathlib.Path(tmp) / "good.grp")
    run(["gen-grp", "--out", good, "--seed", "4", "--files", "5", "--max-size", "64"],
        0, "gen-grp", expect_out="wrote")

    # A generated container must round-trip through our own dumper.
    proc = run(["dump-grp", good], 0, "dump-grp", expect_out="files: 5")
    if "SYN0000.DAT" not in proc.stdout:
        failures.append("dump-grp: generated names missing from output")

    # The published header is 16 bytes: first entry data starts at 16 + 16*5.
    if "data starts at offset 96" not in proc.stdout:
        failures.append("dump-grp: data_start is not 16 + 16*file_count")

    # vfs-stat reads through GrpMount + the normalized VFS lookup, not the
    # directory parse that dump-grp uses.
    proc = run(["vfs-stat", good, "SYN0000.DAT"], 0, "vfs-stat hit", expect_out="OK")
    if "via grp:" not in proc.stdout:
        failures.append("vfs-stat: origin not reported")
    run(["vfs-stat", good, "syn0000.dat"], 0, "vfs-stat case-folded", expect_out="OK")
    run(["vfs-stat", good, "NOSUCH.DAT"], 1, "vfs-stat miss", expect_out="MISS")
    run(["vfs-stat", good], 2, "vfs-stat usage", expect_err="usage")

    bad = pathlib.Path(tmp) / "bad.grp"
    bad.write_bytes(b"NotSilverman" + b"\x00" * 8)
    run(["dump-grp", str(bad)], 1, "dump-grp bad signature", expect_err="bad_signature")

    run(["dump-grp", str(pathlib.Path(tmp) / "missing.grp")], 1, "dump-grp missing",
        expect_err="io_error")

    run(["gen-grp"], 2, "gen-grp without --out", expect_err="--out is required")
    run(["gen-grp", "--out", good, "--files", ""], 2, "gen-grp empty number")
    run(["gen-grp", "--out", good, "--files", "12x"], 2, "gen-grp trailing garbage")
    run(["gen-grp", "--out", good, "--files", "-1"], 2, "gen-grp negative")
    # Previously reached a modulo by zero inside generate_grp (UBSan-confirmed).
    run(["gen-grp", "--out", good, "--max-size", "4294967295"], 2, "gen-grp max-size overflow")
    run(["gen-grp", "--out", "/nonexistent/dir/out.grp"], 1, "gen-grp unwritable",
        expect_err="io_error")
    # Per-option bounds pass individually but multiply into a ~1 TiB request.
    run(["gen-grp", "--out", good, "--files", "65536", "--max-size", "16777216"],
        2, "gen-grp aggregate payload", expect_err="over the")
    # ---------------- MAP v7 commands (M3) ----------------
    fix = str(pathlib.Path(tmp) / "two_sector.MAP")
    run(["gen-map", "--fixture", "two_sector_portal", "--out", fix], 0, "gen-map",
        expect_out="wrote")
    proc = run(["dump-map", fix], 0, "dump-map", expect_out="validation: OK")
    for expected in ("sectors: 2", "walls: 8", "portal walls: 2",
                     "version: 7", "start:", "cstat&0x30", "cstat&0x10"):
        if expected not in proc.stdout:
            failures.append(f"dump-map: stdout missing {expected!r}")
    run(["dump-map", "--verbose", fix], 0, "dump-map verbose")

    run(["validate-map", fix], 0, "validate-map ok", expect_out="0 errors")
    run(["rewrite-map", fix, str(pathlib.Path(tmp) / "rewritten.MAP")], 0, "rewrite-map",
        expect_out="semantic diff empty")
    run(["diff-map", fix, str(pathlib.Path(tmp) / "rewritten.MAP")], 0, "diff-map equal",
        expect_out="semantically identical")
    run(["gen-map", "--list"], 0, "gen-map list", expect_out="multi_loop")

    # Malformed content is a content error (1); usage problems exit 2.
    bad_version = pathlib.Path(tmp) / "bad_version.MAP"
    bad_version.write_bytes((9).to_bytes(4, "little") + b"\x00" * 40)
    run(["validate-map", str(bad_version)], 1, "validate-map bad version",
        expect_err="unsupported_version")
    # No positional: with one present the arity check yields exit 2 on its own,
    # so the case would pass even with unknown-option handling deleted.
    run(["dump-map", "--bogus"], 2, "dump-map unknown option")

    # A failing rewrite-map must publish nothing. Note what this can and cannot
    # reach: the self-check (reparse + semantic diff) cannot be made to fail
    # from outside the process, because the reader and writer enforce identical
    # limits and every field round-trips at fixed width — with a correct writer
    # it is an assertion, not a validation. Its ordering was verified in review
    # by injecting a writer bug (see MILESTONES M3 review round 5). What is
    # externally observable is that neither a rejected parse nor a failed write
    # leaves a file behind.
    unwritten = pathlib.Path(tmp) / "unwritten.MAP"
    run(["rewrite-map", str(bad_version), str(unwritten)], 1,
        "rewrite-map rejects bad input without writing", expect_err="unsupported_version")
    if unwritten.exists():
        failures.append("rewrite-map: wrote output despite a rejected parse")

    # Parse and self-check both succeed here; only the write fails.
    run(["rewrite-map", fix, "/nonexistent-dir/out.MAP"], 1, "rewrite-map unwritable destination",
        expect_err="io_error")
    run(["dump-map", "--grp"], 2, "dump-map dangling option value")
    run(["diff-map", fix], 2, "diff-map arity")
    run(["rewrite-map", fix], 2, "rewrite-map arity")
    run(["gen-map", "--fixture", "nope", "--out", str(pathlib.Path(tmp) / "x.MAP")], 1,
        "gen-map unknown fixture", expect_err="unknown fixture")
    run(["gen-map", "--wat"], 2, "gen-map unknown option")
    run(["dump-map", str(pathlib.Path(tmp) / "missing.MAP")], 1, "dump-map missing",
        expect_err="io_error")

    # The --grp path routes through GrpMount + the normalized VFS lookup, which
    # nothing else in this gate exercises for the MAP commands.
    map_bytes = pathlib.Path(fix).read_bytes()
    name = b"TWOSECT.MAP"
    grp_with_map = pathlib.Path(tmp) / "maps.grp"
    grp_with_map.write_bytes(
        b"KenSilverman"
        + (1).to_bytes(4, "little")
        + name.ljust(12, b"\x00")
        + len(map_bytes).to_bytes(4, "little")
        + map_bytes
    )
    run(["dump-map", "--grp", str(grp_with_map), "TWOSECT.MAP"], 0, "dump-map via grp",
        expect_out="validation: OK")
    run(["validate-map", "--grp", str(grp_with_map), "twosect.map"], 0,
        "validate-map via grp, case-folded", expect_out="0 errors")
    run(["dump-map", "--grp", str(grp_with_map), "NOSUCH.MAP"], 1, "map not in grp",
        expect_err="not_found")

    # Options are per-command and a value may not be an option token: both
    # shapes previously became positional paths (exit 1) instead of usage errors.
    run(["validate-map", "--verbose", fix], 2, "validate-map rejects --verbose")
    run(["rewrite-map", "--verbose", fix, str(pathlib.Path(tmp) / "v.MAP")], 2,
        "rewrite-map rejects --verbose")
    run(["dump-map", "--grp", "--bogus", fix], 2, "--grp value may not be an option")
    run(["gen-map", "--list", "--wat"], 2, "gen-map --list is standalone")

    # Sprite orientation is a two-bit field; 0x0030 is reserved and appears in
    # no real map, so the classifier's fallback is otherwise unexercised.
    ori = pathlib.Path(tmp) / "sprites.MAP"
    run(["gen-map", "--fixture", "sprite_orientations", "--out", str(ori)], 0, "gen-map sprites")
    raw = bytearray(ori.read_bytes())
    nsec = int.from_bytes(raw[20:22], "little")
    off = 22 + 40 * nsec
    nwall = int.from_bytes(raw[off:off + 2], "little")
    off += 2 + 32 * nwall
    sprites = off + 2  # first sprite record; cstat is at +12
    cstat = int.from_bytes(raw[sprites + 12:sprites + 14], "little")
    raw[sprites + 12:sprites + 14] = ((cstat & ~0x0030) | 0x0030).to_bytes(2, "little")
    reserved = pathlib.Path(tmp) / "reserved.MAP"
    reserved.write_bytes(bytes(raw))
    run(["dump-map", str(reserved)], 0, "reserved sprite orientation",
        expect_out="reserved=1")

    # gen-map option values may not be option tokens either.
    run(["gen-map", "--fixture", "minimal", "--out", "--wat"], 2, "gen-map --out needs a value")
    run(["gen-map", "--fixture", "--out", str(pathlib.Path(tmp) / "y.MAP")], 2,
        "gen-map --fixture needs a value")

    # ---------------- palette/lookup commands (M4 slice 1) ----------------
    import struct as _struct
    pal = pathlib.Path(tmp) / "synth.dat"
    pal_bytes = bytearray(i % 64 for i in range(768))
    pal_bytes += _struct.pack("<H", 2)
    pal_bytes += bytes(256 * 2) + bytes(256 * 3)  # 2 declared + 3 extra tables
    pal_bytes += bytes(65536)
    pal.write_bytes(pal_bytes)
    run(["dump-palette", str(pal)], 0, "dump-palette", expect_out="shade tables: 2")
    proc = run(["dump-palette", str(pal)], 0, "dump-palette again")
    if "extra tables: 768 bytes (3 tables" not in proc.stdout:
        failures.append("dump-palette: extra tables not reported")
    if "6-bit VGA" not in proc.stdout:
        failures.append("dump-palette: 6-bit detection missing")

    lut = pathlib.Path(tmp) / "synth_lut.dat"
    lut_bytes = bytearray([1, 4]) + bytes(256) + bytes(i % 64 for i in range(768))
    lut.write_bytes(lut_bytes)
    run(["dump-lookup", str(lut)], 0, "dump-lookup", expect_out="swaps: 1")
    run(["dump-lookup", str(lut)], 0, "dump-lookup alts", expect_out="alt palettes: 1")

    bad = pathlib.Path(tmp) / "bad.dat"
    bad.write_bytes(b"\x00" * 40)
    run(["dump-palette", str(bad)], 1, "dump-palette truncated", expect_err="truncated")
    empty_lut = pathlib.Path(tmp) / "empty_lut.dat"
    empty_lut.write_bytes(b"")
    run(["dump-lookup", str(empty_lut)], 1, "dump-lookup truncated", expect_err="truncated")
    ragged = pathlib.Path(tmp) / "ragged.dat"
    ragged.write_bytes(bytes(768) + _struct.pack("<H", 0) + bytes(65536 + 7))
    run(["dump-palette", str(ragged)], 1, "dump-palette ragged", expect_err="trailing_data")
    run(["dump-palette", "--bogus", str(pal)], 2, "dump-palette unknown option")
    run(["dump-palette", "--grp"], 2, "dump-palette dangling option")
    run(["dump-lookup"], 2, "dump-lookup arity")

#-- -- -- -- -- -- -- -- ART command(M4 slice 2) -- -- -- -- -- -- -- --
    import struct as _s2
    art = pathlib.Path(tmp) / "synth.art"
    specs = [(0, 0, 0), (8, 8, 0x02000043), (3, 2, 0), (1, 1, 0)]  # frames=3 type=1 speed=2
    art_bytes = bytearray(_s2.pack("<iiii", 1, 2816, 0, 3))
    art_bytes += b"".join(_s2.pack("<h", w) for w, _, _ in specs)
    art_bytes += b"".join(_s2.pack("<h", h) for _, h, _ in specs)
    art_bytes += b"".join(_s2.pack("<i", m) for _, _, m in specs)
    n = 0
    for w, h, _ in specs:
        for _ in range(w * h):
            art_bytes.append(n & 0xFF)
            n += 1
    art.write_bytes(art_bytes)
    run(["dump-art", str(art)], 0, "dump-art",
        expect_out="tile range: 0..3 (4 tiles; numtiles field: 2816, global)")
    proc = run(["dump-art", str(art)], 0, "dump-art stats")
    for expected in ("animated tiles: 1", "version: 1"):
        if expected not in proc.stdout:
            failures.append(f"dump-art: stdout missing {expected!r}")
    run(["dump-art", "--verbose", str(art)], 0, "dump-art verbose", expect_out="tile[1]:")

    bad_art = pathlib.Path(tmp) / "bad.art"
    bad_art.write_bytes(_s2.pack("<iiii", 2, 1, 0, 0) + b"\x00" * 8)
    run(["dump-art", str(bad_art)], 1, "dump-art bad version",
        expect_err="unsupported_version")
    trailing_art = pathlib.Path(tmp) / "trailing.art"
    trailing_art.write_bytes(bytes(art_bytes) + b"\xEE")
    run(["dump-art", str(trailing_art)], 1, "dump-art trailing",
        expect_err="trailing_data")
    run(["dump-art", "--bogus", str(art)], 2, "dump-art unknown option")
    run(["dump-art", "--grp"], 2, "dump-art dangling option")
    run(["dump-art"], 2, "dump-art arity")

    # ---------------- build-art / build-palette (M4 slice 3) ----------------
    src_dir = root / "fixtures/source"
    art_out = str(pathlib.Path(tmp) / "diag.art")
    man_out = str(pathlib.Path(tmp) / "diag.manifest")
    run(["build-art", "--source", str(src_dir / "tiles/diagnostics.tileset"),
         "--out", art_out], 2, "build-art without manifest flag",
        expect_err="stable build needs --manifest")
    init_proc = run(["build-art", "--source", str(src_dir / "tiles/diagnostics.tileset"),
                     "--out", art_out, "--init-manifest"], 0, "build-art init",
                    expect_out="built")
    # --init-manifest writes beside the ART output; adopt that as the stable
    # manifest path for the rebuild probe.
    stable_manifest = art_out + ".manifest"
    manifest_path = pathlib.Path(stable_manifest)
    if not manifest_path.exists():
        failures.append(
            f"build-art init: manifest not written to {stable_manifest!r}; "
            f"rc={init_proc.returncode} stdout={init_proc.stdout!r} "
            f"stderr={init_proc.stderr!r}")
    first_manifest = manifest_path.read_text() if manifest_path.exists() else ""
    run(["build-art", "--source", str(src_dir / "tiles/diagnostics.tileset"),
         "--out", art_out, "--manifest", stable_manifest], 0, "build-art stable rebuild",
        expect_out="built")
    rebuilt = pathlib.Path(stable_manifest).read_text() if pathlib.Path(
        stable_manifest).exists() else ""
    if first_manifest and rebuilt != first_manifest:
        failures.append("build-art: stable rebuild changed the manifest")

    run(["dump-art", art_out], 0, "dump-art built", expect_out="tile range: 0..12")

    pal_out = str(pathlib.Path(tmp) / "diag_palette.dat")
    lut_out = str(pathlib.Path(tmp) / "diag_lookup.dat")
    run(["build-palette", "--source", str(src_dir / "palettes/diagnostic.palette"),
         "--palette-out", pal_out, "--lookup-out", lut_out], 0, "build-palette",
        expect_out="PALETTE.DAT")
    run(["dump-palette", pal_out], 0, "dump-palette built", expect_out="6-bit VGA")
    run(["dump-lookup", lut_out], 0, "dump-lookup built", expect_out="swaps: 4")

    # Content immutability, end to end: a tile that keeps its name, size, pivot
    # and animation but changes its pixels must be rejected, and must publish
    # nothing. Without this a picnum silently redefines what every map drawing
    # it shows (review finding, slice 3).
    tampered = pathlib.Path(tmp) / "tampered.tileset"
    original = (src_dir / "tiles/diagnostics.tileset").read_text()
    tampered_lines = []
    for line in original.splitlines(keepends=True):
        if line.startswith("tile solid_dark"):
            line = line.replace("color=1", "color=63")
        tampered_lines.append(line)
    tampered.write_text("".join(tampered_lines))
    if "".join(tampered_lines) == original:
        failures.append("build-art: tamper probe did not modify the tileset (fixture changed?)")
    tamper_out = str(pathlib.Path(tmp) / "tampered.art")
    run(["build-art", "--source", str(tampered), "--out", tamper_out,
         "--manifest", stable_manifest], 1, "build-art rejects changed pixels",
        expect_err="different pixels")
    if pathlib.Path(tamper_out).exists():
        failures.append("build-art: wrote output despite failing the stability check")

    # An accepted repaint must succeed and keep the picnum (D0014 rules 3-6):
    # the manifest is a drift detector, not a freeze on artwork.
    before_line = next(
        (l for l in pathlib.Path(stable_manifest).read_text().splitlines()
         if l.startswith("0 ")), "")
    accepted_out = str(pathlib.Path(tmp) / "accepted.art")
    run(["build-art", "--source", str(tampered), "--out", accepted_out,
         "--manifest", stable_manifest, "--accept-tile-update", "solid_dark"], 0,
        "build-art accepts an acknowledged repaint", expect_out="built")
    after_line = next(
        (l for l in pathlib.Path(stable_manifest).read_text().splitlines()
         if l.startswith("0 ")), "")
    if before_line and after_line:
        if before_line.split()[:2] != after_line.split()[:2]:
            failures.append("build-art: accepted update moved the picnum or renamed the tile")
        if before_line == after_line:
            failures.append("build-art: accepted update did not refresh the content hash")

    # Options belong to one command; values may not be option tokens; stray
    # positionals are usage errors, not silently ignored.
    run(["build-art", "--source", str(src_dir / "tiles/diagnostics.tileset"),
         "--out", art_out, "--palette-out", "/tmp/x.dat"], 2,
        "build-art rejects a build-palette option")
    run(["build-art", "--source", str(src_dir / "tiles/diagnostics.tileset"),
         "--out", "--wat", "--init-manifest"], 2, "build-art --out needs a value")
    run(["build-art", "--source", str(src_dir / "tiles/diagnostics.tileset"),
         "--out", art_out, "stray"], 2, "build-art rejects positionals")
    run(["build-palette", "--source", str(src_dir / "palettes/diagnostic.palette"),
         "--palette-out", str(pathlib.Path(tmp) / "p.dat"), "--init-manifest"], 2,
        "build-palette rejects a build-art option")

    # Publishing over an input destroys the source; two outputs sharing a path
    # means one silently wins.
    run(["build-art", "--source", str(src_dir / "tiles/diagnostics.tileset"),
         "--out", str(src_dir / "tiles/diagnostics.tileset"), "--init-manifest"], 2,
        "build-art refuses to overwrite its source")
    same = str(pathlib.Path(tmp) / "same.dat")
    run(["build-palette", "--source", str(src_dir / "palettes/diagnostic.palette"),
         "--palette-out", same, "--lookup-out", same], 2,
        "build-palette refuses colliding outputs")

    run(["build-art", "--source", "/nonexistent.tileset", "--out", art_out,
         "--init-manifest"], 1, "build-art missing source", expect_err="io_error")
    bad_ts = pathlib.Path(tmp) / "bad.tileset"
    bad_ts.write_text("tileset t\ntile a 8 8\n")
    run(["build-art", "--source", str(bad_ts), "--out", art_out, "--init-manifest"], 1,
        "build-art bad tileset", expect_err="tileset")
    run(["build-art", "--source", str(src_dir / "tiles/diagnostics.tileset")], 2,
        "build-art missing --out")
    run(["build-art", "--wat"], 2, "build-art unknown option")
    run(["build-palette", "--source"], 2, "build-palette dangling option value")
    run(["build-palette"], 2, "build-palette arity")

    # GRP-backed positive paths for every M4 dump command. Slice 4 consumes
    # exactly this route (GRP -> VFS -> ART/palette/lookup -> atlas), so it
    # should inherit a proven mount path rather than being its first caller.
    assets = pathlib.Path(tmp) / "assets.grp"
    entries = [
        ("TILES000.ART", pathlib.Path(art_out).read_bytes()),
        ("PALETTE.DAT", pathlib.Path(pal).read_bytes()),
        ("LOOKUP.DAT", pathlib.Path(lut).read_bytes()),
    ]
    header = b"KenSilverman" + len(entries).to_bytes(4, "little")
    directory = b"".join(
        name.encode("ascii").ljust(12, b"\x00") + len(data).to_bytes(4, "little")
        for name, data in entries)
    assets.write_bytes(header + directory + b"".join(data for _, data in entries))

    proc = run(["dump-art", "--grp", str(assets), "TILES000.ART"], 0, "dump-art via grp",
               expect_out="version: 1")
    if "tile range:" not in proc.stdout:
        failures.append("dump-art via grp: no tile range reported")
    run(["dump-palette", "--grp", str(assets), "PALETTE.DAT"], 0, "dump-palette via grp",
        expect_out="shade tables:")
    run(["dump-lookup", "--grp", str(assets), "LOOKUP.DAT"], 0, "dump-lookup via grp",
        expect_out="swaps:")
    # Case-folding is part of the mount contract, and a miss must be structured.
    run(["dump-art", "--grp", str(assets), "tiles000.art"], 0, "dump-art via grp, case-folded",
        expect_out="version: 1")
    run(["dump-art", "--grp", str(assets), "NOSUCH.ART"], 1, "dump-art grp miss",
        expect_err="not_found")
    run(["dump-palette", "--grp", str(assets), "NOSUCH.DAT"], 1, "dump-palette grp miss",
        expect_err="not_found")
    run(["dump-lookup", "--grp", str(assets), "NOSUCH.DAT"], 1, "dump-lookup grp miss",
        expect_err="not_found")
    # The mount must deliver identical bytes to the loose-file path.
    direct = run(["dump-art", art_out], 0, "dump-art direct")
    if direct.stdout.split("\n")[1:] != proc.stdout.split("\n")[1:]:
        failures.append("dump-art: GRP-backed output differs from the loose-file output")

    run(["no-such-command"], 2, "unknown command", expect_err="unknown command")

    # ---------------- atlas inspection (M4 slice 4) ----------------
    # The committed synthetic fixture exercises the loose-directory route...
    proc = run(["inspect-atlas", "--dir", "fixtures/atlas"], 0, "inspect-atlas dir",
               expect_out="validation: OK")
    for expected in ["global range: 0..10 (11 picnums)", "populated tiles: 5",
                     "empty tiles: 6 (4 gap, 2 zero-dimension)", "atlas pages: 1",
                     "indexed bytes: 4194304", "palette: loaded through VFS",
                     "lookup: loaded through VFS", "ART sources: 2"]:
        if expected not in proc.stdout:
            failures.append(f"inspect-atlas dir: stdout missing {expected!r}")

    # ...and a python-packed synthetic GRP exercises the production mount
    # (no extraction anywhere). Packing in python is deliberate: it proves
    # our writer-agnostic parser reads a container built by foreign code.
    import struct as _struct
    grp_path = pathlib.Path(tmp) / "synthetic.grp"
    entries = []
    for name in ["TILES000.ART", "TILES001.ART", "PALETTE.DAT", "LOOKUP.DAT"]:
        entries.append((name, (root / "fixtures/atlas" / name).read_bytes()))
    blob = bytearray(b"KenSilverman")
    blob += _struct.pack("<I", len(entries))
    for name, data in entries:
        field = name.encode("ascii")[:12]
        blob += field + b"\x00" * (12 - len(field))
        blob += _struct.pack("<I", len(data))
    for _, data in entries:
        blob += data
    grp_path.write_bytes(bytes(blob))
    proc = run(["inspect-atlas", "--grp", str(grp_path)], 0, "inspect-atlas grp",
               expect_out="validation: OK")
    if "global range: 0..10 (11 picnums)" not in proc.stdout:
        failures.append("inspect-atlas grp: composed range wrong through GRP mount")
    if "grp:" not in proc.stdout:
        failures.append("inspect-atlas grp: source line should describe the grp mount")

    # Deterministic placement: two runs, identical summary (and the byte
    # identity itself is unit-tested in atlas.test.cpp).
    second = run(["inspect-atlas", "--grp", str(grp_path)], 0, "inspect-atlas grp rerun",
                 expect_out="validation: OK")
    if second.stdout != proc.stdout:
        failures.append("inspect-atlas: two runs of the same GRP disagree")

    # Usage and failure contracts.
    run(["inspect-atlas"], 2, "inspect-atlas usage (no source)", expect_err="usage")
    run(["inspect-atlas", "--grp", str(grp_path), "--dir", "fixtures/atlas"], 2,
        "inspect-atlas usage (both sources)", expect_err="usage")
    # Discovery fails closed with a precise message when no ART exists.
    empty_dir = pathlib.Path(tmp) / "empty-dir"
    empty_dir.mkdir()
    run(["inspect-atlas", "--dir", str(empty_dir)], 1,
        "inspect-atlas empty dir", expect_err="no TILES*.ART")

    # Overlapping ART ranges inside a GRP must be rejected end to end:
    # duplicate TILES000.ART under a second name claiming the same range.
    overlap = bytearray()
    overlap += b"KenSilverman" + _struct.pack("<I", 5)
    overlap_entries = [(name, data) for name, data in entries]
    overlap_entries.append(("TILES999.ART", entries[0][1]))
    for name, data in overlap_entries:
        field = name.encode("ascii")[:12]
        overlap += field + b"\x00" * (12 - len(field))
        overlap += _struct.pack("<I", len(data))
    for _, data in overlap_entries:
        overlap += data
    overlap_path = pathlib.Path(tmp) / "overlap.grp"
    overlap_path.write_bytes(bytes(overlap))
    run(["inspect-atlas", "--grp", str(overlap_path)], 1, "inspect-atlas overlap",
        expect_err="atlas.range_overlap")

if failures:
    print("fbtool check FAILED:")
    for f in failures:
        print(f"  {f}")
    sys.exit(1)

print("fbtool check: grp + map + palette + art + build + atlas contracts hold")

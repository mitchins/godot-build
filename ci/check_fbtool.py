#!/usr/bin/env python3
"""fbtool command-contract gate: exercises dump-grp / gen-grp end to end.

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

    run(["no-such-command"], 2, "unknown command", expect_err="unknown command")

if failures:
    print("fbtool check FAILED:")
    for f in failures:
        print(f"  {f}")
    sys.exit(1)

print("fbtool check: grp + map command contracts hold")

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
    run(["no-such-command"], 2, "unknown command", expect_err="unknown command")

if failures:
    print("fbtool check FAILED:")
    for f in failures:
        print(f"  {f}")
    sys.exit(1)

print("fbtool check: dump-grp/gen-grp contracts hold")

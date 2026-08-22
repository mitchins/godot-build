#!/usr/bin/env python3
"""Fuzz-gate self-check (D0010).

The fuzz driver used to fall back to a single empty seed when a corpus path
could not be loaded, so a typo'd path reported "completed without crashes"
having executed no committed input. A gate that cannot fail is not a gate:
this asserts the driver refuses to report success on a missing corpus, and
still succeeds on the real one.
"""

import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
binary = sys.argv[1] if len(sys.argv) > 1 else str(root / "build/fuzz/fauxbuild_fuzz_grp")
corpus = root / "tests/fuzz/corpus/grp"

failures = []

missing = subprocess.run([binary, "-runs=1", str(root / "tests/fuzz/NO_SUCH_CORPUS")],
                         capture_output=True, text=True, cwd=root)
if missing.returncode == 0:
    failures.append("missing corpus path reported success (exit 0)")

real = subprocess.run([binary, "-runs=200", "-max_len=4096", str(corpus)],
                      capture_output=True, text=True, cwd=root)
if real.returncode != 0:
    failures.append(f"real corpus run failed with exit {real.returncode}")
seeds = [line for line in real.stderr.splitlines() if "seeds" in line]
if not seeds or " 1 seeds" in seeds[0]:
    failures.append(f"real corpus did not load committed seeds: {seeds}")

if failures:
    print("fuzz gate check FAILED:")
    for f in failures:
        print(f"  {f}")
    sys.exit(1)

print(f"fuzz gate check: driver fails closed on a missing corpus; {seeds[0].strip()}")

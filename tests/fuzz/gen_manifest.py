#!/usr/bin/env python3
"""Regenerates tests/fuzz/MANIFEST. Run after any corpus change and commit
both together; ci/check_corpus.py fails `check` when they drift."""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "ci"))
from check_corpus import compute_manifest  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
target = ROOT / "tests/fuzz" / "MANIFEST"
target.write_text("\n".join(compute_manifest()) + "\n")
print(f"wrote {target} ({len(compute_manifest())} entries)")

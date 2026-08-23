#!/usr/bin/env python3
"""Corpus integrity gate (M3 review, item 3).

The committed fuzz corpus is loaded by tests at runtime through __FILE__
paths, invisible to SCons. A corrupted, deleted, or sneakily-added corpus
file would otherwise leave every gate green (the exact failure shape of the
M2/M3 reviews). This script recomputes the corpus manifest (FNV-1a64 + size,
matching fauxbuild::fnv1a64) and diffs it against the committed MANIFEST:
any difference fails the check.
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FNV_OFFSET = 14695981039346656037  # 0xcbf29ce484222325
FNV_PRIME = 1099511628211
MASK = (1 << 64) - 1


def fnv1a64(data: bytes) -> int:
    h = FNV_OFFSET
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK
    return h


# Published FNV-1a 64-bit vectors. If this port and fauxbuild::fnv1a64 ever
# diverge, one of the two known-answer checks fails instead of the manifest
# quietly agreeing with itself.
KNOWN_VECTORS = {
    b"": 0xCBF29CE484222325,
    b"a": 0xAF63DC4C8601EC8C,
    b"foobar": 0x85944171F73967E8,
}


def check_known_vectors() -> list[str]:
    return [
        f"fnv1a64({data!r}) = {fnv1a64(data):#018x}, expected {want:#018x}"
        for data, want in KNOWN_VECTORS.items()
        if fnv1a64(data) != want
    ]


def compute_manifest() -> list[str]:
    lines = []
    for group in sorted(ROOT.glob("tests/fuzz/*/")):
        for path in sorted(group.rglob("*")):
            # Exact match only: a prefix rule (README*) would let files like
            # README_evil.bin evade the integrity gate while still being
            # loaded as seeds by the fuzz driver (slice-2 review finding 1).
            excluded_names = {"MANIFEST", ".gitkeep", "README.md"}
            if not path.is_file() or path.name in excluded_names:
                continue
            rel = path.relative_to(ROOT).as_posix()
            data = path.read_bytes()
            lines.append(f"{fnv1a64(data):016x} {len(data):>8} {rel}")
    return lines


def main() -> int:
    manifest_path = ROOT / "tests/fuzz" / "MANIFEST"
    committed = manifest_path.read_text().splitlines() if manifest_path.exists() else []
    actual = compute_manifest()

    committed_set = set(committed)
    actual_set = set(actual)

    problems = []
    for bad in check_known_vectors():
        problems.append(f"hash mismatch: {bad}")

    # An empty corpus matches an empty manifest, so the diff alone would
    # report green having verified nothing. The corpus is never empty.
    if not actual:
        print("corpus check FAILED: no corpus files found under tests/fuzz/")
        return 1

    for line in sorted(committed_set - actual_set):
        problems.append(f"stale/changed: {line}")
    for line in sorted(actual_set - committed_set):
        problems.append(f"unlisted/changed: {line}")

    if problems:
        print("corpus check FAILED (manifest out of date or files corrupted):")
        for problem in problems:
            print(f"  {problem}")
        print("regenerate with: python3 tests/fuzz/gen_manifest.py")
        return 1

    print(f"corpus check: {len(actual)} files match MANIFEST")
    return 0


if __name__ == "__main__":
    sys.exit(main())

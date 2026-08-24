#!/usr/bin/env python3
"""Corpus filtering contract, as an executable trace.

The fuzz loader and the manifest gate must agree on exactly one rule, or a
file can be an untracked input that influences fuzzing while evading the
integrity check. A prefix rule (README*) previously did precisely that.

Contract:
  README.md        ignored by both      (documentation, never a seed)
  README_evil.bin  seed AND manifest-covered  (a .bin is always both)
  foo.txt          neither
"""

import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / "ci"))
from check_corpus import compute_manifest  # noqa: E402

fuzzer = sys.argv[1] if len(sys.argv) > 1 else str(root / "build/fuzz/fauxbuild_fuzz_palette")
failures = []


def seed_count(directory):
    proc = subprocess.run([fuzzer, "-runs=1", str(directory)], capture_output=True, text=True)
    if proc.returncode != 0:
        failures.append(f"loader failed on {directory}: {proc.stderr.strip()}")
        return -1
    # The driver reports on stderr; read both so this does not depend on it.
    text = proc.stdout + proc.stderr
    marker = "runs over "
    at = text.find(marker)
    if at < 0:
        failures.append(f"loader printed no seed count for {directory}")
        return -1
    return int(text[at + len(marker):].split()[0])


with tempfile.TemporaryDirectory() as tmp:
    corpus = pathlib.Path(tmp) / "corpus"
    corpus.mkdir()
    (corpus / "a.bin").write_bytes(b"\x00" * 8)
    base = seed_count(corpus)

    # README.md: documentation. Not a seed, and not manifest-covered.
    (corpus / "README.md").write_text("generated fixtures, not extracted\n")
    if seed_count(corpus) != base:
        failures.append("README.md was loaded as a fuzz seed")

    # foo.txt: not a .bin, so not a seed either.
    (corpus / "foo.txt").write_text("scratch\n")
    if seed_count(corpus) != base:
        failures.append("a non-.bin file was loaded as a fuzz seed")

    # README_evil.bin: a .bin is a seed, so it must also be manifest-covered.
    (corpus / "README_evil.bin").write_bytes(b"\x01" * 4)
    if seed_count(corpus) != base + 1:
        failures.append("README_evil.bin was not loaded as a seed despite being .bin")

# The manifest side must use an exact-name rule. A prefix rule (README*) would
# leave a README_*.bin loadable as a seed but invisible to the integrity gate,
# so probe it directly rather than relying on such a file happening to exist.
probe = root / "tests/fuzz/corpus/palette/README_probe.bin"
try:
    probe.write_bytes(b"\x00\x00\x00\x00")
    probe_rel = probe.relative_to(root).as_posix()
    if probe_rel not in {line.split()[-1] for line in compute_manifest()}:
        failures.append(
            f"{probe_rel}: a .bin seed escaped the manifest — the exclusion rule is a "
            "prefix match, not an exact one")
finally:
    probe.unlink(missing_ok=True)

# The manifest side of the same contract, over the real corpus tree.
listed = {line.split()[-1] for line in compute_manifest()}
for path in sorted((root / "tests/fuzz").rglob("*")):
    if not path.is_file() or path.name in {"MANIFEST", ".gitkeep"}:
        continue
    rel = path.relative_to(root).as_posix()
    if path.suffix == ".bin" and rel not in listed:
        failures.append(f"{rel}: loadable as a seed but not manifest-covered")
    if path.name == "README.md" and rel in listed:
        failures.append(f"{rel}: documentation should not be manifest-covered")

if failures:
    print("corpus filter check FAILED:")
    for item in failures:
        print(f"  {item}")
    sys.exit(1)

print("corpus filter check: .bin seeds are manifest-covered; README.md is neither")

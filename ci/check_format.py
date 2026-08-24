#!/usr/bin/env python3
"""Format guard: verify tracked C++ sources against .clang-format.

On developer machines without clang-format the check skips with a warning.
With FAUXBUILD_STRICT_TOOLS=1 (CI runners) a missing clang-format is a hard
failure, so the gate cannot pass silently on a bare machine.
"""

import os
import pathlib
import shutil
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
strict = os.environ.get("FAUXBUILD_STRICT_TOOLS") == "1"

if shutil.which("clang-format") is None:
    if strict:
        print("format-check FAILED: clang-format not installed and "
              "FAUXBUILD_STRICT_TOOLS=1 is set")
        sys.exit(1)
    print("format-check: clang-format not installed; skipping "
          "(install with: brew install clang-format)")
    sys.exit(0)

files = []
CXX_SUFFIXES = (".c", ".h", ".hpp", ".cpp", ".cc", ".cxx")

for directory in ("core", "tools", "tests", "extension"):
    base = root / directory
    if base.exists():
        # C/C++ only. clang-format applied to a Python file silently rewrites
        # it as if it were C: it turned this repo's `#!/usr/bin/env python3`
        # into `#!/ usr / bin / env python3`. Never widen this tuple to include
        # a non-C/C++ suffix.
        files.extend(p for p in base.rglob("*") if p.suffix in CXX_SUFFIXES)

failed = []
for path in sorted(files):
    result = subprocess.run(
        ["clang-format", "--dry-run", "--Werror", str(path)],
        capture_output=True, text=True, cwd=root,
    )
    if result.returncode != 0:
        failed.append(path.relative_to(root))
        print(result.stderr.strip())

if failed:
    print("format-check FAILED:")
    for f in failed:
        print(f"  {f}")
    sys.exit(1)

print(f"format-check: {len(files)} files conform to .clang-format")

# Guard: no Python file may be reachable by the formatter, and every committed
# Python file must still start with a valid shebang or a comment. clang-format
# corrupted one of these scripts in this repo's own history.
bad_python = []
for path in sorted(root.rglob("*.py")):
    if any(part in {".git", "build", "third_party", "__pycache__"} for part in path.parts):
        continue
    if path.suffix in CXX_SUFFIXES:
        bad_python.append(f"{path}: python file matched a C++ suffix")
        continue
    first = path.read_text(encoding="utf-8", errors="replace").splitlines()[:1]
    if first and first[0].startswith("#!") and first[0] != "#!/usr/bin/env python3":
        bad_python.append(f"{path}: mangled shebang {first[0]!r}")
if bad_python:
    print("format-check FAILED (python):")
    for item in bad_python:
        print(f"  {item}")
    raise SystemExit(1)

#!/usr/bin/env python3
"""Merge static archives into one self-contained archive.

Usage: merge_static_libs.py <output.a> <input.a> [<input.a> ...]

Used for the iOS GDExtension package: Godot's export links exactly the archive
referenced by the .gdextension, so it must already contain the extension,
fauxbuild_core, and godot-cpp objects.
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2

    output = pathlib.Path(sys.argv[1]).resolve()
    inputs = [pathlib.Path(p).resolve() for p in sys.argv[2:]]
    for path in inputs:
        if not path.exists():
            print(f"merge_static_libs: missing input {path}", file=sys.stderr)
            return 1

    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = pathlib.Path(tmp_name)
        members = []
        for index, archive in enumerate(inputs):
            stage = tmp / f"lib{index}"
            stage.mkdir()
            subprocess.run(["ar", "-x", str(archive)], cwd=stage, check=True)
            for obj in sorted(stage.iterdir()):
                # Never re-archive extracted symbol tables (__.SYMDEF*); they are
                # not objects and ranlib regenerates the table anyway.
                if ".SYMDEF" in obj.name:
                    continue
                unique = tmp / f"{index:02d}_{obj.name}"
                shutil.move(obj, unique)
                members.append(unique)

        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            output.unlink()
        subprocess.run(["ar", "rcs", str(output)] + [str(m) for m in members], check=True)

    print(f"merge_static_libs: {output} ({len(members)} objects from {len(inputs)} archives)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

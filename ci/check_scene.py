#!/usr/bin/env python3
"""Scene gate: run the sample scene headless and verify the extension is live.

Usage: check_scene.py [path-to-godot-binary]

Cold-cache note (docs/IOS.md): with a GDExtension present, the first headless
editor scan over a cold .godot cache crashes inside Godot 4.7.2 (engine-internal
backtrace, our library is not in the stack). The cache is still written, so the
second import is clean. This script tolerates exactly one crashed import and
requires the retry to succeed.
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def run_godot(godot: str, args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run([godot, "--headless", "--path", str(ROOT / "godot")] + args,
                          capture_output=True, text=True, timeout=600)


def main() -> int:
    godot = sys.argv[1] if len(sys.argv) > 1 else \
        "/Applications/Godot.app/Contents/MacOS/Godot"

    for attempt in (1, 2):
        result = run_godot(godot, ["--import"])
        if result.returncode == 0:
            break
        if attempt == 2:
            print("scene-check FAILED: headless --import failed twice", file=sys.stderr)
            print((result.stdout + result.stderr)[-3000:], file=sys.stderr)
            return 1
        print("scene-check: first import crashed (known cold-cache Godot bug); retrying")

    scene = run_godot(godot, ["res://scenes/main.tscn", "--quit-after", "3"])
    output = scene.stdout + scene.stderr
    if scene.returncode != 0 or "FauxBuild core version:" not in output \
            or "M1 sample scene: OK" not in output:
        print(f"scene-check FAILED (exit {scene.returncode})", file=sys.stderr)
        print(output[-3000:], file=sys.stderr)
        return 1

    # M4 slice 4: the consumer-boundary scene. Runs the synthetic fixture
    # assertions (index bytes/rect metadata as Godot actually receives them)
    # and instantiates the indexed-atlas preview. Real-GRP runs of this
    # scene are the human gate (--grp local_reference/...), not CI.
    atlas_scene = run_godot(
        godot, ["res://scenes/atlas_preview.tscn", "--quit-after", "3", "--"])
    output = atlas_scene.stdout + atlas_scene.stderr
    if atlas_scene.returncode != 0 or "M4 consumer boundary: OK" not in output \
            or "M4 atlas preview: OK" not in output:
        print(f"scene-check FAILED: atlas consumer boundary (exit "
              f"{atlas_scene.returncode})", file=sys.stderr)
        print(output[-3000:], file=sys.stderr)
        return 1

    print("scene-check: sample scene ran with the extension live")
    print("scene-check: atlas consumer boundary + preview verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())

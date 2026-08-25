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


def run_godot(godot: str, args: list[str], timeout: int = 600) -> subprocess.CompletedProcess:
    return subprocess.run([godot, "--headless", "--path", str(ROOT / "godot")] + args,
                          capture_output=True, text=True, timeout=timeout)


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
    # and instantiates the indexed-atlas preview. It is fixture-only by
    # construction and refuses --grp: the human gate uses a separate scene
    # (atlas_preview_human.tscn), so this test never becomes conditional.
    atlas_scene = run_godot(
        godot, ["res://scenes/atlas_preview.tscn", "--quit-after", "3", "--"])
    output = atlas_scene.stdout + atlas_scene.stderr
    if atlas_scene.returncode != 0 or "M4 consumer boundary: OK" not in output \
            or "M4 atlas preview: OK" not in output:
        print(f"scene-check FAILED: atlas consumer boundary (exit "
              f"{atlas_scene.returncode})", file=sys.stderr)
        print(output[-3000:], file=sys.stderr)
        return 1

    # The human harness is exercised here too, against the same fixture, so it
    # cannot rot between the human runs it exists for. It asserts only
    # content-independent invariants, hence no fixture constants to keep in
    # sync.
    human = run_godot(
        godot, ["res://scenes/atlas_preview_human.tscn", "--quit-after", "3", "--"])
    human_output = human.stdout + human.stderr
    if human.returncode != 0 or "atlas-preview: ready for inspection" not in human_output:
        print(f"scene-check FAILED: human atlas harness (exit {human.returncode})",
              file=sys.stderr)
        print(human_output[-3000:], file=sys.stderr)
        return 1

    # ...and it must refuse to run the fixture-only test against real content.
    guarded = run_godot(
        godot, ["res://scenes/atlas_preview.tscn", "--quit-after", "3", "--",
                "--grp", "/nonexistent.grp"])
    # Both halves are required. Matching only the message would pass a scene
    # that printed the refusal and then ran anyway, or exited 0 -- the refusal
    # is a *status* contract, not a log line (CodeRabbit, PR #6; the M4 gate
    # had the same weakness as the M5 one and is fixed here to keep a single
    # standard).
    if guarded.returncode != 2 or \
            "only runs against the synthetic fixture" not in (guarded.stdout + guarded.stderr):
        print(f"scene-check FAILED: the CI boundary test did not refuse --grp "
              f"(exit {guarded.returncode}, expected 2)", file=sys.stderr)
        return 1

    # M5 slice 2: the structural consumer boundary. The scene reads what
    # Godot actually received (MeshInstance3D.mesh -> ArrayMesh ->
    # surface_get_arrays) and compares it against the StructuralWorld the
    # harness packed independently of the view. Fixture-only by
    # construction; real content is slice 3.
    struct_scene = run_godot(
        # 30 frames, not 3: the lifecycle regressions deliberately await frame
        # boundaries so external queue_free() actually completes before the
        # boundary is re-read. A budget too small to reach the awaits would
        # make the scene look silent rather than failing.
        godot, ["res://scenes/structural_view_test.tscn", "--quit-after", "30", "--"])
    output = struct_scene.stdout + struct_scene.stderr
    if struct_scene.returncode != 0 or "M5 structural consumer boundary: OK" not in output:
        print(f"scene-check FAILED: structural consumer boundary (exit "
              f"{struct_scene.returncode})", file=sys.stderr)
        print(output[-3000:], file=sys.stderr)
        return 1

    # The human synthetic structural viewer is launched headless with a
    # short timeout so it cannot rot between the human runs it exists for.
    # It asserts nothing fixture-specific here; the ready marker proves the
    # whole harness (fixture -> world -> view -> camera framing) builds.
    try:
        struct_human = run_godot(
            godot, ["res://scenes/structural_view_human.tscn", "--quit-after", "3", "--"],
            timeout=120)
    except subprocess.TimeoutExpired:
        print("scene-check FAILED: human structural viewer timed out", file=sys.stderr)
        return 1
    output = struct_human.stdout + struct_human.stderr
    if struct_human.returncode != 0 or "structural-view: ready for inspection" not in output:
        print(f"scene-check FAILED: human structural viewer (exit "
              f"{struct_human.returncode})", file=sys.stderr)
        print(output[-3000:], file=sys.stderr)
        return 1

    # ...and the CI structural test must refuse real-content input too.
    struct_guarded = run_godot(
        godot, ["res://scenes/structural_view_test.tscn", "--quit-after", "3", "--",
                "--grp", "/nonexistent.grp"])
    if struct_guarded.returncode != 2 or "only runs against committed synthetic fixtures" \
            not in (struct_guarded.stdout + struct_guarded.stderr):
        print(f"scene-check FAILED: the CI structural test did not refuse --grp "
              f"(exit {struct_guarded.returncode}, expected 2)", file=sys.stderr)
        return 1

    print("scene-check: sample scene ran with the extension live")
    print("scene-check: atlas consumer boundary + preview verified")
    print("scene-check: human atlas harness runs and the CI test refuses real content")
    print("scene-check: structural consumer boundary verified; human viewer runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())

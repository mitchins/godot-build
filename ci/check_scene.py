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
import math
import re
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def run_godot(godot: str, args: list[str], timeout: int = 600) -> subprocess.CompletedProcess:
    return subprocess.run([godot, "--headless", "--path", str(ROOT / "godot")] + args,
                          capture_output=True, text=True, timeout=timeout)


def _vec(text: str, key: str):
    """Pull `key=(a,b,c)` out of a probe line as three floats."""
    match = re.search(re.escape(key) + r"=\(([^)]*)\)", text)
    if match is None:
        return None
    parts = match.group(1).split(",")
    if len(parts) != 3:
        return None
    try:
        return [float(p) for p in parts]
    except ValueError:
        return None


def check_human_framing(output: str) -> bool:
    """Judge the human viewer's headless framing/toggle observations.

    Presentation is not a contract, so this asserts only what a broken
    viewer would get wrong in a way a human would then waste time on: a
    camera at non-finite coordinates, a camera not aimed at the geometry it
    just framed, clip planes that cannot show it, or a visibility toggle
    that quietly rebuilt or mutated mesh arrays.
    """
    framing = None
    toggle = None
    for line in output.splitlines():
        if line.startswith("structural-view framing:"):
            framing = line
        elif line.startswith("structural-view toggle:"):
            toggle = line
    if framing is None:
        print("scene-check FAILED: human viewer printed no framing probe", file=sys.stderr)
        return False

    eye = _vec(framing, "eye")
    center = _vec(framing, "center")
    forward = _vec(framing, "fwd")
    size = _vec(framing, "size")
    if eye is None or center is None or forward is None or size is None:
        print(f"scene-check FAILED: unreadable framing probe: {framing}", file=sys.stderr)
        return False

    planes = {}
    for key in ("near", "far", "speed"):
        match = re.search(key + r"=(-?[0-9.eE+]+)", framing)
        if match is None:
            print(f"scene-check FAILED: framing probe missing {key}: {framing}", file=sys.stderr)
            return False
        planes[key] = float(match.group(1))

    for label, values in (("eye", eye), ("center", center), ("fwd", forward), ("size", size)):
        for value in values:
            if not math.isfinite(value):
                print(f"scene-check FAILED: human viewer {label} is not finite: {framing}",
                      file=sys.stderr)
                return False
    for key, value in planes.items():
        if not math.isfinite(value):
            print(f"scene-check FAILED: human viewer {key} is not finite: {framing}",
                  file=sys.stderr)
            return False

    # The camera must actually look at the bounds it framed.
    to_center = [center[i] - eye[i] for i in range(3)]
    span = math.sqrt(sum(c * c for c in to_center))
    forward_len = math.sqrt(sum(c * c for c in forward))
    if span <= 0.0 or forward_len <= 0.0:
        print(f"scene-check FAILED: degenerate framing vectors: {framing}", file=sys.stderr)
        return False
    alignment = sum(to_center[i] * forward[i] for i in range(3)) / (span * forward_len)
    if alignment < 0.999:
        print(f"scene-check FAILED: human viewer camera is not aimed at the bounds "
              f"(alignment {alignment:.6f}): {framing}", file=sys.stderr)
        return False

    # It must start above the geometry and outside it, and the clip planes
    # must be able to show what it framed.
    if eye[1] <= center[1]:
        print(f"scene-check FAILED: human viewer camera did not start above the "
              f"bounds centre: {framing}", file=sys.stderr)
        return False
    if not 0.0 < planes["near"] < planes["far"]:
        print(f"scene-check FAILED: human viewer clip planes are unusable: {framing}",
              file=sys.stderr)
        return False
    if planes["far"] < span:
        print(f"scene-check FAILED: human viewer far plane {planes['far']} cannot reach "
              f"the geometry it framed ({span}): {framing}", file=sys.stderr)
        return False
    if planes["speed"] <= 0.0:
        print(f"scene-check FAILED: human viewer traversal speed is not positive: {framing}",
              file=sys.stderr)
        return False

    # Visibility toggling must not touch mesh contents.
    if toggle is None:
        print("scene-check FAILED: human viewer printed no ceiling-toggle probe",
              file=sys.stderr)
        return False
    if toggle.endswith("absent"):
        print("scene-check FAILED: the framing fixture presented no Ceilings group, so "
              "the toggle probe proved nothing", file=sys.stderr)
        return False
    visibility = re.search(r"vis=(\w+),(\w+),(\w+)", toggle)
    if visibility is None or visibility.groups() != ("false", "true", "false"):
        print(f"scene-check FAILED: ceilings should start hidden and round-trip "
              f"false->true->false: {toggle}", file=sys.stderr)
        return False
    for key in ("mesh", "vhash", "ihash"):
        match = re.search(key + r"=(-?\d+),(-?\d+)", toggle)
        if match is None:
            print(f"scene-check FAILED: toggle probe missing {key}: {toggle}", file=sys.stderr)
            return False
        if match.group(1) != match.group(2):
            print(f"scene-check FAILED: toggling ceilings changed {key} "
                  f"({match.group(1)} -> {match.group(2)}): visibility must not rebuild or "
                  f"mutate geometry", file=sys.stderr)
            return False
    return True


def check_start_pose_focus(godot: str) -> bool:
    """Real-content mode must aim the initial view at the map's start pose.

    Driven with committed synthetic content through the same source route
    real content uses, so this is CI truth. The expected point is derived
    from the fixture spec and the format's two scale factors, NOT read back
    from the implementation: multi_loop starts at Build (8192, 8192, 4096),
    which is (8192/2048, -4096/32768, 8192/2048) in render space. The
    fixture's start is off-centre on every axis, so aiming at the AABB
    centre instead cannot pass by coincidence.
    """
    fbtool = ROOT / "build/dev/fbtool"
    if not fbtool.exists():
        print(f"scene-check FAILED: {fbtool} missing; build it before the scene gate",
              file=sys.stderr)
        return False

    with tempfile.TemporaryDirectory(prefix="fauxbuild_startpose_") as scratch:
        written = subprocess.run(
            [str(fbtool), "gen-map", "--fixture", "multi_loop",
             "--out", str(pathlib.Path(scratch) / "MULTI_LOOP.MAP")],
            capture_output=True, text=True)
        if written.returncode != 0:
            print(f"scene-check FAILED: could not write the start-pose fixture: "
                  f"{written.stdout + written.stderr}", file=sys.stderr)
            return False

        result = run_godot(godot, ["res://scenes/structural_view_human.tscn",
                                   "--quit-after", "3", "--",
                                   "--dir", scratch, "--map", "MULTI_LOOP.MAP"], timeout=120)
    output = result.stdout + result.stderr
    if result.returncode != 0 or "structural-view: ready for inspection" not in output:
        print(f"scene-check FAILED: start-pose viewer run (exit {result.returncode})",
              file=sys.stderr)
        print(output[-2000:], file=sys.stderr)
        return False

    expected = (8192.0 / 2048.0, -4096.0 / 32768.0, 8192.0 / 2048.0)
    match = re.search(r"start render position: "
                      r"(-?[0-9.]+), (-?[0-9.]+), (-?[0-9.]+)", output)
    if match is None:
        print("scene-check FAILED: source mode printed no start position", file=sys.stderr)
        print(output[-2000:], file=sys.stderr)
        return False
    reported = tuple(float(g) for g in match.groups())
    for axis, (got, want) in enumerate(zip(reported, expected)):
        if abs(got - want) > 1e-4:
            print(f"scene-check FAILED: start position axis {axis} is {got}, expected "
                  f"{want} from the fixture spec through to_render_space", file=sys.stderr)
            return False

    # The framing must actually adopt it, and say so.
    if "focus=start" not in output:
        print("scene-check FAILED: real-content mode did not focus on the start pose",
              file=sys.stderr)
        return False
    framing = next((line for line in output.splitlines()
                    if line.startswith("structural-view framing:")), None)
    aim = _vec(framing or "", "center")
    if aim is None:
        print("scene-check FAILED: start-pose run printed no framing probe", file=sys.stderr)
        return False
    for axis, (got, want) in enumerate(zip(aim, expected)):
        if abs(got - want) > 1e-4:
            print(f"scene-check FAILED: camera aim axis {axis} is {got}, expected the "
                  f"start pose {want}", file=sys.stderr)
            return False

    # P (start mode) must place the camera EXACTLY on the authored point --
    # no floor snapping, no ground-level correction, no capsule height. The
    # overview aims at the start from a distance; this is the separate
    # observation of whether the authored pose sits inside architecture.
    start_mode = _vec(next((line for line in output.splitlines()
                            if line.startswith("structural-view start-mode:")), ""), "pos")
    if start_mode is None:
        print("scene-check FAILED: source mode never exercised the start camera mode",
              file=sys.stderr)
        return False
    for axis, (got, want) in enumerate(zip(start_mode, expected)):
        if abs(got - want) > 1e-4:
            print(f"scene-check FAILED: start-mode camera axis {axis} is {got}, expected "
                  f"the authored position {want}. The camera must land on the start pose "
                  f"exactly -- snapping or height correction would destroy authored "
                  f"information M5 cannot reconstruct", file=sys.stderr)
            return False
    if "camera mode: start" not in output:
        print("scene-check FAILED: start camera mode was never announced", file=sys.stderr)
        return False

    # The synthetic fixture mode keeps the whole-world AABB centre.
    fallback = run_godot(godot, ["res://scenes/structural_view_human.tscn",
                                 "--quit-after", "3", "--",
                                 "--fixture", "multi_loop"], timeout=120)
    fallback_output = fallback.stdout + fallback.stderr
    if fallback.returncode != 0 or "focus=centre" not in fallback_output:
        print(f"scene-check FAILED: synthetic fixture mode should fall back to the AABB "
              f"centre (exit {fallback.returncode})", file=sys.stderr)
        print(fallback_output[-2000:], file=sys.stderr)
        return False
    fallback_aim = _vec(next((line for line in fallback_output.splitlines()
                              if line.startswith("structural-view framing:")), ""), "center")
    if fallback_aim is None:
        print("scene-check FAILED: fixture-mode run printed no framing probe", file=sys.stderr)
        return False
    if all(abs(fallback_aim[i] - expected[i]) <= 1e-4 for i in range(3)):
        print("scene-check FAILED: the fixture's start pose and its AABB centre coincide, "
              "so this test cannot tell the two focus modes apart", file=sys.stderr)
        return False
    return True


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

    # The viewer's headless probe prints raw observations only; the checks
    # below are what judge them, so a broken viewer cannot report itself
    # healthy. Colours are deliberately NOT asserted anywhere: the palette
    # is presentation, not a compatibility contract.
    if not check_human_framing(output):
        return 1
    if not check_start_pose_focus(godot):
        return 1

    # M6.2A: the textured consumer boundary. The expected side is CORE's
    # prepared world; the actual side is the ArrayMesh. Architecture only --
    # the provisional UV constants are settled by the human visual gate.
    textured = run_godot(
        godot, ["res://scenes/textured_boundary_test.tscn", "--quit-after", "10", "--"])
    output = textured.stdout + textured.stderr
    if textured.returncode != 0 or "M6.2A textured consumer boundary: OK" not in output:
        print(f"scene-check FAILED: textured consumer boundary (exit "
              f"{textured.returncode})", file=sys.stderr)
        print(output[-3000:], file=sys.stderr)
        return 1
    guarded_tex = run_godot(
        godot, ["res://scenes/textured_boundary_test.tscn", "--quit-after", "3", "--",
                "--grp", "/nonexistent.grp"])
    if guarded_tex.returncode != 2 or "refuses real-content arguments" \
            not in (guarded_tex.stdout + guarded_tex.stderr):
        print(f"scene-check FAILED: the textured CI test did not refuse --grp "
              f"(exit {guarded_tex.returncode}, expected 2)", file=sys.stderr)
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

    # M5 slice 3: the production source route. The scene serializes
    # committed fixtures into a scratch directory with the canonical MAP
    # writer, re-loads them through the production source owner
    # (DirectoryMount -> VFS -> parser -> derivation -> view seam), and
    # compares the ACTUAL boundary arrays against the direct synthetic
    # route. Its standing corruption case proves the mounted MAP bytes
    # are genuinely consumed. Real content differs only by the mount kind
    # (GrpMount, proven since M2) and stays with the human viewer.
    source_scene = run_godot(
        godot, ["res://scenes/structural_source_test.tscn", "--quit-after", "3", "--"])
    output = source_scene.stdout + source_scene.stderr
    if source_scene.returncode != 0 or "M5 production source route: OK" not in output:
        print(f"scene-check FAILED: production source route (exit "
              f"{source_scene.returncode})", file=sys.stderr)
        print(output[-3000:], file=sys.stderr)
        return 1

    # ...and it must refuse real-content arguments like every CI scene.
    source_guarded = run_godot(
        godot, ["res://scenes/structural_source_test.tscn", "--quit-after", "3", "--",
                "--grp", "/nonexistent.grp"])
    if source_guarded.returncode != 2 or "refuses real-content arguments" \
            not in (source_guarded.stdout + source_guarded.stderr):
        print(f"scene-check FAILED: the CI source-route test did not refuse --grp "
              f"(exit {source_guarded.returncode}, expected 2)", file=sys.stderr)
        return 1

    # The human viewer is the one place real content may enter, so its
    # source-mode argument contract is now load-bearing: mode conflicts,
    # missing --map, and dangling values are usage errors (exit 2), and a
    # missing source fails cleanly (exit 1) instead of crashing.
    for args, label in [
        (["--grp", "/nonexistent.grp"], "source without --map"),
        (["--grp", "/nonexistent.grp", "--dir", "/nonexistent/dir"], "two source modes"),
        (["--dir", "/nonexistent/dir", "--map"], "dangling --map value"),
        (["--bogus"], "unknown option"),
    ]:
        r = run_godot(
            godot, ["res://scenes/structural_view_human.tscn", "--quit-after", "3", "--"]
            + args, timeout=120)
        if r.returncode != 2:
            print(f"scene-check FAILED: human viewer usage contract ({label}): exit "
                  f"{r.returncode}, expected 2", file=sys.stderr)
            print((r.stdout + r.stderr)[-1500:], file=sys.stderr)
            return 1
    graceful = run_godot(
        godot, ["res://scenes/structural_view_human.tscn", "--quit-after", "3", "--",
                "--grp", "/nonexistent.grp", "--map", "X.MAP"], timeout=120)
    graceful_output = graceful.stdout + graceful.stderr
    if graceful.returncode != 1 or "source failed to load/present" not in graceful_output:
        print(f"scene-check FAILED: human viewer graceful source failure (exit "
              f"{graceful.returncode}, expected 1)", file=sys.stderr)
        print(graceful_output[-1500:], file=sys.stderr)
        return 1

    print("scene-check: sample scene ran with the extension live")
    print("scene-check: atlas consumer boundary + preview verified")
    print("scene-check: human atlas harness runs and the CI test refuses real content")
    print("scene-check: structural consumer boundary verified; human viewer runs")
    print("scene-check: production source route verified; human viewer source modes hold")
    print("scene-check: human viewer framing is finite and aimed; ceiling toggle rebuilds nothing")
    print("scene-check: real-content overview aims at the start pose; P lands on it exactly")
    print("scene-check: textured boundary uploads the prepared UVs verbatim from an R8 atlas")
    return 0


if __name__ == "__main__":
    sys.exit(main())

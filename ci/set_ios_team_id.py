#!/usr/bin/env python3
"""Set the Apple team ID in godot/export_presets.cfg without committing it.

The repository is public and the committed preset keeps signing data empty.
This edits the working copy only; leave the change uncommitted.
"""

import pathlib
import sys

PRESET = pathlib.Path(__file__).resolve().parent.parent / "godot" / "export_presets.cfg"


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <TEAM_ID>", file=sys.stderr)
        return 2
    team_id = sys.argv[1].strip()
    text = PRESET.read_text()
    for key in ("application/app_store_team_id", "sandbox/apple_team_id"):
        for line in text.splitlines():
            if line.startswith(f"{key}="):
                text = text.replace(line, f'{key}="{team_id}"')
                break
        else:
            print(f"set_ios_team_id: key {key} not found", file=sys.stderr)
            return 1
    PRESET.write_text(text)
    print(f"set_ios_team_id: set {team_id} (keep this change uncommitted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

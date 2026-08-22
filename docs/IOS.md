# iOS

Binding as of M1 (2026-08-22). The M1 stop condition is resolved: GDExtension
packaging works for iOS with no engine fork.

## Package shape

- The extension ships as a **static archive** `godot/bin/libfauxbuild.ios.a`,
  self-contained: extension objects + `fauxbuild_core` objects + godot-cpp
  objects, merged by `ci/merge_static_libs.py`.
- `extension/fauxbuild.gdextension` lists `ios.debug`/`ios.release` pointing at
  `res://bin/libfauxbuild.ios.a`.

## Build (from repo root)

```sh
scons config=release extension platform=ios target=template_release
```

godot-cpp is built for `ios.arm64.template_release` automatically (submodule
must be initialized: `git submodule update --init --recursive`).

## Export (Xcode project generation)

```sh
mkdir -p build/export/ios
/Applications/Godot.app/Contents/MacOS/Godot --headless --path godot --export-release "iOS"
```

Requirements discovered at M1:

- Export templates 4.7.2 stable installed
  (`~/Library/Application Support/Godot/export_templates/4.7.2.stable/`,
  `macos.zip`/`ios.zip` must be extracted).
- App icons are mandatory: the full set lives in `godot/icons/` (placeholder
  generator: `godot/icons/generate_placeholders.py`).
- `application/min_ios_version` must be ≥ 14 (Metal renderer).
- A team ID is required (`application/app_store_team_id`). The committed preset
  intentionally ships it **empty** (public repo; keep signing identity data out
  of version control). Set it locally before exporting, e.g.:

  ```sh
  python3 ci/set_ios_team_id.py <TEAM_ID>   # edits export_presets.cfg; keep the change uncommitted
  ```
- Godot's integrated xcodebuild step targets app-store/distribution signing and
  fails on a development-only Mac; use the generated Xcode project directly for
  development builds (below).

## Signed development build on device (verified at M1)

```sh
xcodebuild -project build/export/ios/FauxBuild-ios.xcodeproj \
  -target FauxBuild-ios -configuration Debug -sdk iphoneos \
  CODE_SIGN_STYLE=Automatic DEVELOPMENT_TEAM=<TEAM> \
  CODE_SIGN_IDENTITY="Apple Development" build

xcrun devicectl device install app --device <UDID> \
  build/export/ios/build/Debug-iphoneos/FauxBuild-ios.app
xcrun devicectl device process launch --device <UDID> org.fauxbuild.sample
```

Verified 2026-08-22: build succeeded, installed and launched on iPhone 15 Pro
Max (iOS 26.5 SDK, Xcode 26.6).

## Known issues

- Headless editor `--import` with a cold `.godot` cache crashes (Godot-internal
  backtrace, MoltenVK/ZSTD frames; our library is not in the stack) whenever any
  GDExtension is present. Non-headless editor and warm-cache headless runs are
  clean. Watch for this on Linux CI runners; if reproduced there, report
  upstream and use a warm cache workaround.

# iOS

Stub — becomes binding at M1 and M15.

Fixed rules from the plan:

- **M1 stop condition (risk §18.7):** if GDExtension packaging blocks iOS, solve it at M1 or
  switch to a statically compiled Godot module before any engine code depends on the
  boundary. Do not postpone this risk.
- M1 gate requires an iOS device or signed development build launching a blank scene
  containing the extension.
- Native package form: static archive or XCFramework as required by the chosen export path
  (plan §3.2).
- Core threading rule applies: Godot object creation/scene-tree changes on the main thread
  only; workers pass plain immutable buffers or move-owned results.
- M15 hardening: touch layout, controller-first UI, safe areas, pause/resume and
  interruption handling, save-on-background, thermal/performance profiling, memory budgets,
  shader/backend validation, packaging/license screens, crash diagnostics.

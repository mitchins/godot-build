# Declares FauxBuild's top-level SCons variables to godot-cpp's Variables set so
# its "unknown variables" warning stays quiet. godot-cpp itself ignores them.
vars.Add(EnumVariable('config', 'FauxBuild build configuration (unused by godot-cpp)',
                      'dev', allowed_values=('dev', 'release', 'asan')))
vars.Add(PathVariable('godot', 'Godot editor binary (unused by godot-cpp)',
                      '/Applications/Godot.app/Contents/MacOS/Godot',
                      PathVariable.PathAccept))

#pragma once

namespace fauxbuild::tool {

int dump_map(int argc, char** argv);
int validate_map(int argc, char** argv);
int rewrite_map(int argc, char** argv);
int diff_map(int argc, char** argv);
int gen_map(int argc, char** argv);

// Structural derivation summary (M5 slice 1). Pure C++: no Godot, no atlas,
// no textures, no output files -- it reports what build_structural_world
// produced so the derivation is reproducible through shipped tooling instead
// of an ad-hoc harness.
int inspect_structural(int argc, char** argv);

} // namespace fauxbuild::tool

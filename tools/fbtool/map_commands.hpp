#pragma once

namespace fauxbuild::tool {

int dump_map(int argc, char** argv);
int validate_map(int argc, char** argv);
int rewrite_map(int argc, char** argv);
int diff_map(int argc, char** argv);
int gen_map(int argc, char** argv);

} // namespace fauxbuild::tool

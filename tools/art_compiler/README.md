# art_compiler

The original ART fixture compiler for M4 slice 3. Implementation lives in
core/src/tile_build.cpp (parsing/generation in the engine core, linked by all
tools per the no-duplicated-parser rule) and tools/fbtool/build_commands.cpp,
exposed as `fbtool build-art` per plan section 13. Source material:
fixtures/source/tiles/*.tileset.

import os

import SCons.Errors
import subprocess

vars = Variables()
vars.Add(EnumVariable('config', 'Build configuration', 'dev',
                      allowed_values=('dev', 'release', 'asan', 'fuzz')))
vars.Add(PathVariable('godot', 'Path to Godot editor binary (used from M1)',
                      '/Applications/Godot.app/Contents/MacOS/Godot',
                      PathVariable.PathAccept))

env = Environment(variables=vars, ENV=os.environ)
Help(vars.GenerateHelpText(env))

# Plan §3.1 pins clang on POSIX and MSVC on Windows; an externally supplied
# CXX always wins on POSIX. Warning/standard flags are per-toolchain.
is_msvc = env['PLATFORM'] == 'win32'
if is_msvc:
    env.Append(CXXFLAGS=['/std:c++20', '/permissive-', '/Zc:__cplusplus', '/W4', '/WX',
                         '/EHsc'],
               CPPDEFINES=['_CRT_SECURE_NO_WARNINGS'])
else:
    env['CXX'] = os.environ.get('CXX', 'clang++')
    env.Append(CXXFLAGS=['-std=c++20', '-Wall', '-Wextra', '-Werror'])

env.Append(CPPPATH=['#core/include'])

cfg = env['config']
if cfg == 'dev':
    if is_msvc:
        env.Append(CXXFLAGS=['/Od', '/Zi', '/FS'], CPPDEFINES=['FAUXBUILD_CONFIG_DEV'])
    else:
        env.Append(CXXFLAGS=['-O0', '-g3'], CPPDEFINES=['FAUXBUILD_CONFIG_DEV'])
elif cfg == 'release':
    # NDEBUG makes plain assert() development-only. Plan §3.3 content-safety
    # assertions are delivered by FB_CHECK, which is always live independent of
    # NDEBUG (D0006). That keeps "deliberate content-safety invariant" distinct
    # from "debug aid" in shipping builds.
    if is_msvc:
        env.Append(CXXFLAGS=['/O2'], CPPDEFINES=['FAUXBUILD_CONFIG_RELEASE', 'NDEBUG'])
    else:
        env.Append(CXXFLAGS=['-O2'], CPPDEFINES=['FAUXBUILD_CONFIG_RELEASE', 'NDEBUG'])
elif cfg == 'asan':
    env.Append(
        CXXFLAGS=['-O1', '-g3', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'],
        LINKFLAGS=['-fsanitize=address,undefined'],
        CPPDEFINES=['FAUXBUILD_CONFIG_ASAN'],
    )
elif cfg == 'fuzz':
    # Sanitizer-driven fuzz builds (plan §3.3, D0010). Apple clang ships no
    # libFuzzer runtime, so tests/fuzz/fuzz_main.cpp provides a portable
    # deterministic driver with libFuzzer-compatible flags.
    # MSVC cannot build this configuration (no -fsanitize=undefined, no POSIX
    # env-prefix command form). Fail fast rather than emitting flags the
    # toolchain silently mishandles; see docs/DEPENDENCIES.md.
    if is_msvc:
        raise SCons.Errors.UserError(
            "config=fuzz is not supported on MSVC/Windows; run it on Linux or macOS "
            "(documented in docs/DEPENDENCIES.md)")
    env.Append(
        CXXFLAGS=['-O1', '-g3', '-fsanitize=address,undefined',
                  '-fno-omit-frame-pointer'],
        LINKFLAGS=['-fsanitize=address,undefined'],
        CPPDEFINES=['FAUXBUILD_CONFIG_FUZZ'],
    )

# ---------------------------------------------------------------------------
# Extension targets (M1+). godot-cpp consumes `platform`, `target`, and `arch`
# from the command line; supply sane defaults so `scons extension` works.
# ---------------------------------------------------------------------------
_host_platform = {'darwin': 'macos', 'win32': 'windows'}.get(env['PLATFORM'], 'linux')
platform = ARGUMENTS.get('platform', _host_platform)
gd_target = ARGUMENTS.get('target', 'template_debug')
ARGUMENTS.setdefault('platform', platform)
ARGUMENTS.setdefault('target', gd_target)
if platform in ('macos', 'ios'):
    # godot-cpp defaults to universal binaries on Apple platforms; pin arm64.
    ARGUMENTS.setdefault('arch', 'arm64')

if platform == 'ios':
    sdk_root = subprocess.run(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'],
                              capture_output=True, text=True, check=True).stdout.strip()
    ios_flags = ['-arch', 'arm64', '-isysroot', sdk_root, '-miphoneos-version-min=13.0']
    env.Append(CCFLAGS=ios_flags, LINKFLAGS=ios_flags)

bdir = f'#build/{cfg}'

VariantDir(f'{bdir}/core', '#core', duplicate=0)
core_sources = [
    f'{bdir}/core/src/version.cpp',
    f'{bdir}/core/src/check.cpp',
    f'{bdir}/core/src/result.cpp',
    f'{bdir}/core/src/byte_reader.cpp',
    f'{bdir}/core/src/file_io.cpp',
    f'{bdir}/core/src/grp.cpp',
    f'{bdir}/core/src/grp_synth.cpp',
    f'{bdir}/core/src/vfs.cpp',
    f'{bdir}/core/src/map_io.cpp',
    f'{bdir}/core/src/map_validate.cpp',
    f'{bdir}/core/src/map_diff.cpp',
    f'{bdir}/core/src/map_synth.cpp',
    f'{bdir}/core/src/palette.cpp',
    f'{bdir}/core/src/art.cpp',
    f'{bdir}/core/src/tile_manifest.cpp',
    f'{bdir}/core/src/tile_build.cpp',
    f'{bdir}/core/src/asset_set.cpp',
    f'{bdir}/core/src/atlas.cpp',
]
core_objects = env.Object(core_sources)
core = env.StaticLibrary(f'{bdir}/libfauxbuild_core', core_objects)

if cfg == 'fuzz':
    VariantDir(f'{bdir}/tests', '#tests', duplicate=0)
    fuzz_targets = []
    fuzz_runs = []
    for name, target, corpus_dirs in [
        ('grp', 'fauxbuild_fuzz_grp',
         ['tests/fuzz/corpus/grp', 'tests/fuzz/regression/grp']),
        ('map', 'fauxbuild_fuzz_map',
         ['tests/fuzz/corpus/map', 'tests/fuzz/regression/map']),
        ('palette', 'fauxbuild_fuzz_palette',
         ['tests/fuzz/corpus/palette', 'tests/fuzz/regression/palette']),
        ('art', 'fauxbuild_fuzz_art',
         ['tests/fuzz/corpus/art', 'tests/fuzz/regression/art']),
    ]:
        program = env.Program(f'{bdir}/{target}',
                              [f'{bdir}/tests/fuzz/{name}_fuzz.cpp',
                               f'{bdir}/tests/fuzz/fuzz_main.cpp'], LIBS=[core])
        fuzz_targets.append(program)
        # D0010: bounded corpus run over committed seeds + regressions.
        run = env.Command(
            f'{bdir}/fuzz_{name}.stamp', [program],
            ['UBSAN_OPTIONS=halt_on_error=1 ${SOURCE.abspath} -runs=20000 -max_len=65536 '
             + ' '.join(corpus_dirs), Touch('$TARGET')])
        env.AlwaysBuild(run)
        fuzz_runs.append(run)
        # The gate must be able to fail: assert the driver refuses to report
        # success on a corpus it could not load, and that the corpus loads.
        gate = env.Command(
            f'{bdir}/fuzz_gate_{name}.stamp', [program],
            ['python3 ci/check_fuzz_gate.py ${SOURCE.abspath}', Touch('$TARGET')])
        env.AlwaysBuild(gate)
        fuzz_runs.append(gate)
        # The loader and the manifest gate must agree on exactly which files
        # are seeds, or a file can influence fuzzing while evading the
        # integrity check. Executable evidence, not just implementation.
        filter_gate = env.Command(
            f'{bdir}/corpus_filter_{name}.stamp', [program],
            ['python3 ci/check_corpus_filter.py ${SOURCE.abspath}', Touch('$TARGET')])
        env.AlwaysBuild(filter_gate)
        fuzz_runs.append(filter_gate)
    Alias('all', fuzz_targets)
    Alias('fuzz', fuzz_runs)
    Default('all')

# Host tools/tests build whenever we are compiling for the host platform and
# not in the fuzz config (libFuzzer owns main). Cross-compiles (e.g.
# platform=ios) and the fuzz config skip this block. Note: guarding on
# `platform == 'macos'` instead would silently reduce the Linux CI `check`
# target to the layering script alone — a green gate that runs no tests.
host_build = platform == _host_platform and cfg != 'fuzz'

if host_build:
    VariantDir(f'{bdir}/tools', '#tools', duplicate=0)
    VariantDir(f'{bdir}/tests', '#tests', duplicate=0)

    fbtool_env = env.Clone()
    fbtool_env.Append(CPPPATH=['#'])
    fbtool = fbtool_env.Program(
        f'{bdir}/fbtool',
        [f'{bdir}/tools/fbtool/main.cpp', f'{bdir}/tools/fbtool/map_commands.cpp',
         f'{bdir}/tools/fbtool/palette_commands.cpp', f'{bdir}/tools/fbtool/art_commands.cpp', f'{bdir}/tools/fbtool/build_commands.cpp',
         f'{bdir}/tools/fbtool/atlas_commands.cpp'],
        LIBS=[core])

    tests_env = env.Clone()
    tests_env.Append(CPPPATH=['#third_party'])
    if is_msvc:
        # doctest's internal code is not /W4-clean; keep tests strict on POSIX.
        tests_env['CXXFLAGS'] = [f for f in tests_env['CXXFLAGS'] if f not in ('/W4', '/WX')]
        tests_env['CXXFLAGS'].append('/W3')
    tests = tests_env.Program(
        f'{bdir}/fauxbuild_tests',
        [
            f'{bdir}/tests/unit/main.cpp',
            f'{bdir}/tests/unit/version.test.cpp',
            f'{bdir}/tests/unit/check.test.cpp',
            f'{bdir}/tests/unit/map_reader.test.cpp',
            f'{bdir}/tests/unit/map_validate.test.cpp',
            f'{bdir}/tests/unit/map_synth.test.cpp',
            f'{bdir}/tests/unit/palette.test.cpp',
            f'{bdir}/tests/unit/art.test.cpp',
            f'{bdir}/tests/unit/tile_build.test.cpp',
            f'{bdir}/tests/unit/atlas.test.cpp',
            f'{bdir}/tests/unit/byte_reader.test.cpp',
        f'{bdir}/tests/unit/file_io.test.cpp',
            f'{bdir}/tests/unit/grp.test.cpp',
            f'{bdir}/tests/unit/grp_synth.test.cpp',
            f'{bdir}/tests/unit/vfs.test.cpp',
            f'{bdir}/tests/unit/corpus_regression.test.cpp',
        ],
        LIBS=[core],
    )

    # '${SOURCE.abspath}': a bare '$SOURCE' string action is dropped by SCons when the
    # action list also contains Action objects; the abspath form executes reliably.
    # halt_on_error: UBSan otherwise prints a diagnostic and keeps going, so a
    # test that provokes UB still reports SUCCESS and `check` stays green. Any
    # undefined behaviour reached by the suite must fail the build.
    test_cmd = '${SOURCE.abspath}'
    if cfg == 'asan' and not is_msvc:
        test_cmd = 'UBSAN_OPTIONS=halt_on_error=1 ' + test_cmd
    run_tests = tests_env.Command(
        f'{bdir}/tests.stamp', [tests], [test_cmd, Touch('$TARGET')])
    # Tests read runtime inputs SCons cannot track: the committed fuzz corpus
    # is loaded through __FILE__ paths (corpus_regression.test.cpp). Without
    # AlwaysBuild a corrupted corpus leaves a stale green stamp — the failure
    # shape found in M3 review. Tests re-run on every `check`.
    tests_env.AlwaysBuild(run_tests)
    smoke_fbtool = env.Command(
        f'{bdir}/fbtool.stamp', [fbtool], ['${SOURCE.abspath} --version', Touch('$TARGET')])
    # Command contracts (exit codes, stdout) are not observable from the unit
    # suite, so dump-grp/gen-grp get a process-level gate of their own.
    fbtool_contract = env.Command(
        f'{bdir}/fbtool_contract.stamp', [fbtool],
        ['python3 ci/check_fbtool.py ${SOURCE.abspath}', Touch('$TARGET')])
    env.AlwaysBuild(fbtool_contract)

    Alias('all', [core, fbtool, tests])
else:
    Alias('all', [core])

layering = env.Command(
    f'{bdir}/layering.stamp', [], ['python3 ci/check_layering.py', Touch('$TARGET')])
# Script-driven guards have no input SCons can track; without AlwaysBuild the stamp
# would make them run once per build/<cfg> lifetime and report green forever after.
env.AlwaysBuild(layering)

# Corpus integrity: the fuzz corpus is read at runtime via __FILE__ paths;
# a corrupted or deleted file must fail `check` (M3 review item 3).
corpus_check = env.Command(
    f'{bdir}/corpus.stamp', [], ['python3 ci/check_corpus.py', Touch('$TARGET')])
env.AlwaysBuild(corpus_check)

if host_build:
    Alias('check', [run_tests, smoke_fbtool, fbtool_contract, layering, corpus_check])
else:
    Alias('check', [layering, corpus_check])

format_check = env.Command(f'{bdir}/format.stamp', [],
                           ['python3 ci/check_format.py', Touch('$TARGET')])
env.AlwaysBuild(format_check)
Alias('format-check', format_check)

# Scene gate (M1): headless sample-scene run proving the extension is live.
# Requires a built+installed extension (`scons config=dev extension`) and a
# Godot editor binary (default: the `godot` variable's path).
godot_bin = env['godot']
scene_deps = []

# ---------------------------------------------------------------------------
# GDExtension (plan §3.2, §11). Requires the godot-cpp submodule (D0007).
#   scons config=dev extension                          desktop (editor/debug)
#   scons config=dev extension target=template_release  desktop (release export)
#   scons config=release extension platform=ios target=template_release  iOS
# ---------------------------------------------------------------------------
godot_cpp_sconstruct = 'third_party/godot-cpp/SConstruct'
if os.path.exists(godot_cpp_sconstruct):
    # godot-cpp creates its own clean environment (we export no `env`), builds
    # the bindings with the CLI platform/target/arch, and returns an env whose
    # CPPPATH/LIBPATH/LIBS already reference the built bindings library.
    # godot-cpp builds its own Variables set from ARGUMENTS and warns about
    # every key it does not recognise -- including ours. A `customs` file
    # cannot fix that: SCons Variables files are plain assignment files, they
    # cannot *declare* options, and the path resolves relative to godot-cpp's
    # own directory anyway (the previous shim was silently ignored on both
    # counts). Hide our keys for the duration of the call instead; godot-cpp
    # ignores them regardless, and ARGUMENTS is restored immediately after.
    fauxbuild_only = {name: ARGUMENTS.pop(name)
                      for name in ('config', 'godot')
                      if name in ARGUMENTS}
    try:
        gc_env = SConscript(godot_cpp_sconstruct, exports={'api_version': '4.7'})
    finally:
        ARGUMENTS.update(fauxbuild_only)

    ext_env = gc_env.Clone()
    ext_env.Append(CPPPATH=['#extension/include', '#core/include'])
    VariantDir(f'{bdir}/extension', '#extension', duplicate=0)
    ext_sources = [
        f'{bdir}/extension/src/fauxbuild_runtime.cpp',
        f'{bdir}/extension/src/fauxbuild_view.cpp',
        f'{bdir}/extension/src/faux_asset_set.cpp',
        f'{bdir}/extension/src/faux_atlas_preview.cpp',
        f'{bdir}/extension/src/register_extension.cpp',
    ]

    if platform == 'ios':
        # The extension's static archive must be self-contained (extension objects
        # + core objects + godot-cpp objects); Godot's iOS export links it into
        # the app and nothing else resolves godot-cpp symbols for us.
        ext_objects = ext_env.Object(ext_sources)
        gc_lib = ext_env.File(ext_env['LIBS'][-1])
        ext_partial = ext_env.StaticLibrary(f'{bdir}/extension/libfauxbuild.ios.partial.a',
                                            ext_objects + core_objects)
        ext = env.Command(f'{bdir}/extension/libfauxbuild.ios.a',
                          [ext_partial, gc_lib],
                          ['python3 ci/merge_static_libs.py $TARGET $SOURCES'])
        installed = env.Command('#godot/bin/libfauxbuild.ios.a', [ext],
                                Copy('$TARGET', '$SOURCE'))
    elif platform == 'macos':
        ext_name = f'{bdir}/extension/libfauxbuild.macos.{gd_target}.arm64'
        ext = ext_env.SharedLibrary(ext_name, ext_sources,
                                    LIBS=[core] + list(ext_env['LIBS']))
        installed = env.Command(f'#godot/bin/libfauxbuild.macos.{gd_target}.arm64.dylib',
                                [ext], Copy('$TARGET', '$SOURCE'))
    else:
        # Desktop extension targets are wired for macOS/iOS at M1/M2; Linux/
        # Windows extension wiring lands with the plan §14.4 CI matrix.
        installed = None

    installed_manifest = env.Command('#godot/bin/fauxbuild.gdextension',
                                     '#extension/fauxbuild.gdextension',
                                     Copy('$TARGET', '$SOURCE'))
    if installed is not None:
        Alias('extension', [installed, installed_manifest])
    else:
        Alias('extension', [installed_manifest])
    if installed is not None and platform == 'macos':
        scene_deps.append(installed)
        scene_deps.append(installed_manifest)

scene_check = env.Command(f'{bdir}/scene.stamp', scene_deps,
                          [f'python3 ci/check_scene.py "{godot_bin}"',
                           Touch('$TARGET')])
env.AlwaysBuild(scene_check)
Alias('scene-check', scene_check)

Default('all')

import os
import subprocess

vars = Variables()
vars.Add(EnumVariable('config', 'Build configuration', 'dev',
                      allowed_values=('dev', 'release', 'asan')))
vars.Add(PathVariable('godot', 'Path to Godot editor binary (used from M1)',
                      '/Applications/Godot.app/Contents/MacOS/Godot',
                      PathVariable.PathAccept))

env = Environment(variables=vars, ENV=os.environ)
Help(vars.GenerateHelpText(env))

# Plan §3.1 pins clang on POSIX; MSVC on Windows via SCons default detection.
# An externally supplied CXX always wins.
if env['PLATFORM'] != 'win32':
    env['CXX'] = os.environ.get('CXX', 'clang++')

env.Append(
    CXXFLAGS=['-std=c++20', '-Wall', '-Wextra', '-Werror'],
    CPPPATH=['#core/include'],
)

cfg = env['config']
if cfg == 'dev':
    env.Append(CXXFLAGS=['-O0', '-g3'], CPPDEFINES=['FAUXBUILD_CONFIG_DEV'])
elif cfg == 'release':
    # NDEBUG makes plain assert() development-only. Plan §3.3 content-safety
    # assertions are delivered by FB_CHECK, which is always live independent of
    # NDEBUG (D0006). That keeps "deliberate content-safety invariant" distinct
    # from "debug aid" in shipping builds.
    env.Append(CXXFLAGS=['-O2'], CPPDEFINES=['FAUXBUILD_CONFIG_RELEASE', 'NDEBUG'])
elif cfg == 'asan':
    env.Append(
        CXXFLAGS=['-O1', '-g3', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'],
        LINKFLAGS=['-fsanitize=address,undefined'],
        CPPDEFINES=['FAUXBUILD_CONFIG_ASAN'],
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
core_sources = [f'{bdir}/core/src/version.cpp', f'{bdir}/core/src/check.cpp']
core_objects = env.Object(core_sources)
core = env.StaticLibrary(f'{bdir}/libfauxbuild_core', core_objects)

if platform == 'macos':
    VariantDir(f'{bdir}/tools', '#tools', duplicate=0)
    VariantDir(f'{bdir}/tests', '#tests', duplicate=0)

    fbtool = env.Program(f'{bdir}/fbtool', [f'{bdir}/tools/fbtool/main.cpp'], LIBS=[core])

    tests_env = env.Clone()
    tests_env.Append(CPPPATH=['#third_party'])
    tests = tests_env.Program(
        f'{bdir}/fauxbuild_tests',
        [
            f'{bdir}/tests/unit/main.cpp',
            f'{bdir}/tests/unit/version.test.cpp',
            f'{bdir}/tests/unit/check.test.cpp',
        ],
        LIBS=[core],
    )

    # '${SOURCE.abspath}': a bare '$SOURCE' string action is dropped by SCons when the
    # action list also contains Action objects; the abspath form executes reliably.
    run_tests = tests_env.Command(
        f'{bdir}/tests.stamp', [tests], ['${SOURCE.abspath}', Touch('$TARGET')])
    smoke_fbtool = env.Command(
        f'{bdir}/fbtool.stamp', [fbtool], ['${SOURCE.abspath} --version', Touch('$TARGET')])

Alias('all', [core] + ([fbtool, tests] if platform == 'macos' else []))

layering = env.Command(
    f'{bdir}/layering.stamp', [], ['python3 ci/check_layering.py', Touch('$TARGET')])
# Script-driven guards have no input SCons can track; without AlwaysBuild the stamp
# would make them run once per build/<cfg> lifetime and report green forever after.
env.AlwaysBuild(layering)

if platform == 'macos':
    Alias('check', [run_tests, smoke_fbtool, layering])
else:
    Alias('check', [layering])

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
    gc_env = SConscript(godot_cpp_sconstruct,
                        exports={'api_version': '4.7',
                                 'customs': ['tools/godot_cpp_vars.py']})

    ext_env = gc_env.Clone()
    ext_env.Append(CPPPATH=['#extension/include', '#core/include'])
    VariantDir(f'{bdir}/extension', '#extension', duplicate=0)
    ext_sources = [
        f'{bdir}/extension/src/fauxbuild_runtime.cpp',
        f'{bdir}/extension/src/fauxbuild_view.cpp',
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
    else:
        ext_name = f'{bdir}/extension/libfauxbuild.macos.{gd_target}.arm64'
        ext = ext_env.SharedLibrary(ext_name, ext_sources,
                                    LIBS=[core] + list(ext_env['LIBS']))
        installed = env.Command(f'#godot/bin/libfauxbuild.macos.{gd_target}.arm64.dylib',
                                [ext], Copy('$TARGET', '$SOURCE'))

    installed_manifest = env.Command('#godot/bin/fauxbuild.gdextension',
                                     '#extension/fauxbuild.gdextension',
                                     Copy('$TARGET', '$SOURCE'))
    Alias('extension', [installed, installed_manifest])
    if platform == 'macos':
        scene_deps.append(installed)
        scene_deps.append(installed_manifest)

scene_check = env.Command(f'{bdir}/scene.stamp', scene_deps,
                          [f'python3 ci/check_scene.py "{godot_bin}"',
                           Touch('$TARGET')])
env.AlwaysBuild(scene_check)
Alias('scene-check', scene_check)

Default('all')

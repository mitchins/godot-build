import os

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
    # No NDEBUG: plan §3.3 requires release to retain content-safety assertions.
    # Content-safety invariants use FB_CHECK (core/include/fauxbuild/check.hpp),
    # which is independent of NDEBUG in every configuration (D0006).
    env.Append(CXXFLAGS=['-O2'], CPPDEFINES=['FAUXBUILD_CONFIG_RELEASE'])
elif cfg == 'asan':
    env.Append(
        CXXFLAGS=['-O1', '-g3', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'],
        LINKFLAGS=['-fsanitize=address,undefined'],
        CPPDEFINES=['FAUXBUILD_CONFIG_ASAN'],
    )

bdir = f'#build/{cfg}'

VariantDir(f'{bdir}/core', '#core', duplicate=0)
VariantDir(f'{bdir}/tools', '#tools', duplicate=0)
VariantDir(f'{bdir}/tests', '#tests', duplicate=0)

core = env.StaticLibrary(
    f'{bdir}/libfauxbuild_core',
    [f'{bdir}/core/src/version.cpp', f'{bdir}/core/src/check.cpp'],
)

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
layering = env.Command(
    f'{bdir}/layering.stamp', [], ['python3 ci/check_layering.py', Touch('$TARGET')])
# Script-driven guards have no input SCons can track; without AlwaysBuild the stamp
# would make them run once per build/<cfg> lifetime and report green forever after.
env.AlwaysBuild(layering)

Alias('all', [core, fbtool, tests])
Alias('check', [run_tests, smoke_fbtool, layering])
format_check = env.Command(f'{bdir}/format.stamp', [],
                           ['python3 ci/check_format.py', Touch('$TARGET')])
env.AlwaysBuild(format_check)
Alias('format-check', format_check)
Default('all')

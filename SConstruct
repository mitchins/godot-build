import os

vars = Variables()
vars.Add(EnumVariable('config', 'Build configuration', 'dev',
                      allowed_values=('dev', 'release', 'asan')))
vars.Add(PathVariable('godot', 'Path to Godot editor binary (used from M1)',
                      '/Applications/Godot.app/Contents/MacOS/Godot'))

env = Environment(variables=vars, ENV=os.environ)
Help(vars.GenerateHelpText(env))

env['CXX'] = 'clang++'
env.Append(
    CXXFLAGS=['-std=c++20', '-Wall', '-Wextra', '-Werror'],
    CPPPATH=['#core/include'],
)

cfg = env['config']
if cfg == 'dev':
    env.Append(CXXFLAGS=['-O0', '-g3'], CPPDEFINES=['FAUXBUILD_CONFIG_DEV'])
elif cfg == 'release':
    env.Append(CXXFLAGS=['-O2'], CPPDEFINES=['FAUXBUILD_CONFIG_RELEASE', 'NDEBUG'])
elif cfg == 'asan':
    env.Append(
        CXXFLAGS=['-O1', '-g3', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'],
        LINKFLAGS=['-fsanitize=address,undefined'],
        CPPDEFINES=['FAUXBUILD_CONFIG_ASAN'],
    )

bdir = f'#build/{cfg}'

core = env.StaticLibrary(f'{bdir}/fauxbuild_core', ['core/src/version.cpp'])

fbtool = env.Program(f'{bdir}/fbtool', ['tools/fbtool/main.cpp'], LIBS=[core])

tests_env = env.Clone()
tests_env.Append(CPPPATH=['#third_party'])
tests = tests_env.Program(
    f'{bdir}/fauxbuild_tests',
    ['tests/unit/main.cpp', 'tests/unit/version.test.cpp'],
    LIBS=[core],
)

run_tests = tests_env.Command(f'{bdir}/tests.stamp', [tests], ['$SOURCE', Touch('$TARGET')])

Alias('all', [core, fbtool, tests])
Alias('check', run_tests)
Default('all')

project('DrMd', 'c', version: '1.0.0', default_options:['c_std=gnu17'])
COMPAT_VERSION='1.0.0' # for macos dylib

# untested
if host_machine.system() == 'windows'
  ignore_bogus_deprecations = ['-D_CRT_NONSTDC_NO_WARNINGS', '-D_CRT_SECURE_NO_WARNINGS']
else
  ignore_bogus_deprecations = []
endif

if host_machine.system() == 'darwin'
  arches = ['-arch', 'x86_64', '-arch', 'arm64']
else
  arches = []
endif

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

executable(
  'drmd',
  'drmd_cli.c',
  install:true,
  c_args:ignore_bogus_deprecations+arches,
  dependencies:[m_dep]
)

test_drmd = executable(
  'test-drmd',
  'TestDrMd.c',
  c_args: ignore_bogus_deprecations+arches,
  dependencies:[m_dep]
)
test('test-drmd', test_drmd)

project(
  'ludwig',
  'c',
  'cpp',
  version: '0.0.1',
  meson_version: '>=0.63',
  license: 'Apache-2.0',
  default_options: ['c_std=gnu11', 'cpp_std=gnu++23', 'default_library=static'],
)

cc = meson.get_compiler('cpp')
assert(cc.get_id() == 'clang', 'This project only supports the Clang compiler.')

cmake = import('cmake')
cmake_opts = cmake.subproject_options()
cmake_opts.add_cmake_defines(
  {
    'BUILD_SHARED_LIBS': 'OFF',
    'BUILD_STATIC_LIBS': 'ON',
    'BUILD_TESTS': 'OFF',
    'SPM_ENABLE_SHARED': 'OFF',
    'CPPTRACE_USE_EXTERNAL_ZSTD': 'ON',
  },
)
cmake_opts.set_install(false)

add_global_arguments(
  '-fcolor-diagnostics',
  '-ferror-limit=0',
  '-Wno-gnu-anonymous-struct',
  '-Wno-nested-anon-types',
  '-DLIBUS_USE_OPENSSL=1',
  '-DWUFFS_CONFIG__MODULES=1',
  '-DWUFFS_CONFIG__MODULE__ADLER32=1',
  '-DWUFFS_CONFIG__MODULE__AUX__BASE=1',
  '-DWUFFS_CONFIG__MODULE__AUX__IMAGE=1',
  '-DWUFFS_CONFIG__MODULE__AUX__JSON=1',
  '-DWUFFS_CONFIG__MODULE__BASE=1',
  '-DWUFFS_CONFIG__MODULE__BMP=1',
  '-DWUFFS_CONFIG__MODULE__CRC32=1',
  '-DWUFFS_CONFIG__MODULE__DEFLATE=1',
  '-DWUFFS_CONFIG__MODULE__GIF=1',
  '-DWUFFS_CONFIG__MODULE__JPEG=1',
  '-DWUFFS_CONFIG__MODULE__JSON=1',
  '-DWUFFS_CONFIG__MODULE__PNG=1',
  '-DWUFFS_CONFIG__MODULE__QOI=1',
  '-DWUFFS_CONFIG__MODULE__TARGA=1',
  '-DWUFFS_CONFIG__MODULE__XXHASH64=1',
  '-DWUFFS_CONFIG__MODULE__WEBP=1',
  '-DWUFFS_CONFIG__MODULE__ZLIB=1',
  language: ['c', 'cpp'],
)

if get_option('buildtype').startswith('release')
  add_global_arguments(
    '-O2',
    language: ['c', 'cpp'],
  )
else
  add_global_arguments(
    '-g',
    '-DLUDWIG_DEBUG=1',
    language: ['c', 'cpp'],
  )
endif

inc_dir = include_directories(
  'src',
  'vendor/include',
  'vendor/include/uSockets',
)

zlib_dep = dependency('zlib', static: true)
libwebp_dep = dependency('libwebp', static: true)
libwebpmux_dep = dependency('libwebpmux', static: true)
libwebpdemux_dep = dependency('libwebpdemux', static: true)

asio_dep = dependency('asio', static: true)
catch2_dep = dependency('catch2-with-main')
cpptrace = cmake.subproject('cpptrace', options: cmake_opts)
cpptrace_dep = cpptrace.dependency('cpptrace-lib')
dwarf_dep = cpptrace.dependency('dwarf')
flatbuffers_dep = dependency('flatbuffers', static: true)
flatc = find_program('flatc')
fmt_dep = dependency('fmt', static: true)
libxml2_dep = dependency('libxml-2.0', static: true)
lucene_dep = dependency('liblucene++')
boringssl = cmake.subproject('boringssl', options: cmake_opts)
crypto_dep = boringssl.dependency('crypto')
ssl_dep = boringssl.dependency('ssl')
simdjson_dep = dependency('simdjson', static: true)
subproject(
  'spdlog',
  default_options: ['tests=disabled', 'std_format=disabled', 'external_fmt=enabled'],
)
spdlog_dep = dependency('spdlog', static: true)
thread_dep = dependency('threads')
zstd_dep = dependency('libzstd', static: true)

libs = [
  zlib_dep,
  libwebp_dep,
  libwebpmux_dep,
  libwebpdemux_dep,

  asio_dep,
  dwarf_dep,
  cpptrace_dep,
  flatbuffers_dep,
  fmt_dep,
  libxml2_dep,
  lucene_dep,
  crypto_dep,
  ssl_dep,
  simdjson_dep,
  spdlog_dep,
  thread_dep,
  zstd_dep,
]

binary_header = find_program('./extras/binary-header.sh')

subdir('vendor')
subdir('src')
subdir('test')

executable(
  'ludwig',
  'src/ludwig.c++',
  ludwig_sources,
  vendor_sources,
  fbs_gen,
  static_gen,
  dependencies: libs,
  include_directories: inc_dir,
)
project(
  'ludwig',
  'c',
  'cpp',
  version: '0.0.1',
  license: 'Apache-2.0',
  default_options: ['c_std=gnu11', 'cpp_std=gnu++23'],
)

cc = meson.get_compiler('cpp')
assert(cc.get_id() == 'clang', 'This project only supports the Clang compiler.')

cmake = import('cmake')

add_global_arguments(
  '-fcolor-diagnostics',
  '-ferror-limit=0',
  '-Wno-gnu-anonymous-struct',
  '-Wno-nested-anon-types',
  '-DLIBUS_USE_OPENSSL=1',
  '-DXXH_INLINE_ALL=1',
  language: ['c', 'cpp'],
)

inc_dir = include_directories(
  'src',
  'vendor/include',
  'vendor/include/uSockets',
  'subprojects/sentencepiece/src',
)

asio_dep = dependency('asio')
catch2_dep = dependency('catch2-with-main')
flatbuffers_dep = dependency('flatbuffers')
flatc = find_program('flatc')
fmt_dep = dependency('fmt')
giflib_dep = dependency('giflib')
libjpeg_dep = dependency('libjpeg')
libpng_dep = dependency('libpng')
libwebp_dep = dependency('libwebp')
libwebpdecoder_dep = dependency('libwebpdecoder')
libxml2_dep = dependency('libxml-2.0')
boringssl = cmake.subproject('boringssl')
crypto_dep = boringssl.dependency('crypto')
ssl_dep = boringssl.dependency('ssl')
sentencepiece = cmake.subproject('sentencepiece')
sentencepiece_dep = sentencepiece.dependency('sentencepiece')
simdjson_dep = dependency('simdjson')
subproject('spdlog', default_options: ['std_format=disabled', 'external_fmt=enabled'])
spdlog_dep = dependency('spdlog')
thread_dep = dependency('threads')
zlib_dep = dependency('zlib')
zstd_dep = dependency('libzstd')

libs = [
  asio_dep,
  flatbuffers_dep,
  fmt_dep,
  giflib_dep,
  libjpeg_dep,
  libpng_dep,
  libwebp_dep,
  libwebpdecoder_dep,
  libxml2_dep,
  crypto_dep,
  ssl_dep,
  sentencepiece_dep,
  simdjson_dep,
  spdlog_dep,
  thread_dep,
  zlib_dep,
  zstd_dep,
]

binary_header = find_program('./extras/binary-header.sh')

subdir('vendor')
subdir('src')

ludwig = executable(
  'ludwig',
  'src/ludwig.c++',
  ludwig_sources,
  vendor_sources,
  fbs_gen,
  static_gen,
  dependencies: libs,
  include_directories: inc_dir,
)
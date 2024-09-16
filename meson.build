project(
  'ludwig',
  'c',
  'cpp',
  version: '0.0.1',
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
  },
)
cmake_opts.set_install(false)

add_global_arguments(
  '-fcolor-diagnostics',
  '-ferror-limit=0',
  '-Wno-gnu-anonymous-struct',
  '-Wno-nested-anon-types',
  '-DLIBUS_USE_OPENSSL=1',
  '-DXXH_INLINE_ALL=1',
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
  'subprojects/sentencepiece/src',
)

# Include libvips dependencies statically,
# and add them as top-level dependencies,
# to avoid libvips pulling in shared libs.
libpng_dep = dependency('libpng', static: true)
libjpeg_dep = dependency('libjpeg', static: true)
libwebp_dep = dependency('libwebp', static: true)
#libhwy_dep = dependency('libhwy', static: true)
#glib_dep = dependency('glib-2.0', static: true)
gio_dep = dependency('gio-2.0', static: true)
gobject_dep = dependency('gobject-2.0', static: true)
gmodule_dep = dependency('gmodule-2.0', static: true)
expat_dep = dependency('expat', static: true)
#pcre_dep = dependency('libpcre', static: true)
zlib_dep = dependency('zlib', static: true)

libvips_options = [
  'default_library=static',
  'debug=false',
  'deprecated=false',
  'examples=false',
  'cplusplus=true',
  'doxygen=false',
  'gtk_doc=false',
  'modules=disabled',
  'introspection=disabled',
  'vapi=false',
  'cfitsio=disabled',
  'cgif=disabled',
  'exif=disabled',
  'fftw=disabled',
  'fontconfig=disabled',
  'archive=disabled',
  'heif=disabled', # TODO: enable HEIF and AVIF
  'imagequant=disabled',
  'jpeg=enabled',
  'jpeg-xl=disabled', # TODO: enable JXL
  'lcms=disabled',
  'magick=disabled',
  'matio=disabled',
  'nifti=disabled',
  'openexr=disabled',
  'openjpeg=disabled',
  'openslide=disabled',
  'highway=enabled',
  'orc=disabled',
  'pangocairo=disabled',
  'pdfium=disabled', # TODO: enable PDF thumbnails?
  'png=enabled',
  'poppler=disabled',
  'quantizr=disabled',
  'rsvg=disabled', # TODO: enable SVG thumbnails
  'spng=disabled',
  'tiff=disabled',
  'webp=enabled',
  'zlib=enabled',
]

asio_dep = dependency('asio', static: true)
catch2_dep = dependency('catch2-with-main')
flatbuffers_dep = dependency('flatbuffers', static: true)
flatc = find_program('flatc')
fmt_dep = dependency('fmt', static: true)
libvips_dep = subproject('libvips', default_options: libvips_options).get_variable('libvips_cpp_dep')
libxml2_dep = dependency('libxml-2.0', static: true)
boringssl = cmake.subproject('boringssl', options: cmake_opts)
crypto_dep = boringssl.dependency('crypto')
ssl_dep = boringssl.dependency('ssl')
sentencepiece = cmake.subproject('sentencepiece', options: cmake_opts)
sentencepiece_dep = sentencepiece.dependency('sentencepiece-static')
simdjson_dep = dependency('simdjson', static: true)
subproject('spdlog', default_options: ['std_format=disabled', 'external_fmt=enabled'])
spdlog_dep = dependency('spdlog', static: true)
thread_dep = dependency('threads')
zstd_dep = dependency('libzstd', static: true)

libs = [
  libpng_dep,
  libjpeg_dep,
  libwebp_dep,
  #libhwy_dep,
  #glib_dep,
  gio_dep,
  gobject_dep,
  gmodule_dep,
  expat_dep,
  #pcre_dep,
  zlib_dep,

  asio_dep,
  flatbuffers_dep,
  fmt_dep,
  libvips_dep,
  libxml2_dep,
  crypto_dep,
  ssl_dep,
  sentencepiece_dep,
  simdjson_dep,
  spdlog_dep,
  thread_dep,
  zstd_dep,
]

binary_header = find_program('./extras/binary-header.sh')

subdir('vendor')
subdir('src')
subdir('test')

ludwig_exe = executable(
  'ludwig',
  'src/ludwig.c++',
  ludwig_sources,
  vendor_sources,
  fbs_gen,
  static_gen,
  dependencies: libs,
  include_directories: inc_dir,
)
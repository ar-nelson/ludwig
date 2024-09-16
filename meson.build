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
zlib_dep = dependency('zlib', static: true)
libpng_dep = dependency('libpng', static: true)
libjpeg_dep = dependency('libjpeg', static: true)
libwebp_dep = dependency('libwebp', static: true)
libwebpmux_dep = dependency('libwebpmux', static: true)
libwebpdemux_dep = dependency('libwebpdemux', static: true)
libsharpyuv_dep = dependency('libsharpyuv', static: true)
gio_dep = dependency('gio-2.0', static: true)
gobject_dep = dependency('gobject-2.0', static: true)
expat_dep = dependency('expat', static: true)

libvips_options = [
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
  'imagequant=disabled',
  'jpeg=enabled',
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
  'pdfium=disabled',
  'png=enabled',
  'quantizr=disabled',
  'spng=disabled',
  'tiff=disabled',
  'webp=enabled',
  'zlib=enabled',
  'ppm=false',
  'analyze=false',
  'radiance=false',
]

if get_option('thumbnail_avif')
  libvips_options += 'heif=enabled'
  add_global_arguments('-DLUDWIG_THUMBNAIL_AVIF=1', language: 'cpp')
else
  libvips_options += 'heif=disabled'
endif

if get_option('thumbnail_jxl')
  libvips_options += 'jpeg-xl=enabled'
  add_global_arguments('-DLUDWIG_THUMBNAIL_JXL=1', language: 'cpp')
else
  libvips_options += 'jpeg-xl=disabled'
endif

if get_option('thumbnail_svg')
  libvips_options += 'rsvg=enabled'
  add_global_arguments('-DLUDWIG_THUMBNAIL_SVG=1', language: 'cpp')
else
  libvips_options += 'rsvg=disabled'
endif

if get_option('thumbnail_pdf')
  libvips_options += 'poppler=enabled'
  add_global_arguments('-DLUDWIG_THUMBNAIL_PDF=1', language: 'cpp')
else
  libvips_options += 'poppler=disabled'
endif

asio_dep = dependency('asio', static: true)
catch2_dep = dependency('catch2-with-main')
flatbuffers_dep = dependency('flatbuffers', static: true)
flatc = find_program('flatc')
fmt_dep = dependency('fmt', static: true)
libvips = subproject('libvips', default_options: libvips_options)
libvips_dep = libvips.get_variable('libvips_cpp_dep')
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
  zlib_dep,
  libpng_dep,
  libjpeg_dep,
  libwebp_dep,
  libwebpmux_dep,
  libwebpdemux_dep,
  libsharpyuv_dep,
  gio_dep,
  gobject_dep,
  expat_dep,

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
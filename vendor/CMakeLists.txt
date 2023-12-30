add_library(vendor-misc STATIC
  misc/src/mdb.c
  misc/src/midl.c
  misc/src/csprng.cpp
  misc/src/mmd.c
  misc/src/rate_limiter.cpp
)
target_include_directories(vendor-misc PUBLIC misc/include/)
target_compile_definitions(vendor-misc INTERFACE XXH_INLINE_ALL)

add_subdirectory(zlib zlib)
set(ZLIB_LIBRARY libz.a)
set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib)
set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})

add_subdirectory(boringssl boringssl)
include_directories(boringssl/include)

add_library(asio STATIC
  asio/asio.cpp
  asio/asio_ssl.cpp
)
target_compile_definitions(asio PUBLIC ASIO_SEPARATE_COMPILATION)
target_link_libraries(asio PUBLIC ssl crypto)
target_include_directories(asio PUBLIC asio/)

add_library(uWS STATIC
  uWS/uSockets/bsd.c
  uWS/uSockets/context.c
  uWS/uSockets/loop.c
  uWS/uSockets/socket.c
  uWS/uSockets/eventing/epoll_kqueue.c
  uWS/uSockets/crypto/openssl.c
  uWS/uSockets/crypto/sni_tree.cpp
)
target_include_directories(uWS
  PUBLIC
    ${ZLIB_INCLUDE_DIRS}
    uWS/uSockets/
  INTERFACE
    uWS/
)
target_compile_definitions(uWS PUBLIC LIBUS_USE_OPENSSL)
target_link_libraries(uWS INTERFACE zlibstatic)

option(LIBXML2_WITH_C14N "Add the Canonicalization support" OFF)
option(LIBXML2_WITH_CATALOG "Add the Catalog support" OFF)
option(LIBXML2_WITH_DEBUG "Add the debugging module" OFF)
option(LIBXML2_WITH_FTP "Add the FTP support" OFF)
option(LIBXML2_WITH_HTML "Add the HTML support" ON)
option(LIBXML2_WITH_HTTP "Add the HTTP support" OFF)
option(LIBXML2_WITH_ICONV "Add ICONV support" OFF)
option(LIBXML2_WITH_LZMA "Use liblzma" OFF)
option(LIBXML2_WITH_MODULES "Add the dynamic modules support" OFF)
option(LIBXML2_WITH_PATTERN "Add the xmlPattern selection interface" OFF)
option(LIBXML2_WITH_PROGRAMS "Build programs" OFF)
option(LIBXML2_WITH_PYTHON "Build Python bindings" OFF)
option(LIBXML2_WITH_SCHEMAS "Add Relax-NG and Schemas support" OFF)
option(LIBXML2_WITH_SCHEMATRON "Add Schematron support" OFF)
option(LIBXML2_WITH_TESTS "Build tests" OFF)
option(LIBXML2_WITH_THREADS "Add multithread support" ON)
option(LIBXML2_WITH_VALID "Add the DTD validation support" OFF)
option(LIBXML2_WITH_WRITER "Add the xmlWriter saving interface" OFF)
option(LIBXML2_WITH_XINCLUDE "Add the XInclude support" OFF)
option(LIBXML2_WITH_XPATH "Add the XPATH support" OFF)
option(LIBXML2_WITH_XPTR "Add the XPointer support" OFF)
option(LIBXML2_WITH_XPTR_LOCS "Add support for XPointer locations" OFF)
option(LIBXML2_WITH_ZLIB "Use libz" ON)
add_subdirectory(libxml2 libxml2)
add_dependencies(LibXml2 zlibstatic)

option(SIMDJSON_BUILD_STATIC_LIB "Build simdjson_static library along with simdjson" ON)
add_subdirectory(simdjson simdjson)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(flatbuffers flatbuffers)
add_subdirectory(spdlog spdlog)
option(SPM_ENABLE_SHARED "Builds shared libaries in addition to static libraries." OFF)
add_subdirectory(sentencepiece sentencepiece)
target_include_directories(sentencepiece-static SYSTEM INTERFACE sentencepiece/src/)

add_library(giflib STATIC
  giflib/dgif_lib.c
  giflib/egif_lib.c
  giflib/gif_err.c
  giflib/gif_font.c
  giflib/gif_hash.c
  giflib/gifalloc.c
  giflib/openbsd-reallocarray.c
)
target_include_directories(giflib SYSTEM PUBLIC giflib/)

add_library(jpeg STATIC
  libjpeg/jdapimin.c # Application program interface: core routines for decompression.
  libjpeg/jdapistd.c # Application program interface: standard decompression.
  libjpeg/jcomapi.c  # Application program interface routines common to compression and decompression.

  libjpeg/jdmaster.c # Master control: determines which other modules to use.
  libjpeg/jdinput.c # Input controller: controls input processing modules.
  libjpeg/jdmainct.c # Main buffer controller (JPEG decompressor => postprocessor).
  libjpeg/jdcoefct.c # Buffer controller for DCT coefficient buffer.
  libjpeg/jdpostct.c # Postprocessor buffer controller.
  libjpeg/jdmarker.c # JPEG marker reading.
  libjpeg/jdhuff.c # Huffman entropy decoding for sequential JPEG.
  libjpeg/jdphuff.c # Huffman entropy decoding for progressive JPEG.
  libjpeg/jddctmgr.c # IDCT manager (IDCT implementation selection & control).
  libjpeg/jidctint.c # Inverse DCT using slow-but-accurate integer method.
  libjpeg/jidctfst.c # Inverse DCT using faster, less accurate integer method.
  libjpeg/jidctflt.c # Inverse DCT using floating-point arithmetic.
  libjpeg/jidctred.c # Inverse DCTs with reduced-size outputs.
  libjpeg/jdsample.c # Upsampling.
  libjpeg/jdcolor.c # Color space conversion.
  libjpeg/jdmerge.c # Merged upsampling/color conversion (faster, lower quality).
  libjpeg/jquant1.c # One-pass color quantization using a fixed-spacing colormap.
  libjpeg/jquant2.c # Two-pass color quantization using a custom-generated colormap. Also handles one-pass quantization to an externally given map.
  libjpeg/jdatasrc.c # Data source manager for stdio input.

  libjpeg/jerror.c # Standard error handling routines (application replaceable).
  libjpeg/jmemmgr.c # System-independent (more or less) memory management code.
  libjpeg/jutils.c # Miscellaneous utility routines.

  libjpeg/jmemmgr.c # relies on a system-dependent memory management module.
  libjpeg/jmemnobs.c # "No backing store": assumes adequate virtual memory exists.
)
target_include_directories(jpeg SYSTEM PUBLIC libjpeg/)

# manipulating libpng cmake options from within cmake script
set(PNG_BUILD_ZLIB ON CACHE INTERNAL "" FORCE)
option(PNG_SHARED "Build shared lib" OFF)
option(PNG_TESTS  "Build libpng tests" OFF)
add_subdirectory(libpng libpng)
add_dependencies(png_static zlibstatic)

# libwebp insists on using the Android NDK, which is a pain to install on Termux,
# but it only needs the cpu-features library!
if (${CMAKE_SYSTEM_NAME} MATCHES "Android")
  include_directories(cpu_features/include/)
  execute_process(COMMAND mkdir -p
    ${CMAKE_CURRENT_BINARY_DIR}/ndk_root/sources/android
  )
  execute_process(COMMAND ln -s
    ${CMAKE_CURRENT_SOURCE_DIR}/cpu_features/ndk_compat
    ${CMAKE_CURRENT_BINARY_DIR}/ndk_root/sources/android/cpufeatures
  )
  set(ANDROID_NDK ${CMAKE_CURRENT_BINARY_DIR}/ndk_root)
  add_compile_options(-DSTACK_LINE_READER_BUFFER_SIZE=1024)
endif ()
set(ZLIB_LIBRARY zlibstatic)
set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib)
set(JPEG_LIBRARY jpeg)
set(JPEG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libjpeg)
set(PNG_LIBRARY png_static)
set(PNG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libpng/ ${CMAKE_CURRENT_BINARY_DIR}/libpng/)
set(PNG_PNG_INCLUDE_DIR ${PNG_INCLUDE_DIR}) # Bug in the webp cmake file?
set(GIF_LIBRARY giflib)
set(GIF_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/giflib/)
add_subdirectory(libwebp libwebp)
add_dependencies(webp png_static jpeg giflib)
target_include_directories(imagedec SYSTEM INTERFACE libwebp/imageio/)

set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_TESTS OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_LEGACY_SUPPORT OFF)
add_subdirectory(zstd/build/cmake zstd/build/cmake)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_subdirectory(catch2 catch2)
endif ()

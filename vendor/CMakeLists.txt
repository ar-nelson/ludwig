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
target_link_libraries(asio PUBLIC ssl crypto)
target_include_directories(asio SYSTEM PUBLIC asio/)
target_compile_definitions(asio PUBLIC ASIO_SEPARATE_COMPILATION)

add_library(uWS STATIC
  uWS/uSockets/bsd.c
  uWS/uSockets/context.c
  uWS/uSockets/loop.c
  uWS/uSockets/socket.c
  uWS/uSockets/eventing/asio.cpp
  uWS/uSockets/crypto/openssl.c
  uWS/uSockets/crypto/sni_tree.cpp
)
target_include_directories(uWS
  SYSTEM PUBLIC
    ${ZLIB_INCLUDE_DIRS}
    uWS/uSockets/
  SYSTEM INTERFACE
    uWS/
)
target_compile_definitions(uWS PUBLIC LIBUS_USE_ASIO LIBUS_USE_OPENSSL)
target_link_libraries(uWS
  PUBLIC
    asio
  INTERFACE
    zlibstatic
)

add_library(lmdb STATIC
  lmdb/mdb.c
  lmdb/midl.c
)
target_include_directories(lmdb SYSTEM PUBLIC lmdb/)

add_library(xxHash INTERFACE)
target_include_directories(xxHash SYSTEM INTERFACE xxHash/)
target_compile_definitions(xxHash INTERFACE XXH_INLINE_ALL)

add_library(csprng STATIC
  csprng/source/csprng.cpp)
target_include_directories(csprng PUBLIC csprng/source/)

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

# libjpeg-turbo: the modern version doesn't support add_subdirectory,
# so we use an old commit. And it needs dummy install directories.
option(WITH_SIMD "libjpeg-turbo" ON)
option(WITH_MEM_SRCDST "libjpeg-turbo" ON)
option(WITH_TURBOJPEG "libjpeg-turbo" ON)
option(WITH_CRT_DLL "libjpeg-turbo" OFF)
option(WITH_FUZZ "libjpeg-turbo" OFF)
option(WITH_JAVA "libjpeg-turbo" OFF)
set(CMAKE_INSTALL_DOCDIR "${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo/install/docdir")
set(CMAKE_INSTALL_MANDIR "${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo/install/mandir")
add_subdirectory(libjpeg-turbo libjpeg-turbo)
unset(CMAKE_INSTALL_DOCDIR)
unset(CMAKE_INSTALL_MANDIR)

# manipulating libpng cmake options from within cmake script
set(PNG_BUILD_ZLIB ON CACHE INTERNAL "" FORCE)
option(PNG_SHARED "Build shared lib" OFF)
option(PNG_TESTS  "Build libpng tests" OFF)
add_subdirectory(libpng libpng)
add_dependencies(png_static zlibstatic)

set(ZLIB_LIBRARY zlibstatic)
set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib)
set(JPEG_LIBRARY turbojpeg-static)
set(JPEG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libjpeg-turbo/ ${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo/)
set(PNG_LIBRARY png_static)
set(PNG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libpng/ ${CMAKE_CURRENT_BINARY_DIR}/libpng/)
set(PNG_PNG_INCLUDE_DIR ${PNG_INCLUDE_DIR}) # Bug in the webp cmake file?
set(GIF_LIBRARY giflib)
set(GIF_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/giflib/)
add_subdirectory(libwebp libwebp)
add_dependencies(webp png_static turbojpeg-static giflib)
target_include_directories(imagedec SYSTEM INTERFACE libwebp/imageio/)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_subdirectory(catch2 catch2)
endif ()

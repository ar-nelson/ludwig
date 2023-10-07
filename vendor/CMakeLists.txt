add_subdirectory(zlib zlib)

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

add_library(monocypher STATIC
  monocypher/monocypher.c
  monocypher/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC monocypher/ monocypher/optional/)

add_library(csprng STATIC
  csprng/source/csprng.cpp)
target_include_directories(csprng PUBLIC csprng/source/)

set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(SKIP_PERFORMANCE_COMPARISON ON CACHE BOOL "" FORCE)
add_subdirectory(cereal cereal)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(flatbuffers flatbuffers)
add_subdirectory(spdlog spdlog)

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

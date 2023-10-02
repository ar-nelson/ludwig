add_subdirectory(zlib EXCLUDE_FROM_ALL)

add_subdirectory(boringssl EXCLUDE_FROM_ALL)
include_directories("boringssl/include")

add_library(asio STATIC
  asio/asio.cpp
  asio/asio_ssl.cpp
)
target_link_libraries(asio PUBLIC ssl crypto)
target_include_directories(asio PUBLIC asio/)
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
  PUBLIC
    uWS/uSockets/
  INTERFACE
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
target_include_directories(lmdb PUBLIC lmdb/)

add_library(xxHash INTERFACE)
target_include_directories(xxHash INTERFACE xxHash/)
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
add_subdirectory(cereal EXCLUDE_FROM_ALL)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(flatbuffers EXCLUDE_FROM_ALL)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_subdirectory(catch2 EXCLUDE_FROM_ALL)
endif ()

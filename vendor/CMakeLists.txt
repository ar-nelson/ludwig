find_package(ZLIB REQUIRED)

add_library(asio STATIC
  asio/asio.cpp
)
target_include_directories(asio PUBLIC asio/)
target_compile_definitions(asio PUBLIC ASIO_SEPARATE_COMPILATION)

add_library(uWS STATIC
  uWS/uSockets/bsd.c
  uWS/uSockets/context.c
  uWS/uSockets/loop.c
  uWS/uSockets/socket.c
  uWS/uSockets/eventing/asio.cpp
)
target_include_directories(uWS
  PUBLIC
    uWS/uSockets/
  INTERFACE
    uWS/
)
target_compile_definitions(uWS PUBLIC LIBUS_USE_ASIO LIBUS_NO_SSL)
target_link_libraries(uWS
  PUBLIC
    asio
  INTERFACE
    ${ZLIB_LIBRARIES}
)

add_library(lmdb STATIC
  lmdb/mdb.c
  lmdb/midl.c
  lmdb/lmdb-safe.cc
)
target_include_directories(lmdb PUBLIC lmdb/)

add_subdirectory(flatbuffers)

set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(rapidjson)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_subdirectory(catch2)
endif ()

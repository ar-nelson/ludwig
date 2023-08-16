find_package(ZLIB REQUIRED)

add_library(uWS STATIC
  usockets/src/bsd.c
  usockets/src/context.c
  usockets/src/loop.c
  usockets/src/socket.c
  asio/asio/src/asio.cpp
  usockets-asio.c++
)
target_include_directories(uWS
  PUBLIC
    usockets/src/ asio/asio/include/
  INTERFACE
    uwebsockets/src/
)
target_compile_definitions(uWS PRIVATE LIBUS_NO_SSL)
target_link_libraries(uWS INTERFACE ${ZLIB_LIBRARIES})

add_library(lmdb STATIC
  openldap/libraries/liblmdb/mdb.c
  openldap/libraries/liblmdb/midl.c
  lmdb-safe/lmdb-safe.cc
)
target_include_directories(lmdb PUBLIC openldap/libraries/liblmdb/ lmdb-safe/)

add_subdirectory(flatbuffers)

set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(rapidjson)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_subdirectory(catch2)
endif ()

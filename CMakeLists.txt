cmake_minimum_required (VERSION 3.4.1)

project (ludwig VERSION 0.1 LANGUAGES C CXX)

set (CMAKE_C_STANDARD 11)
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_VERBOSE_MAKEFILE ON)

# Export metadata for clangd, even in a nix-shell.
set (CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

add_compile_options (
  -fcolor-diagnostics
  -DASIO_SEPARATE_COMPILATION
  -DLIBUS_USE_ASIO
  -Wno-zero-as-null-pointer-constant
)

# Detect Android Termux to add -llog if necessary
if (${CMAKE_SYSTEM_NAME} MATCHES "Android")
  add_link_options (-llog)
endif ()

add_subdirectory(vendor)

# Generate flatbuffers headers
# This must be a custom command to add the right command-line flags
add_custom_command(
  COMMAND ${PROJECT_BINARY_DIR}/flatc
    -o src/generated/
    -I src/
    --cpp-str-type std::string_view
    --cpp-std c++17
    --gen-json-emit --strict-json
    --reflect-types --reflect-names
    -c src/datatypes.fbs
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  DEPENDS src/datatypes.fbs ${PROJECT_BINARY_DIR}/flatc
  OUTPUT src/generated/datatypes_generated.h
  VERBATIM
)
add_custom_target(flatbuffers_generated DEPENDS src/generated/datatypes_generated.h)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  set (BUILDTYPE_COMPILE_OPTIONS
    -DDEBUG
    -DLOG_TO_CONSOLE
    -g
    -O1
    -fno-omit-frame-pointer
    -fsanitize=address
  )
  set (LINK_OPTIONS -fsanitize=address)
else ()
  set (BUILDTYPE_COMPILE_OPTIONS -O2)
  set (LINK_OPTIONS )
endif ()

set (COMPILE_OPTIONS
  ${BUILDTYPE_COMPILE_OPTIONS}
  -Wall
  -Wextra
  -pedantic-errors
  -Wconversion
  -Wsign-conversion
  -Wno-unused-function
)

set (LUDWIG_COMMON_SRC
  src/id.c++
  src/db.c++
)

add_executable(ludwig src/ludwig.c++ ${LUDWIG_COMMON_SRC})
add_dependencies(ludwig flatbuffers_generated)
target_compile_options(ludwig PRIVATE ${COMPILE_OPTIONS})
target_link_options(ludwig PRIVATE ${LINK_OPTIONS})
target_link_libraries(ludwig PRIVATE lmdb FlatBuffers RapidJSON uWS xxHash)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  file(GLOB_RECURSE TEST_FILES "test/*.c++")
  add_executable(test ${LUDWIG_COMMON_SRC} ${TEST_FILES})
  add_dependencies(test flatbuffers_generated)
  target_link_libraries(test PRIVATE lmdb FlatBuffers RapidJSON uWS xxHash Catch2::Catch2WithMain)
endif ()

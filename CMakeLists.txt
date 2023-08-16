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
)

add_subdirectory(vendor)

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
  -Wno-zero-as-null-pointer-constant
)

set (LUDWIG_COMMON_SRC
  # files go here
)

add_executable(ludwig src/ludwig.c++ ${LUDWIG_COMMON_SRC})
target_compile_options(ludwig PRIVATE ${COMPILE_OPTIONS})
target_link_options(ludwig PRIVATE ${LINK_OPTIONS})
target_link_libraries(ludwig PRIVATE lmdb FlatBuffers RapidJSON uWS)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  file(GLOB_RECURSE TEST_FILES "test/*.c++")
  add_executable(test ${LUDWIG_COMMON_SRC} ${TEST_FILES})
  target_link_libraries(test PRIVATE lmdb FlatBuffers RapidJSON uWS Catch2::Catch2WithMain)
endif ()

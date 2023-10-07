cmake_minimum_required (VERSION 3.4.1)

project (ludwig VERSION 0.1 LANGUAGES C CXX)

set (CMAKE_C_STANDARD 11)
set (CMAKE_CXX_STANDARD 23)
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

# Only do static linking. No shared libraries at all.
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Detect Android Termux to add -llog if necessary
if (${CMAKE_SYSTEM_NAME} MATCHES "Android")
  add_link_options (-llog)
endif ()

# Generate static files
set(static_depends)
find_program(resource_compiler xxd)
file(GLOB_RECURSE static_files "static/*")
foreach (static_path IN ITEMS ${static_files})
  cmake_path(GET static_path FILENAME static_basename)
  string(REGEX REPLACE "[./-]" "_" static_symbol ${static_basename})
  set(static_outname "${PROJECT_SOURCE_DIR}/src/generated/${static_basename}.h")
  add_custom_command(
    COMMAND ${resource_compiler} -i -n ${static_symbol} ${static_path} ${static_outname}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    DEPENDS ${static_path}
    OUTPUT ${static_outname}
    VERBATIM
  )
  list(APPEND static_depends ${static_outname})
endforeach ()
add_custom_target(static_generated DEPENDS ${static_depends})

add_subdirectory(vendor EXCLUDE_FROM_ALL SYSTEM)
include_directories(vendor/singleheader/)

add_compile_options (
  -fcolor-diagnostics
  -ferror-limit=0
  -DASIO_SEPARATE_COMPILATION
  -DLIBUS_USE_ASIO
  -Wno-gnu-anonymous-struct
  -Wno-nested-anon-types
)

# Generate flatbuffers headers
# This must be a custom command to add the right command-line flags
add_custom_command(
  COMMAND ${PROJECT_BINARY_DIR}/flatc
    -o src/generated/
    -I src/
    --cpp-std c++17
    --gen-json-emit --strict-json
    --gen-mutable
    --reflect-types --reflect-names
    -c src/datatypes.fbs
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  DEPENDS src/datatypes.fbs ${PROJECT_BINARY_DIR}/flatc
  OUTPUT src/generated/datatypes_generated.h
  VERBATIM
)
add_custom_command(
  COMMAND ${resource_compiler} -i -n datatypes_fbs src/datatypes.fbs src/generated/datatypes.fbs.h
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  DEPENDS src/datatypes.fbs
  OUTPUT src/generated/datatypes.fbs.h
  VERBATIM
)
add_custom_target(flatbuffers_generated DEPENDS
  src/generated/datatypes_generated.h
  src/generated/datatypes.fbs.h
)

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
  src/base64.c++
  src/jwt.c++
  src/db.c++
  src/controller.c++
  src/webapp_routes.c++
  src/asio_http_client.c++
  src/thumbnailer.c++
)

if (CMAKE_BUILD_TYPE MATCHES Release AND CMAKE_SYSTEM_NAME STREQUAL "Linux")

  # A hack for static compilation.
  #
  # I don't know why, but clang's libstdc++ references arc4random from libbsd,
  # and with -static this function isn't available anymore‽
  #
  # Linux only, because this function should be available on BSD and Mac.

  set (LUDWIG_COMMON_SRC ${LUDWIG_COMMON_SRC} vendor/libbsd/src/arc4random.c)

endif ()

add_executable(ludwig src/ludwig.c++ ${LUDWIG_COMMON_SRC})
add_dependencies(ludwig static_generated flatbuffers_generated)
target_compile_options(ludwig PRIVATE ${COMPILE_OPTIONS})
target_link_options(ludwig PRIVATE ${LINK_OPTIONS})
if (CMAKE_BUILD_TYPE MATCHES Release)
  # Only build a fully static binary in release mode.
  # Debug mode needs to dynamically link asan.
  target_link_libraries(ludwig PRIVATE "-static")
endif ()
target_link_libraries(ludwig PRIVATE cereal csprng flatbuffers lmdb monocypher
  ssl crypto spdlog uWS xxHash webp giflib turbojpeg-static png_static imagedec)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  file(GLOB_RECURSE TEST_FILES "test/*.c++")
  add_executable(test ${LUDWIG_COMMON_SRC} ${TEST_FILES})
  add_dependencies(test static_generated flatbuffers_generated)
  target_link_libraries(test PRIVATE cereal csprng flatbuffers lmdb monocypher
    ssl crypto spdlog uWS xxHash webp giflib turbojpeg-static png_static imagedec Catch2::Catch2WithMain)
endif ()

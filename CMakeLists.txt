cmake_minimum_required (VERSION 3.4.1)

project (ludwig VERSION 0.1 LANGUAGES C CXX ASM)

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

# Detect Android Termux
if (${CMAKE_SYSTEM_NAME} MATCHES "Android")
  add_link_options(-llog)
  # Hack to fix libwebp
  if ("X${ANDROID_ABI}" STREQUAL "X")
    set(ANDROID_ABI aarch64)
  endif ()
endif ()

# Generate static files
set(static_depends)
set(static_asm)
file(GLOB_RECURSE static_files "${PROJECT_SOURCE_DIR}/src/static/*.*")
foreach (static_path IN ITEMS ${static_files})
  cmake_path(GET static_path FILENAME static_basename)
  set(static_h "${PROJECT_BINARY_DIR}/generated/static/${static_basename}.h++")
  set(static_S "${PROJECT_BINARY_DIR}/generated/static/${static_basename}.S")
  add_custom_command(
    COMMAND "${PROJECT_SOURCE_DIR}/extras/binary-header.sh" ${static_path} ${PROJECT_BINARY_DIR}
    DEPENDS ${static_path}
    OUTPUT ${static_h} ${static_S}
    VERBATIM
  )
  list(APPEND static_depends ${static_h} ${static_S})
  list(APPEND static_asm ${static_S})
endforeach ()
add_custom_target(static_generated DEPENDS ${static_depends})

add_subdirectory(vendor EXCLUDE_FROM_ALL SYSTEM)

add_compile_options (
  -fcolor-diagnostics
  -ferror-limit=0
  -DASIO_SEPARATE_COMPILATION
  -Wno-gnu-anonymous-struct
  -Wno-nested-anon-types
)

# Generate flatbuffers headers
# This must be a custom command to add the right command-line flags
set(fbs_depends)
file(GLOB_RECURSE fbs_files "${PROJECT_SOURCE_DIR}/src/models/*.fbs")
foreach (fbs_path IN ITEMS ${fbs_files})
  cmake_path(GET fbs_path FILENAME fbs_basename)
  get_filename_component(fbs_prefix ${fbs_basename} NAME_WE)
  set(fbs_gen_outname "${PROJECT_BINARY_DIR}/generated/models/${fbs_prefix}.h++")
  set(fbs_src_h "${PROJECT_BINARY_DIR}/generated/models/${fbs_basename}.h++")
  set(fbs_src_S "${PROJECT_BINARY_DIR}/generated/models/${fbs_basename}.S")
  add_custom_command(
    COMMAND "${PROJECT_BINARY_DIR}/flatc"
      -o "${PROJECT_BINARY_DIR}/generated/models/"
      -I "${PROJECT_SOURCE_DIR}/src/models/"
      --cpp-std c++17
      --gen-json-emit --strict-json
      --reflect-types --reflect-names
      --filename-suffix "" --filename-ext "h++"
      -c "${fbs_path}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    DEPENDS "${fbs_path}" "${PROJECT_BINARY_DIR}/flatc"
    OUTPUT "${fbs_gen_outname}"
    VERBATIM
  )
  add_custom_command(
    COMMAND "${PROJECT_SOURCE_DIR}/extras/binary-header.sh" ${fbs_path} ${PROJECT_BINARY_DIR}
    DEPENDS ${fbs_path}
    OUTPUT ${fbs_src_h} ${fbs_src_S}
    VERBATIM
  )
  list(APPEND fbs_depends ${fbs_gen_outname} ${fbs_src_h} ${fbs_src_S})
  list(APPEND static_asm ${fbs_src_S})
endforeach ()
add_custom_target(flatbuffers_generated DEPENDS ${fbs_depends})

if (${CMAKE_BUILD_TYPE} MATCHES Debug)
  set (buildtype_compile_options
    -DLUDWIG_DEBUG=1
    -g
    -O0
    -fno-omit-frame-pointer
    -fsanitize=address,undefined
    -fno-sanitize=alignment
  )
  set (link_options -fsanitize=address,undefined -fno-sanitize=alignment -stdlib=libc++ -lc++abi)
else ()
  set (buildtype_compile_options -O3)
  set (link_options -stdlib=libc++ -lc++abi)
endif ()

set (compile_options
  ${buildtype_compile_options}
  -Wall
  -Wextra
  -pedantic-errors
  -Wconversion
  -Wsign-conversion
  -Wno-unused-function
)

set (ludwig_common_src
  ${static_asm}
  src/util/base64.c++
  src/util/iter.c++
  src/util/jwt.c++
  src/util/rate_limiter.c++
  src/util/rich_text.c++
  src/util/thumbnailer.c++
  src/util/zstd_db_dump.c++
  src/services/asio_event_bus.c++
  src/services/asio_http_client.c++
  src/services/db.c++
  src/services/lmdb_search_engine.c++
  src/services/thumbnail_cache.c++
  src/models/detail.c++
  src/models/protocols_parser.c++
  src/controllers/instance.c++
  src/controllers/lemmy_api.c++
  src/controllers/remote_media.c++
  src/views/media.c++
  src/views/lemmy_api.c++
  src/views/webapp.c++
)

set (ludwig_libs
  asio
  crypto
  flatbuffers
  giflib
  imagedec
  jpeg
  LibXml2
  libzstd_static
  png_static
  sentencepiece-static
  simdjson_static
  spdlog
  ssl
  uWS
  webp
  vendor-misc
)

add_executable(ludwig src/ludwig.c++ ${ludwig_common_src})
add_dependencies(ludwig static_generated flatbuffers_generated)
target_compile_options(ludwig PRIVATE ${compile_options})
target_link_options(ludwig PRIVATE ${link_options})
target_include_directories(ludwig PUBLIC src/ "${PROJECT_BINARY_DIR}/generated/")
if (${CMAKE_BUILD_TYPE} MATCHES Release)
  # Only build a fully static binary in release mode.
  # Debug mode needs to dynamically link asan.
  target_link_libraries(ludwig PRIVATE "-static")
endif ()
target_link_libraries(ludwig PRIVATE ${ludwig_libs})

if (${CMAKE_BUILD_TYPE} MATCHES Debug)
  # Don't build tests on Android.
  # Unfortunately, Catch2's templates are so complex that building tests will
  # frequently trigger Android's OOM killer and crash Termux.
  if (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Android")
    file(GLOB_RECURSE test_files "test/*.c++")
    add_executable(test ${ludwig_common_src} ${test_files})
    add_dependencies(test static_generated flatbuffers_generated)
    target_link_libraries(test PRIVATE ${ludwig_libs} Catch2::Catch2WithMain)
    target_include_directories(test PRIVATE src/ "${PROJECT_BINARY_DIR}/generated/")
  endif ()
endif ()

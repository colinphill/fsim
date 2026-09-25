# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

set(FSIM_SQLITE_VERSION "3.53.4")
set(FSIM_SQLITE_ARCHIVE_NAME "sqlite-amalgamation-3530400.zip")
set(FSIM_SQLITE_ARCHIVE_SIZE 2946650)
set(FSIM_SQLITE_ARCHIVE_SHA256
    "1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d")
set(FSIM_SQLITE_SOURCE_ROOT "sqlite-amalgamation-3530400")
set(FSIM_SQLITE_TREE_SHA256
    "b75b857f6ee196fb97ad998a89e905e135f37aeccd7db5645f3d88eece3bd124")

function(fsim_sqlite_validate_archive archive)
  if(NOT EXISTS "${archive}" OR IS_DIRECTORY "${archive}")
    message(FATAL_ERROR
      "official SQLite ${FSIM_SQLITE_VERSION} archive is missing: ${archive}")
  endif()
  file(SIZE "${archive}" actual_size)
  if(NOT actual_size EQUAL FSIM_SQLITE_ARCHIVE_SIZE)
    message(FATAL_ERROR
      "SQLite archive size mismatch: expected ${FSIM_SQLITE_ARCHIVE_SIZE}, found ${actual_size}")
  endif()
  file(SHA256 "${archive}" actual_digest)
  if(NOT actual_digest STREQUAL FSIM_SQLITE_ARCHIVE_SHA256)
    message(FATAL_ERROR "SQLite archive SHA-256 mismatch: ${actual_digest}")
  endif()
endfunction()

function(fsim_sqlite_validate_source_tree root)
  if(NOT IS_DIRECTORY "${root}")
    message(FATAL_ERROR "SQLite source root is missing: ${root}")
  endif()
  file(GLOB_RECURSE files LIST_DIRECTORIES FALSE RELATIVE "${root}" "${root}/*")
  list(SORT files)
  set(expected_files shell.c sqlite3.c sqlite3.h sqlite3ext.h)
  if(NOT "${files}" STREQUAL "${expected_files}")
    message(FATAL_ERROR "SQLite extracted source file set does not match the pinned archive")
  endif()
  set(canonical "")
  foreach(relative_path IN LISTS files)
    if(IS_SYMLINK "${root}/${relative_path}")
      message(FATAL_ERROR "SQLite extracted source is a symlink: ${relative_path}")
    endif()
    file(SHA256 "${root}/${relative_path}" digest)
    string(APPEND canonical "${digest}  ${relative_path}\n")
  endforeach()
  string(SHA256 tree_digest "${canonical}")
  if(NOT tree_digest STREQUAL FSIM_SQLITE_TREE_SHA256)
    message(FATAL_ERROR "SQLite extracted source SHA-256 mismatch: ${tree_digest}")
  endif()
endfunction()

function(fsim_sqlite_materialize_source archive work_root output_root)
  fsim_sqlite_validate_archive("${archive}")
  set(root "${work_root}/${FSIM_SQLITE_SOURCE_ROOT}")
  if(NOT EXISTS "${root}")
    set(staging "${work_root}/extract-${FSIM_SQLITE_ARCHIVE_SHA256}")
    file(REMOVE_RECURSE "${staging}")
    file(MAKE_DIRECTORY "${staging}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${staging}")
    fsim_sqlite_validate_source_tree("${staging}/${FSIM_SQLITE_SOURCE_ROOT}")
    file(RENAME "${staging}/${FSIM_SQLITE_SOURCE_ROOT}" "${root}")
    file(REMOVE_RECURSE "${staging}")
  endif()
  # Check every configure, including an already materialized source directory.
  fsim_sqlite_validate_source_tree("${root}")
  set(${output_root} "${root}" PARENT_SCOPE)
endfunction()

function(fsim_add_sqlite)
  if(TARGET fsim_sqlite)
    return()
  endif()
  set(vendor_root
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/sqlite-${FSIM_SQLITE_VERSION}")
  fsim_sqlite_materialize_source(
    "${vendor_root}/${FSIM_SQLITE_ARCHIVE_NAME}"
    "${CMAKE_BINARY_DIR}/_deps/fsim_sqlite_3_53_4-src"
    source_root)

  find_package(Threads REQUIRED)
  add_library(fsim_sqlite STATIC "${source_root}/sqlite3.c")
  set_target_properties(fsim_sqlite PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    COMPILE_WARNING_AS_ERROR OFF)
  target_include_directories(fsim_sqlite SYSTEM PUBLIC
    "$<BUILD_INTERFACE:${source_root}>")
  target_compile_definitions(fsim_sqlite PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION=1)
  target_link_libraries(fsim_sqlite PRIVATE Threads::Threads)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(fsim_sqlite PRIVATE m)
  endif()

  # The upstream amalgamation does not follow fsim's authored-source warning
  # policy. Keep caller-supplied global warning settings from making it fatal.
  if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(fsim_sqlite PRIVATE /WX-)
  elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
    target_compile_options(fsim_sqlite PRIVATE -Wno-error)
  endif()

  install(FILES
    "${vendor_root}/LICENSE"
    "${vendor_root}/NOTICE"
    "${vendor_root}/SOURCE_MANIFEST.txt"
    "${vendor_root}/sqlite-${FSIM_SQLITE_VERSION}.spdx.json"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/third-party/sqlite-${FSIM_SQLITE_VERSION}")
endfunction()

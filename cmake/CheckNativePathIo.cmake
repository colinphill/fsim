# SPDX-License-Identifier: Apache-2.0

# Keep product filesystem I/O on the long-path seams. Windows rejects ordinary
# paths beyond MAX_PATH unless the process and host both opt in, so product
# sources reach std::filesystem, file streams, and native file APIs through
# support::native_fs or support::path_for_native_io.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_SEAM_FILES
  include/fsim/support/native_filesystem.hpp
  include/fsim/support/path.hpp)
set(FSIM_FILESYSTEM_OPERATIONS
  "canonical|copy|copy_file|copy_symlink|create_directories|create_directory"
  "|create_directory_symlink|create_hard_link|create_symlink|equivalent"
  "|exists|file_size|hard_link_count|is_block_file|is_character_file"
  "|is_directory|is_empty|is_fifo|is_other|is_regular_file|is_socket"
  "|is_symlink|last_write_time|permissions|proximate|read_symlink|relative"
  "|remove|remove_all|rename|resize_file|space|status|symlink_status"
  "|weakly_canonical")
string(JOIN "" FSIM_FILESYSTEM_OPERATIONS ${FSIM_FILESYSTEM_OPERATIONS})
set(FSIM_NATIVE_FILE_APIS
  CopyFileExW CopyFileW CreateDirectoryW CreateFileA CreateFileW DeleteFileW
  FindFirstFileExW FindFirstFileW GetFileAttributesExW GetFileAttributesW
  LoadLibraryExW LoadLibraryW MoveFileExW MoveFileW RemoveDirectoryW
  ReplaceFileW SetFileAttributesW Tcl_EvalFile _wfopen _wopen fopen freopen
  sqlite3_open sqlite3_open16 sqlite3_open_v2)
# Reviewed calls that keep an ordinary spelling: file|API|count.
set(FSIM_REVIEWED_NATIVE_CALLS
  # The loader accepts long paths; an extended spelling hides the module
  # from GetModuleHandleExW.
  "src/platform/dynamic_library.cpp|LoadLibraryExW|1"
  # Tcl reads batch scripts through its own filesystem layer, which
  # third_party/tcl-9.0.4 patches for long paths on Windows.
  "src/app/tcl.cpp|Tcl_EvalFile|1"
  # POSIX-only /proc/self/maps.
  "src/runtime/tf_containment.cpp|fopen|1"
  # The NUL device for compiler standard input.
  "src/systemc/plugin_compiler_process.cpp|CreateFileW|1"
  # SQLite catalog handles, not file streams.
  "src/app/application_workspace_store_catalog.cpp|open|2")
if(DEFINED FSIM_NATIVE_PATH_IO_TEST_REVIEWED)
  set(FSIM_REVIEWED_NATIVE_CALLS "${FSIM_NATIVE_PATH_IO_TEST_REVIEWED}")
endif()

set(FSIM_BOUNDARY "(^|[^A-Za-z0-9_])")
set(FSIM_SPACE "[ \t\r\n]*")
set(FSIM_ARGUMENT "[({]${FSIM_SPACE}[^)} \t\r\n]")

file(GLOB_RECURSE FSIM_SOURCES LIST_DIRECTORIES FALSE
  RELATIVE "${FSIM_SOURCE_DIR}"
  "${FSIM_SOURCE_DIR}/src/*.c" "${FSIM_SOURCE_DIR}/src/*.cpp"
  "${FSIM_SOURCE_DIR}/src/*.h" "${FSIM_SOURCE_DIR}/src/*.hpp"
  "${FSIM_SOURCE_DIR}/include/fsim/*.h" "${FSIM_SOURCE_DIR}/include/fsim/*.hpp")
list(SORT FSIM_SOURCES)
list(LENGTH FSIM_SOURCES FSIM_SOURCE_COUNT)
if(FSIM_SOURCE_COUNT LESS 1)
  message(FATAL_ERROR "native path I/O gate found no product sources")
endif()

set(FSIM_VIOLATIONS)
set(FSIM_REVIEWED_SEEN)
foreach(FSIM_RELATIVE IN LISTS FSIM_SOURCES)
  if(FSIM_RELATIVE IN_LIST FSIM_SEAM_FILES)
    continue()
  endif()
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}" FSIM_TEXT)
  string(REPLACE "\r\n" "\n" FSIM_TEXT "${FSIM_TEXT}")
  string(REGEX REPLACE "//[^\n]*" "" FSIM_TEXT "${FSIM_TEXT}")

  set(FSIM_NAMESPACES "std::filesystem")
  string(REGEX MATCHALL
    "namespace${FSIM_SPACE}[A-Za-z_][A-Za-z0-9_]*${FSIM_SPACE}=${FSIM_SPACE}std::filesystem${FSIM_SPACE};"
    FSIM_ALIASES "${FSIM_TEXT}")
  foreach(FSIM_ALIAS IN LISTS FSIM_ALIASES)
    string(REGEX REPLACE
      "^namespace${FSIM_SPACE}([A-Za-z_][A-Za-z0-9_]*).*$" "\\1"
      FSIM_ALIAS "${FSIM_ALIAS}")
    string(APPEND FSIM_NAMESPACES "|${FSIM_ALIAS}")
  endforeach()

  set(FSIM_RULES)
  string(FIND "${FSIM_TEXT}" "::" FSIM_QUALIFIED)
  if(NOT FSIM_QUALIFIED EQUAL -1)
    list(APPEND FSIM_RULES
      "filesystem operation@@${FSIM_BOUNDARY}(${FSIM_NAMESPACES})::(${FSIM_FILESYSTEM_OPERATIONS})${FSIM_SPACE}\\("
      "directory iterator@@${FSIM_BOUNDARY}(${FSIM_NAMESPACES})::(recursive_)?directory_iterator${FSIM_SPACE}${FSIM_ARGUMENT}"
      "directory iterator@@${FSIM_BOUNDARY}(${FSIM_NAMESPACES})::(recursive_)?directory_iterator[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*${FSIM_SPACE}${FSIM_ARGUMENT}"
      "filesystem using-declaration@@using${FSIM_SPACE}(namespace${FSIM_SPACE})?std::filesystem${FSIM_SPACE}[^A-Za-z0-9_]")
  endif()
  string(FIND "${FSIM_TEXT}" "fstream" FSIM_STREAMS)
  if(NOT FSIM_STREAMS EQUAL -1)
    list(APPEND FSIM_RULES
      "file stream@@std::(basic_)?(i|o)?fstream(<[^>\n]*>)?${FSIM_SPACE}${FSIM_ARGUMENT}"
      "file stream@@std::(basic_)?(i|o)?fstream(<[^>\n]*>)?[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*${FSIM_SPACE}${FSIM_ARGUMENT}")
  endif()
  foreach(FSIM_RULE IN LISTS FSIM_RULES)
    string(REPLACE "@@" ";" FSIM_RULE_FIELDS "${FSIM_RULE}")
    list(GET FSIM_RULE_FIELDS 0 FSIM_RULE_NAME)
    list(GET FSIM_RULE_FIELDS 1 FSIM_RULE_PATTERN)
    string(REGEX MATCHALL "${FSIM_RULE_PATTERN}" FSIM_MATCHES "${FSIM_TEXT}")
    foreach(FSIM_MATCH IN LISTS FSIM_MATCHES)
      string(STRIP "${FSIM_MATCH}" FSIM_MATCH)
      string(REGEX REPLACE "[ \t\r\n]+" " " FSIM_MATCH "${FSIM_MATCH}")
      list(APPEND FSIM_VIOLATIONS
        "${FSIM_RELATIVE}: ${FSIM_RULE_NAME} bypasses support::native_fs: ${FSIM_MATCH}")
    endforeach()
  endforeach()

  # Streams created elsewhere must be built from a native_fs stream. Matches
  # stop before ';', which CMake lists would otherwise split.
  if(NOT FSIM_STREAMS EQUAL -1)
    string(REGEX MATCHALL "make_(unique|shared)<std::(i|o)?fstream>\\([^;]*"
      FSIM_MATCHES "${FSIM_TEXT}")
    foreach(FSIM_MATCH IN LISTS FSIM_MATCHES)
      if(NOT FSIM_MATCH MATCHES "native_fs::open_(i|o)?fstream\\(")
        string(REGEX REPLACE "[ \t\r\n]+" " " FSIM_MATCH "${FSIM_MATCH}")
        list(APPEND FSIM_VIOLATIONS
          "${FSIM_RELATIVE}: file stream bypasses support::native_fs: ${FSIM_MATCH}")
      endif()
    endforeach()
  endif()

  # Stream member opens use native_fs::open; other open() members are reviewed.
  string(REGEX MATCHALL "(\\.|->)open${FSIM_SPACE}\\(" FSIM_MATCHES "${FSIM_TEXT}")
  list(LENGTH FSIM_MATCHES FSIM_OPEN_COUNT)
  set(FSIM_API_COUNTS)
  if(FSIM_OPEN_COUNT GREATER 0)
    list(APPEND FSIM_API_COUNTS "open|${FSIM_OPEN_COUNT}")
  endif()

  foreach(FSIM_API IN LISTS FSIM_NATIVE_FILE_APIS)
    string(FIND "${FSIM_TEXT}" "${FSIM_API}" FSIM_API_OFFSET)
    if(FSIM_API_OFFSET EQUAL -1)
      continue()
    endif()
    string(REGEX MATCHALL "${FSIM_BOUNDARY}${FSIM_API}${FSIM_SPACE}\\([^;]*"
      FSIM_MATCHES "${FSIM_TEXT}")
    set(FSIM_ORDINARY 0)
    foreach(FSIM_MATCH IN LISTS FSIM_MATCHES)
      if(NOT FSIM_MATCH MATCHES "path_for_native_io|native_fs::")
        math(EXPR FSIM_ORDINARY "${FSIM_ORDINARY} + 1")
      endif()
    endforeach()
    if(FSIM_ORDINARY GREATER 0)
      list(APPEND FSIM_API_COUNTS "${FSIM_API}|${FSIM_ORDINARY}")
    endif()
  endforeach()

  foreach(FSIM_API_COUNT IN LISTS FSIM_API_COUNTS)
    set(FSIM_REVIEWED "${FSIM_RELATIVE}|${FSIM_API_COUNT}")
    if(FSIM_REVIEWED IN_LIST FSIM_REVIEWED_NATIVE_CALLS)
      list(APPEND FSIM_REVIEWED_SEEN "${FSIM_REVIEWED}")
    else()
      string(REPLACE "|" " x" FSIM_API_COUNT "${FSIM_API_COUNT}")
      list(APPEND FSIM_VIOLATIONS
        "${FSIM_RELATIVE}: unreviewed native file call keeps an ordinary path: ${FSIM_API_COUNT}")
    endif()
  endforeach()
endforeach()

foreach(FSIM_REVIEWED IN LISTS FSIM_REVIEWED_NATIVE_CALLS)
  if(NOT FSIM_REVIEWED IN_LIST FSIM_REVIEWED_SEEN)
    list(APPEND FSIM_VIOLATIONS
      "reviewed native file call no longer matches its source: ${FSIM_REVIEWED}")
  endif()
endforeach()

if(FSIM_VIOLATIONS)
  list(JOIN FSIM_VIOLATIONS "\n  " FSIM_REPORT)
  message(FATAL_ERROR
    "product filesystem I/O must use support::native_fs or "
    "support::path_for_native_io so Windows paths beyond MAX_PATH work:\n  "
    "${FSIM_REPORT}")
endif()
message(STATUS
  "native path I/O: ${FSIM_SOURCE_COUNT} product sources on long-path seams")

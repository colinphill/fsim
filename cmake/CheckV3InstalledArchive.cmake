# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS FSIM_SOURCE_DIR FSIM_ARCHIVE_AUDIT_BINARY_DIR
    FSIM_ARCHIVE_AUDIT_ARCHIVE FSIM_ARCHIVE_AUDIT_ROOT
    FSIM_ARCHIVE_AUDIT_REGRESSION_LOG FSIM_ARCHIVE_AUDIT_WORK_DIR
    FSIM_ARCHIVE_AUDIT_EXPECTED_TESTS FSIM_ARCHIVE_AUDIT_EXPECTED_ENTRIES
    FSIM_ARCHIVE_AUDIT_TOOLCHAIN)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
if(NOT FSIM_ARCHIVE_AUDIT_EXPECTED_TESTS MATCHES "^[0-9]+$"
    OR NOT FSIM_ARCHIVE_AUDIT_EXPECTED_ENTRIES MATCHES "^[0-9]+$")
  message(FATAL_ERROR "v3 installed-archive counts must be decimal integers")
endif()
if(NOT EXISTS "${FSIM_ARCHIVE_AUDIT_ARCHIVE}"
    OR NOT EXISTS "${FSIM_ARCHIVE_AUDIT_REGRESSION_LOG}")
  message(FATAL_ERROR "v3 installed-archive evidence is missing")
endif()

file(READ "${FSIM_ARCHIVE_AUDIT_REGRESSION_LOG}" regression)
string(FIND "${regression}"
  "100% tests passed, 0 tests failed out of ${FSIM_ARCHIVE_AUDIT_EXPECTED_TESTS}"
  pass_offset)
if(pass_offset EQUAL -1)
  message(FATAL_ERROR "v3 installed-archive regression log is not green")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar tf "${FSIM_ARCHIVE_AUDIT_ARCHIVE}"
  RESULT_VARIABLE list_result OUTPUT_VARIABLE listing ERROR_VARIABLE list_error)
if(NOT list_result EQUAL 0)
  message(FATAL_ERROR "cannot list v3 installed archive: ${list_error}")
endif()
string(REPLACE "\r\n" "\n" listing "${listing}")
string(REGEX REPLACE "\n$" "" listing "${listing}")
string(REPLACE "\n" ";" entries "${listing}")
list(LENGTH entries entry_count)
if(NOT entry_count EQUAL FSIM_ARCHIVE_AUDIT_EXPECTED_ENTRIES)
  message(FATAL_ERROR
    "v3 installed archive expected ${FSIM_ARCHIVE_AUDIT_EXPECTED_ENTRIES} entries, found ${entry_count}")
endif()
set(seen)
foreach(entry IN LISTS entries)
  string(FIND "${entry}" "${FSIM_ARCHIVE_AUDIT_ROOT}/" root_offset)
  if(NOT root_offset EQUAL 0 OR entry MATCHES "(^|/)\\.\\.?(/|$)"
      OR entry MATCHES "^[A-Za-z]:" OR entry MATCHES "\\\\"
      OR entry MATCHES "//" OR entry IN_LIST seen)
    message(FATAL_ERROR "unsafe or duplicate v3 installed archive entry")
  endif()
  list(APPEND seen "${entry}")
endforeach()

file(REMOVE_RECURSE "${FSIM_ARCHIVE_AUDIT_WORK_DIR}")
file(MAKE_DIRECTORY "${FSIM_ARCHIVE_AUDIT_WORK_DIR}/extract")
file(ARCHIVE_EXTRACT INPUT "${FSIM_ARCHIVE_AUDIT_ARCHIVE}"
  DESTINATION "${FSIM_ARCHIVE_AUDIT_WORK_DIR}/extract")
set(prefix
  "${FSIM_ARCHIVE_AUDIT_WORK_DIR}/extract/${FSIM_ARCHIVE_AUDIT_ROOT}")
if(DEFINED FSIM_ARCHIVE_AUDIT_EXECUTABLE_SUFFIX)
  set(suffix "${FSIM_ARCHIVE_AUDIT_EXECUTABLE_SUFFIX}")
else()
  set(suffix "")
endif()
foreach(path IN ITEMS
    "bin/fsim${suffix}" "bin/fsim-sv${suffix}" "bin/fsim-vhdl${suffix}"
    "include/fsim/api.h" "include/fsim/version.hpp"
    "lib/pkgconfig/fsim.pc" "share/doc/fsim/LICENSE")
  if(NOT EXISTS "${prefix}/${path}")
    message(FATAL_ERROR "v3 installed archive omits ${path}")
  endif()
endforeach()
execute_process(COMMAND "${prefix}/bin/fsim${suffix}" --version
  RESULT_VARIABLE version_result OUTPUT_VARIABLE version ERROR_VARIABLE version_error)
string(STRIP "${version}" version)
if(NOT version_result EQUAL 0 OR NOT version STREQUAL "fsim 3.0.0 (C API 1)")
  message(FATAL_ERROR "v3 installed archive version failed: ${version}${version_error}")
endif()
file(READ "${prefix}/lib/pkgconfig/fsim.pc" pc)
foreach(token IN ITEMS "Version: 3.0.0" "Libs: -L\${libdir} -lfsim_api")
  string(FIND "${pc}" "${token}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "v3 installed archive pkg-config metadata misses ${token}")
  endif()
endforeach()
file(REMOVE_RECURSE "${prefix}")
if(EXISTS "${prefix}")
  message(FATAL_ERROR "v3 installed archive removal check failed")
endif()
message(STATUS
  "v3 installed archive passed: ${FSIM_ARCHIVE_AUDIT_TOOLCHAIN}, ${entry_count} entries, ${FSIM_ARCHIVE_AUDIT_EXPECTED_TESTS} tests")

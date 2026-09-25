# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS FSIM_SOURCE_DIR FSIM_ARCHIVE_AUDIT_BINARY_DIR
    FSIM_ARCHIVE_AUDIT_ARCHIVE FSIM_ARCHIVE_AUDIT_ROOT
    FSIM_ARCHIVE_AUDIT_REGRESSION_LOG FSIM_ARCHIVE_AUDIT_WORK_DIR
    FSIM_ARCHIVE_AUDIT_REQUIRED_CTESTS FSIM_ARCHIVE_AUDIT_REQUIRED_ENTRIES
    FSIM_ARCHIVE_AUDIT_TOOLCHAIN)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
if(NOT FSIM_ARCHIVE_AUDIT_ROOT MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]*$")
  message(FATAL_ERROR "v3 installed-archive root is unsafe")
endif()
get_filename_component(FSIM_BINARY_ROOT
  "${FSIM_ARCHIVE_AUDIT_BINARY_DIR}" REALPATH)
get_filename_component(FSIM_WORK_ROOT
  "${FSIM_ARCHIVE_AUDIT_WORK_DIR}" ABSOLUTE)
string(FIND "${FSIM_WORK_ROOT}/" "${FSIM_BINARY_ROOT}/"
  FSIM_WORK_PREFIX)
if(NOT FSIM_WORK_PREFIX EQUAL 0 OR FSIM_WORK_ROOT STREQUAL FSIM_BINARY_ROOT)
  message(FATAL_ERROR "v3 installed-archive work directory must be inside the build")
endif()
if(EXISTS "${FSIM_WORK_ROOT}")
  get_filename_component(FSIM_EXISTING_WORK_ROOT "${FSIM_WORK_ROOT}" REALPATH)
  string(FIND "${FSIM_EXISTING_WORK_ROOT}/" "${FSIM_BINARY_ROOT}/"
    FSIM_EXISTING_WORK_PREFIX)
  if(NOT FSIM_EXISTING_WORK_PREFIX EQUAL 0)
    message(FATAL_ERROR
      "v3 installed-archive work directory resolves outside the build")
  endif()
endif()
if(NOT EXISTS "${FSIM_ARCHIVE_AUDIT_ARCHIVE}"
    OR NOT EXISTS "${FSIM_ARCHIVE_AUDIT_REGRESSION_LOG}"
    OR NOT EXISTS "${FSIM_ARCHIVE_AUDIT_REQUIRED_CTESTS}"
    OR NOT EXISTS "${FSIM_ARCHIVE_AUDIT_REQUIRED_ENTRIES}")
  message(FATAL_ERROR "v3 installed-archive evidence is missing")
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}"
    --test-dir "${FSIM_ARCHIVE_AUDIT_BINARY_DIR}"
    --show-only=json-v1
  RESULT_VARIABLE FSIM_CTEST_RESULT
  OUTPUT_VARIABLE FSIM_CTEST_JSON
  ERROR_VARIABLE FSIM_CTEST_ERROR)
if(NOT FSIM_CTEST_RESULT EQUAL 0)
  message(FATAL_ERROR
    "cannot enumerate v3 installed-archive CTests: ${FSIM_CTEST_ERROR}")
endif()
string(JSON FSIM_REGISTERED_TEST_COUNT LENGTH "${FSIM_CTEST_JSON}" tests)
if(FSIM_REGISTERED_TEST_COUNT LESS 1)
  message(FATAL_ERROR "v3 installed-archive CTest set is empty")
endif()
file(MAKE_DIRECTORY "${FSIM_WORK_ROOT}")
set(FSIM_REGISTERED_TESTS_FILE "${FSIM_WORK_ROOT}/registered-ctests.txt")
file(WRITE "${FSIM_REGISTERED_TESTS_FILE}" "")
math(EXPR FSIM_LAST_TEST "${FSIM_REGISTERED_TEST_COUNT} - 1")
foreach(FSIM_INDEX RANGE 0 ${FSIM_LAST_TEST})
  string(JSON FSIM_TEST_NAME GET "${FSIM_CTEST_JSON}"
    tests ${FSIM_INDEX} name)
  file(APPEND "${FSIM_REGISTERED_TESTS_FILE}" "${FSIM_TEST_NAME}\n")
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DFSIM_REQUIRED_CTESTS_FILE=${FSIM_ARCHIVE_AUDIT_REQUIRED_CTESTS}"
    "-DFSIM_REGISTERED_CTESTS_FILE=${FSIM_REGISTERED_TESTS_FILE}"
    -P "${FSIM_SOURCE_DIR}/cmake/CheckRequiredCTestSet.cmake"
  RESULT_VARIABLE FSIM_REQUIRED_TEST_RESULT
  OUTPUT_VARIABLE FSIM_REQUIRED_TEST_OUTPUT
  ERROR_VARIABLE FSIM_REQUIRED_TEST_ERROR)
if(NOT FSIM_REQUIRED_TEST_RESULT EQUAL 0)
  message(FATAL_ERROR
    "v3 installed archive lost required CTests: "
    "${FSIM_REQUIRED_TEST_OUTPUT}${FSIM_REQUIRED_TEST_ERROR}")
endif()

file(READ "${FSIM_ARCHIVE_AUDIT_REGRESSION_LOG}" regression)
string(REGEX MATCHALL "100% tests passed, 0 tests failed out of [0-9]+"
  FSIM_PASS_SUMMARIES "${regression}")
list(LENGTH FSIM_PASS_SUMMARIES FSIM_PASS_COUNT)
if(NOT FSIM_PASS_COUNT EQUAL 1)
  message(FATAL_ERROR
    "v3 installed-archive regression log must contain one green CTest run")
endif()
list(GET FSIM_PASS_SUMMARIES 0 FSIM_PASS_SUMMARY)
if(NOT FSIM_PASS_SUMMARY STREQUAL
    "100% tests passed, 0 tests failed out of ${FSIM_REGISTERED_TEST_COUNT}")
  message(FATAL_ERROR
    "v3 installed-archive regression count differs from registered CTests")
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

if(DEFINED FSIM_ARCHIVE_AUDIT_EXECUTABLE_SUFFIX)
  set(suffix "${FSIM_ARCHIVE_AUDIT_EXECUTABLE_SUFFIX}")
else()
  set(suffix "")
endif()
file(STRINGS "${FSIM_ARCHIVE_AUDIT_REQUIRED_ENTRIES}"
  FSIM_REQUIRED_ENTRY_LINES)
set(FSIM_REQUIRED_PATHS)
foreach(path IN LISTS FSIM_REQUIRED_ENTRY_LINES)
  if(path STREQUAL "" OR path STREQUAL
      "# SPDX-License-Identifier: Apache-2.0")
    continue()
  endif()
  string(REPLACE "@EXE@" "${suffix}" path "${path}")
  if(IS_ABSOLUTE "${path}" OR path MATCHES "(^|/)\\.\\.?(/|$)"
      OR path MATCHES "[/\\\\]$" OR path MATCHES "\\\\"
      OR path MATCHES "@" OR path IN_LIST FSIM_REQUIRED_PATHS)
    message(FATAL_ERROR "unsafe or duplicate required archive path: ${path}")
  endif()
  if(NOT "${FSIM_ARCHIVE_AUDIT_ROOT}/${path}" IN_LIST seen)
    message(FATAL_ERROR "v3 installed archive omits required entry: ${path}")
  endif()
  list(APPEND FSIM_REQUIRED_PATHS "${path}")
endforeach()
if(NOT FSIM_REQUIRED_PATHS)
  message(FATAL_ERROR "v3 installed archive has no required entry set")
endif()
foreach(path IN ITEMS
    share/doc/fsim/third-party/sqlite-3.53.4/LICENSE
    share/doc/fsim/third-party/sqlite-3.53.4/NOTICE
    share/doc/fsim/third-party/sqlite-3.53.4/SOURCE_MANIFEST.txt
    share/doc/fsim/third-party/sqlite-3.53.4/sqlite-3.53.4.spdx.json)
  if(NOT "${FSIM_ARCHIVE_AUDIT_ROOT}/${path}" IN_LIST seen)
    message(FATAL_ERROR "v3 installed archive omits SQLite provenance: ${path}")
  endif()
  list(APPEND FSIM_REQUIRED_PATHS "${path}")
endforeach()

file(REMOVE_RECURSE "${FSIM_ARCHIVE_AUDIT_WORK_DIR}")
file(MAKE_DIRECTORY "${FSIM_ARCHIVE_AUDIT_WORK_DIR}/extract")
file(ARCHIVE_EXTRACT INPUT "${FSIM_ARCHIVE_AUDIT_ARCHIVE}"
  DESTINATION "${FSIM_ARCHIVE_AUDIT_WORK_DIR}/extract")
set(prefix
  "${FSIM_ARCHIVE_AUDIT_WORK_DIR}/extract/${FSIM_ARCHIVE_AUDIT_ROOT}")
foreach(path IN LISTS FSIM_REQUIRED_PATHS)
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

set(workspace "${FSIM_ARCHIVE_AUDIT_WORK_DIR}/workspace")
file(MAKE_DIRECTORY "${workspace}")
file(WRITE "${workspace}/top.sv"
  "module archive_top; initial begin $display(\"WORKSPACE_ARCHIVE_PASS\"); $finish; end endmodule\n")
function(fsim_installed_workspace_command expected_output)
  execute_process(COMMAND "${prefix}/bin/fsim${suffix}" ${ARGN}
    WORKING_DIRECTORY "${workspace}"
    TIMEOUT 7200
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "installed workspace command failed (${ARGN}): ${output}${error}")
  endif()
  if(NOT expected_output STREQUAL "")
    string(FIND "${output}" "${expected_output}" offset)
    if(offset EQUAL -1)
      message(FATAL_ERROR
        "installed workspace command lost ${expected_output}: ${output}${error}")
    endif()
  endif()
endfunction()
fsim_installed_workspace_command("" compile --library work top.sv)
if(NOT EXISTS "${workspace}/.fsim/libraries/work/library.sqlite3")
  message(FATAL_ERROR "installed compilation did not publish a workspace catalog")
endif()
fsim_installed_workspace_command("archive_top" library objects work)
file(REMOVE "${workspace}/top.sv")
fsim_installed_workspace_command("" elaborate work.archive_top)
fsim_installed_workspace_command("" elaborate work.archive_top --snapshot retained)
if(NOT EXISTS "${workspace}/.fsim/snapshots/default"
    OR NOT EXISTS "${workspace}/.fsim/snapshots/retained")
  message(FATAL_ERROR "installed elaboration did not publish both snapshots")
endif()
fsim_installed_workspace_command("" library delete work)
fsim_installed_workspace_command("WORKSPACE_ARCHIVE_PASS" simulate --engine interpreter)
fsim_installed_workspace_command("WORKSPACE_ARCHIVE_PASS"
  simulate --snapshot retained --engine interpreter)
file(REMOVE_RECURSE "${workspace}")
file(REMOVE_RECURSE "${prefix}")
if(EXISTS "${prefix}")
  message(FATAL_ERROR "v3 installed archive removal check failed")
endif()
message(STATUS
  "v3 installed archive passed: ${FSIM_ARCHIVE_AUDIT_TOOLCHAIN}, "
  "${entry_count} entries, ${FSIM_REGISTERED_TEST_COUNT} tests")

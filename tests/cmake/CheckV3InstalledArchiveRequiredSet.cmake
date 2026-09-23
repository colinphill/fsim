# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(FSIM_REQUIRED IN ITEMS FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_FIXTURE_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

set(FSIM_ROOT_NAME fsim-v3-required-set-fixture)
set(FSIM_ARCHIVE_ROOT "${FSIM_FIXTURE_DIR}/archive/${FSIM_ROOT_NAME}")
foreach(FSIM_DIRECTORY IN ITEMS bin include/fsim lib/pkgconfig share/doc/fsim extra)
  file(MAKE_DIRECTORY "${FSIM_ARCHIVE_ROOT}/${FSIM_DIRECTORY}")
endforeach()
foreach(FSIM_EXECUTABLE IN ITEMS fsim fsim-sv fsim-vhdl)
  set(FSIM_PATH "${FSIM_ARCHIVE_ROOT}/bin/${FSIM_EXECUTABLE}")
  file(WRITE "${FSIM_PATH}"
    "#!/bin/sh\nprintf '%s\\n' 'fsim 3.0.0 (C API 1)'\n")
  file(CHMOD "${FSIM_PATH}" PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
endforeach()
file(WRITE "${FSIM_ARCHIVE_ROOT}/include/fsim/api.h" "fixture\n")
file(WRITE "${FSIM_ARCHIVE_ROOT}/include/fsim/version.hpp" "fixture\n")
file(WRITE "${FSIM_ARCHIVE_ROOT}/lib/pkgconfig/fsim.pc"
  "Version: 3.0.0\nLibs: -L\${libdir} -lfsim_api\n")
file(WRITE "${FSIM_ARCHIVE_ROOT}/share/doc/fsim/LICENSE" "fixture\n")
file(WRITE "${FSIM_ARCHIVE_ROOT}/extra/additive.txt" "allowed\n")
set(FSIM_ARCHIVE "${FSIM_FIXTURE_DIR}/fixture.zip")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar cf "${FSIM_ARCHIVE}"
    --format=zip "${FSIM_ROOT_NAME}"
  WORKING_DIRECTORY "${FSIM_FIXTURE_DIR}/archive"
  RESULT_VARIABLE FSIM_ARCHIVE_STATUS
  ERROR_VARIABLE FSIM_ARCHIVE_ERROR)
if(NOT FSIM_ARCHIVE_STATUS EQUAL 0)
  message(FATAL_ERROR "cannot create archive fixture: ${FSIM_ARCHIVE_ERROR}")
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${FSIM_BINARY_DIR}"
    --show-only=json-v1
  RESULT_VARIABLE FSIM_CTEST_STATUS
  OUTPUT_VARIABLE FSIM_CTEST_JSON)
if(NOT FSIM_CTEST_STATUS EQUAL 0)
  message(FATAL_ERROR "cannot enumerate fixture CTests")
endif()
string(JSON FSIM_TEST_COUNT LENGTH "${FSIM_CTEST_JSON}" tests)
set(FSIM_REGRESSION_LOG "${FSIM_FIXTURE_DIR}/regression.log")
file(WRITE "${FSIM_REGRESSION_LOG}"
  "100% tests passed, 0 tests failed out of ${FSIM_TEST_COUNT}\n")

function(fsim_check_archive FSIM_EXPECTED FSIM_MARKER FSIM_TESTS FSIM_ENTRIES)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      "-DFSIM_ARCHIVE_AUDIT_BINARY_DIR=${FSIM_BINARY_DIR}"
      "-DFSIM_ARCHIVE_AUDIT_ARCHIVE=${FSIM_ARCHIVE}"
      "-DFSIM_ARCHIVE_AUDIT_ROOT=${FSIM_ROOT_NAME}"
      "-DFSIM_ARCHIVE_AUDIT_REGRESSION_LOG=${FSIM_REGRESSION_LOG}"
      "-DFSIM_ARCHIVE_AUDIT_WORK_DIR=${FSIM_BINARY_DIR}/v3-required-set-audit"
      "-DFSIM_ARCHIVE_AUDIT_REQUIRED_CTESTS=${FSIM_TESTS}"
      "-DFSIM_ARCHIVE_AUDIT_REQUIRED_ENTRIES=${FSIM_ENTRIES}"
      "-DFSIM_ARCHIVE_AUDIT_TOOLCHAIN=fixture"
      -P "${FSIM_SOURCE_DIR}/cmake/CheckV3InstalledArchive.cmake"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  string(FIND "${FSIM_OUTPUT}${FSIM_ERROR}" "${FSIM_MARKER}"
    FSIM_MARKER_OFFSET)
  if(FSIM_EXPECTED STREQUAL "pass")
    if(NOT FSIM_RESULT EQUAL 0)
      message(FATAL_ERROR
        "required archive fixture failed: ${FSIM_OUTPUT}${FSIM_ERROR}")
    endif()
  elseif(FSIM_RESULT EQUAL 0 OR FSIM_MARKER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "archive rejection fixture failed: ${FSIM_OUTPUT}${FSIM_ERROR}")
  endif()
endfunction()

set(FSIM_REQUIRED_TESTS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ctests.txt")
set(FSIM_REQUIRED_ENTRIES
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_installed_archive_required.txt")
fsim_check_archive(pass "" "${FSIM_REQUIRED_TESTS}" "${FSIM_REQUIRED_ENTRIES}")

math(EXPR FSIM_WRONG_COUNT "${FSIM_TEST_COUNT} + 1")
file(WRITE "${FSIM_REGRESSION_LOG}"
  "100% tests passed, 0 tests failed out of ${FSIM_WRONG_COUNT}\n")
fsim_check_archive(fail "regression count differs from registered CTests"
  "${FSIM_REQUIRED_TESTS}" "${FSIM_REQUIRED_ENTRIES}")
file(WRITE "${FSIM_REGRESSION_LOG}"
  "100% tests passed, 0 tests failed out of ${FSIM_TEST_COUNT}\n")

set(FSIM_MISSING_TESTS "${FSIM_FIXTURE_DIR}/missing-tests.txt")
file(WRITE "${FSIM_MISSING_TESTS}" "fsim.missing.required\n")
fsim_check_archive(fail "required CTest is not registered"
  "${FSIM_MISSING_TESTS}" "${FSIM_REQUIRED_ENTRIES}")

set(FSIM_DUPLICATE_ENTRIES "${FSIM_FIXTURE_DIR}/duplicate-entries.txt")
file(WRITE "${FSIM_DUPLICATE_ENTRIES}"
  "bin/fsim@EXE@\nbin/fsim@EXE@\n")
fsim_check_archive(fail "duplicate required archive path"
  "${FSIM_REQUIRED_TESTS}" "${FSIM_DUPLICATE_ENTRIES}")

set(FSIM_MISSING_ENTRIES "${FSIM_FIXTURE_DIR}/missing-entries.txt")
file(WRITE "${FSIM_MISSING_ENTRIES}" "bin/not-present@EXE@\n")
fsim_check_archive(fail "omits required entry"
  "${FSIM_REQUIRED_TESTS}" "${FSIM_MISSING_ENTRIES}")

set(FSIM_UNSAFE_ENTRIES "${FSIM_FIXTURE_DIR}/unsafe-entries.txt")
file(WRITE "${FSIM_UNSAFE_ENTRIES}" "../outside\n")
fsim_check_archive(fail "unsafe or duplicate required archive path"
  "${FSIM_REQUIRED_TESTS}" "${FSIM_UNSAFE_ENTRIES}")

set(FSIM_BASE_ARCHIVE "${FSIM_ARCHIVE}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar cf
    "${FSIM_FIXTURE_DIR}/duplicate.zip" --format=zip
    "${FSIM_ROOT_NAME}/bin/fsim" "${FSIM_ROOT_NAME}/bin/fsim"
  WORKING_DIRECTORY "${FSIM_FIXTURE_DIR}/archive"
  RESULT_VARIABLE FSIM_DUPLICATE_ARCHIVE_STATUS)
if(NOT FSIM_DUPLICATE_ARCHIVE_STATUS EQUAL 0)
  message(FATAL_ERROR "cannot create duplicate-entry archive fixture")
endif()
set(FSIM_ARCHIVE "${FSIM_FIXTURE_DIR}/duplicate.zip")
fsim_check_archive(fail "unsafe or duplicate v3 installed"
  "${FSIM_REQUIRED_TESTS}" "${FSIM_REQUIRED_ENTRIES}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar cf
    "${FSIM_FIXTURE_DIR}/unsafe.zip" --format=zip ../regression.log
  WORKING_DIRECTORY "${FSIM_FIXTURE_DIR}/archive"
  RESULT_VARIABLE FSIM_UNSAFE_ARCHIVE_STATUS)
if(NOT FSIM_UNSAFE_ARCHIVE_STATUS EQUAL 0)
  message(FATAL_ERROR "cannot create unsafe-entry archive fixture")
endif()
set(FSIM_ARCHIVE "${FSIM_FIXTURE_DIR}/unsafe.zip")
fsim_check_archive(fail "unsafe or duplicate v3 installed"
  "${FSIM_REQUIRED_TESTS}" "${FSIM_REQUIRED_ENTRIES}")
set(FSIM_ARCHIVE "${FSIM_BASE_ARCHIVE}")

message(STATUS "installed archive required-set fixtures passed")

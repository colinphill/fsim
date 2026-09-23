# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED FSIM_TEST_WORK_DIR OR "${FSIM_TEST_WORK_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_TEST_WORK_DIR must name an isolated writable directory")
endif()

set(FSIM_SOURCE_DIR "${FSIM_TEST_WORK_DIR}/source-root")
set(FSIM_REQUIRED_CTESTS_FILE "${FSIM_TEST_WORK_DIR}/required-ctests.txt")
set(FSIM_REGISTERED_CTESTS_FILE "${FSIM_TEST_WORK_DIR}/registered-ctests.txt")
set(FSIM_ID_LEDGER_FILE "${FSIM_TEST_WORK_DIR}/obligations.tsv")
set(FSIM_REQUIRED_IDS_FILE "${FSIM_TEST_WORK_DIR}/required-ids.txt")
set(FSIM_REQUIRED_CTEST_HELPER
  "${CMAKE_CURRENT_LIST_DIR}/../../cmake/CheckRequiredCTestSet.cmake")
set(FSIM_REQUIRED_ID_HELPER
  "${CMAKE_CURRENT_LIST_DIR}/../../cmake/CheckRequiredIdSet.cmake")

file(MAKE_DIRECTORY
  "${FSIM_SOURCE_DIR}/packaging"
  "${FSIM_SOURCE_DIR}/cmake"
  "${FSIM_SOURCE_DIR}/links")
file(WRITE "${FSIM_SOURCE_DIR}/packaging/LICENSE.txt"
  "license fixture\n")
file(WRITE "${FSIM_SOURCE_DIR}/cmake/Owner.cmake"
  "# named owner fixture\n")

function(fsim_expect_helper_result
    FSIM_CASE_NAME FSIM_HELPER FSIM_EXPECT_SUCCESS FSIM_EXPECTED_TEXT)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      "-DFSIM_REQUIRED_CTESTS_FILE=${FSIM_REQUIRED_CTESTS_FILE}"
      "-DFSIM_REGISTERED_CTESTS_FILE=${FSIM_REGISTERED_CTESTS_FILE}"
      "-DFSIM_ID_LEDGER_FILE=${FSIM_ID_LEDGER_FILE}"
      "-DFSIM_REQUIRED_IDS_FILE=${FSIM_REQUIRED_IDS_FILE}"
      -P "${FSIM_HELPER}"
    RESULT_VARIABLE FSIM_HELPER_RESULT
    OUTPUT_VARIABLE FSIM_HELPER_STDOUT
    ERROR_VARIABLE FSIM_HELPER_STDERR)
  set(FSIM_HELPER_OUTPUT
    "${FSIM_HELPER_STDOUT}\n${FSIM_HELPER_STDERR}")

  if(FSIM_EXPECT_SUCCESS)
    if(NOT FSIM_HELPER_RESULT EQUAL 0)
      message(FATAL_ERROR
        "${FSIM_CASE_NAME} should pass but returned ${FSIM_HELPER_RESULT}:\n${FSIM_HELPER_OUTPUT}")
    endif()
  else()
    if(FSIM_HELPER_RESULT EQUAL 0)
      message(FATAL_ERROR
        "${FSIM_CASE_NAME} should fail but passed:\n${FSIM_HELPER_OUTPUT}")
    endif()
    string(FIND "${FSIM_HELPER_OUTPUT}" "${FSIM_EXPECTED_TEXT}"
      FSIM_EXPECTED_TEXT_POSITION)
    if(FSIM_EXPECTED_TEXT_POSITION LESS 0)
      message(
        FATAL_ERROR
        "${FSIM_CASE_NAME} failed for the wrong reason; expected "
        "'${FSIM_EXPECTED_TEXT}':\n${FSIM_HELPER_OUTPUT}")
    endif()
  endif()
  message(STATUS "PASS ${FSIM_CASE_NAME}")
endfunction()

function(fsim_write_base_registered_tests)
  file(WRITE "${FSIM_REGISTERED_CTESTS_FILE}"
    "fsim.license\nfsim.package\n")
endfunction()

function(fsim_write_base_id_ledger)
  file(WRITE "${FSIM_ID_LEDGER_FILE}"
    "id\tpath\towner\n"
    "LIC-001\tpackaging/LICENSE.txt\tfsim.license\n"
    "PKG-001\tcmake/Owner.cmake\tfsim.package\n")
  file(WRITE "${FSIM_REQUIRED_IDS_FILE}" "LIC-001\n")
endfunction()

# An additive registered CTest is legal; line endings normalize from CRLF.
file(WRITE "${FSIM_REQUIRED_CTESTS_FILE}" "fsim.license\n")
file(WRITE "${FSIM_REGISTERED_CTESTS_FILE}"
  "fsim.license\r\nfsim.package\r\n")
fsim_expect_helper_result(
  "required CTest set allows additive registrations and CRLF"
  "${FSIM_REQUIRED_CTEST_HELPER}" TRUE "")

# Missing and duplicate test names fail on their exact set boundaries.
file(WRITE "${FSIM_REQUIRED_CTESTS_FILE}" "fsim.missing\n")
fsim_write_base_registered_tests()
fsim_expect_helper_result(
  "missing required CTest is rejected"
  "${FSIM_REQUIRED_CTEST_HELPER}" FALSE "required CTest is not registered")

file(WRITE "${FSIM_REQUIRED_CTESTS_FILE}"
  "fsim.license\nfsim.license\n")
fsim_expect_helper_result(
  "duplicate required CTest is rejected"
  "${FSIM_REQUIRED_CTEST_HELPER}" FALSE "duplicate required CTest name")

file(WRITE "${FSIM_REQUIRED_CTESTS_FILE}" "fsim.license\n")
file(WRITE "${FSIM_REGISTERED_CTESTS_FILE}"
  "fsim.license\nfsim.license\n")
fsim_expect_helper_result(
  "duplicate registered CTest is rejected"
  "${FSIM_REQUIRED_CTEST_HELPER}" FALSE "duplicate registered CTest name")

# Valid additional ledger rows are allowed, with existing paths and owners.
fsim_write_base_registered_tests()
fsim_write_base_id_ledger()
fsim_expect_helper_result(
  "required ID set allows additive valid rows"
  "${FSIM_REQUIRED_ID_HELPER}" TRUE "")

file(WRITE "${FSIM_REQUIRED_IDS_FILE}" "LIC-001\nMISSING-001\n")
fsim_expect_helper_result(
  "missing required ID is rejected"
  "${FSIM_REQUIRED_ID_HELPER}" FALSE "required ID is missing from ledger")

fsim_write_base_id_ledger()
file(WRITE "${FSIM_REQUIRED_IDS_FILE}" "LIC-001\nLIC-001\n")
fsim_expect_helper_result(
  "duplicate required ID is rejected"
  "${FSIM_REQUIRED_ID_HELPER}" FALSE "duplicate required ID")

file(WRITE "${FSIM_ID_LEDGER_FILE}"
  "id\tpath\towner\n"
  "LIC-001\tpackaging/LICENSE.txt\tfsim.license\n"
  "LIC-001\tcmake/Owner.cmake\tfsim.package\n")
file(WRITE "${FSIM_REQUIRED_IDS_FILE}" "LIC-001\n")
fsim_expect_helper_result(
  "duplicate ledger ID is rejected"
  "${FSIM_REQUIRED_ID_HELPER}" FALSE "duplicate ledger ID")

file(WRITE "${FSIM_ID_LEDGER_FILE}"
  "id\tpath\towner\n"
  "BAD/ID\tpackaging/LICENSE.txt\tfsim.license\n")
file(WRITE "${FSIM_REQUIRED_IDS_FILE}" "BAD/ID\n")
fsim_expect_helper_result(
  "malformed ledger ID is rejected"
  "${FSIM_REQUIRED_ID_HELPER}" FALSE "malformed ledger ID")

file(WRITE "${FSIM_ID_LEDGER_FILE}"
  "id\tpath\towner\n"
  "LIC-001\t../outside.txt\tfsim.license\n")
file(WRITE "${FSIM_REQUIRED_IDS_FILE}" "LIC-001\n")
fsim_expect_helper_result(
  "unsafe relative path is rejected"
  "${FSIM_REQUIRED_ID_HELPER}" FALSE "unsafe path for ledger ID")

file(WRITE "${FSIM_ID_LEDGER_FILE}"
  "id\tpath\towner\n"
  "LIC-001\tpackaging/missing.txt\tfsim.license\n")
fsim_expect_helper_result(
  "missing ledger path is rejected"
  "${FSIM_REQUIRED_ID_HELPER}" FALSE "does not name an existing regular file")

file(WRITE "${FSIM_ID_LEDGER_FILE}"
  "id\tpath\towner\n"
  "LIC-001\tpackaging/LICENSE.txt\t\n")
fsim_expect_helper_result(
  "missing owner is rejected"
  "${FSIM_REQUIRED_ID_HELPER}" FALSE "missing named owner for ledger ID")

file(WRITE "${FSIM_ID_LEDGER_FILE}"
  "id\tpath\towner\n"
  "LIC-001\tpackaging/LICENSE.txt\tfsim.unregistered\n")
fsim_expect_helper_result(
  "unregistered named owner is rejected"
  "${FSIM_REQUIRED_ID_HELPER}" FALSE "missing named registered owner")

file(MAKE_DIRECTORY "${FSIM_SOURCE_DIR}/outside")
file(WRITE "${FSIM_TEST_WORK_DIR}/outside.txt" "outside source root\n")
set(FSIM_ESCAPE_SYMLINK "${FSIM_SOURCE_DIR}/links/escape.txt")
if(WIN32)
  message(
    STATUS
    "SKIP symlink escape fixture: the Windows test environment may not grant "
    "unprivileged symbolic-link creation")
else()
  if(IS_SYMLINK "${FSIM_ESCAPE_SYMLINK}")
    file(REAL_PATH "${FSIM_ESCAPE_SYMLINK}" FSIM_ACTUAL_SYMLINK_TARGET)
    file(REAL_PATH "${FSIM_TEST_WORK_DIR}/outside.txt"
      FSIM_EXPECTED_SYMLINK_TARGET)
    if(NOT "${FSIM_ACTUAL_SYMLINK_TARGET}"
       STREQUAL "${FSIM_EXPECTED_SYMLINK_TARGET}")
      message(FATAL_ERROR
        "symlink escape fixture has an unexpected target: ${FSIM_ACTUAL_SYMLINK_TARGET}")
    endif()
  else()
    file(CREATE_LINK "${FSIM_TEST_WORK_DIR}/outside.txt"
      "${FSIM_ESCAPE_SYMLINK}" SYMBOLIC RESULT FSIM_SYMLINK_RESULT)
    if(NOT FSIM_SYMLINK_RESULT STREQUAL "0")
      message(FATAL_ERROR
        "cannot create required symlink escape fixture: ${FSIM_SYMLINK_RESULT}")
    endif()
  endif()
  file(WRITE "${FSIM_ID_LEDGER_FILE}"
    "id\tpath\towner\n"
    "LIC-001\tlinks/escape.txt\tfsim.license\n")
  fsim_expect_helper_result(
    "symlink path escaping source root is rejected"
    "${FSIM_REQUIRED_ID_HELPER}" FALSE "escapes FSIM_SOURCE_DIR")
endif()

message(STATUS "required-set helper self-tests passed")

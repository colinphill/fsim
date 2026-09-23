# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(FSIM_REQUIRED_VARIABLE IN ITEMS
    FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_CTEST_COMMAND)
  if(NOT DEFINED ${FSIM_REQUIRED_VARIABLE}
     OR "${${FSIM_REQUIRED_VARIABLE}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED_VARIABLE} is required")
  endif()
endforeach()

execute_process(
  COMMAND "${FSIM_CTEST_COMMAND}" --test-dir "${FSIM_BINARY_DIR}"
          --show-only=json-v1
  RESULT_VARIABLE FSIM_CTEST_STATUS
  OUTPUT_VARIABLE FSIM_CTEST_JSON
  ERROR_VARIABLE FSIM_CTEST_ERROR)
if(NOT FSIM_CTEST_STATUS EQUAL 0)
  message(FATAL_ERROR
    "cannot enumerate registered CTests: ${FSIM_CTEST_ERROR}")
endif()

string(JSON FSIM_CTEST_COUNT ERROR_VARIABLE FSIM_JSON_ERROR
       LENGTH "${FSIM_CTEST_JSON}" tests)
if(FSIM_JSON_ERROR OR FSIM_CTEST_COUNT LESS 1)
  message(FATAL_ERROR "CTest returned no usable test inventory: ${FSIM_JSON_ERROR}")
endif()

set(FSIM_REGISTERED_NAMES "")
math(EXPR FSIM_LAST_CTEST "${FSIM_CTEST_COUNT} - 1")
foreach(FSIM_CTEST_INDEX RANGE 0 ${FSIM_LAST_CTEST})
  string(JSON FSIM_CTEST_NAME ERROR_VARIABLE FSIM_JSON_ERROR
         GET "${FSIM_CTEST_JSON}" tests ${FSIM_CTEST_INDEX} name)
  if(FSIM_JSON_ERROR OR FSIM_CTEST_NAME MATCHES "[\r\n]")
    message(FATAL_ERROR
      "CTest has an invalid test name at index ${FSIM_CTEST_INDEX}")
  endif()
  string(APPEND FSIM_REGISTERED_NAMES "${FSIM_CTEST_NAME}\n")
endforeach()

set(FSIM_LEDGER_DIR "${FSIM_BINARY_DIR}/v3-current-obligations")
file(MAKE_DIRECTORY "${FSIM_LEDGER_DIR}")
set(FSIM_REGISTERED_CTESTS_FILE
    "${FSIM_LEDGER_DIR}/registered-ctests.txt")
file(WRITE "${FSIM_REGISTERED_CTESTS_FILE}" "${FSIM_REGISTERED_NAMES}")

set(FSIM_REQUIRED_CTESTS_FILE
    "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ctests.txt")
include("${FSIM_SOURCE_DIR}/cmake/CheckRequiredCTestSet.cmake")

set(FSIM_ID_LEDGER_FILE
    "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_obligations.tsv")
set(FSIM_REQUIRED_IDS_FILE
    "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ids.txt")
include("${FSIM_SOURCE_DIR}/cmake/CheckRequiredIdSet.cmake")

message(STATUS
  "V3 current obligations: ${FSIM_CTEST_COUNT} registered tests checked")

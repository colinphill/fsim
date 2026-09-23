# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(FSIM_VARIABLE IN ITEMS FSIM_SOURCE_DIR FSIM_BINARY_DIR
    FSIM_CTEST_COMMAND)
  if(NOT DEFINED ${FSIM_VARIABLE} OR "${${FSIM_VARIABLE}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_VARIABLE} is required")
  endif()
endforeach()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/current_resource_domains.tsv")
file(READ "${FSIM_LEDGER}" FSIM_CONTENTS)
file(STRINGS "${FSIM_LEDGER}" FSIM_ROWS REGEX "^RESCOV-001\t")
list(LENGTH FSIM_ROWS FSIM_MATCH_COUNT)
if(NOT FSIM_MATCH_COUNT EQUAL 1)
  message(FATAL_ERROR "resource-domain fixture has no unique coverage row")
endif()
list(GET FSIM_ROWS 0 FSIM_OWNER_ROW)

set(FSIM_FIXTURE_DIR "${FSIM_BINARY_DIR}/resource-domain-fixtures")
file(MAKE_DIRECTORY "${FSIM_FIXTURE_DIR}")

function(fsim_check_resource_fixture FSIM_NAME FSIM_TEXT FSIM_EXPECT_PASS)
  set(FSIM_PATH "${FSIM_FIXTURE_DIR}/${FSIM_NAME}.tsv")
  file(WRITE "${FSIM_PATH}" "${FSIM_TEXT}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      "-DFSIM_BINARY_DIR=${FSIM_BINARY_DIR}"
      "-DFSIM_CTEST_COMMAND=${FSIM_CTEST_COMMAND}"
      "-DFSIM_RESOURCE_DOMAIN=coverage"
      "-DFSIM_RESOURCE_LEDGER=${FSIM_PATH}"
      -P "${FSIM_SOURCE_DIR}/cmake/CheckCurrentResourceDomain.cmake"
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  if(FSIM_EXPECT_PASS AND NOT FSIM_STATUS EQUAL 0)
    message(FATAL_ERROR "positive resource fixture failed: ${FSIM_ERROR}")
  elseif(NOT FSIM_EXPECT_PASS AND FSIM_STATUS EQUAL 0)
    message(FATAL_ERROR "negative resource fixture passed: ${FSIM_NAME}")
  endif()
endfunction()

fsim_check_resource_fixture(additive
  "${FSIM_CONTENTS}\nRESCOV-999\tcoverage\ttests/feature_matrix/current_resource_domains.tsv\tfsim.contract.coverage-resources\n"
  TRUE)
string(REPLACE "${FSIM_OWNER_ROW}\n" "" FSIM_MISSING
  "${FSIM_CONTENTS}")
fsim_check_resource_fixture(missing "${FSIM_MISSING}" FALSE)
fsim_check_resource_fixture(duplicate
  "${FSIM_CONTENTS}${FSIM_OWNER_ROW}\n" FALSE)
string(REPLACE "cmake/CheckCodeCoverageInventory.cmake" "../outside"
  FSIM_UNSAFE "${FSIM_CONTENTS}")
fsim_check_resource_fixture(unsafe "${FSIM_UNSAFE}" FALSE)
string(REPLACE "fsim.code-coverage-inventory" "fsim.unregistered"
  FSIM_OWNERLESS "${FSIM_CONTENTS}")
fsim_check_resource_fixture(ownerless "${FSIM_OWNERLESS}" FALSE)
fsim_check_resource_fixture(wrong-domain
  "${FSIM_CONTENTS}\nRESCOV-999\tlanguage\ttests/feature_matrix/current_resource_domains.tsv\tfsim.contract.language-resources\n"
  FALSE)

message(STATUS "resource-domain validator fixtures passed")

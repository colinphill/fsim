# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(FSIM_VARIABLE IN ITEMS FSIM_SOURCE_DIR FSIM_BINARY_DIR
    FSIM_CTEST_COMMAND)
  if(NOT DEFINED ${FSIM_VARIABLE} OR "${${FSIM_VARIABLE}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_VARIABLE} is required")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CurrentEvidenceOwners.cmake")
fsim_current_registered_ctests(FSIM_REGISTERED_TESTS)

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/current_build_resources_required.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_ROWS)
set(FSIM_IDS)
foreach(FSIM_ROW IN LISTS FSIM_ROWS)
  if(FSIM_ROW MATCHES "^#" OR FSIM_ROW MATCHES "^id\t"
     OR FSIM_ROW STREQUAL "")
    continue()
  endif()
  if(FSIM_ROW MATCHES ";")
    message(FATAL_ERROR "invalid build-resource row")
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "build-resource row has ${FSIM_FIELD_COUNT} fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_PATH)
  list(GET FSIM_FIELDS 2 FSIM_OWNER)
  if(NOT FSIM_ID MATCHES "^RESBUILD-[A-Z0-9-]+$"
     OR FSIM_ID IN_LIST FSIM_IDS
     OR NOT FSIM_OWNER IN_LIST FSIM_REGISTERED_TESTS)
    message(FATAL_ERROR "invalid build-resource ID/owner: ${FSIM_ID}")
  endif()
  fsim_current_evidence_file("${FSIM_PATH}")
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()

file(STRINGS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ids.txt"
  FSIM_REQUIRED_IDS)
foreach(FSIM_ID IN LISTS FSIM_REQUIRED_IDS)
  if(FSIM_ID MATCHES "^RESBUILD-[A-Z0-9-]+$"
     AND NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "required build-resource ID is missing: ${FSIM_ID}")
  endif()
endforeach()

set(FSIM_CACHE "${FSIM_BINARY_DIR}/CMakeCache.txt")
file(STRINGS "${FSIM_CACHE}" FSIM_LINK_POOL_ROWS
  REGEX "^FSIM_LINK_POOL_SIZE:STRING=")
list(LENGTH FSIM_LINK_POOL_ROWS FSIM_LINK_POOL_ROW_COUNT)
if(NOT FSIM_LINK_POOL_ROW_COUNT EQUAL 1)
  message(FATAL_ERROR "configured link pool is missing or ambiguous")
endif()
list(GET FSIM_LINK_POOL_ROWS 0 FSIM_LINK_POOL_ROW)
string(REGEX REPLACE "^[^=]*=" "" FSIM_LINK_POOL "${FSIM_LINK_POOL_ROW}")
if(NOT FSIM_LINK_POOL MATCHES "^[1-9][0-9]*$"
   OR FSIM_LINK_POOL GREATER 8)
  message(FATAL_ERROR "configured link pool exceeds eight jobs")
endif()
file(STRINGS "${FSIM_CACHE}" FSIM_COMPACT_ROWS
  REGEX "^FSIM_COMPACT_DEBUG_BUILD:BOOL=")
if(NOT FSIM_COMPACT_ROWS STREQUAL
    "FSIM_COMPACT_DEBUG_BUILD:BOOL=ON")
  message(FATAL_ERROR "Debug object footprint policy is disabled")
endif()

include("${FSIM_SOURCE_DIR}/cmake/FsimDependencies.cmake")
string(LENGTH "${FSIM_BOOST_SOURCE_SHA256}" FSIM_BOOST_HASH_LENGTH)
if(NOT FSIM_BOOST_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$"
   OR NOT FSIM_BOOST_HASH_LENGTH EQUAL 64
   OR NOT FSIM_BOOST_SOURCE_SHA256 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Boost.PFR fallback is not version/hash pinned")
endif()
file(STRINGS "${FSIM_CACHE}" FSIM_BOOST_INCLUDE_ROWS
  REGEX "^FSIM_BOOST_PFR_INCLUDE_DIR:PATH=")
list(LENGTH FSIM_BOOST_INCLUDE_ROWS FSIM_BOOST_INCLUDE_ROW_COUNT)
if(NOT FSIM_BOOST_INCLUDE_ROW_COUNT EQUAL 1)
  message(FATAL_ERROR "configured Boost.PFR include directory is missing")
endif()
list(GET FSIM_BOOST_INCLUDE_ROWS 0 FSIM_BOOST_INCLUDE_ROW)
string(REGEX REPLACE "^[^=]*=" "" FSIM_BOOST_INCLUDE_DIR
  "${FSIM_BOOST_INCLUDE_ROW}")
if(NOT EXISTS "${FSIM_BOOST_INCLUDE_DIR}/boost/pfr/core.hpp")
  message(FATAL_ERROR "configured Boost.PFR header is unavailable")
endif()

execute_process(
  COMMAND "${FSIM_CTEST_COMMAND}" --test-dir "${FSIM_BINARY_DIR}"
    --show-only=json-v1
  RESULT_VARIABLE FSIM_STATUS OUTPUT_VARIABLE FSIM_JSON
  ERROR_VARIABLE FSIM_ERROR)
if(NOT FSIM_STATUS EQUAL 0)
  message(FATAL_ERROR "cannot inspect generated test bounds: ${FSIM_ERROR}")
endif()
string(JSON FSIM_TEST_COUNT LENGTH "${FSIM_JSON}" tests)
math(EXPR FSIM_LAST_TEST "${FSIM_TEST_COUNT} - 1")
set(FSIM_BOUNDED_TESTS 0)
foreach(FSIM_INDEX RANGE 0 ${FSIM_LAST_TEST})
  string(JSON FSIM_TEST_NAME GET "${FSIM_JSON}" tests ${FSIM_INDEX} name)
  string(JSON FSIM_PROPERTY_COUNT LENGTH
    "${FSIM_JSON}" tests ${FSIM_INDEX} properties)
  if(FSIM_PROPERTY_COUNT LESS 1)
    continue()
  endif()
  math(EXPR FSIM_LAST_PROPERTY "${FSIM_PROPERTY_COUNT} - 1")
  foreach(FSIM_PROPERTY_INDEX RANGE 0 ${FSIM_LAST_PROPERTY})
    string(JSON FSIM_PROPERTY_NAME GET "${FSIM_JSON}" tests ${FSIM_INDEX}
      properties ${FSIM_PROPERTY_INDEX} name)
    if(NOT FSIM_PROPERTY_NAME STREQUAL "TIMEOUT")
      continue()
    endif()
    string(JSON FSIM_TIMEOUT GET "${FSIM_JSON}" tests ${FSIM_INDEX}
      properties ${FSIM_PROPERTY_INDEX} value)
    if(NOT FSIM_TIMEOUT MATCHES "^[0-9]+([.][0-9]+)?$"
       OR FSIM_TIMEOUT LESS_EQUAL 0 OR FSIM_TIMEOUT GREATER 7200)
      message(FATAL_ERROR "unbounded CTest timeout: ${FSIM_TEST_NAME}")
    endif()
    math(EXPR FSIM_BOUNDED_TESTS "${FSIM_BOUNDED_TESTS} + 1")
  endforeach()
endforeach()

list(LENGTH FSIM_IDS FSIM_ID_COUNT)
message(STATUS
  "build resources: ${FSIM_ID_COUNT} required/additive owners, link pool ${FSIM_LINK_POOL}, ${FSIM_BOUNDED_TESTS} explicit bounded CTest timeouts")

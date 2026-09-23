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

set(FSIM_OBLIGATION_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_obligations.tsv")
file(STRINGS "${FSIM_OBLIGATION_LEDGER}" FSIM_OBLIGATION_ROWS)
set(FSIM_CURRENT_IDS)
foreach(FSIM_ROW IN LISTS FSIM_OBLIGATION_ROWS)
  if(NOT FSIM_ROW MATCHES "^V3REJECT-")
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "invalid v2-input obligation row")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_PATH)
  list(GET FSIM_FIELDS 2 FSIM_OWNER)
  if(FSIM_ID IN_LIST FSIM_CURRENT_IDS)
    message(FATAL_ERROR "duplicate current v2-input ID: ${FSIM_ID}")
  endif()
  fsim_current_evidence_file("${FSIM_PATH}")
  list(APPEND FSIM_CURRENT_IDS "${FSIM_ID}")
  set("FSIM_EVIDENCE_${FSIM_ID}" "${FSIM_PATH}")
  set("FSIM_TEST_${FSIM_ID}" "${FSIM_OWNER}")
endforeach()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_v2_input_rejection_inventory.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_ROWS)
set(FSIM_IDS)
set(FSIM_FAMILIES)
foreach(FSIM_ROW IN LISTS FSIM_ROWS)
  if(FSIM_ROW MATCHES "^#" OR FSIM_ROW MATCHES "^id\t"
     OR FSIM_ROW STREQUAL "")
    continue()
  endif()
  if(FSIM_ROW MATCHES ";")
    message(FATAL_ERROR "invalid v2-input rejection row")
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 7)
    message(FATAL_ERROR "v2-input row has ${FSIM_FIELD_COUNT} fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_FAMILY)
  list(GET FSIM_FIELDS 2 FSIM_V2)
  list(GET FSIM_FIELDS 3 FSIM_V3)
  list(GET FSIM_FIELDS 4 FSIM_PATH)
  list(GET FSIM_FIELDS 5 FSIM_CONTAINMENT)
  list(GET FSIM_FIELDS 6 FSIM_OWNER)
  if(NOT FSIM_ID MATCHES "^V3REJECT-[A-Z0-9-]+$"
     OR FSIM_ID IN_LIST FSIM_IDS OR FSIM_V2 STREQUAL ""
     OR FSIM_V3 STREQUAL "" OR FSIM_OWNER STREQUAL ""
     OR NOT FSIM_FAMILY MATCHES
       "^(manifest|object|design|checkpoint|cache|plugin)$"
     OR NOT FSIM_CONTAINMENT MATCHES
       "^(exact-diagnostic-no-config|header-first-exact-diagnostic|header-first-no-unit|metadata-first-exact-diagnostic|header-first-no-state|namespace-miss-no-fallback|abi-error-no-metadata)$")
    message(FATAL_ERROR "invalid v2-input identity/containment: ${FSIM_ID}")
  endif()
  fsim_current_evidence_file("${FSIM_PATH}")
  if(NOT DEFINED FSIM_TEST_${FSIM_ID})
    message(FATAL_ERROR "v2-input ID has no current behavioral owner: ${FSIM_ID}")
  endif()
  set(FSIM_TEST "${FSIM_TEST_${FSIM_ID}}")
  if(NOT FSIM_TEST IN_LIST FSIM_REGISTERED_TESTS)
    message(FATAL_ERROR "v2-input owner is unregistered: ${FSIM_ID}")
  endif()
  if(FSIM_FAMILY STREQUAL "cache")
    if(NOT FSIM_ID STREQUAL "V3REJECT-CACHE"
       OR NOT FSIM_PATH STREQUAL "src/compiler/native_cache_schema.hpp"
       OR NOT FSIM_TEST STREQUAL "fsim.cache"
       OR NOT FSIM_EVIDENCE_${FSIM_ID} STREQUAL
         "tests/compiler/cache_test.cpp")
      message(FATAL_ERROR "native cache lacks namespace-miss owner")
    endif()
    foreach(FSIM_CACHE_TEST IN ITEMS fsim.schema-identity fsim.llvm)
      if(NOT FSIM_CACHE_TEST IN_LIST FSIM_REGISTERED_TESTS)
        message(FATAL_ERROR "native cache owner is unregistered")
      endif()
    endforeach()
  elseif(NOT FSIM_EVIDENCE_${FSIM_ID} STREQUAL FSIM_PATH)
    message(FATAL_ERROR "v2-input evidence/owner path differs: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_FAMILIES "${FSIM_FAMILY}")
endforeach()

file(STRINGS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ids.txt"
  FSIM_REQUIRED_IDS)
foreach(FSIM_ID IN LISTS FSIM_REQUIRED_IDS)
  if(FSIM_ID MATCHES "^V3REJECT-[A-Z0-9-]+$"
     AND NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "required v2-input rejection ID is missing: ${FSIM_ID}")
  endif()
endforeach()
foreach(FSIM_FAMILY IN ITEMS manifest object design checkpoint cache plugin)
  if(NOT FSIM_FAMILY IN_LIST FSIM_FAMILIES)
    message(FATAL_ERROR "v2-input rejection family has no owner: ${FSIM_FAMILY}")
  endif()
endforeach()

list(LENGTH FSIM_IDS FSIM_ID_COUNT)
message(STATUS
  "v2-input rejection: ${FSIM_ID_COUNT} required/additive IDs have registered negative-test owners")

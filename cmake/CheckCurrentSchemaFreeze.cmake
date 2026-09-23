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
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_schema_freeze_inventory.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_ROWS)
set(FSIM_IDS)
set(FSIM_DOMAINS)
foreach(FSIM_ROW IN LISTS FSIM_ROWS)
  if(FSIM_ROW MATCHES "^#" OR FSIM_ROW MATCHES "^id\t"
     OR FSIM_ROW STREQUAL "")
    continue()
  endif()
  if(FSIM_ROW MATCHES ";")
    message(FATAL_ERROR "invalid schema-freeze row")
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 7)
    message(FATAL_ERROR "schema-freeze row has ${FSIM_FIELD_COUNT} fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 2 FSIM_CHECKER)
  list(GET FSIM_FIELDS 5 FSIM_STATE)
  list(GET FSIM_FIELDS 6 FSIM_OWNER)
  if(NOT FSIM_ID MATCHES "^V3SCHEMA-[A-Z0-9-]+$"
     OR FSIM_ID IN_LIST FSIM_IDS OR FSIM_DOMAIN IN_LIST FSIM_DOMAINS
     OR NOT FSIM_STATE MATCHES "^(active|preserved)$"
     OR FSIM_OWNER STREQUAL "")
    message(FATAL_ERROR "invalid schema-freeze identity: ${FSIM_ID}")
  endif()
  fsim_current_evidence_file("${FSIM_CHECKER}")
  if(FSIM_DOMAIN STREQUAL "manifest")
    set(FSIM_OWNERS fsim.project fsim.project-manifest-schema-freeze)
  elseif(FSIM_DOMAIN STREQUAL "abi")
    set(FSIM_OWNERS fsim.schema-identity fsim.api.abi
      fsim.foreign-abi-freeze)
  elseif(FSIM_DOMAIN STREQUAL "object")
    set(FSIM_OWNERS fsim.schema-identity fsim.artifact.object
      fsim.portable-object-schema-freeze)
  elseif(FSIM_DOMAIN STREQUAL "design")
    set(FSIM_OWNERS fsim.schema-identity fsim.artifact.design
      fsim.library.artifact fsim.design-library-schema-freeze)
  elseif(FSIM_DOMAIN STREQUAL "checkpoint")
    set(FSIM_OWNERS fsim.schema-identity
      fsim.application.artifact_phases fsim.nested-portable-schema-freeze)
  elseif(FSIM_DOMAIN STREQUAL "cache")
    set(FSIM_OWNERS fsim.schema-identity fsim.llvm
      fsim.incremental-native-cache-freeze)
  else()
    message(FATAL_ERROR "schema-freeze domain has no behavioral owner: ${FSIM_DOMAIN}")
  endif()
  foreach(FSIM_TEST IN LISTS FSIM_OWNERS)
    if(NOT FSIM_TEST IN_LIST FSIM_REGISTERED_TESTS)
      message(FATAL_ERROR "schema-freeze owner is unregistered: ${FSIM_TEST}")
    endif()
  endforeach()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_DOMAINS "${FSIM_DOMAIN}")
endforeach()

file(STRINGS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ids.txt"
  FSIM_REQUIRED_IDS)
foreach(FSIM_ID IN LISTS FSIM_REQUIRED_IDS)
  if(FSIM_ID MATCHES "^V3SCHEMA-[A-Z0-9-]+$"
     AND NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "required schema-freeze identity is missing: ${FSIM_ID}")
  endif()
endforeach()

list(LENGTH FSIM_IDS FSIM_DOMAIN_COUNT)
message(STATUS
  "schema freeze: ${FSIM_DOMAIN_COUNT} required/additive domains have compiled identities and behavioral owners")

# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_normalized_text_sha256 path output_variable)
  file(READ "${path}" contents)
  string(REPLACE "\r\n" "\n" contents "${contents}")
  string(REPLACE "\r" "\n" contents "${contents}")
  string(SHA256 digest "${contents}")
  set("${output_variable}" "${digest}" PARENT_SCOPE)
endfunction()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/abi_schema_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v2.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS "${FSIM_INVENTORY}" "${FSIM_PLAN}" "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "ABI/schema inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_EXPECTED_DIGEST
  "96d4ee761b927df5a02559f6abf3c0e2014f52bf44635ec245f98aaed2c7b613")
set(FSIM_COMPLETED_CHANGE 19)
fsim_normalized_text_sha256("${FSIM_INVENTORY}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "ABI/schema inventory digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 174 - v2 artifact, ABI, and development-schema freeze" FSIM_BATCH_START)
string(FIND "${FSIM_PLAN_TEXT}" "### Batch 175 -" FSIM_BATCH_END)
if(FSIM_BATCH_START EQUAL -1 OR FSIM_BATCH_END EQUAL -1 OR
   FSIM_BATCH_END LESS_EQUAL FSIM_BATCH_START)
  message(FATAL_ERROR "authoritative Batch 174 plan boundary is missing")
endif()
math(EXPR FSIM_BATCH_LENGTH "${FSIM_BATCH_END} - ${FSIM_BATCH_START}")
string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_BATCH_START}
  ${FSIM_BATCH_LENGTH} FSIM_BATCH_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "Planning and ownership only"
    "development-schema policy"
    "never gain a cross-toolchain compatibility reader"
    "source-regeneration guidance"
    "120-minute command timeouts"
    "neither a sanitizer nor"
    "hosted-CI monitoring boundary")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 174 plan lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_CHANGE RANGE 1 20)
  set(FSIM_CHANGE_TOKEN "\n${FSIM_CHANGE}.")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_CHANGE_TOKEN}" FSIM_CHANGE_OFFSET)
  if(FSIM_CHANGE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 174 plan lost exact Change ${FSIM_CHANGE}")
  endif()
  math(EXPR FSIM_REMAINDER_START "${FSIM_CHANGE_OFFSET} + 1")
  string(SUBSTRING "${FSIM_BATCH_TEXT}" ${FSIM_REMAINDER_START} -1 FSIM_REMAINDER)
  string(FIND "${FSIM_REMAINDER}" "${FSIM_CHANGE_TOKEN}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "Batch 174 plan duplicates Change ${FSIM_CHANGE}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.abi-schema-inventory"
    "CheckAbiSchemaInventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "ABI/schema inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_HEADER
  "id\tbaseline\thdl_profiles\tdomain\tboundary\tclosure\tobligation\timplementation_owner\tpositive_evidence\tnegative_evidence\tprotocol_evidence\tphase_evidence\tartifact_evidence\tdiagnostic_owner\tresource_owner")
set(FSIM_EXPECTED_BASELINE
  "fsim-v2@06f8e03,LLVM22.1.8,SystemC3.0.2,TLM2.0.6,SCV2.0.1")
set(FSIM_EXPECTED_PROFILES
  "VHDL87,VHDL93,VHDL2000,VHDL2002,VHDL2008,V1995,V2001,V2001NoConfig,V2005,SV2005,SV2009,SV2012,SV2017,SystemC302,TLM10,TLM20,SCV201,UVM12,UVM20203.1")
set(FSIM_EXPECTED_DOMAINS
  core-public-c-cpp
  systemc-tlm-scv-abi
  foreign-interface-abi
  portable-object-schema
  design-library-schemas
  incremental-native-cache
  nested-portable-schemas
  project-stale-schema-policy
  portable-stale-schema-policy
  native-producer-policy
  schema-producer-diagnostics
  source-hidden-readonly-relocation
  non-project-restartability
  mixed-host-toolchain-install
  cache-corruption-isolation
  abi-schema-documentation
  exhaustive-evidence-matrix
  closure-release-handoff)

function(fsim_require_abi_schema_owner FSIM_ID FSIM_FIELD FSIM_PATH FSIM_PREFIX)
  if(FSIM_PATH STREQUAL "" OR NOT FSIM_PATH MATCHES "^${FSIM_PREFIX}")
    message(FATAL_ERROR "${FSIM_ID} misplaced ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
  if(NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_PATH}")
    message(FATAL_ERROR "${FSIM_ID} lost ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
endfunction()

file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 20)
  message(FATAL_ERROR "ABI/schema inventory requires SPDX, header and 18 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_ACTUAL_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_ACTUAL_HEADER STREQUAL FSIM_HEADER)
  message(FATAL_ERROR "ABI/schema inventory header or SPDX policy changed")
endif()

set(FSIM_IDS)
set(FSIM_CLOSURES)
set(FSIM_ACTIVE_COUNT 0)
set(FSIM_PRESERVED_COUNT 0)
foreach(FSIM_INDEX RANGE 2 19)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 15)
    message(FATAL_ERROR
      "ABI/schema row ${FSIM_INDEX} does not have fifteen fields")
  endif()

  set(FSIM_CHANGE ${FSIM_INDEX})
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  math(EXPR FSIM_DOMAIN_INDEX "${FSIM_CHANGE} - 2")
  list(GET FSIM_EXPECTED_DOMAINS ${FSIM_DOMAIN_INDEX} FSIM_EXPECTED_DOMAIN)
  if(FSIM_CHANGE LESS_EQUAL FSIM_COMPLETED_CHANGE)
    set(FSIM_EXPECTED_BOUNDARY preserved)
  else()
    set(FSIM_EXPECTED_BOUNDARY active)
  endif()

  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_BASELINE)
  list(GET FSIM_FIELDS 2 FSIM_PROFILES)
  list(GET FSIM_FIELDS 3 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 4 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 5 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 6 FSIM_OBLIGATION)
  if(NOT FSIM_ID STREQUAL "ABI174-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_BASELINE STREQUAL FSIM_EXPECTED_BASELINE OR
     NOT FSIM_PROFILES STREQUAL FSIM_EXPECTED_PROFILES OR
     NOT FSIM_DOMAIN STREQUAL FSIM_EXPECTED_DOMAIN OR
     NOT FSIM_BOUNDARY STREQUAL FSIM_EXPECTED_BOUNDARY OR
     NOT FSIM_CLOSURE STREQUAL "B174-C${FSIM_CHANGE_TEXT}" OR
     FSIM_OBLIGATION STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has a drifted scope or closure field")
  endif()

  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE_ID)
  list(FIND FSIM_CLOSURES "${FSIM_CLOSURE}" FSIM_DUPLICATE_CLOSURE)
  if(NOT FSIM_DUPLICATE_ID EQUAL -1 OR NOT FSIM_DUPLICATE_CLOSURE EQUAL -1)
    message(FATAL_ERROR "duplicate ABI/schema id or closure: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_CLOSURES "${FSIM_CLOSURE}")
  if(FSIM_BOUNDARY STREQUAL active)
    math(EXPR FSIM_ACTIVE_COUNT "${FSIM_ACTIVE_COUNT} + 1")
  else()
    math(EXPR FSIM_PRESERVED_COUNT "${FSIM_PRESERVED_COUNT} + 1")
  endif()

  list(GET FSIM_FIELDS 7 FSIM_IMPLEMENTATION_OWNER)
  fsim_require_abi_schema_owner("${FSIM_ID}" implementation
    "${FSIM_IMPLEMENTATION_OWNER}" "(include|src|cmake|docs|tests)/")
  foreach(FSIM_EVIDENCE_INDEX RANGE 8 12)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE_OWNER)
    fsim_require_abi_schema_owner("${FSIM_ID}"
      "evidence-${FSIM_EVIDENCE_INDEX}" "${FSIM_EVIDENCE_OWNER}" "tests/")
  endforeach()
  list(GET FSIM_FIELDS 13 FSIM_DIAGNOSTIC_OWNER)
  list(GET FSIM_FIELDS 14 FSIM_RESOURCE_OWNER)
  if(NOT FSIM_DIAGNOSTIC_OWNER STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_RESOURCE_OWNER STREQUAL "cmake/CheckSourceLineBudget.cmake")
    message(FATAL_ERROR
      "${FSIM_ID} drifted diagnostic or resource ownership")
  endif()
endforeach()

math(EXPR FSIM_EXPECTED_PRESERVED "${FSIM_COMPLETED_CHANGE} - 1")
math(EXPR FSIM_EXPECTED_ACTIVE "19 - ${FSIM_COMPLETED_CHANGE}")
list(LENGTH FSIM_IDS FSIM_ID_COUNT)
list(LENGTH FSIM_CLOSURES FSIM_CLOSURE_COUNT)
if(NOT FSIM_ID_COUNT EQUAL 18 OR NOT FSIM_CLOSURE_COUNT EQUAL 18 OR
   NOT FSIM_ACTIVE_COUNT EQUAL FSIM_EXPECTED_ACTIVE OR
   NOT FSIM_PRESERVED_COUNT EQUAL FSIM_EXPECTED_PRESERVED)
  message(FATAL_ERROR
    "ABI/schema boundary mismatch: ids=${FSIM_ID_COUNT}, closures=${FSIM_CLOSURE_COUNT}, preserved=${FSIM_PRESERVED_COUNT}, active=${FSIM_ACTIVE_COUNT}")
endif()

message(STATUS
  "ABI/schema inventory passed: rows=18 preserved=${FSIM_PRESERVED_COUNT} active=${FSIM_ACTIVE_COUNT} digest=${FSIM_ACTUAL_DIGEST}")

# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/sdf_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v2.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_INVENTORY}"
    "${FSIM_PLAN}"
    "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "SDF inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_EXPECTED_DIGEST
  "5a4817053834c76fef49650d38b73957a4be5560c62d1eb0f8c72c7180340c4b")
set(FSIM_COMPLETED_CHANGE 18)
file(SHA256 "${FSIM_INVENTORY}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "SDF inventory digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 168 - SDF 4.0 parser, normalization, and artifacts"
  FSIM_BATCH_START)
string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 169 - Verilog/SystemVerilog SDF annotation"
  FSIM_BATCH_END)
if(FSIM_BATCH_START EQUAL -1 OR
   FSIM_BATCH_END EQUAL -1 OR
   FSIM_BATCH_END LESS_EQUAL FSIM_BATCH_START)
  message(FATAL_ERROR "authoritative Batch 168 plan boundary is missing")
endif()
math(EXPR FSIM_BATCH_LENGTH "${FSIM_BATCH_END} - ${FSIM_BATCH_START}")
string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_BATCH_START}
  ${FSIM_BATCH_LENGTH} FSIM_BATCH_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "clean-room SDF 4.0 parser"
    "SDF 2.1 and 3.0"
    "does not apply them to simulator timing behavior"
    "Preserve exact decimal/triple values"
    "Batches 169-170 own application")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 168 plan lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_CHANGE RANGE 1 20)
  set(FSIM_CHANGE_TOKEN "- **Change ${FSIM_CHANGE}:")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_CHANGE_TOKEN}" FSIM_CHANGE_OFFSET)
  if(FSIM_CHANGE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "Batch 168 plan lost the exact Change ${FSIM_CHANGE} allocation")
  endif()
  math(EXPR FSIM_REMAINDER_START "${FSIM_CHANGE_OFFSET} + 1")
  string(SUBSTRING "${FSIM_BATCH_TEXT}" ${FSIM_REMAINDER_START} -1
    FSIM_BATCH_REMAINDER)
  string(FIND "${FSIM_BATCH_REMAINDER}" "${FSIM_CHANGE_TOKEN}"
    FSIM_DUPLICATE_OFFSET)
  if(NOT FSIM_DUPLICATE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "Batch 168 plan duplicates Change ${FSIM_CHANGE}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.sdf-inventory"
    "CheckSdfInventory.cmake"
    "fsim.sdf-inventory")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "SDF inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_HEADER
  "id\trevisions\tdomain\tboundary\tclosure\tobligation\timplementation_owner\tpositive_evidence\tnegative_evidence\trevision_evidence\tcoordinate_evidence\troundtrip_evidence\tdiagnostic_owner\tresource_owner")
set(FSIM_EXPECTED_REVISIONS "SDF21,SDF30,SDF40")
set(FSIM_EXPECTED_DOMAINS
  lexical
  headers
  sdf40-constructs
  sdf21-adapter
  sdf30-adapter
  normalization-values
  normalized-ir
  annotation-scope
  instance-resolution
  endpoint-resolution
  mapping-validation
  schema
  artifact-identity
  library-relocation
  non-project-cache
  corpora
  closure)

function(fsim_require_sdf_path FSIM_ID FSIM_FIELD FSIM_PATH FSIM_PREFIX)
  if(FSIM_PATH STREQUAL "" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_PATH}")
    message(FATAL_ERROR "${FSIM_ID} lost ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
  if(NOT FSIM_PREFIX STREQUAL "" AND NOT FSIM_PATH MATCHES "^${FSIM_PREFIX}")
    message(FATAL_ERROR "${FSIM_ID} misplaced ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
endfunction()

file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 19)
  message(FATAL_ERROR
    "SDF inventory must contain SPDX, header and 17 rows; got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_ACTUAL_HEADER)
if(NOT FSIM_ACTUAL_HEADER STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "SDF inventory lost its SPDX policy")
endif()
list(GET FSIM_ROWS 1 FSIM_ACTUAL_HEADER)
if(NOT FSIM_ACTUAL_HEADER STREQUAL FSIM_HEADER)
  message(FATAL_ERROR "SDF inventory header changed")
endif()

set(FSIM_IDS)
set(FSIM_CLOSURES)
set(FSIM_ACTIVE_COUNT 0)
set(FSIM_PRESERVED_COUNT 0)
foreach(FSIM_INDEX RANGE 2 18)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 14)
    message(FATAL_ERROR
      "SDF row ${FSIM_INDEX} does not have fourteen fields")
  endif()

  set(FSIM_CHANGE ${FSIM_INDEX})
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  math(EXPR FSIM_DOMAIN_INDEX "${FSIM_CHANGE} - 2")
  list(GET FSIM_EXPECTED_DOMAINS ${FSIM_DOMAIN_INDEX} FSIM_EXPECTED_DOMAIN)

  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_REVISIONS)
  list(GET FSIM_FIELDS 2 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 3 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 4 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 5 FSIM_OBLIGATION)
  if(FSIM_CHANGE LESS_EQUAL FSIM_COMPLETED_CHANGE)
    set(FSIM_EXPECTED_BOUNDARY "preserved")
  else()
    set(FSIM_EXPECTED_BOUNDARY "active")
  endif()
  if(NOT FSIM_ID STREQUAL "SDF-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_REVISIONS STREQUAL FSIM_EXPECTED_REVISIONS OR
     NOT FSIM_DOMAIN STREQUAL FSIM_EXPECTED_DOMAIN OR
     NOT FSIM_BOUNDARY STREQUAL FSIM_EXPECTED_BOUNDARY OR
     NOT FSIM_CLOSURE STREQUAL "B168-C${FSIM_CHANGE_TEXT}" OR
     FSIM_OBLIGATION STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has a drifted scope or closure field")
  endif()

  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate SDF inventory id: ${FSIM_ID}")
  endif()
  list(FIND FSIM_CLOSURES "${FSIM_CLOSURE}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate SDF closure allocation: ${FSIM_CLOSURE}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_CLOSURES "${FSIM_CLOSURE}")
  if(FSIM_BOUNDARY STREQUAL "active")
    math(EXPR FSIM_ACTIVE_COUNT "${FSIM_ACTIVE_COUNT} + 1")
  else()
    math(EXPR FSIM_PRESERVED_COUNT "${FSIM_PRESERVED_COUNT} + 1")
  endif()

  list(GET FSIM_FIELDS 6 FSIM_IMPLEMENTATION_OWNER)
  fsim_require_sdf_path("${FSIM_ID}" "implementation"
    "${FSIM_IMPLEMENTATION_OWNER}" "(src|tests|cmake)/")
  foreach(FSIM_EVIDENCE_INDEX RANGE 7 11)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE_OWNER)
    fsim_require_sdf_path("${FSIM_ID}"
      "evidence-${FSIM_EVIDENCE_INDEX}" "${FSIM_EVIDENCE_OWNER}" "tests/")
  endforeach()
  list(GET FSIM_FIELDS 12 FSIM_DIAGNOSTIC_OWNER)
  list(GET FSIM_FIELDS 13 FSIM_RESOURCE_OWNER)
  if(NOT FSIM_DIAGNOSTIC_OWNER STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_RESOURCE_OWNER STREQUAL
       "cmake/CheckResourcePortabilityContract.cmake")
    message(FATAL_ERROR "${FSIM_ID} drifted diagnostic or resource ownership")
  endif()
endforeach()

list(LENGTH FSIM_IDS FSIM_ID_COUNT)
list(LENGTH FSIM_CLOSURES FSIM_CLOSURE_COUNT)
math(EXPR FSIM_EXPECTED_PRESERVED "${FSIM_COMPLETED_CHANGE} - 1")
math(EXPR FSIM_EXPECTED_ACTIVE "18 - ${FSIM_COMPLETED_CHANGE}")
if(NOT FSIM_ID_COUNT EQUAL 17 OR
   NOT FSIM_CLOSURE_COUNT EQUAL 17 OR
   NOT FSIM_ACTIVE_COUNT EQUAL FSIM_EXPECTED_ACTIVE OR
   NOT FSIM_PRESERVED_COUNT EQUAL FSIM_EXPECTED_PRESERVED)
  message(FATAL_ERROR
    "SDF inventory boundary mismatch: ids=${FSIM_ID_COUNT}, closures=${FSIM_CLOSURE_COUNT}, preserved=${FSIM_PRESERVED_COUNT}, active=${FSIM_ACTIVE_COUNT}")
endif()

message(STATUS
  "SDF inventory passed: rows=17 preserved=${FSIM_PRESERVED_COUNT} active=${FSIM_ACTIVE_COUNT} revisions=${FSIM_EXPECTED_REVISIONS} digest=${FSIM_ACTUAL_DIGEST}")

# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/sdf_vital_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v2.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS "${FSIM_INVENTORY}" "${FSIM_PLAN}" "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "SDF VITAL inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_EXPECTED_DIGEST
  "3e84f643e6df24090efb1161e0d6836847784268a139e3da3c2fda4568913c6f")
set(FSIM_COMPLETED_CHANGE 18)
file(SHA256 "${FSIM_INVENTORY}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "SDF VITAL inventory digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 170 - VHDL/VITAL and mixed-language SDF" FSIM_BATCH_START)
string(FIND "${FSIM_PLAN_TEXT}" "### Batch 171 - FST tracing closure" FSIM_BATCH_END)
if(FSIM_BATCH_START EQUAL -1 OR FSIM_BATCH_END EQUAL -1 OR
   FSIM_BATCH_END LESS_EQUAL FSIM_BATCH_START)
  message(FATAL_ERROR "authoritative Batch 170 plan boundary is missing")
endif()
math(EXPR FSIM_BATCH_LENGTH "${FSIM_BATCH_END} - ${FSIM_BATCH_START}")
string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_BATCH_START}
  ${FSIM_BATCH_LENGTH} FSIM_BATCH_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "extend the exact Batch 168 SDF IR"
    "VHDL/VITAL and timing paths that cross a VHDL boundary"
    "Release builds and testing are not required"
    "Avoid header-only formatting changes"
    "sanitizer" "hosted-CI")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 170 plan lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_CHANGE RANGE 1 20)
  set(FSIM_CHANGE_TOKEN "- **Change ${FSIM_CHANGE}:")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_CHANGE_TOKEN}" FSIM_CHANGE_OFFSET)
  if(FSIM_CHANGE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 170 plan lost exact Change ${FSIM_CHANGE}")
  endif()
  math(EXPR FSIM_REMAINDER_START "${FSIM_CHANGE_OFFSET} + 1")
  string(SUBSTRING "${FSIM_BATCH_TEXT}" ${FSIM_REMAINDER_START} -1 FSIM_REMAINDER)
  string(FIND "${FSIM_REMAINDER}" "${FSIM_CHANGE_TOKEN}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "Batch 170 plan duplicates Change ${FSIM_CHANGE}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.sdf-vital-inventory"
    "CheckSdfVitalInventory.cmake"
    "fsim.sdf-vital-inventory")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "SDF VITAL inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_HEADER
  "id\tsdf_revisions\thdl_profiles\tdomain\tboundary\tclosure\tobligation\timplementation_owner\tpositive_evidence\tnegative_evidence\tengine_evidence\tphase_evidence\tartifact_evidence\tdiagnostic_owner\tresource_owner")
set(FSIM_EXPECTED_REVISIONS "SDF21,SDF30,SDF40")
set(FSIM_EXPECTED_PROFILES
  "VHDL87,VHDL93,VHDL2000,VHDL2002,VHDL2008,V1995,V2001,V2001NoConfig,V2005,SV2005,SV2009,SV2012,SV2017,SystemC23")
set(FSIM_EXPECTED_DOMAINS
  vital-target-plan vital-path-check vital-model-wrappers
  vital-precedence-generics vital-delay-semantics vital-negative-checks
  vital-reannotation vhdl-verilog-boundary vhdl-systemverilog-boundary
  vhdl-systemc-proxy mixed-root-resolution foreign-interfaces debug-trace
  artifact-libraries non-project-phases mixed-corpus closure-negatives)

function(fsim_require_sdf_vital_path FSIM_ID FSIM_FIELD FSIM_PATH FSIM_PREFIX FSIM_EXISTS)
  if(FSIM_PATH STREQUAL "" OR NOT FSIM_PATH MATCHES "^${FSIM_PREFIX}")
    message(FATAL_ERROR "${FSIM_ID} misplaced ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
  if(FSIM_EXISTS AND NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_PATH}")
    message(FATAL_ERROR "${FSIM_ID} lost ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
endfunction()

file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 19)
  message(FATAL_ERROR "SDF VITAL inventory requires SPDX, header and 17 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_ACTUAL_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_ACTUAL_HEADER STREQUAL FSIM_HEADER)
  message(FATAL_ERROR "SDF VITAL inventory header or SPDX policy changed")
endif()

set(FSIM_IDS)
set(FSIM_CLOSURES)
set(FSIM_ACTIVE_COUNT 0)
set(FSIM_PRESERVED_COUNT 0)
foreach(FSIM_INDEX RANGE 2 18)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 15)
    message(FATAL_ERROR "SDF VITAL row ${FSIM_INDEX} does not have fifteen fields")
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
    set(FSIM_REQUIRE_EXISTS TRUE)
  else()
    set(FSIM_EXPECTED_BOUNDARY active)
    set(FSIM_REQUIRE_EXISTS FALSE)
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_REVISIONS)
  list(GET FSIM_FIELDS 2 FSIM_PROFILES)
  list(GET FSIM_FIELDS 3 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 4 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 5 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 6 FSIM_OBLIGATION)
  if(NOT FSIM_ID STREQUAL "VITALSDF-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_REVISIONS STREQUAL FSIM_EXPECTED_REVISIONS OR
     NOT FSIM_PROFILES STREQUAL FSIM_EXPECTED_PROFILES OR
     NOT FSIM_DOMAIN STREQUAL FSIM_EXPECTED_DOMAIN OR
     NOT FSIM_BOUNDARY STREQUAL FSIM_EXPECTED_BOUNDARY OR
     NOT FSIM_CLOSURE STREQUAL "B170-C${FSIM_CHANGE_TEXT}" OR
     FSIM_OBLIGATION STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has a drifted scope or closure field")
  endif()
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE_ID)
  list(FIND FSIM_CLOSURES "${FSIM_CLOSURE}" FSIM_DUPLICATE_CLOSURE)
  if(NOT FSIM_DUPLICATE_ID EQUAL -1 OR NOT FSIM_DUPLICATE_CLOSURE EQUAL -1)
    message(FATAL_ERROR "duplicate SDF VITAL id or closure: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_CLOSURES "${FSIM_CLOSURE}")
  if(FSIM_BOUNDARY STREQUAL active)
    math(EXPR FSIM_ACTIVE_COUNT "${FSIM_ACTIVE_COUNT} + 1")
  else()
    math(EXPR FSIM_PRESERVED_COUNT "${FSIM_PRESERVED_COUNT} + 1")
  endif()
  list(GET FSIM_FIELDS 7 FSIM_IMPLEMENTATION_OWNER)
  fsim_require_sdf_vital_path("${FSIM_ID}" implementation
    "${FSIM_IMPLEMENTATION_OWNER}" "(src|cmake)/" "${FSIM_REQUIRE_EXISTS}")
  foreach(FSIM_EVIDENCE_INDEX RANGE 8 12)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE_OWNER)
    fsim_require_sdf_vital_path("${FSIM_ID}" "evidence-${FSIM_EVIDENCE_INDEX}"
      "${FSIM_EVIDENCE_OWNER}" "tests/" "${FSIM_REQUIRE_EXISTS}")
  endforeach()
  list(GET FSIM_FIELDS 13 FSIM_DIAGNOSTIC_OWNER)
  list(GET FSIM_FIELDS 14 FSIM_RESOURCE_OWNER)
  if(NOT FSIM_DIAGNOSTIC_OWNER STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_RESOURCE_OWNER STREQUAL "cmake/CheckResourcePortabilityContract.cmake" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_DIAGNOSTIC_OWNER}" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_RESOURCE_OWNER}")
    message(FATAL_ERROR "${FSIM_ID} drifted diagnostic or resource ownership")
  endif()
endforeach()

math(EXPR FSIM_EXPECTED_PRESERVED "${FSIM_COMPLETED_CHANGE} - 1")
math(EXPR FSIM_EXPECTED_ACTIVE "18 - ${FSIM_COMPLETED_CHANGE}")
list(LENGTH FSIM_IDS FSIM_ID_COUNT)
list(LENGTH FSIM_CLOSURES FSIM_CLOSURE_COUNT)
if(NOT FSIM_ID_COUNT EQUAL 17 OR NOT FSIM_CLOSURE_COUNT EQUAL 17 OR
   NOT FSIM_ACTIVE_COUNT EQUAL FSIM_EXPECTED_ACTIVE OR
   NOT FSIM_PRESERVED_COUNT EQUAL FSIM_EXPECTED_PRESERVED)
  message(FATAL_ERROR
    "SDF VITAL boundary mismatch: ids=${FSIM_ID_COUNT}, closures=${FSIM_CLOSURE_COUNT}, preserved=${FSIM_PRESERVED_COUNT}, active=${FSIM_ACTIVE_COUNT}")
endif()

message(STATUS
  "SDF VITAL inventory passed: rows=17 preserved=${FSIM_PRESERVED_COUNT} active=${FSIM_ACTIVE_COUNT} digest=${FSIM_ACTUAL_DIGEST}")

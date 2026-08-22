# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/code_coverage_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v3.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_INVENTORY}" "${FSIM_PLAN}" "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR
      "code coverage inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_INVENTORY}" FSIM_INVENTORY_TEXT)
string(REPLACE "\r\n" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(REPLACE "\r" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(SHA256 FSIM_ACTUAL_DIGEST "${FSIM_INVENTORY_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "06039618ff2c8530265b14e3250578f3fd0aa77d6e9b2a53a8049e7b760e8234")
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "code coverage inventory digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

foreach(FSIM_FORBIDDEN IN ITEMS "/home/" "standards/" "file://")
  string(FIND "${FSIM_INVENTORY_TEXT}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "code coverage inventory records forbidden private or absolute text")
  endif()
endforeach()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
string(REGEX MATCHALL "### Batch [0-9][0-9][0-9] -"
  FSIM_BATCH_HEADINGS "${FSIM_PLAN_TEXT}")
list(LENGTH FSIM_BATCH_HEADINGS FSIM_BATCH_COUNT)
if(NOT FSIM_BATCH_COUNT EQUAL 20)
  message(FATAL_ERROR
    "v3 plan must contain exactly twenty batch headings; got ${FSIM_BATCH_COUNT}")
endif()
foreach(FSIM_BATCH RANGE 178 197)
  set(FSIM_BATCH_TOKEN "### Batch ${FSIM_BATCH} -")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_BATCH_TOKEN}" FSIM_BATCH_OFFSET)
  if(FSIM_BATCH_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan lost Batch ${FSIM_BATCH}")
  endif()
  math(EXPR FSIM_REMAINDER_START "${FSIM_BATCH_OFFSET} + 1")
  string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_REMAINDER_START} -1
    FSIM_PLAN_REMAINDER)
  string(FIND "${FSIM_PLAN_REMAINDER}" "${FSIM_BATCH_TOKEN}"
    FSIM_DUPLICATE_OFFSET)
  if(NOT FSIM_DUPLICATE_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan duplicates Batch ${FSIM_BATCH}")
  endif()
endforeach()

string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 178 - coverage identity, statement, line, and branch foundation"
  FSIM_BATCH_START)
string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 179 - condition, expression, toggle, and FSM metrics"
  FSIM_BATCH_END)
if(FSIM_BATCH_START EQUAL -1 OR FSIM_BATCH_END EQUAL -1 OR
   FSIM_BATCH_END LESS_EQUAL FSIM_BATCH_START)
  message(FATAL_ERROR "authoritative Batch 178 plan boundary is missing")
endif()
math(EXPR FSIM_BATCH_LENGTH "${FSIM_BATCH_END} - ${FSIM_BATCH_START}")
string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_BATCH_START}
  ${FSIM_BATCH_LENGTH} FSIM_BATCH_TEXT)
foreach(FSIM_CHANGE RANGE 1 20)
  set(FSIM_CHANGE_TOKEN "\n${FSIM_CHANGE}. ")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_CHANGE_TOKEN}"
    FSIM_CHANGE_OFFSET)
  if(FSIM_CHANGE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 178 plan lost exact Change ${FSIM_CHANGE}")
  endif()
  math(EXPR FSIM_REMAINDER_START "${FSIM_CHANGE_OFFSET} + 1")
  string(SUBSTRING "${FSIM_BATCH_TEXT}" ${FSIM_REMAINDER_START} -1
    FSIM_BATCH_REMAINDER)
  string(FIND "${FSIM_BATCH_REMAINDER}" "${FSIM_CHANGE_TOKEN}"
    FSIM_DUPLICATE_OFFSET)
  if(NOT FSIM_DUPLICATE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 178 plan duplicates Change ${FSIM_CHANGE}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "Register a clause-neutral coverage obligation and ownership"
    "Prove Verilog/SystemVerilog/VHDL engine and aggregation equivalence."
    "Run standard batch closure and freeze the foundation inventory."
    "Batch 180 and Batch 190 remain tenth-batch sanitizer and hosted-CI"
    "Readers reject v2 manifests")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.code-coverage-inventory"
    "CheckCodeCoverageInventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "code coverage inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_EXPECTED_HEADER
  "id\tstandards\thdl_profiles\tdomain\tstate\tchange\tobligation\timplementation_owner\tpositive_evidence\tnegative_evidence\tengine_evidence\taggregation_evidence\tartifact_evidence\tdiagnostic_owner\tresource_owner")
set(FSIM_EXPECTED_STANDARDS "IEEE1076,IEEE1364,IEEE1800")
set(FSIM_EXPECTED_PROFILES
  "VHDL87,VHDL93,VHDL2000,VHDL2002,VHDL2008,V1995,V2001,V2001NoConfig,V2005,SV2005,SV2009,SV2012,SV2017")
set(FSIM_EXPECTED_DOMAINS
  coverage-model
  source-identity
  point-identity
  verilog-statement-points
  vhdl-statement-points
  branch-arms
  line-state
  instance-inventory
  simir-hit
  interpreter-counters
  llvm-counters
  debug-engine
  exclusions
  instance-identity
  source-aggregation
  opt-in-control
  artifact-cache-identity)
set(FSIM_COMPLETED_CHANGE 20)

function(fsim_require_code_coverage_owner
    FSIM_ID FSIM_FIELD FSIM_PATH FSIM_PREFIX FSIM_REQUIRE_EXISTS)
  if(FSIM_PATH STREQUAL "" OR IS_ABSOLUTE "${FSIM_PATH}" OR
     NOT FSIM_PATH MATCHES "^${FSIM_PREFIX}")
    message(FATAL_ERROR
      "${FSIM_ID} misplaced ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
  string(FIND "${FSIM_PATH}" ".." FSIM_PARENT_OFFSET)
  string(FIND "${FSIM_PATH}" "\\" FSIM_BACKSLASH_OFFSET)
  if(NOT FSIM_PARENT_OFFSET EQUAL -1 OR
     NOT FSIM_BACKSLASH_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "${FSIM_ID} has unsafe ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
  if(FSIM_REQUIRE_EXISTS AND
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_PATH}")
    message(FATAL_ERROR
      "${FSIM_ID} lost ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
endfunction()

string(REPLACE "\n" ";" FSIM_LINES "${FSIM_INVENTORY_TEXT}")
list(GET FSIM_LINES 0 FSIM_SPDX)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL FSIM_EXPECTED_HEADER)
  message(FATAL_ERROR
    "code coverage inventory SPDX policy or header changed")
endif()

set(FSIM_IDS)
set(FSIM_CHANGES)
set(FSIM_DOMAINS)
set(FSIM_ROW_COUNT 0)
set(FSIM_ACTIVE_COUNT 0)
set(FSIM_PRESERVED_COUNT 0)
foreach(FSIM_LINE IN LISTS FSIM_LINES)
  if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#" OR
     FSIM_LINE STREQUAL FSIM_EXPECTED_HEADER)
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 15)
    message(FATAL_ERROR
      "code coverage inventory row has ${FSIM_FIELD_COUNT} fields")
  endif()

  math(EXPR FSIM_CHANGE "${FSIM_ROW_COUNT} + 2")
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  list(GET FSIM_EXPECTED_DOMAINS ${FSIM_ROW_COUNT} FSIM_EXPECTED_DOMAIN)
  if(FSIM_CHANGE LESS_EQUAL FSIM_COMPLETED_CHANGE)
    set(FSIM_EXPECTED_STATE preserved)
    set(FSIM_REQUIRE_EXISTS TRUE)
  else()
    set(FSIM_EXPECTED_STATE active)
    set(FSIM_REQUIRE_EXISTS FALSE)
  endif()

  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_STANDARDS)
  list(GET FSIM_FIELDS 2 FSIM_PROFILES)
  list(GET FSIM_FIELDS 3 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 4 FSIM_STATE)
  list(GET FSIM_FIELDS 5 FSIM_CHANGE_ID)
  list(GET FSIM_FIELDS 6 FSIM_OBLIGATION)
  if(NOT FSIM_ID STREQUAL "COVBASE-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_STANDARDS STREQUAL FSIM_EXPECTED_STANDARDS OR
     NOT FSIM_PROFILES STREQUAL FSIM_EXPECTED_PROFILES OR
     NOT FSIM_DOMAIN STREQUAL FSIM_EXPECTED_DOMAIN OR
     NOT FSIM_STATE STREQUAL FSIM_EXPECTED_STATE OR
     NOT FSIM_CHANGE_ID STREQUAL "B178-C${FSIM_CHANGE_TEXT}")
    message(FATAL_ERROR
      "${FSIM_ID} has a drifted standard, profile, domain, state, or change")
  endif()
  string(LENGTH "${FSIM_OBLIGATION}" FSIM_OBLIGATION_LENGTH)
  string(TOLOWER "${FSIM_OBLIGATION}" FSIM_OBLIGATION_LOWER)
  string(FIND "${FSIM_OBLIGATION_LOWER}" "clause" FSIM_CLAUSE_OFFSET)
  if(FSIM_OBLIGATION_LENGTH LESS 32 OR
     FSIM_OBLIGATION_LENGTH GREATER 240 OR
     NOT FSIM_CLAUSE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "${FSIM_ID} lost its bounded clause-neutral obligation")
  endif()

  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE_ID)
  list(FIND FSIM_CHANGES "${FSIM_CHANGE_ID}" FSIM_DUPLICATE_CHANGE)
  list(FIND FSIM_DOMAINS "${FSIM_DOMAIN}" FSIM_DUPLICATE_DOMAIN)
  if(NOT FSIM_DUPLICATE_ID EQUAL -1 OR
     NOT FSIM_DUPLICATE_CHANGE EQUAL -1 OR
     NOT FSIM_DUPLICATE_DOMAIN EQUAL -1)
    message(FATAL_ERROR
      "duplicate code coverage inventory identity: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_CHANGES "${FSIM_CHANGE_ID}")
  list(APPEND FSIM_DOMAINS "${FSIM_DOMAIN}")

  list(GET FSIM_FIELDS 7 FSIM_IMPLEMENTATION_OWNER)
  fsim_require_code_coverage_owner("${FSIM_ID}" implementation
    "${FSIM_IMPLEMENTATION_OWNER}" "(include|src|cmake)/"
    "${FSIM_REQUIRE_EXISTS}")
  foreach(FSIM_EVIDENCE_INDEX RANGE 8 12)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE_OWNER)
    set(FSIM_REQUIRE_EVIDENCE_EXISTS FALSE)
    if(FSIM_REQUIRE_EXISTS AND FSIM_EVIDENCE_INDEX LESS 10)
      set(FSIM_REQUIRE_EVIDENCE_EXISTS TRUE)
    elseif(FSIM_COMPLETED_CHANGE GREATER_EQUAL 19)
      set(FSIM_REQUIRE_EVIDENCE_EXISTS TRUE)
    endif()
    fsim_require_code_coverage_owner("${FSIM_ID}"
      "evidence-${FSIM_EVIDENCE_INDEX}" "${FSIM_EVIDENCE_OWNER}" "tests/"
      "${FSIM_REQUIRE_EVIDENCE_EXISTS}")
  endforeach()
  list(GET FSIM_FIELDS 13 FSIM_DIAGNOSTIC_OWNER)
  list(GET FSIM_FIELDS 14 FSIM_RESOURCE_OWNER)
  if(NOT FSIM_DIAGNOSTIC_OWNER STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_RESOURCE_OWNER STREQUAL
       "cmake/CheckResourcePortabilityContract.cmake" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_DIAGNOSTIC_OWNER}" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_RESOURCE_OWNER}")
    message(FATAL_ERROR
      "${FSIM_ID} drifted diagnostic or resource ownership")
  endif()

  math(EXPR FSIM_ROW_COUNT "${FSIM_ROW_COUNT} + 1")
  if(FSIM_STATE STREQUAL active)
    math(EXPR FSIM_ACTIVE_COUNT "${FSIM_ACTIVE_COUNT} + 1")
  else()
    math(EXPR FSIM_PRESERVED_COUNT "${FSIM_PRESERVED_COUNT} + 1")
  endif()
endforeach()

list(LENGTH FSIM_IDS FSIM_ID_COUNT)
list(LENGTH FSIM_CHANGES FSIM_CHANGE_COUNT)
list(LENGTH FSIM_DOMAINS FSIM_DOMAIN_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 17 OR NOT FSIM_ID_COUNT EQUAL 17 OR
   NOT FSIM_CHANGE_COUNT EQUAL 17 OR NOT FSIM_DOMAIN_COUNT EQUAL 17 OR
   NOT FSIM_ACTIVE_COUNT EQUAL 0 OR NOT FSIM_PRESERVED_COUNT EQUAL 17)
  message(FATAL_ERROR
    "code coverage inventory requires seventeen preserved Changes 2-18")
endif()

message(STATUS
  "code coverage inventory passed: rows=${FSIM_ROW_COUNT} active=${FSIM_ACTIVE_COUNT} preserved=${FSIM_PRESERVED_COUNT} standards=${FSIM_EXPECTED_STANDARDS} profiles=${FSIM_EXPECTED_PROFILES} digest=${FSIM_ACTUAL_DIGEST}")

# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/code_coverage_metrics_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v3.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_FEATURE_README
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/README.md")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_INVENTORY}" "${FSIM_PLAN}" "${FSIM_TEST_CMAKE}"
    "${FSIM_FEATURE_README}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR
      "code coverage metrics inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_FEATURE_README}" FSIM_FEATURE_README_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "Batch 179 starts with the exact 17-row"
    "code_coverage_metrics_inventory.tsv"
    "MC/DC is explicitly"
    "Change 20 freezes the complete broad-metric set"
    "e145b9139cf58989ddbd683ff5b478c966d473944684ea11b60cde661dba7443")
  string(FIND "${FSIM_FEATURE_README_TEXT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "code coverage metrics feature contract lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_INVENTORY}" FSIM_INVENTORY_TEXT)
string(REPLACE "\r\n" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(REPLACE "\r" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(SHA256 FSIM_ACTUAL_DIGEST "${FSIM_INVENTORY_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "e145b9139cf58989ddbd683ff5b478c966d473944684ea11b60cde661dba7443")
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "code coverage metrics inventory digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

string(TOLOWER "${FSIM_INVENTORY_TEXT}" FSIM_INVENTORY_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "/home/" "standards/" "file://" "mc/dc" "mcdc")
  string(FIND "${FSIM_INVENTORY_LOWER}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "code coverage metrics inventory records forbidden private, absolute, or deferred-metric text")
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
  "### Batch 179 - condition, expression, toggle, and FSM metrics"
  FSIM_BATCH_START)
string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 180 - unified coverage database, standard API, and reports"
  FSIM_BATCH_END)
if(FSIM_BATCH_START EQUAL -1 OR FSIM_BATCH_END EQUAL -1 OR
   FSIM_BATCH_END LESS_EQUAL FSIM_BATCH_START)
  message(FATAL_ERROR "authoritative Batch 179 plan boundary is missing")
endif()
math(EXPR FSIM_BATCH_LENGTH "${FSIM_BATCH_END} - ${FSIM_BATCH_START}")
string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_BATCH_START}
  ${FSIM_BATCH_LENGTH} FSIM_BATCH_TEXT)
foreach(FSIM_CHANGE RANGE 1 20)
  set(FSIM_CHANGE_TOKEN "\n${FSIM_CHANGE}. ")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_CHANGE_TOKEN}"
    FSIM_CHANGE_OFFSET)
  if(FSIM_CHANGE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 179 plan lost exact Change ${FSIM_CHANGE}")
  endif()
  math(EXPR FSIM_REMAINDER_START "${FSIM_CHANGE_OFFSET} + 1")
  string(SUBSTRING "${FSIM_BATCH_TEXT}" ${FSIM_REMAINDER_START} -1
    FSIM_BATCH_REMAINDER)
  string(FIND "${FSIM_BATCH_REMAINDER}" "${FSIM_CHANGE_TOKEN}"
    FSIM_DUPLICATE_OFFSET)
  if(NOT FSIM_DUPLICATE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 179 plan duplicates Change ${FSIM_CHANGE}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "Register exact condition, expression, toggle, and FSM obligations."
    "Prove metric semantics across generate instances, engines, and mixed designs."
    "20. **Complete.** Run standard batch closure and freeze the broad metric set; MC/DC remains excluded."
    "Run standard batch closure and freeze the broad metric set; MC/DC remains excluded."
    "Batch 180 and Batch 190 remain tenth-batch sanitizer and hosted-CI")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.code-coverage-metrics-inventory"
    "CheckCodeCoverageMetricsInventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "code coverage metrics inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_EXPECTED_HEADER
  "id\tstandards\thdl_profiles\tdomain\tstate\tchange\tobligation\timplementation_owner\tpositive_evidence\tnegative_evidence\tengine_evidence\taggregation_evidence\tartifact_evidence\tdiagnostic_owner\tresource_owner")
set(FSIM_EXPECTED_STANDARDS "IEEE1076,IEEE1364,IEEE1800")
set(FSIM_EXPECTED_PROFILES
  "VHDL87,VHDL93,VHDL2000,VHDL2002,VHDL2008,V1995,V2001,V2001NoConfig,V2005,SV2005,SV2009,SV2012,SV2017")
set(FSIM_EXPECTED_DOMAINS
  verilog-condition-decomposition
  vhdl-condition-decomposition
  short-circuit-recording
  four-state-outcomes
  expression-combinations
  binary-toggle-bins
  verilog-toggle-objects
  vhdl-toggle-objects
  toggle-default-exclusions
  memory-array-selection
  unknown-toggle-diagnostics
  current-state-inference
  next-state-legal-sets
  systemverilog-fsm-pragmas
  vhdl-manifest-fsm-hints
  fsm-visits-transitions
  fsm-description-validation)
set(FSIM_COMPLETED_CHANGE 18)
set(FSIM_COMPLETED_INTEGRATION_CHANGE 19)
set(FSIM_COMPLETED_CLOSURE_CHANGE 20)

function(fsim_require_code_coverage_metrics_owner
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
    "code coverage metrics inventory SPDX policy or header changed")
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
      "code coverage metrics inventory row has ${FSIM_FIELD_COUNT} fields")
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
  if(NOT FSIM_ID STREQUAL "COVMET-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_STANDARDS STREQUAL FSIM_EXPECTED_STANDARDS OR
     NOT FSIM_PROFILES STREQUAL FSIM_EXPECTED_PROFILES OR
     NOT FSIM_DOMAIN STREQUAL FSIM_EXPECTED_DOMAIN OR
     NOT FSIM_STATE STREQUAL FSIM_EXPECTED_STATE OR
     NOT FSIM_CHANGE_ID STREQUAL "B179-C${FSIM_CHANGE_TEXT}")
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
      "${FSIM_ID} lost its bounded independently worded obligation")
  endif()

  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE_ID)
  list(FIND FSIM_CHANGES "${FSIM_CHANGE_ID}" FSIM_DUPLICATE_CHANGE)
  list(FIND FSIM_DOMAINS "${FSIM_DOMAIN}" FSIM_DUPLICATE_DOMAIN)
  if(NOT FSIM_DUPLICATE_ID EQUAL -1 OR
     NOT FSIM_DUPLICATE_CHANGE EQUAL -1 OR
     NOT FSIM_DUPLICATE_DOMAIN EQUAL -1)
    message(FATAL_ERROR
      "duplicate code coverage metrics inventory identity: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_CHANGES "${FSIM_CHANGE_ID}")
  list(APPEND FSIM_DOMAINS "${FSIM_DOMAIN}")

  list(GET FSIM_FIELDS 7 FSIM_IMPLEMENTATION_OWNER)
  fsim_require_code_coverage_metrics_owner("${FSIM_ID}" implementation
    "${FSIM_IMPLEMENTATION_OWNER}" "(include|src|cmake)/"
    "${FSIM_REQUIRE_EXISTS}")
  foreach(FSIM_EVIDENCE_INDEX RANGE 8 12)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE_OWNER)
    if(FSIM_EVIDENCE_INDEX LESS_EQUAL 9 OR
       FSIM_COMPLETED_INTEGRATION_CHANGE GREATER_EQUAL 19)
      set(FSIM_EVIDENCE_REQUIRE_EXISTS "${FSIM_REQUIRE_EXISTS}")
    else()
      # Engine, aggregation, and artifact witnesses are intentionally owned by
      # later Batch 179 integration changes. Their safe paths are registered
      # now, but must not be replaced by placeholder files merely to close a
      # focused language-decomposition change.
      set(FSIM_EVIDENCE_REQUIRE_EXISTS FALSE)
    endif()
    fsim_require_code_coverage_metrics_owner("${FSIM_ID}"
      "evidence-${FSIM_EVIDENCE_INDEX}" "${FSIM_EVIDENCE_OWNER}" "tests/"
      "${FSIM_EVIDENCE_REQUIRE_EXISTS}")
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

  if(FSIM_STATE STREQUAL "active")
    math(EXPR FSIM_ACTIVE_COUNT "${FSIM_ACTIVE_COUNT} + 1")
  elseif(FSIM_STATE STREQUAL "preserved")
    math(EXPR FSIM_PRESERVED_COUNT "${FSIM_PRESERVED_COUNT} + 1")
  else()
    message(FATAL_ERROR "${FSIM_ID} has unsupported state ${FSIM_STATE}")
  endif()
  math(EXPR FSIM_ROW_COUNT "${FSIM_ROW_COUNT} + 1")
endforeach()

math(EXPR FSIM_EXPECTED_PRESERVED "${FSIM_COMPLETED_CHANGE} - 1")
math(EXPR FSIM_EXPECTED_ACTIVE "17 - ${FSIM_EXPECTED_PRESERVED}")
if(NOT FSIM_ROW_COUNT EQUAL 17 OR
   NOT FSIM_ACTIVE_COUNT EQUAL FSIM_EXPECTED_ACTIVE OR
   NOT FSIM_PRESERVED_COUNT EQUAL FSIM_EXPECTED_PRESERVED)
  message(FATAL_ERROR
    "code coverage metrics inventory expected ${FSIM_EXPECTED_ACTIVE} active and ${FSIM_EXPECTED_PRESERVED} preserved rows; got rows=${FSIM_ROW_COUNT}, active=${FSIM_ACTIVE_COUNT}, preserved=${FSIM_PRESERVED_COUNT}")
endif()

message(STATUS
  "code coverage metrics inventory passed: rows=${FSIM_ROW_COUNT} active=${FSIM_ACTIVE_COUNT} preserved=${FSIM_PRESERVED_COUNT} integration=${FSIM_COMPLETED_INTEGRATION_CHANGE} closure=${FSIM_COMPLETED_CLOSURE_CHANGE} digest=${FSIM_ACTUAL_DIGEST}")

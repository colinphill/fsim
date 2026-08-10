# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_psl_gap_inventory.tsv")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS "${FSIM_INVENTORY}" "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "VHDL/PSL gap-inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 35)
  message(FATAL_ERROR
    "VHDL/PSL gap inventory must contain SPDX, one header, and 33 rows; "
    "got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_HEADER_ROW)
if(NOT FSIM_HEADER_ROW STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "VHDL/PSL gap inventory lost its SPDX policy")
endif()
list(GET FSIM_ROWS 1 FSIM_HEADER_ROW)
if(NOT FSIM_HEADER_ROW STREQUAL
    "id\tstandard\tclause\tboundary\tclosure\tfeature\tparser_owner\tanalyzer_owner\telaboration_owner\truntime_owner\tpositive_evidence\tnegative_evidence\texecution_evidence\tdiagnostic_owner\tresource_owner")
  message(FATAL_ERROR "VHDL/PSL gap inventory header changed")
endif()

set(FSIM_IDS)
set(FSIM_SUPPORTED 0)
set(FSIM_UNSUPPORTED 0)
set(FSIM_DEFERRED 0)
set(FSIM_BASELINE_CLAUSES)
set(FSIM_CLOSURES)
foreach(FSIM_INDEX RANGE 2 34)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 15)
    message(FATAL_ERROR
      "VHDL/PSL row ${FSIM_INDEX} does not have fifteen fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_STANDARD)
  list(GET FSIM_FIELDS 2 FSIM_CLAUSE)
  list(GET FSIM_FIELDS 3 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 4 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 5 FSIM_FEATURE)
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_ID_INDEX)
  if(NOT FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate VHDL/PSL inventory id: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  if(FSIM_STANDARD STREQUAL "" OR FSIM_CLAUSE STREQUAL "" OR
     FSIM_FEATURE STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has an empty scope field")
  endif()

  if(FSIM_BOUNDARY STREQUAL "supported")
    math(EXPR FSIM_SUPPORTED "${FSIM_SUPPORTED} + 1")
    if(FSIM_CLOSURE STREQUAL "baseline")
      if(NOT FSIM_FEATURE MATCHES "^Reviewed ")
        message(FATAL_ERROR "${FSIM_ID} baseline row is not review-recorded")
      endif()
      if(FSIM_STANDARD STREQUAL "IEEE1076-2008")
        list(APPEND FSIM_BASELINE_CLAUSES "${FSIM_CLAUSE}")
      endif()
    elseif(FSIM_CLOSURE MATCHES "^B163-C(0[2-9]|1[0-6])$")
      if(NOT FSIM_FEATURE MATCHES "^Closed ")
        message(FATAL_ERROR "${FSIM_ID} closure row is not closure-recorded")
      endif()
      list(APPEND FSIM_CLOSURES "${FSIM_CLOSURE}")
    else()
      message(FATAL_ERROR "${FSIM_ID} supported row has no exact closure owner")
    endif()
  elseif(FSIM_BOUNDARY STREQUAL "unsupported")
    math(EXPR FSIM_UNSUPPORTED "${FSIM_UNSUPPORTED} + 1")
    message(FATAL_ERROR "${FSIM_ID} remains an unresolved active row")
  elseif(FSIM_BOUNDARY STREQUAL "deferred")
    math(EXPR FSIM_DEFERRED "${FSIM_DEFERRED} + 1")
    if(NOT FSIM_CLOSURE STREQUAL "B170" AND
       NOT FSIM_CLOSURE STREQUAL "post-v2")
      message(FATAL_ERROR "${FSIM_ID} deferred row has no exact deferral owner")
    endif()
  else()
    message(FATAL_ERROR "${FSIM_ID} has unknown boundary ${FSIM_BOUNDARY}")
  endif()

  foreach(FSIM_OWNER_INDEX RANGE 6 14)
    list(GET FSIM_FIELDS ${FSIM_OWNER_INDEX} FSIM_OWNER)
    if(FSIM_OWNER STREQUAL "" OR
       NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_OWNER}")
      message(FATAL_ERROR
        "${FSIM_ID} lost owner field ${FSIM_OWNER_INDEX}: ${FSIM_OWNER}")
    endif()
  endforeach()
  if(FSIM_BOUNDARY STREQUAL "supported")
    foreach(FSIM_EVIDENCE_INDEX RANGE 10 12)
      list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE)
      if(NOT FSIM_EVIDENCE MATCHES "^tests/")
        message(FATAL_ERROR
          "${FSIM_ID} evidence field ${FSIM_EVIDENCE_INDEX} is not a test")
      endif()
    endforeach()
  endif()
  list(GET FSIM_FIELDS 6 FSIM_PARSER)
  list(GET FSIM_FIELDS 7 FSIM_ANALYZER)
  list(GET FSIM_FIELDS 8 FSIM_ELABORATION)
  list(GET FSIM_FIELDS 9 FSIM_RUNTIME)
  list(GET FSIM_FIELDS 13 FSIM_DIAGNOSTIC)
  list(GET FSIM_FIELDS 14 FSIM_RESOURCE)
  if(NOT FSIM_PARSER MATCHES "^src/frontend/" OR
     NOT FSIM_ANALYZER MATCHES "^src/app/" OR
     NOT FSIM_ELABORATION MATCHES "^src/elaboration/" OR
     NOT FSIM_RUNTIME MATCHES "^src/(app|runtime)/" OR
     NOT FSIM_DIAGNOSTIC STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_RESOURCE STREQUAL
       "cmake/CheckResourcePortabilityContract.cmake")
    message(FATAL_ERROR "${FSIM_ID} has a misplaced implementation owner")
  endif()
endforeach()

if(NOT FSIM_SUPPORTED EQUAL 29 OR NOT FSIM_UNSUPPORTED EQUAL 0 OR
   NOT FSIM_DEFERRED EQUAL 4)
  message(FATAL_ERROR
    "expected 29 supported, 0 unresolved, and 4 deferred rows; found "
    "${FSIM_SUPPORTED}/${FSIM_UNSUPPORTED}/${FSIM_DEFERRED}")
endif()
foreach(FSIM_EXPECTED_CLOSURE IN ITEMS
    B163-C02 B163-C03 B163-C04 B163-C05 B163-C06 B163-C07 B163-C08
    B163-C09 B163-C10 B163-C11 B163-C12 B163-C13 B163-C14 B163-C15
    B163-C16)
  list(FIND FSIM_CLOSURES "${FSIM_EXPECTED_CLOSURE}" FSIM_CLOSURE_INDEX)
  if(FSIM_CLOSURE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL inventory omits ${FSIM_EXPECTED_CLOSURE}")
  endif()
endforeach()
list(REMOVE_DUPLICATES FSIM_CLOSURES)
list(LENGTH FSIM_CLOSURES FSIM_CLOSURE_COUNT)
if(NOT FSIM_CLOSURE_COUNT EQUAL 15)
  message(FATAL_ERROR "VHDL/PSL inventory duplicates a Batch 163 closure owner")
endif()
foreach(FSIM_CLAUSE RANGE 2 15)
  list(FIND FSIM_BASELINE_CLAUSES "${FSIM_CLAUSE}" FSIM_CLAUSE_INDEX)
  if(FSIM_CLAUSE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "IEEE 1076-2008 supported baseline omits clause ${FSIM_CLAUSE}")
  endif()
endforeach()

file(READ "${FSIM_INVENTORY}" FSIM_CONTENTS)
string(TOLOWER "${FSIM_CONTENTS}" FSIM_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "xfail" "expected-fail" "waiver" "allowlist" "suppress")
  string(FIND "${FSIM_LOWER}" "${FSIM_FORBIDDEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL inventory contains forbidden escape ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.vhdl-psl-gap-inventory"
    "CheckVhdlPslGapInventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "VHDL/PSL gap inventory: 33 rows split 29 supported, 0 unresolved, "
  "4 deferred; IEEE 1076-2008 clauses 2-15 and all evidence owners are frozen")

# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_gap_inventory.tsv")
set(FSIM_WIDTH_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_literal_width_inventory.tsv")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v2.md")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_INVENTORY}"
    "${FSIM_WIDTH_INVENTORY}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_PLAN}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "Verilog gap-inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 39)
  message(FATAL_ERROR
    "Verilog gap inventory must contain SPDX, one header, and 37 rows; "
    "got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_HEADER_ROW)
if(NOT FSIM_HEADER_ROW STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "Verilog gap inventory lost its SPDX policy")
endif()
list(GET FSIM_ROWS 1 FSIM_HEADER_ROW)
if(NOT FSIM_HEADER_ROW STREQUAL
    "id\tstandard\tclause\tboundary\tclosure\tfeature\tparser_owner\tanalyzer_owner\telaboration_owner\truntime_owner\tpositive_evidence\tnegative_evidence\texecution_evidence\tdiagnostic_owner\tresource_owner")
  message(FATAL_ERROR "Verilog gap inventory header changed")
endif()

set(FSIM_EXPECTED_B164_C02 "3,4,Annex-A,Annex-B")
set(FSIM_EXPECTED_B164_C03 "19,28,Annex-H")
set(FSIM_EXPECTED_B164_C04 "4,12.2")
set(FSIM_EXPECTED_B164_C05 "5")
set(FSIM_EXPECTED_B164_C06 "12,13")
set(FSIM_EXPECTED_B164_C07 "7,8")
set(FSIM_EXPECTED_B164_C08 "7")
set(FSIM_EXPECTED_B164_C09 "6,7.14")
set(FSIM_EXPECTED_B164_C10 "6,9.2,9.3")
set(FSIM_EXPECTED_B164_C11 "9,11")
set(FSIM_EXPECTED_B164_C12 "10,17")
set(FSIM_EXPECTED_B164_C13 "4.9,17.2")
set(FSIM_EXPECTED_B164_C14 "14,15")
set(FSIM_EXPECTED_B164_C15 "20,26,27,Annex-G")
set(FSIM_EXPECTED_B164_C16 "18,integration")

set(FSIM_IDS)
set(FSIM_SUPPORTED 0)
set(FSIM_UNSUPPORTED 0)
set(FSIM_DEFERRED 0)
set(FSIM_BASELINE_CLAUSES)
set(FSIM_CLOSURES)
foreach(FSIM_INDEX RANGE 2 38)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 15)
    message(FATAL_ERROR
      "Verilog row ${FSIM_INDEX} does not have fifteen fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_STANDARD)
  list(GET FSIM_FIELDS 2 FSIM_CLAUSE)
  list(GET FSIM_FIELDS 3 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 4 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 5 FSIM_FEATURE)
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_ID_INDEX)
  if(NOT FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate Verilog inventory id: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  if(NOT FSIM_STANDARD STREQUAL "IEEE1364-2005" OR
     FSIM_CLAUSE STREQUAL "" OR FSIM_FEATURE STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has an empty or drifted scope field")
  endif()

  if(FSIM_BOUNDARY STREQUAL "supported")
    math(EXPR FSIM_SUPPORTED "${FSIM_SUPPORTED} + 1")
    if(FSIM_CLOSURE STREQUAL "baseline" AND
       FSIM_FEATURE MATCHES "^Reviewed ")
      list(APPEND FSIM_BASELINE_CLAUSES "${FSIM_CLAUSE}")
    elseif(FSIM_CLOSURE MATCHES "^B164-C(0[2-9]|1[0-6])$" AND
           FSIM_FEATURE MATCHES "^Closed ")
      string(REPLACE "-" "_" FSIM_CLOSURE_KEY "${FSIM_CLOSURE}")
      set(FSIM_EXPECTED_CLAUSE
        "${FSIM_EXPECTED_${FSIM_CLOSURE_KEY}}")
      if(NOT FSIM_CLAUSE STREQUAL FSIM_EXPECTED_CLAUSE)
        message(FATAL_ERROR
          "${FSIM_ID} drifted ${FSIM_CLOSURE} from clause scope "
          "${FSIM_EXPECTED_CLAUSE} to ${FSIM_CLAUSE}")
      endif()
      list(APPEND FSIM_CLOSURES "${FSIM_CLOSURE}")
    else()
      message(FATAL_ERROR
        "${FSIM_ID} supported row is neither reviewed nor closed")
    endif()
  elseif(FSIM_BOUNDARY STREQUAL "unsupported")
    math(EXPR FSIM_UNSUPPORTED "${FSIM_UNSUPPORTED} + 1")
    if(NOT FSIM_CLOSURE MATCHES "^B164-C(0[2-9]|1[0-6])$" OR
       NOT FSIM_FEATURE MATCHES "^Close ")
      message(FATAL_ERROR "${FSIM_ID} active row has no exact closure owner")
    endif()
    string(REPLACE "-" "_" FSIM_CLOSURE_KEY "${FSIM_CLOSURE}")
    set(FSIM_EXPECTED_CLAUSE
      "${FSIM_EXPECTED_${FSIM_CLOSURE_KEY}}")
    if(NOT FSIM_CLAUSE STREQUAL FSIM_EXPECTED_CLAUSE)
      message(FATAL_ERROR
        "${FSIM_ID} drifted ${FSIM_CLOSURE} from clause scope "
        "${FSIM_EXPECTED_CLAUSE} to ${FSIM_CLAUSE}")
    endif()
    list(APPEND FSIM_CLOSURES "${FSIM_CLOSURE}")
  elseif(FSIM_BOUNDARY STREQUAL "deferred")
    math(EXPR FSIM_DEFERRED "${FSIM_DEFERRED} + 1")
    if(NOT FSIM_CLOSURE STREQUAL "B170" AND
       NOT FSIM_CLOSURE STREQUAL "post-v2")
      message(FATAL_ERROR "${FSIM_ID} deferred row has no exact owner")
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
  if(FSIM_BOUNDARY STREQUAL "deferred")
    foreach(FSIM_EVIDENCE_INDEX RANGE 10 12)
      list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE)
      if(NOT FSIM_EVIDENCE MATCHES "^docs/")
        message(FATAL_ERROR
          "${FSIM_ID} deferred evidence field ${FSIM_EVIDENCE_INDEX} "
          "is not an explicit document owner")
      endif()
    endforeach()
  else()
    foreach(FSIM_EVIDENCE_INDEX RANGE 10 12)
      list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE)
      if(NOT FSIM_EVIDENCE MATCHES "^tests/")
        message(FATAL_ERROR
          "${FSIM_ID} active evidence field ${FSIM_EVIDENCE_INDEX} "
          "is not a test")
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

if(NOT FSIM_SUPPORTED EQUAL 34 OR NOT FSIM_UNSUPPORTED EQUAL 0 OR
   NOT FSIM_DEFERRED EQUAL 3)
  message(FATAL_ERROR
    "expected 34 supported, 0 active, and 3 deferred rows; found "
    "${FSIM_SUPPORTED}/${FSIM_UNSUPPORTED}/${FSIM_DEFERRED}")
endif()
foreach(FSIM_EXPECTED_CLOSURE IN ITEMS
    B164-C02 B164-C03 B164-C04 B164-C05 B164-C06 B164-C07 B164-C08
    B164-C09 B164-C10 B164-C11 B164-C12 B164-C13 B164-C14 B164-C15
    B164-C16)
  list(FIND FSIM_CLOSURES "${FSIM_EXPECTED_CLOSURE}" FSIM_CLOSURE_INDEX)
  if(FSIM_CLOSURE_INDEX EQUAL -1)
    message(FATAL_ERROR "Verilog inventory omits ${FSIM_EXPECTED_CLOSURE}")
  endif()
endforeach()
list(REMOVE_DUPLICATES FSIM_CLOSURES)
list(LENGTH FSIM_CLOSURES FSIM_CLOSURE_COUNT)
if(NOT FSIM_CLOSURE_COUNT EQUAL 15)
  message(FATAL_ERROR "Verilog inventory duplicates a Batch 164 closure owner")
endif()
foreach(FSIM_CLAUSE IN ITEMS
    3 4 5 6 7 8 9 10 11 12 13 14 15 17 18 19 20 26 27)
  list(FIND FSIM_BASELINE_CLAUSES "${FSIM_CLAUSE}" FSIM_CLAUSE_INDEX)
  if(FSIM_CLAUSE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "IEEE 1364-2005 supported baseline omits clause ${FSIM_CLAUSE}")
  endif()
endforeach()

file(READ "${FSIM_INVENTORY}" FSIM_CONTENTS)
foreach(FSIM_DEFERRED_TOKEN IN ITEMS
    "DEF-VLOG-SDF\tIEEE1364-2005\t16\tdeferred\tB170"
    "DEF-VLOG-TFACC\tIEEE1364-2005\t21,22,23,24,25\tdeferred\tpost-v2"
    "DEF-VLOG-OPTIONAL\tIEEE1364-2005\tAnnex-C,Annex-D\tdeferred\tpost-v2")
  string(FIND "${FSIM_CONTENTS}" "${FSIM_DEFERRED_TOKEN}"
    FSIM_DEFERRED_INDEX)
  if(FSIM_DEFERRED_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog inventory lost frozen deferral: ${FSIM_DEFERRED_TOKEN}")
  endif()
endforeach()

file(STRINGS "${FSIM_WIDTH_INVENTORY}" FSIM_WIDTH_ROWS)
list(LENGTH FSIM_WIDTH_ROWS FSIM_WIDTH_ROW_COUNT)
if(NOT FSIM_WIDTH_ROW_COUNT EQUAL 17)
  message(FATAL_ERROR
    "Verilog literal-width inventory must contain SPDX, one header, and "
    "15 rows; got ${FSIM_WIDTH_ROW_COUNT}")
endif()
list(GET FSIM_WIDTH_ROWS 0 FSIM_WIDTH_HEADER)
if(NOT FSIM_WIDTH_HEADER STREQUAL
    "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "Verilog literal-width inventory lost its SPDX policy")
endif()
list(GET FSIM_WIDTH_ROWS 1 FSIM_WIDTH_HEADER)
if(NOT FSIM_WIDTH_HEADER STREQUAL
    "id\tstage\tdisposition\tclosure\tassumption\timplementation_owner\tsource_anchor\tpositive_evidence\tnegative_evidence\texecution_evidence\tdiagnostic_owner\tresource_owner")
  message(FATAL_ERROR "Verilog literal-width inventory header changed")
endif()

set(FSIM_WIDTH_EXPECTATIONS
  "VLW-LEXER-TEXT|preserved|baseline"
  "VLW-PARSER-WIDTH|preserved|B164-C02"
  "VLW-PARSER-OUTPUT|preserved|B164-C02"
  "VLW-MATERIALIZATION|physical|resource-policy"
  "VLW-LEGACY-INTEGER|preserved|B164-C04"
  "VLW-PACKED-CONSTANT|preserved|baseline"
  "VLW-LOW-WORD|preserved|B164-C05"
  "VLW-PACKED-STORAGE|preserved|baseline"
  "VLW-ENGINE-PLANES|preserved|B164-C05"
  "VLW-SCALAR-HOST|physical|host-api"
  "VLW-VPI-VECTOR|preserved|B164-C15"
  "VLW-VPI-INTEGER|physical|host-api"
  "VLW-MEMORY-WORD|preserved|B164-C13"
  "VLW-TRACE-PUBLIC|preserved|B164-C16"
  "VLW-ARTIFACT-CACHE|preserved|B164-C17")
set(FSIM_WIDTH_IDS)
set(FSIM_WIDTH_ANCHORS)
set(FSIM_WIDTH_PRESERVED 0)
set(FSIM_WIDTH_ACTIVE 0)
set(FSIM_WIDTH_PHYSICAL 0)
foreach(FSIM_INDEX RANGE 2 16)
  list(GET FSIM_WIDTH_ROWS ${FSIM_INDEX} FSIM_WIDTH_ROW)
  string(REPLACE "\t" ";" FSIM_WIDTH_FIELDS "${FSIM_WIDTH_ROW}")
  list(LENGTH FSIM_WIDTH_FIELDS FSIM_WIDTH_FIELD_COUNT)
  if(NOT FSIM_WIDTH_FIELD_COUNT EQUAL 12)
    message(FATAL_ERROR
      "Verilog literal-width row ${FSIM_INDEX} does not have twelve fields")
  endif()
  list(GET FSIM_WIDTH_FIELDS 0 FSIM_WIDTH_ID)
  list(GET FSIM_WIDTH_FIELDS 1 FSIM_WIDTH_STAGE)
  list(GET FSIM_WIDTH_FIELDS 2 FSIM_WIDTH_DISPOSITION)
  list(GET FSIM_WIDTH_FIELDS 3 FSIM_WIDTH_CLOSURE)
  list(GET FSIM_WIDTH_FIELDS 4 FSIM_WIDTH_ASSUMPTION)
  list(GET FSIM_WIDTH_FIELDS 5 FSIM_WIDTH_OWNER)
  list(GET FSIM_WIDTH_FIELDS 6 FSIM_WIDTH_ANCHOR)
  if(FSIM_WIDTH_STAGE STREQUAL "" OR FSIM_WIDTH_ASSUMPTION STREQUAL "" OR
     FSIM_WIDTH_ANCHOR STREQUAL "")
    message(FATAL_ERROR "${FSIM_WIDTH_ID} has an empty width-audit field")
  endif()
  list(FIND FSIM_WIDTH_IDS "${FSIM_WIDTH_ID}" FSIM_WIDTH_ID_INDEX)
  list(FIND FSIM_WIDTH_ANCHORS "${FSIM_WIDTH_ANCHOR}"
    FSIM_WIDTH_ANCHOR_INDEX)
  if(NOT FSIM_WIDTH_ID_INDEX EQUAL -1 OR
     NOT FSIM_WIDTH_ANCHOR_INDEX EQUAL -1)
    message(FATAL_ERROR
      "duplicate Verilog literal-width id or anchor: ${FSIM_WIDTH_ID}")
  endif()
  list(APPEND FSIM_WIDTH_IDS "${FSIM_WIDTH_ID}")
  list(APPEND FSIM_WIDTH_ANCHORS "${FSIM_WIDTH_ANCHOR}")

  set(FSIM_WIDTH_EXPECTED "")
  foreach(FSIM_EXPECTATION IN LISTS FSIM_WIDTH_EXPECTATIONS)
    if(FSIM_EXPECTATION MATCHES "^${FSIM_WIDTH_ID}\\|")
      set(FSIM_WIDTH_EXPECTED "${FSIM_EXPECTATION}")
    endif()
  endforeach()
  if(FSIM_WIDTH_EXPECTED STREQUAL "")
    message(FATAL_ERROR "unregistered Verilog width row: ${FSIM_WIDTH_ID}")
  endif()
  string(REPLACE "|" ";" FSIM_WIDTH_EXPECTED_FIELDS
    "${FSIM_WIDTH_EXPECTED}")
  list(GET FSIM_WIDTH_EXPECTED_FIELDS 1 FSIM_EXPECTED_DISPOSITION)
  list(GET FSIM_WIDTH_EXPECTED_FIELDS 2 FSIM_EXPECTED_CLOSURE)
  if(NOT FSIM_WIDTH_DISPOSITION STREQUAL FSIM_EXPECTED_DISPOSITION OR
     NOT FSIM_WIDTH_CLOSURE STREQUAL FSIM_EXPECTED_CLOSURE)
    message(FATAL_ERROR
      "${FSIM_WIDTH_ID} drifted from ${FSIM_EXPECTED_DISPOSITION}/"
      "${FSIM_EXPECTED_CLOSURE} to ${FSIM_WIDTH_DISPOSITION}/"
      "${FSIM_WIDTH_CLOSURE}")
  endif()

  if(FSIM_WIDTH_DISPOSITION STREQUAL "preserved")
    math(EXPR FSIM_WIDTH_PRESERVED "${FSIM_WIDTH_PRESERVED} + 1")
  elseif(FSIM_WIDTH_DISPOSITION STREQUAL "active")
    math(EXPR FSIM_WIDTH_ACTIVE "${FSIM_WIDTH_ACTIVE} + 1")
  elseif(FSIM_WIDTH_DISPOSITION STREQUAL "physical")
    math(EXPR FSIM_WIDTH_PHYSICAL "${FSIM_WIDTH_PHYSICAL} + 1")
    string(TOLOWER "${FSIM_WIDTH_ASSUMPTION}" FSIM_WIDTH_ASSUMPTION_LOWER)
    if(NOT FSIM_WIDTH_ASSUMPTION_LOWER MATCHES "host|governed")
      message(FATAL_ERROR
        "${FSIM_WIDTH_ID} physical boundary lacks host or governance scope")
    endif()
  else()
    message(FATAL_ERROR
      "${FSIM_WIDTH_ID} has unknown disposition ${FSIM_WIDTH_DISPOSITION}")
  endif()

  foreach(FSIM_WIDTH_PATH_INDEX RANGE 5 11)
    if(FSIM_WIDTH_PATH_INDEX EQUAL 6)
      continue()
    endif()
    list(GET FSIM_WIDTH_FIELDS ${FSIM_WIDTH_PATH_INDEX} FSIM_WIDTH_PATH)
    if(FSIM_WIDTH_PATH STREQUAL "" OR
       NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_WIDTH_PATH}")
      message(FATAL_ERROR
        "${FSIM_WIDTH_ID} lost path field ${FSIM_WIDTH_PATH_INDEX}: "
        "${FSIM_WIDTH_PATH}")
    endif()
  endforeach()
  foreach(FSIM_WIDTH_EVIDENCE_INDEX RANGE 7 9)
    list(GET FSIM_WIDTH_FIELDS ${FSIM_WIDTH_EVIDENCE_INDEX}
      FSIM_WIDTH_EVIDENCE)
    if(NOT FSIM_WIDTH_EVIDENCE MATCHES "^tests/")
      message(FATAL_ERROR
        "${FSIM_WIDTH_ID} evidence field ${FSIM_WIDTH_EVIDENCE_INDEX} "
        "is not a test")
    endif()
  endforeach()
  list(GET FSIM_WIDTH_FIELDS 10 FSIM_WIDTH_DIAGNOSTIC)
  list(GET FSIM_WIDTH_FIELDS 11 FSIM_WIDTH_RESOURCE)
  if(NOT FSIM_WIDTH_OWNER MATCHES "^src/" OR
     NOT FSIM_WIDTH_DIAGNOSTIC STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_WIDTH_RESOURCE STREQUAL
       "cmake/CheckResourcePortabilityContract.cmake")
    message(FATAL_ERROR "${FSIM_WIDTH_ID} has a misplaced width owner")
  endif()
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_WIDTH_OWNER}" FSIM_OWNER_CONTENTS)
  string(FIND "${FSIM_OWNER_CONTENTS}" "${FSIM_WIDTH_ANCHOR}"
    FSIM_WIDTH_ANCHOR_OFFSET)
  if(FSIM_WIDTH_ANCHOR_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "${FSIM_WIDTH_ID} source anchor drifted: ${FSIM_WIDTH_ANCHOR}")
  endif()
endforeach()

list(LENGTH FSIM_WIDTH_EXPECTATIONS FSIM_WIDTH_EXPECTATION_COUNT)
list(LENGTH FSIM_WIDTH_IDS FSIM_WIDTH_ID_COUNT)
if(NOT FSIM_WIDTH_EXPECTATION_COUNT EQUAL FSIM_WIDTH_ID_COUNT OR
   NOT FSIM_WIDTH_PRESERVED EQUAL 12 OR NOT FSIM_WIDTH_ACTIVE EQUAL 0 OR
   NOT FSIM_WIDTH_PHYSICAL EQUAL 3)
  message(FATAL_ERROR
    "expected 15 width rows split 12 preserved, 0 active, and 3 physical; "
    "found ${FSIM_WIDTH_ID_COUNT} split ${FSIM_WIDTH_PRESERVED}/"
    "${FSIM_WIDTH_ACTIVE}/${FSIM_WIDTH_PHYSICAL}")
endif()
foreach(FSIM_WIDTH_EXPECTATION IN LISTS FSIM_WIDTH_EXPECTATIONS)
  string(REPLACE "|" ";" FSIM_WIDTH_EXPECTED_FIELDS
    "${FSIM_WIDTH_EXPECTATION}")
  list(GET FSIM_WIDTH_EXPECTED_FIELDS 0 FSIM_WIDTH_EXPECTED_ID)
  list(FIND FSIM_WIDTH_IDS "${FSIM_WIDTH_EXPECTED_ID}"
    FSIM_WIDTH_EXPECTED_INDEX)
  if(FSIM_WIDTH_EXPECTED_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog width inventory omits ${FSIM_WIDTH_EXPECTED_ID}")
  endif()
endforeach()

file(READ "${FSIM_WIDTH_INVENTORY}" FSIM_WIDTH_CONTENTS)
string(TOLOWER "${FSIM_CONTENTS}\n${FSIM_WIDTH_CONTENTS}" FSIM_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "xfail" "expected-fail" "waiver" "allowlist" "suppress")
  string(FIND "${FSIM_LOWER}" "${FSIM_FORBIDDEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog inventories contain forbidden escape ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.verilog-gap-inventory"
    "CheckVerilogGapInventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()
file(READ "${FSIM_PLAN}" FSIM_PLAN_CONTENTS)
foreach(FSIM_PLAN_TOKEN IN ITEMS
    "### Batch 164 - Verilog-2005 residual language closure"
    "Cross-cutting width requirement:"
    "separately inventory every literal-width cap or host-word"
    "assign Changes 2-16 one-to-one closure ownership")
  string(FIND "${FSIM_PLAN_CONTENTS}" "${FSIM_PLAN_TOKEN}"
    FSIM_PLAN_TOKEN_INDEX)
  if(FSIM_PLAN_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog inventory plan scope lost token: ${FSIM_PLAN_TOKEN}")
  endif()
endforeach()

message(STATUS
  "Verilog gap inventory: 37 rows split 34 supported, 0 active, and "
  "3 deferred; 15 width rows split 12 preserved, 0 active, and 3 physical")

# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/systemverilog_gap_inventory.tsv")
set(FSIM_WIDTH_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/systemverilog_literal_width_inventory.tsv")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v2.md")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_INVENTORY}"
    "${FSIM_WIDTH_INVENTORY}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_PLAN}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR
      "SystemVerilog gap-inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 37)
  message(FATAL_ERROR
    "SystemVerilog gap inventory must contain SPDX, one header, and 35 rows; "
    "got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_HEADER_ROW)
if(NOT FSIM_HEADER_ROW STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "SystemVerilog gap inventory lost its SPDX policy")
endif()
list(GET FSIM_ROWS 1 FSIM_HEADER_ROW)
if(NOT FSIM_HEADER_ROW STREQUAL
    "id\tstandard\tclause\tboundary\tclosure\tfeature\tparser_owner\tanalyzer_owner\telaboration_owner\truntime_owner\tpositive_evidence\tnegative_evidence\texecution_evidence\tdiagnostic_owner\tresource_owner")
  message(FATAL_ERROR "SystemVerilog gap inventory header changed")
endif()

set(FSIM_EXPECTED_B165_C02 "5,22,Annex-A,Annex-B")
set(FSIM_EXPECTED_B165_C03 "6,23,25,26")
set(FSIM_EXPECTED_B165_C04 "6,7,8,25")
set(FSIM_EXPECTED_B165_C05 "11")
set(FSIM_EXPECTED_B165_C06 "6,7,21")
set(FSIM_EXPECTED_B165_C07 "8,13,17,24,25")
set(FSIM_EXPECTED_B165_C08 "18")
set(FSIM_EXPECTED_B165_C09 "23,24,25,26,27,33")
set(FSIM_EXPECTED_B165_C10 "4,9,10,12,15")
set(FSIM_EXPECTED_B165_C11 "4,14,24,30,31")
set(FSIM_EXPECTED_B165_C12 "16,17,19")
set(FSIM_EXPECTED_B165_C13 "20,21")
set(FSIM_EXPECTED_B165_C14 "35,36,37,38,39,40")
set(FSIM_EXPECTED_B165_C15 "integration,UVM")
set(FSIM_EXPECTED_B165_C16 "integration")

set(FSIM_IDS)
set(FSIM_SUPPORTED 0)
set(FSIM_UNSUPPORTED 0)
set(FSIM_DEFERRED 0)
set(FSIM_BASELINE_SCOPES)
set(FSIM_ALL_SCOPES)
set(FSIM_CLOSURES)
foreach(FSIM_INDEX RANGE 2 36)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 15)
    message(FATAL_ERROR
      "SystemVerilog row ${FSIM_INDEX} does not have fifteen fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_STANDARD)
  list(GET FSIM_FIELDS 2 FSIM_CLAUSE)
  list(GET FSIM_FIELDS 3 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 4 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 5 FSIM_FEATURE)
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_ID_INDEX)
  if(NOT FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate SystemVerilog inventory id: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  if(NOT FSIM_STANDARD STREQUAL "IEEE1800-2017" OR
     FSIM_CLAUSE STREQUAL "" OR FSIM_FEATURE STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has an empty or drifted scope field")
  endif()
  list(APPEND FSIM_ALL_SCOPES "${FSIM_CLAUSE}")

  if(FSIM_BOUNDARY STREQUAL "supported")
    math(EXPR FSIM_SUPPORTED "${FSIM_SUPPORTED} + 1")
    if(FSIM_CLOSURE STREQUAL "baseline" AND
       FSIM_FEATURE MATCHES "^Reviewed ")
      list(APPEND FSIM_BASELINE_SCOPES "${FSIM_CLAUSE}")
    elseif(FSIM_CLOSURE MATCHES "^B165-C(0[2-9]|1[0-6])$" AND
           FSIM_FEATURE MATCHES "^Closed ")
      string(REPLACE "-" "_" FSIM_CLOSURE_KEY "${FSIM_CLOSURE}")
      set(FSIM_EXPECTED_CLAUSE "${FSIM_EXPECTED_${FSIM_CLOSURE_KEY}}")
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
    if(NOT FSIM_CLOSURE MATCHES "^B165-C(0[2-9]|1[0-6])$" OR
       NOT FSIM_FEATURE MATCHES "^Close ")
      message(FATAL_ERROR "${FSIM_ID} active row has no exact closure owner")
    endif()
    string(REPLACE "-" "_" FSIM_CLOSURE_KEY "${FSIM_CLOSURE}")
    set(FSIM_EXPECTED_CLAUSE "${FSIM_EXPECTED_${FSIM_CLOSURE_KEY}}")
    if(NOT FSIM_CLAUSE STREQUAL FSIM_EXPECTED_CLAUSE)
      message(FATAL_ERROR
        "${FSIM_ID} drifted ${FSIM_CLOSURE} from clause scope "
        "${FSIM_EXPECTED_CLAUSE} to ${FSIM_CLAUSE}")
    endif()
    list(APPEND FSIM_CLOSURES "${FSIM_CLOSURE}")
  elseif(FSIM_BOUNDARY STREQUAL "deferred")
    math(EXPR FSIM_DEFERRED "${FSIM_DEFERRED} + 1")
    if(NOT FSIM_CLOSURE MATCHES "^(B170|B171|B172-B173|post-v2)$")
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
     NOT FSIM_RUNTIME MATCHES "^src/" OR
     NOT FSIM_DIAGNOSTIC STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_RESOURCE STREQUAL
       "cmake/CheckResourcePortabilityContract.cmake")
    message(FATAL_ERROR "${FSIM_ID} has a misplaced implementation owner")
  endif()
endforeach()

if(NOT FSIM_SUPPORTED EQUAL 30 OR NOT FSIM_UNSUPPORTED EQUAL 0 OR
   NOT FSIM_DEFERRED EQUAL 5)
  message(FATAL_ERROR
    "expected 30 supported, 0 active, and 5 deferred rows; found "
    "${FSIM_SUPPORTED}/${FSIM_UNSUPPORTED}/${FSIM_DEFERRED}")
endif()
foreach(FSIM_EXPECTED_CLOSURE IN ITEMS
    B165-C02 B165-C03 B165-C04 B165-C05 B165-C06 B165-C07 B165-C08
    B165-C09 B165-C10 B165-C11 B165-C12 B165-C13 B165-C14 B165-C15
    B165-C16)
  list(FIND FSIM_CLOSURES "${FSIM_EXPECTED_CLOSURE}" FSIM_CLOSURE_INDEX)
  if(FSIM_CLOSURE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog inventory omits ${FSIM_EXPECTED_CLOSURE}")
  endif()
endforeach()
list(REMOVE_DUPLICATES FSIM_CLOSURES)
list(LENGTH FSIM_CLOSURES FSIM_CLOSURE_COUNT)
if(NOT FSIM_CLOSURE_COUNT EQUAL 15)
  message(FATAL_ERROR
    "SystemVerilog inventory duplicates a Batch 165 closure owner")
endif()
foreach(FSIM_REQUIRED_CLAUSE RANGE 3 31)
  set(FSIM_FOUND_CLAUSE FALSE)
  foreach(FSIM_BASELINE_SCOPE IN LISTS FSIM_BASELINE_SCOPES)
    if(FSIM_BASELINE_SCOPE MATCHES
        "(^|,)${FSIM_REQUIRED_CLAUSE}(,|$)")
      set(FSIM_FOUND_CLAUSE TRUE)
    endif()
  endforeach()
  if(NOT FSIM_FOUND_CLAUSE)
    message(FATAL_ERROR
      "IEEE 1800-2017 reviewed baseline omits clause ${FSIM_REQUIRED_CLAUSE}")
  endif()
endforeach()
foreach(FSIM_REQUIRED_CLAUSE RANGE 3 40)
  set(FSIM_FOUND_CLAUSE FALSE)
  foreach(FSIM_SCOPE IN LISTS FSIM_ALL_SCOPES)
    if(FSIM_SCOPE MATCHES "(^|,)${FSIM_REQUIRED_CLAUSE}(,|$)")
      set(FSIM_FOUND_CLAUSE TRUE)
    endif()
  endforeach()
  if(NOT FSIM_FOUND_CLAUSE)
    message(FATAL_ERROR
      "IEEE 1800-2017 complete boundary omits clause ${FSIM_REQUIRED_CLAUSE}")
  endif()
endforeach()

file(STRINGS "${FSIM_WIDTH_INVENTORY}" FSIM_WIDTH_ROWS)
list(LENGTH FSIM_WIDTH_ROWS FSIM_WIDTH_ROW_COUNT)
if(NOT FSIM_WIDTH_ROW_COUNT EQUAL 27)
  message(FATAL_ERROR
    "SystemVerilog literal-width inventory must contain SPDX, one header, "
    "and 25 rows; got ${FSIM_WIDTH_ROW_COUNT}")
endif()
list(GET FSIM_WIDTH_ROWS 0 FSIM_WIDTH_HEADER)
if(NOT FSIM_WIDTH_HEADER STREQUAL
    "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR
    "SystemVerilog literal-width inventory lost its SPDX policy")
endif()
list(GET FSIM_WIDTH_ROWS 1 FSIM_WIDTH_HEADER)
if(NOT FSIM_WIDTH_HEADER STREQUAL
    "id\tstage\tdisposition\tclosure\tassumption\timplementation_owner\tsource_anchor\tpositive_evidence\tnegative_evidence\texecution_evidence\tdiagnostic_owner\tresource_owner")
  message(FATAL_ERROR "SystemVerilog literal-width inventory header changed")
endif()

set(FSIM_WIDTH_EXPECTATIONS
  "SVW-LEXER-TEXT|preserved|baseline"
  "SVW-PARSER-WIDTH|preserved|B164-C02"
  "SVW-PARSER-OUTPUT|preserved|B164-C02"
  "SVW-UNSIZED-IDENTITY|preserved|baseline"
  "SVW-PACKED-CONSTANT|preserved|baseline"
  "SVW-PARAMETER|preserved|baseline"
  "SVW-ENUM-METADATA|preserved|baseline"
  "SVW-PACKED-AGGREGATE|preserved|baseline"
  "SVW-CONSTRAINT-VALUE|preserved|baseline"
  "SVW-RUNTIME-PACKED|preserved|baseline"
  "SVW-ENGINE-PLANES|preserved|B164-C05"
  "SVW-DPI-PACKED|preserved|baseline"
  "SVW-VPI-VECTOR|preserved|B164-C15"
  "SVW-CONTAINER-ELEMENT|preserved|baseline"
  "SVW-TRACE-PUBLIC|preserved|B164-C16"
  "SVW-ARTIFACT-CACHE|preserved|B164-C17"
  "SVW-ASSOCIATIVE-INDEX|preserved|B165-C06"
  "SVW-COVERAGE-LITERAL|preserved|B165-C12"
  "SVW-VPI-ENUM|preserved|B165-C14"
  "SVW-LOGIC9-ENGINE|preserved|B165-C16"
  "SVW-LOGIC9-DEBUG|preserved|B165-C16"
  "SVW-MATERIALIZATION|physical|resource-policy"
  "SVW-SCALAR-HOST|physical|host-api"
  "SVW-DPI-BUDGET|physical|resource-policy"
  "SVW-UVM-BUDGET|physical|resource-policy")
set(FSIM_WIDTH_IDS)
set(FSIM_WIDTH_ANCHORS)
set(FSIM_WIDTH_PRESERVED 0)
set(FSIM_WIDTH_ACTIVE 0)
set(FSIM_WIDTH_PHYSICAL 0)
foreach(FSIM_INDEX RANGE 2 26)
  list(GET FSIM_WIDTH_ROWS ${FSIM_INDEX} FSIM_WIDTH_ROW)
  string(REPLACE "\t" ";" FSIM_WIDTH_FIELDS "${FSIM_WIDTH_ROW}")
  list(LENGTH FSIM_WIDTH_FIELDS FSIM_WIDTH_FIELD_COUNT)
  if(NOT FSIM_WIDTH_FIELD_COUNT EQUAL 12)
    message(FATAL_ERROR
      "SystemVerilog literal-width row ${FSIM_INDEX} does not have twelve fields")
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
      "duplicate SystemVerilog literal-width id or anchor: ${FSIM_WIDTH_ID}")
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
    message(FATAL_ERROR
      "unregistered SystemVerilog width row: ${FSIM_WIDTH_ID}")
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
    if(NOT FSIM_WIDTH_CLOSURE MATCHES "^B165-C(12|14|16)$")
      message(FATAL_ERROR
        "${FSIM_WIDTH_ID} active width row has no exact Batch 165 owner")
    endif()
  elseif(FSIM_WIDTH_DISPOSITION STREQUAL "physical")
    math(EXPR FSIM_WIDTH_PHYSICAL "${FSIM_WIDTH_PHYSICAL} + 1")
    string(TOLOWER "${FSIM_WIDTH_ASSUMPTION}" FSIM_WIDTH_ASSUMPTION_LOWER)
    if(NOT FSIM_WIDTH_ASSUMPTION_LOWER MATCHES
        "host|governed|explicit|configured|caller")
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
   NOT FSIM_WIDTH_PRESERVED EQUAL 21 OR NOT FSIM_WIDTH_ACTIVE EQUAL 0 OR
   NOT FSIM_WIDTH_PHYSICAL EQUAL 4)
  message(FATAL_ERROR
    "expected 25 width rows split 21 preserved, 0 active, and 4 physical; "
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
      "SystemVerilog width inventory omits ${FSIM_WIDTH_EXPECTED_ID}")
  endif()
endforeach()

file(READ "${FSIM_INVENTORY}" FSIM_CONTENTS)
file(READ "${FSIM_WIDTH_INVENTORY}" FSIM_WIDTH_CONTENTS)
string(TOLOWER "${FSIM_CONTENTS}\n${FSIM_WIDTH_CONTENTS}" FSIM_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "xfail" "expected-fail" "waiver" "allowlist" "suppress")
  string(FIND "${FSIM_LOWER}" "${FSIM_FORBIDDEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog inventories contain forbidden escape ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.systemverilog-gap-inventory"
    "CheckSystemVerilogGapInventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()
file(READ "${FSIM_PLAN}" FSIM_PLAN_CONTENTS)
foreach(FSIM_PLAN_TOKEN IN ITEMS
    "### Batch 165 - SystemVerilog-2017 residual language closure"
    "Cross-cutting width requirement:"
    "arbitrary-bit-string-limit ledger"
    "Changes 2-16")
  string(FIND "${FSIM_PLAN_CONTENTS}" "${FSIM_PLAN_TOKEN}"
    FSIM_PLAN_TOKEN_INDEX)
  if(FSIM_PLAN_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog inventory plan scope lost token: ${FSIM_PLAN_TOKEN}")
  endif()
endforeach()

message(STATUS
  "SystemVerilog gap inventory: ${FSIM_SUPPORTED} supported, "
  "${FSIM_UNSUPPORTED} active, ${FSIM_DEFERRED} deferred; width ledger: "
  "${FSIM_WIDTH_PRESERVED} preserved, ${FSIM_WIDTH_ACTIVE} active, "
  "${FSIM_WIDTH_PHYSICAL} physical")

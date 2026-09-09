# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
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
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/systemverilog_2023_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v3.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_FEATURE_README
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/README.md")
set(FSIM_SOURCE_MANIFEST
  "${FSIM_SOURCE_DIR}/packaging/source-package-manifest.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_INVENTORY}"
    "${FSIM_PLAN}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_FEATURE_README}"
    "${FSIM_SOURCE_MANIFEST}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR
      "SystemVerilog-2023 inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_EXPECTED_DIGEST
  "f9563a7227c58207c6414804756c22376cbb8e4038ec9e8dd2c34dcd0ac82aeb")
fsim_normalized_text_sha256("${FSIM_INVENTORY}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "SystemVerilog-2023 inventory digest changed: expected "
    "${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 58)
  message(FATAL_ERROR
    "SystemVerilog-2023 inventory must contain SPDX, one header, and 56 rows; got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "SystemVerilog-2023 inventory lost its SPDX policy")
endif()
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL
    "id\tstandard\tclauses\tdomain\tstate\tclosure_owner\tobligation\tparser_owner\tsemantic_owner\telaboration_owner\truntime_owner\tevidence_owner\tresource_owner")
  message(FATAL_ERROR "SystemVerilog-2023 inventory header changed")
endif()

set(FSIM_IDS)
set(FSIM_CLOSURES)
set(FSIM_DOMAINS)
set(FSIM_ACTIVE_COUNT 0)
set(FSIM_PRESERVED_COUNT 0)
foreach(FSIM_INDEX RANGE 2 57)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 13)
    message(FATAL_ERROR
      "SystemVerilog-2023 row ${FSIM_INDEX} does not have thirteen fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_STANDARD)
  list(GET FSIM_FIELDS 2 FSIM_CLAUSES)
  list(GET FSIM_FIELDS 3 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 4 FSIM_STATE)
  list(GET FSIM_FIELDS 5 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 6 FSIM_OBLIGATION)

  foreach(FSIM_UNIQUE_KIND IN ITEMS ID CLOSURE DOMAIN)
    set(FSIM_VALUE "${FSIM_${FSIM_UNIQUE_KIND}}")
    list(FIND FSIM_${FSIM_UNIQUE_KIND}S "${FSIM_VALUE}" FSIM_OFFSET)
    if(NOT FSIM_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "duplicate SystemVerilog-2023 ${FSIM_UNIQUE_KIND}: ${FSIM_VALUE}")
    endif()
    list(APPEND FSIM_${FSIM_UNIQUE_KIND}S "${FSIM_VALUE}")
  endforeach()

  if(NOT FSIM_ID MATCHES "^S23-B18(5|6|7)-C[0-9][0-9]$")
    message(FATAL_ERROR "invalid SystemVerilog-2023 inventory id: ${FSIM_ID}")
  endif()
  if(NOT FSIM_STANDARD MATCHES
      "^(IEEE1800-2023|IEEE1800-2017,IEEE1800-2023)$")
    message(FATAL_ERROR "${FSIM_ID} has an invalid standard identity")
  endif()
  if(FSIM_CLAUSES STREQUAL "" OR FSIM_DOMAIN STREQUAL "" OR
     FSIM_OBLIGATION STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has an empty scope or obligation")
  endif()
  if(NOT FSIM_CLOSURE MATCHES "^B18(5|6|7)-C[0-9][0-9]$")
    message(FATAL_ERROR "${FSIM_ID} has an invalid closure owner")
  endif()
  if(FSIM_STATE STREQUAL "active")
    math(EXPR FSIM_ACTIVE_COUNT "${FSIM_ACTIVE_COUNT} + 1")
  elseif(FSIM_STATE STREQUAL "preserved")
    math(EXPR FSIM_PRESERVED_COUNT "${FSIM_PRESERVED_COUNT} + 1")
  else()
    message(FATAL_ERROR "${FSIM_ID} has an invalid state: ${FSIM_STATE}")
  endif()

  foreach(FSIM_OWNER_INDEX RANGE 7 12)
    list(GET FSIM_FIELDS ${FSIM_OWNER_INDEX} FSIM_OWNER)
    if(FSIM_OWNER STREQUAL "" OR NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_OWNER}")
      message(FATAL_ERROR
        "${FSIM_ID} has a missing repository owner: ${FSIM_OWNER}")
    endif()
  endforeach()
endforeach()

if(NOT FSIM_ACTIVE_COUNT EQUAL 38 OR NOT FSIM_PRESERVED_COUNT EQUAL 18)
  message(FATAL_ERROR
    "SystemVerilog-2023 inventory must contain 38 active and eighteen preserved rows after Batch 185 Change 19")
endif()
foreach(FSIM_BATCH IN ITEMS 185 186 187)
  if(FSIM_BATCH EQUAL 185)
    set(FSIM_FIRST_CHANGE 2)
  else()
    set(FSIM_FIRST_CHANGE 1)
  endif()
  foreach(FSIM_CHANGE RANGE ${FSIM_FIRST_CHANGE} 19)
    if(FSIM_CHANGE LESS 10)
      set(FSIM_CHANGE_ID "0${FSIM_CHANGE}")
    else()
      set(FSIM_CHANGE_ID "${FSIM_CHANGE}")
    endif()
    list(FIND FSIM_CLOSURES
      "B${FSIM_BATCH}-C${FSIM_CHANGE_ID}" FSIM_OWNER_OFFSET)
    if(FSIM_OWNER_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "SystemVerilog-2023 inventory omits Batch ${FSIM_BATCH} Change ${FSIM_CHANGE_ID}")
    endif()
  endforeach()
endforeach()

file(READ "${FSIM_INVENTORY}" FSIM_INVENTORY_TEXT)
string(TOLOWER "${FSIM_INVENTORY_TEXT}" FSIM_INVENTORY_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "/home/" "standards/" ".pdf" "private-reference" "lrm text"
    "ieee says" "verbatim")
  string(FIND "${FSIM_INVENTORY_LOWER}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog-2023 inventory contains forbidden reference token: ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
foreach(FSIM_BATCH IN ITEMS 185 186 187)
  string(FIND "${FSIM_PLAN_TEXT}" "### Batch ${FSIM_BATCH} -"
    FSIM_BATCH_OFFSET)
  if(FSIM_BATCH_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan lost Batch ${FSIM_BATCH}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "independently worded 56-row SystemVerilog-2023"
    "SystemVerilog-2023 frontend, data model, classes, and processes"
    "SystemVerilog-2023 verification, hierarchy, and timing"
    "SystemVerilog-2023 foreign APIs and complete closure")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "v3 plan lost required SystemVerilog-2023 token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.systemverilog-2023-inventory"
    "CheckSystemVerilog2023Inventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog-2023 inventory test registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_FEATURE_README}" FSIM_FEATURE_README_TEXT)
string(FIND "${FSIM_FEATURE_README_TEXT}"
  "feature_matrix/systemverilog_2023_inventory.tsv" FSIM_README_OFFSET)
if(FSIM_README_OFFSET EQUAL -1)
  message(FATAL_ERROR
    "feature-matrix README omits the SystemVerilog-2023 inventory")
endif()

file(READ "${FSIM_SOURCE_MANIFEST}" FSIM_SOURCE_MANIFEST_TEXT)
foreach(FSIM_PATH IN ITEMS
    "cmake/CheckSystemVerilog2023Inventory.cmake"
    "tests/feature_matrix/systemverilog_2023_inventory.tsv")
  string(FIND "${FSIM_SOURCE_MANIFEST_TEXT}" "${FSIM_PATH}"
    FSIM_MANIFEST_OFFSET)
  if(FSIM_MANIFEST_OFFSET EQUAL -1)
    message(FATAL_ERROR "source-package manifest omits ${FSIM_PATH}")
  endif()
endforeach()

message(STATUS
  "SystemVerilog-2023 inventory passed: rows=56 active=${FSIM_ACTIVE_COUNT} preserved=${FSIM_PRESERVED_COUNT} digest=${FSIM_ACTUAL_DIGEST}")

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
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_2019_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v3.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_FEATURE_README
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/README.md")
set(FSIM_GUIDE "${FSIM_SOURCE_DIR}/docs/vhdl-2019.md")
set(FSIM_SOURCE_MANIFEST
  "${FSIM_SOURCE_DIR}/packaging/source-package-manifest.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_INVENTORY}"
    "${FSIM_PLAN}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_FEATURE_README}"
    "${FSIM_GUIDE}"
    "${FSIM_SOURCE_MANIFEST}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "VHDL-2019 inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_EXPECTED_DIGEST
  "87e90ceea9effba9b2fbbb44a63152bb11f07d4afb674d71c43ae667afe30dbe")
fsim_normalized_text_sha256("${FSIM_INVENTORY}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "VHDL-2019 inventory digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 39)
  message(FATAL_ERROR
    "VHDL-2019 inventory must contain SPDX, one header, and 37 rows; got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "VHDL-2019 inventory lost its SPDX policy")
endif()
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL
    "id\tstandard\tclauses\tdomain\tstate\tclosure_owner\tobligation\tparser_owner\tsemantic_owner\telaboration_owner\truntime_owner\tevidence_owner\tresource_owner")
  message(FATAL_ERROR "VHDL-2019 inventory header changed")
endif()

set(FSIM_IDS)
set(FSIM_CLOSURES)
set(FSIM_ACTIVE_COUNT 0)
set(FSIM_PRESERVED_COUNT 0)
foreach(FSIM_INDEX RANGE 2 38)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 13)
    message(FATAL_ERROR
      "VHDL-2019 row ${FSIM_INDEX} does not have thirteen fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_STANDARD)
  list(GET FSIM_FIELDS 2 FSIM_CLAUSES)
  list(GET FSIM_FIELDS 3 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 4 FSIM_STATE)
  list(GET FSIM_FIELDS 5 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 6 FSIM_OBLIGATION)

  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_ID_OFFSET)
  if(NOT FSIM_ID_OFFSET EQUAL -1)
    message(FATAL_ERROR "duplicate VHDL-2019 inventory id: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(FIND FSIM_CLOSURES "${FSIM_CLOSURE}" FSIM_CLOSURE_OFFSET)
  if(NOT FSIM_CLOSURE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "duplicate VHDL-2019 closure owner: ${FSIM_CLOSURE}")
  endif()
  list(APPEND FSIM_CLOSURES "${FSIM_CLOSURE}")

  if(NOT FSIM_ID MATCHES "^V19-B18(3|4)-C[0-9][0-9]$")
    message(FATAL_ERROR "invalid VHDL-2019 inventory id: ${FSIM_ID}")
  endif()
  if(NOT FSIM_STANDARD MATCHES
      "^(IEEE1076-2019|IEEE1076-2008,IEEE1076-2019)$")
    message(FATAL_ERROR "${FSIM_ID} has an invalid standard identity")
  endif()
  if(FSIM_CLAUSES STREQUAL "" OR FSIM_DOMAIN STREQUAL "" OR
     FSIM_OBLIGATION STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has an empty scope or obligation")
  endif()
  if(NOT FSIM_CLOSURE MATCHES "^B18(3|4)-C[0-9][0-9]$")
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

if(NOT FSIM_ACTIVE_COUNT EQUAL 0 OR NOT FSIM_PRESERVED_COUNT EQUAL 37)
  message(FATAL_ERROR
    "VHDL-2019 inventory must contain zero active and 37 preserved rows after Batch 184 Change 19")
endif()
foreach(FSIM_CHANGE RANGE 2 19)
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_ID "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_ID "${FSIM_CHANGE}")
  endif()
  list(FIND FSIM_CLOSURES "B183-C${FSIM_CHANGE_ID}" FSIM_OWNER_OFFSET)
  if(FSIM_OWNER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 inventory omits Batch 183 Change ${FSIM_CHANGE_ID}")
  endif()
endforeach()
foreach(FSIM_CHANGE RANGE 1 19)
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_ID "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_ID "${FSIM_CHANGE}")
  endif()
  list(FIND FSIM_CLOSURES "B184-C${FSIM_CHANGE_ID}" FSIM_OWNER_OFFSET)
  if(FSIM_OWNER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 inventory omits Batch 184 Change ${FSIM_CHANGE_ID}")
  endif()
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
      "VHDL-2019 inventory contains forbidden reference material token: ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
foreach(FSIM_BATCH IN ITEMS 183 184)
  string(FIND "${FSIM_PLAN_TEXT}" "### Batch ${FSIM_BATCH} -"
    FSIM_BATCH_OFFSET)
  if(FSIM_BATCH_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan lost Batch ${FSIM_BATCH}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "Build a private-reference-derived, independently worded 2008-to-2019 clause inventory."
    "Closed the last active independently worded VHDL-2019 row"
    "zero active and 37 preserved rows"
    "Sanitizers and hosted Linux and Windows qualification run only at every")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan lost required VHDL-2019 token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.vhdl-2019-inventory" "CheckVhdl2019Inventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 inventory test registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_FEATURE_README}" FSIM_FEATURE_README_TEXT)
string(FIND "${FSIM_FEATURE_README_TEXT}"
  "feature_matrix/vhdl_2019_inventory.tsv" FSIM_README_OFFSET)
if(FSIM_README_OFFSET EQUAL -1)
  message(FATAL_ERROR "feature-matrix README omits the VHDL-2019 inventory")
endif()

file(READ "${FSIM_GUIDE}" FSIM_GUIDE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "# VHDL-2019 support"
    "standard = \"2019\""
    "## Language additions"
    "## Runtime and predefined APIs"
    "## Coverage"
    "## VHPI"
    "## Artifacts and mixed-language designs"
    "## Deliberate boundaries"
    "vhdl_2019_inventory.tsv")
  string(FIND "${FSIM_GUIDE_TEXT}" "${FSIM_TOKEN}" FSIM_GUIDE_OFFSET)
  if(FSIM_GUIDE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 guide lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()
string(TOLOWER "${FSIM_GUIDE_TEXT}" FSIM_GUIDE_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "/home/" "standards/" ".pdf" "private-reference" "lrm text"
    "ieee says" "verbatim")
  string(FIND "${FSIM_GUIDE_LOWER}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 guide contains forbidden reference material token: ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_MANIFEST}" FSIM_SOURCE_MANIFEST_TEXT)
foreach(FSIM_PATH IN ITEMS
    "cmake/CheckVhdl2019Inventory.cmake"
    "docs/vhdl-2019.md"
    "src/app/application_vhdl_mode_view.cpp"
    "src/frontend/vhdl_conditional_analysis.cpp"
    "src/frontend/vhdl_conditional_analysis_internal.hpp"
    "tests/feature_matrix/vhdl_2019_inventory.tsv")
  string(FIND "${FSIM_SOURCE_MANIFEST_TEXT}" "${FSIM_PATH}"
    FSIM_MANIFEST_OFFSET)
  if(FSIM_MANIFEST_OFFSET EQUAL -1)
    message(FATAL_ERROR "source-package manifest omits ${FSIM_PATH}")
  endif()
endforeach()

message(STATUS
  "VHDL-2019 inventory passed: rows=37 active=${FSIM_ACTIVE_COUNT} preserved=${FSIM_PRESERVED_COUNT} digest=${FSIM_ACTUAL_DIGEST}")

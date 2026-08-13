# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MODE_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_standard_mode_inventory.tsv")
set(FSIM_PACKAGE_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_synopsys_package_inventory.tsv")
set(FSIM_REVISION_CORPUS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_revision_corpus.tsv")
set(FSIM_PACKAGE_CORPUS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_synopsys_package_corpus.tsv")
set(FSIM_CLOSURE_RUNNER
  "${FSIM_SOURCE_DIR}/cmake/RunVhdlStandardModeClosureMatrix.cmake")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v2.md")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_MODE_INVENTORY}"
    "${FSIM_PACKAGE_INVENTORY}"
    "${FSIM_REVISION_CORPUS}"
    "${FSIM_PACKAGE_CORPUS}"
    "${FSIM_CLOSURE_RUNNER}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_PLAN}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "older-VHDL inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CLOSURE_RUNNER}" FSIM_CLOSURE_TEXT)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "set(FSIM_STAGE_TIMEOUT_SECONDS 1200)"
    "fsim.application.vhdl_ieee_integration"
    "fsim.application.vhdl_numeric"
    "fsim.application.vhdl_logic9"
    "fsim.application.artifact_phases"
    "fsim.llvm"
    "fsim.msvc-debug-contract"
    "fsim.windows-llvm-contract"
    "fsim.resource-portability-contract"
    "FSIM-VHDL-OLDER-ENVIRONMENT-PASS revisions=1987/1993/2000/2002"
    "FSIM-VHDL-SYNOPSYS-PACKAGES-PASS"
    "FSIM-VHDL-OLDER-MODES-PASS revisions=1987/1993/2000/2002"
    "FSIM-VHDL-OLDER-MODES-ARTIFACT-PASS"
    "FSIM_WITNESS_COUNT EQUAL 15")
  string(FIND "${FSIM_CLOSURE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "VHDL closure runner lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.vhdl-standard-mode-closure-matrix"
    "RunVhdlStandardModeClosureMatrix.cmake"
    "RUN_SERIAL TRUE"
    "TIMEOUT 7200")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "VHDL closure registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "### Batch 166 - Older VHDL standard modes"
    "VHDL-87"
    "VHDL-93"
    "VHDL-2000"
    "VHDL-2002"
    "ieee.std_logic_signed"
    "ieee.std_logic_unsigned"
    "ieee.std_logic_arith"
    "ieee.std_logic_misc"
    "No vector or numeric operation may narrow to a host word")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 166 plan lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_MODE_HEADER
  "id\trevisions\tdomain\tboundary\tclosure\tobligation\timplementation_owner\tpositive_evidence\tnegative_evidence\texecution_evidence\tdiagnostic_owner\tresource_owner")
set(FSIM_PACKAGE_HEADER
  "id\tpackages\tdomain\tboundary\tclosure\tobligation\tdeclaration_owner\tbody_owner\tpositive_evidence\tnegative_evidence\texecution_evidence\tprovenance_owner\tresource_owner")
set(FSIM_EXPECTED_REVISIONS "VHDL87,VHDL93,VHDL2000,VHDL2002")
set(FSIM_EXPECTED_PACKAGES
  "std_logic_signed,std_logic_unsigned,std_logic_arith,std_logic_misc")

function(fsim_require_inventory_path FSIM_ID FSIM_FIELD FSIM_PATH FSIM_PREFIX)
  if(FSIM_PATH STREQUAL "" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_PATH}")
    message(FATAL_ERROR "${FSIM_ID} lost ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
  if(NOT FSIM_PREFIX STREQUAL "" AND NOT FSIM_PATH MATCHES "^${FSIM_PREFIX}")
    message(FATAL_ERROR "${FSIM_ID} misplaced ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
endfunction()

function(fsim_require_evidence_anchor FSIM_ID FSIM_PATH FSIM_ANCHOR)
  fsim_require_inventory_path("${FSIM_ID}" "evidence" "${FSIM_PATH}" "tests/")
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_PATH}" FSIM_EVIDENCE_TEXT)
  string(FIND "${FSIM_EVIDENCE_TEXT}" "${FSIM_ANCHOR}" FSIM_ANCHOR_OFFSET)
  if(FSIM_ANCHOR STREQUAL "" OR FSIM_ANCHOR_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "${FSIM_ID} lost evidence anchor '${FSIM_ANCHOR}' in ${FSIM_PATH}")
  endif()
endfunction()

file(STRINGS "${FSIM_MODE_INVENTORY}" FSIM_MODE_ROWS)
list(LENGTH FSIM_MODE_ROWS FSIM_MODE_ROW_COUNT)
if(NOT FSIM_MODE_ROW_COUNT EQUAL 19)
  message(FATAL_ERROR
    "VHDL standard-mode inventory must contain SPDX, header and 17 rows; got ${FSIM_MODE_ROW_COUNT}")
endif()
list(GET FSIM_MODE_ROWS 0 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "VHDL standard-mode inventory lost its SPDX policy")
endif()
list(GET FSIM_MODE_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL FSIM_MODE_HEADER)
  message(FATAL_ERROR "VHDL standard-mode inventory header changed")
endif()

set(FSIM_MODE_IDS)
set(FSIM_MODE_CLOSURES)
foreach(FSIM_INDEX RANGE 2 18)
  list(GET FSIM_MODE_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 12)
    message(FATAL_ERROR "VHDL standard-mode row ${FSIM_INDEX} does not have twelve fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_REVISIONS)
  list(GET FSIM_FIELDS 2 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 3 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 4 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 5 FSIM_OBLIGATION)
  math(EXPR FSIM_CHANGE "${FSIM_INDEX}")
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  set(FSIM_EXPECTED_CLOSURE "B166-C${FSIM_CHANGE_TEXT}")
  if(NOT FSIM_ID STREQUAL "VHMODE-C${FSIM_CHANGE_TEXT}")
    message(FATAL_ERROR "mode row ${FSIM_INDEX} has drifted id ${FSIM_ID}")
  endif()
  if(FSIM_CHANGE LESS_EQUAL 18)
    set(FSIM_EXPECTED_BOUNDARY "preserved")
  else()
    set(FSIM_EXPECTED_BOUNDARY "active")
  endif()
  if(NOT FSIM_REVISIONS STREQUAL FSIM_EXPECTED_REVISIONS OR
     FSIM_DOMAIN STREQUAL "" OR
     NOT FSIM_BOUNDARY STREQUAL FSIM_EXPECTED_BOUNDARY OR
     NOT FSIM_CLOSURE STREQUAL FSIM_EXPECTED_CLOSURE OR
     FSIM_OBLIGATION STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has a drifted scope or closure field")
  endif()
  list(FIND FSIM_MODE_IDS "${FSIM_ID}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate VHDL standard-mode id: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_MODE_IDS "${FSIM_ID}")
  list(APPEND FSIM_MODE_CLOSURES "${FSIM_CLOSURE}")
  list(GET FSIM_FIELDS 6 FSIM_OWNER)
  fsim_require_inventory_path("${FSIM_ID}" "implementation" "${FSIM_OWNER}" "(src|tests|cmake)/")
  foreach(FSIM_EVIDENCE_INDEX RANGE 7 9)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE)
    fsim_require_inventory_path("${FSIM_ID}" "evidence-${FSIM_EVIDENCE_INDEX}" "${FSIM_EVIDENCE}" "tests/")
  endforeach()
  list(GET FSIM_FIELDS 10 FSIM_DIAGNOSTIC)
  list(GET FSIM_FIELDS 11 FSIM_RESOURCE)
  if(NOT FSIM_DIAGNOSTIC STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_RESOURCE STREQUAL "cmake/CheckResourcePortabilityContract.cmake")
    message(FATAL_ERROR "${FSIM_ID} drifted diagnostic or resource ownership")
  endif()
endforeach()

file(STRINGS "${FSIM_PACKAGE_INVENTORY}" FSIM_PACKAGE_ROWS)
list(LENGTH FSIM_PACKAGE_ROWS FSIM_PACKAGE_ROW_COUNT)
if(NOT FSIM_PACKAGE_ROW_COUNT EQUAL 19)
  message(FATAL_ERROR
    "VHDL Synopsys inventory must contain SPDX, header and 17 rows; got ${FSIM_PACKAGE_ROW_COUNT}")
endif()
list(GET FSIM_PACKAGE_ROWS 0 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "VHDL Synopsys inventory lost its SPDX policy")
endif()
list(GET FSIM_PACKAGE_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL FSIM_PACKAGE_HEADER)
  message(FATAL_ERROR "VHDL Synopsys inventory header changed")
endif()

set(FSIM_PACKAGE_IDS)
set(FSIM_PACKAGE_CLOSURES)
foreach(FSIM_INDEX RANGE 2 18)
  list(GET FSIM_PACKAGE_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 13)
    message(FATAL_ERROR "VHDL Synopsys row ${FSIM_INDEX} does not have thirteen fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_PACKAGES)
  list(GET FSIM_FIELDS 2 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 3 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 4 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 5 FSIM_OBLIGATION)
  math(EXPR FSIM_CHANGE "${FSIM_INDEX}")
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  set(FSIM_EXPECTED_CLOSURE "B166-C${FSIM_CHANGE_TEXT}")
  if(FSIM_CHANGE LESS_EQUAL 18)
    set(FSIM_EXPECTED_BOUNDARY "preserved")
  else()
    set(FSIM_EXPECTED_BOUNDARY "active")
  endif()
  if(NOT FSIM_ID STREQUAL "VHSYN-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_PACKAGES STREQUAL FSIM_EXPECTED_PACKAGES OR
     FSIM_DOMAIN STREQUAL "" OR
     NOT FSIM_BOUNDARY STREQUAL FSIM_EXPECTED_BOUNDARY OR
     NOT FSIM_CLOSURE STREQUAL FSIM_EXPECTED_CLOSURE OR
     FSIM_OBLIGATION STREQUAL "")
    message(FATAL_ERROR "Synopsys row ${FSIM_INDEX} has a drifted scope or closure field")
  endif()
  list(FIND FSIM_PACKAGE_IDS "${FSIM_ID}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate VHDL Synopsys id: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_PACKAGE_IDS "${FSIM_ID}")
  list(APPEND FSIM_PACKAGE_CLOSURES "${FSIM_CLOSURE}")
  foreach(FSIM_OWNER_INDEX RANGE 6 7)
    list(GET FSIM_FIELDS ${FSIM_OWNER_INDEX} FSIM_OWNER)
    fsim_require_inventory_path("${FSIM_ID}" "package-owner-${FSIM_OWNER_INDEX}" "${FSIM_OWNER}" "(src|tests|cmake)/")
  endforeach()
  foreach(FSIM_EVIDENCE_INDEX RANGE 8 10)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE)
    fsim_require_inventory_path("${FSIM_ID}" "evidence-${FSIM_EVIDENCE_INDEX}" "${FSIM_EVIDENCE}" "tests/")
  endforeach()
  list(GET FSIM_FIELDS 11 FSIM_PROVENANCE)
  list(GET FSIM_FIELDS 12 FSIM_RESOURCE)
  fsim_require_inventory_path("${FSIM_ID}" "provenance" "${FSIM_PROVENANCE}" "docs/")
  if(NOT FSIM_RESOURCE STREQUAL "cmake/CheckResourcePortabilityContract.cmake")
    message(FATAL_ERROR "${FSIM_ID} drifted resource ownership")
  endif()
endforeach()

foreach(FSIM_CHANGE RANGE 2 18)
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  set(FSIM_EXPECTED_CLOSURE "B166-C${FSIM_CHANGE_TEXT}")
  foreach(FSIM_CLOSURE_LIST IN ITEMS FSIM_MODE_CLOSURES FSIM_PACKAGE_CLOSURES)
    set(FSIM_MATCH_COUNT 0)
    foreach(FSIM_CLOSURE IN LISTS ${FSIM_CLOSURE_LIST})
      if(FSIM_CLOSURE STREQUAL FSIM_EXPECTED_CLOSURE)
        math(EXPR FSIM_MATCH_COUNT "${FSIM_MATCH_COUNT} + 1")
      endif()
    endforeach()
    if(NOT FSIM_MATCH_COUNT EQUAL 1)
      message(FATAL_ERROR
        "${FSIM_CLOSURE_LIST} must assign ${FSIM_EXPECTED_CLOSURE} exactly once")
    endif()
  endforeach()
endforeach()

set(FSIM_REVISION_CORPUS_HEADER
  "revision\tpositive_stage\tpositive_evidence\tpositive_anchor\tnegative_stage\tnegative_evidence\tnegative_anchor\tdiagnostic_code\tdiagnostic_line\tdiagnostic_column")
file(STRINGS "${FSIM_REVISION_CORPUS}" FSIM_CORPUS_ROWS)
list(LENGTH FSIM_CORPUS_ROWS FSIM_CORPUS_ROW_COUNT)
if(NOT FSIM_CORPUS_ROW_COUNT EQUAL 6)
  message(FATAL_ERROR
    "VHDL revision corpus must contain SPDX, header and four rows")
endif()
list(GET FSIM_CORPUS_ROWS 0 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "VHDL revision corpus lost its SPDX policy")
endif()
list(GET FSIM_CORPUS_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL FSIM_REVISION_CORPUS_HEADER)
  message(FATAL_ERROR "VHDL revision corpus header changed")
endif()
set(FSIM_CORPUS_REVISIONS VHDL87 VHDL93 VHDL2000 VHDL2002)
foreach(FSIM_INDEX RANGE 2 5)
  list(GET FSIM_CORPUS_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 10)
    message(FATAL_ERROR "VHDL revision corpus row ${FSIM_INDEX} is malformed")
  endif()
  math(EXPR FSIM_REVISION_INDEX "${FSIM_INDEX} - 2")
  list(GET FSIM_CORPUS_REVISIONS ${FSIM_REVISION_INDEX} FSIM_EXPECTED_REVISION)
  list(GET FSIM_FIELDS 0 FSIM_REVISION)
  list(GET FSIM_FIELDS 1 FSIM_POSITIVE_STAGE)
  list(GET FSIM_FIELDS 2 FSIM_POSITIVE_EVIDENCE)
  list(GET FSIM_FIELDS 3 FSIM_POSITIVE_ANCHOR)
  list(GET FSIM_FIELDS 4 FSIM_NEGATIVE_STAGE)
  list(GET FSIM_FIELDS 5 FSIM_NEGATIVE_EVIDENCE)
  list(GET FSIM_FIELDS 6 FSIM_NEGATIVE_ANCHOR)
  list(GET FSIM_FIELDS 7 FSIM_DIAGNOSTIC)
  list(GET FSIM_FIELDS 8 FSIM_LINE)
  list(GET FSIM_FIELDS 9 FSIM_COLUMN)
  if(NOT FSIM_REVISION STREQUAL FSIM_EXPECTED_REVISION OR
     NOT FSIM_POSITIVE_STAGE MATCHES "^(parse|semantic|elaboration|execution)$" OR
     NOT FSIM_NEGATIVE_STAGE MATCHES "^(parse|semantic|elaboration|execution)$" OR
     NOT FSIM_DIAGNOSTIC STREQUAL "FSIM-FE-VHSTD-003" OR
     NOT FSIM_LINE MATCHES "^[1-9][0-9]*$" OR
     NOT FSIM_COLUMN MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "${FSIM_REVISION} corpus ownership drifted")
  endif()
  fsim_require_evidence_anchor(
    "${FSIM_REVISION}-positive" "${FSIM_POSITIVE_EVIDENCE}" "${FSIM_POSITIVE_ANCHOR}")
  fsim_require_evidence_anchor(
    "${FSIM_REVISION}-negative" "${FSIM_NEGATIVE_EVIDENCE}" "${FSIM_NEGATIVE_ANCHOR}")
endforeach()

set(FSIM_PACKAGE_CORPUS_HEADER
  "package\trevisions\tpositive_stage\tpositive_evidence\tpositive_anchor\tnegative_evidence\tnegative_anchor\tdiagnostic_code\tarbitrary_width_evidence\tprovenance_evidence")
file(STRINGS "${FSIM_PACKAGE_CORPUS}" FSIM_CORPUS_ROWS)
list(LENGTH FSIM_CORPUS_ROWS FSIM_CORPUS_ROW_COUNT)
if(NOT FSIM_CORPUS_ROW_COUNT EQUAL 6)
  message(FATAL_ERROR
    "VHDL Synopsys package corpus must contain SPDX, header and four rows")
endif()
list(GET FSIM_CORPUS_ROWS 0 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "VHDL Synopsys package corpus lost its SPDX policy")
endif()
list(GET FSIM_CORPUS_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL FSIM_PACKAGE_CORPUS_HEADER)
  message(FATAL_ERROR "VHDL Synopsys package corpus header changed")
endif()
set(FSIM_CORPUS_PACKAGES
  std_logic_signed std_logic_unsigned std_logic_arith std_logic_misc)
foreach(FSIM_INDEX RANGE 2 5)
  list(GET FSIM_CORPUS_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 10)
    message(FATAL_ERROR
      "VHDL Synopsys package corpus row ${FSIM_INDEX} is malformed")
  endif()
  math(EXPR FSIM_PACKAGE_INDEX "${FSIM_INDEX} - 2")
  list(GET FSIM_CORPUS_PACKAGES ${FSIM_PACKAGE_INDEX} FSIM_EXPECTED_PACKAGE)
  list(GET FSIM_FIELDS 0 FSIM_PACKAGE)
  list(GET FSIM_FIELDS 1 FSIM_REVISIONS)
  list(GET FSIM_FIELDS 2 FSIM_POSITIVE_STAGE)
  list(GET FSIM_FIELDS 3 FSIM_POSITIVE_EVIDENCE)
  list(GET FSIM_FIELDS 4 FSIM_POSITIVE_ANCHOR)
  list(GET FSIM_FIELDS 5 FSIM_NEGATIVE_EVIDENCE)
  list(GET FSIM_FIELDS 6 FSIM_NEGATIVE_ANCHOR)
  list(GET FSIM_FIELDS 7 FSIM_DIAGNOSTIC)
  list(GET FSIM_FIELDS 8 FSIM_WIDTH_EVIDENCE)
  list(GET FSIM_FIELDS 9 FSIM_PROVENANCE_EVIDENCE)
  if(NOT FSIM_PACKAGE STREQUAL FSIM_EXPECTED_PACKAGE OR
     NOT FSIM_REVISIONS STREQUAL FSIM_EXPECTED_REVISIONS OR
     NOT FSIM_POSITIVE_STAGE MATCHES "^(parse|semantic|elaboration|execution)$" OR
     NOT FSIM_DIAGNOSTIC MATCHES "^FSIM-[A-Z0-9-]+$")
    message(FATAL_ERROR "${FSIM_PACKAGE} package corpus ownership drifted")
  endif()
  fsim_require_evidence_anchor(
    "${FSIM_PACKAGE}-positive" "${FSIM_POSITIVE_EVIDENCE}" "${FSIM_POSITIVE_ANCHOR}")
  fsim_require_evidence_anchor(
    "${FSIM_PACKAGE}-negative" "${FSIM_NEGATIVE_EVIDENCE}" "${FSIM_NEGATIVE_ANCHOR}")
  fsim_require_inventory_path(
    "${FSIM_PACKAGE}" "arbitrary-width evidence" "${FSIM_WIDTH_EVIDENCE}" "tests/")
  fsim_require_inventory_path(
    "${FSIM_PACKAGE}" "provenance evidence" "${FSIM_PROVENANCE_EVIDENCE}" "tests/")
endforeach()

message(STATUS
  "VHDL standard-mode inventory: 17 preserved and zero active revision rows, "
  "plus 17 preserved and zero active Synopsys-package rows, assigned one-to-one "
  "to Batch 166 Changes 2-18; four revision and four package corpus rows are "
  "anchored to positive, negative, exact-diagnostic, width and provenance "
  "evidence, and the 15-witness serial closure matrix is registered")

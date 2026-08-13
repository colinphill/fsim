# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MODE_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_systemverilog_standard_mode_inventory.tsv")
set(FSIM_COMPATIBILITY_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_systemverilog_compatibility_inventory.tsv")
set(FSIM_REVISION_CORPUS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_systemverilog_revision_corpus.tsv")
set(FSIM_COMPATIBILITY_CORPUS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_systemverilog_compatibility_corpus.tsv")
set(FSIM_CLOSURE_RUNNER
  "${FSIM_SOURCE_DIR}/cmake/RunVerilogSystemVerilogStandardModeClosureMatrix.cmake")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v2.md")
set(FSIM_DOCUMENTS
  "${FSIM_SOURCE_DIR}/docs/architecture.md"
  "${FSIM_SOURCE_DIR}/docs/diagnostics.md"
  "${FSIM_SOURCE_DIR}/docs/feature-matrix.md"
  "${FSIM_SOURCE_DIR}/docs/language-support.md"
  "${FSIM_SOURCE_DIR}/docs/systemverilog-vpi.md"
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/README.md")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_MODE_INVENTORY}"
    "${FSIM_COMPATIBILITY_INVENTORY}"
    "${FSIM_REVISION_CORPUS}"
    "${FSIM_COMPATIBILITY_CORPUS}"
    "${FSIM_CLOSURE_RUNNER}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_PLAN}"
    ${FSIM_DOCUMENTS})
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR
      "older-Verilog/SystemVerilog inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CLOSURE_RUNNER}" FSIM_CLOSURE_TEXT)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "set(FSIM_STAGE_TIMEOUT_SECONDS 1200)"
    "fsim.verilog-systemverilog-standard-mode-inventory"
    "fsim.application.sv_conformance"
    "fsim.application.typed_boundaries"
    "fsim.application.artifact_phases"
    "fsim.llvm"
    "fsim.application.vpi"
    "fsim.application.tcl"
    "fsim.api"
    "fsim.api.c_header"
    "fsim.runtime"
    "fsim.msvc-debug-contract"
    "fsim.msvc-release-contract"
    "fsim.windows-llvm-contract"
    "fsim.tool-portability-contract"
    "fsim.resource-portability-contract"
    "FSIM-OLDER-MODE-MIXED-PASS"
    "FSIM-OLDER-STANDARD-ARTIFACT-MATRIX-PASS modes=6"
    "FSIM_WITNESS_COUNT EQUAL 16")
  string(FIND "${FSIM_CLOSURE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "standard-mode closure runner lost token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.verilog-systemverilog-standard-mode-closure-matrix"
    "RunVerilogSystemVerilogStandardModeClosureMatrix.cmake"
    "RUN_SERIAL TRUE"
    "TIMEOUT 7200")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "standard-mode closure registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "### Batch 167 - Older Verilog and SystemVerilog standard modes"
    "Verilog-1995"
    "Verilog-2001"
    "Verilog-2001-noconfig"
    "SystemVerilog-2005"
    "SystemVerilog-2009"
    "SystemVerilog-2012"
    "Verilog-2005 and SystemVerilog-2017 as the"
    "arbitrary-width behavior and exact four-state semantics"
    "switches must be explicit, deterministic and provenance-bearing")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 167 plan lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_CHANGE RANGE 1 20)
  string(FIND "${FSIM_PLAN_TEXT}" "**Change ${FSIM_CHANGE}"
    FSIM_CHANGE_OFFSET)
  if(FSIM_CHANGE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "Batch 167 plan lost the exact Change ${FSIM_CHANGE} allocation")
  endif()
endforeach()

set(FSIM_MODE_HEADER
  "id\tmodes\tdomain\tboundary\tclosure\tobligation\timplementation_owner\tpositive_evidence\tnegative_evidence\texecution_evidence\tdiagnostic_owner\tresource_owner")
set(FSIM_COMPATIBILITY_HEADER
  "id\tswitches\tdomain\tboundary\tclosure\tobligation\tselection_owner\tsemantic_owner\tpositive_evidence\tnegative_evidence\texecution_evidence\tprovenance_owner\tresource_owner")
set(FSIM_EXPECTED_MODES
  "V1995,V2001,V2001NOCONFIG,SV2005,SV2009,SV2012")
set(FSIM_EXPECTED_SWITCHES
  "keyword_profile,implicit_net,port_connection,sizing,lifetime,scheduler_assertion,configuration")

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
    "standard-mode inventory must contain SPDX, header and 17 rows; got ${FSIM_MODE_ROW_COUNT}")
endif()
list(GET FSIM_MODE_ROWS 0 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "standard-mode inventory lost its SPDX policy")
endif()
list(GET FSIM_MODE_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL FSIM_MODE_HEADER)
  message(FATAL_ERROR "standard-mode inventory header changed")
endif()

set(FSIM_MODE_IDS)
set(FSIM_MODE_CLOSURES)
set(FSIM_MODE_ACTIVE_COUNT 0)
foreach(FSIM_INDEX RANGE 2 18)
  list(GET FSIM_MODE_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 12)
    message(FATAL_ERROR
      "standard-mode row ${FSIM_INDEX} does not have twelve fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_MODES)
  list(GET FSIM_FIELDS 2 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 3 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 4 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 5 FSIM_OBLIGATION)
  set(FSIM_CHANGE ${FSIM_INDEX})
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  if(FSIM_CHANGE LESS_EQUAL 18)
    set(FSIM_EXPECTED_BOUNDARY "preserved")
  else()
    set(FSIM_EXPECTED_BOUNDARY "active")
  endif()
  if(NOT FSIM_ID STREQUAL "VSVMODE-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_MODES STREQUAL FSIM_EXPECTED_MODES OR
     FSIM_DOMAIN STREQUAL "" OR
     NOT FSIM_BOUNDARY STREQUAL FSIM_EXPECTED_BOUNDARY OR
     NOT FSIM_CLOSURE STREQUAL "B167-C${FSIM_CHANGE_TEXT}" OR
     FSIM_OBLIGATION STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has a drifted scope or closure field")
  endif()
  list(FIND FSIM_MODE_IDS "${FSIM_ID}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate standard-mode id: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_MODE_IDS "${FSIM_ID}")
  list(APPEND FSIM_MODE_CLOSURES "${FSIM_CLOSURE}")
  if(FSIM_BOUNDARY STREQUAL "active")
    math(EXPR FSIM_MODE_ACTIVE_COUNT "${FSIM_MODE_ACTIVE_COUNT} + 1")
  endif()
  list(GET FSIM_FIELDS 6 FSIM_OWNER)
  fsim_require_inventory_path(
    "${FSIM_ID}" "implementation" "${FSIM_OWNER}" "(src|tests|cmake)/")
  foreach(FSIM_EVIDENCE_INDEX RANGE 7 9)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE)
    fsim_require_inventory_path(
      "${FSIM_ID}" "evidence-${FSIM_EVIDENCE_INDEX}"
      "${FSIM_EVIDENCE}" "tests/")
  endforeach()
  list(GET FSIM_FIELDS 10 FSIM_DIAGNOSTIC)
  list(GET FSIM_FIELDS 11 FSIM_RESOURCE)
  if(NOT FSIM_DIAGNOSTIC STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_RESOURCE STREQUAL "cmake/CheckResourcePortabilityContract.cmake")
    message(FATAL_ERROR "${FSIM_ID} drifted diagnostic or resource ownership")
  endif()
endforeach()

file(STRINGS "${FSIM_COMPATIBILITY_INVENTORY}" FSIM_COMPATIBILITY_ROWS)
list(LENGTH FSIM_COMPATIBILITY_ROWS FSIM_COMPATIBILITY_ROW_COUNT)
if(NOT FSIM_COMPATIBILITY_ROW_COUNT EQUAL 19)
  message(FATAL_ERROR
    "compatibility inventory must contain SPDX, header and 17 rows; got ${FSIM_COMPATIBILITY_ROW_COUNT}")
endif()
list(GET FSIM_COMPATIBILITY_ROWS 0 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "compatibility inventory lost its SPDX policy")
endif()
list(GET FSIM_COMPATIBILITY_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL FSIM_COMPATIBILITY_HEADER)
  message(FATAL_ERROR "compatibility inventory header changed")
endif()

set(FSIM_COMPATIBILITY_IDS)
set(FSIM_COMPATIBILITY_CLOSURES)
set(FSIM_COMPATIBILITY_ACTIVE_COUNT 0)
foreach(FSIM_INDEX RANGE 2 18)
  list(GET FSIM_COMPATIBILITY_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 13)
    message(FATAL_ERROR
      "compatibility row ${FSIM_INDEX} does not have thirteen fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_SWITCHES)
  list(GET FSIM_FIELDS 2 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 3 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 4 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 5 FSIM_OBLIGATION)
  set(FSIM_CHANGE ${FSIM_INDEX})
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  if(FSIM_CHANGE LESS_EQUAL 18)
    set(FSIM_EXPECTED_BOUNDARY "preserved")
  else()
    set(FSIM_EXPECTED_BOUNDARY "active")
  endif()
  if(NOT FSIM_ID STREQUAL "VSVCOMP-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_SWITCHES STREQUAL FSIM_EXPECTED_SWITCHES OR
     FSIM_DOMAIN STREQUAL "" OR
     NOT FSIM_BOUNDARY STREQUAL FSIM_EXPECTED_BOUNDARY OR
     NOT FSIM_CLOSURE STREQUAL "B167-C${FSIM_CHANGE_TEXT}" OR
     FSIM_OBLIGATION STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} has a drifted scope or closure field")
  endif()
  list(FIND FSIM_COMPATIBILITY_IDS "${FSIM_ID}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate compatibility id: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_COMPATIBILITY_IDS "${FSIM_ID}")
  list(APPEND FSIM_COMPATIBILITY_CLOSURES "${FSIM_CLOSURE}")
  if(FSIM_BOUNDARY STREQUAL "active")
    math(EXPR FSIM_COMPATIBILITY_ACTIVE_COUNT
      "${FSIM_COMPATIBILITY_ACTIVE_COUNT} + 1")
  endif()
  foreach(FSIM_OWNER_INDEX RANGE 6 7)
    list(GET FSIM_FIELDS ${FSIM_OWNER_INDEX} FSIM_OWNER)
    fsim_require_inventory_path(
      "${FSIM_ID}" "owner-${FSIM_OWNER_INDEX}"
      "${FSIM_OWNER}" "(src|tests|cmake)/")
  endforeach()
  foreach(FSIM_EVIDENCE_INDEX RANGE 8 10)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE)
    fsim_require_inventory_path(
      "${FSIM_ID}" "evidence-${FSIM_EVIDENCE_INDEX}"
      "${FSIM_EVIDENCE}" "tests/")
  endforeach()
  list(GET FSIM_FIELDS 11 FSIM_PROVENANCE)
  list(GET FSIM_FIELDS 12 FSIM_RESOURCE)
  if(NOT FSIM_PROVENANCE STREQUAL "docs/language-support.md" OR
     NOT FSIM_RESOURCE STREQUAL "cmake/CheckResourcePortabilityContract.cmake")
    message(FATAL_ERROR "${FSIM_ID} drifted provenance or resource ownership")
  endif()
endforeach()

foreach(FSIM_CHANGE RANGE 2 18)
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  list(FIND FSIM_MODE_CLOSURES "B167-C${FSIM_CHANGE_TEXT}"
    FSIM_MODE_CLOSURE_INDEX)
  list(FIND FSIM_COMPATIBILITY_CLOSURES "B167-C${FSIM_CHANGE_TEXT}"
    FSIM_COMPATIBILITY_CLOSURE_INDEX)
  if(FSIM_MODE_CLOSURE_INDEX EQUAL -1 OR
     FSIM_COMPATIBILITY_CLOSURE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Change ${FSIM_CHANGE} is not owned exactly once by both inventories")
  endif()
endforeach()

if(NOT FSIM_MODE_ACTIVE_COUNT EQUAL 0 OR
   NOT FSIM_COMPATIBILITY_ACTIVE_COUNT EQUAL 0)
  message(FATAL_ERROR
    "Batch 167 Change 18 inventories must retain zero active rows")
endif()

set(FSIM_CORPUS_HEADER
  "revision\tpositive_stage\tpositive_evidence\tpositive_anchor\tnegative_stage\tnegative_evidence\tnegative_anchor\tdiagnostic_code\tdiagnostic_line\tdiagnostic_column\texecution_evidence\texecution_anchor\tarbitrary_width_evidence\tarbitrary_width_anchor\tinclude_provenance_evidence\tinclude_provenance_anchor\tartifact_mismatch_evidence\tartifact_mismatch_anchor")
file(STRINGS "${FSIM_REVISION_CORPUS}" FSIM_CORPUS_ROWS)
list(LENGTH FSIM_CORPUS_ROWS FSIM_CORPUS_ROW_COUNT)
if(NOT FSIM_CORPUS_ROW_COUNT EQUAL 8)
  message(FATAL_ERROR
    "Verilog/SystemVerilog revision corpus must contain SPDX, header and six rows")
endif()
list(GET FSIM_CORPUS_ROWS 0 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "Verilog/SystemVerilog revision corpus lost its SPDX policy")
endif()
list(GET FSIM_CORPUS_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL FSIM_CORPUS_HEADER)
  message(FATAL_ERROR "Verilog/SystemVerilog revision corpus header changed")
endif()
set(FSIM_CORPUS_IDS V1995 V2001 V2001NOCONFIG SV2005 SV2009 SV2012)
set(FSIM_CORPUS_CODES
  FSIM-SV-PARSE-346 FSIM-SV-PARSE-346 FSIM-SV-PARSE-348
  FSIM-SV-PARSE-348 FSIM-SV-PARSE-346 FSIM-SV-PARSE-001)
set(FSIM_CORPUS_LINES 1 1 2 1 1 1)
set(FSIM_CORPUS_COLUMNS 21 23 1 21 25 45)
foreach(FSIM_INDEX RANGE 2 7)
  list(GET FSIM_CORPUS_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 18)
    message(FATAL_ERROR "revision corpus row ${FSIM_INDEX} is malformed")
  endif()
  math(EXPR FSIM_CORPUS_INDEX "${FSIM_INDEX} - 2")
  list(GET FSIM_CORPUS_IDS ${FSIM_CORPUS_INDEX} FSIM_EXPECTED_ID)
  list(GET FSIM_CORPUS_CODES ${FSIM_CORPUS_INDEX} FSIM_EXPECTED_CODE)
  list(GET FSIM_CORPUS_LINES ${FSIM_CORPUS_INDEX} FSIM_EXPECTED_LINE)
  list(GET FSIM_CORPUS_COLUMNS ${FSIM_CORPUS_INDEX} FSIM_EXPECTED_COLUMN)
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_POSITIVE_STAGE)
  list(GET FSIM_FIELDS 4 FSIM_NEGATIVE_STAGE)
  list(GET FSIM_FIELDS 7 FSIM_CODE)
  list(GET FSIM_FIELDS 8 FSIM_LINE)
  list(GET FSIM_FIELDS 9 FSIM_COLUMN)
  if(NOT FSIM_ID STREQUAL FSIM_EXPECTED_ID OR
     NOT FSIM_POSITIVE_STAGE MATCHES "^(parse|semantic|elaboration|execution)$" OR
     NOT FSIM_NEGATIVE_STAGE MATCHES "^(parse|semantic|elaboration|execution)$" OR
     NOT FSIM_CODE STREQUAL FSIM_EXPECTED_CODE OR
     NOT FSIM_LINE STREQUAL FSIM_EXPECTED_LINE OR
     NOT FSIM_COLUMN STREQUAL FSIM_EXPECTED_COLUMN)
    message(FATAL_ERROR "${FSIM_ID} revision corpus ownership drifted")
  endif()
  foreach(FSIM_PATH_INDEX IN ITEMS 2 5 10 12 14 16)
    math(EXPR FSIM_ANCHOR_INDEX "${FSIM_PATH_INDEX} + 1")
    list(GET FSIM_FIELDS ${FSIM_PATH_INDEX} FSIM_EVIDENCE)
    list(GET FSIM_FIELDS ${FSIM_ANCHOR_INDEX} FSIM_ANCHOR)
    fsim_require_evidence_anchor(
      "${FSIM_ID}-${FSIM_PATH_INDEX}" "${FSIM_EVIDENCE}" "${FSIM_ANCHOR}")
  endforeach()
endforeach()

set(FSIM_COMPATIBILITY_CORPUS_HEADER
  "switch\tpositive_stage\tpositive_evidence\tpositive_anchor\tnegative_stage\tnegative_evidence\tnegative_anchor\tdiagnostic_code\tdiagnostic_line\tdiagnostic_column\texecution_evidence\texecution_anchor\tarbitrary_width_evidence\tarbitrary_width_anchor\tprovenance_evidence\tprovenance_anchor\tartifact_mismatch_evidence\tartifact_mismatch_anchor")
file(STRINGS "${FSIM_COMPATIBILITY_CORPUS}" FSIM_CORPUS_ROWS)
list(LENGTH FSIM_CORPUS_ROWS FSIM_CORPUS_ROW_COUNT)
if(NOT FSIM_CORPUS_ROW_COUNT EQUAL 9)
  message(FATAL_ERROR
    "compatibility corpus must contain SPDX, header and seven rows")
endif()
list(GET FSIM_CORPUS_ROWS 0 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "compatibility corpus lost its SPDX policy")
endif()
list(GET FSIM_CORPUS_ROWS 1 FSIM_HEADER)
if(NOT FSIM_HEADER STREQUAL FSIM_COMPATIBILITY_CORPUS_HEADER)
  message(FATAL_ERROR "compatibility corpus header changed")
endif()
set(FSIM_CORPUS_SWITCHES
  keyword-profile implicit-net port-connection sizing lifetime
  scheduler-assertion configuration)
foreach(FSIM_INDEX RANGE 2 8)
  list(GET FSIM_CORPUS_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 18)
    message(FATAL_ERROR "compatibility corpus row ${FSIM_INDEX} is malformed")
  endif()
  math(EXPR FSIM_CORPUS_INDEX "${FSIM_INDEX} - 2")
  list(GET FSIM_CORPUS_SWITCHES ${FSIM_CORPUS_INDEX} FSIM_EXPECTED_SWITCH)
  list(GET FSIM_FIELDS 0 FSIM_SWITCH)
  list(GET FSIM_FIELDS 1 FSIM_POSITIVE_STAGE)
  list(GET FSIM_FIELDS 4 FSIM_NEGATIVE_STAGE)
  list(GET FSIM_FIELDS 7 FSIM_CODE)
  list(GET FSIM_FIELDS 8 FSIM_LINE)
  list(GET FSIM_FIELDS 9 FSIM_COLUMN)
  if(NOT FSIM_SWITCH STREQUAL FSIM_EXPECTED_SWITCH OR
     NOT FSIM_POSITIVE_STAGE STREQUAL "parse" OR
     NOT FSIM_NEGATIVE_STAGE STREQUAL "parse" OR
     NOT FSIM_CODE STREQUAL "FSIM-SV-PARSE-346" OR
     NOT FSIM_LINE STREQUAL "1" OR
     NOT FSIM_COLUMN STREQUAL "31")
    message(FATAL_ERROR "${FSIM_SWITCH} compatibility corpus ownership drifted")
  endif()
  foreach(FSIM_PATH_INDEX IN ITEMS 2 5 10 12 14 16)
    math(EXPR FSIM_ANCHOR_INDEX "${FSIM_PATH_INDEX} + 1")
    list(GET FSIM_FIELDS ${FSIM_PATH_INDEX} FSIM_EVIDENCE)
    list(GET FSIM_FIELDS ${FSIM_ANCHOR_INDEX} FSIM_ANCHOR)
    fsim_require_evidence_anchor(
      "${FSIM_SWITCH}-${FSIM_PATH_INDEX}" "${FSIM_EVIDENCE}" "${FSIM_ANCHOR}")
  endforeach()
endforeach()

file(SHA256 "${FSIM_MODE_INVENTORY}" FSIM_MODE_SHA256)
file(SHA256 "${FSIM_COMPATIBILITY_INVENTORY}" FSIM_COMPATIBILITY_SHA256)
file(SHA256 "${FSIM_REVISION_CORPUS}" FSIM_REVISION_CORPUS_SHA256)
file(SHA256 "${FSIM_COMPATIBILITY_CORPUS}" FSIM_COMPATIBILITY_CORPUS_SHA256)
set(FSIM_DOCUMENTATION_TEXT)
foreach(FSIM_DOCUMENT IN LISTS FSIM_DOCUMENTS)
  file(READ "${FSIM_DOCUMENT}" FSIM_DOCUMENT_TEXT)
  string(APPEND FSIM_DOCUMENTATION_TEXT "\n${FSIM_DOCUMENT_TEXT}")
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "Older Verilog and SystemVerilog selectable modes"
    "Older Verilog and SystemVerilog revision identity"
    "17 preserved and zero active obligations"
    "six-row revision corpus"
    "seven-row switch corpus"
    "16-witness serial"
    "fsim::provenance PATH"
    "size-gated append-only fields"
    "full historical tool emulation"
    "${FSIM_MODE_SHA256}"
    "${FSIM_COMPATIBILITY_SHA256}"
    "${FSIM_REVISION_CORPUS_SHA256}"
    "${FSIM_COMPATIBILITY_CORPUS_SHA256}")
  string(FIND "${FSIM_DOCUMENTATION_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "synchronized standard-mode documentation lost token: ${FSIM_TOKEN}")
  endif()
endforeach()
message(STATUS
  "Verilog/SystemVerilog standard-mode inventory: ${FSIM_MODE_SHA256}; active=${FSIM_MODE_ACTIVE_COUNT}")
message(STATUS
  "Verilog/SystemVerilog compatibility inventory: ${FSIM_COMPATIBILITY_SHA256}; active=${FSIM_COMPATIBILITY_ACTIVE_COUNT}")
message(STATUS
  "Verilog/SystemVerilog Change 17 corpora: six revision rows and seven "
  "compatibility-switch rows freeze positive/negative stages, exact diagnostics, "
  "execution, arbitrary-width, provenance and artifact-mismatch evidence")
message(STATUS
  "Verilog/SystemVerilog Batch 167 synchronization: 17+17 preserved inventory "
  "rows, zero active obligations, 6+7 corpus rows, 16 retained-log closure "
  "witnesses, six synchronized public documents and four exact digests")

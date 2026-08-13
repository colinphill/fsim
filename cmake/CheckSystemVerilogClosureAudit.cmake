# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MATRIX
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/systemverilog_release_closure.tsv")
set(FSIM_GAPS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/systemverilog_gap_inventory.tsv")
set(FSIM_WIDTHS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/systemverilog_literal_width_inventory.tsv")
set(FSIM_TYPED
  "${FSIM_SOURCE_DIR}/tests/app/typed_boundary_application_test.cpp")
set(FSIM_ARTIFACT
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
set(FSIM_RUNNER "${FSIM_SOURCE_DIR}/cmake/RunSystemVerilogClosureMatrix.cmake")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_RUNTIME_CMAKE "${FSIM_SOURCE_DIR}/tests/runtime/CMakeLists.txt")
set(FSIM_RELEASE_AUDIT
  "${FSIM_SOURCE_DIR}/docs/v1-systemverilog-release-audit.md")
set(FSIM_LANGUAGE_SUPPORT "${FSIM_SOURCE_DIR}/docs/language-support.md")
set(FSIM_FEATURE_MATRIX "${FSIM_SOURCE_DIR}/docs/feature-matrix.md")
set(FSIM_ARCHITECTURE "${FSIM_SOURCE_DIR}/docs/architecture.md")
set(FSIM_VPI_GUIDE "${FSIM_SOURCE_DIR}/docs/systemverilog-vpi.md")
set(FSIM_DPI_GUIDE "${FSIM_SOURCE_DIR}/docs/systemverilog-dpi.md")
set(FSIM_UVM_TUTORIAL "${FSIM_SOURCE_DIR}/docs/uvm-tutorial.md")
set(FSIM_EVIDENCE_README
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/README.md")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_MATRIX}" "${FSIM_GAPS}" "${FSIM_WIDTHS}" "${FSIM_TYPED}"
    "${FSIM_ARTIFACT}" "${FSIM_RUNNER}" "${FSIM_TEST_CMAKE}"
    "${FSIM_RUNTIME_CMAKE}" "${FSIM_RELEASE_AUDIT}"
    "${FSIM_LANGUAGE_SUPPORT}" "${FSIM_FEATURE_MATRIX}"
    "${FSIM_ARCHITECTURE}" "${FSIM_VPI_GUIDE}" "${FSIM_DPI_GUIDE}"
    "${FSIM_UVM_TUTORIAL}" "${FSIM_EVIDENCE_README}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "SystemVerilog closure input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_MATRIX}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 53)
  message(FATAL_ERROR
    "SystemVerilog release closure must contain SPDX, one header, and 51 rows; "
    "got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tkind\tscope\tclosure\tpositive_ctest\tnegative_ctest\texecution_ctest\tstages\ttranscript_owner\tlimit_profile")
  message(FATAL_ERROR "SystemVerilog release-closure schema changed")
endif()

file(READ "${FSIM_GAPS}" FSIM_GAP_CONTENTS)
file(READ "${FSIM_WIDTHS}" FSIM_WIDTH_CONTENTS)
file(SHA256 "${FSIM_GAPS}" FSIM_GAP_DIGEST)
file(SHA256 "${FSIM_WIDTHS}" FSIM_WIDTH_DIGEST)
file(SHA256 "${FSIM_MATRIX}" FSIM_MATRIX_DIGEST)
set(FSIM_IDS)
set(FSIM_WITNESS_TESTS)
set(FSIM_CLAUSE_ROWS 0)
set(FSIM_WIDTH_ROWS 0)
set(FSIM_WITNESS_CELLS 0)
foreach(FSIM_INDEX RANGE 2 52)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 10)
    message(FATAL_ERROR
      "SystemVerilog closure row ${FSIM_INDEX} does not have ten fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_KIND)
  list(GET FSIM_FIELDS 3 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 7 FSIM_STAGES)
  list(GET FSIM_FIELDS 8 FSIM_TRANSCRIPT_OWNER)
  list(GET FSIM_FIELDS 9 FSIM_LIMIT_PROFILE)
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_ID_INDEX)
  if(NOT FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate SystemVerilog closure row ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  if(FSIM_KIND STREQUAL "clause")
    math(EXPR FSIM_CLAUSE_ROWS "${FSIM_CLAUSE_ROWS} + 1")
    string(FIND "${FSIM_GAP_CONTENTS}" "\n${FSIM_ID}\t" FSIM_INVENTORY_INDEX)
  elseif(FSIM_KIND STREQUAL "width")
    math(EXPR FSIM_WIDTH_ROWS "${FSIM_WIDTH_ROWS} + 1")
    string(FIND "${FSIM_WIDTH_CONTENTS}" "\n${FSIM_ID}\t" FSIM_INVENTORY_INDEX)
  else()
    message(FATAL_ERROR "${FSIM_ID} has unknown kind ${FSIM_KIND}")
  endif()
  if(FSIM_INVENTORY_INDEX EQUAL -1 OR FSIM_CLOSURE STREQUAL "")
    message(FATAL_ERROR "${FSIM_ID} is absent from its inventory or closure")
  endif()
  if(NOT FSIM_STAGES STREQUAL "governed-17" OR
     NOT FSIM_TRANSCRIPT_OWNER STREQUAL
       "fsim.application.typed_boundaries+fsim.application.artifact_phases" OR
     NOT FSIM_LIMIT_PROFILE STREQUAL "as6g-delta1000-vcd64-time7200")
    message(FATAL_ERROR "${FSIM_ID} drifted from the governed matrix profile")
  endif()
  foreach(FSIM_FIELD_INDEX RANGE 4 6)
    list(GET FSIM_FIELDS ${FSIM_FIELD_INDEX} FSIM_WITNESS)
    if(FSIM_WITNESS STREQUAL "")
      message(FATAL_ERROR "${FSIM_ID} has an empty witness cell")
    endif()
    list(APPEND FSIM_WITNESS_TESTS "${FSIM_WITNESS}")
    math(EXPR FSIM_WITNESS_CELLS "${FSIM_WITNESS_CELLS} + 1")
  endforeach()
endforeach()
if(NOT FSIM_CLAUSE_ROWS EQUAL 30 OR NOT FSIM_WIDTH_ROWS EQUAL 21 OR
   NOT FSIM_WITNESS_CELLS EQUAL 153)
  message(FATAL_ERROR
    "expected 30 clause rows, 21 width rows, and 153 witnesses; found "
    "${FSIM_CLAUSE_ROWS}/${FSIM_WIDTH_ROWS}/${FSIM_WITNESS_CELLS}")
endif()

list(REMOVE_DUPLICATES FSIM_WITNESS_TESTS)
list(SORT FSIM_WITNESS_TESTS)
set(FSIM_EXPECTED_TESTS
  fsim.application.artifact_phases
  fsim.application.assertions
  fsim.application.classes
  fsim.application.expressions
  fsim.application.sv_containers
  fsim.application.systemverilog_hir
  fsim.application.typed_boundaries
  fsim.application.vpi
  fsim.frontend
  fsim.library.artifact
  fsim.llvm
  fsim.runtime
  fsim.semantic
  fsim.uvm-phase-tlm-matrix)
list(SORT FSIM_EXPECTED_TESTS)
if(NOT FSIM_WITNESS_TESTS STREQUAL FSIM_EXPECTED_TESTS)
  message(FATAL_ERROR "SystemVerilog closure witness CTest set drifted")
endif()

file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
string(TOLOWER "${FSIM_MATRIX_CONTENTS}" FSIM_MATRIX_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "xfail" "expected-fail" "waiver" "allowlist" "suppress")
  string(FIND "${FSIM_MATRIX_LOWER}" "${FSIM_FORBIDDEN}" FSIM_INDEX)
  if(NOT FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog closure matrix contains forbidden escape ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_TYPED}" FSIM_TYPED_CONTENTS)
file(READ "${FSIM_ARTIFACT}" FSIM_ARTIFACT_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "FSIM-SYSTEMVERILOG-2017-PASS"
    "debug/vcd/multiple-root/uvm/mixed-vhdl/mixed-systemc/public-api"
    "resources=as6g/delta1000/vcd64 gaps=0 widths=129logic9")
  string(FIND "${FSIM_TYPED_CONTENTS}" "${FSIM_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "typed-boundary transcript lost ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "FSIM-SYSTEMVERILOG-2017-ARTIFACT-PASS"
    "stages=object/library/design/relocation/replay/checkpoint"
    "resources=as6g gaps=0 widths=exact-xz-logic9")
  string(FIND "${FSIM_ARTIFACT_CONTENTS}" "${FSIM_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "artifact transcript lost ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CONTENTS)
file(READ "${FSIM_RUNTIME_CMAKE}" FSIM_RUNTIME_CONTENTS)
string(APPEND FSIM_TEST_CONTENTS "\n${FSIM_RUNTIME_CONTENTS}")
file(READ "${FSIM_RUNNER}" FSIM_RUNNER_CONTENTS)
foreach(FSIM_WITNESS IN LISTS FSIM_EXPECTED_TESTS)
  string(FIND "${FSIM_TEST_CONTENTS}" "${FSIM_WITNESS}" FSIM_REGISTERED)
  string(FIND "${FSIM_RUNNER_CONTENTS}" "${FSIM_WITNESS}" FSIM_RUN)
  if(FSIM_REGISTERED EQUAL -1 OR FSIM_RUN EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog closure witness is not registered and run: ${FSIM_WITNESS}")
  endif()
endforeach()

file(READ "${FSIM_RELEASE_AUDIT}" FSIM_AUDIT_CONTENTS)
foreach(FSIM_DIGEST IN ITEMS
    "${FSIM_GAP_DIGEST}" "${FSIM_WIDTH_DIGEST}" "${FSIM_MATRIX_DIGEST}")
  string(FIND "${FSIM_AUDIT_CONTENTS}" "${FSIM_DIGEST}" FSIM_DIGEST_INDEX)
  if(FSIM_DIGEST_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog release audit lost current digest ${FSIM_DIGEST}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "30 supported clause/integration"
    "21 preserved"
    "153 positive, negative, and execution witness cells"
    "Exactly 14"
    "registered tests cover 17"
    "165.34 seconds"
    "2,246 production diagnostics, 887 bounded"
    "1,035 SPDX-owned files, and 332 authored test/control files"
    "not SystemVerilog legality limits")
  string(FIND "${FSIM_AUDIT_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog release audit lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_DOCUMENTATION_CONTENTS)
foreach(FSIM_DOCUMENT IN ITEMS
    "${FSIM_LANGUAGE_SUPPORT}" "${FSIM_FEATURE_MATRIX}"
    "${FSIM_ARCHITECTURE}" "${FSIM_VPI_GUIDE}" "${FSIM_DPI_GUIDE}"
    "${FSIM_UVM_TUTORIAL}" "${FSIM_EVIDENCE_README}")
  file(READ "${FSIM_DOCUMENT}" FSIM_DOCUMENT_CONTENTS)
  string(APPEND FSIM_DOCUMENTATION_CONTENTS "\n${FSIM_DOCUMENT_CONTENTS}")
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "SystemVerilog-2017 release audit"
    "30 supported"
    "21 preserved"
    "zero active"
    "physical host/resource ceilings"
    "SystemVerilog legality."
    "systemverilog_release_closure.tsv")
  string(FIND "${FSIM_DOCUMENTATION_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "synchronized SystemVerilog documentation lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/CMakeLists.txt" FSIM_ROOT_CMAKE)
foreach(FSIM_TOKEN IN ITEMS "DIRECTORY docs/" "PATTERN \"*.md\"")
  string(FIND "${FSIM_ROOT_CMAKE}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "installed SystemVerilog Markdown policy lost: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "SystemVerilog closure audit: 51 rows, 153 witnesses, 14 registered tests, "
  "17 governed stages, 30 supported rows, 21 preserved width paths, zero "
  "active, synchronized public documentation and exact digests")

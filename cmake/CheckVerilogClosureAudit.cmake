# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MATRIX
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_release_closure.tsv")
set(FSIM_GAP_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_gap_inventory.tsv")
set(FSIM_WIDTH_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_literal_width_inventory.tsv")
set(FSIM_TYPED
  "${FSIM_SOURCE_DIR}/tests/app/typed_boundary_application_test.cpp")
set(FSIM_ARTIFACT
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
set(FSIM_LIMITS
  "${FSIM_SOURCE_DIR}/tests/app/governed_process_limits.hpp")
set(FSIM_RUNNER "${FSIM_SOURCE_DIR}/cmake/RunVerilogClosureMatrix.cmake")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_RUNTIME_CMAKE
  "${FSIM_SOURCE_DIR}/tests/runtime/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_MATRIX}" "${FSIM_GAP_INVENTORY}" "${FSIM_WIDTH_INVENTORY}"
    "${FSIM_TYPED}" "${FSIM_ARTIFACT}" "${FSIM_LIMITS}"
    "${FSIM_RUNNER}" "${FSIM_TEST_CMAKE}" "${FSIM_RUNTIME_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "Verilog closure input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_MATRIX}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 48)
  message(FATAL_ERROR
    "Verilog release closure must contain SPDX, one header, and 46 rows; "
    "got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tkind\tscope\tclosure\tpositive_ctest\tnegative_ctest\texecution_ctest\tstages\ttranscript_owner\tlimit_profile")
  message(FATAL_ERROR "Verilog release-closure schema changed")
endif()

file(READ "${FSIM_GAP_INVENTORY}" FSIM_GAP_CONTENTS)
file(READ "${FSIM_WIDTH_INVENTORY}" FSIM_WIDTH_CONTENTS)
set(FSIM_IDS)
set(FSIM_WITNESS_TESTS)
set(FSIM_CLAUSE_ROWS 0)
set(FSIM_WIDTH_ROWS 0)
set(FSIM_WITNESS_CELLS 0)
foreach(FSIM_INDEX RANGE 2 47)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 10)
    message(FATAL_ERROR
      "Verilog closure row ${FSIM_INDEX} does not have ten fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_KIND)
  list(GET FSIM_FIELDS 3 FSIM_CLOSURE)
  list(GET FSIM_FIELDS 7 FSIM_STAGES)
  list(GET FSIM_FIELDS 8 FSIM_TRANSCRIPT_OWNER)
  list(GET FSIM_FIELDS 9 FSIM_LIMIT_PROFILE)
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_ID_INDEX)
  if(NOT FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate Verilog closure row ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")

  if(FSIM_KIND STREQUAL "clause")
    math(EXPR FSIM_CLAUSE_ROWS "${FSIM_CLAUSE_ROWS} + 1")
    string(FIND "${FSIM_GAP_CONTENTS}" "\n${FSIM_ID}\t"
      FSIM_INVENTORY_INDEX)
  elseif(FSIM_KIND STREQUAL "width")
    math(EXPR FSIM_WIDTH_ROWS "${FSIM_WIDTH_ROWS} + 1")
    string(FIND "${FSIM_WIDTH_CONTENTS}" "\n${FSIM_ID}\t"
      FSIM_INVENTORY_INDEX)
  else()
    message(FATAL_ERROR "${FSIM_ID} has unknown kind ${FSIM_KIND}")
  endif()
  if(FSIM_INVENTORY_INDEX EQUAL -1 OR FSIM_CLOSURE STREQUAL "")
    message(FATAL_ERROR
      "${FSIM_ID} is absent from its inventory or has no closure")
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
if(NOT FSIM_CLAUSE_ROWS EQUAL 34 OR NOT FSIM_WIDTH_ROWS EQUAL 12 OR
   NOT FSIM_WITNESS_CELLS EQUAL 138)
  message(FATAL_ERROR
    "expected 34 clause rows, 12 width rows, and 138 witnesses; found "
    "${FSIM_CLAUSE_ROWS}/${FSIM_WIDTH_ROWS}/${FSIM_WITNESS_CELLS}")
endif()

list(REMOVE_DUPLICATES FSIM_WITNESS_TESTS)
list(SORT FSIM_WITNESS_TESTS)
set(FSIM_EXPECTED_TESTS
  fsim.api
  fsim.api.c_header
  fsim.application
  fsim.application.artifact_phases
  fsim.application.display
  fsim.application.expressions
  fsim.application.fork
  fsim.application.line_directives
  fsim.application.ordering_interactions
  fsim.application.procedural_assignments
  fsim.application.resolution
  fsim.application.specialization
  fsim.application.specify
  fsim.application.sv_files
  fsim.application.systemverilog_hir
  fsim.application.transition_delays
  fsim.application.typed_boundaries
  fsim.application.vpi
  fsim.elaboration
  fsim.frontend
  fsim.library.artifact
  fsim.llvm
  fsim.runtime)
list(SORT FSIM_EXPECTED_TESTS)
if(NOT FSIM_WITNESS_TESTS STREQUAL FSIM_EXPECTED_TESTS)
  message(FATAL_ERROR "Verilog closure witness CTest set drifted")
endif()

file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
string(TOLOWER "${FSIM_MATRIX_CONTENTS}" FSIM_MATRIX_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "xfail" "expected-fail" "waiver" "allowlist" "suppress")
  string(FIND "${FSIM_MATRIX_LOWER}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog closure matrix contains forbidden escape ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_TYPED}" FSIM_TYPED_CONTENTS)
file(READ "${FSIM_ARTIFACT}" FSIM_ARTIFACT_CONTENTS)
file(READ "${FSIM_LIMITS}" FSIM_LIMIT_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "FSIM-VERILOG-2005-PASS"
    "stages=direct/interpreter/llvm-o0/llvm-o2/cache-cold/cache-warm/"
    "debug/vcd/multiple-root/mixed-vhdl/mixed-systemc"
    "resources=as6g/delta1000/vcd64 gaps=0 widths=137xz")
  string(FIND "${FSIM_TYPED_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "typed-boundary transcript lost ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "FSIM-VERILOG-2005-ARTIFACT-PASS"
    "stages=object/library/design/relocation/replay/checkpoint"
    "resources=as6g gaps=0 widths=exact-xz")
  string(FIND "${FSIM_ARTIFACT_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "artifact transcript lost ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "6ULL * 1024ULL * 1024ULL * 1024ULL"
    "getrlimit(RLIMIT_AS"
    "JOB_OBJECT_LIMIT_PROCESS_MEMORY")
  string(FIND "${FSIM_LIMIT_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "governed process limit lost ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_RUNNER}" FSIM_RUNNER_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_RUNTIME_CMAKE}" FSIM_RUNTIME_CMAKE_CONTENTS)
string(APPEND FSIM_TEST_CMAKE_CONTENTS "\n${FSIM_RUNTIME_CMAKE_CONTENTS}")
foreach(FSIM_WITNESS IN LISTS FSIM_EXPECTED_TESTS)
  string(FIND "${FSIM_RUNNER_CONTENTS}" "${FSIM_WITNESS}"
    FSIM_RUNNER_INDEX)
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_WITNESS}"
    FSIM_REGISTRATION_INDEX)
  if(FSIM_RUNNER_INDEX EQUAL -1 OR FSIM_REGISTRATION_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog closure witness is not run and registered: ${FSIM_WITNESS}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.verilog-closure-audit"
    "NAME fsim.verilog-closure-matrix"
    "RunVerilogClosureMatrix.cmake"
    "RUN_SERIAL TRUE"
    "TIMEOUT 7200"
    "fsim.application.artifact_phases"
    "fsim.application.typed_boundaries"
    "TIMEOUT 1200")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "Verilog closure CTest contract lost ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_COMPOSED_OUTPUT)
foreach(FSIM_GATE IN ITEMS
    CheckVerilogGapInventory.cmake
    CheckDiagnosticCatalog.cmake
    CheckSourceLineBudget.cmake
    CheckV1ConformanceAudit.cmake
    CheckResourcePortabilityContract.cmake)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}"
    RESULT_VARIABLE FSIM_GATE_RESULT
    OUTPUT_VARIABLE FSIM_GATE_OUTPUT
    ERROR_VARIABLE FSIM_GATE_ERROR)
  if(NOT FSIM_GATE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Verilog closure composed gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
  string(APPEND FSIM_COMPOSED_OUTPUT
    "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}\n")
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "34 supported, 0 active, and 3 deferred"
    "12 preserved, 0 active, and 3 physical"
    "diagnostic catalog covers 2246 production codes"
    "against the 2500-line hard limit with a 2000-line refactor target"
    "v1 conformance audit:"
    "resource portability contract:")
  string(FIND "${FSIM_COMPOSED_OUTPUT}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog closure lost composed evidence: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "Verilog closure audit: 46 rows, 138 witnesses, 23 registered tests, "
  "17 governed stages, 34 supported clauses, 12 preserved width paths, "
  "6 GiB address space, 1000 deltas, 64 trace signals, 7200 seconds, and "
  "zero unresolved rows")

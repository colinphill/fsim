# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_CTEST_COMMAND FSIM_BINARY_DIR FSIM_OUTPUT_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "Verilog closure fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/RunClosureWitnesses.cmake")

set(FSIM_STAGE_TIMEOUT_SECONDS 1200)
set(FSIM_WITNESSES
  fsim.application.systemverilog_hir
  fsim.frontend
  fsim.library.artifact
  fsim.elaboration
  fsim.llvm
  fsim.application.vpi
  fsim.application
  fsim.application.specialization
  fsim.application.artifact_phases
  fsim.application.expressions
  fsim.application.specify
  fsim.application.fork
  fsim.application.display
  fsim.application.line_directives
  fsim.application.transition_delays
  fsim.application.procedural_assignments
  fsim.application.resolution
  fsim.application.ordering_interactions
  fsim.application.sv_files
  fsim.application.typed_boundaries
  fsim.api
  fsim.api.c_header
  fsim.runtime)

file(REMOVE_RECURSE "${FSIM_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}")
file(WRITE "${FSIM_OUTPUT_DIR}/stage-results.tsv"
  "test\tresult\tlog\n")
fsim_run_closure_matrix(
  "Verilog closure" "${FSIM_STAGE_TIMEOUT_SECONDS}"
  ${FSIM_WITNESSES})

foreach(FSIM_TRANSCRIPT IN ITEMS
    "FSIM-VERILOG-2005-PASS stages=direct/interpreter/llvm-o0/llvm-o2/"
    "debug/vcd/multiple-root/mixed-vhdl/mixed-systemc"
    "resources=as6g/delta1000/vcd64 gaps=0 widths=137xz"
    "FSIM-VERILOG-2005-ARTIFACT-PASS stages=object/library/design/"
    "relocation/replay/checkpoint"
    "resources=as6g gaps=0 widths=exact-xz")
  string(FIND "${FSIM_COMBINED_OUTPUT}" "${FSIM_TRANSCRIPT}"
    FSIM_TRANSCRIPT_INDEX)
  if(FSIM_TRANSCRIPT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog closure matrix lost transcript token: ${FSIM_TRANSCRIPT}")
  endif()
endforeach()

list(LENGTH FSIM_WITNESSES FSIM_WITNESS_COUNT)
if(NOT FSIM_WITNESS_COUNT EQUAL 23 OR NOT FSIM_COMPLETED EQUAL 23)
  message(FATAL_ERROR
    "Verilog closure matrix expected 23 witnesses; completed ${FSIM_COMPLETED}")
endif()

message(STATUS
  "Verilog closure matrix: 23/23 witnesses, 17 governed stages, "
  "1200-second stage timeout, retained evidence ${FSIM_OUTPUT_DIR}")

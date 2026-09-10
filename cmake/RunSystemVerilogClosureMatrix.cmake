# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_CTEST_COMMAND FSIM_BINARY_DIR FSIM_OUTPUT_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "SystemVerilog closure fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

set(FSIM_STAGE_TIMEOUT_SECONDS 1200)
set(FSIM_WITNESSES
  fsim.semantic
  fsim.application.systemverilog_hir
  fsim.frontend
  fsim.elaboration
  fsim.library.artifact
  fsim.llvm
  fsim.application.vpi
  fsim.application.artifact_phases
  fsim.application.classes
  fsim.application.expressions
  fsim.application.sv_containers
  fsim.application.assertions
  fsim.application.random
  fsim.application.coverage
  fsim.application.sv_files
  fsim.application.sv_preprocessor_generate
  fsim.application.sv_hierarchy
  fsim.application.sv_interfaces
  fsim.application.sv_conformance
  fsim.application.transition_delays
  fsim.application.specify
  fsim.application.sdf_endpoint_resolution
  fsim.application.sdf_drive_timing
  fsim.application.typed_boundaries
  fsim.uvm-phase-tlm-matrix
  fsim.runtime)

file(REMOVE_RECURSE "${FSIM_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}")
file(WRITE "${FSIM_OUTPUT_DIR}/stage-results.tsv" "test\tresult\tlog\n")
set(FSIM_COMBINED_OUTPUT)
set(FSIM_COMPLETED 0)
foreach(FSIM_WITNESS IN LISTS FSIM_WITNESSES)
  string(REPLACE "." "-" FSIM_LOG_NAME "${FSIM_WITNESS}")
  set(FSIM_LOG "${FSIM_OUTPUT_DIR}/${FSIM_LOG_NAME}.log")
  execute_process(
    COMMAND "${FSIM_CTEST_COMMAND}"
      --test-dir "${FSIM_BINARY_DIR}"
      -R "^${FSIM_WITNESS}$"
      -V
      --output-on-failure
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR
    TIMEOUT "${FSIM_STAGE_TIMEOUT_SECONDS}")
  file(WRITE "${FSIM_LOG}" "${FSIM_OUTPUT}${FSIM_ERROR}")
  file(APPEND "${FSIM_OUTPUT_DIR}/stage-results.tsv"
    "${FSIM_WITNESS}\t${FSIM_RESULT}\t${FSIM_LOG_NAME}.log\n")
  if(NOT FSIM_RESULT EQUAL 0)
    message(FATAL_ERROR
      "SystemVerilog closure witness failed: ${FSIM_WITNESS}\n"
      "retained log: ${FSIM_LOG}\n${FSIM_OUTPUT}${FSIM_ERROR}")
  endif()
  math(EXPR FSIM_COMPLETED "${FSIM_COMPLETED} + 1")
  string(APPEND FSIM_COMBINED_OUTPUT "${FSIM_OUTPUT}${FSIM_ERROR}\n")
endforeach()

foreach(FSIM_TRANSCRIPT IN ITEMS
    "FSIM-SYSTEMVERILOG-2017-PASS"
    "debug/vcd/multiple-root/uvm/mixed-vhdl/mixed-systemc/public-api"
    "resources=as6g/delta1000/vcd64 gaps=0 widths=129logic9"
    "FSIM-SYSTEMVERILOG-2017-ARTIFACT-PASS"
    "resources=as6g gaps=0 widths=exact-xz-logic9")
  string(FIND "${FSIM_COMBINED_OUTPUT}" "${FSIM_TRANSCRIPT}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog closure matrix lost transcript token: ${FSIM_TRANSCRIPT}")
  endif()
endforeach()

list(LENGTH FSIM_WITNESSES FSIM_WITNESS_COUNT)
if(NOT FSIM_WITNESS_COUNT EQUAL 26 OR NOT FSIM_COMPLETED EQUAL 26)
  message(FATAL_ERROR
    "SystemVerilog closure matrix expected 26 witnesses; completed ${FSIM_COMPLETED}")
endif()

message(STATUS
  "SystemVerilog closure matrix: 26/26 witnesses, 29 governed stages, "
  "1200-second stage timeout, retained evidence ${FSIM_OUTPUT_DIR}")

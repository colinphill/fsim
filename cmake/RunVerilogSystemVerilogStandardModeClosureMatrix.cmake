# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_CTEST_COMMAND FSIM_BINARY_DIR FSIM_OUTPUT_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

set(FSIM_STAGE_TIMEOUT_SECONDS 1200)
set(FSIM_WITNESSES
  fsim.verilog-systemverilog-standard-mode-inventory
  fsim.frontend
  fsim.application.sv_conformance
  fsim.application.typed_boundaries
  fsim.application.artifact_phases
  fsim.llvm
  fsim.application.vpi
  fsim.application.tcl
  fsim.api
  fsim.api.c_header
  fsim.runtime
  fsim.msvc-debug-contract
  fsim.msvc-release-contract
  fsim.windows-llvm-contract
  fsim.tool-portability-contract
  fsim.resource-portability-contract)

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
      "Verilog/SystemVerilog standard-mode closure witness failed: "
      "${FSIM_WITNESS}\nretained log: ${FSIM_LOG}\n"
      "${FSIM_OUTPUT}${FSIM_ERROR}")
  endif()
  math(EXPR FSIM_COMPLETED "${FSIM_COMPLETED} + 1")
  string(APPEND FSIM_COMBINED_OUTPUT "${FSIM_OUTPUT}${FSIM_ERROR}\n")
endforeach()

foreach(FSIM_TRANSCRIPT IN ITEMS
    "FSIM-OLDER-MODE-MIXED-PASS"
    "modes=v1995/v2001/v2001-noconfig/sv2005/sv2009/sv2012"
    "profile=all-explicit stages=interpreter/llvm-o0/llvm-o2/"
    "multiple-root/mixed-vhdl/mixed-systemc widths=137xz time=1ns"
    "FSIM-OLDER-STANDARD-ARTIFACT-MATRIX-PASS modes=6"
    "stages=compile/object/elaborate/design/simulate/cache-cold/"
    "cache-warm/relocation/checkpoint-replay engines=interpreter-llvm"
    "profile=sizing producers=hidden"
    "C API tests passed"
    "runtime tests passed")
  string(FIND "${FSIM_COMBINED_OUTPUT}" "${FSIM_TRANSCRIPT}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog/SystemVerilog standard-mode closure matrix lost transcript "
      "token: ${FSIM_TRANSCRIPT}")
  endif()
endforeach()

list(LENGTH FSIM_WITNESSES FSIM_WITNESS_COUNT)
if(NOT FSIM_WITNESS_COUNT EQUAL 16 OR NOT FSIM_COMPLETED EQUAL 16)
  message(FATAL_ERROR
    "Verilog/SystemVerilog standard-mode closure matrix expected 16 "
    "witnesses; completed ${FSIM_COMPLETED}")
endif()

message(STATUS
  "Verilog/SystemVerilog standard-mode closure matrix: 16/16 witnesses, "
  "six older revisions, seven explicit compatibility switches, "
  "1200-second stage timeout, 6-GiB governed processes, delta1000 work and "
  "vcd64 trace ceilings, retained evidence ${FSIM_OUTPUT_DIR}")

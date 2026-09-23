# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_CTEST_COMMAND FSIM_BINARY_DIR FSIM_OUTPUT_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "VHDL standard-mode fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/RunClosureWitnesses.cmake")

set(FSIM_STAGE_TIMEOUT_SECONDS 1200)
set(FSIM_WITNESSES
  fsim.vhdl-standard-mode-inventory
  fsim.frontend
  fsim.application.vhdl_ieee_integration
  fsim.application.vhdl_numeric
  fsim.application.vhdl_logic9
  fsim.application.artifact_phases
  fsim.llvm
  fsim.api
  fsim.api.c_header
  fsim.tcl.version-selection
  fsim.msvc-debug-contract
  fsim.msvc-release-contract
  fsim.windows-llvm-contract
  fsim.tool-portability-contract
  fsim.contract.language-resources)

file(REMOVE_RECURSE "${FSIM_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}")
file(WRITE "${FSIM_OUTPUT_DIR}/stage-results.tsv" "test\tresult\tlog\n")
fsim_run_closure_matrix(
  "VHDL standard-mode closure" "${FSIM_STAGE_TIMEOUT_SECONDS}"
  ${FSIM_WITNESSES})

foreach(FSIM_TRANSCRIPT IN ITEMS
    "FSIM-VHDL-OLDER-ENVIRONMENT-PASS revisions=1987/1993/2000/2002"
    "packages=std_logic_signed/std_logic_unsigned/std_logic_arith/std_logic_misc"
    "FSIM-VHDL-SYNOPSYS-PACKAGES-PASS"
    "engines=interpreter/llvm-o0/llvm-o2 widths=137"
    "nulls=6 directions=ascending/descending"
    "ambiguity=FSIM-ELAB-VHSYN-001 resources=as6g/delta1000"
    "FSIM-VHDL-OLDER-MODES-PASS revisions=1987/1993/2000/2002"
    "stages=mixed-systemverilog/debug/vhpi/vcd widths=137logic9"
    "directions=ascending/descending resources=as6g/delta1000/vcd64"
    "FSIM-VHDL-OLDER-MODES-ARTIFACT-PASS"
    "stages=object/library/design/cache-cold/cache-warm/relocation/replay/"
    "checkpoint/public-debug-vhpi-vcd"
    "resources=as6g provenance=exact")
  string(FIND "${FSIM_COMBINED_OUTPUT}" "${FSIM_TRANSCRIPT}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL standard-mode closure matrix lost transcript token: "
      "${FSIM_TRANSCRIPT}")
  endif()
endforeach()

list(LENGTH FSIM_WITNESSES FSIM_WITNESS_COUNT)
if(NOT FSIM_WITNESS_COUNT EQUAL 15 OR NOT FSIM_COMPLETED EQUAL 15)
  message(FATAL_ERROR
    "VHDL standard-mode closure matrix expected 15 witnesses; completed "
    "${FSIM_COMPLETED}")
endif()

message(STATUS
  "VHDL standard-mode closure matrix: 15/15 witnesses, four older revisions, "
  "four Synopsys packages, 1200-second stage timeout, 6-GiB governed "
  "processes, delta1000 work and vcd64 trace ceilings, retained evidence "
  "${FSIM_OUTPUT_DIR}")

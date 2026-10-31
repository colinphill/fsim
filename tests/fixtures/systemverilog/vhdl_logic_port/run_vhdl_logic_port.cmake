# SPDX-License-Identifier: Apache-2.0
#
# Standard-logic functions (TO_BIT, IS_X, TO_X01, TO_UX01, unary AND,
# TO_BITVECTOR, elements of their results) on VHDL std_logic in ports driven
# from SystemVerilog. The
# ports alias four-state nets; the reports must equal those of the same
# entity driven from VHDL, on the interpreter, the v4 kernel and the
# reference scheduler.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the logic port fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/fsim-vhdl-logic-port-${workspace_id}")

function(run_fsim_step CASE_DIR STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${CASE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
      "${STEP_NAME} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${CASE_DIR}")
  endif()
  # The report texts, without source locations and times.
  string(REGEX MATCHALL "b='[^\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

foreach(top IN ITEMS sv vhdl)
  set(case_dir "${workspace}/${top}")
  file(MAKE_DIRECTORY "${case_dir}")
  foreach(file IN ITEMS logic_port_dut.vhd logic_port_tb.sv logic_port_tbv.vhd)
    file(COPY_FILE "${FSIM_FIXTURE_DIR}/${file}" "${case_dir}/${file}")
  endforeach()
endforeach()

set(case_dir "${workspace}/sv")
run_fsim_step("${case_dir}" compile_vhdl compile -q --standard 2008 logic_port_dut.vhd)
run_fsim_step("${case_dir}" compile_sv compile -q logic_port_tb.sv)
run_fsim_step("${case_dir}" elaborate
  elaborate -q --top logic_port_tb --no-aot --optimization O2)
run_fsim_step("${case_dir}" sv_interpreter simulate --engine interpreter)
run_fsim_step("${case_dir}" sv_kernel simulate --engine v4)
set(ENV{FSIM_STATIC_KERNEL} 0)
run_fsim_step("${case_dir}" elaborate_scheduler
  elaborate -q --top logic_port_tb --no-aot --optimization O2)
run_fsim_step("${case_dir}" sv_scheduler simulate --engine v4)
unset(ENV{FSIM_STATIC_KERNEL})

set(case_dir "${workspace}/vhdl")
run_fsim_step("${case_dir}" compile_vhdl
  compile -q --standard 2008 logic_port_dut.vhd logic_port_tbv.vhd)
run_fsim_step("${case_dir}" elaborate
  elaborate -q --top "vhdl:work.logic_port_tbv(sim)" --no-aot --optimization O2)
run_fsim_step("${case_dir}" vhdl_reference simulate --engine v4)

string(FIND "${vhdl_reference_reports}" "b='1' is_x=false x01='1'" one)
string(FIND "${vhdl_reference_reports}" "x01='X' ux01='X'" unknown)
# v = "X001": TO_BITVECTOR gives "0001" (3 downto 0), TO_X01 "X001" (1 to 4).
string(FIND "${vhdl_reference_reports}" "i0='1' i3='0' m1='X' m4='1'" element)
if(one EQUAL -1 OR unknown EQUAL -1 OR element EQUAL -1)
  message(FATAL_ERROR "the VHDL reference reports are incomplete:\n"
    "${vhdl_reference_reports}\nworkspace: ${workspace}")
endif()
foreach(run IN ITEMS sv_interpreter sv_kernel sv_scheduler)
  if(NOT "${${run}_reports}" STREQUAL "${vhdl_reference_reports}")
    message(FATAL_ERROR
      "${run}: the reports differ from the VHDL-driven reference\n"
      "Reference:\n${vhdl_reference_reports}\n${run}:\n${${run}_reports}\n"
      "workspace: ${workspace}")
  endif()
endforeach()

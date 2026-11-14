# SPDX-License-Identifier: Apache-2.0
#
# SystemVerilog <-> VHDL ports under fsim's cross-language contract
# (docs/cross-language-semantics.md): the LRM port rules and the logic
# mapping. A VHDL std_logic in port reading a SystemVerilog variable or net
# initializes on its default 'U', and the SystemVerilog time-zero writes
# reach it as changes; SystemVerilog reads the shared value in four states.
# A VHDL out or inout driver of a SystemVerilog net starts at its port's
# default, and its 'L', 'H' and 'W' are weak on the net. Every case runs on
# the interpreter, the v4 kernel and the reference scheduler, and its
# reports must equal <case>.expected. The order of the two languages'
# processes within a time step is unspecified, so the mixed cases read the
# ports between time steps and report time-0 activity only that way.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the port time-zero fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/fsim-vhdl-port-time0-${workspace_id}")
fsim_test_workspace("${workspace}")

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
  # The report and display texts, without source locations.
  string(REGEX MATCHALL "(quiet|stable)=[^\n]*|(sv|vhdl) [^\n]*" reports
    "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

# case | sources (VHDL first) | top
set(cases
  "time0_initializer|stable_leaf.vhd,time0_initializer.sv|sv:work.top"
  "time0_initial_write|stable_leaf.vhd,time0_initial_write.sv|sv:work.top"
  "time0_net|stable_leaf.vhd,time0_net.sv|sv:work.top"
  "time0_unwritten|stable_leaf.vhd,time0_unwritten.sv|sv:work.top"
  "time0_sv_only|time0_sv_only.sv|sv:work.top"
  "time0_vhdl|stable_leaf.vhd,time0_vhdl.vhd|vhdl:work.top(sim)"
  "time0_vhdl_parent|sv_leaf.sv,time0_vhdl_parent.vhd|vhdl:work.top(sim)"
  "inout_net|inout_leaf.vhd,inout_net.sv|sv:work.top"
  "inout_vhdl_parent|inout_sv_leaf.sv,inout_vhdl_parent.vhd|vhdl:work.top(sim)"
  "inout_bus|inout_bus_leaf.vhd,inout_bus.sv|sv:work.top")

foreach(entry IN LISTS cases)
  string(REPLACE "|" ";" fields "${entry}")
  list(GET fields 0 case)
  list(GET fields 1 sources)
  list(GET fields 2 top)
  string(REPLACE "," ";" sources "${sources}")
  set(case_dir "${workspace}/${case}")
  file(MAKE_DIRECTORY "${case_dir}")
  foreach(file IN LISTS sources)
    file(COPY_FILE "${FSIM_FIXTURE_DIR}/${file}" "${case_dir}/${file}")
    if(file MATCHES "\\.vhd$")
      run_fsim_step("${case_dir}" compile compile -q --standard 2008 "${file}")
    else()
      run_fsim_step("${case_dir}" compile compile -q "${file}")
    endif()
  endforeach()
  run_fsim_step("${case_dir}" elaborate elaborate -q "${top}" --no-aot)
  run_fsim_step("${case_dir}" interpreter simulate --engine interpreter)
  run_fsim_step("${case_dir}" kernel simulate --engine v4)
  set(ENV{FSIM_STATIC_KERNEL} 0)
  run_fsim_step("${case_dir}" elaborate_scheduler elaborate -q "${top}" --no-aot)
  run_fsim_step("${case_dir}" scheduler simulate --engine v4)
  unset(ENV{FSIM_STATIC_KERNEL})

  file(READ "${FSIM_FIXTURE_DIR}/${case}.expected" expected)
  string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
  string(STRIP "${expected}" expected)
  foreach(run IN ITEMS interpreter kernel scheduler)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "${case} (${run}): the reports differ from ${case}.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endforeach()

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

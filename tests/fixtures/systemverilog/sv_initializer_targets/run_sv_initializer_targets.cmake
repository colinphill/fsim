# SPDX-License-Identifier: Apache-2.0
#
# Static variable declaration initializers that are not elaboration
# constants (initializer_targets.sv, IEEE 1800-2023 6.8, 6.21): plain,
# resolved and aggregate targets are initialized before any procedure is
# armed, with no event for a SystemVerilog procedure and no transaction for
# a VHDL process (initializer_targets.vhd). The reports of the interpreter,
# the compiled kernel and the compiled kernel with FSIM_KERNEL_VERIFY must
# equal initializer_targets.expected.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the initializer target fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

file(READ "${FSIM_FIXTURE_DIR}/initializer_targets.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-sv-initializer-targets-${workspace_id}")
fsim_test_workspace("${case_dir}")
foreach(source IN ITEMS initializer_targets.sv initializer_targets.vhd)
  configure_file("${FSIM_FIXTURE_DIR}/${source}" "${case_dir}/${source}"
    COPYONLY)
endforeach()

function(run_fsim_step STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
      "${STEP_NAME} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${case_dir}")
  endif()
  string(REGEX MATCHALL "sit [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

run_fsim_step(compile_vhdl compile -q initializer_targets.vhd)
run_fsim_step(compile compile -q --standard 2023 initializer_targets.sv)
run_fsim_step(elaborate elaborate -q work.top --no-aot)
run_fsim_step(interpreter simulate --engine interpreter)
run_fsim_step(kernel simulate --engine v4)
set(ENV{FSIM_KERNEL_VERIFY} 1)
run_fsim_step(verified simulate --engine v4)
unset(ENV{FSIM_KERNEL_VERIFY})
foreach(run IN ITEMS interpreter kernel verified)
  if(NOT "${${run}_reports}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "initializer_targets (${run}): the reports differ from "
      "initializer_targets.expected\n"
      "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
      "workspace: ${case_dir}")
  endif()
endforeach()

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

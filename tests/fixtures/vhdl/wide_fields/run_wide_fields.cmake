# SPDX-License-Identifier: Apache-2.0
#
# Slice and element reads and writes of 65, 128, 129 and 200 element
# std_logic_vectors in the v4 kernel (wide_fields.vhd), against element-by-element
# oracles. The summary lines of the interpreter, the compiled kernel and the
# compiled kernel with FSIM_KERNEL_VERIFY (every run replayed on the reference
# evaluator and compared register by register, so each KOp::wide_alu is
# checked against the reference value functions) must equal wide_fields.expected:
# zero mismatches at every width. The order of the four instances' lines is
# unspecified, so the lines are sorted.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the VHDL wide_fields fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-vhdl-wide-fields-${workspace_id}")
fsim_test_workspace("${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/wide_fields.vhd" "${case_dir}/wide_fields.vhd")

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
  string(REGEX MATCHALL "wide_fields W=[0-9]+ checks=[0-9]+ mismatches=[0-9]+" reports "${stdout}\n${stderr}")
  list(SORT reports)
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

run_fsim_step(compile compile -q --standard 2008 wide_fields.vhd)
run_fsim_step(elaborate elaborate -q work.top --no-aot)
run_fsim_step(interpreter simulate --engine interpreter)
run_fsim_step(kernel simulate --engine v4)
set(ENV{FSIM_KERNEL_VERIFY} 1)
run_fsim_step(verified simulate --engine v4)
unset(ENV{FSIM_KERNEL_VERIFY})

file(READ "${FSIM_FIXTURE_DIR}/wide_fields.expected" expected)
string(STRIP "${expected}" expected)
foreach(run IN ITEMS interpreter kernel verified)
  if(NOT "${${run}_reports}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "wide_fields (${run}): the reports differ from wide_fields.expected\n"
      "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
      "workspace: ${case_dir}")
  endif()
endforeach()

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

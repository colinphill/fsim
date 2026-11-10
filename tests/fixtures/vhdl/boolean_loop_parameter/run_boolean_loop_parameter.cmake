# SPDX-License-Identifier: Apache-2.0
#
# A for-loop parameter over BOOLEAN is a BOOLEAN wherever its name is used,
# whether its discrete range is given by the type mark, by literals, by
# T'LOW, T'HIGH, T'LEFT and T'RIGHT of BOOLEAN or of a subtype of it, by
# T'RANGE and T'REVERSE_RANGE, or by a subtype's type mark
# (boolean_loop_parameter.vhd, IEEE 1076-2019 10.10, 5.2.1, 16.2.2):
# assignments, array indices, case selectors, NOT, AND, "=", actuals, 'IMAGE
# and 'POS in ascending and descending loops. The reports of the
# interpreter, the compiled kernel and the compiled kernel with
# FSIM_KERNEL_VERIFY (every run replayed on the reference evaluator and
# compared) must equal boolean_loop_parameter.expected.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the BOOLEAN loop parameter fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

file(READ "${FSIM_FIXTURE_DIR}/boolean_loop_parameter.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-boolean-loop-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
configure_file("${FSIM_FIXTURE_DIR}/boolean_loop_parameter.vhd"
  "${case_dir}/boolean_loop_parameter.vhd" COPYONLY)

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
  # The report texts, without source locations.
  string(REGEX MATCHALL "boolloop [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

run_fsim_step(compile compile -q --standard 2008 boolean_loop_parameter.vhd)
run_fsim_step(elaborate elaborate -q work.top --no-aot)
run_fsim_step(interpreter simulate --engine interpreter)
run_fsim_step(kernel simulate --engine v4)
set(ENV{FSIM_KERNEL_VERIFY} 1)
run_fsim_step(verified simulate --engine v4)
unset(ENV{FSIM_KERNEL_VERIFY})
foreach(run IN ITEMS interpreter kernel verified)
  if(NOT "${${run}_reports}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "boolean_loop_parameter (${run}): the reports differ from "
      "boolean_loop_parameter.expected\n"
      "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
      "workspace: ${case_dir}")
  endif()
endforeach()

# SPDX-License-Identifier: Apache-2.0
#
# A for-loop parameter over CHARACTER or a declared enumeration type is a
# value of that type wherever its name is used (enumeration_loop_parameter.vhd,
# IEEE 1076-2019 10.10, 5.2.2, 9.3, 12.5, 16.2.2): case selectors,
# concatenations, comparisons, overloaded calls, assignments and 'POS, 'VAL
# and 'IMAGE in ascending and descending loops. The reports of the
# interpreter, the compiled kernel and the compiled kernel with
# FSIM_KERNEL_VERIFY (every run replayed on the reference evaluator and
# compared) must equal enumeration_loop_parameter.expected, in several
# workspace directories.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the enumeration loop parameter fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

file(READ "${FSIM_FIXTURE_DIR}/enumeration_loop_parameter.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)

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
  # The report texts, without source locations.
  string(REGEX MATCHALL "enumloop [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

# Each workspace has a path of its own, so the compiled objects' order
# differs from one to the next.
foreach(workspace IN ITEMS a bb ccc dddd)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir
    "${CMAKE_CURRENT_BINARY_DIR}/fsim-enumeration-loop-${workspace}-${workspace_id}")
  file(MAKE_DIRECTORY "${case_dir}")
  configure_file("${FSIM_FIXTURE_DIR}/enumeration_loop_parameter.vhd"
    "${case_dir}/enumeration_loop_parameter.vhd" COPYONLY)
  run_fsim_step("${case_dir}" compile
    compile -q --standard 2008 enumeration_loop_parameter.vhd)
  run_fsim_step("${case_dir}" elaborate elaborate -q work.top --no-aot)
  run_fsim_step("${case_dir}" interpreter simulate --engine interpreter)
  run_fsim_step("${case_dir}" kernel simulate --engine v4)
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  run_fsim_step("${case_dir}" verified simulate --engine v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  foreach(run IN ITEMS interpreter kernel verified)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "enumeration_loop_parameter (${run}, workspace ${workspace}): the "
        "reports differ from enumeration_loop_parameter.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endforeach()

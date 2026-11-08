# SPDX-License-Identifier: Apache-2.0
#
# Leftmost values of range-constrained STD_ULOGIC and CHARACTER subtypes
# (subtype_left.vhd): the reports of the interpreter, the compiled kernel,
# the compiled kernel with FSIM_KERNEL_VERIFY (every run replayed on the
# reference evaluator and compared) and the reference scheduler must equal
# subtype_left.expected, derived from IEEE 1076-2008 6.4.2.3, IEEE 1076-2019
# 16.2.2 and 16.3, and the IEEE 1164 subtypes.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the subtype left fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-subtype-left-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/subtype_left.vhd" "${case_dir}/subtype_left.vhd")

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
  # The report texts, without source locations (the ';' after each text is
  # a list separator here, so it is not part of the match).
  string(REGEX MATCHALL "subtype [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

run_fsim_step(compile compile -q --standard 2008 subtype_left.vhd)
run_fsim_step(elaborate elaborate -q work.top --no-aot)
run_fsim_step(interpreter simulate --engine interpreter)
run_fsim_step(kernel simulate --engine v4)
set(ENV{FSIM_KERNEL_VERIFY} 1)
run_fsim_step(verified simulate --engine v4)
unset(ENV{FSIM_KERNEL_VERIFY})
set(ENV{FSIM_STATIC_KERNEL} 0)
run_fsim_step(elaborate_scheduler elaborate -q work.top --no-aot)
run_fsim_step(scheduler simulate --engine v4)
unset(ENV{FSIM_STATIC_KERNEL})

file(READ "${FSIM_FIXTURE_DIR}/subtype_left.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)
foreach(run IN ITEMS interpreter kernel verified scheduler)
  if(NOT "${${run}_reports}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "subtype_left (${run}): the reports differ from subtype_left.expected\n"
      "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
      "workspace: ${case_dir}")
  endif()
endforeach()

# SPDX-License-Identifier: Apache-2.0
#
# The TEXTIO subprograms of IEEE.STD_LOGIC_1164 for STD_ULOGIC and
# STD_ULOGIC_VECTOR (IEEE 1076-2019 16.7). The reports of the interpreter, the
# compiled kernel, the compiled kernel with FSIM_KERNEL_VERIFY (every run
# replayed on the reference evaluator and compared) and the reference
# scheduler must equal std_logic_textio.expected: the "weak" reports of the
# source and the messages that READ, OREAD and HREAD without GOOD report.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the std_logic TextIO fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/fsim-std-logic-textio-${workspace_id}")

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
  string(REGEX MATCHALL "(weak [^;\n]*|STD_LOGIC_1164\\.[^\n]*)" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

foreach(case IN ITEMS std_logic_textio)
  set(case_dir "${workspace}/${case}")
  file(MAKE_DIRECTORY "${case_dir}")
  file(COPY_FILE "${FSIM_FIXTURE_DIR}/${case}.vhd" "${case_dir}/${case}.vhd")
  run_fsim_step("${case_dir}" compile compile -q --standard 2008 "${case}.vhd")
  run_fsim_step("${case_dir}" elaborate elaborate -q work.top --no-aot)
  run_fsim_step("${case_dir}" interpreter simulate --engine interpreter)
  run_fsim_step("${case_dir}" kernel simulate --engine v4)
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  run_fsim_step("${case_dir}" verified simulate --engine v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  set(ENV{FSIM_STATIC_KERNEL} 0)
  run_fsim_step("${case_dir}" elaborate_scheduler elaborate -q work.top --no-aot)
  run_fsim_step("${case_dir}" scheduler simulate --engine v4)
  unset(ENV{FSIM_STATIC_KERNEL})

  file(READ "${FSIM_FIXTURE_DIR}/${case}.expected" expected)
  string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
  string(STRIP "${expected}" expected)
  foreach(run IN ITEMS interpreter kernel verified scheduler)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "${case} (${run}): the reports differ from ${case}.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endforeach()

# SPDX-License-Identifier: Apache-2.0
#
# The matching equality "?=" of 65, 128, 200 and 256 element std_logic_vectors
# holding all nine values (wide_match.vhd), on the interpreter, the compiled v4
# kernel and the compiled kernel under FSIM_KERNEL_VERIFY (every run replayed
# on the reference evaluator and compared register by register, so each
# KOp::wide_alu is checked against binary_value). The summary lines of the
# three runs must equal wide_match.expected (sorted: the order of the four
# instances' lines is unspecified). The kernel run also reports the kernel's
# operations that run through an operation helper (FSIM_PROFILE_PHASES): the
# comparison must be compiled inline, so no `Binary` may be among them.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the VHDL wide_match fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-vhdl-wide-match-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/wide_match.vhd" "${case_dir}/wide_match.vhd")

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
  string(REGEX MATCHALL
    "wide_match W=[0-9]+ checks=[0-9]+ mismatches=[0-9]+ outcomes=[01UX.]+"
    reports "${stdout}\n${stderr}")
  list(SORT reports)
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
  set(${STEP_NAME}_stderr "${stderr}" PARENT_SCOPE)
endfunction()

run_fsim_step(compile compile -q --standard 2008 wide_match.vhd)
run_fsim_step(elaborate elaborate -q work.top --no-aot)
run_fsim_step(interpreter simulate --engine interpreter)
set(ENV{FSIM_PROFILE_PHASES} 1)
run_fsim_step(kernel simulate --engine v4)
unset(ENV{FSIM_PROFILE_PHASES})
set(ENV{FSIM_KERNEL_VERIFY} 1)
run_fsim_step(verified simulate --engine v4)
unset(ENV{FSIM_KERNEL_VERIFY})

file(READ "${FSIM_FIXTURE_DIR}/wide_match.expected" expected)
string(STRIP "${expected}" expected)
foreach(run IN ITEMS interpreter kernel verified)
  if(NOT "${${run}_reports}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "wide_match (${run}): the reports differ from wide_match.expected\n"
      "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
      "workspace: ${case_dir}")
  endif()
endforeach()

# No Binary through the operation helper (a wide comparison that was not
# compiled inline runs there as `generic:Binary`).
if("${kernel_stderr}" MATCHES "reason=generic:Binary")
  message(FATAL_ERROR
    "wide_match: a Binary operation runs through the operation helper\n"
    "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
endif()

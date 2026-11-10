# SPDX-License-Identifier: Apache-2.0
#
# Character literals of BOOLEAN, BIT, CHARACTER and STD_ULOGIC fold by the
# type their context gives them (intrinsic_literal_context.vhd, IEEE
# 1076-2019 9.3.2, 12.5, 16.2.2): the reports of the interpreter, the
# compiled kernel and the compiled kernel with FSIM_KERNEL_VERIFY (every run
# replayed on the reference evaluator and compared) must equal
# intrinsic_literal_context.expected. The design is compiled in several
# workspace directories, whose paths order the compiled objects differently,
# and every one must report the same.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the intrinsic literal context fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

file(READ "${FSIM_FIXTURE_DIR}/intrinsic_literal_context.expected" expected)
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
  string(REGEX MATCHALL "intlit [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

# Each workspace has a path of its own, so the compiled objects' order
# differs from one to the next.
foreach(workspace IN ITEMS a bb ccc dddd)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir
    "${CMAKE_CURRENT_BINARY_DIR}/fsim-intrinsic-literal-${workspace}-${workspace_id}")
  file(MAKE_DIRECTORY "${case_dir}")
  configure_file("${FSIM_FIXTURE_DIR}/intrinsic_literal_context.vhd"
    "${case_dir}/intrinsic_literal_context.vhd" COPYONLY)
  run_fsim_step("${case_dir}" compile
    compile -q --standard 2008 intrinsic_literal_context.vhd)
  run_fsim_step("${case_dir}" elaborate elaborate -q work.top --no-aot)
  run_fsim_step("${case_dir}" interpreter simulate --engine interpreter)
  run_fsim_step("${case_dir}" kernel simulate --engine v4)
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  run_fsim_step("${case_dir}" verified simulate --engine v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  foreach(run IN ITEMS interpreter kernel verified)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "intrinsic_literal_context (${run}, workspace ${workspace}): the "
        "reports differ from intrinsic_literal_context.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endforeach()

# SPDX-License-Identifier: Apache-2.0
#
# A use clause of the design unit being elaborated does not reach the
# declarations of a package it uses (package_use_visibility.vhd, IEEE
# 1076-2019 12.3, 12.4): the reports of the interpreter, the compiled kernel
# and the compiled kernel with FSIM_KERNEL_VERIFY (every run replayed on the
# reference evaluator and compared) must equal package_use_visibility.expected,
# with and without TOP's use clauses for SYM_B and IEEE.VITAL_MEMORY.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the package use visibility fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

file(READ "${FSIM_FIXTURE_DIR}/package_use_visibility.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)
file(READ "${FSIM_FIXTURE_DIR}/package_use_visibility.vhd" source_text)

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
  string(REGEX MATCHALL "pkguse [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

function(check_variant VARIANT SOURCE)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-package-use-${VARIANT}-${workspace_id}")
  file(MAKE_DIRECTORY "${case_dir}")
  file(WRITE "${case_dir}/package_use_visibility.vhd" "${SOURCE}")
  run_fsim_step("${case_dir}" compile compile -q --standard 2008 package_use_visibility.vhd)
  run_fsim_step("${case_dir}" elaborate elaborate -q work.top --no-aot)
  run_fsim_step("${case_dir}" interpreter simulate --engine interpreter)
  run_fsim_step("${case_dir}" kernel simulate --engine v4)
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  run_fsim_step("${case_dir}" verified simulate --engine v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  foreach(run IN ITEMS interpreter kernel verified)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "package_use_visibility ${VARIANT} (${run}): the reports differ from "
        "package_use_visibility.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endfunction()

check_variant(with_uses "${source_text}")
string(REPLACE "use ieee.vital_memory.all;\nuse work.sym_b.all;\n" ""
  without_uses "${source_text}")
if("${without_uses}" STREQUAL "${source_text}")
  message(FATAL_ERROR "the fixture has no SYM_B and VITAL_MEMORY use clauses to remove")
endif()
check_variant(without_uses "${without_uses}")

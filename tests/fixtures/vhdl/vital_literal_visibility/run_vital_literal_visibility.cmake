# SPDX-License-Identifier: Apache-2.0
#
# Character literals shared by STD_ULOGIC, BIT, a user enumeration and
# IEEE.VITAL_MEMORY's symbol type (vital_literal_visibility.vhd): the reports
# of the interpreter, the compiled kernel, the compiled kernel with
# FSIM_KERNEL_VERIFY (every run replayed on the reference evaluator and
# compared) and the reference scheduler must equal
# vital_literal_visibility.expected, derived from IEEE 1076-2019 9.3.2, 12.3,
# 12.4 and 12.5 and the IEEE 1164 tables. The design runs with and without the
# VITAL_MEMORY use clause, and must give the same reports. A literal that fits
# two overloads (`?? '1'`: BIT's and STD_ULOGIC's) is ambiguous (12.5).

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the VITAL literal visibility fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

file(READ "${FSIM_FIXTURE_DIR}/vital_literal_visibility.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)
file(READ "${FSIM_FIXTURE_DIR}/vital_literal_visibility.vhd" source_text)

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
  # The report texts, without source locations (the ';' after each text is
  # a list separator here, so it is not part of the match).
  string(REGEX MATCHALL "vitlit [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

function(check_variant VARIANT SOURCE)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-vital-literals-${VARIANT}-${workspace_id}")
  fsim_test_workspace("${case_dir}")
  file(WRITE "${case_dir}/vital_literal_visibility.vhd" "${SOURCE}")
  run_fsim_step("${case_dir}" compile compile -q --standard 2008 vital_literal_visibility.vhd)
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
  foreach(run IN ITEMS interpreter kernel verified scheduler)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "vital_literal_visibility ${VARIANT} (${run}): the reports differ from "
        "vital_literal_visibility.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endfunction()

check_variant(with_vital "${source_text}")
string(REPLACE "use ieee.vital_memory.all;" "" without_vital "${source_text}")
if("${without_vital}" STREQUAL "${source_text}")
  message(FATAL_ERROR "the fixture has no VITAL_MEMORY use clause to remove")
endif()
check_variant(without_vital "${without_vital}")

# A literal that fits more than one overload, with nothing in its context to
# choose, is ambiguous (12.5): FSIM-ELAB-VHENUM-004, with and without the
# VITAL_MEMORY use clause. `?? '1'` fits BIT's and STD_ULOGIC's condition
# operators; the operand of an unresolved TO_STRING call gives TO_X01('1') no
# result type to choose its BIT or STD_ULOGIC overload by.
function(expect_ambiguous NAME USE_CLAUSES BODY)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-vital-literals-${NAME}-${workspace_id}")
  fsim_test_workspace("${case_dir}")
  file(WRITE "${case_dir}/ambiguous.vhd" "library ieee;
use ieee.std_logic_1164.all;
${USE_CLAUSES}
entity top is end entity;
architecture test of top is
begin
  process
    variable b : boolean;
  begin
    ${BODY}
    wait;
  end process;
end architecture;
")
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" compile -q --standard 2008 ambiguous.vhd
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_out
    ERROR_VARIABLE compile_err)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" elaborate -q work.top --no-aot
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE elaborate_result
    OUTPUT_VARIABLE elaborate_out
    ERROR_VARIABLE elaborate_err)
  set(all_output "${compile_out}${compile_err}${elaborate_out}${elaborate_err}")
  if("${compile_result}" STREQUAL "0" AND "${elaborate_result}" STREQUAL "0")
    message(FATAL_ERROR "${NAME}: the ambiguous literal was accepted\n${all_output}")
  endif()
  if(NOT all_output MATCHES "FSIM-ELAB-VHENUM-004")
    message(FATAL_ERROR
      "${NAME}: the ambiguity was not reported as FSIM-ELAB-VHENUM-004\n${all_output}")
  endif()
endfunction()

expect_ambiguous(condition_vital "use ieee.vital_memory.all;" "b := ?? '1';")
expect_ambiguous(condition_plain "" "b := ?? '1';")
expect_ambiguous(to_x01_vital "use ieee.vital_memory.all;"
  "report to_string(to_x01('1'));")
expect_ambiguous(to_x01_plain "" "report to_string(to_x01('1'));")

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

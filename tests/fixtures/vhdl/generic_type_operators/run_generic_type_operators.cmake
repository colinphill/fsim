# SPDX-License-Identifier: Apache-2.0
#
# The "=" of a generic type is the operation of its actual's type, which an
# explicit "=" declared with that type replaces (generic_type_operators.vhd,
# IEEE 1076-2019 5.8.2, 6.5.3.1, 12.3): the reports of the interpreter, the
# compiled kernel and the compiled kernel with FSIM_KERNEL_VERIFY (every run
# replayed on the reference evaluator and compared) must equal
# generic_type_operators.expected under VHDL-2008 and VHDL-2019, with and
# without TOP's use clause for COLORS. The operators of VHDL-2019's
# classified generic types (classified_generic_operators.vhd) must give
# classified_generic_operators.expected.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the generic type operators fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

function(read_expected NAME OUTPUT)
  file(READ "${FSIM_FIXTURE_DIR}/${NAME}.expected" text)
  string(REGEX REPLACE "^# SPDX[^\n]*\n" "" text "${text}")
  string(STRIP "${text}" text)
  set(${OUTPUT} "${text}" PARENT_SCOPE)
endfunction()
read_expected(generic_type_operators expected)
read_expected(classified_generic_operators classified_expected)
file(READ "${FSIM_FIXTURE_DIR}/generic_type_operators.vhd" source_text)
file(READ "${FSIM_FIXTURE_DIR}/classified_generic_operators.vhd" classified_text)

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
  string(REGEX MATCHALL "(gtop|cgop) [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

function(check_variant VARIANT STANDARD SOURCE EXPECTED)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-generic-type-operators-${VARIANT}-${STANDARD}-${workspace_id}")
  file(MAKE_DIRECTORY "${case_dir}")
  file(WRITE "${case_dir}/generic_type_operators.vhd" "${SOURCE}")
  run_fsim_step("${case_dir}" compile compile -q --standard ${STANDARD} generic_type_operators.vhd)
  run_fsim_step("${case_dir}" elaborate elaborate -q work.top --no-aot)
  run_fsim_step("${case_dir}" interpreter simulate --engine interpreter)
  run_fsim_step("${case_dir}" kernel simulate --engine v4)
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  run_fsim_step("${case_dir}" verified simulate --engine v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  foreach(run IN ITEMS interpreter kernel verified)
    if(NOT "${${run}_reports}" STREQUAL "${EXPECTED}")
      message(FATAL_ERROR
        "generic_type_operators ${VARIANT} ${STANDARD} (${run}): the reports "
        "differ from the expected reports\n"
        "Expected:\n${EXPECTED}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endfunction()

string(REPLACE "use work.colors.all;\n" "" without_use "${source_text}")
if("${without_use}" STREQUAL "${source_text}")
  message(FATAL_ERROR "the fixture has no COLORS use clause to remove")
endif()
foreach(standard IN ITEMS 2008 2019)
  check_variant(with_use ${standard} "${source_text}" "${expected}")
  check_variant(without_use ${standard} "${without_use}" "${expected}")
endforeach()
check_variant(classified 2019 "${classified_text}" "${classified_expected}")

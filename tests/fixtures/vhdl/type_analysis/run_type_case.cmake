# SPDX-License-Identifier: Apache-2.0
#
# Compiles one VHDL type-analysis fixture with the fsim CLI.
#   FSIM_EXECUTABLE   built fsim command
#   FSIM_FIXTURE      fixture file
#   FSIM_STANDARD     VHDL standard for --standard (default 2008)
#   FSIM_EXPECT_CODE  diagnostic code that compile must report (omit: compile must succeed)
#   FSIM_EXPECT_LINE  source line of that diagnostic (optional)

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE OR NOT EXISTS "${FSIM_FIXTURE}")
  message(FATAL_ERROR "FSIM_FIXTURE must name a type-analysis fixture")
endif()
if(NOT DEFINED FSIM_STANDARD)
  set(FSIM_STANDARD 2008)
endif()

get_filename_component(fixture_name "${FSIM_FIXTURE}" NAME_WE)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-type-analysis-${fixture_name}-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")
file(COPY_FILE "${FSIM_FIXTURE}" "${workspace}/${fixture_name}.vhd")

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" compile -q --lang vhdl
    --standard "${FSIM_STANDARD}" "${fixture_name}.vhd"
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
set(all_output "${output}\n${error}")

if(NOT DEFINED FSIM_EXPECT_CODE)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR
      "${fixture_name} should compile, got ${result}:\n${all_output}\n"
      "workspace: ${workspace}")
  endif()
else()
  if(result STREQUAL "0")
    message(FATAL_ERROR
      "${fixture_name} compiled but should report ${FSIM_EXPECT_CODE}\n"
      "workspace: ${workspace}")
  endif()
  if(NOT all_output MATCHES "error\\[${FSIM_EXPECT_CODE}\\]")
    message(FATAL_ERROR
      "${fixture_name} failed without ${FSIM_EXPECT_CODE}:\n${all_output}\n"
      "workspace: ${workspace}")
  endif()
  if(DEFINED FSIM_EXPECT_LINE AND NOT all_output MATCHES
      "${fixture_name}\\.vhd:${FSIM_EXPECT_LINE}:[0-9]+: error\\[${FSIM_EXPECT_CODE}\\]")
    message(FATAL_ERROR
      "${FSIM_EXPECT_CODE} was not reported at line ${FSIM_EXPECT_LINE}:\n"
      "${all_output}\nworkspace: ${workspace}")
  endif()
endif()
file(REMOVE_RECURSE "${workspace}")

# SPDX-License-Identifier: Apache-2.0
#
# Analyzes one invalid VHDL unit with `fsim compile` and requires analysis to
# fail with the diagnostic code named on the fixture's first line:
#
#   -- fsim-expect: <code> [std=<standard>]

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE OR NOT EXISTS "${FSIM_FIXTURE}")
  message(FATAL_ERROR "FSIM_FIXTURE must name an analysis-error fixture")
endif()

file(STRINGS "${FSIM_FIXTURE}" header LIMIT_COUNT 1)
if(NOT header MATCHES "fsim-expect: ([A-Z0-9-]+)")
  message(FATAL_ERROR "${FSIM_FIXTURE} has no 'fsim-expect:' header")
endif()
set(expected "${CMAKE_MATCH_1}")
set(standard_args "")
if(header MATCHES "std=([0-9]+)")
  set(standard_args --standard "${CMAKE_MATCH_1}")
endif()

get_filename_component(fixture_name "${FSIM_FIXTURE}" NAME_WE)
get_filename_component(fixture_file "${FSIM_FIXTURE}" NAME)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-analysis-${fixture_name}-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")
file(COPY_FILE "${FSIM_FIXTURE}" "${workspace}/${fixture_file}")

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" compile -q --lang vhdl ${standard_args}
    "${fixture_file}"
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)
if(result STREQUAL "0")
  message(FATAL_ERROR
    "${fixture_name} was accepted at analysis; expected ${expected}\n"
    "workspace: ${workspace}")
endif()
if(NOT output MATCHES "error\\[${expected}\\]")
  message(FATAL_ERROR
    "${fixture_name} failed without ${expected}:\n${output}\n"
    "workspace: ${workspace}")
endif()
file(REMOVE_RECURSE "${workspace}")

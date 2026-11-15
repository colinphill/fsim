# SPDX-License-Identifier: Apache-2.0
#
# Compiles one SystemVerilog design with `fsim compile` and requires
# `fsim elaborate` of its top to fail with the diagnostic code, and the
# text, named on the fixture's first two lines:
#
#   // fsim-expect: <code> top=<module>
#   // fsim-expect-text: <text the diagnostic must contain>

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE OR NOT EXISTS "${FSIM_FIXTURE}")
  message(FATAL_ERROR "FSIM_FIXTURE must name an elaboration-error fixture")
endif()

file(STRINGS "${FSIM_FIXTURE}" header LIMIT_COUNT 2)
list(GET header 0 first)
if(NOT first MATCHES "fsim-expect: ([A-Z0-9-]+) top=([A-Za-z_][A-Za-z0-9_]*)")
  message(FATAL_ERROR "${FSIM_FIXTURE} has no 'fsim-expect: <code> top=' header")
endif()
set(expected "${CMAKE_MATCH_1}")
set(top "${CMAKE_MATCH_2}")
set(expected_text "")
list(LENGTH header header_lines)
if(header_lines GREATER 1)
  list(GET header 1 second)
  if(second MATCHES "fsim-expect-text: (.+)$")
    set(expected_text "${CMAKE_MATCH_1}")
  endif()
endif()

get_filename_component(fixture_name "${FSIM_FIXTURE}" NAME_WE)
get_filename_component(fixture_file "${FSIM_FIXTURE}" NAME)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-elaboration-${fixture_name}-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")
file(COPY_FILE "${FSIM_FIXTURE}" "${workspace}/${fixture_file}")

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" compile -q "${fixture_file}"
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR
    "${fixture_name} failed to compile:\n${output}\nworkspace: ${workspace}")
endif()
execute_process(
  COMMAND "${FSIM_EXECUTABLE}" elaborate "${top}"
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)
if(result STREQUAL "0")
  message(FATAL_ERROR
    "${fixture_name} was accepted at elaboration; expected ${expected}\n"
    "workspace: ${workspace}")
endif()
if(NOT output MATCHES "error\\[${expected}\\]")
  message(FATAL_ERROR
    "${fixture_name} failed without ${expected}:\n${output}\n"
    "workspace: ${workspace}")
endif()
if(NOT expected_text STREQUAL "")
  string(FIND "${output}" "${expected_text}" text_at)
  if(text_at EQUAL -1)
    message(FATAL_ERROR
      "${fixture_name} reported ${expected} without '${expected_text}':\n"
      "${output}\nworkspace: ${workspace}")
  endif()
endif()
file(REMOVE_RECURSE "${workspace}")

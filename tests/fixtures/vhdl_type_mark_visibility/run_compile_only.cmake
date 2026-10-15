# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE OR NOT EXISTS "${FSIM_FIXTURE}")
  message(FATAL_ERROR "FSIM_FIXTURE must name a legal VHDL analysis control")
endif()

get_filename_component(fixture_name "${FSIM_FIXTURE}" NAME)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-type-mark-${fixture_name}-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")
file(COPY_FILE "${FSIM_FIXTURE}" "${workspace}/${fixture_name}")

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" compile -q
    --lang vhdl --standard 1993 "${fixture_name}"
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR
    "${fixture_name} should compile, got ${result}:\n${output}\n${error}\n"
    "workspace: ${workspace}")
endif()
file(REMOVE_RECURSE "${workspace}")

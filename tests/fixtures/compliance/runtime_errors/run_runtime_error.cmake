# SPDX-License-Identifier: Apache-2.0
#
# Compiles and elaborates one design and requires `fsim simulate` on the
# engine FSIM_ENGINE to fail with a run-time error whose text contains the
# text named on the fixture's first two lines:
#
#   // fsim-expect-run: top=<module> std=<standard>
#   // fsim-expect-text: <text the error must contain>

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE OR NOT EXISTS "${FSIM_FIXTURE}")
  message(FATAL_ERROR "FSIM_FIXTURE must name a run-time error fixture")
endif()
if(NOT DEFINED FSIM_ENGINE)
  set(FSIM_ENGINE interpreter)
endif()

file(STRINGS "${FSIM_FIXTURE}" header LIMIT_COUNT 2)
list(LENGTH header header_lines)
if(header_lines LESS 2)
  message(FATAL_ERROR "${FSIM_FIXTURE} has no two-line fsim-expect-run header")
endif()
list(GET header 0 first)
list(GET header 1 second)
if(NOT first MATCHES "fsim-expect-run: top=([A-Za-z_][A-Za-z0-9_]*) std=([0-9]+)")
  message(FATAL_ERROR "${FSIM_FIXTURE} has no 'fsim-expect-run: top= std=' header")
endif()
set(top "${CMAKE_MATCH_1}")
set(standard "${CMAKE_MATCH_2}")
if(NOT second MATCHES "fsim-expect-text: (.+)$")
  message(FATAL_ERROR "${FSIM_FIXTURE} has no 'fsim-expect-text:' header")
endif()
set(expected_text "${CMAKE_MATCH_1}")

get_filename_component(fixture_name "${FSIM_FIXTURE}" NAME_WE)
get_filename_component(fixture_file "${FSIM_FIXTURE}" NAME)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-runtime-error-${fixture_name}-${FSIM_ENGINE}-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")
file(COPY_FILE "${FSIM_FIXTURE}" "${workspace}/${fixture_file}")

foreach(step IN ITEMS compile elaborate)
  if(step STREQUAL "compile")
    set(arguments compile -q --standard "${standard}" "${fixture_file}")
  else()
    set(arguments elaborate -q --top "${top}")
  endif()
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${arguments}
    WORKING_DIRECTORY "${workspace}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR
      "${fixture_name} failed at ${step}:\n${output}\nworkspace: ${workspace}")
  endif()
endforeach()
execute_process(
  COMMAND "${FSIM_EXECUTABLE}" simulate --engine "${FSIM_ENGINE}"
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)
if(result STREQUAL "0" AND NOT output MATCHES "(error|failure|fatal)\\[")
  message(FATAL_ERROR
    "${fixture_name} ran without a run-time error on ${FSIM_ENGINE}:\n"
    "${output}\nworkspace: ${workspace}")
endif()
string(FIND "${output}" "${expected_text}" text_at)
if(text_at EQUAL -1)
  message(FATAL_ERROR
    "${fixture_name} failed on ${FSIM_ENGINE} without '${expected_text}':\n"
    "${output}\nworkspace: ${workspace}")
endif()
if(output MATCHES "(^|\n)PASS(\r?\n|$)")
  message(FATAL_ERROR
    "${fixture_name} reached PASS on ${FSIM_ENGINE} after the error:\n${output}")
endif()
file(REMOVE_RECURSE "${workspace}")

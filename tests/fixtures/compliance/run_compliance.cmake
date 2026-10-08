# SPDX-License-Identifier: Apache-2.0
#
# Runs one self-checking language-compliance fixture through the fsim CLI.
# The fixture's first lines carry its options:
#
#   // fsim: top=<top> [std=<standard>] [lang=<language>]
#   -- fsim: top=<entity(architecture)> [std=2008]
#
# A fixture passes when every step succeeds, simulation prints a line that is
# exactly PASS (or a VHDL report whose text is PASS), and no error, failure,
# or fatal report is emitted.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE OR NOT EXISTS "${FSIM_FIXTURE}")
  message(FATAL_ERROR "FSIM_FIXTURE must name a compliance fixture")
endif()
if(NOT DEFINED FSIM_ENGINE)
  set(FSIM_ENGINE interpreter)
endif()

file(STRINGS "${FSIM_FIXTURE}" header LIMIT_COUNT 3)
set(top "")
set(standard "")
set(language "")
set(before "")
set(generics "")
foreach(line IN LISTS header)
  if(line MATCHES "fsim:(.*)$")
    string(STRIP "${CMAKE_MATCH_1}" options)
    string(REPLACE " " ";" options "${options}")
    foreach(option IN LISTS options)
      if(option MATCHES "^top=(.+)$")
        set(top "${CMAKE_MATCH_1}")
      elseif(option MATCHES "^std=(.+)$")
        set(standard "${CMAKE_MATCH_1}")
      elseif(option MATCHES "^lang=(.+)$")
        set(language "${CMAKE_MATCH_1}")
      elseif(option MATCHES "^generic=(.+)$")
        # A top-level generic override passed to elaborate as --generic.
        list(APPEND generics --generic "${CMAKE_MATCH_1}")
      elseif(option MATCHES "^before=(.+)$")
        # A source compiled into the same library first, for example to
        # exercise re-analysis of a unit it defines.
        set(before "${CMAKE_MATCH_1}")
      endif()
    endforeach()
  endif()
endforeach()
if(top STREQUAL "")
  message(FATAL_ERROR "${FSIM_FIXTURE} has no 'fsim: top=' header")
endif()

get_filename_component(fixture_name "${FSIM_FIXTURE}" NAME_WE)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-compliance-${fixture_name}-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")
get_filename_component(fixture_file "${FSIM_FIXTURE}" NAME)
file(COPY_FILE "${FSIM_FIXTURE}" "${workspace}/${fixture_file}")
if(NOT before STREQUAL "")
  get_filename_component(fixture_directory "${FSIM_FIXTURE}" DIRECTORY)
  string(REGEX REPLACE "\\.in$" "" before_file "${before}")
  file(COPY_FILE "${fixture_directory}/${before}" "${workspace}/${before_file}")
endif()

set(compile_args compile -q)
if(NOT language STREQUAL "")
  list(APPEND compile_args --lang "${language}")
endif()
if(NOT standard STREQUAL "")
  list(APPEND compile_args --standard "${standard}")
endif()
list(APPEND compile_args "${fixture_file}")

function(run_step step)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${workspace}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR
      "${fixture_name} ${step} failed (${result}):\n${output}\n"
      "workspace: ${workspace}")
  endif()
  set(step_output "${output}" PARENT_SCOPE)
endfunction()

if(NOT before STREQUAL "")
  set(before_args ${compile_args})
  list(REMOVE_AT before_args -1)
  list(APPEND before_args "${before_file}")
  run_step(compile-before ${before_args})
endif()
run_step(compile ${compile_args})
run_step(elaborate elaborate -q --no-aot --top "${top}" ${generics})
run_step(simulate simulate --engine "${FSIM_ENGINE}")
if(step_output MATCHES "(error|failure|fatal)\\[")
  message(FATAL_ERROR
    "${fixture_name} reported an error:\n${step_output}\nworkspace: ${workspace}")
endif()
# VHDL reports print as "<location>: note[FSIM-HDL-REPORT]: PASS".
if(NOT step_output MATCHES "(^|\n|: )PASS(\r?\n|$)")
  message(FATAL_ERROR
    "${fixture_name} did not print PASS:\n${step_output}\nworkspace: ${workspace}")
endif()

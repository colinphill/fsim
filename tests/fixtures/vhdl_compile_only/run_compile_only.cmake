# SPDX-License-Identifier: Apache-2.0
#
# Compile legal signal-formal actuals, then isolate the known runtime-lowering
# limitation with an explicit elaboration of the fixture's top architecture.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE OR NOT EXISTS "${FSIM_FIXTURE}")
  message(FATAL_ERROR "FSIM_FIXTURE must name a VHDL compile-only control")
endif()

file(STRINGS "${FSIM_FIXTURE}" fixture_header LIMIT_COUNT 1)
if(NOT fixture_header MATCHES
   "^-- fsim-compile-only: std=([0-9]+) top=([A-Za-z][A-Za-z0-9_]*)\\(([A-Za-z][A-Za-z0-9_]*)\\)$")
  message(FATAL_ERROR
    "${FSIM_FIXTURE} must declare '-- fsim-compile-only: std=<year> top=<entity>(<architecture>)'")
endif()
set(standard "${CMAKE_MATCH_1}")
set(top_entity "${CMAKE_MATCH_2}")
set(top_architecture "${CMAKE_MATCH_3}")
set(top "${top_entity}(${top_architecture})")

get_filename_component(fixture_name "${FSIM_FIXTURE}" NAME)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-vhdl-signal-actual-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")
file(COPY_FILE "${FSIM_FIXTURE}" "${workspace}/${fixture_name}")

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" compile -q
    --lang vhdl --standard "${standard}" "${fixture_name}"
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_stdout
  ERROR_VARIABLE compile_stderr)
set(compile_output "${compile_stdout}\n${compile_stderr}")
if(NOT compile_result STREQUAL "0")
  message(FATAL_ERROR
    "${fixture_name} compile failed (${compile_result}):\n${compile_output}\n"
    "workspace: ${workspace}")
endif()
if(compile_output MATCHES "FSIM-ELAB-VHCALL-001" OR
   compile_output MATCHES "FSIM-ELAB-VHCALL-003")
  message(FATAL_ERROR
    "${fixture_name} compile reported a signal-actual analysis error:\n"
    "${compile_output}\nworkspace: ${workspace}")
endif()

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" elaborate --no-aot --top "${top}"
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE elaborate_result
  OUTPUT_VARIABLE elaborate_stdout
  ERROR_VARIABLE elaborate_stderr)
set(elaborate_output "${elaborate_stdout}\n${elaborate_stderr}")
if(elaborate_result STREQUAL "0")
  message(FATAL_ERROR
    "${fixture_name} unexpectedly elaborated; this control must reach the "
    "known signal-formal lowering boundary:\n${elaborate_output}\n"
    "workspace: ${workspace}")
endif()
if(NOT elaborate_output MATCHES "FSIM-ELAB-VHFUNC-010")
  message(FATAL_ERROR
    "${fixture_name} elaboration failed without the expected VHFUNC-010:\n"
    "${elaborate_output}\nworkspace: ${workspace}")
endif()
if(elaborate_output MATCHES "FSIM-ELAB-VHCALL-001" OR
   elaborate_output MATCHES "FSIM-ELAB-VHCALL-003")
  message(FATAL_ERROR
    "${fixture_name} elaboration promoted a legal signal actual to an "
    "analysis error:\n${elaborate_output}\nworkspace: ${workspace}")
endif()

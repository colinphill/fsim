# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the visibility-control directory")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/fsim-type-mark-library-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")
foreach(fixture IN ITEMS library_marker.vhd imported_type.vhd
    explicit_library_mask_consumer.vhd)
  file(COPY_FILE "${FSIM_FIXTURE_DIR}/${fixture}" "${workspace}/${fixture}")
endforeach()

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" compile -q --lang vhdl --standard 1993
    --library masklib library_marker.vhd
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE marker_result
  OUTPUT_VARIABLE marker_output
  ERROR_VARIABLE marker_error)
if(NOT marker_result STREQUAL "0")
  message(FATAL_ERROR
    "masklib setup compile failed (${marker_result}):\n${marker_output}\n${marker_error}")
endif()

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" compile -q --lang vhdl --standard 1993
    --library work imported_type.vhd
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE package_result
  OUTPUT_VARIABLE package_output
  ERROR_VARIABLE package_error)
if(NOT package_result STREQUAL "0")
  message(FATAL_ERROR
    "work package setup compile failed (${package_result}):\n${package_output}\n${package_error}")
endif()

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" compile -q --lang vhdl --standard 1993
    --library work --search-library masklib explicit_library_mask_consumer.vhd
  WORKING_DIRECTORY "${workspace}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result STREQUAL "0")
  message(FATAL_ERROR
    "explicit library mask was accepted; workspace: ${workspace}")
endif()
set(all_output "${output}\n${error}")
if(NOT all_output MATCHES "error\\[FSIM-ELAB-VHTYPE-006\\]")
  message(FATAL_ERROR
    "explicit library mask failed without FSIM-ELAB-VHTYPE-006:\n${all_output}\n"
    "workspace: ${workspace}")
endif()
file(REMOVE_RECURSE "${workspace}")

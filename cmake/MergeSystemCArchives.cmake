# SPDX-License-Identifier: Apache-2.0

foreach(required_variable IN ITEMS
    FSIM_SYSTEMC_AR
    FSIM_SYSTEMC_LAUNCHER_ARCHIVE
    FSIM_SYSTEMC_RUNTIME_ARCHIVE)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

set(launcher_input "${FSIM_SYSTEMC_LAUNCHER_ARCHIVE}.launcher")
set(mri_script "${FSIM_SYSTEMC_LAUNCHER_ARCHIVE}.mri")
file(REMOVE "${launcher_input}" "${mri_script}")
file(RENAME "${FSIM_SYSTEMC_LAUNCHER_ARCHIVE}" "${launcher_input}")

file(TO_CMAKE_PATH "${FSIM_SYSTEMC_LAUNCHER_ARCHIVE}" launcher_archive)
file(TO_CMAKE_PATH "${launcher_input}" launcher_input_archive)
file(TO_CMAKE_PATH "${FSIM_SYSTEMC_RUNTIME_ARCHIVE}" runtime_archive)
file(WRITE "${mri_script}"
  "create \"${launcher_archive}\"\n"
  "addlib \"${launcher_input_archive}\"\n"
  "addlib \"${runtime_archive}\"\n"
  "save\n"
  "end\n")

execute_process(
  COMMAND "${FSIM_SYSTEMC_AR}" -M
  INPUT_FILE "${mri_script}"
  RESULT_VARIABLE archive_result
  OUTPUT_VARIABLE archive_output
  ERROR_VARIABLE archive_error)
if(NOT archive_result EQUAL 0)
  message(FATAL_ERROR
    "SystemC archive merge failed (${archive_result})\n"
    "${archive_output}${archive_error}")
endif()

if(DEFINED FSIM_SYSTEMC_RANLIB AND NOT "${FSIM_SYSTEMC_RANLIB}" STREQUAL "")
  execute_process(
    COMMAND "${FSIM_SYSTEMC_RANLIB}" "${FSIM_SYSTEMC_LAUNCHER_ARCHIVE}"
    RESULT_VARIABLE ranlib_result
    OUTPUT_VARIABLE ranlib_output
    ERROR_VARIABLE ranlib_error)
  if(NOT ranlib_result EQUAL 0)
    message(FATAL_ERROR
      "SystemC archive indexing failed (${ranlib_result})\n"
      "${ranlib_output}${ranlib_error}")
  endif()
endif()

file(REMOVE "${launcher_input}" "${mri_script}")

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

foreach(FSIM_REQUIRED IN ITEMS FSIM_SOURCE_DIR FSIM_FIXTURE_DIR
    FSIM_TCL_VERSION FSIM_TCL_SOURCE_SHA256)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

set(FSIM_PATCH_SCRIPT "${FSIM_SOURCE_DIR}/cmake/PatchTcl.cmake")
set(FSIM_PATCH_SET "third_party/tcl-${FSIM_TCL_VERSION}")

function(fsim_verify_patch_set FSIM_ROOT FSIM_OUTPUT_VARIABLE)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_ROOT}"
      "-DFSIM_TCL_VERSION=${FSIM_TCL_VERSION}"
      "-DFSIM_TCL_SOURCE_SHA256=${FSIM_TCL_SOURCE_SHA256}"
      -DMODE=verify
      -P "${FSIM_PATCH_SCRIPT}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  set(${FSIM_OUTPUT_VARIABLE} "${FSIM_RESULT}\n${FSIM_OUTPUT}${FSIM_ERROR}"
    PARENT_SCOPE)
endfunction()

fsim_verify_patch_set("${FSIM_SOURCE_DIR}" FSIM_GOVERNED)
if(NOT FSIM_GOVERNED MATCHES "^0\n")
  message(FATAL_ERROR "governed Tcl patch set failed verification:\n${FSIM_GOVERNED}")
endif()

# A changed manifest or patch must be rejected before any source is touched.
foreach(FSIM_CASE IN ITEMS manifest patch)
  set(FSIM_ROOT "${FSIM_FIXTURE_DIR}/${FSIM_CASE}")
  file(REMOVE_RECURSE "${FSIM_ROOT}")
  file(COPY "${FSIM_SOURCE_DIR}/${FSIM_PATCH_SET}/"
    DESTINATION "${FSIM_ROOT}/${FSIM_PATCH_SET}")
  if(FSIM_CASE STREQUAL "manifest")
    file(APPEND "${FSIM_ROOT}/${FSIM_PATCH_SET}/PATCHES.txt" "tampered=1\n")
    set(FSIM_EXPECTED "Tcl patch manifest SHA-256 mismatch")
    fsim_verify_patch_set("${FSIM_ROOT}" FSIM_RESULT)
  else()
    file(APPEND
      "${FSIM_ROOT}/${FSIM_PATCH_SET}/patches/tcl-windows-long-paths.patch"
      " \n")
    set(FSIM_EXPECTED "Tcl patch tcl-windows-long-paths SHA-256 mismatch")
    fsim_verify_patch_set("${FSIM_ROOT}" FSIM_RESULT)
  endif()
  string(FIND "${FSIM_RESULT}" "${FSIM_EXPECTED}" FSIM_OFFSET)
  if(FSIM_RESULT MATCHES "^0\n" OR FSIM_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "Tcl patch verification accepted a changed ${FSIM_CASE}:\n${FSIM_RESULT}")
  endif()
endforeach()

file(REMOVE_RECURSE "${FSIM_FIXTURE_DIR}")
message(STATUS "Tcl patch governance: governed set verified, changes rejected")

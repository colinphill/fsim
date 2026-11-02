# SPDX-License-Identifier: Apache-2.0

foreach(required IN ITEMS
    FSIM_EXECUTABLE
    FSIM_TCL_LIBRARY_DIR
    FSIM_TCL_LIBRARY_VERSION
    FSIM_STAGE_DIR
    FSIM_SYSTEMC_BRIDGE_LIBRARY
    FSIM_SYSTEMC_UPSTREAM_LIBRARY)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

if(NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "fsim executable does not exist: ${FSIM_EXECUTABLE}")
endif()
if(NOT EXISTS "${FSIM_TCL_LIBRARY_DIR}/init.tcl")
  message(
    FATAL_ERROR
    "fetched Tcl standard library is incomplete: ${FSIM_TCL_LIBRARY_DIR}"
  )
endif()

file(REMOVE_RECURSE "${FSIM_STAGE_DIR}")
file(MAKE_DIRECTORY "${FSIM_STAGE_DIR}/bin")
file(COPY "${FSIM_EXECUTABLE}" DESTINATION "${FSIM_STAGE_DIR}/bin")
file(COPY
  "${FSIM_SYSTEMC_BRIDGE_LIBRARY}"
  "${FSIM_SYSTEMC_UPSTREAM_LIBRARY}"
  DESTINATION "${FSIM_STAGE_DIR}/bin")
get_filename_component(executable_name "${FSIM_EXECUTABLE}" NAME)
set(staged_executable "${FSIM_STAGE_DIR}/bin/${executable_name}")

# The staged executable has no Tcl files beside it: the script library is
# built in and mounted with zipfs.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=FSIM_TCL_LIBRARY
    "${staged_executable}" tcl -c "puts [info library]; puts [info patchlevel]"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(
    FATAL_ERROR
    "relocated fetched Tcl failed with ${result}\nstdout:\n${output}\nstderr:\n${error}"
  )
endif()
if(NOT output MATCHES
    "^//zipfs:/fsim/tcl${FSIM_TCL_LIBRARY_VERSION}\n${FSIM_TCL_LIBRARY_VERSION}")
  message(
    FATAL_ERROR
    "relocated Tcl did not use the embedded library: ${output}"
  )
endif()

# FSIM_TCL_LIBRARY still selects a script library on disk.
set(override_library "${FSIM_STAGE_DIR}/override/tcl${FSIM_TCL_LIBRARY_VERSION}")
file(MAKE_DIRECTORY "${override_library}")
file(COPY "${FSIM_TCL_LIBRARY_DIR}/" DESTINATION "${override_library}")
file(
  APPEND
  "${override_library}/init.tcl"
  "\nset ::fsim_fetched_tcl_relocation_probe 1\n"
)
set(
  probe
  "if {![info exists ::fsim_fetched_tcl_relocation_probe]} {error {FSIM_TCL_LIBRARY init.tcl was not loaded}}; puts [info patchlevel]"
)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "FSIM_TCL_LIBRARY=${override_library}"
    "${staged_executable}" tcl -c "${probe}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(
    FATAL_ERROR
    "FSIM_TCL_LIBRARY override failed with ${result}\nstdout:\n${output}\nstderr:\n${error}"
  )
endif()
if(NOT output MATCHES "^${FSIM_TCL_LIBRARY_VERSION}")
  message(
    FATAL_ERROR
    "overridden Tcl reported an unexpected version: ${output}"
  )
endif()

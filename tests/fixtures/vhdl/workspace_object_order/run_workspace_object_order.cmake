# SPDX-License-Identifier: Apache-2.0
#
# One design compiled, elaborated and simulated in several workspace
# directories reports the same (workspace_object_order.vhd): its compiled
# objects are ordered, linked and given their IEEE packages by what they
# define, not by checksums that carry the workspace path. The interpreter,
# the compiled kernel and the compiled kernel with FSIM_KERNEL_VERIFY (every
# run replayed on the reference evaluator and compared) must each equal
# workspace_object_order.expected in every workspace. The workspace names are
# fixed. The earlier order hashed the objects' contents, whose declaration
# identities carry absolute paths, so it changed with the directory: two
# sibling workspaces named a and b ordered these four objects differently.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the workspace object order fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

file(READ "${FSIM_FIXTURE_DIR}/workspace_object_order.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)

function(run_fsim_step CASE_DIR STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${CASE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
      "${STEP_NAME} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${CASE_DIR}")
  endif()
  # The report texts, without source locations.
  string(REGEX MATCHALL "objorder [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

foreach(workspace IN ITEMS a b c d)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-object-order/${workspace}")
  file(REMOVE_RECURSE "${case_dir}")
  file(MAKE_DIRECTORY "${case_dir}")
  configure_file("${FSIM_FIXTURE_DIR}/workspace_object_order.vhd"
    "${case_dir}/workspace_object_order.vhd" COPYONLY)
  run_fsim_step("${case_dir}" compile
    compile -q --standard 2008 workspace_object_order.vhd)
  run_fsim_step("${case_dir}" elaborate elaborate -q work.top --no-aot)
  run_fsim_step("${case_dir}" interpreter simulate --engine interpreter)
  run_fsim_step("${case_dir}" kernel simulate --engine v4)
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  run_fsim_step("${case_dir}" verified simulate --engine v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  foreach(run IN ITEMS interpreter kernel verified)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "workspace_object_order (${run}, workspace ${workspace}): the reports "
        "differ from workspace_object_order.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endforeach()

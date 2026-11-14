# SPDX-License-Identifier: Apache-2.0
#
# Per-run test workspaces. A runner script makes each of its
# fsim-<name>-<12 hex digits> folders with fsim_test_workspace, which records
# it, and calls fsim_test_workspace_cleanup as its last step: a run that
# reaches it passed, and its folders are removed. A failing run stops at its
# FATAL_ERROR first and keeps them for triage.
#
# The removal is guarded: only a recorded folder directly in the run's
# binary directory, inside a build's tests tree, and named
# fsim-<name>-<12 hex digits> is removed.

function(fsim_test_workspace dir)
  file(MAKE_DIRECTORY "${dir}")
  set_property(GLOBAL APPEND PROPERTY FSIM_TEST_WORKSPACES "${dir}")
endfunction()

# fsim_test_case_workspace(<variable> <case directory>): a runner whose case
# is named by a build-tree directory (FSIM_WORK_DIR) works in a new per-run
# folder instead of that directory, so nothing an earlier run or an earlier
# fsim build left there takes part. The folder is
# fsim-<group>-<case>-<12 hex digits> in the run's binary directory, <group>
# and <case> being the last two parts of the case directory; <variable> is
# set to it.
function(fsim_test_case_workspace variable case_directory)
  get_filename_component(case "${case_directory}" NAME)
  get_filename_component(group_directory "${case_directory}" DIRECTORY)
  get_filename_component(group "${group_directory}" NAME)
  string(REGEX REPLACE "[^A-Za-z0-9_.+-]" "_" name "${group}-${case}")
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef id)
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-${name}-${id}")
  fsim_test_workspace("${dir}")
  set(${variable} "${dir}" PARENT_SCOPE)
endfunction()

function(fsim_test_workspace_cleanup)
  get_property(dirs GLOBAL PROPERTY FSIM_TEST_WORKSPACES)
  get_filename_component(binary "${CMAKE_CURRENT_BINARY_DIR}" ABSOLUTE)
  foreach(dir IN LISTS dirs)
    get_filename_component(path "${dir}" ABSOLUTE)
    get_filename_component(parent "${path}" DIRECTORY)
    get_filename_component(name "${path}" NAME)
    if(NOT parent STREQUAL binary
        OR NOT parent MATCHES "/tests(/[^/]+)*$"
        OR NOT name MATCHES "^fsim-[A-Za-z0-9_.+-]+-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]$"
        OR NOT IS_DIRECTORY "${path}"
        OR IS_SYMLINK "${path}")
      message(FATAL_ERROR
        "refusing to remove test workspace '${dir}': not a recorded "
        "fsim-<name>-<hex> folder in the tests binary directory '${binary}'")
    endif()
    file(REMOVE_RECURSE "${path}")
  endforeach()
  set_property(GLOBAL PROPERTY FSIM_TEST_WORKSPACES "")
endfunction()

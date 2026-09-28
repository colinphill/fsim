# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_TEST_EXECUTABLE OR NOT DEFINED FSIM_TEST_WORK_ROOT)
  message(FATAL_ERROR "debugger test executable and work root are required")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef suffix)
set(workspace "${FSIM_TEST_WORK_ROOT}/tcl-debugger-${suffix}")
file(MAKE_DIRECTORY "${workspace}")
execute_process(
  COMMAND "${FSIM_TEST_EXECUTABLE}" "${workspace}"
  RESULT_VARIABLE result)
# SystemC retains plug-in images through process teardown. Cleanup must run
# after the test exits so Windows has released its DLL mappings.
file(REMOVE_RECURSE "${workspace}")
if(EXISTS "${workspace}")
  message(FATAL_ERROR "failed to remove debugger test workspace: ${workspace}")
endif()
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Tcl debugger test failed: ${result}")
endif()

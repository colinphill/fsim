# SPDX-License-Identifier: Apache-2.0
if(NOT DEFINED FSIM_LLVM_ARGUMENTS_TEST)
  message(FATAL_ERROR "FSIM_LLVM_ARGUMENTS_TEST is required")
endif()

function(read_identity arguments output)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "FSIM_LLVM_ARGS=${arguments}"
      "${FSIM_LLVM_ARGUMENTS_TEST}" identity
    RESULT_VARIABLE status
    OUTPUT_VARIABLE identity
    ERROR_VARIABLE diagnostics
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "LLVM argument identity failed: ${diagnostics}")
  endif()
  if(NOT identity MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "Missing native identity: ${identity}")
  endif()
  set("${output}" "${identity}" PARENT_SCOPE)
endfunction()

read_identity("" default_identity)
read_identity("-time-passes=false" explicit_identity)
read_identity("-time-passes=false" repeated_identity)
if(default_identity STREQUAL explicit_identity)
  message(FATAL_ERROR "LLVM arguments did not enter native cache identity")
endif()
if(NOT explicit_identity STREQUAL repeated_identity)
  message(FATAL_ERROR "LLVM argument cache identity is nondeterministic")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/llvm-arguments-${workspace_id}")
function(check_cache mode arguments expected)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "FSIM_LLVM_ARGS=${arguments}"
      "${FSIM_LLVM_ARGUMENTS_TEST}" "${mode}" "${workspace}/${mode}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE statistics
    ERROR_VARIABLE diagnostics
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT status EQUAL 0 OR NOT statistics STREQUAL expected)
    message(FATAL_ERROR
      "${mode}: expected '${expected}', got '${statistics}', "
      "status ${status}: ${diagnostics}; retained ${workspace}")
  endif()
endfunction()
foreach(mode cache immutable-cache)
  check_cache("${mode}" "" "0 1")
  check_cache("${mode}" "" "1 0")
  check_cache("${mode}" "-time-passes=false" "0 1")
  check_cache("${mode}" "-time-passes=false" "1 0")
endforeach()
file(REMOVE_RECURSE "${workspace}")

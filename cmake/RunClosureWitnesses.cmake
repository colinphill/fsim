# SPDX-License-Identifier: Apache-2.0

# Preserve each closure's exact CTest arguments while sharing process execution.
# The caller owns witness selection, evidence logs, markers, and failure policy.
function(fsim_run_closure_witnesses
    FSIM_RESULT_NAME FSIM_OUTPUT_NAME FSIM_ERROR_NAME FSIM_TIMEOUT)
  execute_process(
    COMMAND "${FSIM_CTEST_COMMAND}" ${ARGN}
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_STDOUT
    ERROR_VARIABLE FSIM_STDERR
    TIMEOUT "${FSIM_TIMEOUT}")
  set("${FSIM_RESULT_NAME}" "${FSIM_STATUS}" PARENT_SCOPE)
  set("${FSIM_OUTPUT_NAME}" "${FSIM_STDOUT}" PARENT_SCOPE)
  set("${FSIM_ERROR_NAME}" "${FSIM_STDERR}" PARENT_SCOPE)
endfunction()

function(fsim_run_closure_matrix FSIM_CLOSURE_NAME FSIM_TIMEOUT)
  set(FSIM_COMBINED_OUTPUT "")
  set(FSIM_COMPLETED 0)
  foreach(FSIM_WITNESS IN LISTS ARGN)
    string(REPLACE "." "-" FSIM_LOG_NAME "${FSIM_WITNESS}")
    set(FSIM_LOG "${FSIM_OUTPUT_DIR}/${FSIM_LOG_NAME}.log")
    fsim_run_closure_witnesses(
      FSIM_RESULT FSIM_OUTPUT FSIM_ERROR "${FSIM_TIMEOUT}"
      --test-dir "${FSIM_BINARY_DIR}"
      -R "^${FSIM_WITNESS}$"
      -V
      --output-on-failure)
    file(WRITE "${FSIM_LOG}" "${FSIM_OUTPUT}${FSIM_ERROR}")
    file(APPEND "${FSIM_OUTPUT_DIR}/stage-results.tsv"
      "${FSIM_WITNESS}\t${FSIM_RESULT}\t${FSIM_LOG_NAME}.log\n")
    if(NOT FSIM_RESULT EQUAL 0)
      message(FATAL_ERROR
        "${FSIM_CLOSURE_NAME} witness failed: ${FSIM_WITNESS}\n"
        "retained log: ${FSIM_LOG}\n${FSIM_OUTPUT}${FSIM_ERROR}")
    endif()
    math(EXPR FSIM_COMPLETED "${FSIM_COMPLETED} + 1")
    string(APPEND FSIM_COMBINED_OUTPUT "${FSIM_OUTPUT}${FSIM_ERROR}\n")
  endforeach()
  set(FSIM_COMBINED_OUTPUT "${FSIM_COMBINED_OUTPUT}" PARENT_SCOPE)
  set(FSIM_COMPLETED "${FSIM_COMPLETED}" PARENT_SCOPE)
endfunction()

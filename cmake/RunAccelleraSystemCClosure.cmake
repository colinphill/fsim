# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_CTEST_COMMAND FSIM_OUTPUT_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "Accellera SystemC closure fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

file(REMOVE_RECURSE "${FSIM_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}/logs")
set(FSIM_CONSOLE "${FSIM_OUTPUT_DIR}/console.log")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.tsv")
file(WRITE "${FSIM_CONSOLE}" "FSIM-ACCELLERA-SYSTEMC-CLOSURE-START\n")
file(WRITE "${FSIM_RESULT}" "stage\tstatus\tlog\n")

set(FSIM_STAGES
  "corpus@@^fsim\\.systemc\\.(accellera_corpus|upstream\\.(simple_fifo|event_list|tlm_fifo))$"
  "backend@@^fsim\\.systemc\\.(shared_runtime|compatibility|kernel_backend_.*|tlm[12]_backend)$"
  "integration@@^fsim\\.(systemc\\.(shared-runtime-contract|abi-c|plugin|compiler|incremental|matrix)|application\\.(systemc_trace|systemc_matrix|systemc_datatypes|systemc_tlm1|typed_boundaries|artifact_phases))$"
  "contracts@@^fsim\\.(systemc-accellera-(inventory|portability-contract)|systemc-upstream-provenance|systemc-portability-contract|installed-public-contract|resource-portability-contract)$")

foreach(FSIM_STAGE_SPEC IN LISTS FSIM_STAGES)
  string(REPLACE "@@" ";" FSIM_STAGE_FIELDS "${FSIM_STAGE_SPEC}")
  list(GET FSIM_STAGE_FIELDS 0 FSIM_STAGE)
  list(GET FSIM_STAGE_FIELDS 1 FSIM_REGEX)
  set(FSIM_LOG "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log")
  execute_process(
    COMMAND "${FSIM_CTEST_COMMAND}"
      --test-dir "${FSIM_BINARY_DIR}"
      --output-on-failure
      --verbose
      --timeout 7200
      -j 8
      -R "${FSIM_REGEX}"
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_STDOUT
    ERROR_VARIABLE FSIM_STDERR
    TIMEOUT 7200)
  file(WRITE "${FSIM_LOG}" "${FSIM_STDOUT}${FSIM_STDERR}")
  file(APPEND "${FSIM_CONSOLE}" "${FSIM_STDOUT}${FSIM_STDERR}")
  string(FIND "${FSIM_STDOUT}${FSIM_STDERR}" "100% tests passed"
    FSIM_PASS_OFFSET)
  if(NOT FSIM_STATUS EQUAL 0 OR FSIM_PASS_OFFSET EQUAL -1)
    file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tFAIL\t${FSIM_LOG}\n")
    message(FATAL_ERROR
      "Accellera SystemC ${FSIM_STAGE} closure failed; retained log: ${FSIM_LOG}")
  endif()
  file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tPASS\t${FSIM_LOG}\n")
endforeach()

file(READ "${FSIM_OUTPUT_DIR}/logs/corpus.log" FSIM_CORPUS_TEXT)
foreach(FSIM_MARKER IN ITEMS
    "FSIM SystemC corpus PASS"
    "roots=2"
    "crossings=4096"
    "tlm1=2048"
    "tlm2=4096"
    "backpressure=2040")
  string(FIND "${FSIM_CORPUS_TEXT}" "${FSIM_MARKER}" FSIM_MARKER_OFFSET)
  if(FSIM_MARKER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "Accellera SystemC corpus lost marker '${FSIM_MARKER}'")
  endif()
endforeach()

foreach(FSIM_STAGE IN ITEMS corpus backend integration contracts)
  set(FSIM_LOG "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log")
  file(SIZE "${FSIM_LOG}" FSIM_LOG_SIZE)
  if(FSIM_LOG_SIZE EQUAL 0)
    message(FATAL_ERROR
      "Accellera SystemC closure retained an empty log: ${FSIM_LOG}")
  endif()
endforeach()

file(APPEND "${FSIM_CONSOLE}" "FSIM-ACCELLERA-SYSTEMC-CLOSURE-PASS\n")
message(STATUS
  "Accellera SystemC closure passed; retained evidence: ${FSIM_OUTPUT_DIR}")

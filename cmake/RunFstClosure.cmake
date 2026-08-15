# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR
   NOT DEFINED FSIM_BINARY_DIR OR
   NOT DEFINED FSIM_CTEST_COMMAND OR
   NOT DEFINED FSIM_OUTPUT_DIR)
  message(FATAL_ERROR
    "FST closure requires source, binary, ctest and output directories")
endif()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "FST closure fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}/logs")
set(FSIM_CONSOLE "${FSIM_OUTPUT_DIR}/console.log")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.tsv")
file(WRITE "${FSIM_CONSOLE}" "FSIM-FST-CLOSURE-START\n")
file(WRITE "${FSIM_RESULT}" "stage\tstatus\tlog\n")

function(fsim_run_fst_stage FSIM_STAGE FSIM_REGEX)
  set(FSIM_LOG "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log")
  execute_process(
    COMMAND "${FSIM_CTEST_COMMAND}"
      --test-dir "${FSIM_BINARY_DIR}"
      --output-on-failure
      --verbose
      --timeout 7200
      -j 4
      -R "${FSIM_REGEX}"
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_STDOUT
    ERROR_VARIABLE FSIM_STDERR
    TIMEOUT 7200
  )
  file(WRITE "${FSIM_LOG}" "${FSIM_STDOUT}${FSIM_STDERR}")
  file(APPEND "${FSIM_CONSOLE}" "${FSIM_STDOUT}${FSIM_STDERR}")
  string(FIND "${FSIM_STDOUT}${FSIM_STDERR}" "100% tests passed"
    FSIM_PASS_OFFSET)
  if(NOT FSIM_STATUS EQUAL 0 OR FSIM_PASS_OFFSET EQUAL -1)
    file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tFAIL\t${FSIM_LOG}\n")
    message(FATAL_ERROR
      "FST ${FSIM_STAGE} stage failed; retained log: ${FSIM_LOG}")
  endif()
  file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tPASS\t${FSIM_LOG}\n")
endfunction()

fsim_run_fst_stage(corpus
  "^fsim\\.application\\.fst_corpus$")
fsim_run_fst_stage(lifecycle
  "^fsim\\.application\\.(trace_control|trace_observation|vcd_control)$")
fsim_run_fst_stage(phases-artifacts
  "^fsim\\.(application\\.(core_non_project_cli|trace_archive)|artifact\\.design)$")
fsim_run_fst_stage(negatives-portability
  "^fsim\\.(runtime\\.fst_reader|fst-portability-contract)$")

set(FSIM_CORPUS_LOG "${FSIM_OUTPUT_DIR}/logs/corpus.log")
file(READ "${FSIM_CORPUS_LOG}" FSIM_CORPUS_TEXT)
foreach(FSIM_MARKER IN ITEMS
    "FSIM-FST-CORPUS-PASS"
    "formats=vcd,fst"
    "profiles=stored,deterministic"
    "values=bit,logic,real,shortreal,realtime,time,chandle,string,enum,physical,vhdl-time,logic9,typed-leaves"
    "roots=verilog,systemverilog,vhdl,systemc"
    "aliases=yes"
    "selection=selective,late"
    "callbacks=retained"
    "phases=project,compile,elaborate,simulate"
    "artifacts=object,design,library,native-cache,checkpoint"
    "relocation=yes"
    "negatives=yes"
    "time-advanced=5,6,7"
    "pass=yes"
    "clean-close=yes")
  string(FIND "${FSIM_CORPUS_TEXT}" "${FSIM_MARKER}" FSIM_MARKER_OFFSET)
  if(FSIM_MARKER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "FST corpus lost marker '${FSIM_MARKER}'; retained log: ${FSIM_CORPUS_LOG}")
  endif()
endforeach()

set(FSIM_MANIFEST
  "${FSIM_SOURCE_DIR}/tests/fixtures/fst/complete_trace.tsv")
set(FSIM_CORPUS_SOURCE
  "${FSIM_SOURCE_DIR}/tests/app/fst_corpus_test.cpp")
foreach(FSIM_INPUT IN ITEMS "${FSIM_MANIFEST}" "${FSIM_CORPUS_SOURCE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "FST closure lost clean-room input: ${FSIM_INPUT}")
  endif()
endforeach()
file(READ "${FSIM_MANIFEST}" FSIM_MANIFEST_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "# SPDX-License-Identifier: Apache-2.0"
    "FST-CORPUS-001"
    "FST-CORPUS-028"
    "mixed-roots"
    "selection"
    "callbacks"
    "artifacts"
    "negative PASS time advancement and clean close")
  string(FIND "${FSIM_MANIFEST_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "FST closure manifest lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_STAGE IN ITEMS
    corpus lifecycle phases-artifacts negatives-portability)
  set(FSIM_RETAINED_LOG "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log")
  file(SIZE "${FSIM_RETAINED_LOG}" FSIM_RETAINED_LOG_SIZE)
  if(FSIM_RETAINED_LOG_SIZE EQUAL 0)
    message(FATAL_ERROR "FST closure retained an empty log: ${FSIM_RETAINED_LOG}")
  endif()
endforeach()

file(APPEND "${FSIM_CONSOLE}"
  "FSIM-FST-CLOSURE-PASS formats=vcd,fst corpus-obligations=28 "
  "retained-logs=4 project-non-project=yes artifacts=yes relocation=yes "
  "negatives=yes time-advanced=5,6,7 pass=yes clean-close=yes\n")
message(STATUS "FST closure passed; retained log: ${FSIM_CORPUS_LOG}")

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR
   NOT DEFINED FSIM_BINARY_DIR OR
   NOT DEFINED FSIM_CTEST_COMMAND OR
   NOT DEFINED FSIM_OUTPUT_DIR)
  message(FATAL_ERROR
    "SDF VITAL closure requires source, binary, ctest and output directories")
endif()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "SDF VITAL closure fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/RunClosureWitnesses.cmake")

file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}/logs")
set(FSIM_LOG "${FSIM_OUTPUT_DIR}/logs/corpus.log")
set(FSIM_CONSOLE "${FSIM_OUTPUT_DIR}/console.log")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.tsv")
fsim_run_closure_witnesses(
  FSIM_STATUS FSIM_STDOUT FSIM_STDERR 1200
  --test-dir "${FSIM_BINARY_DIR}"
  --output-on-failure
  --verbose
  --timeout 1200
  -R "^fsim\\.application\\.sdf_vital_corpus$")
file(WRITE "${FSIM_LOG}" "${FSIM_STDOUT}${FSIM_STDERR}")
file(WRITE "${FSIM_CONSOLE}"
  "FSIM-SDF-VITAL-CLOSURE-START\n${FSIM_STDOUT}${FSIM_STDERR}")
file(WRITE "${FSIM_RESULT}" "stage\tstatus\tlog\n")
string(FIND "${FSIM_STDOUT}${FSIM_STDERR}" "100% tests passed"
  FSIM_PASS_OFFSET)
if(NOT FSIM_STATUS EQUAL 0 OR FSIM_PASS_OFFSET EQUAL -1)
  file(APPEND "${FSIM_RESULT}" "corpus\tFAIL\t${FSIM_LOG}\n")
  message(FATAL_ERROR
    "SDF VITAL corpus failed; retained log: ${FSIM_LOG}")
endif()
file(APPEND "${FSIM_RESULT}" "corpus\tPASS\t${FSIM_LOG}\n")

set(FSIM_NEGATIVE_LOG "${FSIM_OUTPUT_DIR}/logs/negatives.log")
fsim_run_closure_witnesses(
  FSIM_NEGATIVE_STATUS FSIM_NEGATIVE_STDOUT FSIM_NEGATIVE_STDERR 1200
  --test-dir "${FSIM_BINARY_DIR}"
  --output-on-failure
  --verbose
  --timeout 1200
  -R "^fsim\\.application\\.sdf_(vital_models|mixed_resolution|vital_archive|vital_phases)$")
file(WRITE "${FSIM_NEGATIVE_LOG}"
  "${FSIM_NEGATIVE_STDOUT}${FSIM_NEGATIVE_STDERR}")
file(APPEND "${FSIM_CONSOLE}"
  "${FSIM_NEGATIVE_STDOUT}${FSIM_NEGATIVE_STDERR}")
string(FIND "${FSIM_NEGATIVE_STDOUT}${FSIM_NEGATIVE_STDERR}"
  "100% tests passed" FSIM_NEGATIVE_PASS_OFFSET)
if(NOT FSIM_NEGATIVE_STATUS EQUAL 0 OR FSIM_NEGATIVE_PASS_OFFSET EQUAL -1)
  file(APPEND "${FSIM_RESULT}"
    "negatives\tFAIL\t${FSIM_NEGATIVE_LOG}\n")
  message(FATAL_ERROR
    "SDF VITAL negatives failed; retained log: ${FSIM_NEGATIVE_LOG}")
endif()
file(APPEND "${FSIM_RESULT}"
  "negatives\tPASS\t${FSIM_NEGATIVE_LOG}\n")

set(FSIM_PORTABILITY_LOG "${FSIM_OUTPUT_DIR}/logs/portability.log")
fsim_run_closure_witnesses(
  FSIM_PORTABILITY_STATUS FSIM_PORTABILITY_STDOUT FSIM_PORTABILITY_STDERR
  1200
  --test-dir "${FSIM_BINARY_DIR}"
  --output-on-failure
  --verbose
  --timeout 1200
  -R "^fsim\\.contract\\.artifact-resources$")
file(WRITE "${FSIM_PORTABILITY_LOG}"
  "${FSIM_PORTABILITY_STDOUT}${FSIM_PORTABILITY_STDERR}")
file(APPEND "${FSIM_CONSOLE}"
  "${FSIM_PORTABILITY_STDOUT}${FSIM_PORTABILITY_STDERR}")
string(FIND "${FSIM_PORTABILITY_STDOUT}${FSIM_PORTABILITY_STDERR}"
  "100% tests passed" FSIM_PORTABILITY_PASS_OFFSET)
if(NOT FSIM_PORTABILITY_STATUS EQUAL 0 OR
   FSIM_PORTABILITY_PASS_OFFSET EQUAL -1)
  file(APPEND "${FSIM_RESULT}"
    "portability\tFAIL\t${FSIM_PORTABILITY_LOG}\n")
  message(FATAL_ERROR
    "SDF VITAL portability failed; retained log: ${FSIM_PORTABILITY_LOG}")
endif()
file(APPEND "${FSIM_RESULT}"
  "portability\tPASS\t${FSIM_PORTABILITY_LOG}\n")

foreach(FSIM_MARKER IN ITEMS
    "FSIM-SDF-VITAL-CORPUS-PASS"
    "sdf-revisions=2.1,3.0,4.0"
    "vhdl-revisions=87,93,2000,2002,2008"
    "models=standard-cell,primitive,state-table,memory,wrapper"
    "directions=vhdl-verilog,verilog-vhdl,vhdl-systemverilog,systemverilog-vhdl,vhdl-systemc,systemc-vhdl"
    "engines=interpreter,llvm"
    "phases=cold,warm,relocated"
    "roots=multiple"
    "platform-contract=linux,windows"
    "artifacts=object,design,library,native-cache,checkpoint"
    "negatives=ambiguity,mismatch,unsupported-model,overflow,resource"
    "cases=2700"
    "time-advanced=2700"
    "pass=yes"
    "clean-exit=yes")
  string(FIND "${FSIM_STDOUT}${FSIM_STDERR}" "${FSIM_MARKER}"
    FSIM_MARKER_OFFSET)
  if(FSIM_MARKER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "SDF VITAL corpus lost marker '${FSIM_MARKER}'; retained log: ${FSIM_LOG}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/tests/app/sdf_vital_models_test.cpp"
  FSIM_MODEL_NEGATIVE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "ambiguous state-table memory shape"
    "wrapper port mismatch"
    "unsupported unowned model process"
    "model identity overflow"
    "zero model limits"
    "FSIM-SDF-VITAL-MODEL-002")
  string(FIND "${FSIM_MODEL_NEGATIVE_TEXT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "SDF VITAL closure lost negative evidence: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_FIXTURE_DIR "${FSIM_SOURCE_DIR}/tests/fixtures/sdf/vital")
foreach(FSIM_FIXTURE IN ITEMS
    timing_21.sdf timing_30.sdf timing_40.sdf
    standard_cell.vhd primitive.vhd state_table.vhd memory.vhd wrapper.vhd
    boundary_verilog.v boundary_systemverilog.sv boundary_systemc.cpp)
  if(NOT EXISTS "${FSIM_FIXTURE_DIR}/${FSIM_FIXTURE}")
    message(FATAL_ERROR "SDF VITAL closure lost owned fixture: ${FSIM_FIXTURE}")
  endif()
endforeach()

foreach(FSIM_RETAINED_LOG IN ITEMS
    "${FSIM_LOG}" "${FSIM_NEGATIVE_LOG}" "${FSIM_PORTABILITY_LOG}")
  file(SIZE "${FSIM_RETAINED_LOG}" FSIM_RETAINED_LOG_SIZE)
  if(FSIM_RETAINED_LOG_SIZE EQUAL 0)
    message(FATAL_ERROR
      "SDF VITAL closure retained an empty log: ${FSIM_RETAINED_LOG}")
  endif()
endforeach()

file(APPEND "${FSIM_CONSOLE}"
  "FSIM-SDF-VITAL-CLOSURE-PASS cases=2700 negatives=5 application-evidence=yes retained-logs=3 time-advanced=2700 pass=yes clean-exit=yes\n")
message(STATUS
  "SDF VITAL closure passed; retained log: ${FSIM_LOG}")

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR
   NOT DEFINED FSIM_BINARY_DIR OR
   NOT DEFINED FSIM_CTEST_COMMAND OR
   NOT DEFINED FSIM_OUTPUT_DIR)
  message(FATAL_ERROR
    "SDF application closure requires source, binary, ctest and output directories")
endif()

file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}/logs")
set(FSIM_CONSOLE "${FSIM_OUTPUT_DIR}/console.log")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.tsv")
file(WRITE "${FSIM_CONSOLE}"
  "FSIM-SDF-APPLICATION-CLOSURE-START revisions=2.1,3.0,4.0\n")
file(WRITE "${FSIM_RESULT}" "stage\tstatus\tlog\n")

set(FSIM_STAGES
  "corpus@@^fsim\\.application\\.sdf_application_corpus$"
  "timing@@^fsim\\.application\\.sdf_(path_timing|interconnect_timing|mixed_verilog|mixed_systemverilog|mixed_systemc|mixed_resolution|foreign_interfaces|vital_observability|vital_archive|vital_phases|delay_modes|primary_timing_checks|secondary_timing_checks|condition_timing|pulse_timing|precedence|scheduling|drive_timing|reannotation)$"
  "publication@@^fsim\\.application\\.sdf_(control|effective_archive|observability)$"
  "artifact@@^fsim\\.(library\\.artifact|artifact\\.(object|design)|cache)$"
  "public@@^fsim\\.(application\\.(specify|resolution|vpi|vcd_control)|api|api\\.c_header)$"
  "project@@^fsim\\.application$"
  "contracts@@^fsim\\.(diagnostics-catalog|source-line-budget|sdf-application-inventory|resource-portability-contract)$")

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
      --timeout 1200
      -R "${FSIM_REGEX}"
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_STDOUT
    ERROR_VARIABLE FSIM_STDERR
    TIMEOUT 1200
  )
  file(WRITE "${FSIM_LOG}" "${FSIM_STDOUT}${FSIM_STDERR}")
  file(APPEND "${FSIM_CONSOLE}" "${FSIM_STDOUT}${FSIM_STDERR}")
  string(FIND "${FSIM_STDOUT}${FSIM_STDERR}" "100% tests passed"
    FSIM_PASS_OFFSET)
  if(NOT FSIM_STATUS EQUAL 0 OR FSIM_PASS_OFFSET EQUAL -1)
    file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tFAIL\t${FSIM_LOG}\n")
    message(FATAL_ERROR
      "SDF application closure stage ${FSIM_STAGE} failed; retained log: ${FSIM_LOG}")
  endif()
  file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tPASS\t${FSIM_LOG}\n")
endforeach()

file(READ "${FSIM_OUTPUT_DIR}/logs/corpus.log" FSIM_CORPUS_LOG)
foreach(FSIM_MARKER IN ITEMS
    "FSIM-SDF-APPLICATION-CORPUS-PASS"
    "cells=standard-cell,primitive,interconnect,pulse,timing-check"
    "timing=advanced"
    "violation=setuphold,pulse"
    "engines=interpreter,llvm"
    "modes=optimized,debug"
    "phases=project,non-project"
    "artifacts=object,design,library"
    "cache=cold,warm,relocated"
    "checkpoint=replay"
    "surfaces=debugger,callback,trace,vpi,vcd"
    "platform-contract=linux,windows"
    "negatives=missing,mismatch,conflict,overflow,resource"
    "time-advanced=12"
    "clean-exit=yes")
  string(FIND "${FSIM_CORPUS_LOG}" "${FSIM_MARKER}" FSIM_MARKER_OFFSET)
  if(FSIM_MARKER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "SDF application closure lost transcript marker: ${FSIM_MARKER}")
  endif()
endforeach()

set(FSIM_NEGATIVE_TEXT "")
foreach(FSIM_INPUT IN ITEMS
    sdf_value_policy_test.cpp
    sdf_target_plan_test.cpp
    sdf_path_timing_test.cpp
    sdf_interconnect_timing_test.cpp
    sdf_mixed_verilog_test.cpp
    sdf_mixed_systemverilog_test.cpp
    sdf_mixed_systemc_test.cpp
    sdf_mixed_resolution_test.cpp
    sdf_foreign_interfaces_test.cpp
    sdf_vital_observability_test.cpp
    sdf_vital_archive_test.cpp
    sdf_vital_phases_test.cpp
    sdf_delay_modes_test.cpp
    sdf_primary_timing_checks_test.cpp
    sdf_secondary_timing_checks_test.cpp
    sdf_condition_timing_test.cpp
    sdf_pulse_timing_test.cpp
    sdf_precedence_test.cpp
    sdf_scheduling_test.cpp
    sdf_drive_timing_test.cpp
    sdf_reannotation_test.cpp
    sdf_control_test.cpp
    sdf_effective_archive_test.cpp
    sdf_observability_test.cpp)
  file(READ "${FSIM_SOURCE_DIR}/tests/app/${FSIM_INPUT}" FSIM_INPUT_TEXT)
  string(APPEND FSIM_NEGATIVE_TEXT "${FSIM_INPUT_TEXT}")
endforeach()
foreach(FSIM_FAMILY IN ITEMS
    FSIM-SDF-VALUE-
    FSIM-SDF-PLAN-
    FSIM-SDF-PATH-
    FSIM-SDF-INTERCONNECT-
    FSIM-SDF-MIXED-VERILOG-
    FSIM-SDF-MIXED-SYSTEMVERILOG-
    FSIM-SDF-MIXED-SYSTEMC-
    FSIM-SDF-MIXED-RESOLUTION-
    FSIM-SDF-FOREIGN-
    FSIM-SDF-VITAL-OBSERVE-
    FSIM-SDF-VITAL-ARCHIVE-
    FSIM-SDF-VITAL-PHASE-
    FSIM-SDF-DELAY-MODE-
    FSIM-SDF-PRIMARY-CHECK-
    FSIM-SDF-SECONDARY-CHECK-
    FSIM-SDF-CONDITION-CHECK-
    FSIM-SDF-PULSE-
    FSIM-SDF-PRECEDENCE-
    FSIM-SDF-SCHEDULING-
    FSIM-SDF-DRIVE-
    FSIM-SDF-REANNOTATION-
    FSIM-SDF-CONTROL-
    FSIM-SDF-EFFECTIVE-
    FSIM-SDF-OBSERVE-)
  string(FIND "${FSIM_NEGATIVE_TEXT}" "${FSIM_FAMILY}" FSIM_FAMILY_OFFSET)
  if(FSIM_FAMILY_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "SDF application closure lost negative family ${FSIM_FAMILY}")
  endif()
endforeach()

file(APPEND "${FSIM_CONSOLE}"
  "FSIM-SDF-APPLICATION-CLOSURE-PASS stages=7 retained-logs=7 negatives=23 resources=governed\n")
message(STATUS
  "FSIM-SDF-APPLICATION-CLOSURE-PASS retained=${FSIM_OUTPUT_DIR}")

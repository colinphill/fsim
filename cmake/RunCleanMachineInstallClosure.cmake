# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_CTEST_COMMAND FSIM_OUTPUT_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

set(FSIM_MAINTAINED_EXAMPLES
    non_project_phases
    precompiled_library
    sdf_annotation
    sdf_vital_mixed
    three_language_hierarchy
    v3_coverage
    vertical_slice)
file(GLOB FSIM_EXAMPLE_ENTRIES RELATIVE "${FSIM_SOURCE_DIR}/examples"
     "${FSIM_SOURCE_DIR}/examples/*")
set(FSIM_ACTUAL_EXAMPLES)
foreach(FSIM_EXAMPLE IN LISTS FSIM_EXAMPLE_ENTRIES)
  if(IS_DIRECTORY "${FSIM_SOURCE_DIR}/examples/${FSIM_EXAMPLE}")
    list(APPEND FSIM_ACTUAL_EXAMPLES "${FSIM_EXAMPLE}")
  endif()
endforeach()
list(SORT FSIM_ACTUAL_EXAMPLES)
if(NOT FSIM_ACTUAL_EXAMPLES STREQUAL FSIM_MAINTAINED_EXAMPLES)
  message(FATAL_ERROR
    "maintained example inventory drifted: ${FSIM_ACTUAL_EXAMPLES}")
endif()
foreach(FSIM_EXAMPLE IN LISTS FSIM_MAINTAINED_EXAMPLES)
  if(NOT EXISTS "${FSIM_SOURCE_DIR}/examples/${FSIM_EXAMPLE}/README.md")
    message(FATAL_ERROR "maintained example lacks README: ${FSIM_EXAMPLE}")
  endif()
endforeach()

set(FSIM_ROOT "${FSIM_SOURCE_DIR}/CMakeLists.txt")
file(READ "${FSIM_ROOT}" FSIM_ROOT_CONTENTS)
foreach(FSIM_INSTALL_POLICY IN ITEMS
    "DIRECTORY examples/"
    "DESTINATION \"\${CMAKE_INSTALL_DATADIR}/fsim/examples\""
    "PATTERN \".fsim-cache\" EXCLUDE"
    "PATTERN \"*.fst\" EXCLUDE"
    "PATTERN \"*.vcd\" EXCLUDE")
  string(FIND "${FSIM_ROOT_CONTENTS}" "${FSIM_INSTALL_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "installed example ownership drifted: ${FSIM_INSTALL_POLICY}")
  endif()
endforeach()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "clean-machine install closure fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

file(REMOVE_RECURSE "${FSIM_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}/logs")
set(FSIM_CONSOLE "${FSIM_OUTPUT_DIR}/console.log")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.tsv")
set(FSIM_INVENTORY "${FSIM_OUTPUT_DIR}/example-inventory.tsv")
file(WRITE "${FSIM_CONSOLE}" "FSIM-CLEAN-MACHINE-INSTALL-START\n")
file(WRITE "${FSIM_RESULT}" "stage\tstatus\twitnesses\tlog\n")
file(WRITE "${FSIM_INVENTORY}" "example\towner\n")
file(APPEND "${FSIM_INVENTORY}"
  "non_project_phases\tfsim.non-project-restartability-contract\n"
  "precompiled_library\tfsim.library.artifact\n"
  "sdf_annotation\tfsim.sdf-application-inventory,fsim.application.sdf_control\n"
  "sdf_vital_mixed\tfsim.sdf-vital-inventory,fsim.application.sdf_vital_corpus\n"
  "three_language_hierarchy\tfsim.application.systemc_matrix,fsim.application.trace_control,fsim.application.fst_corpus\n"
  "v3_coverage\tfsim.v3-release-documentation\n"
  "vertical_slice\tfsim.application\n")

set(FSIM_STAGES
  "install@@3@@^fsim\\.(binary-install-ownership|installed-public-contract|installed-pkg-config-consumer)$"
  "examples@@9@@^fsim\\.(application|non-project-restartability-contract|library\\.artifact|sdf-application-inventory|sdf-vital-inventory|v3-release-documentation|application\\.(sdf_control|sdf_vital_corpus|systemc_matrix))$"
  "artifacts@@4@@^fsim\\.(artifact\\.(object|design)|application\\.artifact_phases|source-hidden-relocation-contract)$"
  "observation@@4@@^fsim\\.(application\\.(trace_control|fst_corpus|vcd_control)|tool-portability-contract)$")

foreach(FSIM_STAGE_SPEC IN LISTS FSIM_STAGES)
  string(REPLACE "@@" ";" FSIM_STAGE_FIELDS "${FSIM_STAGE_SPEC}")
  list(GET FSIM_STAGE_FIELDS 0 FSIM_STAGE)
  list(GET FSIM_STAGE_FIELDS 1 FSIM_EXPECTED_COUNT)
  list(GET FSIM_STAGE_FIELDS 2 FSIM_REGEX)
  set(FSIM_LOG "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log")
  execute_process(
    COMMAND "${FSIM_CTEST_COMMAND}"
      --test-dir "${FSIM_BINARY_DIR}"
      --output-on-failure
      --timeout 7200
      -j 8
      -R "${FSIM_REGEX}"
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_STDOUT
    ERROR_VARIABLE FSIM_STDERR
    TIMEOUT 7200)
  file(WRITE "${FSIM_LOG}" "${FSIM_STDOUT}${FSIM_STDERR}")
  file(APPEND "${FSIM_CONSOLE}" "${FSIM_STDOUT}${FSIM_STDERR}")
  string(FIND "${FSIM_STDOUT}${FSIM_STDERR}"
    "100% tests passed, 0 tests failed out of ${FSIM_EXPECTED_COUNT}"
    FSIM_PASS_OFFSET)
  if(NOT FSIM_STATUS EQUAL 0 OR FSIM_PASS_OFFSET EQUAL -1)
    file(APPEND "${FSIM_RESULT}"
      "${FSIM_STAGE}\tFAIL\t${FSIM_EXPECTED_COUNT}\t${FSIM_LOG}\n")
    message(FATAL_ERROR
      "clean-machine ${FSIM_STAGE} closure failed; retained log: ${FSIM_LOG}")
  endif()
  file(APPEND "${FSIM_RESULT}"
    "${FSIM_STAGE}\tPASS\t${FSIM_EXPECTED_COUNT}\t${FSIM_LOG}\n")
endforeach()

foreach(FSIM_STAGE IN ITEMS install examples artifacts observation)
  file(SIZE "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log" FSIM_LOG_SIZE)
  if(FSIM_LOG_SIZE EQUAL 0)
    message(FATAL_ERROR "clean-machine closure retained an empty ${FSIM_STAGE} log")
  endif()
endforeach()
file(APPEND "${FSIM_CONSOLE}" "FSIM-CLEAN-MACHINE-INSTALL-PASS\n")
message(STATUS
  "clean-machine install closure passed 19 witnesses for 6 maintained examples; retained evidence: ${FSIM_OUTPUT_DIR}")

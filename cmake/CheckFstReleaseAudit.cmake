# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v2-fst-release-audit.md")
set(FSIM_TRACING_DOC "${FSIM_SOURCE_DIR}/docs/tracing.md")
set(FSIM_API_DOC "${FSIM_SOURCE_DIR}/docs/api.md")
set(FSIM_LANGUAGE_DOC "${FSIM_SOURCE_DIR}/docs/language-support.md")
set(FSIM_ARCHITECTURE_DOC "${FSIM_SOURCE_DIR}/docs/architecture.md")
set(FSIM_FEATURE_DOC "${FSIM_SOURCE_DIR}/docs/feature-matrix.md")
set(FSIM_RESUME_DOC "${FSIM_SOURCE_DIR}/docs/v2-resume.md")
set(FSIM_README "${FSIM_SOURCE_DIR}/README.md")
set(FSIM_EXAMPLE_DIR
  "${FSIM_SOURCE_DIR}/examples/three_language_hierarchy")
set(FSIM_RESOURCE_CONTRACT
  "${FSIM_SOURCE_DIR}/cmake/CheckResourcePortabilityContract.cmake")
set(FSIM_LICENSE "${FSIM_SOURCE_DIR}/LICENSE")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_AUDIT}" "${FSIM_TRACING_DOC}" "${FSIM_API_DOC}"
    "${FSIM_LANGUAGE_DOC}" "${FSIM_ARCHITECTURE_DOC}"
    "${FSIM_FEATURE_DOC}" "${FSIM_RESUME_DOC}" "${FSIM_README}"
    "${FSIM_EXAMPLE_DIR}/README.md" "${FSIM_EXAMPLE_DIR}/fsim.toml"
    "${FSIM_EXAMPLE_DIR}/fsim-fst.toml" "${FSIM_RESOURCE_CONTRACT}"
    "${FSIM_LICENSE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "FST release-audit input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_COMPOSED_OUTPUT)
foreach(FSIM_GATE IN ITEMS
    CheckFstInventory.cmake
    CheckDiagnosticCatalog.cmake
    CheckSourceLineBudget.cmake
    CheckFstPortabilityContract.cmake
    CheckApplicationTestDeduplication.cmake)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}"
    RESULT_VARIABLE FSIM_GATE_RESULT
    OUTPUT_VARIABLE FSIM_GATE_OUTPUT
    ERROR_VARIABLE FSIM_GATE_ERROR)
  if(NOT FSIM_GATE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "composed FST gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
  string(APPEND FSIM_COMPOSED_OUTPUT
    "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}\n")
endforeach()

foreach(FSIM_EXACT_OUTPUT IN ITEMS
    "FST inventory passed: rows=17 preserved=17 active=0 digest=fa40e80a69015276a850de6f4f84355d93ec541c774a76003f2b1718d1eac248"
    "diagnostic catalog covers 2548 production codes"
    "Checked 1149 authored sources against the 2500-line hard limit with a 2000-line refactor target"
    "FST portability contract: bounded fixed-width decoding, binary filesystem I/O, transactional diagnostics, corruption/resource negatives, semantic differentials, and Linux/Windows dependency independence are present"
    "regression de-duplication: unique commands, application partitions and thirteen fixture-backed closure drivers are present")
  string(FIND "${FSIM_COMPOSED_OUTPUT}" "${FSIM_EXACT_OUTPUT}"
    FSIM_OUTPUT_INDEX)
  if(FSIM_OUTPUT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "FST release inventory changed or lost exact evidence: ${FSIM_EXACT_OUTPUT}")
  endif()
endforeach()

file(GLOB_RECURSE FSIM_AUTHORED_FILES LIST_DIRECTORIES FALSE
  "${FSIM_SOURCE_DIR}/.github/*"
  "${FSIM_SOURCE_DIR}/cmake/*"
  "${FSIM_SOURCE_DIR}/docs/*"
  "${FSIM_SOURCE_DIR}/examples/*"
  "${FSIM_SOURCE_DIR}/include/*"
  "${FSIM_SOURCE_DIR}/scripts/*"
  "${FSIM_SOURCE_DIR}/src/*"
  "${FSIM_SOURCE_DIR}/tests/*")
list(APPEND FSIM_AUTHORED_FILES
  "${FSIM_SOURCE_DIR}/.clang-format"
  "${FSIM_SOURCE_DIR}/.gitignore"
  "${FSIM_SOURCE_DIR}/CMakeLists.txt"
  "${FSIM_SOURCE_DIR}/CMakePresets.json"
  "${FSIM_SOURCE_DIR}/README.md")
list(FILTER FSIM_AUTHORED_FILES EXCLUDE REGEX "/tests/fuzz/corpus/")
list(FILTER FSIM_AUTHORED_FILES EXCLUDE REGEX "/\\.fsim-cache/")
list(FILTER FSIM_AUTHORED_FILES EXCLUDE REGEX
  "/examples/three_language_hierarchy/three_language\\.vcd$")
list(REMOVE_DUPLICATES FSIM_AUTHORED_FILES)
list(LENGTH FSIM_AUTHORED_FILES FSIM_AUTHORED_COUNT)
set(FSIM_EXPECTED_AUTHORED_COUNT 1434)
if(NOT FSIM_AUTHORED_COUNT EQUAL FSIM_EXPECTED_AUTHORED_COUNT)
  message(FATAL_ERROR
    "authored FST inventory changed: expected ${FSIM_EXPECTED_AUTHORED_COUNT} files, found ${FSIM_AUTHORED_COUNT}")
endif()
foreach(FSIM_FILE IN LISTS FSIM_AUTHORED_FILES)
  file(READ "${FSIM_FILE}" FSIM_PREFIX LIMIT 4096)
  string(FIND "${FSIM_PREFIX}" "SPDX-License-Identifier: Apache-2.0"
    FSIM_SPDX_INDEX)
  if(FSIM_SPDX_INDEX EQUAL -1)
    file(RELATIVE_PATH FSIM_RELATIVE "${FSIM_SOURCE_DIR}" "${FSIM_FILE}")
    message(FATAL_ERROR
      "authored FST artifact lacks Apache-2.0 SPDX notice: ${FSIM_RELATIVE}")
  endif()
endforeach()

file(GLOB_RECURSE FSIM_TEST_CONTROL_FILES LIST_DIRECTORIES FALSE
  "${FSIM_SOURCE_DIR}/cmake/*"
  "${FSIM_SOURCE_DIR}/tests/*")
list(FILTER FSIM_TEST_CONTROL_FILES EXCLUDE REGEX "/tests/fuzz/corpus/")
list(REMOVE_DUPLICATES FSIM_TEST_CONTROL_FILES)
list(LENGTH FSIM_TEST_CONTROL_FILES FSIM_TEST_CONTROL_COUNT)
set(FSIM_EXPECTED_TEST_CONTROL_COUNT 634)
if(NOT FSIM_TEST_CONTROL_COUNT EQUAL FSIM_EXPECTED_TEST_CONTROL_COUNT)
  message(FATAL_ERROR
    "FST test/control inventory changed: expected ${FSIM_EXPECTED_TEST_CONTROL_COUNT} files, found ${FSIM_TEST_CONTROL_COUNT}")
endif()

file(READ "${FSIM_LICENSE}" FSIM_LICENSE_CONTENTS LIMIT 256)
if(NOT FSIM_LICENSE_CONTENTS MATCHES "Apache License")
  message(FATAL_ERROR "repository LICENSE is not the reviewed Apache license")
endif()

file(READ "${FSIM_AUDIT}" FSIM_AUDIT_TEXT)
foreach(FSIM_REVIEW_ID IN ITEMS
    B171-C19-LEDGER B171-C19-FORMATS B171-C19-VALUES
    B171-C19-BOUNDARIES B171-C19-CONTROLS B171-C19-ARTIFACTS
    B171-C19-NEGATIVE B171-C19-DEFERRED)
  string(FIND "${FSIM_AUDIT_TEXT}" "`${FSIM_REVIEW_ID}`" FSIM_ID_INDEX)
  if(FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "FST audit omits review ${FSIM_REVIEW_ID}")
  endif()
endforeach()

set(FSIM_PUBLIC_TEXT)
foreach(FSIM_PUBLIC_DOCUMENT IN ITEMS
    "${FSIM_README}" "${FSIM_TRACING_DOC}" "${FSIM_API_DOC}"
    "${FSIM_LANGUAGE_DOC}" "${FSIM_ARCHITECTURE_DOC}"
    "${FSIM_FEATURE_DOC}" "${FSIM_RESUME_DOC}" "${FSIM_AUDIT}"
    "${FSIM_EXAMPLE_DIR}/README.md")
  file(READ "${FSIM_PUBLIC_DOCUMENT}" FSIM_PUBLIC_DOCUMENT_TEXT)
  string(APPEND FSIM_PUBLIC_TEXT "${FSIM_PUBLIC_DOCUMENT_TEXT}\n")
endforeach()
foreach(FSIM_DOC_TOKEN IN ITEMS
    "VCD and FST tracing"
    "format-neutral trace model"
    "Verilog, SystemVerilog, VHDL, and SystemC"
    "project, CLI, Tcl, debugger, native C, C++"
    "Release build testing is not required except during the final batch checks"
    "Header formatting changes that would induce long rebuilds are avoided"
    "Batch 172 planned restart checkpoint")
  string(FIND "${FSIM_PUBLIC_TEXT}" "${FSIM_DOC_TOKEN}"
    FSIM_DOC_TOKEN_INDEX)
  if(FSIM_DOC_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "FST public documentation lost token: ${FSIM_DOC_TOKEN}")
  endif()
endforeach()

message(STATUS
  "FST release audit: 17 rows, 2548 diagnostics, 1149 bounded sources, ${FSIM_AUTHORED_COUNT} SPDX-owned files, ${FSIM_TEST_CONTROL_COUNT} test/control files")

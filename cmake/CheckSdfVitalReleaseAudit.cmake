# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v2-sdf-vital-release-audit.md")
set(FSIM_SDF_DOC "${FSIM_SOURCE_DIR}/docs/sdf.md")
set(FSIM_LANGUAGE_DOC "${FSIM_SOURCE_DIR}/docs/language-support.md")
set(FSIM_ARCHITECTURE_DOC "${FSIM_SOURCE_DIR}/docs/architecture.md")
set(FSIM_FEATURE_DOC "${FSIM_SOURCE_DIR}/docs/feature-matrix.md")
set(FSIM_RESUME_DOC "${FSIM_SOURCE_DIR}/docs/v2-resume.md")
set(FSIM_README "${FSIM_SOURCE_DIR}/README.md")
set(FSIM_EXAMPLE_DIR "${FSIM_SOURCE_DIR}/examples/sdf_vital_mixed")
set(FSIM_LICENSE "${FSIM_SOURCE_DIR}/LICENSE")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_AUDIT}" "${FSIM_SDF_DOC}" "${FSIM_LANGUAGE_DOC}"
    "${FSIM_ARCHITECTURE_DOC}" "${FSIM_FEATURE_DOC}" "${FSIM_RESUME_DOC}"
    "${FSIM_README}"
    "${FSIM_EXAMPLE_DIR}/README.md" "${FSIM_EXAMPLE_DIR}/vital_cell.vhd"
    "${FSIM_EXAMPLE_DIR}/boundary.sv" "${FSIM_EXAMPLE_DIR}/mixed.sdf"
    "${FSIM_EXAMPLE_DIR}/annotate.tcl" "${FSIM_LICENSE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "SDF VITAL release-audit input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_COMPOSED_OUTPUT)
foreach(FSIM_GATE IN ITEMS
    CheckSdfVitalInventory.cmake
    CheckDiagnosticCatalog.cmake
    CheckSourceLineBudget.cmake
    CheckResourcePortabilityContract.cmake)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}"
    RESULT_VARIABLE FSIM_GATE_RESULT
    OUTPUT_VARIABLE FSIM_GATE_OUTPUT
    ERROR_VARIABLE FSIM_GATE_ERROR)
  if(NOT FSIM_GATE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "composed SDF VITAL gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
  string(APPEND FSIM_COMPOSED_OUTPUT
    "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}\n")
endforeach()

foreach(FSIM_EXACT_OUTPUT IN ITEMS
    "SDF VITAL inventory passed: rows=17 preserved=17 active=0 digest=3e84f643e6df24090efb1161e0d6836847784268a139e3da3c2fda4568913c6f"
    "diagnostic catalog covers 2746 production codes"
    "Checked 1510 authored sources against the 2000-line hard limit"
    "resource portability contract: five four-worker build/test steps, 120-minute hosted jobs, eight-link pool, compact Debug objects, 128 MiB Windows stacks, bounded large-test, code-coverage model/source/point/statement/branch discovery, opt-in and standard SystemVerilog control/query/merge/save, v3 artifact identity, bounded .fsimcov schema, and mixed-language engine/aggregation equivalence line-state derivation and instance inventory attachment, FST value/change/hierarchy storage, pinned Boost headers, broad coverage-metric generate/mixed-engine equivalence, complete legacy ACC routine/object inventory, public C/C++ header ABI, bounded transactional ACC lifecycle, generation-qualified ACC/VPI handles, bounded hierarchy lookup, independently worded VHDL-2019 clause ownership and profile/artifact/cache identity, and scoped/SystemC phase traces are present")
  string(FIND "${FSIM_COMPOSED_OUTPUT}" "${FSIM_EXACT_OUTPUT}"
    FSIM_OUTPUT_INDEX)
  if(FSIM_OUTPUT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SDF VITAL release inventory changed or lost exact evidence: ${FSIM_EXACT_OUTPUT}")
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
if(NOT FSIM_AUTHORED_COUNT EQUAL 1813)
  message(FATAL_ERROR
    "authored SDF VITAL inventory changed: expected 1813 files, found ${FSIM_AUTHORED_COUNT}")
endif()
foreach(FSIM_FILE IN LISTS FSIM_AUTHORED_FILES)
  file(READ "${FSIM_FILE}" FSIM_PREFIX LIMIT 4096)
  string(FIND "${FSIM_PREFIX}" "SPDX-License-Identifier: Apache-2.0"
    FSIM_SPDX_INDEX)
  if(FSIM_SPDX_INDEX EQUAL -1)
    file(RELATIVE_PATH FSIM_RELATIVE "${FSIM_SOURCE_DIR}" "${FSIM_FILE}")
    message(FATAL_ERROR
      "authored SDF VITAL artifact lacks Apache-2.0 SPDX notice: ${FSIM_RELATIVE}")
  endif()
endforeach()

file(READ "${FSIM_LICENSE}" FSIM_LICENSE_CONTENTS LIMIT 256)
if(NOT FSIM_LICENSE_CONTENTS MATCHES "Apache License")
  message(FATAL_ERROR "repository LICENSE is not the reviewed Apache license")
endif()

file(READ "${FSIM_AUDIT}" FSIM_AUDIT_TEXT)
foreach(FSIM_REVIEW_ID IN ITEMS
    B170-C19-LEDGER B170-C19-PROFILES B170-C19-MODELS
    B170-C19-BOUNDARIES B170-C19-PUBLIC B170-C19-ARTIFACTS
    B170-C19-NEGATIVE B170-C19-DEFERRED)
  string(FIND "${FSIM_AUDIT_TEXT}" "`${FSIM_REVIEW_ID}`" FSIM_ID_INDEX)
  if(FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "SDF VITAL audit omits review ${FSIM_REVIEW_ID}")
  endif()
endforeach()

set(FSIM_PUBLIC_TEXT)
foreach(FSIM_PUBLIC_DOCUMENT IN ITEMS
    "${FSIM_README}" "${FSIM_SDF_DOC}" "${FSIM_LANGUAGE_DOC}"
    "${FSIM_ARCHITECTURE_DOC}" "${FSIM_FEATURE_DOC}" "${FSIM_RESUME_DOC}"
    "${FSIM_AUDIT}"
    "${FSIM_EXAMPLE_DIR}/README.md")
  file(READ "${FSIM_PUBLIC_DOCUMENT}" FSIM_PUBLIC_DOCUMENT_TEXT)
  string(APPEND FSIM_PUBLIC_TEXT "${FSIM_PUBLIC_DOCUMENT_TEXT}\n")
endforeach()
foreach(FSIM_DOC_TOKEN IN ITEMS
    "VHDL/VITAL and mixed-language timing application"
    "VHDL/VITAL and mixed-language SDF application"
    "both directions of VHDL-Verilog"
    "models=standard-cell,primitive,state-table,memory,wrapper"
    "project, CLI, Tcl, C, C++ and non-project"
    "Release build testing is not required except during the final batch checks"
    "Header formatting changes that would induce long rebuilds are avoided"
    "Batch 171 planned restart checkpoint")
  string(FIND "${FSIM_PUBLIC_TEXT}" "${FSIM_DOC_TOKEN}" FSIM_DOC_TOKEN_INDEX)
  if(FSIM_DOC_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SDF VITAL public documentation lost token: ${FSIM_DOC_TOKEN}")
  endif()
endforeach()

message(STATUS
  "SDF VITAL release audit: 17 rows, 2746 diagnostics, 1510 bounded sources, ${FSIM_AUTHORED_COUNT} SPDX-owned files")

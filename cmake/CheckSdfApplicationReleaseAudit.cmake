# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_AUDIT
  "${FSIM_SOURCE_DIR}/docs/v2-sdf-application-release-audit.md")
set(FSIM_SDF_DOC "${FSIM_SOURCE_DIR}/docs/sdf.md")
set(FSIM_LANGUAGE_DOC "${FSIM_SOURCE_DIR}/docs/language-support.md")
set(FSIM_ARCHITECTURE_DOC "${FSIM_SOURCE_DIR}/docs/architecture.md")
set(FSIM_FEATURE_DOC "${FSIM_SOURCE_DIR}/docs/feature-matrix.md")
set(FSIM_EXAMPLE "${FSIM_SOURCE_DIR}/examples/sdf_annotation/README.md")
set(FSIM_EXAMPLE_TCL "${FSIM_SOURCE_DIR}/examples/sdf_annotation/annotate.tcl")
set(FSIM_LICENSE "${FSIM_SOURCE_DIR}/LICENSE")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_AUDIT}"
    "${FSIM_SDF_DOC}"
    "${FSIM_LANGUAGE_DOC}"
    "${FSIM_ARCHITECTURE_DOC}"
    "${FSIM_FEATURE_DOC}"
    "${FSIM_EXAMPLE}"
    "${FSIM_EXAMPLE_TCL}"
    "${FSIM_LICENSE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "SDF application release-audit input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_COMPOSED_OUTPUT)
foreach(FSIM_GATE IN ITEMS
    CheckSdfApplicationInventory.cmake
    CheckDiagnosticCatalog.cmake
    CheckSourceLineBudget.cmake)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}"
    RESULT_VARIABLE FSIM_GATE_RESULT
    OUTPUT_VARIABLE FSIM_GATE_OUTPUT
    ERROR_VARIABLE FSIM_GATE_ERROR)
  if(NOT FSIM_GATE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "composed SDF application gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
  string(APPEND FSIM_COMPOSED_OUTPUT
    "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}\n")
endforeach()

foreach(FSIM_EXACT_OUTPUT IN ITEMS
    "SDF application inventory passed: rows=17 preserved=17 active=0 revisions=SDF21,SDF30,SDF40 profiles=V1995,V2001,V2001NoConfig,V2005,SV2005,SV2009,SV2012,SV2017 digest=47e7f5b95aae9f0e3df5cb4fcb4939255e1803f9c920081a47198e21b75f2754"
    "diagnostic catalog covers 2516 production codes"
    "Checked 1100 authored sources against the 2500-line hard limit with a 2000-line refactor target")
  string(FIND "${FSIM_COMPOSED_OUTPUT}" "${FSIM_EXACT_OUTPUT}" FSIM_OUTPUT_INDEX)
  if(FSIM_OUTPUT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SDF application release inventory changed or lost exact evidence: ${FSIM_EXACT_OUTPUT}")
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
if(NOT FSIM_AUTHORED_COUNT EQUAL 1306)
  message(FATAL_ERROR
    "authored SDF application inventory changed: expected 1306 files, "
    "found ${FSIM_AUTHORED_COUNT}")
endif()
foreach(FSIM_FILE IN LISTS FSIM_AUTHORED_FILES)
  file(READ "${FSIM_FILE}" FSIM_PREFIX LIMIT 4096)
  string(FIND
    "${FSIM_PREFIX}" "SPDX-License-Identifier: Apache-2.0" FSIM_SPDX_INDEX)
  if(FSIM_SPDX_INDEX EQUAL -1)
    file(RELATIVE_PATH FSIM_RELATIVE "${FSIM_SOURCE_DIR}" "${FSIM_FILE}")
    message(FATAL_ERROR
      "authored SDF application artifact lacks Apache-2.0 SPDX notice: ${FSIM_RELATIVE}")
  endif()
endforeach()

file(READ "${FSIM_LICENSE}" FSIM_LICENSE_CONTENTS LIMIT 256)
if(NOT FSIM_LICENSE_CONTENTS MATCHES "Apache License")
  message(FATAL_ERROR "repository LICENSE is not the reviewed Apache license")
endif()

file(READ "${FSIM_AUDIT}" FSIM_AUDIT_TEXT)
foreach(FSIM_REVIEW_ID IN ITEMS
    B169-C19-LEDGER
    B169-C19-PROFILES
    B169-C19-PUBLIC
    B169-C19-NEGATIVE
    B169-C19-DEFERRED)
  string(FIND "${FSIM_AUDIT_TEXT}" "`${FSIM_REVIEW_ID}`" FSIM_ID_INDEX)
  if(FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "SDF application audit omits review ${FSIM_REVIEW_ID}")
  endif()
endforeach()

set(FSIM_PUBLIC_TEXT)
foreach(FSIM_PUBLIC_DOCUMENT IN ITEMS
    "${FSIM_SDF_DOC}"
    "${FSIM_LANGUAGE_DOC}"
    "${FSIM_ARCHITECTURE_DOC}"
    "${FSIM_FEATURE_DOC}"
    "${FSIM_AUDIT}")
  file(READ "${FSIM_PUBLIC_DOCUMENT}" FSIM_PUBLIC_DOCUMENT_TEXT)
  string(APPEND FSIM_PUBLIC_TEXT "${FSIM_PUBLIC_DOCUMENT_TEXT}\n")
endforeach()
foreach(FSIM_DOC_TOKEN IN ITEMS
    "Batch 169 applies that immutable representation"
    "Verilog and SystemVerilog SDF timing application in v2"
    "SDF timing-application pipeline"
    "Verilog and SystemVerilog SDF application"
    "Release builds and"
    "tests occur only in final Change 20 checks"
    "Semantic header"
    "edits are not reformatted merely for style")
  string(FIND "${FSIM_PUBLIC_TEXT}" "${FSIM_DOC_TOKEN}" FSIM_DOC_TOKEN_INDEX)
  if(FSIM_DOC_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SDF application public documentation lost token: ${FSIM_DOC_TOKEN}")
  endif()
endforeach()

message(STATUS
  "SDF application release audit: 17 rows, 2516 diagnostics, 1100 bounded sources, "
  "${FSIM_AUTHORED_COUNT} SPDX-owned files")

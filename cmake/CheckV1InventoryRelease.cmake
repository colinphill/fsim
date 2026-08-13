# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v1-inventory-release-audit.md")
set(FSIM_LICENSE "${FSIM_SOURCE_DIR}/LICENSE")
set(FSIM_CONFORMANCE "${FSIM_SOURCE_DIR}/docs/v1-conformance-audit.md")
set(FSIM_CORPUS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v1_conformance_corpus.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_AUDIT}" "${FSIM_LICENSE}" "${FSIM_CONFORMANCE}" "${FSIM_CORPUS}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "inventory release-audit input not found: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_COMPOSED_OUTPUT)
foreach(FSIM_GATE IN ITEMS
    CheckDiagnosticCatalog.cmake
    CheckSourceLineBudget.cmake
    CheckIeeePackageInventory.cmake
    CheckV1ConformanceAudit.cmake
    CheckV1ConformanceCorpus.cmake)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}"
    RESULT_VARIABLE FSIM_GATE_RESULT
    OUTPUT_VARIABLE FSIM_GATE_OUTPUT
    ERROR_VARIABLE FSIM_GATE_ERROR
  )
  if(NOT FSIM_GATE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "composed inventory gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
  string(APPEND FSIM_COMPOSED_OUTPUT
    "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}\n")
endforeach()

foreach(FSIM_EXACT_OUTPUT IN ITEMS
    "diagnostic catalog covers 2236 production codes"
    "Checked 887 authored sources against the 2500-line hard limit with a 2000-line refactor target"
    "v1 conformance audit: 332 authored test/control files, 10 reviewed source IDs, 6 excluded source IDs, 8 coverage queues"
    "v1 conformance corpus: 105 expectations, 28 fixtures, 27 CTests, 49f5754862e3785fa964621b770f202d1d1ad5fb21bd4144b016b7ceb47dd5dd")
  string(FIND
    "${FSIM_COMPOSED_OUTPUT}" "${FSIM_EXACT_OUTPUT}" FSIM_OUTPUT_INDEX)
  if(FSIM_OUTPUT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "release inventory changed or lost exact evidence: ${FSIM_EXACT_OUTPUT}")
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
list(REMOVE_DUPLICATES FSIM_AUTHORED_FILES)
list(LENGTH FSIM_AUTHORED_FILES FSIM_AUTHORED_COUNT)
if(NOT FSIM_AUTHORED_COUNT EQUAL 1029)
  message(FATAL_ERROR
    "authored license inventory changed: expected 1029 files, "
    "found ${FSIM_AUTHORED_COUNT}")
endif()
foreach(FSIM_FILE IN LISTS FSIM_AUTHORED_FILES)
  file(READ "${FSIM_FILE}" FSIM_PREFIX LIMIT 4096)
  string(FIND
    "${FSIM_PREFIX}" "SPDX-License-Identifier: Apache-2.0" FSIM_SPDX_INDEX)
  if(FSIM_SPDX_INDEX EQUAL -1)
    file(RELATIVE_PATH FSIM_RELATIVE "${FSIM_SOURCE_DIR}" "${FSIM_FILE}")
    message(FATAL_ERROR
      "authored release artifact lacks Apache-2.0 SPDX notice: ${FSIM_RELATIVE}")
  endif()
endforeach()

file(READ "${FSIM_LICENSE}" FSIM_LICENSE_CONTENTS LIMIT 256)
if(NOT FSIM_LICENSE_CONTENTS MATCHES "Apache License")
  message(FATAL_ERROR "repository LICENSE is not the reviewed Apache license")
endif()

set(FSIM_IEEE_ROOT "${FSIM_SOURCE_DIR}/third_party/ieee-1076-2019")
file(GLOB_RECURSE FSIM_IEEE_FILES LIST_DIRECTORIES FALSE "${FSIM_IEEE_ROOT}/*")
file(GLOB_RECURSE FSIM_IEEE_VHDL LIST_DIRECTORIES FALSE "${FSIM_IEEE_ROOT}/*.vhdl")
list(LENGTH FSIM_IEEE_FILES FSIM_IEEE_FILE_COUNT)
list(LENGTH FSIM_IEEE_VHDL FSIM_IEEE_VHDL_COUNT)
if(NOT FSIM_IEEE_FILE_COUNT EQUAL 31 OR NOT FSIM_IEEE_VHDL_COUNT EQUAL 26)
  message(FATAL_ERROR
    "reviewed IEEE inventory changed: expected 31 files/26 VHDL, found "
    "${FSIM_IEEE_FILE_COUNT}/${FSIM_IEEE_VHDL_COUNT}")
endif()

file(READ "${FSIM_AUDIT}" FSIM_AUDIT_CONTENTS)
foreach(FSIM_REVIEW_ID IN ITEMS
    B130-T7-DIAGNOSTICS
    B130-T7-SOURCES
    B130-T7-LICENSES
    B130-T7-THIRD-PARTY
    B130-T7-CONFORMANCE
    B130-T7-PROVENANCE)
  string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_REVIEW_ID}`" FSIM_ID_INDEX)
  if(FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "inventory audit omits review ${FSIM_REVIEW_ID}")
  endif()
endforeach()

message(STATUS
  "final inventory audit: 2236 diagnostics, 887 bounded sources, "
  "1029 SPDX-owned files, 31 reviewed IEEE files, and 105 conformance expectations")

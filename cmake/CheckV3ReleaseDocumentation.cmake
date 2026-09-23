# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR OR FSIM_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CurrentEvidenceOwners.cmake")

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_release_documentation.tsv")
file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
set(FSIM_IDS)
set(FSIM_PATHS)
foreach(FSIM_ROW IN LISTS FSIM_ROWS)
  if(FSIM_ROW MATCHES "^#" OR FSIM_ROW STREQUAL "")
    continue()
  endif()
  if(FSIM_ROW MATCHES ";")
    message(FATAL_ERROR "invalid v3 release documentation row")
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 4)
    message(FATAL_ERROR "invalid v3 release documentation row: ${FSIM_ROW}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_KIND)
  list(GET FSIM_FIELDS 2 FSIM_PATH)
  list(GET FSIM_FIELDS 3 FSIM_OWNER)
  if(NOT FSIM_ID MATCHES "^V3DOC-[A-Z0-9-]+$"
     OR NOT FSIM_KIND MATCHES "^(guide|reference|example)$"
     OR FSIM_OWNER STREQUAL "")
    message(FATAL_ERROR "invalid v3 release documentation identity: ${FSIM_ROW}")
  endif()
  if(FSIM_ID IN_LIST FSIM_IDS OR FSIM_PATH IN_LIST FSIM_PATHS)
    message(FATAL_ERROR "duplicate v3 release documentation ID/path: ${FSIM_ID}")
  endif()
  fsim_current_evidence_file("${FSIM_PATH}")
  file(STRINGS "${FSIM_SOURCE_DIR}/${FSIM_PATH}" FSIM_HEADINGS
    REGEX "^# ")
  list(LENGTH FSIM_HEADINGS FSIM_HEADING_COUNT)
  if(FSIM_HEADING_COUNT LESS 1)
    message(FATAL_ERROR "v3 release document has no title: ${FSIM_PATH}")
  endif()
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_PATH}" FSIM_CONTENTS)
  if(NOT FSIM_CONTENTS MATCHES
      "SPDX-License-Identifier: Apache-2.0")
    message(FATAL_ERROR "missing SPDX ownership in ${FSIM_PATH}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_PATHS "${FSIM_PATH}")
endforeach()

set(FSIM_REQUIRED_FILE
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ids.txt")
file(STRINGS "${FSIM_REQUIRED_FILE}" FSIM_REQUIRED_IDS)
foreach(FSIM_ID IN LISTS FSIM_REQUIRED_IDS)
  if(NOT FSIM_ID MATCHES "^V3DOC-[A-Z0-9-]+$")
    continue()
  endif()
  if(NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "required v3 release document is missing: ${FSIM_ID}")
  endif()
endforeach()

file(STRINGS "${FSIM_SOURCE_DIR}/packaging/source-package-manifest.txt"
  FSIM_MANIFEST)
foreach(FSIM_PATH IN LISTS FSIM_PATHS)
  if(NOT FSIM_PATH IN_LIST FSIM_MANIFEST)
    message(FATAL_ERROR "v3 release document is not packaged: ${FSIM_PATH}")
  endif()
endforeach()
foreach(FSIM_PATH IN ITEMS
    examples/v3_coverage/fsim.toml
    examples/v3_coverage/counter.sv
    examples/v3_coverage/coverage_tb.sv)
  fsim_current_evidence_file("${FSIM_PATH}")
  if(NOT FSIM_PATH IN_LIST FSIM_MANIFEST)
    message(FATAL_ERROR "v3 coverage example is not packaged: ${FSIM_PATH}")
  endif()
endforeach()

list(LENGTH FSIM_IDS FSIM_DOCUMENT_COUNT)
message(STATUS
  "v3 release documentation: ${FSIM_DOCUMENT_COUNT} required/additive documents are licensed, titled, and packaged")

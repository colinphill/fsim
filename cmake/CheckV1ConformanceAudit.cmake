# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v1-conformance-audit.md")
set(FSIM_REPOSITORY_LICENSE "${FSIM_SOURCE_DIR}/LICENSE")
set(FSIM_IEEE_ROOT "${FSIM_SOURCE_DIR}/third_party/ieee-1076-2019")
set(FSIM_SYSTEMC_ROOT "${FSIM_SOURCE_DIR}/third_party/systemc-3.0.2")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_AUDIT}"
    "${FSIM_REPOSITORY_LICENSE}"
    "${FSIM_IEEE_ROOT}/LICENSE"
    "${FSIM_IEEE_ROOT}/AUTHORS.md"
    "${FSIM_IEEE_ROOT}/README.md"
    "${FSIM_IEEE_ROOT}/SHA256SUMS"
    "${FSIM_IEEE_ROOT}/inventory.cmake"
    "${FSIM_SYSTEMC_ROOT}/LICENSE"
    "${FSIM_SYSTEMC_ROOT}/NOTICE"
    "${FSIM_SYSTEMC_ROOT}/README.md"
    "${FSIM_SYSTEMC_ROOT}/SOURCE_MANIFEST.txt"
    "${FSIM_SYSTEMC_ROOT}/systemc-3.0.2.spdx.json"
    "${FSIM_SYSTEMC_ROOT}/systemc-3.0.2.tar.gz")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "v1 conformance audit input not found: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_AUDIT}" FSIM_AUDIT_CONTENTS)

set(FSIM_REQUIRED_SOURCE_IDS
  SRC-FSIM
  SRC-IEEE-P1076
  SRC-SV-TESTS
  SRC-SURELOG
  SRC-SLANG
  SRC-UVVM
  SRC-SYSTEMC
  SRC-COCOTB
  SRC-LLVM
  SRC-TCL
)
set(FSIM_EXCLUDED_SOURCE_IDS
  NO-GHDL
  NO-IVERILOG
  NO-NVC
  NO-VUNIT
  NO-VERILATOR
  NO-PROPRIETARY
)
set(FSIM_OPEN_QUEUE_IDS
  B128-T2
  B128-T3
  B128-T4
  B128-T5
  B128-T6
  B128-T7
  B128-T8
  B128-T9
)
foreach(FSIM_ID IN LISTS
    FSIM_REQUIRED_SOURCE_IDS
    FSIM_EXCLUDED_SOURCE_IDS
    FSIM_OPEN_QUEUE_IDS)
  string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_ID}`" FSIM_ID_INDEX)
  if(FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR
      "v1 conformance audit is missing required inventory ID ${FSIM_ID}")
  endif()
endforeach()

set(FSIM_PINNED_COMMITS
  ee17e3999addf3c4c61b985a037be6afd5a2fa81
  16a012320947d378611cc7457f64ed76cb52bac4
  d0cee26833c9138d81e7502a6ca0bdb12ddc4502
  014608ae5145ed38d28b7536cd330433ee50548a
  99197ea10f8d7a476af46718eaacf1b5e93b5e74
  90d56e93c542bf0d5e2ab9f791cc7395bd1aa896
  adb09b1e3f998db9cce702fb8dce22a302c58001
  3ea222d265c71584539040c24764f27f42616f5f
  5daadaa0264a350c3faa4dd0759fec7ef5fe8a75
  b2295663629e1b93173ef17b057ffecea7f1326e
)
foreach(FSIM_COMMIT IN LISTS FSIM_PINNED_COMMITS)
  string(FIND "${FSIM_AUDIT_CONTENTS}" "${FSIM_COMMIT}" FSIM_COMMIT_INDEX)
  if(FSIM_COMMIT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "v1 conformance audit is missing pinned commit ${FSIM_COMMIT}")
  endif()
endforeach()

file(GLOB_RECURSE FSIM_AUTHORED_TEST_FILES LIST_DIRECTORIES FALSE
  "${FSIM_SOURCE_DIR}/tests/*.c"
  "${FSIM_SOURCE_DIR}/tests/*.cpp"
  "${FSIM_SOURCE_DIR}/tests/*.hpp"
  "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt"
)
list(LENGTH FSIM_AUTHORED_TEST_FILES FSIM_AUTHORED_TEST_COUNT)
if(FSIM_AUTHORED_TEST_COUNT LESS 179)
  message(FATAL_ERROR
    "authored test/control inventory shrank below 179 files: "
    "found ${FSIM_AUTHORED_TEST_COUNT}")
endif()

foreach(FSIM_TEST_FILE IN LISTS FSIM_AUTHORED_TEST_FILES)
  file(READ "${FSIM_TEST_FILE}" FSIM_TEST_PREFIX LIMIT 2048)
  string(FIND
    "${FSIM_TEST_PREFIX}"
    "SPDX-License-Identifier: Apache-2.0"
    FSIM_SPDX_INDEX)
  if(FSIM_SPDX_INDEX EQUAL -1)
    file(RELATIVE_PATH
      FSIM_TEST_RELATIVE "${FSIM_SOURCE_DIR}" "${FSIM_TEST_FILE}")
    message(FATAL_ERROR
      "authored test/control file lacks Apache-2.0 SPDX notice: "
      "${FSIM_TEST_RELATIVE}")
  endif()
endforeach()

file(GLOB FSIM_THIRD_PARTY_ENTRIES LIST_DIRECTORIES TRUE
  "${FSIM_SOURCE_DIR}/third_party/*")
list(LENGTH FSIM_THIRD_PARTY_ENTRIES FSIM_THIRD_PARTY_COUNT)
if(NOT FSIM_THIRD_PARTY_COUNT EQUAL 2)
  message(FATAL_ERROR
    "expected exactly two reviewed third-party roots, found "
    "${FSIM_THIRD_PARTY_COUNT}")
endif()
foreach(FSIM_THIRD_PARTY_ENTRY IN LISTS FSIM_THIRD_PARTY_ENTRIES)
  if(NOT FSIM_THIRD_PARTY_ENTRY STREQUAL FSIM_IEEE_ROOT
      AND NOT FSIM_THIRD_PARTY_ENTRY STREQUAL FSIM_SYSTEMC_ROOT)
    message(FATAL_ERROR
      "unreviewed third-party root present: ${FSIM_THIRD_PARTY_ENTRY}")
  endif()
endforeach()

string(FIND "${FSIM_AUDIT_CONTENTS}" "Task 1 imports" FSIM_NO_IMPORT_PREFIX)
string(FIND
  "${FSIM_AUDIT_CONTENTS}" "no upstream test text" FSIM_NO_IMPORT_TEXT)
if(FSIM_NO_IMPORT_PREFIX EQUAL -1 OR FSIM_NO_IMPORT_TEXT EQUAL -1)
  message(FATAL_ERROR
    "v1 conformance audit must state the Task 1 no-import boundary")
endif()

message(STATUS
  "v1 conformance audit: ${FSIM_AUTHORED_TEST_COUNT} authored test/control "
  "files, 10 reviewed source IDs, 6 excluded source IDs, 8 coverage queues")

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_DOCUMENTS
  README.md
  docs/architecture.md
  docs/language-support.md
  docs/systemverilog-uvm.md
  docs/uvm-tutorial.md
  docs/uvm-source-provenance.md
  docs/uvm-closure-audit.md
  docs/diagnostics.md
  docs/feature-matrix.md
  docs/v1-public-release-audit.md
  docs/v1-resource-release-audit.md
  docs/v1-inventory-release-audit.md
  docs/v1-release-audit.md
  docs/v2-resume.md
  tests/feature_matrix/README.md
)

set(FSIM_ALL_CONTENTS)
foreach(FSIM_RELATIVE IN LISTS FSIM_DOCUMENTS)
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "UVM documentation input not found: ${FSIM_RELATIVE}")
  endif()
  file(READ "${FSIM_PATH}" FSIM_CONTENTS)
  string(APPEND FSIM_ALL_CONTENTS "\n${FSIM_CONTENTS}")
  if(NOT FSIM_CONTENTS MATCHES
      "SPDX-License-Identifier: Apache-2\\.0")
    message(FATAL_ERROR "UVM documentation lost SPDX owner: ${FSIM_RELATIVE}")
  endif()
endforeach()

foreach(FSIM_GATE IN ITEMS
    CheckUvmSourceHarness.cmake
    CheckUvmPlatformContract.cmake
    CheckUvmConformanceInventory.cmake
    CheckUvmClosureAudit.cmake
    CheckInstalledPublicContract.cmake)
  if(NOT EXISTS "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}")
    message(FATAL_ERROR "UVM documentation gate owner is missing: ${FSIM_GATE}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/docs/uvm-tutorial.md" FSIM_TUTORIAL)
foreach(FSIM_TOKEN IN ITEMS
    "Producer-independent UVM tutorial"
    "without depending on a vendor wrapper"
    "--uvm-release \"$UVM_RELEASE\" -j 8"
    "--engine interpreter"
    "--engine compiled -O O2"
    "smoke.fsimobj"
    "smoke.fsimdesign"
    "+UVM_CONFIG_DB_TRACE"
    "6 GiB child-process"
    "process exits successfully"
    "no fatal, assertion, timeout, or resource diagnostic appears")
  string(FIND "${FSIM_TUTORIAL}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "producer-independent tutorial lost: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_TOKEN IN ITEMS
    "SV-839"
    "82 `FSIM-UVM-*`"
    "zero unresolved supported gaps"
    "uvm_release_closure.tsv"
    "uvm-tutorial.md"
    "Change 19")
  string(FIND "${FSIM_ALL_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "synchronized UVM documentation lost: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/CMakeLists.txt" FSIM_ROOT_CMAKE)
foreach(FSIM_TOKEN IN ITEMS
    "DIRECTORY docs/"
    "PATTERN \"*.md\"")
  string(FIND "${FSIM_ROOT_CMAKE}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "installed UVM documentation contract lost: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt" FSIM_TEST_CMAKE)
foreach(FSIM_TEST IN ITEMS
    fsim.uvm-documentation
    fsim.uvm-closure-audit
    fsim.installed-public-contract)
  string(FIND "${FSIM_TEST_CMAKE}" "NAME ${FSIM_TEST}" FSIM_TEST_INDEX)
  if(FSIM_TEST_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM documentation test is not registered: ${FSIM_TEST}")
  endif()
endforeach()

message(STATUS
  "UVM documentation contract: producer-independent tutorial, 15 synchronized "
  "public/evidence/release documents, installed Markdown, and zero gaps")

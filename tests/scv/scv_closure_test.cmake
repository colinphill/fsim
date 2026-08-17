# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_REQUIRED_FILES
  "README.md"
  "cmake/RunScvClosure.cmake"
  "docs/architecture.md"
  "docs/diagnostics.md"
  "docs/feature-matrix.md"
  "docs/implementation_plan_v2.md"
  "docs/language-support.md"
  "docs/v2-resume.md"
  "docs/v2-scv-release-audit.md"
  "tests/feature_matrix/scv_inventory.tsv"
  "third_party/scv-2.0.1/PATCHES.txt"
  "third_party/scv-2.0.1/SOURCE_MANIFEST.txt")
foreach(FSIM_RELATIVE IN LISTS FSIM_REQUIRED_FILES)
  if(NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
    message(FATAL_ERROR "SCV closure owner is missing: ${FSIM_RELATIVE}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/tests/feature_matrix/scv_inventory.tsv"
  FSIM_INVENTORY)
string(REGEX MATCHALL "\tpreserved\t" FSIM_PRESERVED_ROWS "${FSIM_INVENTORY}")
list(LENGTH FSIM_PRESERVED_ROWS FSIM_PRESERVED_COUNT)
string(FIND "${FSIM_INVENTORY}" "\tactive\t" FSIM_ACTIVE_OFFSET)
if(NOT FSIM_PRESERVED_COUNT EQUAL 18 OR NOT FSIM_ACTIVE_OFFSET EQUAL -1)
  message(FATAL_ERROR
    "SCV closure requires eighteen preserved rows and zero active rows")
endif()

function(fsim_scv_require_tokens FSIM_PATH)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_PATH}" FSIM_TEXT)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "SCV closure owner ${FSIM_PATH} lost token: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_scv_require_tokens("third_party/scv-2.0.1/SOURCE_MANIFEST.txt"
  "version=2.0.1"
  "archive_sha256=7bd1c4037f3c108d02f45cae003d112efdb788d469cb029fada247d330ca4881"
  "tree_files=578"
  "license=Apache-2.0")
fsim_scv_require_tokens("third_party/scv-2.0.1/PATCHES.txt"
  "patch_count=5"
  "decision=external-cmake-adapter-with-five-generated-source-patches"
  "platform_scope=all-supported-compilers"
  "removal_criteria=remove-each-patch-only")
fsim_scv_require_tokens("docs/architecture.md"
  "official SCV 2.0.1"
  "pointer-free transaction records"
  "post-v2")
fsim_scv_require_tokens("docs/language-support.md"
  "SCV 2.0.1"
  "randomization"
  "transaction recording"
  "post-push gate")
fsim_scv_require_tokens("docs/feature-matrix.md"
  "SCV-001"
  "SCV-012"
  "v2-scv-release-audit.md")
fsim_scv_require_tokens("docs/diagnostics.md"
  "FSIM-SCV-C001"
  "FSIM-SCV-W003"
  "FSIM-SCV-E003")
fsim_scv_require_tokens("docs/v2-scv-release-audit.md"
  "78fda3ccb3264ad3833ddb7334f1b49cdd3c63970fb7aaacd32b8c8b6ad997b0"
  "08de48315d224e3a9340adecaec8c3bb747dc84b43b1a08e6c23a99d264f42a2"
  "Release, sanitizer, and hosted CI"
  "Batch 177")
fsim_scv_require_tokens("docs/implementation_plan_v2.md"
  "Change 19: Complete."
  "Batch 174 - v2 artifact")
fsim_scv_require_tokens("docs/v2-resume.md"
  "Batch 175 planned restart checkpoint"
  "Change 19 is complete")
fsim_scv_require_tokens("README.md"
  "v2-scv-release-audit.md")
fsim_scv_require_tokens("tests/CMakeLists.txt"
  "fsim.scv.closure"
  "fsim.scv-closure-contract"
  "fsim_scv_closure_witnesses")

file(GLOB_RECURSE FSIM_PRODUCTION_SOURCES
  "${FSIM_SOURCE_DIR}/include/*.hpp"
  "${FSIM_SOURCE_DIR}/include/*.h"
  "${FSIM_SOURCE_DIR}/src/*.cpp")
set(FSIM_SCV_CODES)
foreach(FSIM_PRODUCTION_SOURCE IN LISTS FSIM_PRODUCTION_SOURCES)
  file(READ "${FSIM_PRODUCTION_SOURCE}" FSIM_PRODUCTION_TEXT)
  string(REGEX MATCHALL "FSIM-SCV-[A-Z][0-9][0-9][0-9]"
    FSIM_FILE_CODES "${FSIM_PRODUCTION_TEXT}")
  list(APPEND FSIM_SCV_CODES ${FSIM_FILE_CODES})
endforeach()
list(REMOVE_DUPLICATES FSIM_SCV_CODES)
list(LENGTH FSIM_SCV_CODES FSIM_SCV_CODE_COUNT)
if(NOT FSIM_SCV_CODE_COUNT EQUAL 32)
  message(FATAL_ERROR
    "SCV closure requires exactly 32 production diagnostics, got ${FSIM_SCV_CODE_COUNT}")
endif()

message(STATUS
  "SCV closure contract passed: rows=18 active=0 diagnostics=32 release=2.0.1")

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_DOCUMENTS
  README.md
  docs/architecture.md
  docs/language-support.md
  docs/vhdl-psl.md
  docs/vhdl-psl-tutorial.md
  docs/vhdl-psl-closure-audit.md
  docs/vhdl-vhpi.md
  docs/diagnostics.md
  tests/feature_matrix/README.md)
set(FSIM_ALL_CONTENTS)
foreach(FSIM_RELATIVE IN LISTS FSIM_DOCUMENTS)
  set(FSIM_DOCUMENT "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
  if(NOT EXISTS "${FSIM_DOCUMENT}")
    message(FATAL_ERROR "VHDL/PSL documentation input is missing: ${FSIM_RELATIVE}")
  endif()
  file(READ "${FSIM_DOCUMENT}" FSIM_CONTENTS)
  string(APPEND FSIM_ALL_CONTENTS "\n${FSIM_CONTENTS}")
endforeach()

file(READ "${FSIM_SOURCE_DIR}/docs/vhdl-psl.md" FSIM_GUIDE)
foreach(FSIM_TOKEN IN ITEMS
    "29 active rows are supported"
    "four boundaries are explicitly"
    "interpreter, compiled LLVM O0, debug, and LLVM O2"
    "POSIX `RLIMIT_AS` or a Windows Job Object"
    "WebKit preset"
    "FSIM-VHDL-PSL-001` through `019")
  string(FIND "${FSIM_GUIDE}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "VHDL/PSL guide lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/docs/vhdl-psl-tutorial.md" FSIM_TUTORIAL)
foreach(FSIM_TOKEN IN ITEMS
    "fsim-vhdl run -p fsim.toml"
    "vhdl summary"
    "--lang vhdl --standard 2008 --library work"
    "--engine compiled"
    "Move the complete `.fsimdesign` directory"
    "A zero process exit code is not a substitute")
  string(FIND "${FSIM_TUTORIAL}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "VHDL/PSL tutorial lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_GAP
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_psl_gap_inventory.tsv")
set(FSIM_CLOSURE
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_psl_release_closure.tsv")
file(SHA256 "${FSIM_GAP}" FSIM_GAP_DIGEST)
file(SHA256 "${FSIM_CLOSURE}" FSIM_CLOSURE_DIGEST)
file(READ "${FSIM_SOURCE_DIR}/docs/vhdl-psl-closure-audit.md" FSIM_AUDIT)
foreach(FSIM_DIGEST IN ITEMS "${FSIM_GAP_DIGEST}" "${FSIM_CLOSURE_DIGEST}")
  string(FIND "${FSIM_AUDIT}" "${FSIM_DIGEST}" FSIM_DIGEST_INDEX)
  if(FSIM_DIGEST_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL closure audit lost current digest ${FSIM_DIGEST}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "33-row clause inventory"
    "44-row release-closure matrix"
    "17 direct/interpreter/LLVM/cache/"
    "195,272 KiB peak RSS"
    "maximum cognitive complexity 20"
    "offsets 568, 576, and 584")
  string(FIND "${FSIM_AUDIT}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "VHDL/PSL closure audit lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_TOKEN IN ITEMS
    "vhdl-psl.md"
    "vhdl-psl-tutorial.md"
    "vhdl-psl-closure-audit.md"
    "zero unresolved active rows"
    "6 GiB process")
  string(FIND "${FSIM_ALL_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "synchronized VHDL/PSL documentation lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/CMakeLists.txt" FSIM_ROOT_CMAKE)
foreach(FSIM_TOKEN IN ITEMS
    "DIRECTORY docs/"
    "PATTERN \"*.md\"")
  string(FIND "${FSIM_ROOT_CMAKE}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "installed Markdown policy lost: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/cmake/CheckInstalledPublicContract.cmake"
  FSIM_INSTALLED)
foreach(FSIM_DOCUMENT IN ITEMS
    vhdl-psl.md vhdl-psl-tutorial.md vhdl-psl-closure-audit.md)
  string(FIND "${FSIM_INSTALLED}" "${FSIM_DOCUMENT}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "installed VHDL/PSL documentation lost: ${FSIM_DOCUMENT}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt" FSIM_TEST_CMAKE)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.vhdl-psl-documentation"
    "CheckVhdlPslDocumentation.cmake"
    "fsim.vhdl-psl-gap-inventory"
    "fsim.vhdl-psl-closure-audit")
  string(FIND "${FSIM_TEST_CMAKE}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL documentation registration lost: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "VHDL/PSL documentation: support guide, tutorial, closure audit, exact "
  "inventory digests, installed Markdown, and zero unresolved active rows")

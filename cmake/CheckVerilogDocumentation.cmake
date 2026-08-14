# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_normalized_text_sha256 path output_variable)
  file(READ "${path}" contents)
  string(REPLACE "\r\n" "\n" contents "${contents}")
  string(REPLACE "\r" "\n" contents "${contents}")
  string(SHA256 digest "${contents}")
  set("${output_variable}" "${digest}" PARENT_SCOPE)
endfunction()

set(FSIM_DOCUMENTS
  docs/verilog-2005.md
  docs/verilog-2005-tutorial.md
  docs/verilog-2005-closure-audit.md
  docs/systemverilog-vpi.md
  docs/diagnostics.md
  docs/feature-matrix.md
  docs/language-support.md
  docs/architecture.md
  tests/feature_matrix/README.md)
set(FSIM_ALL_CONTENTS)
foreach(FSIM_RELATIVE IN LISTS FSIM_DOCUMENTS)
  set(FSIM_DOCUMENT "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
  if(NOT EXISTS "${FSIM_DOCUMENT}")
    message(FATAL_ERROR
      "Verilog documentation input is missing: ${FSIM_RELATIVE}")
  endif()
  file(READ "${FSIM_DOCUMENT}" FSIM_CONTENTS)
  string(APPEND FSIM_ALL_CONTENTS "\n${FSIM_CONTENTS}")
endforeach()

file(READ "${FSIM_SOURCE_DIR}/docs/verilog-2005.md" FSIM_GUIDE)
foreach(FSIM_TOKEN IN ITEMS
    "34 supported clause rows"
    "12 value paths and records three physical"
    "Verilog source legality is independent of a host word"
    "no implementation-defined 64-bit or one-megabit"
    "VPI vector descriptors"
    "These are test-evidence ceilings, not Verilog language limits")
  string(FIND "${FSIM_GUIDE}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "Verilog support guide lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ
  "${FSIM_SOURCE_DIR}/docs/verilog-2005-tutorial.md"
  FSIM_TUTORIAL)
foreach(FSIM_TOKEN IN ITEMS
    "137'h1_0000_0000_0000_0000_0000_0000_0000_0000_xz"
    "fsim-sv run -p fsim.toml"
    "--lang verilog --standard 2005 --library work"
    "--engine interpreter"
    "Move the complete `.fsimdesign` directory"
    "A zero process exit code is not a substitute")
  string(FIND "${FSIM_TUTORIAL}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "Verilog tutorial lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_GAP
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_gap_inventory.tsv")
set(FSIM_WIDTH
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_literal_width_inventory.tsv")
set(FSIM_CLOSURE
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_release_closure.tsv")
foreach(FSIM_INPUT IN ITEMS "${FSIM_GAP}" "${FSIM_WIDTH}" "${FSIM_CLOSURE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "Verilog documentation inventory is missing: ${FSIM_INPUT}")
  endif()
endforeach()
fsim_normalized_text_sha256("${FSIM_GAP}" FSIM_GAP_DIGEST)
fsim_normalized_text_sha256("${FSIM_WIDTH}" FSIM_WIDTH_DIGEST)
fsim_normalized_text_sha256("${FSIM_CLOSURE}" FSIM_CLOSURE_DIGEST)
file(READ
  "${FSIM_SOURCE_DIR}/docs/verilog-2005-closure-audit.md"
  FSIM_AUDIT)
foreach(FSIM_DIGEST IN ITEMS
    "${FSIM_GAP_DIGEST}" "${FSIM_WIDTH_DIGEST}" "${FSIM_CLOSURE_DIGEST}")
  string(FIND "${FSIM_AUDIT}" "${FSIM_DIGEST}" FSIM_DIGEST_INDEX)
  if(FSIM_DIGEST_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog closure audit lost current digest ${FSIM_DIGEST}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "37-row clause inventory"
    "15-row literal-width inventory"
    "46-row release-closure matrix"
    "138 witness cells, exactly 23 registered CTests, and 17"
    "94.63 seconds"
    "2,389 production diagnostics, 974 bounded C/C++ sources, 1,139 SPDX-owned"
    "not Verilog width or")
  string(FIND "${FSIM_AUDIT}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "Verilog closure audit lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_TOKEN IN ITEMS
    "verilog-2005.md"
    "verilog-2005-tutorial.md"
    "verilog-2005-closure-audit.md"
    "three physical"
    "6 GiB"
    "Older Verilog and SystemVerilog selectable modes"
    "Older Verilog and SystemVerilog revision identity"
    "17 preserved and zero active obligations"
    "fsim::provenance PATH"
    "RunVerilogSystemVerilogStandardModeClosureMatrix.cmake")
  string(FIND "${FSIM_ALL_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "synchronized Verilog documentation lost token: ${FSIM_TOKEN}")
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

file(READ "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt" FSIM_TEST_CMAKE)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.verilog-documentation"
    "CheckVerilogDocumentation.cmake"
    "fsim.verilog-gap-inventory"
    "fsim.verilog-closure-audit")
  string(FIND "${FSIM_TEST_CMAKE}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog documentation registration lost: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "Verilog documentation: support guide, tutorial, closure audit, exact "
  "inventory digests, installed Markdown, width rule, zero unresolved rows, "
  "and synchronized Batch 167 selectable-mode/public-provenance guidance")

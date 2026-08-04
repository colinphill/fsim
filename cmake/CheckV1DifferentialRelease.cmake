# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)
cmake_policy(SET CMP0054 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MATRIX "${FSIM_SOURCE_DIR}/docs/feature-matrix.md")
set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v1-differential-release-audit.md")
set(FSIM_CONFORMANCE
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v1_conformance_corpus.txt")
set(FSIM_PORTABILITY "${FSIM_SOURCE_DIR}/docs/v1-portability-corpus.txt")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_MATRIX}"
    "${FSIM_AUDIT}"
    "${FSIM_CONFORMANCE}"
    "${FSIM_PORTABILITY}"
    "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "differential release-audit input not found: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_COMPOSED_GATES
  CheckV1ReleaseAudit.cmake
  CheckV1SystemVerilogRelease.cmake
  CheckV1VhdlRelease.cmake
  CheckV1MixedSystemCRelease.cmake
  CheckV1ConformanceCorpus.cmake
  CheckV1PortabilityCorpus.cmake
)
foreach(FSIM_GATE IN LISTS FSIM_COMPOSED_GATES)
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
      "composed differential gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
endforeach()

file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
file(READ "${FSIM_AUDIT}" FSIM_AUDIT_CONTENTS)
file(READ "${FSIM_CONFORMANCE}" FSIM_CONFORMANCE_CONTENTS)
file(READ "${FSIM_PORTABILITY}" FSIM_PORTABILITY_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)

string(REPLACE ";" "<SEMICOLON>" FSIM_MATRIX_LINES "${FSIM_MATRIX_CONTENTS}")
string(REPLACE "\n" ";" FSIM_MATRIX_LINES "${FSIM_MATRIX_LINES}")
set(FSIM_REQUIRED_COUNT 0)
set(FSIM_INTERPRETER_ROWS 0)
set(FSIM_LLVM_ROWS 0)
set(FSIM_CACHE_ROWS 0)
set(FSIM_DEBUGGER_ROWS 0)
set(FSIM_VCD_ROWS 0)
set(FSIM_SCHEDULING_ROWS 0)
set(FSIM_FAILURE_ROWS 0)
set(FSIM_RUNTIME_OWNERS)

foreach(FSIM_LINE IN LISTS FSIM_MATRIX_LINES)
  if(NOT FSIM_LINE MATCHES
      "^\\| ((VH|SV|CM|SC|ML|V1-CM|V1-SV|V1-VH)-[0-9]+) \\|")
    continue()
  endif()
  math(EXPR FSIM_REQUIRED_COUNT "${FSIM_REQUIRED_COUNT} + 1")
  set(FSIM_FIELDS_TEXT "${FSIM_LINE}")
  string(REPLACE "\\|" "<PIPE>" FSIM_FIELDS_TEXT "${FSIM_FIELDS_TEXT}")
  string(REPLACE "|" ";" FSIM_FIELDS "${FSIM_FIELDS_TEXT}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  math(EXPR FSIM_RUNTIME_INDEX "${FSIM_FIELD_COUNT} - 2")
  list(GET FSIM_FIELDS ${FSIM_RUNTIME_INDEX} FSIM_RUNTIME)
  string(STRIP "${FSIM_RUNTIME}" FSIM_RUNTIME)
  string(TOLOWER "${FSIM_RUNTIME}" FSIM_RUNTIME_LOWER)
  if(FSIM_RUNTIME_LOWER MATCHES "interpreter")
    math(EXPR FSIM_INTERPRETER_ROWS "${FSIM_INTERPRETER_ROWS} + 1")
  endif()
  if(FSIM_RUNTIME_LOWER MATCHES "llvm|compiled|native")
    math(EXPR FSIM_LLVM_ROWS "${FSIM_LLVM_ROWS} + 1")
  endif()
  if(FSIM_RUNTIME_LOWER MATCHES "cache|cold|warm|edit|invalidation|reuse")
    math(EXPR FSIM_CACHE_ROWS "${FSIM_CACHE_ROWS} + 1")
  endif()
  if(FSIM_RUNTIME_LOWER MATCHES "debug")
    math(EXPR FSIM_DEBUGGER_ROWS "${FSIM_DEBUGGER_ROWS} + 1")
  endif()
  if(FSIM_RUNTIME_LOWER MATCHES "vcd|trace")
    math(EXPR FSIM_VCD_ROWS "${FSIM_VCD_ROWS} + 1")
  endif()
  if(FSIM_RUNTIME_LOWER MATCHES
      "schedul|delta|phase|wait|event|delay|time|lifecycle")
    math(EXPR FSIM_SCHEDULING_ROWS "${FSIM_SCHEDULING_ROWS} + 1")
  endif()
  if(FSIM_RUNTIME_LOWER MATCHES
      "fail|diagnostic|reject|invalid|error|callback|abi")
    math(EXPR FSIM_FAILURE_ROWS "${FSIM_FAILURE_ROWS} + 1")
  endif()
  set(FSIM_RUNTIME_LINKS "${FSIM_RUNTIME}")
  while(FSIM_RUNTIME_LINKS MATCHES "\\]\\((\\.\\./tests/[^)#]+)")
    list(APPEND FSIM_RUNTIME_OWNERS "${CMAKE_MATCH_1}")
    string(REPLACE "${CMAKE_MATCH_0}" "" FSIM_RUNTIME_LINKS "${FSIM_RUNTIME_LINKS}")
  endwhile()
endforeach()
list(REMOVE_DUPLICATES FSIM_RUNTIME_OWNERS)
list(LENGTH FSIM_RUNTIME_OWNERS FSIM_RUNTIME_OWNER_COUNT)

set(FSIM_CORPUS_OWNERS)
foreach(FSIM_CORPUS IN ITEMS FSIM_CONFORMANCE_CONTENTS FSIM_PORTABILITY_CONTENTS)
  set(FSIM_CORPUS_LINES "${${FSIM_CORPUS}}")
  string(REPLACE ";" "<SEMICOLON>" FSIM_CORPUS_LINES "${FSIM_CORPUS_LINES}")
  string(REPLACE "\n" ";" FSIM_CORPUS_LINES "${FSIM_CORPUS_LINES}")
  foreach(FSIM_LINE IN LISTS FSIM_CORPUS_LINES)
    if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#")
      continue()
    endif()
    string(REPLACE "|" ";" FSIM_FIELDS "${FSIM_LINE}")
    list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
    if(FSIM_CORPUS STREQUAL "FSIM_CONFORMANCE_CONTENTS")
      if(NOT FSIM_FIELD_COUNT EQUAL 3)
        message(FATAL_ERROR "malformed conformance corpus row: ${FSIM_LINE}")
      endif()
      list(GET FSIM_FIELDS 1 FSIM_CTEST)
    else()
      if(NOT FSIM_FIELD_COUNT EQUAL 6)
        message(FATAL_ERROR "malformed portability corpus row: ${FSIM_LINE}")
      endif()
      list(GET FSIM_FIELDS 3 FSIM_CTEST)
    endif()
    list(APPEND FSIM_CORPUS_OWNERS "${FSIM_CTEST}")
  endforeach()
endforeach()
list(REMOVE_DUPLICATES FSIM_CORPUS_OWNERS)
list(SORT FSIM_CORPUS_OWNERS)
list(LENGTH FSIM_CORPUS_OWNERS FSIM_CORPUS_OWNER_COUNT)

set(FSIM_MODE_TEXT "${FSIM_CONFORMANCE_CONTENTS}\n${FSIM_PORTABILITY_CONTENTS}")
string(TOLOWER "${FSIM_MODE_TEXT}" FSIM_MODE_TEXT)
foreach(FSIM_MODE IN ITEMS
    interpreter
    llvm-o0
    llvm-o2
    cache-cold
    cache-warm
    cache-edit
    debugger
    vcd
    callback
    diagnostic
    lifecycle
    abi
    plugin
    compiler
    source-map
    portable-path)
  string(FIND "${FSIM_MODE_TEXT}" "${FSIM_MODE}" FSIM_MODE_INDEX)
  if(FSIM_MODE_INDEX EQUAL -1)
    message(FATAL_ERROR "differential corpus union omits mode ${FSIM_MODE}")
  endif()
endforeach()

set(FSIM_REVIEW_IDS
  B130-T5-ENGINE
  B130-T5-CACHE
  B130-T5-DEBUG
  B130-T5-SCHEDULING
  B130-T5-FAILURE
)
foreach(FSIM_REVIEW_ID IN LISTS FSIM_REVIEW_IDS)
  string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_REVIEW_ID}`" FSIM_REVIEW_INDEX)
  if(FSIM_REVIEW_INDEX EQUAL -1)
    message(FATAL_ERROR "differential audit omits review ${FSIM_REVIEW_ID}")
  endif()
endforeach()

if(NOT FSIM_REQUIRED_COUNT EQUAL 1111
    OR NOT FSIM_RUNTIME_OWNER_COUNT EQUAL 103
    OR NOT FSIM_CORPUS_OWNER_COUNT EQUAL 36
    OR NOT FSIM_INTERPRETER_ROWS EQUAL 459
    OR NOT FSIM_LLVM_ROWS EQUAL 378
    OR NOT FSIM_CACHE_ROWS EQUAL 254
    OR NOT FSIM_DEBUGGER_ROWS EQUAL 97
    OR NOT FSIM_VCD_ROWS EQUAL 127
    OR NOT FSIM_SCHEDULING_ROWS EQUAL 410
    OR NOT FSIM_FAILURE_ROWS EQUAL 92)
  message(FATAL_ERROR
    "differential baseline is incomplete: rows=${FSIM_REQUIRED_COUNT}, "
    "runtime owners=${FSIM_RUNTIME_OWNER_COUNT}, corpus owners=${FSIM_CORPUS_OWNER_COUNT}, "
    "interpreter=${FSIM_INTERPRETER_ROWS}, LLVM=${FSIM_LLVM_ROWS}, "
    "cache=${FSIM_CACHE_ROWS}, debugger=${FSIM_DEBUGGER_ROWS}, "
    "VCD=${FSIM_VCD_ROWS}, scheduling=${FSIM_SCHEDULING_ROWS}, "
    "failure=${FSIM_FAILURE_ROWS}")
endif()

string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "NAME fsim.v1-differential-release"
  FSIM_REGISTRATION_INDEX
)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "fsim.v1-differential-release is not registered")
endif()

message(STATUS
  "final differential audit: ${FSIM_REQUIRED_COUNT} rows, "
  "${FSIM_RUNTIME_OWNER_COUNT} runtime files, ${FSIM_CORPUS_OWNER_COUNT} corpus CTests; "
  "interpreter=${FSIM_INTERPRETER_ROWS}, LLVM=${FSIM_LLVM_ROWS}, "
  "cache=${FSIM_CACHE_ROWS}, debugger=${FSIM_DEBUGGER_ROWS}, "
  "VCD=${FSIM_VCD_ROWS}, scheduling=${FSIM_SCHEDULING_ROWS}, "
  "failure=${FSIM_FAILURE_ROWS}")

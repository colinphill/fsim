# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MATRIX
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_psl_release_closure.tsv")
set(FSIM_APPLICATION
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_psl_application_test.cpp")
set(FSIM_DIAGNOSTICS "${FSIM_SOURCE_DIR}/docs/diagnostics.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_INSTALLED
  "${FSIM_SOURCE_DIR}/cmake/CheckInstalledPublicContract.cmake")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_MATRIX}" "${FSIM_APPLICATION}" "${FSIM_DIAGNOSTICS}"
    "${FSIM_TEST_CMAKE}" "${FSIM_INSTALLED}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "VHDL/PSL closure-audit input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_MATRIX}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 46)
  message(FATAL_ERROR
    "VHDL/PSL release closure must contain SPDX, one header, and 44 rows; "
    "got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL "id\tcontract\tvalue\tevidence")
  message(FATAL_ERROR "VHDL/PSL release-closure schema changed")
endif()

set(FSIM_IDS)
set(FSIM_CONTRACTS)
foreach(FSIM_INDEX RANGE 2 45)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 4)
    message(FATAL_ERROR
      "VHDL/PSL closure row ${FSIM_INDEX} does not have four fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_CONTRACT)
  list(GET FSIM_FIELDS 2 FSIM_VALUE)
  list(GET FSIM_FIELDS 3 FSIM_EVIDENCE)
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_ID_INDEX)
  list(FIND FSIM_CONTRACTS "${FSIM_CONTRACT}" FSIM_CONTRACT_INDEX)
  if(NOT FSIM_ID_INDEX EQUAL -1 OR NOT FSIM_CONTRACT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "duplicate VHDL/PSL closure identity: ${FSIM_ID}/${FSIM_CONTRACT}")
  endif()
  if(FSIM_VALUE STREQUAL "" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE}")
    message(FATAL_ERROR
      "${FSIM_ID} has an empty value or missing evidence ${FSIM_EVIDENCE}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_CONTRACTS "${FSIM_CONTRACT}")
endforeach()

file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
foreach(FSIM_EXPECTED IN ITEMS
    "supported_rows=29"
    "unresolved_active_rows=0"
    "deferred_rows=4"
    "supported_witnesses=87"
    "exact_stage_count=17"
    "direct_build=present"
    "interpreter=present"
    "llvm_o0=present"
    "llvm_o2=present"
    "cache_cold=present"
    "cache_warm=present"
    "debugger=present"
    "vcd_trace=exact-equality"
    "object_artifact=portable"
    "library_artifact=portable"
    "design_artifact=portable"
    "relocation=source-independent"
    "replay=cold+warm"
    "checkpoint=vhpi-remap"
    "multiple_root=ordered-identities"
    "mixed_systemverilog=executable"
    "mixed_systemc=executable"
    "malformed_input=transactional"
    "race_snapshot=immutable"
    "cancellation_abort=deterministic"
    "nonconvergence=bounded"
    "resource_limits=transactional"
    "process_address_space_bytes=6442450944"
    "maximum_deltas=1000"
    "maximum_trace_signals=64"
    "ctest_timeout_seconds=1200"
    "source_line_hard_limit=2500"
    "license_review=SPDX+third-party"
    "artifact_provenance=checksum+identity"
    "platform_process_limit=POSIX+Windows"
    "installed_public_alias=fsim-vhdl"
    "diagnostic_catalog=2546"
    "vhdl_psl_diagnostics=19"
    "bounded_authored_sources=1149"
    "authored_test_controls=436"
    "reviewed_max_cognitive_complexity=20"
    "reviewed_max_loop_depth=1"
    "reviewed_recursion=none"
    "observed_maximum_rss_kib=195272")
  string(REPLACE "=" ";" FSIM_EXPECTED_FIELDS "${FSIM_EXPECTED}")
  list(GET FSIM_EXPECTED_FIELDS 0 FSIM_EXPECTED_CONTRACT)
  list(GET FSIM_EXPECTED_FIELDS 1 FSIM_EXPECTED_VALUE)
  string(FIND "${FSIM_MATRIX_CONTENTS}"
    "\t${FSIM_EXPECTED_CONTRACT}\t${FSIM_EXPECTED_VALUE}\t"
    FSIM_EXPECTED_INDEX)
  if(FSIM_EXPECTED_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL closure matrix lost ${FSIM_EXPECTED}")
  endif()
endforeach()
string(TOLOWER "${FSIM_MATRIX_CONTENTS}" FSIM_MATRIX_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "xfail" "expected-fail" "waiver" "allowlist" "suppress")
  string(FIND "${FSIM_MATRIX_LOWER}" "${FSIM_FORBIDDEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL closure matrix contains forbidden escape ${FSIM_FORBIDDEN}")
  endif()
endforeach()

set(FSIM_COMPOSED_OUTPUT)
foreach(FSIM_GATE IN ITEMS
    CheckVhdlPslGapInventory.cmake
    CheckDiagnosticCatalog.cmake
    CheckSourceLineBudget.cmake
    CheckV1ConformanceAudit.cmake)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}"
    RESULT_VARIABLE FSIM_GATE_RESULT
    OUTPUT_VARIABLE FSIM_GATE_OUTPUT
    ERROR_VARIABLE FSIM_GATE_ERROR)
  if(NOT FSIM_GATE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "VHDL/PSL closure composed gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
  string(APPEND FSIM_COMPOSED_OUTPUT
    "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}\n")
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "33 rows split 29 supported, 0 unresolved, 4 deferred"
    "diagnostic catalog covers 2576 production codes"
    "Checked 1265 authored sources against the 2500-line hard limit"
    "v1 conformance audit: 471 authored test/control files")
  string(FIND "${FSIM_COMPOSED_OUTPUT}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL closure lost composed evidence: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_DIAGNOSTICS}" FSIM_DIAGNOSTIC_CONTENTS)
string(REGEX MATCHALL "FSIM-VHDL-PSL-[0-9]+" FSIM_PSL_CODES
  "${FSIM_DIAGNOSTIC_CONTENTS}")
list(REMOVE_DUPLICATES FSIM_PSL_CODES)
list(LENGTH FSIM_PSL_CODES FSIM_PSL_CODE_COUNT)
if(NOT FSIM_PSL_CODE_COUNT EQUAL 19)
  message(FATAL_ERROR
    "VHDL/PSL diagnostic inventory changed: expected 19, got ${FSIM_PSL_CODE_COUNT}")
endif()

file(READ "${FSIM_APPLICATION}" FSIM_APPLICATION_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "FSIM-VHDL-PSL-PASS stages=direct/interpreter/llvm-o0/llvm-o2/"
    "cache-cold/cache-warm/debug/vcd/object/library/design/relocation/"
    "replay/checkpoint/multiple-root/systemverilog/systemc"
    "resources=as6g/delta1000/vcd64 gaps=0"
    "6ULL * 1024ULL * 1024ULL * 1024ULL"
    "getrlimit(RLIMIT_AS"
    "JOB_OBJECT_LIMIT_PROCESS_MEMORY")
  string(FIND "${FSIM_APPLICATION_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL application lost governed token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.vhdl-psl-closure-audit"
    "CheckVhdlPslClosureAudit.cmake"
    "fsim.application.vhdl_psl\n      PROPERTIES TIMEOUT 1200")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL/PSL CTest contract lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_INSTALLED}" FSIM_INSTALLED_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_VHDL_EXECUTABLE_NAME"
    "^Usage: fsim-vhdl")
  string(FIND "${FSIM_INSTALLED_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "installed VHDL public contract lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "VHDL/PSL closure audit: 44 contracts, 17 governed stages, 29 supported "
  "rows, 19 PSL diagnostics, 2576 catalog codes, 1265 bounded sources, "
  "436 test/control files, cognitive complexity 20, loop depth 1, no "
  "recursion, 195272 KiB observed RSS, 6 GiB ceiling, and zero unresolved rows")

# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

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

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/mixed_toolchain_install_contract.tsv")
set(FSIM_EXPECTED_DIGEST
  "728dfff671a198543bbd60474cafed1a40c2a99d345cab016388324f95abb116")
fsim_normalized_text_sha256("${FSIM_CONTRACT}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "mixed-toolchain install digest changed: expected ${FSIM_EXPECTED_DIGEST}, "
    "got ${FSIM_ACTUAL_DIGEST}")
endif()

file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 34)
  message(FATAL_ERROR
    "mixed-toolchain install contract requires SPDX, header and 32 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tboundary\tproducer_identity\tportable_policy\tnative_policy\truntime_resolution\tmismatch_action\tevidence")
  message(FATAL_ERROR "mixed-toolchain install header or SPDX drifted")
endif()

set(FSIM_IDS)
foreach(FSIM_INDEX RANGE 2 33)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 8)
    message(FATAL_ERROR "mixed-toolchain row ${FSIM_INDEX} is malformed")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 3 FSIM_PORTABLE)
  list(GET FSIM_FIELDS 4 FSIM_NATIVE)
  list(GET FSIM_FIELDS 5 FSIM_RUNTIME)
  list(GET FSIM_FIELDS 6 FSIM_ACTION)
  list(GET FSIM_FIELDS 7 FSIM_EVIDENCE)
  math(EXPR FSIM_NUMBER "${FSIM_INDEX} - 1")
  if(FSIM_NUMBER LESS 10)
    set(FSIM_EXPECTED_ID "TOOL174-0${FSIM_NUMBER}")
  else()
    set(FSIM_EXPECTED_ID "TOOL174-${FSIM_NUMBER}")
  endif()
  if(NOT FSIM_ID STREQUAL FSIM_EXPECTED_ID OR
     NOT FSIM_PORTABLE STREQUAL "reusable" OR FSIM_NATIVE STREQUAL "" OR
     FSIM_RUNTIME STREQUAL "" OR FSIM_ACTION STREQUAL "" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE}")
    message(FATAL_ERROR "mixed-toolchain row ${FSIM_ID} drifted")
  endif()
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate mixed-toolchain id ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()

function(fsim_require_toolchain_tokens relative_path)
  file(READ "${FSIM_SOURCE_DIR}/${relative_path}" FSIM_TEXT)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_TEXT}" "${FSIM_TOKEN}" FSIM_OFFSET)
    if(FSIM_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "mixed-toolchain owner ${relative_path} lost ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_toolchain_tokens(src/systemc/plugin_compiler_common.cpp
  "clang-cl" "cl.exe" "HostToolchain::gcc_like"
  "compiler.requested" "compiler.resolved" "compiler.binary")
fsim_require_toolchain_tokens(tests/systemc/plugin_compiler_test.cpp
  "HostToolchain::msvc" "clang-cl.exe"
  "expected_msvc_runtime_option" "/bigobj" "/vmg")
fsim_require_toolchain_tokens(tests/systemc/incremental_compiler_test.cpp
  "fake clang-cl object" "toolchain == \"msvc\""
  "compiler_fingerprint" "stale_target_plugin")
fsim_require_toolchain_tokens(src/app/application_library_import.cpp
  "mapped LLVM native object producer"
  "mapped SystemC native plug-in producer" "read_payload(")
fsim_require_toolchain_tokens(cmake/CheckInstalledPublicContract.cmake
  "relocated install é" "CMAKE_PREFIX_PATH" "file(CHMOD_RECURSE")
fsim_require_toolchain_tokens(tests/scv/scv_installed_consumer_test.cmake
  "installed consumer resolved a non-relocated SCV or SystemC runtime")
fsim_require_toolchain_tokens(tests/api/api_mapped_library_test.cpp
  "native_accepted" "native_fingerprint" "fsim_session_build")

message(STATUS
  "mixed-toolchain install contract passed: rows=32 digest=${FSIM_ACTUAL_DIGEST}")

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_TARGET_DIR "${FSIM_SOURCE_DIR}/packaging/targets")
set(FSIM_TARGET_FILES
    windows-llvm-mingw-llvm22.txt
    windows-llvm-mingw-no-llvm.txt)
set(FSIM_TARGET_IDS)
foreach(FSIM_TARGET_FILE IN LISTS FSIM_TARGET_FILES)
  set(FSIM_PATH "${FSIM_TARGET_DIR}/${FSIM_TARGET_FILE}")
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "Windows package target is missing: ${FSIM_TARGET_FILE}")
  endif()
  file(STRINGS "${FSIM_PATH}" FSIM_LINES ENCODING UTF-8)
  set(FSIM_KEYS)
  foreach(FSIM_LINE IN LISTS FSIM_LINES)
    if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#")
      continue()
    endif()
    if(NOT FSIM_LINE MATCHES "^([a-z0-9_]+)=(.*)$")
      message(FATAL_ERROR
        "malformed Windows package target row in ${FSIM_TARGET_FILE}: ${FSIM_LINE}")
    endif()
    set(FSIM_KEY "${CMAKE_MATCH_1}")
    set(FSIM_VALUE "${CMAKE_MATCH_2}")
    list(FIND FSIM_KEYS "${FSIM_KEY}" FSIM_DUPLICATE_INDEX)
    if(NOT FSIM_DUPLICATE_INDEX EQUAL -1 OR FSIM_VALUE STREQUAL "")
      message(FATAL_ERROR
        "duplicate/empty Windows package key ${FSIM_KEY}: ${FSIM_TARGET_FILE}")
    endif()
    list(APPEND FSIM_KEYS "${FSIM_KEY}")
    set("FSIM_VALUE_${FSIM_KEY}" "${FSIM_VALUE}")
  endforeach()
  foreach(FSIM_REQUIRED_KEY IN ITEMS
      schema target_id host toolchain_family toolchain_release compiler_version
      target_triple crt llvm_mode llvm_version llvm_package tcl_mode
      configurations archive_format toolchain_archive_url
      toolchain_archive_sha256 job_timeout_minutes hosted_workers
      warning_log_owner batch176_evidence batch177_required signature)
    list(FIND FSIM_KEYS "${FSIM_REQUIRED_KEY}" FSIM_KEY_INDEX)
    if(FSIM_KEY_INDEX EQUAL -1)
      message(FATAL_ERROR
        "Windows package target ${FSIM_TARGET_FILE} omits ${FSIM_REQUIRED_KEY}")
    endif()
  endforeach()
  if(NOT FSIM_VALUE_schema STREQUAL "fsim-package-target-v1"
     OR NOT FSIM_VALUE_host STREQUAL "windows-x86_64"
     OR NOT FSIM_VALUE_toolchain_family STREQUAL "LLVM-MinGW"
     OR NOT FSIM_VALUE_toolchain_release STREQUAL "20260616"
     OR NOT FSIM_VALUE_compiler_version STREQUAL "22.1.8"
     OR NOT FSIM_VALUE_target_triple STREQUAL "x86_64-w64-windows-gnu"
     OR NOT FSIM_VALUE_crt STREQUAL "ucrt"
     OR NOT FSIM_VALUE_tcl_mode STREQUAL "ON"
     OR NOT FSIM_VALUE_configurations STREQUAL "Debug,Release"
     OR NOT FSIM_VALUE_archive_format STREQUAL "zip"
     OR NOT FSIM_VALUE_toolchain_archive_url STREQUAL
        "https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip"
     OR NOT FSIM_VALUE_toolchain_archive_sha256 STREQUAL
        "b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35"
     OR NOT FSIM_VALUE_job_timeout_minutes STREQUAL "120"
     OR NOT FSIM_VALUE_hosted_workers STREQUAL "4"
     OR NOT FSIM_VALUE_warning_log_owner STREQUAL
        "batch177-retained-compiler-linker-and-test-log"
     OR NOT FSIM_VALUE_batch176_evidence STREQUAL
        "definition-and-static-portability-only"
     OR NOT FSIM_VALUE_batch177_required STREQUAL
        "debug-build,release-build,binary-archive,install-smoke,warning-audit,hosted-windows-matrix"
     OR NOT FSIM_VALUE_signature STREQUAL "unsigned-release")
    message(FATAL_ERROR
      "Windows package target policy drifted: ${FSIM_TARGET_FILE}")
  endif()
  if(FSIM_VALUE_llvm_mode STREQUAL "ON")
    if(NOT FSIM_VALUE_llvm_version STREQUAL "22.1.8"
       OR NOT FSIM_VALUE_llvm_package STREQUAL
          "mingw-w64-clang-x86_64-llvm-22.1.8-2")
      message(FATAL_ERROR "Windows LLVM-enabled package identity drifted")
    endif()
  elseif(NOT FSIM_VALUE_llvm_mode STREQUAL "OFF"
         OR NOT FSIM_VALUE_llvm_version STREQUAL "none"
         OR NOT FSIM_VALUE_llvm_package STREQUAL "none")
    message(FATAL_ERROR "invalid Windows LLVM package profile")
  endif()
  list(FIND FSIM_TARGET_IDS "${FSIM_VALUE_target_id}" FSIM_ID_INDEX)
  if(NOT FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate Windows package target: ${FSIM_VALUE_target_id}")
  endif()
  list(APPEND FSIM_TARGET_IDS "${FSIM_VALUE_target_id}")
endforeach()

list(SORT FSIM_TARGET_IDS)
if(NOT FSIM_TARGET_IDS STREQUAL
   "windows-x86_64-llvm-mingw-20260616-llvm22;windows-x86_64-llvm-mingw-20260616-no-llvm")
  message(FATAL_ERROR "Windows package target set drifted: ${FSIM_TARGET_IDS}")
endif()

set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_INSTALLER
    "${FSIM_SOURCE_DIR}/.github/scripts/install-windows-llvm-mingw.ps1")
set(FSIM_ROOT "${FSIM_SOURCE_DIR}/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS "${FSIM_WORKFLOW}" "${FSIM_INSTALLER}" "${FSIM_ROOT}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "Windows package policy input is missing: ${FSIM_INPUT}")
  endif()
endforeach()
file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
file(READ "${FSIM_INSTALLER}" FSIM_INSTALLER_CONTENTS)
file(READ "${FSIM_ROOT}" FSIM_ROOT_CONTENTS)

foreach(FSIM_WORKFLOW_POLICY IN ITEMS
    "windows-llvm-mingw:"
    "llvm-mingw-20260616-ucrt-x86_64.zip"
    "b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35"
    "mingw-w64-clang-x86_64-llvm-22.1.8-2"
    "configuration: Debug"
    "configuration: Release"
    "llvm_mode: 'ON'"
    "llvm_mode: 'OFF'"
    "-DFSIM_TCL_MODE=ON"
    "fsim-v2.0.0-windows-x86_64-llvm-mingw-no-llvm.zip"
    "fsim-v2.0.0-windows-x86_64-llvm-mingw-llvm22.zip"
    "batch177-change20-windows-no-llvm-install.log"
    "batch177-change20-windows-llvm22-install.log"
    "expected_tests: '352'"
    "expected_tests: '356'"
    "expected_archive_entries: '1247'"
    "for attempt in 1 2 3; do"
    "LLVM package installation attempt \${attempt} failed; retrying"
    "-DFSIM_BINARY_ONLY=ON"
    "-DFSIM_BINARY_PACKAGE_NAME=\${{ matrix.binary_package }}"
    "Create deterministic Windows binary archive"
    "Audit deterministic Windows install lane"
    "-DFSIM_CHANGE12_LANE=ON"
    "-DFSIM_CHANGE12_EXPECTED_ARCHIVE_ENTRIES=\${{ matrix.expected_archive_entries }}"
    "Upload deterministic Windows binary archive"
    "actions/upload-artifact@v7"
    "if-no-files-found: error"
    "--parallel 4")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_WORKFLOW_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "workflow lost Windows package policy: ${FSIM_WORKFLOW_POLICY}")
  endif()
endforeach()
string(REGEX MATCHALL "timeout-minutes:[ ]*([0-9]+)" FSIM_TIMEOUT_ROWS
  "${FSIM_WORKFLOW_CONTENTS}")
list(LENGTH FSIM_TIMEOUT_ROWS FSIM_TIMEOUT_COUNT)
if(NOT FSIM_TIMEOUT_COUNT EQUAL 4)
  message(FATAL_ERROR
    "hosted job timeout inventory drifted: expected 4, found ${FSIM_TIMEOUT_COUNT}")
endif()
foreach(FSIM_TIMEOUT_ROW IN LISTS FSIM_TIMEOUT_ROWS)
  if(NOT FSIM_TIMEOUT_ROW MATCHES "timeout-minutes:[ ]*120$")
    message(FATAL_ERROR "hosted job timeout is not 120 minutes: ${FSIM_TIMEOUT_ROW}")
  endif()
endforeach()

foreach(FSIM_INSTALLER_POLICY IN ITEMS
    "Get-FileHash -LiteralPath $archivePath -Algorithm SHA256"
    "llvm-mingw-$Release-ucrt-x86_64"
    "x86_64-w64-windows-gnu"
    "bin/clang++.exe"
    "bin/llvm-windres.exe")
  string(FIND "${FSIM_INSTALLER_CONTENTS}" "${FSIM_INSTALLER_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "LLVM-MinGW installer lost package policy: ${FSIM_INSTALLER_POLICY}")
  endif()
endforeach()

foreach(FSIM_STATIC_POLICY IN ITEMS
    "CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL \"MSVC\""
    "add_compile_options(/bigobj)"
    "FSIM_TEST_ASSERTIONS_HEADER"
    "CONTENT \"#pragma once\\n#undef NDEBUG\\n\""
    "\"/FI\${FSIM_TEST_ASSERTIONS_HEADER}\""
    "target_compile_options(\${target} PRIVATE -UNDEBUG)")
  string(FIND "${FSIM_ROOT_CONTENTS}" "${FSIM_STATIC_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "MSVC-compatible static portability policy drifted: ${FSIM_STATIC_POLICY}")
  endif()
endforeach()
string(FIND "${FSIM_ROOT_CONTENTS}" "PRIVATE /UNDEBUG" FSIM_UNDEBUG_INDEX)
if(NOT FSIM_UNDEBUG_INDEX EQUAL -1)
  message(FATAL_ERROR
    "conflicting MSVC /DNDEBUG /UNDEBUG warning policy was reintroduced")
endif()

message(STATUS
  "Windows package definitions: 2 LLVM-MinGW 20260616 UCRT targets; all 4 "
  "hosted timeouts are 120 minutes; four retained Windows lane artifacts and "
  "two Release binary archives, /bigobj and warning-clean test assertion "
  "policy are statically owned; every real Windows result is deferred to "
  "Batch 177 Change 20")

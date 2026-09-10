# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_hosted_toolchain_inventory.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_LINES ENCODING UTF-8)
list(LENGTH FSIM_LINES FSIM_LINE_COUNT)
if(NOT FSIM_LINE_COUNT EQUAL 11)
  message(FATAL_ERROR "v3 hosted-toolchain ledger must contain nine rows")
endif()
list(GET FSIM_LINES 0 FSIM_LICENSE)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\trunner\tcompiler\tcompiler_version\tllvm_mode\tllvm_version\tconfiguration\tartifact\tretained_log\ttimeout_minutes\tworkers\towner")
  message(FATAL_ERROR "v3 hosted-toolchain ledger header changed")
endif()

set(FSIM_EXPECTED_IDS
  V3HOST-LINUX-CLANG-DEBUG V3HOST-LINUX-CLANG-RELEASE
  V3HOST-LINUX-LLVM-DEBUG V3HOST-LINUX-LLVM-RELEASE V3HOST-LINUX-FUZZ
  V3HOST-WINDOWS-DEBUG-OFF V3HOST-WINDOWS-DEBUG-ON
  V3HOST-WINDOWS-RELEASE-OFF V3HOST-WINDOWS-RELEASE-ON)
set(FSIM_IDS)
set(FSIM_LINUX_COUNT 0)
set(FSIM_WINDOWS_COUNT 0)
set(FSIM_LLVM_ON_COUNT 0)
foreach(FSIM_INDEX RANGE 2 10)
  list(GET FSIM_LINES ${FSIM_INDEX} FSIM_LINE)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 12)
    message(FATAL_ERROR "v3 hosted-toolchain row must contain twelve fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_RUNNER)
  list(GET FSIM_FIELDS 2 FSIM_COMPILER)
  list(GET FSIM_FIELDS 4 FSIM_LLVM_MODE)
  list(GET FSIM_FIELDS 5 FSIM_LLVM_VERSION)
  list(GET FSIM_FIELDS 6 FSIM_CONFIGURATION)
  list(GET FSIM_FIELDS 7 FSIM_ARTIFACT)
  list(GET FSIM_FIELDS 8 FSIM_RETAINED_LOG)
  list(GET FSIM_FIELDS 9 FSIM_TIMEOUT)
  list(GET FSIM_FIELDS 10 FSIM_WORKERS)
  list(GET FSIM_FIELDS 11 FSIM_OWNER)
  if(FSIM_ID IN_LIST FSIM_IDS OR NOT FSIM_ID IN_LIST FSIM_EXPECTED_IDS OR
     NOT FSIM_RUNNER MATCHES "^(ubuntu-latest|ubuntu-24.04|windows-2022)$" OR
     NOT FSIM_COMPILER MATCHES "^(clang|llvm-mingw)$" OR
     NOT FSIM_LLVM_MODE MATCHES "^(ON|OFF)$" OR
     NOT FSIM_CONFIGURATION MATCHES "^(Debug|Release|RelWithDebInfo)$" OR
     NOT FSIM_TIMEOUT STREQUAL "120" OR NOT FSIM_WORKERS STREQUAL "2" OR
     NOT FSIM_OWNER STREQUAL "B188-C14" OR FSIM_ARTIFACT STREQUAL "" OR
     IS_ABSOLUTE "${FSIM_RETAINED_LOG}" OR
     FSIM_RETAINED_LOG MATCHES "(^|/)\.\.(/|$)" OR
     NOT FSIM_RETAINED_LOG MATCHES "^build/qualification/.*\.log$")
    message(FATAL_ERROR "v3 hosted-toolchain row is malformed: ${FSIM_ID}")
  endif()
  if(FSIM_LLVM_MODE STREQUAL "ON")
    if(NOT FSIM_LLVM_VERSION STREQUAL "22.1.8")
      message(FATAL_ERROR "v3 hosted LLVM version changed: ${FSIM_ID}")
    endif()
    math(EXPR FSIM_LLVM_ON_COUNT "${FSIM_LLVM_ON_COUNT} + 1")
  elseif(NOT FSIM_LLVM_VERSION STREQUAL "none")
    message(FATAL_ERROR "v3 hosted LLVM-off lane names a version: ${FSIM_ID}")
  endif()
  if(FSIM_RUNNER MATCHES "^ubuntu")
    math(EXPR FSIM_LINUX_COUNT "${FSIM_LINUX_COUNT} + 1")
  else()
    math(EXPR FSIM_WINDOWS_COUNT "${FSIM_WINDOWS_COUNT} + 1")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()
foreach(FSIM_ID IN LISTS FSIM_EXPECTED_IDS)
  if(NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "v3 hosted-toolchain lane is missing: ${FSIM_ID}")
  endif()
endforeach()
if(NOT FSIM_LINUX_COUNT EQUAL 5 OR NOT FSIM_WINDOWS_COUNT EQUAL 4 OR
   NOT FSIM_LLVM_ON_COUNT EQUAL 4)
  message(FATAL_ERROR
    "v3 hosted-toolchain counts changed: linux=${FSIM_LINUX_COUNT} windows=${FSIM_WINDOWS_COUNT} llvm=${FSIM_LLVM_ON_COUNT}")
endif()

function(fsim_require_hosted_tokens FSIM_RELATIVE)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR
        "v3 hosted-toolchain evidence changed in ${FSIM_RELATIVE}: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_hosted_tokens(.github/workflows/ci.yml
  "runs-on: ubuntu-latest"
  "runs-on: ubuntu-24.04"
  "runs-on: windows-2022"
  "timeout-minutes: 120"
  "clang-22 --version"
  "clang++-22 --version"
  "installed_version=$(/usr/lib/llvm-22/bin/llvm-config --version)"
  "if [[ \"\${installed_version}\" != \"22.1.8\" ]]"
  "-Release \"20260616\""
  "-Version \"22.1.8\""
  "-Sha256 \"b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35\""
  "test \"$(/clang64/bin/llvm-config.exe --version)\" = \"22.1.8\""
  "--parallel 2"
  "actions/upload-artifact@v7"
  "if-no-files-found: error")
fsim_require_hosted_tokens(CMakePresets.json
  "\"name\": \"ci-linux\""
  "\"name\": \"ci-linux-release\""
  "\"name\": \"ci-fuzz\""
  "\"name\": \"ci-windows\""
  "\"name\": \"ci-windows-release\""
  "\"CMAKE_C_COMPILER\": \"clang-22\""
  "\"FSIM_WARNINGS_AS_ERRORS\": \"ON\"")
fsim_require_hosted_tokens(tests/CMakeLists.txt
  "NAME fsim.v3-hosted-toolchains"
  "CheckV3HostedToolchains.cmake")

file(SHA256 "${FSIM_LEDGER}" FSIM_LEDGER_DIGEST)
set(FSIM_EXPECTED_DIGEST "0d2009b5f38a5833c94cacf91898c4f81d1c97ea4d057712b107ff1591b0492a")
if(NOT FSIM_LEDGER_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "v3 hosted-toolchain digest changed: expected=${FSIM_EXPECTED_DIGEST} actual=${FSIM_LEDGER_DIGEST}")
endif()
message(STATUS
  "v3 hosted toolchains passed: lanes=9 linux=5 windows=4 llvm22=4 digest=${FSIM_LEDGER_DIGEST}")

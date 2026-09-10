# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_warning_audit_inventory.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_LINES ENCODING UTF-8)
list(LENGTH FSIM_LINES FSIM_LINE_COUNT)
if(NOT FSIM_LINE_COUNT EQUAL 11)
  message(FATAL_ERROR "v3 warning-audit ledger must contain nine rows")
endif()
list(GET FSIM_LINES 0 FSIM_LICENSE)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tplatform\tcompiler\tconfiguration\tllvm_mode\tconfigure_owner\tworkflow_owner\twarning_policy\towner")
  message(FATAL_ERROR "v3 warning-audit ledger header changed")
endif()

set(FSIM_EXPECTED_IDS
  V3WARN-LINUX-CLANG-DEBUG V3WARN-LINUX-CLANG-RELEASE
  V3WARN-LINUX-LLVM-DEBUG V3WARN-LINUX-LLVM-RELEASE V3WARN-LINUX-FUZZ
  V3WARN-WINDOWS-DEBUG-OFF V3WARN-WINDOWS-DEBUG-ON
  V3WARN-WINDOWS-RELEASE-OFF V3WARN-WINDOWS-RELEASE-ON)
set(FSIM_IDS)
set(FSIM_LINUX_COUNT 0)
set(FSIM_WINDOWS_COUNT 0)
foreach(FSIM_INDEX RANGE 2 10)
  list(GET FSIM_LINES ${FSIM_INDEX} FSIM_LINE)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 9)
    message(FATAL_ERROR "v3 warning-audit row must contain nine fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_PLATFORM)
  list(GET FSIM_FIELDS 2 FSIM_COMPILER)
  list(GET FSIM_FIELDS 3 FSIM_CONFIGURATION)
  list(GET FSIM_FIELDS 4 FSIM_LLVM_MODE)
  list(GET FSIM_FIELDS 8 FSIM_OWNER)
  if(FSIM_ID IN_LIST FSIM_IDS OR NOT FSIM_ID IN_LIST FSIM_EXPECTED_IDS OR
     NOT FSIM_COMPILER MATCHES "^(clang(-22)?|llvm-mingw-clang)$" OR
     NOT FSIM_CONFIGURATION MATCHES "^(Debug|Release|RelWithDebInfo)$" OR
     NOT FSIM_LLVM_MODE MATCHES "^(ON|OFF)$" OR
     NOT FSIM_OWNER STREQUAL "B188-C13")
    message(FATAL_ERROR "v3 warning-audit row is malformed: ${FSIM_ID}")
  endif()
  if(FSIM_PLATFORM MATCHES "^ubuntu")
    math(EXPR FSIM_LINUX_COUNT "${FSIM_LINUX_COUNT} + 1")
  elseif(FSIM_PLATFORM STREQUAL "windows-2022")
    math(EXPR FSIM_WINDOWS_COUNT "${FSIM_WINDOWS_COUNT} + 1")
  else()
    message(FATAL_ERROR "v3 warning-audit platform is unsupported: ${FSIM_ID}")
  endif()
  foreach(FSIM_FIELD_INDEX RANGE 5 7)
    list(GET FSIM_FIELDS ${FSIM_FIELD_INDEX} FSIM_REFERENCE)
    string(REGEX REPLACE ":[^:]+$" "" FSIM_RELATIVE "${FSIM_REFERENCE}")
    if(IS_ABSOLUTE "${FSIM_RELATIVE}" OR
       FSIM_RELATIVE MATCHES "(^|/)\.\.(/|$)" OR
       FSIM_RELATIVE MATCHES "[\\]" OR
       NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
      message(FATAL_ERROR
        "v3 warning-audit owner is unsafe or missing: ${FSIM_ID}")
    endif()
  endforeach()
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()
foreach(FSIM_ID IN LISTS FSIM_EXPECTED_IDS)
  if(NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "v3 warning-audit lane is missing: ${FSIM_ID}")
  endif()
endforeach()
if(NOT FSIM_LINUX_COUNT EQUAL 5 OR NOT FSIM_WINDOWS_COUNT EQUAL 4)
  message(FATAL_ERROR
    "v3 warning-audit platform counts changed: ${FSIM_LINUX_COUNT}/${FSIM_WINDOWS_COUNT}")
endif()

function(fsim_require_warning_tokens FSIM_RELATIVE)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR
        "v3 warning-audit evidence changed in ${FSIM_RELATIVE}: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_warning_tokens(cmake/FsimWarnings.cmake
  "function(fsim_enable_warnings target)"
  "if(FSIM_WARNINGS_AS_ERRORS)"
  "target_compile_options(\${target} PRIVATE -Werror)")
fsim_require_warning_tokens(CMakePresets.json
  "\"CMAKE_C_COMPILER\": \"clang-22\""
  "\"CMAKE_CXX_COMPILER\": \"clang++-22\""
  "\"name\": \"ci-linux\""
  "\"name\": \"ci-linux-release\""
  "\"name\": \"ci-fuzz\""
  "\"name\": \"ci-windows\""
  "\"name\": \"ci-windows-release\""
  "\"FSIM_WARNINGS_AS_ERRORS\": \"ON\"")
fsim_require_warning_tokens(.github/workflows/ci.yml
  "linux-clang:"
  "linux-llvm22:"
  "linux-fuzz:"
  "windows-llvm-mingw:"
  "-DCMAKE_C_COMPILER=clang-22"
  "-DCMAKE_CXX_COMPILER=clang++-22"
  "-DCMAKE_C_COMPILER=$env:LLVM_MINGW_ROOT/bin/clang.exe"
  "-DCMAKE_CXX_COMPILER=$env:LLVM_MINGW_ROOT/bin/clang++.exe"
  "-DFSIM_WARNINGS_AS_ERRORS=ON"
  "--parallel 2")
fsim_require_warning_tokens(tests/CMakeLists.txt
  "NAME fsim.v3-warning-audit"
  "CheckV3WarningAudit.cmake")

file(SHA256 "${FSIM_LEDGER}" FSIM_LEDGER_DIGEST)
set(FSIM_EXPECTED_DIGEST
  "92f5da8d3cd328c8924a4383861b52c0c2ecb2608046980e202a7585946d13ff")
if(NOT FSIM_LEDGER_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "v3 warning-audit digest changed: expected=${FSIM_EXPECTED_DIGEST} actual=${FSIM_LEDGER_DIGEST}")
endif()
message(STATUS
  "v3 warning audit passed: lanes=9 linux=5 windows=4 digest=${FSIM_LEDGER_DIGEST}")

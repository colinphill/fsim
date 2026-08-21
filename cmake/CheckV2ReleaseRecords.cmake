# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)
cmake_policy(SET CMP0057 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_RECORD "${FSIM_SOURCE_DIR}/packaging/v2-release-record.txt")
set(FSIM_SUPPORT "${FSIM_SOURCE_DIR}/packaging/v2-support-matrix.tsv")
set(FSIM_EXAMPLES "${FSIM_SOURCE_DIR}/packaging/example-output-freeze.tsv")
set(FSIM_CHANGELOG "${FSIM_SOURCE_DIR}/docs/changelog-v2.md")
set(FSIM_KNOWN_ISSUES "${FSIM_SOURCE_DIR}/docs/known-issues-v2.md")
set(FSIM_RELEASE_GUIDE "${FSIM_SOURCE_DIR}/docs/release-and-post-v2.md")
set(FSIM_IMPLEMENTATION_PLAN
    "${FSIM_SOURCE_DIR}/docs/implementation_plan_v2.md")
set(FSIM_ROOT_CMAKE "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_VERSION_HEADER "${FSIM_SOURCE_DIR}/include/fsim/version.hpp")
set(FSIM_PC_TEMPLATE "${FSIM_SOURCE_DIR}/cmake/fsim.pc.in")
set(FSIM_CLI_DRIVER "${FSIM_SOURCE_DIR}/src/cli/driver.cpp")
set(FSIM_TCL_APPLICATION "${FSIM_SOURCE_DIR}/src/app/tcl.cpp")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_RECORD}" "${FSIM_SUPPORT}" "${FSIM_EXAMPLES}"
    "${FSIM_CHANGELOG}" "${FSIM_KNOWN_ISSUES}" "${FSIM_RELEASE_GUIDE}"
    "${FSIM_IMPLEMENTATION_PLAN}"
    "${FSIM_ROOT_CMAKE}" "${FSIM_VERSION_HEADER}" "${FSIM_PC_TEMPLATE}"
    "${FSIM_CLI_DRIVER}" "${FSIM_TCL_APPLICATION}"
    "${FSIM_TEST_CMAKE}" "${FSIM_WORKFLOW}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "v2 release-record input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_RECORD}" FSIM_RECORD_LINES ENCODING UTF-8)
set(FSIM_RECORD_KEYS)
foreach(FSIM_LINE IN LISTS FSIM_RECORD_LINES)
  if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#")
    continue()
  endif()
  if(NOT FSIM_LINE MATCHES "^([a-z0-9_]+)=(.*)$")
    message(FATAL_ERROR "malformed v2 release-record row: ${FSIM_LINE}")
  endif()
  set(FSIM_KEY "${CMAKE_MATCH_1}")
  set(FSIM_VALUE "${CMAKE_MATCH_2}")
  list(FIND FSIM_RECORD_KEYS "${FSIM_KEY}" FSIM_DUPLICATE_INDEX)
  if(NOT FSIM_DUPLICATE_INDEX EQUAL -1 OR FSIM_VALUE STREQUAL "" OR
     FSIM_VALUE STREQUAL "TO_FILL")
    message(FATAL_ERROR "duplicate/empty/unfrozen v2 release key: ${FSIM_KEY}")
  endif()
  list(APPEND FSIM_RECORD_KEYS "${FSIM_KEY}")
  set("FSIM_RECORD_${FSIM_KEY}" "${FSIM_VALUE}")
endforeach()
foreach(FSIM_REQUIRED_KEY IN ITEMS
    schema candidate compiled_version tag_name tag_kind
    release_notes_sha256 known_issues_sha256 release_guide_sha256
    source_artifact linux_clang22_llvm22_artifact
    linux_gcc13_llvm22_artifact linux_gcc13_no_llvm_artifact
    windows_llvm_mingw_llvm22_artifact
    windows_llvm_mingw_no_llvm_artifact source_manifest_entries
    source_manifest_sha256 source_exclusions package_policy_sha256
    linux_clang22_llvm22_target_sha256 linux_gcc13_llvm22_target_sha256
    linux_gcc13_no_llvm_target_sha256
    windows_llvm_mingw_llvm22_target_sha256
    windows_llvm_mingw_no_llvm_target_sha256 support_matrix_rows
    support_matrix_sha256 example_rows example_output_sha256
    qualification_rows qualification_sha256 performance_rows
    performance_sha256 execution_matrix_rows execution_matrix_sha256
    candidate_logs candidate_log_audit_sha256
    systemc_sbom_sha256 scv_sbom_sha256 root_license_sha256
    systemc_license_sha256 systemc_notice_sha256 scv_license_sha256
    scv_notice_sha256 change12_source_debug_proof_sha256
    change12_binary_debug_proof_sha256 signature batch177_required)
  list(FIND FSIM_RECORD_KEYS "${FSIM_REQUIRED_KEY}" FSIM_KEY_INDEX)
  if(FSIM_KEY_INDEX EQUAL -1)
    message(FATAL_ERROR "v2 release record omits ${FSIM_REQUIRED_KEY}")
  endif()
endforeach()

if(NOT FSIM_RECORD_schema STREQUAL "fsim-v2-release-record-v1" OR
   NOT FSIM_RECORD_candidate STREQUAL "v2.0.0" OR
   NOT FSIM_RECORD_compiled_version STREQUAL "2.0.0" OR
   NOT FSIM_RECORD_tag_name STREQUAL "v2.0.0" OR
   NOT FSIM_RECORD_tag_kind STREQUAL "annotated" OR
   NOT FSIM_RECORD_source_artifact STREQUAL "fsim-v2.0.0-source.zip" OR
   NOT FSIM_RECORD_linux_clang22_llvm22_artifact STREQUAL
       "fsim-v2.0.0-linux-x86_64-clang22-llvm22.zip" OR
   NOT FSIM_RECORD_linux_gcc13_llvm22_artifact STREQUAL
       "fsim-v2.0.0-linux-x86_64-gcc13-llvm22.zip" OR
   NOT FSIM_RECORD_linux_gcc13_no_llvm_artifact STREQUAL
       "fsim-v2.0.0-linux-x86_64-gcc13-no-llvm.zip" OR
   NOT FSIM_RECORD_windows_llvm_mingw_llvm22_artifact STREQUAL
       "fsim-v2.0.0-windows-x86_64-llvm-mingw-llvm22.zip" OR
   NOT FSIM_RECORD_windows_llvm_mingw_no_llvm_artifact STREQUAL
       "fsim-v2.0.0-windows-x86_64-llvm-mingw-no-llvm.zip" OR
   NOT FSIM_RECORD_source_exclusions STREQUAL "23" OR
   NOT FSIM_RECORD_support_matrix_rows STREQUAL "7" OR
   NOT FSIM_RECORD_example_rows STREQUAL "6" OR
   NOT FSIM_RECORD_qualification_rows STREQUAL "19" OR
   NOT FSIM_RECORD_performance_rows STREQUAL "44" OR
   NOT FSIM_RECORD_execution_matrix_rows STREQUAL "30" OR
   NOT FSIM_RECORD_candidate_logs STREQUAL "69" OR
   NOT FSIM_RECORD_signature STREQUAL "unsigned-release")
  message(FATAL_ERROR "v2 release record scalar policy drifted")
endif()

file(STRINGS "${FSIM_RELEASE_GUIDE}" FSIM_RELEASE_GUIDE_LINES ENCODING UTF-8)
set(FSIM_EXECUTION_ROWS)
set(FSIM_EXECUTION_IDS)
set(FSIM_HOSTED_ARTIFACTS)
set(FSIM_HOSTED_LOGS)
set(FSIM_WINDOWS_BINARY_ARTIFACTS)
set(FSIM_WINDOWS_BINARY_LOGS)
set(FSIM_WINDOWS_INSTALL_LOGS)
foreach(FSIM_LINE IN LISTS FSIM_RELEASE_GUIDE_LINES)
  if(NOT FSIM_LINE MATCHES "^E177-")
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 12)
    message(FATAL_ERROR
      "Batch 177 execution row has ${FSIM_FIELD_COUNT} fields: ${FSIM_LINE}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_CHANGE)
  list(GET FSIM_FIELDS 2 FSIM_STATE)
  list(GET FSIM_FIELDS 3 FSIM_PLATFORM)
  list(GET FSIM_FIELDS 4 FSIM_TOOLCHAIN)
  list(GET FSIM_FIELDS 5 FSIM_CONFIGURATION)
  list(GET FSIM_FIELDS 6 FSIM_WORKERS)
  list(GET FSIM_FIELDS 7 FSIM_TIMEOUT)
  list(GET FSIM_FIELDS 8 FSIM_ACTION)
  list(GET FSIM_FIELDS 9 FSIM_ARTIFACT)
  list(GET FSIM_FIELDS 10 FSIM_LOG)
  list(GET FSIM_FIELDS 11 FSIM_BOUNDARY)
  list(FIND FSIM_EXECUTION_IDS "${FSIM_ID}" FSIM_DUPLICATE_INDEX)
  if(NOT FSIM_DUPLICATE_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate Batch 177 execution row: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_EXECUTION_IDS "${FSIM_ID}")
  list(APPEND FSIM_EXECUTION_ROWS "${FSIM_LINE}")
  if(NOT FSIM_ID MATCHES "^E177-[0-9][0-9]$" OR
     NOT FSIM_CHANGE MATCHES "^B177-C(0[5-9]|1[0-2])$" OR
     NOT FSIM_STATE STREQUAL "planned" OR
     NOT FSIM_PLATFORM MATCHES "^(linux-x86_64|windows-x86_64|all)$" OR
     NOT FSIM_CONFIGURATION MATCHES "^(Debug|Release|RelWithDebInfo|all)$" OR
     NOT FSIM_WORKERS MATCHES "^(1|4|8)$" OR
     NOT FSIM_TIMEOUT STREQUAL "120" OR
     FSIM_TOOLCHAIN STREQUAL "" OR FSIM_ACTION STREQUAL "" OR
     FSIM_ARTIFACT STREQUAL "" OR
     NOT FSIM_LOG MATCHES
       "^build/qualification/batch177-change(0[5-9]|1[0-2]|20)-.*\\.log$" OR
     NOT FSIM_BOUNDARY MATCHES
       "^(local-change(0[5-9]|1[0-2])|post-push-hosted-change20)$")
    message(FATAL_ERROR "malformed Batch 177 execution row: ${FSIM_LINE}")
  endif()
  if(FSIM_PLATFORM STREQUAL "windows-x86_64" OR
     FSIM_CHANGE STREQUAL "B177-C08")
    if(NOT FSIM_WORKERS STREQUAL "4" OR
       NOT FSIM_BOUNDARY STREQUAL "post-push-hosted-change20")
      message(FATAL_ERROR
        "hosted/Windows execution is not owned by final Change 20: ${FSIM_ID}")
    endif()
    if(FSIM_CHANGE STREQUAL "B177-C08")
      list(APPEND FSIM_HOSTED_ARTIFACTS "${FSIM_ARTIFACT}")
      list(APPEND FSIM_HOSTED_LOGS "${FSIM_LOG}")
    elseif(FSIM_CHANGE STREQUAL "B177-C10")
      list(APPEND FSIM_WINDOWS_BINARY_ARTIFACTS "${FSIM_ARTIFACT}")
      list(APPEND FSIM_WINDOWS_BINARY_LOGS "${FSIM_LOG}")
    elseif(FSIM_CHANGE STREQUAL "B177-C12")
      list(APPEND FSIM_WINDOWS_INSTALL_LOGS "${FSIM_LOG}")
    endif()
  elseif(FSIM_BOUNDARY STREQUAL "post-push-hosted-change20")
    if(NOT FSIM_CHANGE STREQUAL "B177-C08" OR
       NOT FSIM_WORKERS STREQUAL "4")
      message(FATAL_ERROR
        "non-Windows post-push ownership is invalid: ${FSIM_ID}")
    endif()
  endif()
  string(REPLACE "-" "_" FSIM_CHANGE_KEY "${FSIM_CHANGE}")
  set(FSIM_COUNT_NAME "FSIM_COUNT_${FSIM_CHANGE_KEY}")
  if(NOT DEFINED ${FSIM_COUNT_NAME})
    set(${FSIM_COUNT_NAME} 0)
  endif()
  math(EXPR ${FSIM_COUNT_NAME} "${${FSIM_COUNT_NAME}} + 1")
endforeach()

list(LENGTH FSIM_EXECUTION_ROWS FSIM_EXECUTION_COUNT)
if(NOT FSIM_EXECUTION_COUNT EQUAL 30 OR
   NOT FSIM_COUNT_B177_C05 EQUAL 2 OR
   NOT FSIM_COUNT_B177_C06 EQUAL 2 OR
   NOT FSIM_COUNT_B177_C07 EQUAL 5 OR
   NOT FSIM_COUNT_B177_C08 EQUAL 9 OR
   NOT FSIM_COUNT_B177_C09 EQUAL 1 OR
   NOT FSIM_COUNT_B177_C10 EQUAL 5 OR
   NOT FSIM_COUNT_B177_C11 EQUAL 1 OR
   NOT FSIM_COUNT_B177_C12 EQUAL 5)
  message(FATAL_ERROR
    "Batch 177 execution matrix requires 30 rows owned 2/2/5/9/1/5/1/5")
endif()
set(FSIM_EXPECTED_INDEX 1)
foreach(FSIM_ID IN LISTS FSIM_EXECUTION_IDS)
  if(FSIM_EXPECTED_INDEX LESS 10)
    set(FSIM_EXPECTED_ID "E177-0${FSIM_EXPECTED_INDEX}")
  else()
    set(FSIM_EXPECTED_ID "E177-${FSIM_EXPECTED_INDEX}")
  endif()
  if(NOT FSIM_ID STREQUAL FSIM_EXPECTED_ID)
    message(FATAL_ERROR
      "Batch 177 execution order drifted: expected ${FSIM_EXPECTED_ID}, got ${FSIM_ID}")
  endif()
  math(EXPR FSIM_EXPECTED_INDEX "${FSIM_EXPECTED_INDEX} + 1")
endforeach()
string(JOIN "\n" FSIM_EXECUTION_TEXT ${FSIM_EXECUTION_ROWS})
string(APPEND FSIM_EXECUTION_TEXT "\n")
string(SHA256 FSIM_EXECUTION_SHA256 "${FSIM_EXECUTION_TEXT}")
if(NOT FSIM_EXECUTION_SHA256 STREQUAL FSIM_RECORD_execution_matrix_sha256)
  message(FATAL_ERROR
    "Batch 177 execution matrix digest drifted: expected "
    "${FSIM_RECORD_execution_matrix_sha256}, got ${FSIM_EXECUTION_SHA256}")
endif()

file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
string(REPLACE "\r\n" "\n" FSIM_WORKFLOW_CONTENTS
  "${FSIM_WORKFLOW_CONTENTS}")

function(fsim_require_workflow_occurrences token expected_count)
  string(LENGTH "${token}" FSIM_TOKEN_LENGTH)
  if(FSIM_TOKEN_LENGTH EQUAL 0)
    message(FATAL_ERROR "empty hosted-workflow policy token")
  endif()
  string(LENGTH "${FSIM_WORKFLOW_CONTENTS}" FSIM_BEFORE_LENGTH)
  string(REPLACE "${token}" "" FSIM_WITHOUT_TOKEN
    "${FSIM_WORKFLOW_CONTENTS}")
  string(LENGTH "${FSIM_WITHOUT_TOKEN}" FSIM_AFTER_LENGTH)
  math(EXPR FSIM_REMOVED_LENGTH
    "${FSIM_BEFORE_LENGTH} - ${FSIM_AFTER_LENGTH}")
  math(EXPR FSIM_OCCURRENCES
    "${FSIM_REMOVED_LENGTH} / ${FSIM_TOKEN_LENGTH}")
  if(NOT FSIM_OCCURRENCES EQUAL expected_count)
    message(FATAL_ERROR
      "hosted-workflow token count drifted: expected ${expected_count}, "
      "found ${FSIM_OCCURRENCES}: ${token}")
  endif()
endfunction()

list(LENGTH FSIM_HOSTED_ARTIFACTS FSIM_HOSTED_ARTIFACT_COUNT)
list(LENGTH FSIM_HOSTED_LOGS FSIM_HOSTED_LOG_COUNT)
if(NOT FSIM_HOSTED_ARTIFACT_COUNT EQUAL 9 OR
   NOT FSIM_HOSTED_LOG_COUNT EQUAL 9)
  message(FATAL_ERROR "Batch 177 Change 8 requires nine hosted artifacts/logs")
endif()
list(REMOVE_DUPLICATES FSIM_HOSTED_ARTIFACTS)
list(REMOVE_DUPLICATES FSIM_HOSTED_LOGS)
list(LENGTH FSIM_HOSTED_ARTIFACTS FSIM_HOSTED_ARTIFACT_UNIQUE_COUNT)
list(LENGTH FSIM_HOSTED_LOGS FSIM_HOSTED_LOG_UNIQUE_COUNT)
if(NOT FSIM_HOSTED_ARTIFACT_UNIQUE_COUNT EQUAL 9 OR
   NOT FSIM_HOSTED_LOG_UNIQUE_COUNT EQUAL 9)
  message(FATAL_ERROR "hosted artifact/log identities are not unique")
endif()

foreach(FSIM_HOSTED_INDEX RANGE 0 8)
  list(GET FSIM_HOSTED_ARTIFACTS ${FSIM_HOSTED_INDEX} FSIM_ARTIFACT)
  list(GET FSIM_HOSTED_LOGS ${FSIM_HOSTED_INDEX} FSIM_LOG)
  fsim_require_workflow_occurrences("${FSIM_ARTIFACT}" 1)
  fsim_require_workflow_occurrences("${FSIM_LOG}" 1)
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_ARTIFACT}"
    FSIM_ARTIFACT_INDEX)
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_LOG}" FSIM_LOG_INDEX)
  math(EXPR FSIM_MAPPING_DISTANCE "${FSIM_LOG_INDEX} - ${FSIM_ARTIFACT_INDEX}")
  if(FSIM_MAPPING_DISTANCE LESS 0 OR FSIM_MAPPING_DISTANCE GREATER 200)
    message(FATAL_ERROR
      "hosted artifact/log mapping is not adjacent: ${FSIM_ARTIFACT}/${FSIM_LOG}")
  endif()
endforeach()

list(LENGTH FSIM_WINDOWS_BINARY_ARTIFACTS FSIM_WINDOWS_BINARY_ARTIFACT_COUNT)
list(LENGTH FSIM_WINDOWS_BINARY_LOGS FSIM_WINDOWS_BINARY_LOG_COUNT)
if(NOT FSIM_WINDOWS_BINARY_ARTIFACT_COUNT EQUAL 2 OR
   NOT FSIM_WINDOWS_BINARY_LOG_COUNT EQUAL 2)
  message(FATAL_ERROR "Batch 177 Change 10 requires two Windows archive lanes")
endif()
foreach(FSIM_WINDOWS_BINARY_INDEX RANGE 0 1)
  list(GET FSIM_WINDOWS_BINARY_ARTIFACTS ${FSIM_WINDOWS_BINARY_INDEX}
    FSIM_BINARY_ARTIFACT)
  list(GET FSIM_WINDOWS_BINARY_LOGS ${FSIM_WINDOWS_BINARY_INDEX}
    FSIM_BINARY_LOG)
  fsim_require_workflow_occurrences("${FSIM_BINARY_ARTIFACT}" 1)
  fsim_require_workflow_occurrences("${FSIM_BINARY_LOG}" 1)
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_BINARY_ARTIFACT}"
    FSIM_BINARY_ARTIFACT_INDEX)
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_BINARY_LOG}"
    FSIM_BINARY_LOG_INDEX)
  math(EXPR FSIM_BINARY_MAPPING_DISTANCE
    "${FSIM_BINARY_LOG_INDEX} - ${FSIM_BINARY_ARTIFACT_INDEX}")
  if(FSIM_BINARY_MAPPING_DISTANCE LESS 0 OR
     FSIM_BINARY_MAPPING_DISTANCE GREATER 240)
    message(FATAL_ERROR
      "Windows archive/log mapping is not adjacent: "
      "${FSIM_BINARY_ARTIFACT}/${FSIM_BINARY_LOG}")
  endif()
endforeach()

list(LENGTH FSIM_WINDOWS_INSTALL_LOGS FSIM_WINDOWS_INSTALL_LOG_COUNT)
if(NOT FSIM_WINDOWS_INSTALL_LOG_COUNT EQUAL 2)
  message(FATAL_ERROR "Batch 177 Change 12 requires two Windows install lanes")
endif()
foreach(FSIM_WINDOWS_INSTALL_LOG IN LISTS FSIM_WINDOWS_INSTALL_LOGS)
  fsim_require_workflow_occurrences("${FSIM_WINDOWS_INSTALL_LOG}" 1)
endforeach()

foreach(FSIM_HOSTED_FRAGMENT IN ITEMS
    "configuration: Debug\n            preset: ci-linux\n            artifact: github-linux-gcc-debug"
    "configuration: Release\n            preset: ci-linux-release\n            artifact: github-linux-gcc-release"
    "configuration: Debug\n            build_dir: build/ci-linux-llvm22-debug\n            artifact: github-linux-llvm22-debug"
    "configuration: Release\n            build_dir: build/ci-linux-llvm22-release\n            artifact: github-linux-llvm22-release"
    "FSIM_ARTIFACT: github-linux-fuzz\n      FSIM_RETAINED_LOG: build/qualification/batch177-change20-hosted-linux-fuzz.log"
    "configuration: Debug\n            llvm_mode: 'OFF'\n            build_dir: build/ci-windows-llvm-mingw-Debug-llvm-OFF\n            artifact: github-windows-debug-llvm-off"
    "configuration: Debug\n            llvm_mode: 'ON'\n            build_dir: build/ci-windows-llvm-mingw-Debug-llvm-ON\n            artifact: github-windows-debug-llvm-on"
    "configuration: Release\n            llvm_mode: 'OFF'\n            build_dir: build/ci-windows-llvm-mingw-Release-llvm-OFF\n            artifact: github-windows-release-llvm-off"
    "configuration: Release\n            llvm_mode: 'ON'\n            build_dir: build/ci-windows-llvm-mingw-Release-llvm-ON\n            artifact: github-windows-release-llvm-on")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_HOSTED_FRAGMENT}"
    FSIM_FRAGMENT_INDEX)
  if(FSIM_FRAGMENT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "hosted lane configuration/artifact mapping drifted: ${FSIM_HOSTED_FRAGMENT}")
  endif()
endforeach()

foreach(FSIM_WINDOWS_ARCHIVE_FRAGMENT IN ITEMS
    "binary_package: fsim-v2.0.0-windows-x86_64-llvm-mingw-no-llvm\n            binary_archive: fsim-v2.0.0-windows-x86_64-llvm-mingw-no-llvm.zip\n            binary_log: build/qualification/batch177-change20-windows-no-llvm-archive.log\n            install_log: build/qualification/batch177-change20-windows-no-llvm-install.log"
    "binary_package: fsim-v2.0.0-windows-x86_64-llvm-mingw-llvm22\n            binary_archive: fsim-v2.0.0-windows-x86_64-llvm-mingw-llvm22.zip\n            binary_log: build/qualification/batch177-change20-windows-llvm22-archive.log\n            install_log: build/qualification/batch177-change20-windows-llvm22-install.log")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_WINDOWS_ARCHIVE_FRAGMENT}"
    FSIM_WINDOWS_ARCHIVE_INDEX)
  if(FSIM_WINDOWS_ARCHIVE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Windows archive package/artifact/log mapping drifted: "
      "${FSIM_WINDOWS_ARCHIVE_FRAGMENT}")
  endif()
endforeach()

fsim_require_workflow_occurrences("actions/upload-artifact@v7" 5)
fsim_require_workflow_occurrences("if: always()" 5)
fsim_require_workflow_occurrences("if-no-files-found: error" 5)
fsim_require_workflow_occurrences("Initialize retained lane log" 4)
fsim_require_workflow_occurrences("Upload retained lane log" 4)
fsim_require_workflow_occurrences(
  "Create deterministic Windows binary archive" 1)
fsim_require_workflow_occurrences(
  "Upload deterministic Windows binary archive" 1)
fsim_require_workflow_occurrences(
  "Audit deterministic Windows install lane" 1)
fsim_require_workflow_occurrences("timeout-minutes: 120" 4)
foreach(FSIM_HOSTED_COMMAND IN ITEMS
    "cmake --preset \"\${{ matrix.preset }}\""
    "cmake --build --preset \"\${{ matrix.preset }}\" --parallel 4"
    "ctest --preset \"\${{ matrix.preset }}\" -LE '^recursive-closure$'"
    "ctest --preset \"\${{ matrix.preset }}\" -L '^recursive-closure$'"
    "-DLLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm"
    "cmake --preset ci-fuzz"
    "-runs=20000"
    "\$buildDirectory = \"\${{ matrix.build_dir }}\""
    "-DFSIM_LLVM_MODE=\${{ matrix.llvm_mode }}"
    "ctest --test-dir \"\${{ matrix.build_dir }}\" --parallel 4 --progress --output-on-failure -LE '^recursive-closure$'"
    "tee -a \"\${{ matrix.retained_log }}\""
    "Tee-Object -FilePath \"\${{ matrix.retained_log }}\" -Append")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_HOSTED_COMMAND}"
    FSIM_COMMAND_INDEX)
  if(FSIM_COMMAND_INDEX EQUAL -1)
    message(FATAL_ERROR
      "hosted lane command/log policy drifted: ${FSIM_HOSTED_COMMAND}")
  endif()
endforeach()
foreach(FSIM_WINDOWS_ARCHIVE_COMMAND IN ITEMS
    "-DFSIM_BINARY_ONLY=ON"
    "-DFSIM_BINARY_PACKAGE_NAME=\${{ matrix.binary_package }}"
    "-DFSIM_BINARY_ARCHIVE_OUTPUT=\$env:GITHUB_WORKSPACE/\${{ matrix.binary_archive }}"
    "cmake/CheckDeterministicPackaging.cmake"
    "-DFSIM_CHANGE12_LANE=ON"
    "-DFSIM_CHANGE12_REGRESSION_LOG=\$env:GITHUB_WORKSPACE/\${{ matrix.retained_log }}"
    "-DFSIM_CHANGE12_ARCHIVE=\$env:GITHUB_WORKSPACE/\${{ matrix.binary_archive }}"
    "-DFSIM_CHANGE12_ARCHIVE_ROOT=\${{ matrix.binary_package }}"
    "-DFSIM_CHANGE12_EXPECTED_TESTS=\${{ matrix.expected_tests }}"
    "-DFSIM_CHANGE12_EXECUTABLE_SUFFIX=.exe"
    "name: \${{ matrix.binary_archive }}"
    "\${{ matrix.binary_log }}"
    "\${{ matrix.install_log }}")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_WINDOWS_ARCHIVE_COMMAND}"
    FSIM_WINDOWS_ARCHIVE_COMMAND_INDEX)
  if(FSIM_WINDOWS_ARCHIVE_COMMAND_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Windows archive command/upload policy drifted: "
      "${FSIM_WINDOWS_ARCHIVE_COMMAND}")
  endif()
endforeach()
foreach(FSIM_BATCH177_TOKEN IN ITEMS
    all-release-builds-tests-gates final-archives final-install-smokes
    sanitizers linux-windows-hosted-ci version-tag signature-disposition)
  string(FIND "${FSIM_RECORD_batch177_required}"
    "${FSIM_BATCH177_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "Batch 177 release record omits ${FSIM_BATCH177_TOKEN}")
  endif()
endforeach()

function(fsim_require_record_digest record_key relative_path)
  file(READ "${FSIM_SOURCE_DIR}/${relative_path}" FSIM_ACTUAL_CONTENTS)
  string(REPLACE "\r\n" "\n" FSIM_ACTUAL_CONTENTS
    "${FSIM_ACTUAL_CONTENTS}")
  string(REPLACE "\r" "\n" FSIM_ACTUAL_CONTENTS
    "${FSIM_ACTUAL_CONTENTS}")
  string(SHA256 FSIM_ACTUAL_SHA256 "${FSIM_ACTUAL_CONTENTS}")
  if(NOT FSIM_ACTUAL_SHA256 STREQUAL "${FSIM_RECORD_${record_key}}")
    message(FATAL_ERROR
      "v2 release digest drifted for ${relative_path}: "
      "recorded=${FSIM_RECORD_${record_key}} actual=${FSIM_ACTUAL_SHA256}")
  endif()
endfunction()
if(NOT EXISTS "${FSIM_SOURCE_DIR}/docs/implementation_plan_v3.md")
  fsim_require_record_digest(source_manifest_sha256
    packaging/source-package-manifest.txt)
endif()
fsim_require_record_digest(release_notes_sha256 docs/changelog-v2.md)
fsim_require_record_digest(known_issues_sha256 docs/known-issues-v2.md)
fsim_require_record_digest(release_guide_sha256 docs/release-and-post-v2.md)
fsim_require_record_digest(package_policy_sha256 packaging/package-policy.txt)
fsim_require_record_digest(linux_clang22_llvm22_target_sha256
  packaging/targets/linux-clang22-llvm22.txt)
fsim_require_record_digest(linux_gcc13_llvm22_target_sha256
  packaging/targets/linux-gcc13-llvm22.txt)
fsim_require_record_digest(linux_gcc13_no_llvm_target_sha256
  packaging/targets/linux-gcc13-no-llvm.txt)
fsim_require_record_digest(windows_llvm_mingw_llvm22_target_sha256
  packaging/targets/windows-llvm-mingw-llvm22.txt)
fsim_require_record_digest(windows_llvm_mingw_no_llvm_target_sha256
  packaging/targets/windows-llvm-mingw-no-llvm.txt)
fsim_require_record_digest(support_matrix_sha256 packaging/v2-support-matrix.tsv)
fsim_require_record_digest(example_output_sha256
  packaging/example-output-freeze.tsv)
fsim_require_record_digest(qualification_sha256
  tests/feature_matrix/v2_qualification_inventory.tsv)
fsim_require_record_digest(performance_sha256
  tests/feature_matrix/v2_performance_baselines.tsv)
fsim_require_record_digest(systemc_sbom_sha256
  third_party/systemc-3.0.2/systemc-3.0.2.spdx.json)
fsim_require_record_digest(scv_sbom_sha256
  third_party/scv-2.0.1/scv-2.0.1.spdx.json)
fsim_require_record_digest(root_license_sha256 LICENSE)
fsim_require_record_digest(systemc_license_sha256 third_party/systemc-3.0.2/LICENSE)
fsim_require_record_digest(systemc_notice_sha256 third_party/systemc-3.0.2/NOTICE)
fsim_require_record_digest(scv_license_sha256 third_party/scv-2.0.1/LICENSE)
fsim_require_record_digest(scv_notice_sha256 third_party/scv-2.0.1/NOTICE)

if(NOT EXISTS "${FSIM_SOURCE_DIR}/docs/implementation_plan_v3.md")
  file(STRINGS "${FSIM_SOURCE_DIR}/packaging/source-package-manifest.txt"
    FSIM_SOURCE_LINES ENCODING UTF-8)
  set(FSIM_SOURCE_COUNT 0)
  foreach(FSIM_LINE IN LISTS FSIM_SOURCE_LINES)
    if(NOT FSIM_LINE STREQUAL "" AND NOT FSIM_LINE MATCHES "^#")
      math(EXPR FSIM_SOURCE_COUNT "${FSIM_SOURCE_COUNT} + 1")
    endif()
  endforeach()
  if(NOT FSIM_SOURCE_COUNT EQUAL FSIM_RECORD_source_manifest_entries)
    message(FATAL_ERROR
      "v2 release source count drifted: ${FSIM_SOURCE_COUNT}")
  endif()
endif()

file(STRINGS "${FSIM_SUPPORT}" FSIM_SUPPORT_LINES ENCODING UTF-8)
set(FSIM_SUPPORT_COUNT 0)
set(FSIM_SUPPORT_TARGETS)
foreach(FSIM_LINE IN LISTS FSIM_SUPPORT_LINES)
  if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#" OR
     FSIM_LINE MATCHES "^target")
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 4)
    message(FATAL_ERROR "support-matrix row is malformed: ${FSIM_LINE}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_TARGET)
  list(GET FSIM_FIELDS 1 FSIM_STATUS)
  list(GET FSIM_FIELDS 2 FSIM_EVIDENCE)
  list(GET FSIM_FIELDS 3 FSIM_REQUIRED)
  if(FSIM_TARGET MATCHES "windows.*llvm-mingw")
    if(NOT FSIM_STATUS STREQUAL "definition-only" OR
       NOT FSIM_EVIDENCE STREQUAL "static-portability-only" OR
       NOT FSIM_REQUIRED MATCHES "hosted-disposition")
      message(FATAL_ERROR "Windows support row invents evidence: ${FSIM_LINE}")
    endif()
  elseif(FSIM_TARGET MATCHES "windows.*(msvc|clang-cl)")
    if(NOT FSIM_STATUS STREQUAL "retired-package-target" OR
       NOT FSIM_REQUIRED STREQUAL "none")
      message(FATAL_ERROR "retired Windows package row drifted: ${FSIM_LINE}")
    endif()
  endif()
  list(APPEND FSIM_SUPPORT_TARGETS "${FSIM_TARGET}")
  math(EXPR FSIM_SUPPORT_COUNT "${FSIM_SUPPORT_COUNT} + 1")
endforeach()
list(REMOVE_DUPLICATES FSIM_SUPPORT_TARGETS)
list(LENGTH FSIM_SUPPORT_TARGETS FSIM_SUPPORT_UNIQUE_COUNT)
if(NOT FSIM_SUPPORT_COUNT EQUAL 7 OR NOT FSIM_SUPPORT_UNIQUE_COUNT EQUAL 7)
  message(FATAL_ERROR "support matrix requires seven unique rows")
endif()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(STRINGS "${FSIM_EXAMPLES}" FSIM_EXAMPLE_LINES ENCODING UTF-8)
set(FSIM_EXAMPLE_COUNT 0)
foreach(FSIM_LINE IN LISTS FSIM_EXAMPLE_LINES)
  if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#" OR
     FSIM_LINE MATCHES "^example")
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "example-output row is malformed: ${FSIM_LINE}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_EXAMPLE)
  list(GET FSIM_FIELDS 1 FSIM_TOKEN)
  list(GET FSIM_FIELDS 2 FSIM_OWNER)
  set(FSIM_README "${FSIM_SOURCE_DIR}/examples/${FSIM_EXAMPLE}/README.md")
  if(NOT EXISTS "${FSIM_README}")
    message(FATAL_ERROR "frozen example is missing: ${FSIM_EXAMPLE}")
  endif()
  file(READ "${FSIM_README}" FSIM_README_CONTENTS)
  string(FIND "${FSIM_README_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_OWNER}" FSIM_OWNER_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1 OR FSIM_OWNER_INDEX EQUAL -1)
    message(FATAL_ERROR
      "frozen example token/owner drifted: ${FSIM_EXAMPLE}/${FSIM_OWNER}")
  endif()
  math(EXPR FSIM_EXAMPLE_COUNT "${FSIM_EXAMPLE_COUNT} + 1")
endforeach()
if(NOT FSIM_EXAMPLE_COUNT EQUAL 6)
  message(FATAL_ERROR "example-output freeze requires six rows")
endif()

file(READ "${FSIM_CHANGELOG}" FSIM_CHANGELOG_CONTENTS)
file(READ "${FSIM_KNOWN_ISSUES}" FSIM_KNOWN_CONTENTS)
file(READ "${FSIM_IMPLEMENTATION_PLAN}" FSIM_IMPLEMENTATION_PLAN_CONTENTS)
file(READ "${FSIM_ROOT_CMAKE}" FSIM_ROOT_CMAKE_CONTENTS)
file(READ "${FSIM_VERSION_HEADER}" FSIM_VERSION_HEADER_CONTENTS)
file(READ "${FSIM_PC_TEMPLATE}" FSIM_PC_TEMPLATE_CONTENTS)
file(READ "${FSIM_CLI_DRIVER}" FSIM_CLI_DRIVER_CONTENTS)
file(READ "${FSIM_TCL_APPLICATION}" FSIM_TCL_APPLICATION_CONTENTS)
foreach(FSIM_VERSION_TOKEN IN ITEMS
    "VERSION 2.0.0"
    "version = \"2.0.0\""
    "Version: @PROJECT_VERSION@"
    "output << \"fsim \" << fsim::version"
    "std::string(fsim::version) + \" (C API \"")
  set(FSIM_VERSION_TEXT
    "${FSIM_ROOT_CMAKE_CONTENTS}\n${FSIM_VERSION_HEADER_CONTENTS}\n${FSIM_PC_TEMPLATE_CONTENTS}\n${FSIM_CLI_DRIVER_CONTENTS}\n${FSIM_TCL_APPLICATION_CONTENTS}")
  string(FIND "${FSIM_VERSION_TEXT}" "${FSIM_VERSION_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "v2.0.0 version seam drifted: ${FSIM_VERSION_TOKEN}")
  endif()
endforeach()
foreach(FSIM_DOC_TOKEN IN ITEMS
    "fsim 2.0.0 is the current-only"
    "former fsim facade/custom kernel remains removed"
    "Source and binary contents now have explicit manifests")
  string(FIND "${FSIM_CHANGELOG_CONTENTS}" "${FSIM_DOC_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "v2 changelog lost release boundary: ${FSIM_DOC_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TAG_TOKEN IN ITEMS
    "# fsim v2.0.0 release notes"
    "## Annotated tag message"
    "fsim v2.0.0"
    "Signature disposition: unsigned-release.")
  string(FIND "${FSIM_CHANGELOG_CONTENTS}" "${FSIM_TAG_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "v2 release notes lost tag token: ${FSIM_TAG_TOKEN}")
  endif()
endforeach()
foreach(FSIM_PRIORITY_TOKEN IN ITEMS
    "Completed in Batch 136"
    "Completed in Batch 137"
    "Completed in Batch 138"
    "Batches 139-143 complete"
    "Batches 144-146 complete"
    "Batches 147-165 complete"
    "Batches 166-167 complete"
    "Batches 168-170 complete"
    "Batch 171 complete"
    "Batch 172 complete"
    "Batch 173 complete"
    "Batches 174-177 complete through the final Change 20 gate")
  string(FIND "${FSIM_IMPLEMENTATION_PLAN_CONTENTS}" "${FSIM_PRIORITY_TOKEN}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "v2 roadmap priority is not closed: ${FSIM_PRIORITY_TOKEN}")
  endif()
endforeach()
set(FSIM_CURRENT_RELEASE_DOCS
  "${FSIM_CHANGELOG_CONTENTS}\n${FSIM_KNOWN_CONTENTS}")
foreach(FSIM_STALE_TOKEN IN ITEMS
    "0.1.0-dev"
    "current v2 release-candidate"
    "source tree and installed command still carry the development version")
  string(FIND "${FSIM_CURRENT_RELEASE_DOCS}" "${FSIM_STALE_TOKEN}" FSIM_INDEX)
  if(NOT FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "v2 release docs retain stale claim: ${FSIM_STALE_TOKEN}")
  endif()
endforeach()
foreach(FSIM_DOC_TOKEN IN ITEMS
    "unsigned-release"
    "post-push rows are green"
    "as package targets"
    "one deterministic scheduler/time domain")
  string(FIND "${FSIM_KNOWN_CONTENTS}" "${FSIM_DOC_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "v2 known issues lost boundary: ${FSIM_DOC_TOKEN}")
  endif()
endforeach()

message(STATUS
  "v2.0.0 release records: ${FSIM_SOURCE_COUNT} source entries, 7 support rows, 6 examples, 19 qualification rows, 44 Linux performance rows, 30 frozen Batch 177 execution rows, unsigned release; post-push Windows evidence remains platform-specific")

if(FSIM_CHANGE12_LANE)
  foreach(FSIM_REQUIRED IN ITEMS
      FSIM_CHANGE12_BINARY_DIR FSIM_CHANGE12_ARCHIVE
      FSIM_CHANGE12_ARCHIVE_ROOT FSIM_CHANGE12_REGRESSION_LOG
      FSIM_CHANGE12_WORK_DIR FSIM_CHANGE12_EXPECTED_TESTS
      FSIM_CHANGE12_TOOLCHAIN)
    if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
      message(FATAL_ERROR "${FSIM_REQUIRED} is required for a Change 12 lane")
    endif()
  endforeach()
  if(NOT EXISTS "${FSIM_CHANGE12_ARCHIVE}" OR
     NOT EXISTS "${FSIM_CHANGE12_REGRESSION_LOG}")
    message(FATAL_ERROR "Change 12 archive or regression evidence is missing")
  endif()
  if(NOT FSIM_CHANGE12_EXPECTED_TESTS MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Change 12 expected-test count is not numeric")
  endif()

  file(READ "${FSIM_CHANGE12_REGRESSION_LOG}" FSIM_CHANGE12_LOG)
  string(FIND "${FSIM_CHANGE12_LOG}"
    "100% tests passed, 0 tests failed out of ${FSIM_CHANGE12_EXPECTED_TESTS}"
    FSIM_CHANGE12_PASS_INDEX)
  if(FSIM_CHANGE12_PASS_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Change 12 regression log lacks its exact green inventory")
  endif()
  set(FSIM_CHANGE12_WITNESSES
      fsim.binary-install-ownership
      fsim.installed-public-contract
      fsim.installed-pkg-config-consumer
      fsim.scv.installed_consumer
      fsim.abi-schema-inventory
      fsim.systemc.abi-freeze
      fsim.foreign-abi-freeze
      fsim.portable-object-schema-freeze
      fsim.design-library-schema-freeze
      fsim.incremental-native-cache-freeze
      fsim.native-producer-policy
      fsim.schema-producer-diagnostics
      fsim.abi-schema-reference
      fsim.abi-schema-evidence-matrix
      fsim.nested-portable-schema-freeze
      fsim.project-manifest-schema-freeze
      fsim.portable-stale-schema-policy
      fsim.artifact.object
      fsim.artifact.design
      fsim.library.artifact
      fsim.cache
      fsim.application.artifact_phases
      fsim.non-project-restartability-contract
      fsim.cache-corruption-isolation-contract
      fsim.source-hidden-relocation-contract
      fsim.systemc.compiler
      fsim.systemc.incremental
      fsim.scv.plugin_compiler
      fsim.scv.artifact
      fsim.application
      fsim.sdf-application-inventory
      fsim.sdf-vital-inventory
      fsim.application.sdf_control
      fsim.application.sdf_vital_corpus
      fsim.application.systemc_matrix
      fsim.application.trace_control
      fsim.application.fst_corpus
      fsim.application.vcd_control
      fsim.tool-portability-contract)
  foreach(FSIM_CHANGE12_WITNESS IN LISTS FSIM_CHANGE12_WITNESSES)
    string(REPLACE "." "\\." FSIM_CHANGE12_WITNESS_REGEX
      "${FSIM_CHANGE12_WITNESS}")
    string(REGEX MATCH
      "Test[^\n]*${FSIM_CHANGE12_WITNESS_REGEX}[^\n]*Passed"
      FSIM_CHANGE12_WITNESS_PASS "${FSIM_CHANGE12_LOG}")
    if(FSIM_CHANGE12_WITNESS_PASS STREQUAL "")
      message(FATAL_ERROR
        "Change 12 regression lacks passed witness: ${FSIM_CHANGE12_WITNESS}")
    endif()
  endforeach()

  if(NOT CMAKE_CTEST_COMMAND)
    find_program(CMAKE_CTEST_COMMAND NAMES ctest REQUIRED)
  endif()
  execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${FSIM_CHANGE12_BINARY_DIR}"
      --show-only=json-v1
    RESULT_VARIABLE FSIM_CHANGE12_CTEST_RESULT
    OUTPUT_VARIABLE FSIM_CHANGE12_CTEST_JSON
    ERROR_VARIABLE FSIM_CHANGE12_CTEST_ERROR
    TIMEOUT 120)
  if(NOT FSIM_CHANGE12_CTEST_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Change 12 cannot inspect generated CTest inventory: "
      "${FSIM_CHANGE12_CTEST_ERROR}")
  endif()
  foreach(FSIM_CHANGE12_WITNESS IN LISTS FSIM_CHANGE12_WITNESSES)
    string(FIND "${FSIM_CHANGE12_CTEST_JSON}"
      "\"${FSIM_CHANGE12_WITNESS}\"" FSIM_CHANGE12_CTEST_INDEX)
    if(FSIM_CHANGE12_CTEST_INDEX EQUAL -1)
      message(FATAL_ERROR
        "Change 12 generated CTest inventory lost ${FSIM_CHANGE12_WITNESS}")
    endif()
  endforeach()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${FSIM_CHANGE12_ARCHIVE}"
    RESULT_VARIABLE FSIM_CHANGE12_LIST_RESULT
    OUTPUT_VARIABLE FSIM_CHANGE12_LIST_OUTPUT
    ERROR_VARIABLE FSIM_CHANGE12_LIST_ERROR)
  if(NOT FSIM_CHANGE12_LIST_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Change 12 cannot list archive: ${FSIM_CHANGE12_LIST_ERROR}")
  endif()
  string(REPLACE "\r\n" "\n" FSIM_CHANGE12_LIST_OUTPUT
    "${FSIM_CHANGE12_LIST_OUTPUT}")
  string(REGEX REPLACE "\n$" "" FSIM_CHANGE12_LIST_OUTPUT
    "${FSIM_CHANGE12_LIST_OUTPUT}")
  string(REPLACE "\n" ";" FSIM_CHANGE12_ENTRIES
    "${FSIM_CHANGE12_LIST_OUTPUT}")
  list(LENGTH FSIM_CHANGE12_ENTRIES FSIM_CHANGE12_ENTRY_COUNT)
  if(NOT FSIM_CHANGE12_ENTRY_COUNT EQUAL 542)
    message(FATAL_ERROR
      "Change 12 archive requires 542 entries, found ${FSIM_CHANGE12_ENTRY_COUNT}")
  endif()
  string(REPLACE "." "\\." FSIM_CHANGE12_ROOT_REGEX
    "${FSIM_CHANGE12_ARCHIVE_ROOT}")
  foreach(FSIM_CHANGE12_ENTRY IN LISTS FSIM_CHANGE12_ENTRIES)
    if(NOT FSIM_CHANGE12_ENTRY MATCHES
       "^${FSIM_CHANGE12_ROOT_REGEX}/[^/].*$")
      message(FATAL_ERROR
        "Change 12 archive entry escapes its exact root: ${FSIM_CHANGE12_ENTRY}")
    endif()
  endforeach()

  file(REMOVE_RECURSE "${FSIM_CHANGE12_WORK_DIR}")
  file(MAKE_DIRECTORY "${FSIM_CHANGE12_WORK_DIR}/extract")
  set(FSIM_CHANGE12_SENTINEL
    "${FSIM_CHANGE12_WORK_DIR}/outside-archive.sentinel")
  file(WRITE "${FSIM_CHANGE12_SENTINEL}" "outside archive\n")
  file(ARCHIVE_EXTRACT INPUT "${FSIM_CHANGE12_ARCHIVE}"
    DESTINATION "${FSIM_CHANGE12_WORK_DIR}/extract")
  set(FSIM_CHANGE12_PREFIX
    "${FSIM_CHANGE12_WORK_DIR}/extract/${FSIM_CHANGE12_ARCHIVE_ROOT}")
  if(DEFINED FSIM_CHANGE12_EXECUTABLE_SUFFIX)
    set(FSIM_CHANGE12_SUFFIX "${FSIM_CHANGE12_EXECUTABLE_SUFFIX}")
  else()
    set(FSIM_CHANGE12_SUFFIX "")
  endif()
  foreach(FSIM_CHANGE12_PATH IN ITEMS
      "bin/fsim${FSIM_CHANGE12_SUFFIX}"
      "bin/fsim-sv${FSIM_CHANGE12_SUFFIX}"
      "bin/fsim-vhdl${FSIM_CHANGE12_SUFFIX}"
      "include/fsim/api.h"
      "include/fsim/version.hpp"
      "lib/cmake/fsim/fsimConfig.cmake"
      "lib/cmake/SystemCLanguage/SystemCLanguageConfig.cmake"
      "lib/cmake/SCV/SCVConfig.cmake"
      "lib/pkgconfig/fsim.pc"
      "share/fsim/examples/vertical_slice/README.md")
    if(NOT EXISTS "${FSIM_CHANGE12_PREFIX}/${FSIM_CHANGE12_PATH}")
      message(FATAL_ERROR
        "Change 12 installed archive omits ${FSIM_CHANGE12_PATH}")
    endif()
  endforeach()
  foreach(FSIM_CHANGE12_METADATA IN ITEMS
      "lib/pkgconfig/fsim.pc"
      "lib/cmake/fsim/fsimConfig.cmake"
      "lib/cmake/SystemCLanguage/SystemCLanguageConfig.cmake"
      "lib/cmake/SCV/SCVConfig.cmake")
    file(READ "${FSIM_CHANGE12_PREFIX}/${FSIM_CHANGE12_METADATA}"
      FSIM_CHANGE12_METADATA_TEXT)
    foreach(FSIM_CHANGE12_LEAK IN ITEMS
        "${FSIM_SOURCE_DIR}" "${FSIM_CHANGE12_BINARY_DIR}" "/usr/local")
      string(FIND "${FSIM_CHANGE12_METADATA_TEXT}" "${FSIM_CHANGE12_LEAK}"
        FSIM_CHANGE12_LEAK_INDEX)
      if(NOT FSIM_CHANGE12_LEAK_INDEX EQUAL -1)
        message(FATAL_ERROR
          "Change 12 metadata leaks ${FSIM_CHANGE12_LEAK}: "
          "${FSIM_CHANGE12_METADATA}")
      endif()
    endforeach()
  endforeach()

  set(FSIM_CHANGE12_EXAMPLES
      non_project_phases precompiled_library sdf_annotation sdf_vital_mixed
      three_language_hierarchy vertical_slice)
  file(GLOB FSIM_CHANGE12_EXAMPLE_ENTRIES RELATIVE
    "${FSIM_CHANGE12_PREFIX}/share/fsim/examples"
    "${FSIM_CHANGE12_PREFIX}/share/fsim/examples/*")
  set(FSIM_CHANGE12_ACTUAL_EXAMPLES)
  foreach(FSIM_CHANGE12_EXAMPLE IN LISTS FSIM_CHANGE12_EXAMPLE_ENTRIES)
    if(IS_DIRECTORY
       "${FSIM_CHANGE12_PREFIX}/share/fsim/examples/${FSIM_CHANGE12_EXAMPLE}")
      list(APPEND FSIM_CHANGE12_ACTUAL_EXAMPLES "${FSIM_CHANGE12_EXAMPLE}")
    endif()
  endforeach()
  list(SORT FSIM_CHANGE12_ACTUAL_EXAMPLES)
  if(NOT FSIM_CHANGE12_ACTUAL_EXAMPLES STREQUAL FSIM_CHANGE12_EXAMPLES)
    message(FATAL_ERROR
      "Change 12 installed example inventory drifted: "
      "${FSIM_CHANGE12_ACTUAL_EXAMPLES}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "http_proxy=http://127.0.0.1:9" "https_proxy=http://127.0.0.1:9"
      "NO_PROXY=*"
      "${FSIM_CHANGE12_PREFIX}/bin/fsim${FSIM_CHANGE12_SUFFIX}" --version
    RESULT_VARIABLE FSIM_CHANGE12_VERSION_RESULT
    OUTPUT_VARIABLE FSIM_CHANGE12_VERSION_OUTPUT
    ERROR_VARIABLE FSIM_CHANGE12_VERSION_ERROR)
  if(NOT FSIM_CHANGE12_VERSION_RESULT EQUAL 0 OR
     NOT FSIM_CHANGE12_VERSION_OUTPUT MATCHES
       "^fsim 2\\.0\\.0 \\(C API 1\\)")
    message(FATAL_ERROR
      "Change 12 installed version failed: "
      "${FSIM_CHANGE12_VERSION_OUTPUT}${FSIM_CHANGE12_VERSION_ERROR}")
  endif()
  foreach(FSIM_CHANGE12_ALIAS IN ITEMS fsim-sv fsim-vhdl)
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env
        "http_proxy=http://127.0.0.1:9" "https_proxy=http://127.0.0.1:9"
        "NO_PROXY=*"
        "${FSIM_CHANGE12_PREFIX}/bin/${FSIM_CHANGE12_ALIAS}${FSIM_CHANGE12_SUFFIX}"
        --help
      RESULT_VARIABLE FSIM_CHANGE12_ALIAS_RESULT
      OUTPUT_VARIABLE FSIM_CHANGE12_ALIAS_OUTPUT
      ERROR_VARIABLE FSIM_CHANGE12_ALIAS_ERROR)
    if(NOT FSIM_CHANGE12_ALIAS_RESULT EQUAL 0 OR
       NOT FSIM_CHANGE12_ALIAS_OUTPUT MATCHES "^Usage: ${FSIM_CHANGE12_ALIAS}")
      message(FATAL_ERROR
        "Change 12 installed alias failed: ${FSIM_CHANGE12_ALIAS}\n"
        "${FSIM_CHANGE12_ALIAS_OUTPUT}${FSIM_CHANGE12_ALIAS_ERROR}")
    endif()
  endforeach()

  file(SHA256 "${FSIM_CHANGE12_ARCHIVE}" FSIM_CHANGE12_ARCHIVE_SHA256)
  file(SHA256 "${FSIM_CHANGE12_REGRESSION_LOG}"
    FSIM_CHANGE12_REGRESSION_SHA256)
  file(SIZE "${FSIM_CHANGE12_ARCHIVE}" FSIM_CHANGE12_ARCHIVE_BYTES)
  list(LENGTH FSIM_CHANGE12_WITNESSES FSIM_CHANGE12_WITNESS_COUNT)
  file(WRITE "${FSIM_CHANGE12_WORK_DIR}/result.tsv"
    "toolchain\tarchive_entries\tarchive_bytes\tarchive_sha256\tregression_tests\tregression_sha256\twitnesses\tstatus\n"
    "${FSIM_CHANGE12_TOOLCHAIN}\t${FSIM_CHANGE12_ENTRY_COUNT}\t${FSIM_CHANGE12_ARCHIVE_BYTES}\t${FSIM_CHANGE12_ARCHIVE_SHA256}\t${FSIM_CHANGE12_EXPECTED_TESTS}\t${FSIM_CHANGE12_REGRESSION_SHA256}\t${FSIM_CHANGE12_WITNESS_COUNT}\tPASS\n")

  file(REMOVE_RECURSE "${FSIM_CHANGE12_PREFIX}")
  if(EXISTS "${FSIM_CHANGE12_PREFIX}" OR
     NOT EXISTS "${FSIM_CHANGE12_SENTINEL}")
    message(FATAL_ERROR "Change 12 isolated archive uninstall failed")
  endif()
  message(STATUS
    "Change 12 install reproducibility: ${FSIM_CHANGE12_TOOLCHAIN}, "
    "${FSIM_CHANGE12_ENTRY_COUNT} archive entries, "
    "${FSIM_CHANGE12_EXPECTED_TESTS} regression tests, "
    "${FSIM_CHANGE12_WITNESS_COUNT} exact witnesses, offline aliases and "
    "isolated uninstall passed")
endif()

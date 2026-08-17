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
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_RECORD}" "${FSIM_SUPPORT}" "${FSIM_EXAMPLES}"
    "${FSIM_CHANGELOG}" "${FSIM_KNOWN_ISSUES}" "${FSIM_TEST_CMAKE}")
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
    schema candidate compiled_version source_manifest_entries
    source_manifest_sha256 source_exclusions package_policy_sha256
    linux_clang22_llvm22_target_sha256 linux_gcc13_llvm22_target_sha256
    linux_gcc13_no_llvm_target_sha256
    windows_llvm_mingw_llvm22_target_sha256
    windows_llvm_mingw_no_llvm_target_sha256 support_matrix_rows
    support_matrix_sha256 example_rows example_output_sha256
    qualification_rows qualification_sha256 performance_rows
    performance_sha256 candidate_logs candidate_log_audit_sha256
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
   NOT FSIM_RECORD_candidate STREQUAL "v2.0-batch176" OR
   NOT FSIM_RECORD_compiled_version STREQUAL "0.1.0-dev" OR
   NOT FSIM_RECORD_source_exclusions STREQUAL "23" OR
   NOT FSIM_RECORD_support_matrix_rows STREQUAL "7" OR
   NOT FSIM_RECORD_example_rows STREQUAL "6" OR
   NOT FSIM_RECORD_qualification_rows STREQUAL "19" OR
   NOT FSIM_RECORD_performance_rows STREQUAL "44" OR
   NOT FSIM_RECORD_candidate_logs STREQUAL "69" OR
   NOT FSIM_RECORD_signature STREQUAL "unsigned-release-candidate")
  message(FATAL_ERROR "v2 release record scalar policy drifted")
endif()
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
  file(SHA256 "${FSIM_SOURCE_DIR}/${relative_path}" FSIM_ACTUAL_SHA256)
  if(NOT FSIM_ACTUAL_SHA256 STREQUAL "${FSIM_RECORD_${record_key}}")
    message(FATAL_ERROR
      "v2 release digest drifted for ${relative_path}: "
      "recorded=${FSIM_RECORD_${record_key}} actual=${FSIM_ACTUAL_SHA256}")
  endif()
endfunction()
fsim_require_record_digest(source_manifest_sha256
  packaging/source-package-manifest.txt)
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
foreach(FSIM_DOC_TOKEN IN ITEMS
    "Batch 177 owns the final"
    "former fsim facade/custom kernel remains removed"
    "Source and binary contents now have explicit manifests")
  string(FIND "${FSIM_CHANGELOG_CONTENTS}" "${FSIM_DOC_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "v2 changelog lost release boundary: ${FSIM_DOC_TOKEN}")
  endif()
endforeach()
foreach(FSIM_DOC_TOKEN IN ITEMS
    "No Batch 176 Release build"
    "Candidate ZIPs are explicitly unsigned"
    "as package targets"
    "identity until Batch 177")
  string(FIND "${FSIM_KNOWN_CONTENTS}" "${FSIM_DOC_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "v2 known issues lost boundary: ${FSIM_DOC_TOKEN}")
  endif()
endforeach()

message(STATUS
  "v2 release records: ${FSIM_SOURCE_COUNT} source entries, 7 support rows, 6 examples, 19 qualification rows, 44 Linux performance rows, unsigned candidate; all Release/Windows execution remains Batch 177")

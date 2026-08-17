# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

foreach(FSIM_REQUIRED IN ITEMS FSIM_SOURCE_DIR FSIM_RELEASE_DIR FSIM_WORK_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

function(fsim_list_archive archive output_paths)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${archive}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  if(NOT FSIM_RESULT EQUAL 0)
    message(FATAL_ERROR "cannot list ${archive}: ${FSIM_ERROR}")
  endif()
  string(REPLACE "\r\n" "\n" FSIM_OUTPUT "${FSIM_OUTPUT}")
  string(REPLACE "\n" ";" FSIM_PATHS "${FSIM_OUTPUT}")
  list(FILTER FSIM_PATHS EXCLUDE REGEX "^$")
  set(${output_paths} "${FSIM_PATHS}" PARENT_SCOPE)
endfunction()

function(fsim_require_safe_archive root archive_paths)
  set(FSIM_SEEN)
  set(FSIM_PREVIOUS)
  foreach(FSIM_PATH IN LISTS archive_paths)
    string(LENGTH "${FSIM_PATH}" FSIM_PATH_LENGTH)
    string(FIND "${FSIM_PATH}" "${root}/" FSIM_ROOT_INDEX)
    if(FSIM_PATH STREQUAL ""
       OR NOT FSIM_ROOT_INDEX EQUAL 0
       OR FSIM_PATH_LENGTH GREATER 1024
       OR FSIM_PATH MATCHES "^/"
       OR FSIM_PATH MATCHES "^[A-Za-z]:"
       OR FSIM_PATH MATCHES "\\\\"
       OR FSIM_PATH MATCHES "(^|/)\\.\\.?(/|$)"
       OR FSIM_PATH MATCHES "//")
      message(FATAL_ERROR "unsafe release archive path: ${FSIM_PATH}")
    endif()
    list(FIND FSIM_SEEN "${FSIM_PATH}" FSIM_DUPLICATE_INDEX)
    if(NOT FSIM_DUPLICATE_INDEX EQUAL -1)
      message(FATAL_ERROR "duplicate release archive path: ${FSIM_PATH}")
    endif()
    if(NOT FSIM_PREVIOUS STREQUAL "" AND
       FSIM_PREVIOUS STRGREATER FSIM_PATH)
      message(FATAL_ERROR
        "release archive paths are not ordered: ${FSIM_PREVIOUS}/${FSIM_PATH}")
    endif()
    list(APPEND FSIM_SEEN "${FSIM_PATH}")
    set(FSIM_PREVIOUS "${FSIM_PATH}")
  endforeach()
endfunction()

function(fsim_require_same_file expected actual description)
  if(NOT EXISTS "${expected}" OR NOT EXISTS "${actual}")
    message(FATAL_ERROR "${description} is missing: ${expected}/${actual}")
  endif()
  file(SHA256 "${expected}" FSIM_EXPECTED_SHA256)
  file(SHA256 "${actual}" FSIM_ACTUAL_SHA256)
  if(NOT FSIM_ACTUAL_SHA256 STREQUAL FSIM_EXPECTED_SHA256)
    message(FATAL_ERROR
      "${description} digest differs: "
      "${FSIM_EXPECTED_SHA256}/${FSIM_ACTUAL_SHA256}")
  endif()
endfunction()

file(REMOVE_RECURSE "${FSIM_WORK_DIR}")
file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
    -P "${FSIM_SOURCE_DIR}/cmake/CheckV2ReleaseRecords.cmake"
  RESULT_VARIABLE FSIM_RECORD_RESULT
  OUTPUT_VARIABLE FSIM_RECORD_OUTPUT
  ERROR_VARIABLE FSIM_RECORD_ERROR)
if(NOT FSIM_RECORD_RESULT EQUAL 0)
  message(FATAL_ERROR
    "release record validation failed with ${FSIM_RECORD_RESULT}\n"
    "${FSIM_RECORD_OUTPUT}${FSIM_RECORD_ERROR}")
endif()

foreach(FSIM_SIGNATURE_INPUT IN ITEMS
    packaging/package-policy.txt
    packaging/v2-release-record.txt
    packaging/targets/linux-clang22-llvm22.txt
    packaging/targets/linux-gcc13-llvm22.txt
    packaging/targets/linux-gcc13-no-llvm.txt
    packaging/targets/windows-llvm-mingw-llvm22.txt
    packaging/targets/windows-llvm-mingw-no-llvm.txt)
  file(STRINGS "${FSIM_SOURCE_DIR}/${FSIM_SIGNATURE_INPUT}"
    FSIM_SIGNATURE_LINES ENCODING UTF-8)
  list(FIND FSIM_SIGNATURE_LINES
    "signature=unsigned-release" FSIM_SIGNATURE_INDEX)
  if(FSIM_SIGNATURE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "signature disposition drifted: ${FSIM_SIGNATURE_INPUT}")
  endif()
endforeach()

set(FSIM_SYSTEMC_ARCHIVE_SHA256
  "9b3693ed286aab958b9e5d79bb0ad3bc523bbc46931100553275352038f4a0c4")
set(FSIM_SCV_ARCHIVE_SHA256
  "7bd1c4037f3c108d02f45cae003d112efdb788d469cb029fada247d330ca4881")
foreach(FSIM_SBOM_SPEC IN ITEMS
    "third_party/systemc-3.0.2/systemc-3.0.2.spdx.json@@SystemC@@3.0.2@@${FSIM_SYSTEMC_ARCHIVE_SHA256}@@pkg:github/accellera-official/systemc@3.0.2"
    "third_party/scv-2.0.1/scv-2.0.1.spdx.json@@SCV@@2.0.1@@${FSIM_SCV_ARCHIVE_SHA256}@@pkg:generic/scv@2.0.1")
  string(REPLACE "@@" ";" FSIM_FIELDS "${FSIM_SBOM_SPEC}")
  list(GET FSIM_FIELDS 0 FSIM_SBOM_PATH)
  list(GET FSIM_FIELDS 1 FSIM_SBOM_NAME)
  list(GET FSIM_FIELDS 2 FSIM_SBOM_VERSION)
  list(GET FSIM_FIELDS 3 FSIM_SBOM_ARCHIVE_SHA256)
  list(GET FSIM_FIELDS 4 FSIM_SBOM_PURL)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_SBOM_PATH}" FSIM_SBOM_CONTENTS)
  foreach(FSIM_TOKEN IN ITEMS
      "\"spdxVersion\": \"SPDX-2.3\""
      "\"dataLicense\": \"CC0-1.0\""
      "\"name\": \"${FSIM_SBOM_NAME}\""
      "\"versionInfo\": \"${FSIM_SBOM_VERSION}\""
      "\"checksumValue\": \"${FSIM_SBOM_ARCHIVE_SHA256}\""
      "\"licenseDeclared\": \"Apache-2.0\""
      "${FSIM_SBOM_PURL}")
    string(FIND "${FSIM_SBOM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
    if(FSIM_TOKEN_INDEX EQUAL -1)
      message(FATAL_ERROR "SBOM ${FSIM_SBOM_PATH} omits ${FSIM_TOKEN}")
    endif()
  endforeach()
endforeach()

file(SHA256
  "${FSIM_SOURCE_DIR}/third_party/systemc-3.0.2/systemc-3.0.2.tar.gz"
  FSIM_ACTUAL_SYSTEMC_ARCHIVE_SHA256)
file(SHA256 "${FSIM_SOURCE_DIR}/third_party/scv-2.0.1/scv-2.0.1.tar.gz"
  FSIM_ACTUAL_SCV_ARCHIVE_SHA256)
if(NOT FSIM_ACTUAL_SYSTEMC_ARCHIVE_SHA256 STREQUAL
       FSIM_SYSTEMC_ARCHIVE_SHA256 OR
   NOT FSIM_ACTUAL_SCV_ARCHIVE_SHA256 STREQUAL FSIM_SCV_ARCHIVE_SHA256)
  message(FATAL_ERROR "upstream archive identity differs from the SBOM")
endif()

file(STRINGS "${FSIM_SOURCE_DIR}/third_party/scv-2.0.1/PATCHES.txt"
  FSIM_PATCH_LINES ENCODING UTF-8)
set(FSIM_PATCH_COUNT 0)
foreach(FSIM_LINE IN LISTS FSIM_PATCH_LINES)
  if(NOT FSIM_LINE MATCHES
     "^patch=([a-z0-9-]+)[|]([0-9a-f]+)[|].*$")
    continue()
  endif()
  set(FSIM_PATCH_NAME "${CMAKE_MATCH_1}")
  set(FSIM_PATCH_SHA256 "${CMAKE_MATCH_2}")
  string(LENGTH "${FSIM_PATCH_SHA256}" FSIM_PATCH_SHA256_LENGTH)
  if(NOT FSIM_PATCH_SHA256_LENGTH EQUAL 64)
    message(FATAL_ERROR
      "governed SCV patch digest is malformed: ${FSIM_PATCH_NAME}")
  endif()
  set(FSIM_PATCH_PATH
    "${FSIM_SOURCE_DIR}/third_party/scv-2.0.1/patches/${FSIM_PATCH_NAME}.patch")
  if(NOT EXISTS "${FSIM_PATCH_PATH}")
    message(FATAL_ERROR "governed SCV patch is missing: ${FSIM_PATCH_NAME}")
  endif()
  file(SHA256 "${FSIM_PATCH_PATH}" FSIM_ACTUAL_PATCH_SHA256)
  if(NOT FSIM_ACTUAL_PATCH_SHA256 STREQUAL FSIM_PATCH_SHA256)
    message(FATAL_ERROR "governed SCV patch digest drifted: ${FSIM_PATCH_NAME}")
  endif()
  math(EXPR FSIM_PATCH_COUNT "${FSIM_PATCH_COUNT} + 1")
endforeach()
if(NOT FSIM_PATCH_COUNT EQUAL 4)
  message(FATAL_ERROR "SCV supply-chain audit requires four governed patches")
endif()

set(FSIM_SOURCE_ROOT "fsim-v2.0.0-source")
set(FSIM_SOURCE_ARCHIVE "${FSIM_RELEASE_DIR}/${FSIM_SOURCE_ROOT}.zip")
if(NOT EXISTS "${FSIM_SOURCE_ARCHIVE}")
  message(FATAL_ERROR "final source archive is missing: ${FSIM_SOURCE_ARCHIVE}")
endif()
fsim_list_archive("${FSIM_SOURCE_ARCHIVE}" FSIM_SOURCE_ARCHIVE_PATHS)
fsim_require_safe_archive("${FSIM_SOURCE_ROOT}" "${FSIM_SOURCE_ARCHIVE_PATHS}")

file(STRINGS "${FSIM_SOURCE_DIR}/packaging/source-package-manifest.txt"
  FSIM_SOURCE_MANIFEST_LINES ENCODING UTF-8)
set(FSIM_SOURCE_FILES)
set(FSIM_EXPECTED_SOURCE_PATHS)
foreach(FSIM_LINE IN LISTS FSIM_SOURCE_MANIFEST_LINES)
  if(NOT FSIM_LINE STREQUAL "" AND NOT FSIM_LINE MATCHES "^#")
    list(APPEND FSIM_SOURCE_FILES "${FSIM_LINE}")
    list(APPEND FSIM_EXPECTED_SOURCE_PATHS "${FSIM_SOURCE_ROOT}/${FSIM_LINE}")
  endif()
endforeach()
list(SORT FSIM_EXPECTED_SOURCE_PATHS)
if(NOT FSIM_SOURCE_ARCHIVE_PATHS STREQUAL FSIM_EXPECTED_SOURCE_PATHS)
  message(FATAL_ERROR "source archive content differs from the checked manifest")
endif()

set(FSIM_SOURCE_EXTRACT "${FSIM_WORK_DIR}/source")
file(MAKE_DIRECTORY "${FSIM_SOURCE_EXTRACT}")
file(ARCHIVE_EXTRACT INPUT "${FSIM_SOURCE_ARCHIVE}"
  DESTINATION "${FSIM_SOURCE_EXTRACT}")
foreach(FSIM_RELATIVE IN LISTS FSIM_SOURCE_FILES)
  fsim_require_same_file(
    "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}"
    "${FSIM_SOURCE_EXTRACT}/${FSIM_SOURCE_ROOT}/${FSIM_RELATIVE}"
    "source archive entry ${FSIM_RELATIVE}")
endforeach()

set(FSIM_BINARY_ROOTS
  fsim-v2.0.0-linux-x86_64-clang22-llvm22
  fsim-v2.0.0-linux-x86_64-gcc13-llvm22
  fsim-v2.0.0-linux-x86_64-gcc13-no-llvm)
set(FSIM_REQUIRED_DOC_MAPPINGS
  "LICENSE@@share/doc/fsim/LICENSE"
  "third_party/systemc-3.0.2/LICENSE@@share/doc/fsim/third-party/systemc-3.0.2/LICENSE"
  "third_party/systemc-3.0.2/NOTICE@@share/doc/fsim/third-party/systemc-3.0.2/NOTICE"
  "third_party/systemc-3.0.2/SOURCE_MANIFEST.txt@@share/doc/fsim/third-party/systemc-3.0.2/SOURCE_MANIFEST.txt"
  "third_party/systemc-3.0.2/systemc-3.0.2.spdx.json@@share/doc/fsim/third-party/systemc-3.0.2/systemc-3.0.2.spdx.json"
  "third_party/scv-2.0.1/LICENSE@@share/doc/fsim/third-party/scv-2.0.1/LICENSE"
  "third_party/scv-2.0.1/NOTICE@@share/doc/fsim/third-party/scv-2.0.1/NOTICE"
  "third_party/scv-2.0.1/PATCHES.txt@@share/doc/fsim/third-party/scv-2.0.1/PATCHES.txt"
  "third_party/scv-2.0.1/SOURCE_MANIFEST.txt@@share/doc/fsim/third-party/scv-2.0.1/SOURCE_MANIFEST.txt"
  "third_party/scv-2.0.1/scv-2.0.1.spdx.json@@share/doc/fsim/third-party/scv-2.0.1/scv-2.0.1.spdx.json")

set(FSIM_ARCHIVE_RECORDS)
file(SHA256 "${FSIM_SOURCE_ARCHIVE}" FSIM_SOURCE_ARCHIVE_SHA256)
file(SIZE "${FSIM_SOURCE_ARCHIVE}" FSIM_SOURCE_ARCHIVE_SIZE)
list(LENGTH FSIM_SOURCE_ARCHIVE_PATHS FSIM_SOURCE_ARCHIVE_COUNT)
list(APPEND FSIM_ARCHIVE_RECORDS
  "source\t${FSIM_SOURCE_ARCHIVE_COUNT}\t${FSIM_SOURCE_ARCHIVE_SIZE}\t${FSIM_SOURCE_ARCHIVE_SHA256}")
set(FSIM_EXPECTED_DOCUMENT_PATHS)
set(FSIM_EXPECTED_DOCUMENT_DIGEST "")
foreach(FSIM_BINARY_ROOT IN LISTS FSIM_BINARY_ROOTS)
  set(FSIM_BINARY_ARCHIVE "${FSIM_RELEASE_DIR}/${FSIM_BINARY_ROOT}.zip")
  if(NOT EXISTS "${FSIM_BINARY_ARCHIVE}")
    message(FATAL_ERROR "Linux binary archive is missing: ${FSIM_BINARY_ARCHIVE}")
  endif()
  fsim_list_archive("${FSIM_BINARY_ARCHIVE}" FSIM_BINARY_ARCHIVE_PATHS)
  fsim_require_safe_archive("${FSIM_BINARY_ROOT}" "${FSIM_BINARY_ARCHIVE_PATHS}")
  list(LENGTH FSIM_BINARY_ARCHIVE_PATHS FSIM_BINARY_ARCHIVE_COUNT)
  if(NOT FSIM_BINARY_ARCHIVE_COUNT EQUAL 542)
    message(FATAL_ERROR
      "Linux binary archive entry count drifted: "
      "${FSIM_BINARY_ROOT}/${FSIM_BINARY_ARCHIVE_COUNT}")
  endif()
  set(FSIM_BINARY_EXTRACT "${FSIM_WORK_DIR}/${FSIM_BINARY_ROOT}")
  file(MAKE_DIRECTORY "${FSIM_BINARY_EXTRACT}")
  file(ARCHIVE_EXTRACT INPUT "${FSIM_BINARY_ARCHIVE}"
    DESTINATION "${FSIM_BINARY_EXTRACT}")
  set(FSIM_BINARY_PACKAGE_ROOT "${FSIM_BINARY_EXTRACT}/${FSIM_BINARY_ROOT}")
  foreach(FSIM_MAPPING IN LISTS FSIM_REQUIRED_DOC_MAPPINGS)
    string(REPLACE "@@" ";" FSIM_FIELDS "${FSIM_MAPPING}")
    list(GET FSIM_FIELDS 0 FSIM_SOURCE_RELATIVE)
    list(GET FSIM_FIELDS 1 FSIM_BINARY_RELATIVE)
    fsim_require_same_file(
      "${FSIM_SOURCE_DIR}/${FSIM_SOURCE_RELATIVE}"
      "${FSIM_BINARY_PACKAGE_ROOT}/${FSIM_BINARY_RELATIVE}"
      "binary supply-chain file ${FSIM_BINARY_ROOT}/${FSIM_BINARY_RELATIVE}")
  endforeach()
  set(FSIM_TCL_LICENSE
    "${FSIM_BINARY_PACKAGE_ROOT}/share/doc/fsim/third-party/tcl-license.terms")
  if(NOT EXISTS "${FSIM_TCL_LICENSE}")
    message(FATAL_ERROR "binary archive omits the Tcl license: ${FSIM_BINARY_ROOT}")
  endif()
  file(GLOB_RECURSE FSIM_DOCUMENT_PATHS LIST_DIRECTORIES FALSE
    RELATIVE "${FSIM_BINARY_PACKAGE_ROOT}"
    "${FSIM_BINARY_PACKAGE_ROOT}/share/doc/fsim/*")
  list(SORT FSIM_DOCUMENT_PATHS)
  set(FSIM_DOCUMENT_RECORD)
  foreach(FSIM_DOCUMENT_PATH IN LISTS FSIM_DOCUMENT_PATHS)
    file(SHA256 "${FSIM_BINARY_PACKAGE_ROOT}/${FSIM_DOCUMENT_PATH}"
      FSIM_DOCUMENT_SHA256)
    string(APPEND FSIM_DOCUMENT_RECORD
      "${FSIM_DOCUMENT_PATH}=${FSIM_DOCUMENT_SHA256}\n")
  endforeach()
  string(SHA256 FSIM_DOCUMENT_DIGEST "${FSIM_DOCUMENT_RECORD}")
  if(FSIM_EXPECTED_DOCUMENT_PATHS)
    if(NOT FSIM_DOCUMENT_PATHS STREQUAL FSIM_EXPECTED_DOCUMENT_PATHS OR
       NOT FSIM_DOCUMENT_DIGEST STREQUAL FSIM_EXPECTED_DOCUMENT_DIGEST)
      message(FATAL_ERROR
        "installed release documentation differs across Linux archives")
    endif()
  else()
    set(FSIM_EXPECTED_DOCUMENT_PATHS "${FSIM_DOCUMENT_PATHS}")
    set(FSIM_EXPECTED_DOCUMENT_DIGEST "${FSIM_DOCUMENT_DIGEST}")
  endif()
  file(SHA256 "${FSIM_BINARY_ARCHIVE}" FSIM_BINARY_ARCHIVE_SHA256)
  file(SIZE "${FSIM_BINARY_ARCHIVE}" FSIM_BINARY_ARCHIVE_SIZE)
  list(APPEND FSIM_ARCHIVE_RECORDS
    "binary\t${FSIM_BINARY_ARCHIVE_COUNT}\t${FSIM_BINARY_ARCHIVE_SIZE}\t${FSIM_BINARY_ARCHIVE_SHA256}")
endforeach()

file(GLOB FSIM_UNEXPECTED_SIGNATURES
  "${FSIM_RELEASE_DIR}/*.asc" "${FSIM_RELEASE_DIR}/*.sig")
if(FSIM_UNEXPECTED_SIGNATURES)
  message(FATAL_ERROR
    "unsigned release unexpectedly contains signature artifacts: "
    "${FSIM_UNEXPECTED_SIGNATURES}")
endif()

file(SHA256 "${FSIM_SOURCE_DIR}/LICENSE" FSIM_ROOT_LICENSE_SHA256)
file(SHA256 "${FSIM_SOURCE_DIR}/third_party/systemc-3.0.2/LICENSE"
  FSIM_SYSTEMC_LICENSE_SHA256)
file(SHA256 "${FSIM_SOURCE_DIR}/third_party/systemc-3.0.2/NOTICE"
  FSIM_SYSTEMC_NOTICE_SHA256)
file(SHA256 "${FSIM_SOURCE_DIR}/third_party/scv-2.0.1/LICENSE"
  FSIM_SCV_LICENSE_SHA256)
file(SHA256 "${FSIM_SOURCE_DIR}/third_party/scv-2.0.1/NOTICE"
  FSIM_SCV_NOTICE_SHA256)
file(SHA256 "${FSIM_SOURCE_DIR}/third_party/scv-2.0.1/PATCHES.txt"
  FSIM_SCV_PATCH_MANIFEST_SHA256)
set(FSIM_RESULT
  "kind\tentries\tbytes\tsha256\n")
foreach(FSIM_ARCHIVE_RECORD IN LISTS FSIM_ARCHIVE_RECORDS)
  string(APPEND FSIM_RESULT "${FSIM_ARCHIVE_RECORD}\n")
endforeach()
string(APPEND FSIM_RESULT
  "signature\tunsigned-release\n"
  "root_license_sha256\t${FSIM_ROOT_LICENSE_SHA256}\n"
  "systemc_archive_sha256\t${FSIM_SYSTEMC_ARCHIVE_SHA256}\n"
  "systemc_license_sha256\t${FSIM_SYSTEMC_LICENSE_SHA256}\n"
  "systemc_notice_sha256\t${FSIM_SYSTEMC_NOTICE_SHA256}\n"
  "scv_archive_sha256\t${FSIM_SCV_ARCHIVE_SHA256}\n"
  "scv_license_sha256\t${FSIM_SCV_LICENSE_SHA256}\n"
  "scv_notice_sha256\t${FSIM_SCV_NOTICE_SHA256}\n"
  "scv_patch_manifest_sha256\t${FSIM_SCV_PATCH_MANIFEST_SHA256}\n"
  "installed_documentation_sha256\t${FSIM_EXPECTED_DOCUMENT_DIGEST}\n")
file(WRITE "${FSIM_RELEASE_DIR}/v2-supply-chain.tsv" "${FSIM_RESULT}")

list(LENGTH FSIM_EXPECTED_DOCUMENT_PATHS FSIM_DOCUMENT_COUNT)
message(STATUS
  "v2 supply chain: source=${FSIM_SOURCE_ARCHIVE_COUNT}, binaries=3x542, "
  "documents=${FSIM_DOCUMENT_COUNT}, sboms=2, patches=4, "
  "signature=unsigned-release")

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR NOT DEFINED FSIM_BINARY_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR and FSIM_BINARY_DIR are required")
endif()

set(module "${FSIM_SOURCE_DIR}/cmake/FsimScv.cmake")
set(vendor_root "${FSIM_SOURCE_DIR}/third_party/scv-2.0.1")
set(archive "${vendor_root}/scv-2.0.1.tar.gz")
set(source_manifest "${vendor_root}/SOURCE_MANIFEST.txt")
set(sbom "${vendor_root}/scv-2.0.1.spdx.json")
set(root_cmake "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(test_cmake "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(input IN ITEMS
    "${module}" "${archive}" "${vendor_root}/LICENSE" "${vendor_root}/NOTICE"
    "${source_manifest}" "${sbom}" "${root_cmake}" "${test_cmake}")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "SCV provenance input is missing: ${input}")
  endif()
endforeach()

include("${module}")
fsim_scv_validate_source_metadata()
fsim_scv_validate_archive("${archive}")

file(READ "${source_manifest}" manifest_text)
foreach(token IN ITEMS
    "schema=${FSIM_SCV_SOURCE_SCHEMA}"
    "version=${FSIM_SCV_VERSION}"
    "release_date=${FSIM_SCV_RELEASE_DATE}"
    "release_id=${FSIM_SCV_RELEASE_ID}"
    "upstream_url=${FSIM_SCV_UPSTREAM_URL}"
    "archive_size=${FSIM_SCV_ARCHIVE_SIZE}"
    "archive_sha256=${FSIM_SCV_ARCHIVE_SHA256}"
    "tree_files=${FSIM_SCV_TREE_FILE_COUNT}"
    "tree_sha256=${FSIM_SCV_TREE_SHA256}"
    "license=${FSIM_SCV_LICENSE_EXPRESSION}"
    "purl=${FSIM_SCV_SBOM_PURL}")
  string(FIND "${manifest_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "SCV source manifest omits ${token}")
  endif()
endforeach()

foreach(entry IN LISTS FSIM_SCV_REQUIRED_FILES)
  string(REPLACE "|" ";" fields "${entry}")
  list(GET fields 0 relative_path)
  list(GET fields 1 expected_digest)
  if(relative_path STREQUAL "LICENSE" OR relative_path STREQUAL "NOTICE")
    file(SHA256 "${vendor_root}/${relative_path}" actual_digest)
    if(NOT actual_digest STREQUAL expected_digest)
      message(FATAL_ERROR "vendored SCV ${relative_path} changed")
    endif()
  endif()
endforeach()

file(READ "${sbom}" sbom_text)
string(JSON spdx_version GET "${sbom_text}" spdxVersion)
string(JSON package_name GET "${sbom_text}" packages 0 name)
string(JSON package_version GET "${sbom_text}" packages 0 versionInfo)
string(JSON package_download GET "${sbom_text}" packages 0 downloadLocation)
string(JSON package_digest GET "${sbom_text}" packages 0 checksums 0 checksumValue)
string(JSON package_license GET "${sbom_text}" packages 0 licenseDeclared)
string(JSON package_purl GET "${sbom_text}" packages 0 externalRefs 0 referenceLocator)
if(NOT spdx_version STREQUAL "SPDX-2.3" OR
   NOT package_name STREQUAL "SCV" OR
   NOT package_version STREQUAL FSIM_SCV_VERSION OR
   NOT package_download STREQUAL FSIM_SCV_UPSTREAM_URL OR
   NOT package_digest STREQUAL FSIM_SCV_ARCHIVE_SHA256 OR
   NOT package_license STREQUAL FSIM_SCV_LICENSE_EXPRESSION OR
   NOT package_purl STREQUAL FSIM_SCV_SBOM_PURL)
  message(FATAL_ERROR "SCV SBOM component drifted from governed metadata")
endif()

set(work_root "${FSIM_BINARY_DIR}/tests/scv-2.0.1-provenance")
fsim_scv_materialize_source("${archive}" "${work_root}"
                            materialized_root materialized_manifest)
if(NOT EXISTS "${materialized_manifest}")
  message(FATAL_ERROR "SCV materialization manifest was not written")
endif()
file(READ "${materialized_manifest}" materialized_text)
foreach(token IN ITEMS
    "schema=${FSIM_SCV_SOURCE_SCHEMA}"
    "archive_sha256=${FSIM_SCV_ARCHIVE_SHA256}"
    "tree_files=${FSIM_SCV_TREE_FILE_COUNT}"
    "tree_sha256=${FSIM_SCV_TREE_SHA256}")
  string(FIND "${materialized_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "materialized SCV manifest omits ${token}")
  endif()
endforeach()

# Preserve the archive's own release evidence, including its stale public
# header. Compatibility work may adapt it later, but provenance must not
# rewrite an upstream byte or misrepresent the package as a source tag.
file(READ "${materialized_root}/configure.ac" configure_text)
file(READ "${materialized_root}/RELEASENOTES" release_notes_text)
file(READ "${materialized_root}/src/scv/scv_ver.h" version_header_text)
foreach(token IN ITEMS
    "AC_INIT([SCV],[2.0.1])"
    "Release Notes for SCV 2.0.1")
  string(FIND "${configure_text}\n${release_notes_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "SCV package release evidence omits ${token}")
  endif()
endforeach()
foreach(token IN ITEMS
    "#define SCV_SHORT_RELEASE_DATE 20140417"
    "#define SCV_VERSION_PATCH      0")
  string(FIND "${version_header_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "SCV legacy version-header evidence omits ${token}")
  endif()
endforeach()

fsim_scv_work_root_error(
  "${FSIM_SOURCE_DIR}/generated-scv" unsafe_root unsafe_error)
if(unsafe_error STREQUAL "")
  message(FATAL_ERROR "SCV work-root guard accepted the source tree")
endif()

set(negative_root "${work_root}/negative")
file(MAKE_DIRECTORY "${negative_root}")
set(corrupt_archive "${negative_root}/corrupt-scv-2.0.1.tar.gz")
file(WRITE "${corrupt_archive}" "not the governed archive\n")
fsim_scv_archive_error("${corrupt_archive}" corrupt_error)
if(corrupt_error STREQUAL "")
  message(FATAL_ERROR "corrupt SCV archive was accepted")
endif()

set(wrong_tree "${negative_root}/scv-2.0.0")
file(MAKE_DIRECTORY "${wrong_tree}")
file(WRITE "${wrong_tree}/README.md" "wrong release\n")
set(wrong_archive "${negative_root}/scv-2.0.0.tar.gz")
file(ARCHIVE_CREATE OUTPUT "${wrong_archive}" PATHS "${wrong_tree}"
     FORMAT gnutar COMPRESSION GZip)
fsim_scv_archive_error("${wrong_archive}" wrong_archive_error)
if(wrong_archive_error STREQUAL "")
  message(FATAL_ERROR "wrong-version SCV archive was accepted")
endif()
fsim_scv_source_tree_error("${wrong_tree}" wrong_error)
if(NOT wrong_error MATCHES "wrong SCV source version/root")
  message(FATAL_ERROR "wrong-version SCV tree did not reject distinctly")
endif()

set(incomplete_tree "${negative_root}/scv-2.0.1")
file(MAKE_DIRECTORY "${incomplete_tree}")
file(COPY "${vendor_root}/LICENSE" DESTINATION "${incomplete_tree}")
set(incomplete_archive "${negative_root}/incomplete-scv-2.0.1.tar.gz")
file(ARCHIVE_CREATE OUTPUT "${incomplete_archive}" PATHS "${incomplete_tree}"
     FORMAT gnutar COMPRESSION GZip)
fsim_scv_archive_error("${incomplete_archive}" incomplete_archive_error)
if(incomplete_archive_error STREQUAL "")
  message(FATAL_ERROR "incomplete SCV archive was accepted")
endif()
fsim_scv_source_tree_error("${incomplete_tree}" incomplete_error)
if(NOT incomplete_error MATCHES "incomplete SCV source tree")
  message(FATAL_ERROR "incomplete SCV tree did not reject distinctly")
endif()

file(READ "${root_cmake}" root_cmake_text)
foreach(token IN ITEMS
    "include(cmake/FsimScv.cmake)"
    "FSIM_SCV_2_0_1_ARCHIVE"
    "fsim_scv_validate_archive")
  string(FIND "${root_cmake_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "top-level SCV provenance boundary omits ${token}")
  endif()
endforeach()
file(READ "${test_cmake}" test_cmake_text)
foreach(token IN ITEMS
    "NAME fsim.scv-upstream-provenance"
    "scv_upstream_provenance_test.cmake")
  string(FIND "${test_cmake_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "SCV provenance test registration omits ${token}")
  endif()
endforeach()

message(STATUS
  "SCV upstream provenance passed: version=${FSIM_SCV_VERSION} "
  "archive=${FSIM_SCV_ARCHIVE_SHA256} files=${FSIM_SCV_TREE_FILE_COUNT} "
  "tree=${FSIM_SCV_TREE_SHA256} license=${FSIM_SCV_LICENSE_EXPRESSION}")

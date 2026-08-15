# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR NOT DEFINED FSIM_BINARY_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR and FSIM_BINARY_DIR are required")
endif()

set(module "${FSIM_SOURCE_DIR}/cmake/FsimSystemCAccellera.cmake")
set(vendor_root "${FSIM_SOURCE_DIR}/third_party/systemc-3.0.2")
set(archive "${vendor_root}/systemc-3.0.2.tar.gz")
set(source_manifest "${vendor_root}/SOURCE_MANIFEST.txt")
set(sbom "${vendor_root}/systemc-3.0.2.spdx.json")
set(root_cmake "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(test_cmake "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(input IN ITEMS
    "${module}" "${archive}" "${vendor_root}/LICENSE" "${vendor_root}/NOTICE"
    "${source_manifest}" "${sbom}" "${root_cmake}" "${test_cmake}")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "SystemC provenance input is missing: ${input}")
  endif()
endforeach()

include("${module}")
fsim_systemc_validate_source_metadata()
fsim_systemc_validate_archive("${archive}")

file(READ "${source_manifest}" manifest_text)
foreach(token IN ITEMS
    "schema=${FSIM_SYSTEMC_SOURCE_SCHEMA}"
    "version=${FSIM_SYSTEMC_VERSION}"
    "release_date=${FSIM_SYSTEMC_RELEASE_DATE}"
    "release_tag=${FSIM_SYSTEMC_RELEASE_TAG}"
    "upstream_commit=${FSIM_SYSTEMC_UPSTREAM_COMMIT}"
    "upstream_url=${FSIM_SYSTEMC_UPSTREAM_URL}"
    "archive_size=${FSIM_SYSTEMC_ARCHIVE_SIZE}"
    "archive_sha256=${FSIM_SYSTEMC_ARCHIVE_SHA256}"
    "tree_files=${FSIM_SYSTEMC_TREE_FILE_COUNT}"
    "tree_sha256=${FSIM_SYSTEMC_TREE_SHA256}"
    "license=${FSIM_SYSTEMC_LICENSE_EXPRESSION}"
    "purl=${FSIM_SYSTEMC_SBOM_PURL}")
  string(FIND "${manifest_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "SystemC source manifest omits ${token}")
  endif()
endforeach()

foreach(entry IN LISTS FSIM_SYSTEMC_REQUIRED_FILES)
  string(REPLACE "|" ";" fields "${entry}")
  list(GET fields 0 relative_path)
  list(GET fields 1 expected_digest)
  if(relative_path STREQUAL "LICENSE" OR relative_path STREQUAL "NOTICE")
    file(SHA256 "${vendor_root}/${relative_path}" actual_digest)
    if(NOT actual_digest STREQUAL expected_digest)
      message(FATAL_ERROR "vendored SystemC ${relative_path} changed")
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
   NOT package_name STREQUAL "SystemC" OR
   NOT package_version STREQUAL FSIM_SYSTEMC_VERSION OR
   NOT package_download STREQUAL FSIM_SYSTEMC_UPSTREAM_URL OR
   NOT package_digest STREQUAL FSIM_SYSTEMC_ARCHIVE_SHA256 OR
   NOT package_license STREQUAL FSIM_SYSTEMC_LICENSE_EXPRESSION OR
   NOT package_purl STREQUAL FSIM_SYSTEMC_SBOM_PURL)
  message(FATAL_ERROR "SystemC SBOM component drifted from governed metadata")
endif()

set(work_root "${FSIM_BINARY_DIR}/tests/systemc-3.0.2-provenance")
fsim_systemc_materialize_source("${archive}" "${work_root}"
                                materialized_root materialized_manifest)
if(NOT EXISTS "${materialized_manifest}")
  message(FATAL_ERROR "SystemC materialization manifest was not written")
endif()
file(READ "${materialized_manifest}" materialized_text)
foreach(token IN ITEMS
    "schema=${FSIM_SYSTEMC_SOURCE_SCHEMA}"
    "archive_sha256=${FSIM_SYSTEMC_ARCHIVE_SHA256}"
    "tree_files=${FSIM_SYSTEMC_TREE_FILE_COUNT}"
    "tree_sha256=${FSIM_SYSTEMC_TREE_SHA256}")
  string(FIND "${materialized_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "materialized SystemC manifest omits ${token}")
  endif()
endforeach()

fsim_systemc_work_root_error(
  "${FSIM_SOURCE_DIR}/generated-systemc" unsafe_root unsafe_error)
if(unsafe_error STREQUAL "")
  message(FATAL_ERROR "SystemC work-root guard accepted the source tree")
endif()

set(negative_root "${work_root}/negative")
file(MAKE_DIRECTORY "${negative_root}")
set(corrupt_archive "${negative_root}/corrupt-systemc-3.0.2.tar.gz")
file(WRITE "${corrupt_archive}" "not the governed archive\n")
fsim_systemc_archive_error("${corrupt_archive}" corrupt_error)
if(corrupt_error STREQUAL "")
  message(FATAL_ERROR "corrupt SystemC archive was accepted")
endif()

set(wrong_tree "${negative_root}/systemc-3.0.1")
file(MAKE_DIRECTORY "${wrong_tree}")
file(WRITE "${wrong_tree}/README.md" "wrong release\n")
set(wrong_archive "${negative_root}/systemc-3.0.1.tar.gz")
file(ARCHIVE_CREATE OUTPUT "${wrong_archive}" PATHS "${wrong_tree}"
     FORMAT gnutar COMPRESSION GZip)
fsim_systemc_archive_error("${wrong_archive}" wrong_archive_error)
if(wrong_archive_error STREQUAL "")
  message(FATAL_ERROR "wrong-version SystemC archive was accepted")
endif()
fsim_systemc_source_tree_error("${wrong_tree}" wrong_error)
if(NOT wrong_error MATCHES "wrong SystemC source version/root")
  message(FATAL_ERROR "wrong-version SystemC tree did not reject distinctly")
endif()

set(incomplete_tree "${negative_root}/systemc-3.0.2")
file(MAKE_DIRECTORY "${incomplete_tree}")
file(COPY "${vendor_root}/LICENSE" DESTINATION "${incomplete_tree}")
set(incomplete_archive "${negative_root}/incomplete-systemc-3.0.2.tar.gz")
file(ARCHIVE_CREATE OUTPUT "${incomplete_archive}" PATHS "${incomplete_tree}"
     FORMAT gnutar COMPRESSION GZip)
fsim_systemc_archive_error("${incomplete_archive}" incomplete_archive_error)
if(incomplete_archive_error STREQUAL "")
  message(FATAL_ERROR "incomplete SystemC archive was accepted")
endif()
fsim_systemc_source_tree_error("${incomplete_tree}" incomplete_error)
if(NOT incomplete_error MATCHES "incomplete SystemC source tree")
  message(FATAL_ERROR "incomplete SystemC tree did not reject distinctly")
endif()

file(READ "${root_cmake}" root_cmake_text)
foreach(token IN ITEMS
    "include(cmake/FsimSystemCAccellera.cmake)"
    "FSIM_SYSTEMC_3_0_2_ARCHIVE"
    "fsim_systemc_validate_archive")
  string(FIND "${root_cmake_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "top-level SystemC provenance boundary omits ${token}")
  endif()
endforeach()
file(READ "${test_cmake}" test_cmake_text)
foreach(token IN ITEMS
    "NAME fsim.systemc-upstream-provenance"
    "systemc_upstream_provenance_test.cmake")
  string(FIND "${test_cmake_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "SystemC provenance test registration omits ${token}")
  endif()
endforeach()

message(STATUS
  "SystemC upstream provenance passed: version=${FSIM_SYSTEMC_VERSION} "
  "archive=${FSIM_SYSTEMC_ARCHIVE_SHA256} files=${FSIM_SYSTEMC_TREE_FILE_COUNT} "
  "tree=${FSIM_SYSTEMC_TREE_SHA256} license=${FSIM_SYSTEMC_LICENSE_EXPRESSION}")

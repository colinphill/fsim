# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

set(FSIM_UVM_SOURCE_SCHEMA "fsim-uvm-sources-v1")
set(FSIM_UVM_RELEASE_IDS uvm-1.2 uvm-2020.3.1)

set(FSIM_UVM_1_2_VERSION "1.2")
set(FSIM_UVM_1_2_STANDARD "Accellera UVM 1.2")
set(FSIM_UVM_1_2_RELEASE_ID "accellera-2014-06")
set(FSIM_UVM_1_2_UPSTREAM_COMMIT "archive-release")
set(
  FSIM_UVM_1_2_URL
  "https://www.accellera.org/images/downloads/standards/uvm/uvm-1.2.tar.gz"
)
set(FSIM_UVM_1_2_ARCHIVE_NAME "uvm-1.2.tar.gz")
set(
  FSIM_UVM_1_2_ARCHIVE_SHA256
  "502a2e605ce552bfd9767803c7e99a053715b00f7a9c4c511c3fbfddfb30157c"
)
set(FSIM_UVM_1_2_ARCHIVE_SIZE 2463986)
set(FSIM_UVM_1_2_ROOT_DIR "uvm-1.2")
set(FSIM_UVM_1_2_TREE_FILE_COUNT 960)
set(
  FSIM_UVM_1_2_TREE_SHA256
  "badb7104548cabd934c6ca95dd126a3b6ee3c71ff6974acffdd4e63d2bfd1f49"
)
set(
  FSIM_UVM_1_2_REQUIRED_FILES
  "src/uvm_pkg.sv|b8d2fde129abcc2df53e184a0cb2be5338436be55a111aa26943920eede2f826"
  "src/uvm_macros.svh|0fb102cbeaf168bc659c659a4442a722f0e137496f3dad3c837ee6794476cd51"
  "src/dpi/uvm_dpi.cc|478d0ac720c59093f370222555dc77837bc58b8f76d47e5e323750749d6fd756"
  "LICENSE.txt|c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4"
  "NOTICE.txt|5d097b0e4ba7e26bd8f71255eff7ab38ab91511cdba65428af2d7424fafcd71d"
  "README.txt|2e836d3a0a1fdaa562fe3d86d37a00deb27121f866c3bfa4442b0c07846d86c0"
)

set(FSIM_UVM_2020_3_1_VERSION "2020.3.1")
set(FSIM_UVM_2020_3_1_STANDARD "IEEE 1800.2-2020")
set(FSIM_UVM_2020_3_1_RELEASE_ID "2020.3.1")
set(
  FSIM_UVM_2020_3_1_UPSTREAM_COMMIT
  "78c06547a2a0a29b3dc9dcafae62b75b2ff61544"
)
set(
  FSIM_UVM_2020_3_1_URL
  "https://www.accellera.org/images/downloads/standards/uvm/UVM-1800.2-2020.3.1.tar.gz"
)
set(FSIM_UVM_2020_3_1_ARCHIVE_NAME "UVM-1800.2-2020.3.1.tar.gz")
set(
  FSIM_UVM_2020_3_1_ARCHIVE_SHA256
  "0d6a2ca5811c787e5aa1e945abaaaa5d5c295148d5e704c9fa910d7b288cbcf7"
)
set(FSIM_UVM_2020_3_1_ARCHIVE_SIZE 724207)
set(FSIM_UVM_2020_3_1_ROOT_DIR "1800.2-2020.3.1")
set(FSIM_UVM_2020_3_1_TREE_FILE_COUNT 326)
set(
  FSIM_UVM_2020_3_1_TREE_SHA256
  "0d0c409af4ba5984df5a3e7d4730289183fa5b714b019b712c9f01e3f769f980"
)
set(
  FSIM_UVM_2020_3_1_REQUIRED_FILES
  "src/uvm_pkg.sv|ad84a883a3d723d080304e83bd70f9c0983493e18ff073b039e0ea5daeac38dc"
  "src/uvm_macros.svh|164eba3d2426473ce336fec6ee8462e7b24bd12a1d2c74bada8ba88d4435e13b"
  "src/dpi/uvm_dpi.cc|8e55ecfed3e3502cd8d5fe7712c18b065bfcf8a2c55a64132216a79662d59b99"
  "LICENSE.txt|c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4"
  "NOTICE.txt|b89f85d3f14ae457890c932bd543124e6521f74b3b01ab208932ef72488748c2"
  "README.md|42add1aae2da45d12674de9bf099c6ab2cb0ca7e9b6b3e64383431a594aa88aa"
)

function(fsim_uvm_release_prefix release output)
  if(release STREQUAL "uvm-1.2")
    set(prefix FSIM_UVM_1_2)
  elseif(release STREQUAL "uvm-2020.3.1")
    set(prefix FSIM_UVM_2020_3_1)
  else()
    message(FATAL_ERROR "unknown governed UVM release: ${release}")
  endif()
  set(${output} "${prefix}" PARENT_SCOPE)
endfunction()

function(fsim_uvm_release_field release field output)
  if(NOT field MATCHES "^[A-Z][A-Z0-9_]*$")
    message(FATAL_ERROR "invalid UVM release metadata field: ${field}")
  endif()
  fsim_uvm_release_prefix("${release}" prefix)
  set(variable "${prefix}_${field}")
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "missing ${field} metadata for ${release}")
  endif()
  set(${output} "${${variable}}" PARENT_SCOPE)
endfunction()

function(fsim_uvm_validate_metadata)
  list(LENGTH FSIM_UVM_RELEASE_IDS release_count)
  if(NOT release_count EQUAL 2)
    message(FATAL_ERROR "expected exactly two governed UVM releases")
  endif()
  foreach(release IN LISTS FSIM_UVM_RELEASE_IDS)
    foreach(field IN ITEMS
        VERSION STANDARD RELEASE_ID UPSTREAM_COMMIT URL ARCHIVE_NAME
        ARCHIVE_SHA256 ARCHIVE_SIZE ROOT_DIR TREE_FILE_COUNT TREE_SHA256
        REQUIRED_FILES)
      fsim_uvm_release_field("${release}" "${field}" value)
      if(value STREQUAL "")
        message(FATAL_ERROR "empty ${field} metadata for ${release}")
      endif()
    endforeach()
    fsim_uvm_release_field("${release}" URL url)
    if(NOT url MATCHES "^https://www\\.accellera\\.org/")
      message(FATAL_ERROR "non-Accellera UVM source URL: ${url}")
    endif()
    fsim_uvm_release_field("${release}" UPSTREAM_COMMIT upstream_commit)
    if(release STREQUAL "uvm-1.2")
      if(NOT upstream_commit STREQUAL "archive-release")
        message(FATAL_ERROR "UVM 1.2 must retain its archive-only identity")
      endif()
    elseif(NOT upstream_commit MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "invalid upstream commit for ${release}")
    else()
      string(LENGTH "${upstream_commit}" commit_length)
      if(NOT commit_length EQUAL 40)
        message(FATAL_ERROR "invalid upstream commit length for ${release}")
      endif()
    endif()
    foreach(field IN ITEMS ARCHIVE_SIZE TREE_FILE_COUNT)
      fsim_uvm_release_field("${release}" "${field}" count)
      if(NOT count MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "invalid ${field} for ${release}: ${count}")
      endif()
    endforeach()
    foreach(field IN ITEMS ARCHIVE_NAME ROOT_DIR)
      fsim_uvm_release_field("${release}" "${field}" relative_name)
      if(NOT relative_name MATCHES "^[A-Za-z0-9][A-Za-z0-9_.-]*$")
        message(FATAL_ERROR "unsafe ${field} for ${release}: ${relative_name}")
      endif()
    endforeach()
    foreach(field IN ITEMS ARCHIVE_SHA256 TREE_SHA256)
      fsim_uvm_release_field("${release}" "${field}" digest)
      if(NOT digest MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "invalid ${field} for ${release}")
      endif()
      string(LENGTH "${digest}" digest_length)
      if(NOT digest_length EQUAL 64)
        message(FATAL_ERROR "invalid ${field} length for ${release}")
      endif()
    endforeach()
    fsim_uvm_release_field("${release}" REQUIRED_FILES required_files)
    list(LENGTH required_files required_count)
    if(NOT required_count EQUAL 6)
      message(FATAL_ERROR "expected six governed entry points for ${release}")
    endif()
    foreach(entry IN LISTS required_files)
      string(REPLACE "|" ";" fields "${entry}")
      list(LENGTH fields field_count)
      if(NOT field_count EQUAL 2)
        message(FATAL_ERROR "malformed UVM entry-point identity: ${entry}")
      endif()
      list(GET fields 0 relative_path)
      list(GET fields 1 digest)
      if(relative_path MATCHES "(^/|(^|/)\\.\\.(/|$))")
        message(FATAL_ERROR "unsafe UVM entry-point path: ${relative_path}")
      endif()
      if(NOT digest MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "invalid UVM entry-point digest: ${entry}")
      endif()
      string(LENGTH "${digest}" digest_length)
      if(NOT digest_length EQUAL 64)
        message(FATAL_ERROR "invalid UVM entry-point digest length: ${entry}")
      endif()
    endforeach()
  endforeach()
endfunction()

function(fsim_uvm_work_root_error work_root output_root output_error)
  if(work_root STREQUAL "")
    set(${output_root} "" PARENT_SCOPE)
    set(${output_error} "the UVM work root is empty" PARENT_SCOPE)
    return()
  endif()
  if(DEFINED FSIM_BINARY_DIR AND NOT FSIM_BINARY_DIR STREQUAL "")
    set(binary_root "${FSIM_BINARY_DIR}")
  else()
    set(binary_root "${CMAKE_BINARY_DIR}")
  endif()
  cmake_path(NORMAL_PATH binary_root OUTPUT_VARIABLE binary_root)
  set(candidate "${work_root}")
  cmake_path(
    ABSOLUTE_PATH candidate
    BASE_DIRECTORY "${binary_root}"
    NORMALIZE
    OUTPUT_VARIABLE normalized
  )
  set(error "")
  if(DEFINED FSIM_SOURCE_DIR AND NOT FSIM_SOURCE_DIR STREQUAL "")
    set(source_root "${FSIM_SOURCE_DIR}")
    cmake_path(NORMAL_PATH source_root OUTPUT_VARIABLE source_root)
    cmake_path(IS_PREFIX source_root "${normalized}" NORMALIZE inside_source)
    cmake_path(IS_PREFIX binary_root "${normalized}" NORMALIZE inside_binary)
    if(inside_source AND NOT inside_binary)
      set(error "the UVM work root must remain outside the source tree")
    endif()
  endif()
  set(${output_root} "${normalized}" PARENT_SCOPE)
  set(${output_error} "${error}" PARENT_SCOPE)
endfunction()

function(fsim_uvm_validate_work_root work_root output_root)
  fsim_uvm_work_root_error("${work_root}" normalized error)
  if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}: ${normalized}")
  endif()
  set(${output_root} "${normalized}" PARENT_SCOPE)
endfunction()

function(fsim_uvm_compute_tree_identity root output_count output_digest)
  if(NOT IS_DIRECTORY "${root}")
    message(FATAL_ERROR "UVM source root is not a directory: ${root}")
  endif()
  file(
    GLOB_RECURSE files
    LIST_DIRECTORIES FALSE
    RELATIVE "${root}"
    "${root}/*"
  )
  list(SORT files)
  set(canonical "")
  foreach(relative_path IN LISTS files)
    if(relative_path MATCHES "(^|/)\\.\\.(/|$)")
      message(FATAL_ERROR "unsafe extracted UVM path: ${relative_path}")
    endif()
    file(SHA256 "${root}/${relative_path}" digest)
    string(APPEND canonical "${digest}  ${relative_path}\n")
  endforeach()
  list(LENGTH files file_count)
  string(SHA256 tree_digest "${canonical}")
  set(${output_count} "${file_count}" PARENT_SCOPE)
  set(${output_digest} "${tree_digest}" PARENT_SCOPE)
endfunction()

function(fsim_uvm_validate_source_tree release root)
  fsim_uvm_release_field("${release}" ROOT_DIR expected_root_name)
  get_filename_component(actual_root_name "${root}" NAME)
  if(NOT actual_root_name STREQUAL expected_root_name)
    message(
      FATAL_ERROR
      "unexpected ${release} source-root name: ${actual_root_name}"
    )
  endif()
  fsim_uvm_release_field("${release}" REQUIRED_FILES required_files)
  foreach(entry IN LISTS required_files)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 relative_path)
    list(GET fields 1 expected_digest)
    if(NOT EXISTS "${root}/${relative_path}")
      message(FATAL_ERROR "${release} omits ${relative_path}")
    endif()
    file(SHA256 "${root}/${relative_path}" actual_digest)
    if(NOT actual_digest STREQUAL expected_digest)
      message(FATAL_ERROR "${release} entry-point changed: ${relative_path}")
    endif()
  endforeach()
  fsim_uvm_compute_tree_identity("${root}" actual_count actual_digest)
  fsim_uvm_release_field("${release}" TREE_FILE_COUNT expected_count)
  fsim_uvm_release_field("${release}" TREE_SHA256 expected_digest)
  if(NOT actual_count EQUAL expected_count OR NOT actual_digest STREQUAL expected_digest)
    message(
      FATAL_ERROR
      "${release} source tree changed: files=${actual_count}, "
      "SHA256=${actual_digest}"
    )
  endif()
  file(READ "${root}/LICENSE.txt" license LIMIT 512)
  if(NOT license MATCHES "Apache License")
    message(FATAL_ERROR "${release} license is not Apache-2.0")
  endif()
endfunction()

function(fsim_uvm_validate_archive release archive)
  if(NOT EXISTS "${archive}")
    message(FATAL_ERROR "${release} archive does not exist: ${archive}")
  endif()
  fsim_uvm_release_field("${release}" ARCHIVE_SIZE expected_size)
  file(SIZE "${archive}" actual_size)
  if(NOT actual_size EQUAL expected_size)
    message(
      FATAL_ERROR
      "${release} archive size changed: expected ${expected_size}, "
      "found ${actual_size}"
    )
  endif()
  fsim_uvm_release_field("${release}" ARCHIVE_SHA256 expected_digest)
  file(SHA256 "${archive}" actual_digest)
  if(NOT actual_digest STREQUAL expected_digest)
    message(FATAL_ERROR "${release} archive SHA-256 changed: ${actual_digest}")
  endif()
endfunction()

function(fsim_uvm_manifest_contents release root archive output)
  fsim_uvm_compute_tree_identity("${root}" tree_count tree_digest)
  set(contents "schema=${FSIM_UVM_SOURCE_SCHEMA}\n")
  string(APPEND contents "release=${release}\n")
  foreach(field IN ITEMS VERSION STANDARD RELEASE_ID UPSTREAM_COMMIT URL)
    fsim_uvm_release_field("${release}" "${field}" value)
    string(TOLOWER "${field}" key)
    string(APPEND contents "${key}=${value}\n")
  endforeach()
  fsim_uvm_release_field("${release}" ARCHIVE_SHA256 archive_digest)
  string(APPEND contents "archive=${archive}\n")
  string(APPEND contents "archive_sha256=${archive_digest}\n")
  string(APPEND contents "source_root=${root}\n")
  string(APPEND contents "tree_files=${tree_count}\n")
  string(APPEND contents "tree_sha256=${tree_digest}\n")
  fsim_uvm_release_field("${release}" REQUIRED_FILES required_files)
  foreach(entry IN LISTS required_files)
    string(APPEND contents "entry=${entry}\n")
  endforeach()
  set(${output} "${contents}" PARENT_SCOPE)
endfunction()

function(fsim_uvm_materialize_release release work_root archive_override output_root output_manifest)
  fsim_uvm_validate_metadata()
  fsim_uvm_validate_work_root("${work_root}" work_root)
  fsim_uvm_release_field("${release}" ARCHIVE_NAME archive_name)
  if(archive_override STREQUAL "")
    set(archive "${work_root}/downloads/${archive_name}")
    if(NOT EXISTS "${archive}")
      file(MAKE_DIRECTORY "${work_root}/downloads")
      fsim_uvm_release_field("${release}" URL url)
      fsim_uvm_release_field("${release}" ARCHIVE_SHA256 expected_digest)
      file(
        DOWNLOAD "${url}" "${archive}"
        EXPECTED_HASH "SHA256=${expected_digest}"
        TLS_VERIFY ON
        STATUS download_status
        LOG download_log
        SHOW_PROGRESS
      )
      list(GET download_status 0 download_code)
      list(GET download_status 1 download_message)
      if(NOT download_code EQUAL 0)
        message(
          FATAL_ERROR
          "failed to download ${release}: ${download_message}\n${download_log}"
        )
      endif()
    endif()
  else()
    set(archive_candidate "${archive_override}")
    if(DEFINED FSIM_BINARY_DIR AND NOT FSIM_BINARY_DIR STREQUAL "")
      set(archive_base "${FSIM_BINARY_DIR}")
    else()
      set(archive_base "${CMAKE_BINARY_DIR}")
    endif()
    cmake_path(
      ABSOLUTE_PATH archive_candidate
      BASE_DIRECTORY "${archive_base}"
      NORMALIZE
      OUTPUT_VARIABLE archive
    )
  endif()
  fsim_uvm_validate_archive("${release}" "${archive}")

  fsim_uvm_release_field("${release}" ROOT_DIR root_dir)
  set(source_parent "${work_root}/sources")
  set(root "${source_parent}/${root_dir}")
  if(NOT EXISTS "${root}")
    file(MAKE_DIRECTORY "${source_parent}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${source_parent}")
  endif()
  fsim_uvm_validate_source_tree("${release}" "${root}")

  file(MAKE_DIRECTORY "${work_root}/manifests")
  set(manifest "${work_root}/manifests/${release}.txt")
  fsim_uvm_manifest_contents("${release}" "${root}" "${archive}" contents)
  file(WRITE "${manifest}" "${contents}")
  set(${output_root} "${root}" PARENT_SCOPE)
  set(${output_manifest} "${manifest}" PARENT_SCOPE)
endfunction()

function(fsim_uvm_configure_sources)
  string(TOUPPER "${FSIM_UVM_SOURCE_MODE}" mode)
  if(NOT mode MATCHES "^(OFF|FETCH|ARCHIVE)$")
    message(
      FATAL_ERROR
      "FSIM_UVM_SOURCE_MODE must be OFF, FETCH, or ARCHIVE "
      "(received '${FSIM_UVM_SOURCE_MODE}')"
    )
  endif()
  if(mode STREQUAL "OFF")
    set(FSIM_UVM_1_2_SOURCE_DIR "" PARENT_SCOPE)
    set(FSIM_UVM_2020_3_1_SOURCE_DIR "" PARENT_SCOPE)
    message(STATUS "fsim external UVM sources disabled")
    return()
  endif()
  if(mode STREQUAL "ARCHIVE")
    if(FSIM_UVM_1_2_ARCHIVE STREQUAL "" OR FSIM_UVM_2020_3_1_ARCHIVE STREQUAL "")
      message(FATAL_ERROR "ARCHIVE mode requires both pinned UVM archives")
    endif()
    set(archive_1_2 "${FSIM_UVM_1_2_ARCHIVE}")
    set(archive_2020 "${FSIM_UVM_2020_3_1_ARCHIVE}")
  else()
    set(archive_1_2 "")
    set(archive_2020 "")
  endif()
  fsim_uvm_materialize_release(
    uvm-1.2 "${FSIM_UVM_WORK_ROOT}" "${archive_1_2}"
    root_1_2 manifest_1_2
  )
  fsim_uvm_materialize_release(
    uvm-2020.3.1 "${FSIM_UVM_WORK_ROOT}" "${archive_2020}"
    root_2020 manifest_2020
  )
  set(FSIM_UVM_1_2_SOURCE_DIR "${root_1_2}" PARENT_SCOPE)
  set(FSIM_UVM_2020_3_1_SOURCE_DIR "${root_2020}" PARENT_SCOPE)
  set(FSIM_UVM_SOURCE_MANIFESTS "${manifest_1_2};${manifest_2020}" PARENT_SCOPE)
  message(STATUS "fsim UVM 1.2 source: ${root_1_2}")
  message(STATUS "fsim UVM 2020.3.1 source: ${root_2020}")
endfunction()

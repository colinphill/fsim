# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

foreach(FSIM_REQUIRED IN ITEMS FSIM_SOURCE_DIR FSIM_WORK_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()
if(FSIM_SOURCE_ONLY AND FSIM_BINARY_ONLY)
  message(FATAL_ERROR "source-only and binary-only packaging are exclusive")
endif()
if(NOT FSIM_SOURCE_ONLY)
  foreach(FSIM_REQUIRED IN ITEMS FSIM_BINARY_DIR FSIM_PLATFORM FSIM_COMPILER)
    if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
      message(FATAL_ERROR "${FSIM_REQUIRED} is required")
    endif()
  endforeach()
endif()

set(FSIM_POLICY "${FSIM_SOURCE_DIR}/packaging/package-policy.txt")
set(FSIM_SOURCE_MANIFEST
    "${FSIM_SOURCE_DIR}/packaging/source-package-manifest.txt")
set(FSIM_ARCHIVE_HELPER
    "${FSIM_SOURCE_DIR}/cmake/CreateDeterministicArchive.cmake")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_POLICY}" "${FSIM_SOURCE_MANIFEST}" "${FSIM_ARCHIVE_HELPER}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "deterministic package input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_POLICY}" FSIM_POLICY_LINES ENCODING UTF-8)
foreach(FSIM_EXPECTED IN ITEMS
    "schema=fsim-package-policy-v1"
    "release_identity=fsim-v2.0.0"
    "archive_format=zip"
    "archive_compression=python-zipfile-deflate-level-9"
    "entry_order=utf8-bytewise-ascending"
    "entry_mtime=2000-01-01T00:00:00Z"
    "regular_file_mode=0644"
    "executable_file_mode=0755"
    "directory_entries=implicit"
    "host_owner_identity=not-encoded"
    "source_entry_limit=5000"
    "binary_entry_limit=5000"
    "source_archive_byte_limit=67108864"
    "source_package_root=fsim-v2.0.0-source"
    "source_artifact=build/release/fsim-v2.0.0-source.zip"
    "source_extraction=preflight-safe-paths-before-extract"
    "source_reconstruction=manifest-validate-and-fetchcontent-disconnected-configure"
    "binary_archive_byte_limit=536870912"
    "binary_artifacts=fsim-v2.0.0-linux-x86_64-clang22-llvm22.zip,fsim-v2.0.0-linux-x86_64-gcc13-llvm22.zip,fsim-v2.0.0-linux-x86_64-gcc13-no-llvm.zip,fsim-v2.0.0-windows-x86_64-llvm-mingw-no-llvm.zip,fsim-v2.0.0-windows-x86_64-llvm-mingw-llvm22.zip"
    "binary_extraction=preflight-safe-paths-and-relative-symlink-targets"
    "path_byte_limit=1024"
    "checksum=sha256"
    "signature=unsigned-release")
  list(FIND FSIM_POLICY_LINES "${FSIM_EXPECTED}" FSIM_POLICY_INDEX)
  if(FSIM_POLICY_INDEX EQUAL -1)
    message(FATAL_ERROR "package policy omits ${FSIM_EXPECTED}")
  endif()
endforeach()

function(fsim_normalize_package_file path source_relative)
  if(source_relative MATCHES "^scripts/.*\\.(py|sh)$"
     OR source_relative MATCHES "^bin/")
    file(CHMOD "${path}"
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                  WORLD_READ WORLD_EXECUTE)
  else()
    file(CHMOD "${path}"
      PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
  endif()
endfunction()

function(fsim_write_archive_manifest package_parent package_name relative_files
         output_manifest output_paths)
  set(FSIM_PATHS)
  foreach(FSIM_RELATIVE IN LISTS relative_files)
    string(LENGTH "${FSIM_RELATIVE}" FSIM_PATH_LENGTH)
    if(FSIM_PATH_LENGTH GREATER 1024)
      message(FATAL_ERROR "package path exceeds 1024 bytes: ${FSIM_RELATIVE}")
    endif()
    list(APPEND FSIM_PATHS "${package_name}/${FSIM_RELATIVE}")
  endforeach()
  list(SORT FSIM_PATHS)
  set(FSIM_CONTENT)
  foreach(FSIM_PATH IN LISTS FSIM_PATHS)
    string(APPEND FSIM_CONTENT "${FSIM_PATH}\n")
  endforeach()
  file(WRITE "${output_manifest}" "${FSIM_CONTENT}")
  set(${output_paths} "${FSIM_PATHS}" PARENT_SCOPE)
endfunction()

function(fsim_create_archive package_parent manifest output mtime result_var)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_ARCHIVE_MANIFEST=${manifest}"
      "-DFSIM_ARCHIVE_OUTPUT=${output}"
      "-DFSIM_ARCHIVE_MTIME=${mtime}"
      -P "${FSIM_ARCHIVE_HELPER}"
    WORKING_DIRECTORY "${package_parent}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  set(${result_var} "${FSIM_RESULT}" PARENT_SCOPE)
  set(FSIM_LAST_ARCHIVE_OUTPUT "${FSIM_OUTPUT}${FSIM_ERROR}" PARENT_SCOPE)
endfunction()

function(fsim_list_archive archive output_paths)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${archive}"
    RESULT_VARIABLE FSIM_LIST_RESULT
    OUTPUT_VARIABLE FSIM_LIST_OUTPUT
    ERROR_VARIABLE FSIM_LIST_ERROR)
  if(NOT FSIM_LIST_RESULT EQUAL 0)
    message(FATAL_ERROR
      "cannot list deterministic archive ${archive}: ${FSIM_LIST_ERROR}")
  endif()
  string(REPLACE "\r\n" "\n" FSIM_LIST_OUTPUT "${FSIM_LIST_OUTPUT}")
  string(REPLACE "\n" ";" FSIM_LIST "${FSIM_LIST_OUTPUT}")
  list(FILTER FSIM_LIST EXCLUDE REGEX "^$")
  set(${output_paths} "${FSIM_LIST}" PARENT_SCOPE)
endfunction()

function(fsim_require_safe_archive_paths package_name archive_paths)
  set(FSIM_SEEN)
  foreach(FSIM_PATH IN LISTS archive_paths)
    string(LENGTH "${FSIM_PATH}" FSIM_PATH_LENGTH)
    string(FIND "${FSIM_PATH}" "${package_name}/" FSIM_ROOT_INDEX)
    if(FSIM_PATH STREQUAL ""
       OR NOT FSIM_ROOT_INDEX EQUAL 0
       OR FSIM_PATH_LENGTH GREATER 1024
       OR FSIM_PATH MATCHES "^/"
       OR FSIM_PATH MATCHES "^[A-Za-z]:"
       OR FSIM_PATH MATCHES "\\\\"
       OR FSIM_PATH MATCHES "(^|/)\\.\\.?(/|$)"
       OR FSIM_PATH MATCHES "//")
      message(FATAL_ERROR "archive extraction path is unsafe: ${FSIM_PATH}")
    endif()
    list(FIND FSIM_SEEN "${FSIM_PATH}" FSIM_DUPLICATE_INDEX)
    if(NOT FSIM_DUPLICATE_INDEX EQUAL -1)
      message(FATAL_ERROR "archive extraction path is repeated: ${FSIM_PATH}")
    endif()
    list(APPEND FSIM_SEEN "${FSIM_PATH}")
  endforeach()
endfunction()

function(fsim_expect_archive_manifest_failure name contents)
  set(FSIM_MANIFEST "${FSIM_WORK_DIR}/unsafe-${name}.manifest")
  set(FSIM_ARCHIVE "${FSIM_WORK_DIR}/unsafe-${name}.zip")
  file(WRITE "${FSIM_MANIFEST}" "${contents}")
  fsim_create_archive(
    "${FSIM_WORK_DIR}/source-a" "${FSIM_MANIFEST}" "${FSIM_ARCHIVE}"
    "2000-01-01T00:00:00Z" FSIM_UNSAFE_RESULT)
  if(FSIM_UNSAFE_RESULT EQUAL 0)
    message(FATAL_ERROR "unsafe ${name} archive manifest was accepted")
  endif()
endfunction()

file(REMOVE_RECURSE "${FSIM_WORK_DIR}")
file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")
if(NOT FSIM_BINARY_ONLY)
file(STRINGS "${FSIM_SOURCE_MANIFEST}" FSIM_SOURCE_LINES ENCODING UTF-8)
set(FSIM_SOURCE_FILES)
foreach(FSIM_LINE IN LISTS FSIM_SOURCE_LINES)
  if(NOT FSIM_LINE STREQUAL "" AND NOT FSIM_LINE MATCHES "^#")
    list(APPEND FSIM_SOURCE_FILES "${FSIM_LINE}")
  endif()
endforeach()
list(LENGTH FSIM_SOURCE_FILES FSIM_SOURCE_COUNT)
if(FSIM_SOURCE_COUNT GREATER 5000)
  message(FATAL_ERROR "source package exceeds 5000 entries")
endif()

if(DEFINED FSIM_SOURCE_PACKAGE_NAME AND
   NOT FSIM_SOURCE_PACKAGE_NAME STREQUAL "")
  set(FSIM_SOURCE_PACKAGE "${FSIM_SOURCE_PACKAGE_NAME}")
else()
  set(FSIM_SOURCE_PACKAGE "fsim-v2.0.0-test-source")
endif()
if(NOT FSIM_SOURCE_PACKAGE MATCHES "^[A-Za-z0-9_.+-]+$")
  message(FATAL_ERROR "source package name is unsafe: ${FSIM_SOURCE_PACKAGE}")
endif()
if(FSIM_SOURCE_ONLY AND
   NOT FSIM_SOURCE_PACKAGE STREQUAL "fsim-v2.0.0-source")
  message(FATAL_ERROR
    "final source package root must be fsim-v2.0.0-source")
endif()
foreach(FSIM_RUN IN ITEMS a b)
  set(FSIM_PARENT "${FSIM_WORK_DIR}/source-${FSIM_RUN}")
  set(FSIM_ROOT "${FSIM_PARENT}/${FSIM_SOURCE_PACKAGE}")
  foreach(FSIM_RELATIVE IN LISTS FSIM_SOURCE_FILES)
    get_filename_component(FSIM_DIRECTORY "${FSIM_RELATIVE}" DIRECTORY)
    file(MAKE_DIRECTORY "${FSIM_ROOT}/${FSIM_DIRECTORY}")
    file(COPY_FILE
      "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}"
      "${FSIM_ROOT}/${FSIM_RELATIVE}" ONLY_IF_DIFFERENT)
    fsim_normalize_package_file(
      "${FSIM_ROOT}/${FSIM_RELATIVE}" "${FSIM_RELATIVE}")
  endforeach()
  set(FSIM_MANIFEST "${FSIM_WORK_DIR}/source-${FSIM_RUN}.manifest")
  fsim_write_archive_manifest(
    "${FSIM_PARENT}" "${FSIM_SOURCE_PACKAGE}" "${FSIM_SOURCE_FILES}"
    "${FSIM_MANIFEST}" FSIM_SOURCE_ARCHIVE_PATHS)
  set(FSIM_ARCHIVE "${FSIM_WORK_DIR}/${FSIM_SOURCE_PACKAGE}-${FSIM_RUN}.zip")
  fsim_create_archive(
    "${FSIM_PARENT}" "${FSIM_MANIFEST}" "${FSIM_ARCHIVE}"
    "2000-01-01T00:00:00Z" FSIM_CREATE_RESULT)
  if(NOT FSIM_CREATE_RESULT EQUAL 0)
    message(FATAL_ERROR "source archive creation failed: ${FSIM_LAST_ARCHIVE_OUTPUT}")
  endif()
endforeach()
file(SHA256 "${FSIM_WORK_DIR}/${FSIM_SOURCE_PACKAGE}-a.zip" FSIM_SOURCE_DIGEST_A)
file(SHA256 "${FSIM_WORK_DIR}/${FSIM_SOURCE_PACKAGE}-b.zip" FSIM_SOURCE_DIGEST_B)
if(NOT FSIM_SOURCE_DIGEST_A STREQUAL FSIM_SOURCE_DIGEST_B)
  message(FATAL_ERROR "repeated source package digests differ")
endif()
file(SIZE "${FSIM_WORK_DIR}/${FSIM_SOURCE_PACKAGE}-a.zip" FSIM_SOURCE_SIZE)
if(FSIM_SOURCE_SIZE GREATER 67108864)
  message(FATAL_ERROR "source package exceeds 64 MiB: ${FSIM_SOURCE_SIZE}")
endif()
fsim_list_archive(
  "${FSIM_WORK_DIR}/${FSIM_SOURCE_PACKAGE}-a.zip" FSIM_SOURCE_LIST)
fsim_require_safe_archive_paths("${FSIM_SOURCE_PACKAGE}" "${FSIM_SOURCE_LIST}")
if(NOT FSIM_SOURCE_LIST STREQUAL FSIM_SOURCE_ARCHIVE_PATHS)
  message(FATAL_ERROR "source archive entry order/content differs from manifest")
endif()

list(GET FSIM_SOURCE_ARCHIVE_PATHS 0 FSIM_FIRST_SOURCE_PATH)
fsim_expect_archive_manifest_failure(absolute "/absolute\n")
fsim_expect_archive_manifest_failure(drive "C:/escape\n")
fsim_expect_archive_manifest_failure(traversal "../escape\n")
fsim_expect_archive_manifest_failure(nested-traversal
  "${FSIM_SOURCE_PACKAGE}/../escape\n")
fsim_expect_archive_manifest_failure(backslash
  "${FSIM_SOURCE_PACKAGE}\\\\escape\n")
fsim_expect_archive_manifest_failure(double-separator
  "${FSIM_SOURCE_PACKAGE}//escape\n")
fsim_expect_archive_manifest_failure(duplicate
  "${FSIM_FIRST_SOURCE_PATH}\n${FSIM_FIRST_SOURCE_PATH}\n")
endif()

if(NOT FSIM_SOURCE_ONLY)
  string(TOLOWER "${FSIM_PLATFORM}" FSIM_PLATFORM_ID)
  string(TOLOWER "${FSIM_COMPILER}" FSIM_COMPILER_ID)
  string(REGEX REPLACE "[^a-z0-9]+" "-" FSIM_PLATFORM_ID "${FSIM_PLATFORM_ID}")
  string(REGEX REPLACE "[^a-z0-9]+" "-" FSIM_COMPILER_ID "${FSIM_COMPILER_ID}")
  if(DEFINED FSIM_BINARY_PACKAGE_NAME AND
     NOT FSIM_BINARY_PACKAGE_NAME STREQUAL "")
    set(FSIM_BINARY_PACKAGE "${FSIM_BINARY_PACKAGE_NAME}")
  else()
    set(FSIM_BINARY_PACKAGE
        "fsim-v2.0.0-test-debug-${FSIM_PLATFORM_ID}-${FSIM_COMPILER_ID}")
  endif()
  if(NOT FSIM_BINARY_PACKAGE MATCHES "^[A-Za-z0-9_.+-]+$")
    message(FATAL_ERROR "binary package name is unsafe: ${FSIM_BINARY_PACKAGE}")
  endif()
  if(FSIM_PACKAGE_FIXTURE_MODE)
    string(APPEND FSIM_BINARY_PACKAGE "-fixture")
  endif()
  foreach(FSIM_RUN IN ITEMS a b)
  set(FSIM_PARENT "${FSIM_WORK_DIR}/binary-${FSIM_RUN}")
  set(FSIM_ROOT "${FSIM_PARENT}/${FSIM_BINARY_PACKAGE}")
  if(FSIM_PACKAGE_FIXTURE_MODE)
    foreach(FSIM_DIRECTORY IN ITEMS
        bin include/fsim lib/pkgconfig share/doc/fsim
        share/fsim/examples/vertical_slice)
      file(MAKE_DIRECTORY "${FSIM_ROOT}/${FSIM_DIRECTORY}")
    endforeach()
    file(WRITE "${FSIM_ROOT}/bin/fsim" "fsim deterministic binary fixture\n")
    file(COPY_FILE "${FSIM_SOURCE_DIR}/include/fsim/api.h"
         "${FSIM_ROOT}/include/fsim/api.h")
    file(COPY_FILE "${FSIM_BINARY_DIR}/fsim.pc"
         "${FSIM_ROOT}/lib/pkgconfig/fsim.pc")
    file(COPY_FILE "${FSIM_SOURCE_DIR}/LICENSE"
         "${FSIM_ROOT}/share/doc/fsim/LICENSE")
    file(COPY_FILE "${FSIM_SOURCE_DIR}/examples/vertical_slice/fsim.toml"
         "${FSIM_ROOT}/share/fsim/examples/vertical_slice/fsim.toml")
  else()
    set(FSIM_INSTALL_COMMAND
        "${CMAKE_COMMAND}" --install "${FSIM_BINARY_DIR}" --prefix "${FSIM_ROOT}")
    if(DEFINED FSIM_CONFIG AND NOT FSIM_CONFIG STREQUAL "")
      list(APPEND FSIM_INSTALL_COMMAND --config "${FSIM_CONFIG}")
    endif()
    execute_process(
      COMMAND ${FSIM_INSTALL_COMMAND}
      RESULT_VARIABLE FSIM_INSTALL_RESULT
      OUTPUT_VARIABLE FSIM_INSTALL_OUTPUT
      ERROR_VARIABLE FSIM_INSTALL_ERROR)
    if(NOT FSIM_INSTALL_RESULT EQUAL 0)
      message(FATAL_ERROR
        "binary package staging failed with ${FSIM_INSTALL_RESULT}\n"
        "${FSIM_INSTALL_OUTPUT}${FSIM_INSTALL_ERROR}")
    endif()
  endif()
  file(GLOB_RECURSE FSIM_BINARY_FILES LIST_DIRECTORIES FALSE
       RELATIVE "${FSIM_ROOT}" "${FSIM_ROOT}/*")
  list(SORT FSIM_BINARY_FILES)
  list(LENGTH FSIM_BINARY_FILES FSIM_BINARY_COUNT)
  if(FSIM_BINARY_COUNT GREATER 5000)
    message(FATAL_ERROR "binary package exceeds 5000 entries")
  endif()
  foreach(FSIM_RELATIVE IN LISTS FSIM_BINARY_FILES)
    if(IS_SYMLINK "${FSIM_ROOT}/${FSIM_RELATIVE}")
      file(READ_SYMLINK "${FSIM_ROOT}/${FSIM_RELATIVE}" FSIM_LINK_TARGET)
      if(FSIM_LINK_TARGET STREQUAL ""
         OR IS_ABSOLUTE "${FSIM_LINK_TARGET}"
         OR FSIM_LINK_TARGET MATCHES "^[A-Za-z]:"
         OR FSIM_LINK_TARGET MATCHES "\\\\"
         OR FSIM_LINK_TARGET MATCHES "(^|/)\\.\\.?(/|$)"
         OR FSIM_LINK_TARGET MATCHES "//")
        message(FATAL_ERROR
          "binary package symlink target is unsafe: "
          "${FSIM_RELATIVE} -> ${FSIM_LINK_TARGET}")
      endif()
    else()
      fsim_normalize_package_file(
        "${FSIM_ROOT}/${FSIM_RELATIVE}" "${FSIM_RELATIVE}")
    endif()
  endforeach()
  set(FSIM_MANIFEST "${FSIM_WORK_DIR}/binary-${FSIM_RUN}.manifest")
  fsim_write_archive_manifest(
    "${FSIM_PARENT}" "${FSIM_BINARY_PACKAGE}" "${FSIM_BINARY_FILES}"
    "${FSIM_MANIFEST}" FSIM_BINARY_ARCHIVE_PATHS)
  set(FSIM_ARCHIVE "${FSIM_WORK_DIR}/${FSIM_BINARY_PACKAGE}-${FSIM_RUN}.zip")
  fsim_create_archive(
    "${FSIM_PARENT}" "${FSIM_MANIFEST}" "${FSIM_ARCHIVE}"
    "2000-01-01T00:00:00Z" FSIM_CREATE_RESULT)
  if(NOT FSIM_CREATE_RESULT EQUAL 0)
    message(FATAL_ERROR "binary archive creation failed: ${FSIM_LAST_ARCHIVE_OUTPUT}")
  endif()
  endforeach()
  file(SHA256
    "${FSIM_WORK_DIR}/${FSIM_BINARY_PACKAGE}-a.zip" FSIM_BINARY_DIGEST_A)
  file(SHA256
    "${FSIM_WORK_DIR}/${FSIM_BINARY_PACKAGE}-b.zip" FSIM_BINARY_DIGEST_B)
  if(NOT FSIM_BINARY_DIGEST_A STREQUAL FSIM_BINARY_DIGEST_B)
    message(FATAL_ERROR "repeated binary package digests differ")
  endif()
  file(SIZE "${FSIM_WORK_DIR}/${FSIM_BINARY_PACKAGE}-a.zip" FSIM_BINARY_SIZE)
  if(FSIM_BINARY_SIZE GREATER 536870912)
    message(FATAL_ERROR "binary package exceeds 512 MiB: ${FSIM_BINARY_SIZE}")
  endif()
  fsim_list_archive(
    "${FSIM_WORK_DIR}/${FSIM_BINARY_PACKAGE}-a.zip" FSIM_BINARY_LIST)
  fsim_require_safe_archive_paths("${FSIM_BINARY_PACKAGE}" "${FSIM_BINARY_LIST}")
  if(NOT FSIM_BINARY_LIST STREQUAL FSIM_BINARY_ARCHIVE_PATHS)
    message(FATAL_ERROR "binary archive entry order/content differs from manifest")
  endif()
  if(DEFINED FSIM_BINARY_ARCHIVE_OUTPUT AND
     NOT FSIM_BINARY_ARCHIVE_OUTPUT STREQUAL "")
    get_filename_component(
      FSIM_BINARY_ARCHIVE_OUTPUT_NAME "${FSIM_BINARY_ARCHIVE_OUTPUT}" NAME)
    if(NOT FSIM_BINARY_ARCHIVE_OUTPUT_NAME STREQUAL
       "${FSIM_BINARY_PACKAGE}.zip")
      message(FATAL_ERROR
        "binary artifact name does not match package root: "
        "${FSIM_BINARY_ARCHIVE_OUTPUT_NAME}/${FSIM_BINARY_PACKAGE}")
    endif()
    get_filename_component(
      FSIM_BINARY_ARCHIVE_OUTPUT_DIR "${FSIM_BINARY_ARCHIVE_OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${FSIM_BINARY_ARCHIVE_OUTPUT_DIR}")
    file(COPY_FILE
      "${FSIM_WORK_DIR}/${FSIM_BINARY_PACKAGE}-a.zip"
      "${FSIM_BINARY_ARCHIVE_OUTPUT}")
    file(SHA256 "${FSIM_BINARY_ARCHIVE_OUTPUT}" FSIM_OUTPUT_BINARY_DIGEST)
    if(NOT FSIM_OUTPUT_BINARY_DIGEST STREQUAL FSIM_BINARY_DIGEST_A)
      message(FATAL_ERROR "published binary archive digest changed during copy")
    endif()
  endif()
endif()

if(NOT FSIM_BINARY_ONLY)
set(FSIM_REVERSED "${FSIM_SOURCE_ARCHIVE_PATHS}")
list(REVERSE FSIM_REVERSED)
set(FSIM_REVERSED_CONTENT)
foreach(FSIM_PATH IN LISTS FSIM_REVERSED)
  string(APPEND FSIM_REVERSED_CONTENT "${FSIM_PATH}\n")
endforeach()
file(WRITE "${FSIM_WORK_DIR}/source-reversed.manifest" "${FSIM_REVERSED_CONTENT}")
fsim_create_archive(
  "${FSIM_WORK_DIR}/source-a" "${FSIM_WORK_DIR}/source-reversed.manifest"
  "${FSIM_WORK_DIR}/invalid-order.zip" "2000-01-01T00:00:00Z"
  FSIM_REVERSED_RESULT)
if(FSIM_REVERSED_RESULT EQUAL 0)
  message(FATAL_ERROR "out-of-order archive manifest was accepted")
endif()

fsim_create_archive(
  "${FSIM_WORK_DIR}/source-a" "${FSIM_WORK_DIR}/source-a.manifest"
  "${FSIM_WORK_DIR}/changed-mtime.zip" "2001-01-01T00:00:00Z"
  FSIM_MTIME_RESULT)
if(NOT FSIM_MTIME_RESULT EQUAL 0)
  message(FATAL_ERROR "changed-mtime negative could not be created")
endif()
file(SHA256 "${FSIM_WORK_DIR}/changed-mtime.zip" FSIM_MTIME_DIGEST)
if(FSIM_MTIME_DIGEST STREQUAL FSIM_SOURCE_DIGEST_A)
  message(FATAL_ERROR "changed archive timestamp did not change package identity")
endif()

file(COPY_FILE
  "${FSIM_WORK_DIR}/${FSIM_SOURCE_PACKAGE}-a.zip"
  "${FSIM_WORK_DIR}/corrupt-source.zip")
file(APPEND "${FSIM_WORK_DIR}/corrupt-source.zip" "FSIM-CORRUPTION")
file(SHA256 "${FSIM_WORK_DIR}/corrupt-source.zip" FSIM_CORRUPT_DIGEST)
if(FSIM_CORRUPT_DIGEST STREQUAL FSIM_SOURCE_DIGEST_A)
  message(FATAL_ERROR "corrupted source archive retained the expected digest")
endif()

set(FSIM_EXTRACT "${FSIM_WORK_DIR}/extracted")
file(MAKE_DIRECTORY "${FSIM_EXTRACT}")
file(ARCHIVE_EXTRACT
  INPUT "${FSIM_WORK_DIR}/${FSIM_SOURCE_PACKAGE}-a.zip"
  DESTINATION "${FSIM_EXTRACT}")
foreach(FSIM_REQUIRED_PATH IN ITEMS CMakeLists.txt LICENSE cmake/fsim.pc.in)
  if(NOT EXISTS "${FSIM_EXTRACT}/${FSIM_SOURCE_PACKAGE}/${FSIM_REQUIRED_PATH}")
    message(FATAL_ERROR
      "extracted source package omits ${FSIM_REQUIRED_PATH}")
  endif()
endforeach()

set(FSIM_EXTRACTED_SOURCE "${FSIM_EXTRACT}/${FSIM_SOURCE_PACKAGE}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${FSIM_EXTRACTED_SOURCE}"
    -P "${FSIM_EXTRACTED_SOURCE}/cmake/CheckSourcePackageManifest.cmake"
  RESULT_VARIABLE FSIM_RECONSTRUCT_RESULT
  OUTPUT_VARIABLE FSIM_RECONSTRUCT_OUTPUT
  ERROR_VARIABLE FSIM_RECONSTRUCT_ERROR)
if(NOT FSIM_RECONSTRUCT_RESULT EQUAL 0)
  message(FATAL_ERROR
    "offline extracted-source manifest reconstruction failed with "
    "${FSIM_RECONSTRUCT_RESULT}\n"
    "${FSIM_RECONSTRUCT_OUTPUT}${FSIM_RECONSTRUCT_ERROR}")
endif()

set(FSIM_OFFLINE_BUILD "${FSIM_WORK_DIR}/offline-configure")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${FSIM_EXTRACTED_SOURCE}"
    -B "${FSIM_OFFLINE_BUILD}"
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTING=OFF
    -DFSIM_BUILD_TESTS=OFF
    -DFSIM_LLVM_MODE=OFF
    -DFSIM_TCL_MODE=OFF
    -DFSIM_UVM_SOURCE_MODE=OFF
    -DFSIM_WARNINGS_AS_ERRORS=ON
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
  RESULT_VARIABLE FSIM_OFFLINE_CONFIGURE_RESULT
  OUTPUT_VARIABLE FSIM_OFFLINE_CONFIGURE_OUTPUT
  ERROR_VARIABLE FSIM_OFFLINE_CONFIGURE_ERROR)
if(NOT FSIM_OFFLINE_CONFIGURE_RESULT EQUAL 0)
  message(FATAL_ERROR
    "offline extracted-source configure failed with "
    "${FSIM_OFFLINE_CONFIGURE_RESULT}\n"
    "${FSIM_OFFLINE_CONFIGURE_OUTPUT}${FSIM_OFFLINE_CONFIGURE_ERROR}")
endif()

if(DEFINED FSIM_SOURCE_ARCHIVE_OUTPUT AND
   NOT FSIM_SOURCE_ARCHIVE_OUTPUT STREQUAL "")
  get_filename_component(
    FSIM_SOURCE_ARCHIVE_OUTPUT_NAME "${FSIM_SOURCE_ARCHIVE_OUTPUT}" NAME)
  if(FSIM_SOURCE_ONLY AND
     NOT FSIM_SOURCE_ARCHIVE_OUTPUT_NAME STREQUAL "fsim-v2.0.0-source.zip")
    message(FATAL_ERROR
      "final source artifact must be fsim-v2.0.0-source.zip")
  endif()
  get_filename_component(
    FSIM_SOURCE_ARCHIVE_OUTPUT_DIR "${FSIM_SOURCE_ARCHIVE_OUTPUT}" DIRECTORY)
  file(MAKE_DIRECTORY "${FSIM_SOURCE_ARCHIVE_OUTPUT_DIR}")
  file(COPY_FILE
    "${FSIM_WORK_DIR}/${FSIM_SOURCE_PACKAGE}-a.zip"
    "${FSIM_SOURCE_ARCHIVE_OUTPUT}")
  file(SHA256 "${FSIM_SOURCE_ARCHIVE_OUTPUT}" FSIM_OUTPUT_SOURCE_DIGEST)
  if(NOT FSIM_OUTPUT_SOURCE_DIGEST STREQUAL FSIM_SOURCE_DIGEST_A)
    message(FATAL_ERROR "published source archive digest changed during copy")
  endif()
endif()

if(FSIM_SOURCE_ONLY)
  file(WRITE "${FSIM_WORK_DIR}/result.tsv"
    "kind\tentries\tbytes\tsha256\n"
    "source\t${FSIM_SOURCE_COUNT}\t${FSIM_SOURCE_SIZE}\t${FSIM_SOURCE_DIGEST_A}\n")
  message(STATUS
    "deterministic source packaging: source=${FSIM_SOURCE_COUNT}/${FSIM_SOURCE_SIZE}/${FSIM_SOURCE_DIGEST_A}; safe extraction and offline reconstruction passed")
  return()
endif()
endif()

string(TOLOWER "${FSIM_CONFIG}" FSIM_BINARY_CONFIGURATION_ID)
if(FSIM_BINARY_CONFIGURATION_ID STREQUAL "")
  set(FSIM_BINARY_CONFIGURATION_ID "unknown")
endif()
string(REGEX REPLACE "[^a-z0-9]+" "-" FSIM_BINARY_CONFIGURATION_ID
  "${FSIM_BINARY_CONFIGURATION_ID}")
set(FSIM_BINARY_KIND "binary-${FSIM_BINARY_CONFIGURATION_ID}")
if(FSIM_PACKAGE_FIXTURE_MODE)
  string(APPEND FSIM_BINARY_KIND "-fixture")
endif()
if(FSIM_BINARY_ONLY)
  file(WRITE "${FSIM_WORK_DIR}/result.tsv"
    "kind\tentries\tbytes\tsha256\n"
    "${FSIM_BINARY_KIND}\t${FSIM_BINARY_COUNT}\t${FSIM_BINARY_SIZE}\t${FSIM_BINARY_DIGEST_A}\n")
  message(STATUS
    "deterministic binary packaging: binary=${FSIM_BINARY_COUNT}/${FSIM_BINARY_SIZE}/${FSIM_BINARY_DIGEST_A}; safe path and symlink preflight passed")
else()
  file(WRITE "${FSIM_WORK_DIR}/result.tsv"
    "kind\tentries\tbytes\tsha256\n"
    "source\t${FSIM_SOURCE_COUNT}\t${FSIM_SOURCE_SIZE}\t${FSIM_SOURCE_DIGEST_A}\n"
    "${FSIM_BINARY_KIND}\t${FSIM_BINARY_COUNT}\t${FSIM_BINARY_SIZE}\t${FSIM_BINARY_DIGEST_A}\n")
  message(STATUS
    "deterministic packaging: source=${FSIM_SOURCE_COUNT}/${FSIM_SOURCE_SIZE}/${FSIM_SOURCE_DIGEST_A} binary=${FSIM_BINARY_COUNT}/${FSIM_BINARY_SIZE}/${FSIM_BINARY_DIGEST_A}")
endif()

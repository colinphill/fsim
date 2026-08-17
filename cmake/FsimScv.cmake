# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

set(FSIM_SCV_SOURCE_SCHEMA "fsim-scv-accellera-source-v1")
set(FSIM_SCV_VERSION "2.0.1")
set(FSIM_SCV_RELEASE_DATE "2017-12-08")
set(FSIM_SCV_RELEASE_ID "Accellera-download-2017-12-08")
set(FSIM_SCV_UPSTREAM_URL
    "https://www.accellera.org/images/downloads/standards/systemc/scv-2.0.1.tar.gz")
set(FSIM_SCV_ARCHIVE_NAME "scv-2.0.1.tar.gz")
set(FSIM_SCV_ARCHIVE_SIZE 2835735)
set(FSIM_SCV_ARCHIVE_SHA256
    "7bd1c4037f3c108d02f45cae003d112efdb788d469cb029fada247d330ca4881")
set(FSIM_SCV_SOURCE_ROOT "scv-2.0.1")
set(FSIM_SCV_TREE_FILE_COUNT 578)
set(FSIM_SCV_TREE_SHA256
    "85cc2e4e3ee1893878de2567f253003859c847a151f080e48f185825c34f933c")
set(FSIM_SCV_LICENSE_EXPRESSION "Apache-2.0")
set(FSIM_SCV_SBOM_PURL
    "pkg:generic/scv@2.0.1?download_url=https%3A%2F%2Fwww.accellera.org%2Fimages%2Fdownloads%2Fstandards%2Fsystemc%2Fscv-2.0.1.tar.gz")
set(FSIM_SCV_PATCH_SCHEMA "fsim-scv-patch-set-v1")
set(FSIM_SCV_PATCH_COUNT 4)
set(FSIM_SCV_PATCH_MANIFEST_SHA256
    "61f2a7a414b317bba1f566bfd855c0ae1329c6f848d8bf5fa22b584c2f71969a")
set(FSIM_SCV_PATCHED_TREE_SHA256
    "e7590f83e157e7df3362c9987ccc55b6b50328d51ec809c489e9ab1b2f80c598")
set(FSIM_SCV_BAG_PATCH_SHA256
    "f931c715608578638a7d49c88d20a9d323aa836023cac0af64c078c21f3a39fb")
set(FSIM_SCV_BAG_INPUT_SHA256
    "ef2eacb6f536c3d06e78f83b853525a83a01cc71a13cd9688d0c01d41c858dad")
set(FSIM_SCV_BAG_OUTPUT_SHA256
    "c1d917666f20f553f03fe7d3af51c76c950651e194aab223053287cf32f80dd6")
set(FSIM_SCV_NESTED_EXTENSION_PATCH_SHA256
    "3b098f1500ad07f4e57f23c8e608ea18d39fedcdb38660318dabbc0b22ef1105")
set(FSIM_SCV_NESTED_EXTENSION_INPUT_SHA256
    "b970da079e8b103d83acc9114ac592b6f8e44c668a63d07bbffa4831942289ae")
set(FSIM_SCV_NESTED_EXTENSION_OUTPUT_SHA256
    "8b17414a6495c541a2d351a485b9320b4704c73016fdac280846f778da6f4a77")
set(FSIM_SCV_INT_RANGE_PATCH_SHA256
    "3d46addd533b114bd33ddf06bb6f557b1eec28c3d82a7bb1a9d90797021d7492")
set(FSIM_SCV_INT_RANGE_INPUT_SHA256
    "d50879a72da809902148a30834028aa0d81091d92395dc4a2ce977c12c7107e0")
set(FSIM_SCV_INT_RANGE_OUTPUT_SHA256
    "d7aaaceeb9110159cab65c78e11dbfbb52d25ad920dafc6aaf195074448ac44a")
set(FSIM_SCV_RANGE_SIZE_PATCH_SHA256
    "9303202ba36d8deb38aad33226dea17c96198f14061eb21ad83e9358fdfc5f5a")
set(FSIM_SCV_RANGE_SIZE_INPUT_SHA256
    "5e0407c4076e2c9d6d7644ab1d75d3a64d5f209e205bef1b06b2f19e13ebbef1")
set(FSIM_SCV_RANGE_SIZE_OUTPUT_SHA256
    "00f3691467099f783716d0a2267cd76a5cdf08f998213f4e7b7c7ba0a0274a73")
set(
  FSIM_SCV_REQUIRED_FILES
  "LICENSE|4b4fe282d05e6f3f63e36b124565a5d07727e84bc32fe30e2937ca0f84a34601"
  "NOTICE|6b6ca891607a6d5e59b4af51edd5a2a486e414b115e0d5358ee78e906c31ac0f"
  "configure.ac|8943cd55723abdd7191412c12dfcf8a90b98eb92a5f066bbb8111baec2f3bf6d"
  "src/scv.h|75768fd42f73b5cf4aaf8393aa5a16d10f473aea2eef8d69ed7dc2315323dad7"
  "src/scv/scv_ver.h|e4c15e67681d1a126d303906836ec6957cc02fe38439bece29f19a661332304e"
  "RELEASENOTES|89a64e447af2226cb574c1c184005ea0189534ffd6243cc51c9b7e72eb1525ea"
)

function(fsim_scv_validate_source_metadata)
  foreach(variable IN ITEMS
      FSIM_SCV_SOURCE_SCHEMA FSIM_SCV_VERSION FSIM_SCV_RELEASE_DATE
      FSIM_SCV_RELEASE_ID FSIM_SCV_UPSTREAM_URL FSIM_SCV_ARCHIVE_NAME
      FSIM_SCV_ARCHIVE_SIZE FSIM_SCV_ARCHIVE_SHA256 FSIM_SCV_SOURCE_ROOT
      FSIM_SCV_TREE_FILE_COUNT FSIM_SCV_TREE_SHA256
      FSIM_SCV_LICENSE_EXPRESSION FSIM_SCV_SBOM_PURL
      FSIM_SCV_REQUIRED_FILES)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
      message(FATAL_ERROR "missing governed SCV metadata: ${variable}")
    endif()
  endforeach()
  if(NOT FSIM_SCV_VERSION STREQUAL "2.0.1" OR
     NOT FSIM_SCV_SOURCE_ROOT STREQUAL "scv-2.0.1" OR
     NOT FSIM_SCV_ARCHIVE_NAME STREQUAL "scv-2.0.1.tar.gz")
    message(FATAL_ERROR "governed SCV release identity is not 2.0.1")
  endif()
  if(NOT FSIM_SCV_UPSTREAM_URL MATCHES
       "^https://www\\.accellera\\.org/images/downloads/standards/systemc/")
    message(FATAL_ERROR "SCV source URL is not the official Accellera download")
  endif()
  foreach(digest IN ITEMS
      "${FSIM_SCV_ARCHIVE_SHA256}" "${FSIM_SCV_TREE_SHA256}")
    if(NOT digest MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "invalid SCV provenance digest: ${digest}")
    endif()
    string(LENGTH "${digest}" digest_length)
    if(NOT digest_length EQUAL 64)
      message(FATAL_ERROR "invalid SCV provenance digest length")
    endif()
  endforeach()
  if(NOT FSIM_SCV_ARCHIVE_SIZE MATCHES "^[1-9][0-9]*$" OR
     NOT FSIM_SCV_TREE_FILE_COUNT MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "invalid SCV archive or tree size")
  endif()
  list(LENGTH FSIM_SCV_REQUIRED_FILES required_count)
  if(NOT required_count EQUAL 6)
    message(FATAL_ERROR "expected six governed SCV source entry points")
  endif()
  foreach(entry IN LISTS FSIM_SCV_REQUIRED_FILES)
    string(REPLACE "|" ";" fields "${entry}")
    list(LENGTH fields field_count)
    list(GET fields 0 relative_path)
    list(GET fields 1 digest)
    string(LENGTH "${digest}" digest_length)
    if(NOT field_count EQUAL 2 OR
       relative_path MATCHES "(^/|(^|/)\\.\\.(/|$))" OR
       NOT digest MATCHES "^[0-9a-f]+$" OR NOT digest_length EQUAL 64)
      message(FATAL_ERROR "unsafe or malformed SCV source identity: ${entry}")
    endif()
  endforeach()
endfunction()

function(fsim_scv_archive_error archive output_error)
  set(error "")
  if(NOT EXISTS "${archive}")
    set(error "official SCV 2.0.1 archive is missing: ${archive}")
  else()
    file(SIZE "${archive}" actual_size)
    if(NOT actual_size EQUAL FSIM_SCV_ARCHIVE_SIZE)
      set(error
          "SCV archive size mismatch: expected ${FSIM_SCV_ARCHIVE_SIZE}, found ${actual_size}")
    else()
      file(SHA256 "${archive}" actual_digest)
      if(NOT actual_digest STREQUAL FSIM_SCV_ARCHIVE_SHA256)
        set(error "SCV archive SHA-256 mismatch: ${actual_digest}")
      endif()
    endif()
  endif()
  set(${output_error} "${error}" PARENT_SCOPE)
endfunction()

function(fsim_scv_validate_archive archive)
  fsim_scv_validate_source_metadata()
  fsim_scv_archive_error("${archive}" error)
  if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}")
  endif()
endfunction()

function(fsim_scv_compute_tree_identity root output_count output_digest)
  if(NOT IS_DIRECTORY "${root}")
    message(FATAL_ERROR "SCV source root is not a directory: ${root}")
  endif()
  file(GLOB_RECURSE files LIST_DIRECTORIES FALSE RELATIVE "${root}" "${root}/*")
  list(SORT files)
  set(canonical "")
  foreach(relative_path IN LISTS files)
    if(relative_path MATCHES "(^|/)\\.\\.(/|$)")
      message(FATAL_ERROR "unsafe extracted SCV path: ${relative_path}")
    endif()
    file(SHA256 "${root}/${relative_path}" digest)
    string(APPEND canonical "${digest}  ${relative_path}\n")
  endforeach()
  list(LENGTH files file_count)
  string(SHA256 tree_digest "${canonical}")
  set(${output_count} "${file_count}" PARENT_SCOPE)
  set(${output_digest} "${tree_digest}" PARENT_SCOPE)
endfunction()

function(fsim_scv_source_tree_error root output_error)
  set(error "")
  get_filename_component(root_name "${root}" NAME)
  if(NOT root_name STREQUAL FSIM_SCV_SOURCE_ROOT)
    set(error
        "wrong SCV source version/root: expected ${FSIM_SCV_SOURCE_ROOT}, found ${root_name}")
  elseif(NOT IS_DIRECTORY "${root}")
    set(error "SCV source root is missing: ${root}")
  else()
    foreach(entry IN LISTS FSIM_SCV_REQUIRED_FILES)
      string(REPLACE "|" ";" fields "${entry}")
      list(GET fields 0 relative_path)
      list(GET fields 1 expected_digest)
      if(NOT EXISTS "${root}/${relative_path}")
        set(error "incomplete SCV source tree omits ${relative_path}")
        break()
      endif()
      file(SHA256 "${root}/${relative_path}" actual_digest)
      if(NOT actual_digest STREQUAL expected_digest)
        set(error "SCV source entry changed: ${relative_path}")
        break()
      endif()
    endforeach()
    if(error STREQUAL "")
      fsim_scv_compute_tree_identity("${root}" actual_count actual_digest)
      if(NOT actual_count EQUAL FSIM_SCV_TREE_FILE_COUNT OR
         NOT actual_digest STREQUAL FSIM_SCV_TREE_SHA256)
        set(error
            "SCV source tree mismatch: files=${actual_count}, SHA256=${actual_digest}")
      endif()
    endif()
  endif()
  set(${output_error} "${error}" PARENT_SCOPE)
endfunction()

function(fsim_scv_validate_source_tree root)
  fsim_scv_validate_source_metadata()
  fsim_scv_source_tree_error("${root}" error)
  if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}")
  endif()
endfunction()

function(fsim_scv_work_root_error work_root output_root output_error)
  if(work_root STREQUAL "")
    set(${output_root} "" PARENT_SCOPE)
    set(${output_error} "the SCV work root is empty" PARENT_SCOPE)
    return()
  endif()
  if(DEFINED FSIM_BINARY_DIR AND NOT FSIM_BINARY_DIR STREQUAL "")
    set(binary_root "${FSIM_BINARY_DIR}")
  else()
    set(binary_root "${CMAKE_BINARY_DIR}")
  endif()
  set(candidate "${work_root}")
  cmake_path(ABSOLUTE_PATH candidate BASE_DIRECTORY "${binary_root}"
             NORMALIZE OUTPUT_VARIABLE normalized)
  set(error "")
  if(DEFINED FSIM_SOURCE_DIR AND NOT FSIM_SOURCE_DIR STREQUAL "")
    set(source_root "${FSIM_SOURCE_DIR}")
    cmake_path(NORMAL_PATH source_root OUTPUT_VARIABLE source_root)
    cmake_path(IS_PREFIX source_root "${normalized}" NORMALIZE inside_source)
    cmake_path(IS_PREFIX binary_root "${normalized}" NORMALIZE inside_binary)
    if(inside_source AND NOT inside_binary)
      set(error "the SCV work root must remain outside the source tree")
    endif()
  endif()
  set(${output_root} "${normalized}" PARENT_SCOPE)
  set(${output_error} "${error}" PARENT_SCOPE)
endfunction()

function(fsim_scv_manifest_contents root archive output_contents)
  fsim_scv_compute_tree_identity("${root}" tree_count tree_digest)
  set(contents "schema=${FSIM_SCV_SOURCE_SCHEMA}\n")
  string(APPEND contents "name=SCV\nversion=${FSIM_SCV_VERSION}\n")
  string(APPEND contents "release_date=${FSIM_SCV_RELEASE_DATE}\n")
  string(APPEND contents "release_id=${FSIM_SCV_RELEASE_ID}\n")
  string(APPEND contents "upstream_url=${FSIM_SCV_UPSTREAM_URL}\n")
  string(APPEND contents "archive=${archive}\n")
  string(APPEND contents "archive_size=${FSIM_SCV_ARCHIVE_SIZE}\n")
  string(APPEND contents "archive_sha256=${FSIM_SCV_ARCHIVE_SHA256}\n")
  string(APPEND contents "source_root=${root}\n")
  string(APPEND contents "tree_files=${tree_count}\ntree_sha256=${tree_digest}\n")
  string(APPEND contents "license=${FSIM_SCV_LICENSE_EXPRESSION}\n")
  string(APPEND contents "purl=${FSIM_SCV_SBOM_PURL}\n")
  foreach(entry IN LISTS FSIM_SCV_REQUIRED_FILES)
    string(APPEND contents "entry=${entry}\n")
  endforeach()
  set(${output_contents} "${contents}" PARENT_SCOPE)
endfunction()

function(fsim_scv_materialize_source archive work_root output_root output_manifest)
  fsim_scv_validate_archive("${archive}")
  fsim_scv_work_root_error("${work_root}" normalized_work_root error)
  if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}: ${work_root}")
  endif()
  set(source_parent "${normalized_work_root}/sources")
  set(root "${source_parent}/${FSIM_SCV_SOURCE_ROOT}")
  if(NOT EXISTS "${root}")
    set(staging "${normalized_work_root}/extract-${FSIM_SCV_ARCHIVE_SHA256}")
    file(REMOVE_RECURSE "${staging}")
    file(MAKE_DIRECTORY "${staging}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${staging}")
    fsim_scv_validate_source_tree("${staging}/${FSIM_SCV_SOURCE_ROOT}")
    file(MAKE_DIRECTORY "${source_parent}")
    file(RENAME "${staging}/${FSIM_SCV_SOURCE_ROOT}" "${root}")
    file(REMOVE_RECURSE "${staging}")
  endif()
  fsim_scv_validate_source_tree("${root}")
  file(MAKE_DIRECTORY "${normalized_work_root}/manifests")
  set(manifest "${normalized_work_root}/manifests/scv-2.0.1.txt")
  fsim_scv_manifest_contents("${root}" "${archive}" contents)
  file(WRITE "${manifest}" "${contents}")
  set(${output_root} "${root}" PARENT_SCOPE)
  set(${output_manifest} "${manifest}" PARENT_SCOPE)
endfunction()

function(fsim_scv_patch_governance_error root manifest output_error)
  set(error "")
  fsim_scv_source_tree_error("${root}" source_error)
  if(NOT source_error STREQUAL "")
    set(error "SCV patch input is not the governed pristine tree: ${source_error}")
  elseif(NOT EXISTS "${manifest}")
    set(error "SCV patch manifest is missing: ${manifest}")
  else()
    file(SHA256 "${manifest}" manifest_digest)
    if(NOT manifest_digest STREQUAL FSIM_SCV_PATCH_MANIFEST_SHA256)
      set(error "SCV patch manifest SHA-256 mismatch: ${manifest_digest}")
    else()
      file(READ "${manifest}" manifest_text)
      foreach(token IN ITEMS
          "schema=${FSIM_SCV_PATCH_SCHEMA}"
          "upstream_version=${FSIM_SCV_VERSION}"
          "upstream_archive_sha256=${FSIM_SCV_ARCHIVE_SHA256}"
          "upstream_tree_sha256=${FSIM_SCV_TREE_SHA256}"
          "patch_count=${FSIM_SCV_PATCH_COUNT}"
          "patch=scv-bag-mutable-random|${FSIM_SCV_BAG_PATCH_SHA256}|src/scv/scv_bag.h|${FSIM_SCV_BAG_INPUT_SHA256}|${FSIM_SCV_BAG_OUTPUT_SHA256}"
          "patch=scv-int-range-overflow|${FSIM_SCV_INT_RANGE_PATCH_SHA256}|src/scv/scv_constraint.cpp|${FSIM_SCV_INT_RANGE_INPUT_SHA256}|${FSIM_SCV_INT_RANGE_OUTPUT_SHA256}"
          "patch=scv-range-size-overflow|${FSIM_SCV_RANGE_SIZE_PATCH_SHA256}|src/scv/scv_constraint_range.cpp|${FSIM_SCV_RANGE_SIZE_INPUT_SHA256}|${FSIM_SCV_RANGE_SIZE_OUTPUT_SHA256}"
          "patch=scv-nested-extension-constructors|${FSIM_SCV_NESTED_EXTENSION_PATCH_SHA256}|src/scv/_scv_introspection.h|${FSIM_SCV_NESTED_EXTENSION_INPUT_SHA256}|${FSIM_SCV_NESTED_EXTENSION_OUTPUT_SHA256}"
          "unmodified_linux_result=shared-and-static-library-build-pass"
          "llvm22_configure_result=blocked-before-source-compilation"
          "gcc13_cxx20_result=blocked-by-template-id-constructor-spelling"
          "decision=external-cmake-adapter-with-four-generated-source-patches"
          "positive_probe=patched-exact-tree-builds-with-clang-and-gcc-and-preserves-const-peek-randomization-nested-extension-construction-full-width-int-randomization-and-overflow-safe-interval-sizing"
          "negative_probe=changed-manifest-patch-input-output-or-source-tree-rejected-before-build"
          "removal_criteria=")
        string(FIND "${manifest_text}" "${token}" token_index)
        if(token_index EQUAL -1)
          set(error "SCV patch manifest omits ${token}")
          break()
        endif()
      endforeach()
    endif()
  endif()
  if(error STREQUAL "")
    get_filename_component(manifest_root "${manifest}" DIRECTORY)
    file(GLOB patch_files LIST_DIRECTORIES FALSE
         "${manifest_root}/patches/*.patch")
    list(LENGTH patch_files patch_count)
    if(NOT patch_count EQUAL FSIM_SCV_PATCH_COUNT)
      set(error
          "SCV patch count mismatch: expected ${FSIM_SCV_PATCH_COUNT}, found ${patch_count}")
    else()
      set(bag_patch "${manifest_root}/patches/scv-bag-mutable-random.patch")
      set(int_range_patch
          "${manifest_root}/patches/scv-int-range-overflow.patch")
      set(range_size_patch
          "${manifest_root}/patches/scv-range-size-overflow.patch")
      set(nested_extension_patch
          "${manifest_root}/patches/scv-nested-extension-constructors.patch")
      if(NOT EXISTS "${bag_patch}" OR NOT EXISTS "${int_range_patch}" OR
         NOT EXISTS "${range_size_patch}" OR
         NOT EXISTS "${nested_extension_patch}")
        set(error "SCV compatibility patch set is incomplete")
      else()
        file(SHA256 "${bag_patch}" bag_patch_digest)
        if(NOT bag_patch_digest STREQUAL FSIM_SCV_BAG_PATCH_SHA256)
          set(error "SCV bag compatibility patch SHA-256 mismatch")
        endif()
        file(SHA256 "${int_range_patch}" int_range_patch_digest)
        if(NOT int_range_patch_digest STREQUAL FSIM_SCV_INT_RANGE_PATCH_SHA256)
          set(error "SCV int-range compatibility patch SHA-256 mismatch")
        endif()
        file(SHA256 "${range_size_patch}" range_size_patch_digest)
        if(NOT range_size_patch_digest STREQUAL
           FSIM_SCV_RANGE_SIZE_PATCH_SHA256)
          set(error "SCV range-size compatibility patch SHA-256 mismatch")
        endif()
        file(SHA256 "${nested_extension_patch}" nested_extension_patch_digest)
        if(NOT nested_extension_patch_digest STREQUAL
           FSIM_SCV_NESTED_EXTENSION_PATCH_SHA256)
          set(error "SCV nested-extension compatibility patch SHA-256 mismatch")
        endif()
      endif()
    endif()
  endif()
  set(${output_error} "${error}" PARENT_SCOPE)
endfunction()

function(fsim_scv_validate_patch_governance root manifest)
  fsim_scv_validate_source_metadata()
  fsim_scv_patch_governance_error("${root}" "${manifest}" error)
  if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}")
  endif()
endfunction()

function(fsim_scv_apply_governed_patches root manifest output_root)
  fsim_scv_validate_patch_governance("${root}" "${manifest}")
  get_filename_component(source_parent "${root}" DIRECTORY)
  get_filename_component(work_root "${source_parent}" DIRECTORY)
  set(patched_root
      "${work_root}/patched-${FSIM_SCV_PATCH_MANIFEST_SHA256}/${FSIM_SCV_SOURCE_ROOT}")
  if(EXISTS "${patched_root}")
    fsim_scv_compute_tree_identity(
      "${patched_root}" patched_count patched_digest)
    if(patched_count EQUAL FSIM_SCV_TREE_FILE_COUNT AND
       patched_digest STREQUAL FSIM_SCV_PATCHED_TREE_SHA256)
      set(${output_root} "${patched_root}" PARENT_SCOPE)
      return()
    endif()
    file(REMOVE_RECURSE "${patched_root}")
  endif()
  file(MAKE_DIRECTORY "${patched_root}")
  file(COPY "${root}/" DESTINATION "${patched_root}")
  set(bag_header "${patched_root}/src/scv/scv_bag.h")
  file(SHA256 "${bag_header}" bag_input_digest)
  if(NOT bag_input_digest STREQUAL FSIM_SCV_BAG_INPUT_SHA256)
    message(FATAL_ERROR "SCV bag patch input SHA-256 mismatch")
  endif()
  file(READ "${bag_header}" bag_contents)
  set(old_declaration "  scv_random* _randomP;")
  set(new_declaration "  mutable scv_random* _randomP;")
  string(FIND "${bag_contents}" "${old_declaration}" declaration_offset)
  if(declaration_offset EQUAL -1)
    message(FATAL_ERROR "SCV bag patch anchor is missing")
  endif()
  string(REPLACE "${old_declaration}" "${new_declaration}"
         bag_patched_contents "${bag_contents}")
  if(bag_patched_contents STREQUAL bag_contents)
    message(FATAL_ERROR "SCV bag compatibility patch made no change")
  endif()
  file(CONFIGURE OUTPUT "${bag_header}" CONTENT "${bag_patched_contents}"
       @ONLY NEWLINE_STYLE LF)
  file(SHA256 "${bag_header}" bag_output_digest)
  if(NOT bag_output_digest STREQUAL FSIM_SCV_BAG_OUTPUT_SHA256)
    message(FATAL_ERROR "SCV bag patch output SHA-256 mismatch")
  endif()
  set(nested_extension_header
      "${patched_root}/src/scv/_scv_introspection.h")
  file(SHA256 "${nested_extension_header}" nested_extension_input_digest)
  if(NOT nested_extension_input_digest STREQUAL
     FSIM_SCV_NESTED_EXTENSION_INPUT_SHA256)
    message(FATAL_ERROR "SCV nested-extension patch input SHA-256 mismatch")
  endif()
  file(READ "${nested_extension_header}" nested_extension_contents)
  set(old_default_constructor
      "  scv_extensions< scv_extensions<T> > () {}")
  set(new_default_constructor "  scv_extensions() {}")
  set(old_copy_constructor
      "  scv_extensions< scv_extensions<T> > (const scv_extensions<T>& rhs) : scv_extensions<T>(rhs) {}")
  set(new_copy_constructor
      "  scv_extensions(const scv_extensions<T>& rhs) : scv_extensions<T>(rhs) {}")
  foreach(anchor IN ITEMS "${old_default_constructor}" "${old_copy_constructor}")
    string(FIND "${nested_extension_contents}" "${anchor}" anchor_offset)
    if(anchor_offset EQUAL -1)
      message(FATAL_ERROR "SCV nested-extension patch anchor is missing")
    endif()
  endforeach()
  string(REPLACE "${old_default_constructor}" "${new_default_constructor}"
         nested_extension_contents "${nested_extension_contents}")
  string(REPLACE "${old_copy_constructor}" "${new_copy_constructor}"
         nested_extension_contents "${nested_extension_contents}")
  file(CONFIGURE OUTPUT "${nested_extension_header}"
       CONTENT "${nested_extension_contents}" @ONLY NEWLINE_STYLE LF)
  file(SHA256 "${nested_extension_header}" nested_extension_output_digest)
  if(NOT nested_extension_output_digest STREQUAL
     FSIM_SCV_NESTED_EXTENSION_OUTPUT_SHA256)
    message(FATAL_ERROR "SCV nested-extension patch output SHA-256 mismatch")
  endif()
  set(int_range_source "${patched_root}/src/scv/scv_constraint.cpp")
  file(SHA256 "${int_range_source}" int_range_input_digest)
  if(NOT int_range_input_digest STREQUAL FSIM_SCV_INT_RANGE_INPUT_SHA256)
    message(FATAL_ERROR "SCV int-range patch input SHA-256 mismatch")
  endif()
  file(READ "${int_range_source}" int_range_contents)
  set(old_int_range
      "    int ub = (0x1 << (s->get_bitwidth()-1) ) -1;")
  set(new_int_range
      "    int ub = (0x1U << (s->get_bitwidth()-1) ) -1U;")
  string(FIND "${int_range_contents}" "${old_int_range}" int_range_offset)
  if(int_range_offset EQUAL -1)
    message(FATAL_ERROR "SCV int-range patch anchor is missing")
  endif()
  string(REPLACE "${old_int_range}" "${new_int_range}"
         int_range_contents "${int_range_contents}")
  file(CONFIGURE OUTPUT "${int_range_source}" CONTENT "${int_range_contents}"
       @ONLY NEWLINE_STYLE LF)
  file(SHA256 "${int_range_source}" int_range_output_digest)
  if(NOT int_range_output_digest STREQUAL FSIM_SCV_INT_RANGE_OUTPUT_SHA256)
    message(FATAL_ERROR "SCV int-range patch output SHA-256 mismatch")
  endif()
  set(range_size_source
      "${patched_root}/src/scv/scv_constraint_range.cpp")
  file(SHA256 "${range_size_source}" range_size_input_digest)
  if(NOT range_size_input_digest STREQUAL FSIM_SCV_RANGE_SIZE_INPUT_SHA256)
    message(FATAL_ERROR "SCV range-size patch input SHA-256 mismatch")
  endif()
  file(READ "${range_size_source}" range_size_contents)
  set(old_range_size
      "      _tmp = _upperbound - _lowerbound;  \\")
  set(new_range_size
      "      _tmp = static_cast<SizeT>(_upperbound) - static_cast<SizeT>(_lowerbound);  \\")
  string(FIND "${range_size_contents}" "${old_range_size}" range_size_offset)
  if(range_size_offset EQUAL -1)
    message(FATAL_ERROR "SCV range-size patch anchor is missing")
  endif()
  string(REPLACE "${old_range_size}" "${new_range_size}"
         range_size_contents "${range_size_contents}")
  file(CONFIGURE OUTPUT "${range_size_source}"
       CONTENT "${range_size_contents}" @ONLY NEWLINE_STYLE LF)
  file(SHA256 "${range_size_source}" range_size_output_digest)
  if(NOT range_size_output_digest STREQUAL FSIM_SCV_RANGE_SIZE_OUTPUT_SHA256)
    message(FATAL_ERROR "SCV range-size patch output SHA-256 mismatch")
  endif()
  fsim_scv_compute_tree_identity(
    "${patched_root}" patched_count patched_digest)
  if(NOT patched_count EQUAL FSIM_SCV_TREE_FILE_COUNT OR
     NOT patched_digest STREQUAL FSIM_SCV_PATCHED_TREE_SHA256)
    message(FATAL_ERROR
      "SCV patched tree mismatch: files=${patched_count}, SHA256=${patched_digest}")
  endif()
  set(${output_root} "${patched_root}" PARENT_SCOPE)
endfunction()

function(fsim_scv_collect_runtime_sources source_root output_sources)
  file(GLOB scv_sources LIST_DIRECTORIES FALSE
       "${source_root}/src/scv/*.cpp")
  file(GLOB cudd_sources LIST_DIRECTORIES FALSE
       "${source_root}/src/cudd/2.3.0/cudd/*.c")
  file(GLOB util_sources LIST_DIRECTORIES FALSE
       "${source_root}/src/cudd/2.3.0/util/*.c")
  file(GLOB mtr_sources LIST_DIRECTORIES FALSE
       "${source_root}/src/cudd/2.3.0/mtr/*.c")
  file(GLOB st_sources LIST_DIRECTORIES FALSE
       "${source_root}/src/cudd/2.3.0/st/*.c")
  file(GLOB obj_sources LIST_DIRECTORIES FALSE
       "${source_root}/src/cudd/2.3.0/obj/*.cc")
  list(REMOVE_ITEM cudd_sources
       "${source_root}/src/cudd/2.3.0/cudd/testcudd.c")
  list(REMOVE_ITEM mtr_sources
       "${source_root}/src/cudd/2.3.0/mtr/testmtr.c")
  list(REMOVE_ITEM obj_sources
       "${source_root}/src/cudd/2.3.0/obj/testobj.cc")
  list(LENGTH scv_sources scv_count)
  list(LENGTH cudd_sources cudd_count)
  list(LENGTH util_sources util_count)
  list(LENGTH mtr_sources mtr_count)
  list(LENGTH st_sources st_count)
  list(LENGTH obj_sources obj_count)
  if(NOT scv_count EQUAL 11 OR NOT cudd_count EQUAL 61 OR
     NOT util_count EQUAL 14 OR NOT mtr_count EQUAL 2 OR
     NOT st_count EQUAL 1 OR NOT obj_count EQUAL 1)
    message(FATAL_ERROR
      "SCV runtime source closure changed: scv=${scv_count}, cudd=${cudd_count}, util=${util_count}, mtr=${mtr_count}, st=${st_count}, obj=${obj_count}")
  endif()
  set(sources
      ${scv_sources} ${cudd_sources} ${util_sources}
      ${mtr_sources} ${st_sources} ${obj_sources})
  list(SORT sources)
  list(LENGTH sources source_count)
  if(NOT source_count EQUAL 90)
    message(FATAL_ERROR "SCV runtime requires exactly 90 upstream sources")
  endif()
  set(${output_sources} "${sources}" PARENT_SCOPE)
endfunction()

function(fsim_scv_write_config_header output_path)
  if(WIN32)
    set(sizeof_long 4)
  else()
    set(sizeof_long 8)
  endif()
  set(contents "#ifndef FSIM_GOVERNED_SCV_CONFIG_H\n")
  string(APPEND contents "#define FSIM_GOVERNED_SCV_CONFIG_H\n")
  string(APPEND contents "#define HAVE_INTTYPES_H 1\n")
  string(APPEND contents "#define HAVE_MEMORY_H 1\n")
  string(APPEND contents "#define HAVE_STDINT_H 1\n")
  string(APPEND contents "#define HAVE_STDLIB_H 1\n")
  string(APPEND contents "#define HAVE_STRING_H 1\n")
  string(APPEND contents "#define HAVE_SYS_STAT_H 1\n")
  string(APPEND contents "#define HAVE_SYS_TYPES_H 1\n")
  if(NOT WIN32)
    string(APPEND contents "#define HAVE_DLFCN_H 1\n")
    string(APPEND contents "#define HAVE_PTHREAD 1\n")
    string(APPEND contents "#define HAVE_STRINGS_H 1\n")
    string(APPEND contents "#define HAVE_UNISTD_H 1\n")
  endif()
  string(APPEND contents "#define SIZEOF_INT 4\n")
  string(APPEND contents "#define SIZEOF_LONG ${sizeof_long}\n")
  string(APPEND contents "#define SIZEOF_VOID_P 8\n")
  string(APPEND contents "#define STDC_HEADERS 1\n")
  string(APPEND contents "#define PACKAGE_NAME \"SCV\"\n")
  string(APPEND contents "#define PACKAGE_VERSION \"${FSIM_SCV_VERSION}\"\n")
  string(APPEND contents "#define VERSION \"${FSIM_SCV_VERSION}\"\n")
  string(APPEND contents "#endif\n")
  get_filename_component(output_directory "${output_path}" DIRECTORY)
  file(MAKE_DIRECTORY "${output_directory}")
  if(EXISTS "${output_path}")
    file(READ "${output_path}" existing_contents)
    if(existing_contents STREQUAL contents)
      return()
    endif()
  endif()
  file(WRITE "${output_path}" "${contents}")
endfunction()

function(fsim_scv_configure_install_metadata binary_root)
  include(CMakePackageConfigHelpers)
  set(config_directory "${binary_root}/cmake")
  file(MAKE_DIRECTORY "${config_directory}")
  configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/SCVConfig.cmake.in"
    "${config_directory}/SCVConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SCV")
  write_basic_package_version_file(
    "${config_directory}/SCVConfigVersion.cmake"
    VERSION "${FSIM_SCV_VERSION}"
    COMPATIBILITY ExactVersion)
  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/scv.pc.in"
    "${binary_root}/scv.pc" @ONLY)
  install(
    FILES
      "${config_directory}/SCVConfig.cmake"
      "${config_directory}/SCVConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SCV")
  install(
    FILES "${binary_root}/scv.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endfunction()

macro(fsim_scv_add_official_runtime archive work_root)
  if(TARGET scv OR TARGET SCV::scv)
    message(FATAL_ERROR "an SCV runtime target already exists")
  endif()
  fsim_scv_materialize_source(
    "${archive}" "${work_root}"
    FSIM_SCV_OFFICIAL_SOURCE_DIR FSIM_SCV_SOURCE_MANIFEST)
  set(FSIM_SCV_PATCH_MANIFEST
      "${PROJECT_SOURCE_DIR}/third_party/scv-2.0.1/PATCHES.txt")
  fsim_scv_apply_governed_patches(
    "${FSIM_SCV_OFFICIAL_SOURCE_DIR}" "${FSIM_SCV_PATCH_MANIFEST}"
    FSIM_SCV_BUILD_SOURCE_DIR)
  set(FSIM_SCV_BINARY_ROOT "${CMAKE_BINARY_DIR}/_deps/fsim_scv_2_0_1-build")
  set(FSIM_SCV_GENERATED_INCLUDE_DIR "${FSIM_SCV_BINARY_ROOT}/generated")
  fsim_scv_write_config_header(
    "${FSIM_SCV_GENERATED_INCLUDE_DIR}/scv/scv_config.h")
  fsim_scv_collect_runtime_sources(
    "${FSIM_SCV_BUILD_SOURCE_DIR}" FSIM_SCV_RUNTIME_SOURCES)

  add_library(
    scv SHARED
    ${FSIM_SCV_RUNTIME_SOURCES}
    "${PROJECT_SOURCE_DIR}/src/systemc/scv_compatibility.cpp")
  add_library(SCV::scv ALIAS scv)
  set_target_properties(
    scv
    PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED ON
      C_EXTENSIONS ON
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
      EXPORT_NAME scv
      OUTPUT_NAME scv
      VERSION "${FSIM_SCV_VERSION}"
      SOVERSION 2)
  if(WIN32)
    set_target_properties(scv PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  else()
    set_target_properties(scv PROPERTIES INSTALL_RPATH "$ORIGIN")
  endif()
  target_compile_definitions(
    scv PRIVATE
    HAVE_CONFIG_H=1
    "FSIM_SCV_SYSTEMC_RUNTIME_IDENTITY=\"${FSIM_SYSTEMC_RUNTIME_IDENTITY}\"")
  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(
      scv PRIVATE
      "$<$<COMPILE_LANGUAGE:C>:-Wno-deprecated-non-prototype>"
      "$<$<COMPILE_LANGUAGE:C>:-Wno-void-pointer-to-enum-cast>")
  endif()
  target_include_directories(
    scv
    SYSTEM
    PUBLIC
      "$<BUILD_INTERFACE:${FSIM_SCV_BUILD_SOURCE_DIR}/src>"
      "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
      "${FSIM_SCV_GENERATED_INCLUDE_DIR}"
      "${FSIM_SCV_BUILD_SOURCE_DIR}/src/cudd/2.3.0/cudd"
      "${FSIM_SCV_BUILD_SOURCE_DIR}/src/cudd/2.3.0/obj"
      "${FSIM_SCV_BUILD_SOURCE_DIR}/src/cudd/2.3.0/util"
      "${FSIM_SCV_BUILD_SOURCE_DIR}/src/cudd/2.3.0/mtr"
      "${FSIM_SCV_BUILD_SOURCE_DIR}/src/cudd/2.3.0/st")
  if(WIN32)
    target_link_libraries(
      scv
      PRIVATE "${FSIM_SYSTEMC_INTERNAL_RUNTIME_TARGET}"
      INTERFACE SystemC::systemc)
  else()
    target_link_libraries(scv PUBLIC SystemC::systemc)
  endif()

  install(
    TARGETS scv
    EXPORT SCVTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(
    EXPORT SCVTargets
    NAMESPACE SCV::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SCV")
  install(
    FILES "${FSIM_SCV_BUILD_SOURCE_DIR}/src/scv.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(
    FILES "${PROJECT_SOURCE_DIR}/include/fsim/systemc/scv.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fsim/systemc")
  install(
    DIRECTORY "${FSIM_SCV_BUILD_SOURCE_DIR}/src/scv/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/scv"
    FILES_MATCHING PATTERN "*.h")
  install(
    FILES
      "${PROJECT_SOURCE_DIR}/third_party/scv-2.0.1/LICENSE"
      "${PROJECT_SOURCE_DIR}/third_party/scv-2.0.1/NOTICE"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/third-party/scv-2.0.1")
  fsim_scv_configure_install_metadata("${FSIM_SCV_BINARY_ROOT}")
endmacro()

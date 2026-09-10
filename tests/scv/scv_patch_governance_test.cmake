# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR NOT DEFINED FSIM_BINARY_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR and FSIM_BINARY_DIR are required")
endif()

set(module "${FSIM_SOURCE_DIR}/cmake/FsimScv.cmake")
set(vendor_root "${FSIM_SOURCE_DIR}/third_party/scv-2.0.1")
set(archive "${vendor_root}/scv-2.0.1.tar.gz")
set(patch_manifest "${vendor_root}/PATCHES.txt")
foreach(input IN ITEMS "${module}" "${archive}" "${patch_manifest}")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "SCV patch-governance input is missing: ${input}")
  endif()
endforeach()

include("${module}")
file(READ "${module}" module_text)
string(REGEX MATCHALL "NEWLINE_STYLE LF" lf_write_policies "${module_text}")
list(LENGTH lf_write_policies lf_write_policy_count)
if(NOT lf_write_policy_count EQUAL 6)
  message(FATAL_ERROR
    "SCV patch materialization must force LF for all six governed outputs")
endif()
set(work_root "${FSIM_BINARY_DIR}/tests/scv-2.0.1-patch-governance")
fsim_scv_materialize_source("${archive}" "${work_root}"
                            materialized_root materialized_manifest)
fsim_scv_apply_governed_patches(
  "${materialized_root}" "${patch_manifest}" adapted_root)
if(adapted_root STREQUAL materialized_root)
  message(FATAL_ERROR "SCV compatibility patch changed the pristine source root")
endif()
fsim_scv_compute_tree_identity("${adapted_root}" tree_count tree_digest)
if(NOT tree_count EQUAL FSIM_SCV_TREE_FILE_COUNT OR
   NOT tree_digest STREQUAL FSIM_SCV_PATCHED_TREE_SHA256)
  message(FATAL_ERROR "SCV compatibility patch output identity drifted")
endif()
file(SHA256 "${adapted_root}/src/scv/scv_bag.h" bag_output_digest)
if(NOT bag_output_digest STREQUAL FSIM_SCV_BAG_OUTPUT_SHA256)
  message(FATAL_ERROR "SCV bag compatibility output drifted")
endif()
file(SHA256 "${adapted_root}/src/scv/_scv_introspection.h"
  nested_extension_output_digest)
if(NOT nested_extension_output_digest STREQUAL
   FSIM_SCV_NESTED_EXTENSION_OUTPUT_SHA256)
  message(FATAL_ERROR "SCV nested-extension compatibility output drifted")
endif()
file(SHA256 "${adapted_root}/src/scv/scv_constraint.cpp"
  int_range_output_digest)
if(NOT int_range_output_digest STREQUAL FSIM_SCV_INT_RANGE_OUTPUT_SHA256)
  message(FATAL_ERROR "SCV int-range compatibility output drifted")
endif()
file(SHA256 "${adapted_root}/src/scv/scv_constraint_range.cpp"
  range_size_output_digest)
if(NOT range_size_output_digest STREQUAL FSIM_SCV_RANGE_SIZE_OUTPUT_SHA256)
  message(FATAL_ERROR "SCV range-size compatibility output drifted")
endif()
file(SHA256 "${adapted_root}/src/scv/scv_random.cpp"
  windows_rand_output_digest)
if(NOT windows_rand_output_digest STREQUAL FSIM_SCV_WINDOWS_RAND_OUTPUT_SHA256)
  message(FATAL_ERROR "SCV Windows rand_r compatibility output drifted")
endif()
file(SHA256 "${adapted_root}/src/scv/scv_tr.cpp"
  stream_core_output_digest)
if(NOT stream_core_output_digest STREQUAL FSIM_SCV_STREAM_CORE_OUTPUT_SHA256)
  message(FATAL_ERROR "SCV stream-core lifetime output drifted")
endif()
foreach(relative_path IN ITEMS
    src/scv/scv_bag.h
    src/scv/_scv_introspection.h
    src/scv/scv_constraint.cpp
    src/scv/scv_constraint_range.cpp
    src/scv/scv_random.cpp
    src/scv/scv_tr.cpp)
  file(READ "${adapted_root}/${relative_path}" adapted_contents)
  string(FIND "${adapted_contents}" "\r" carriage_return_index)
  if(NOT carriage_return_index EQUAL -1)
    message(FATAL_ERROR
      "SCV governed output contains a platform-dependent CR: ${relative_path}")
  endif()
endforeach()
fsim_scv_validate_source_tree("${materialized_root}")

file(READ "${patch_manifest}" patch_manifest_text)
foreach(token IN ITEMS
    "patch_count=6"
    "patch=scv-bag-mutable-random|${FSIM_SCV_BAG_PATCH_SHA256}|src/scv/scv_bag.h|${FSIM_SCV_BAG_INPUT_SHA256}|${FSIM_SCV_BAG_OUTPUT_SHA256}"
    "patch=scv-int-range-overflow|${FSIM_SCV_INT_RANGE_PATCH_SHA256}|src/scv/scv_constraint.cpp|${FSIM_SCV_INT_RANGE_INPUT_SHA256}|${FSIM_SCV_INT_RANGE_OUTPUT_SHA256}"
    "patch=scv-range-size-overflow|${FSIM_SCV_RANGE_SIZE_PATCH_SHA256}|src/scv/scv_constraint_range.cpp|${FSIM_SCV_RANGE_SIZE_INPUT_SHA256}|${FSIM_SCV_RANGE_SIZE_OUTPUT_SHA256}"
    "patch=scv-nested-extension-constructors|${FSIM_SCV_NESTED_EXTENSION_PATCH_SHA256}|src/scv/_scv_introspection.h|${FSIM_SCV_NESTED_EXTENSION_INPUT_SHA256}|${FSIM_SCV_NESTED_EXTENSION_OUTPUT_SHA256}"
    "patch=scv-stream-core-lifetime|${FSIM_SCV_STREAM_CORE_PATCH_SHA256}|src/scv/scv_tr.cpp|${FSIM_SCV_STREAM_CORE_INPUT_SHA256}|${FSIM_SCV_STREAM_CORE_OUTPUT_SHA256}"
    "patch=scv-windows-rand-r|${FSIM_SCV_WINDOWS_RAND_PATCH_SHA256}|src/scv/scv_random.cpp|${FSIM_SCV_WINDOWS_RAND_INPUT_SHA256}|${FSIM_SCV_WINDOWS_RAND_OUTPUT_SHA256}"
    "unmodified_linux_compiler=g++-13"
    "unmodified_linux_systemc=3.0.2"
    "unmodified_linux_result=shared-and-static-library-build-pass"
    "llvm22_configure_blockers=unrecognized-clang-compiler,legacy-lib-gnu-layout"
    "gcc13_cxx20_result=blocked-by-template-id-constructor-spelling"
    "decision=external-cmake-adapter-with-six-generated-source-patches"
    "rationale="
    "platform_scope=all-supported-compilers"
    "positive_probe=patched-exact-tree-builds-with-clang-gcc-and-llvm-mingw-and-preserves-const-peek-randomization-nested-extension-construction-full-width-int-randomization-overflow-safe-interval-sizing-windows-random-stream-compilation-and-leak-free-stream-destruction"
    "negative_probe=changed-manifest-patch-input-output-or-source-tree-rejected-before-build"
    "removal_criteria=")
  string(FIND "${patch_manifest_text}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "SCV patch governance omits ${token}")
  endif()
endforeach()

set(negative_root "${work_root}/negative")
file(MAKE_DIRECTORY "${negative_root}")
set(changed_manifest "${negative_root}/PATCHES.txt")
file(READ "${patch_manifest}" changed_manifest_text)
string(APPEND changed_manifest_text "unexpected_patch=1\n")
file(WRITE "${changed_manifest}" "${changed_manifest_text}")
fsim_scv_patch_governance_error(
  "${materialized_root}" "${changed_manifest}" changed_manifest_error)
if(NOT changed_manifest_error MATCHES "manifest SHA-256 mismatch")
  message(FATAL_ERROR "changed SCV patch manifest was not rejected distinctly")
endif()

set(incomplete_tree "${negative_root}/scv-2.0.1")
file(MAKE_DIRECTORY "${incomplete_tree}")
file(COPY "${vendor_root}/LICENSE" DESTINATION "${incomplete_tree}")
fsim_scv_patch_governance_error(
  "${incomplete_tree}" "${patch_manifest}" changed_source_error)
if(NOT changed_source_error MATCHES "governed pristine tree")
  message(FATAL_ERROR "changed SCV source tree was not rejected distinctly")
endif()

message(STATUS
  "SCV patch governance passed: patches=${FSIM_SCV_PATCH_COUNT} "
  "manifest=${FSIM_SCV_PATCH_MANIFEST_SHA256} tree=${FSIM_SCV_PATCHED_TREE_SHA256}")

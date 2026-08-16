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
fsim_scv_validate_source_tree("${materialized_root}")

file(READ "${patch_manifest}" patch_manifest_text)
foreach(token IN ITEMS
    "patch_count=1"
    "patch=scv-bag-mutable-random|${FSIM_SCV_BAG_PATCH_SHA256}|src/scv/scv_bag.h|${FSIM_SCV_BAG_INPUT_SHA256}|${FSIM_SCV_BAG_OUTPUT_SHA256}"
    "unmodified_linux_compiler=g++-13"
    "unmodified_linux_systemc=3.0.2"
    "unmodified_linux_result=shared-and-static-library-build-pass"
    "llvm22_configure_blockers=unrecognized-clang-compiler,legacy-lib-gnu-layout"
    "decision=external-cmake-adapter-with-one-generated-source-patch"
    "rationale="
    "platform_scope=all-supported-compilers"
    "positive_probe=patched-exact-tree-builds-with-clang-and-preserves-const-peek-randomization"
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

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR NOT DEFINED FSIM_BINARY_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR and FSIM_BINARY_DIR are required")
endif()

set(module "${FSIM_SOURCE_DIR}/cmake/FsimUvmSources.cmake")
set(materializer "${FSIM_SOURCE_DIR}/cmake/MaterializeUvmSources.cmake")
set(provenance "${FSIM_SOURCE_DIR}/docs/uvm-source-provenance.md")
set(root_cmake "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(test_cmake "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(smoke_probe
  "${FSIM_SOURCE_DIR}/tests/fixtures/systemverilog/uvm_core_smoke_probe.sv")
set(flow_probe
  "${FSIM_SOURCE_DIR}/tests/fixtures/systemverilog/uvm_phase_tlm_example.sv")
foreach(input IN ITEMS "${module}" "${materializer}" "${provenance}" "${root_cmake}" "${test_cmake}" "${smoke_probe}" "${flow_probe}")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "UVM source-harness input not found: ${input}")
  endif()
endforeach()

file(READ "${flow_probe}" flow_contents)
foreach(token IN ITEMS
    fsim_native_component fsim_uvm_sequence fsim_uvm_virtual_sequence
    fsim_uvm_sequencer fsim_uvm_driver fsim_uvm_monitor fsim_uvm_agent
    fsim_uvm_callback uvm_blocking_put_port uvm_tlm_fifo)
  string(FIND "${flow_contents}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "project-owned UVM flow probe omits ${token}")
  endif()
endforeach()

foreach(token IN ITEMS
    fsim_uvm_reg fsim_uvm_reg_block fsim_uvm_reg_adapter
    fsim_uvm_reg_predictor fsim_uvm_reg_sequence fsim_uvm_reg_callback)
  string(FIND "${flow_contents}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "project-owned UVM register probe omits ${token}")
  endif()
endforeach()

file(READ "${smoke_probe}" smoke_contents)
foreach(suite IN ITEMS
    object_policy factory resource configuration command_line reporting
    callback test_selection topology timeout seed)
  string(FIND "${smoke_contents}" "${suite}_smoke" suite_index)
  if(suite_index EQUAL -1)
    message(FATAL_ERROR "project-owned UVM smoke probe omits ${suite}")
  endif()
endforeach()

include("${module}")
fsim_uvm_validate_metadata()

file(READ "${provenance}" provenance_contents)
foreach(release IN LISTS FSIM_UVM_RELEASE_IDS)
  foreach(field IN ITEMS VERSION RELEASE_ID UPSTREAM_COMMIT URL ARCHIVE_SHA256 TREE_SHA256)
    fsim_uvm_release_field("${release}" "${field}" token)
    string(FIND "${provenance_contents}" "${token}" token_index)
    if(token_index EQUAL -1)
      message(FATAL_ERROR "UVM provenance omits ${release} ${field}: ${token}")
    endif()
  endforeach()
endforeach()

fsim_uvm_work_root_error(
  "${FSIM_SOURCE_DIR}/generated-uvm" unsafe_root unsafe_error
)
if(unsafe_error STREQUAL "")
  message(FATAL_ERROR "UVM work-root guard accepted the source tree")
endif()
set(fixture "${FSIM_BINARY_DIR}/tests/uvm-source-harness-fixture")
fsim_uvm_work_root_error("${fixture}" safe_root safe_error)
if(NOT safe_error STREQUAL "")
  message(FATAL_ERROR "UVM work-root guard rejected the build tree: ${safe_error}")
endif()

file(MAKE_DIRECTORY "${safe_root}/tree/nested")
file(WRITE "${safe_root}/tree/alpha.txt" "alpha\n")
file(WRITE "${safe_root}/tree/nested/beta.txt" "beta\n")
fsim_uvm_compute_tree_identity("${safe_root}/tree" first_count first_digest)
fsim_uvm_compute_tree_identity("${safe_root}/tree" second_count second_digest)
if(NOT first_count EQUAL 2 OR NOT first_digest STREQUAL second_digest)
  message(FATAL_ERROR "UVM tree identity is not deterministic")
endif()
file(WRITE "${safe_root}/tree/nested/beta.txt" "changed\n")
fsim_uvm_compute_tree_identity("${safe_root}/tree" changed_count changed_digest)
if(NOT changed_count EQUAL 2 OR changed_digest STREQUAL first_digest)
  message(FATAL_ERROR "UVM tree identity did not detect a content change")
endif()

file(READ "${root_cmake}" root_cmake_contents)
foreach(token IN ITEMS
    "FSIM_UVM_SOURCE_MODE"
    "FSIM_UVM_WORK_ROOT"
    "FSIM_UVM_1_2_ARCHIVE"
    "FSIM_UVM_2020_3_1_ARCHIVE"
    "fsim_uvm_configure_sources()")
  string(FIND "${root_cmake_contents}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "top-level UVM configuration omits ${token}")
  endif()
endforeach()
file(READ "${test_cmake}" test_cmake_contents)
string(FIND "${test_cmake_contents}" "NAME fsim.uvm-source-harness" registration_index)
if(registration_index EQUAL -1)
  message(FATAL_ERROR "fsim.uvm-source-harness is not registered")
endif()

message(
  STATUS
  "UVM source harness governs 2 releases, 1286 extracted files, "
  "12 exact entry points, 11 core, 10 flow, and 6 register smoke contracts, "
  "isolated roots, and deterministic tree identities"
)

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()
if(NOT DEFINED FSIM_UVM_WORK_ROOT)
  message(FATAL_ERROR "FSIM_UVM_WORK_ROOT is required")
endif()
if(NOT DEFINED FSIM_BINARY_DIR)
  message(FATAL_ERROR "FSIM_BINARY_DIR is required")
endif()

include("${FSIM_SOURCE_DIR}/cmake/FsimUvmSources.cmake")
fsim_uvm_materialize_release(
  uvm-1.2 "${FSIM_UVM_WORK_ROOT}" "${FSIM_UVM_1_2_ARCHIVE}"
  root_1_2 manifest_1_2
)
fsim_uvm_materialize_release(
  uvm-2020.3.1 "${FSIM_UVM_WORK_ROOT}" "${FSIM_UVM_2020_3_1_ARCHIVE}"
  root_2020 manifest_2020
)
message(
  STATUS
  "materialized governed UVM sources: ${root_1_2};${root_2020}; "
  "manifests=${manifest_1_2};${manifest_2020}"
)

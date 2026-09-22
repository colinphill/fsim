# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE FSIM_TPP_FILES
  RELATIVE "${FSIM_SOURCE_DIR}"
  "${FSIM_SOURCE_DIR}/include/*.tpp"
  "${FSIM_SOURCE_DIR}/src/*.tpp"
  "${FSIM_SOURCE_DIR}/tests/*.tpp")
list(SORT FSIM_TPP_FILES)
if(FSIM_TPP_FILES)
  list(JOIN FSIM_TPP_FILES ", " FSIM_TPP_LIST)
  message(FATAL_ERROR
    ".tpp implementation files are forbidden: ${FSIM_TPP_LIST}; "
    "keep template definitions in their owning header and move all "
    "non-template implementation to independently compiled .cpp files")
endif()

message(STATUS
  "Translation-unit structure checked: 0 forbidden .tpp files")

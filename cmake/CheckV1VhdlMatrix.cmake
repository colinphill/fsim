# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

file(
  READ
  "${FSIM_SOURCE_DIR}/docs/feature-matrix.md"
  matrix
)
string(
  REGEX MATCHALL
  "\\| V1-VH-[0-9][0-9] \\|[^\n]*"
  rows
  "${matrix}"
)
list(LENGTH rows row_count)
if(NOT row_count EQUAL 8)
  message(FATAL_ERROR
    "expected exactly 8 V1 VHDL release rows, found ${row_count}")
endif()

set(expected_ids 01 02 03 04 05 06 07 08)
set(index 0)
foreach(row IN LISTS rows)
  list(GET expected_ids ${index} expected_id)
  if(NOT row MATCHES "^\\| V1-VH-${expected_id} \\|")
    message(FATAL_ERROR
      "expected V1-VH-${expected_id} at release-row index ${index}")
  endif()
  if(NOT row MATCHES "\\| execute \\|")
    message(FATAL_ERROR "V1-VH-${expected_id} is not executable")
  endif()
  if(row MATCHES "\\| — \\|")
    message(FATAL_ERROR "V1-VH-${expected_id} has an empty evidence column")
  endif()
  math(EXPR index "${index} + 1")
endforeach()

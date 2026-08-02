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
  "\\| ML-00[56] \\|[^\n]*"
  rows
  "${matrix}"
)
list(LENGTH rows row_count)
if(NOT row_count EQUAL 2)
  message(FATAL_ERROR
    "expected exactly 2 mixed conversion release rows, found ${row_count}")
endif()

set(expected_ids 5 6)
set(index 0)
foreach(row IN LISTS rows)
  list(GET expected_ids ${index} expected_id)
  if(NOT row MATCHES "^\\| ML-00${expected_id} \\|")
    message(FATAL_ERROR
      "expected ML-00${expected_id} at conversion-row index ${index}")
  endif()
  if(NOT row MATCHES "\\| execute \\|")
    message(FATAL_ERROR "ML-00${expected_id} is not executable")
  endif()
  if(row MATCHES "\\| — \\|")
    message(FATAL_ERROR "ML-00${expected_id} has an empty evidence column")
  endif()
  math(EXPR index "${index} + 1")
endforeach()

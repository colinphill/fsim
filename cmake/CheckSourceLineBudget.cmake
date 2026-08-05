# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_SOURCE_LINE_LIMIT 2500)
set(FSIM_SOURCE_LINE_REFACTOR_TARGET 2000)

file(
  GLOB_RECURSE FSIM_AUTHORED_SOURCES
  RELATIVE "${FSIM_SOURCE_DIR}"
  "${FSIM_SOURCE_DIR}/include/*.c"
  "${FSIM_SOURCE_DIR}/include/*.cpp"
  "${FSIM_SOURCE_DIR}/include/*.h"
  "${FSIM_SOURCE_DIR}/include/*.hpp"
  "${FSIM_SOURCE_DIR}/include/*.ixx"
  "${FSIM_SOURCE_DIR}/include/*.tpp"
  "${FSIM_SOURCE_DIR}/src/*.c"
  "${FSIM_SOURCE_DIR}/src/*.cpp"
  "${FSIM_SOURCE_DIR}/src/*.h"
  "${FSIM_SOURCE_DIR}/src/*.hpp"
  "${FSIM_SOURCE_DIR}/src/*.ixx"
  "${FSIM_SOURCE_DIR}/src/*.tpp"
  "${FSIM_SOURCE_DIR}/tests/*.c"
  "${FSIM_SOURCE_DIR}/tests/*.cpp"
  "${FSIM_SOURCE_DIR}/tests/*.h"
  "${FSIM_SOURCE_DIR}/tests/*.hpp"
  "${FSIM_SOURCE_DIR}/tests/*.ixx"
  "${FSIM_SOURCE_DIR}/tests/*.tpp"
)

set(FSIM_OBSERVED_ALLOWLIST)
foreach(source IN LISTS FSIM_AUTHORED_SOURCES)
  file(READ "${FSIM_SOURCE_DIR}/${source}" contents)
  string(REGEX MATCHALL "\n" newlines "${contents}")
  list(LENGTH newlines line_count)
  if(line_count GREATER FSIM_SOURCE_LINE_LIMIT)
    message(
      FATAL_ERROR
      "${source} has ${line_count} lines and exceeds the "
      "${FSIM_SOURCE_LINE_LIMIT}-line hard limit; refactor it below the "
      "${FSIM_SOURCE_LINE_REFACTOR_TARGET}-line target before retrying"
    )
  endif()
endforeach()

list(LENGTH FSIM_AUTHORED_SOURCES source_count)
message(
  STATUS
  "Checked ${source_count} authored sources against the "
  "${FSIM_SOURCE_LINE_LIMIT}-line hard limit with a "
  "${FSIM_SOURCE_LINE_REFACTOR_TARGET}-line refactor target"
)

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_SOURCE_LINE_LIMIT 2000)
set(
  FSIM_SOURCE_LINE_ALLOWLIST
  "include/fsim/systemc.hpp"
  "src/compiler/llvm_jit_lowering.cpp"
  "src/elaboration/elaborator.cpp"
)

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
  list(FIND FSIM_SOURCE_LINE_ALLOWLIST "${source}" allowlist_index)
  if(line_count GREATER FSIM_SOURCE_LINE_LIMIT)
    if(allowlist_index EQUAL -1)
      message(
        FATAL_ERROR
        "${source} has ${line_count} lines; authored sources are limited to "
        "${FSIM_SOURCE_LINE_LIMIT}"
      )
    endif()
    list(APPEND FSIM_OBSERVED_ALLOWLIST "${source}")
  elseif(NOT allowlist_index EQUAL -1)
    message(
      FATAL_ERROR
      "${source} is within the ${FSIM_SOURCE_LINE_LIMIT}-line limit and must "
      "be removed from FSIM_SOURCE_LINE_ALLOWLIST"
    )
  endif()
endforeach()

foreach(source IN LISTS FSIM_SOURCE_LINE_ALLOWLIST)
  list(FIND FSIM_OBSERVED_ALLOWLIST "${source}" observed_index)
  if(observed_index EQUAL -1)
    message(
      FATAL_ERROR
      "source-line allowlist entry '${source}' was not observed"
    )
  endif()
endforeach()

list(LENGTH FSIM_AUTHORED_SOURCES source_count)
message(
  STATUS
  "Checked ${source_count} authored sources against the "
  "${FSIM_SOURCE_LINE_LIMIT}-line limit"
)

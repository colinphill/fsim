# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_TRANSLATION_UNIT_TARGET 2000)

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

file(GLOB_RECURSE FSIM_CPP_SOURCES
  RELATIVE "${FSIM_SOURCE_DIR}"
  "${FSIM_SOURCE_DIR}/src/*.cpp"
  "${FSIM_SOURCE_DIR}/tests/*.cpp")
list(SORT FSIM_CPP_SOURCES)
set(FSIM_OVERSIZED_TRANSLATION_UNITS)
foreach(FSIM_SOURCE IN LISTS FSIM_CPP_SOURCES)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_SOURCE}" FSIM_CONTENTS)
  string(REGEX MATCHALL "\n" FSIM_NEWLINES "${FSIM_CONTENTS}")
  list(LENGTH FSIM_NEWLINES FSIM_EFFECTIVE_LINES)
  math(EXPR FSIM_EFFECTIVE_LINES "${FSIM_EFFECTIVE_LINES} + 1")
  if(FSIM_EFFECTIVE_LINES GREATER FSIM_TRANSLATION_UNIT_TARGET)
    list(APPEND FSIM_OVERSIZED_TRANSLATION_UNITS
      "${FSIM_SOURCE}=${FSIM_EFFECTIVE_LINES}")
  endif()
endforeach()

if(FSIM_OVERSIZED_TRANSLATION_UNITS)
  list(JOIN FSIM_OVERSIZED_TRANSLATION_UNITS ", " FSIM_OVERSIZED_LIST)
  message(FATAL_ERROR
    "translation units exceed the ${FSIM_TRANSLATION_UNIT_TARGET}-line target: "
    "${FSIM_OVERSIZED_LIST}")
endif()

list(LENGTH FSIM_OVERSIZED_TRANSLATION_UNITS FSIM_TU_DEBT_COUNT)
message(STATUS
  "Translation-unit structure checked: 0 .tpp files, "
  "${FSIM_TU_DEBT_COUNT} units above the ${FSIM_TRANSLATION_UNIT_TARGET}-line target")

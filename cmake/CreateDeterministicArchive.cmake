# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_ARCHIVE_MANIFEST FSIM_ARCHIVE_OUTPUT FSIM_ARCHIVE_MTIME)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()
if(NOT EXISTS "${FSIM_ARCHIVE_MANIFEST}")
  message(FATAL_ERROR "archive manifest is missing: ${FSIM_ARCHIVE_MANIFEST}")
endif()

file(STRINGS "${FSIM_ARCHIVE_MANIFEST}" FSIM_ARCHIVE_PATHS ENCODING UTF-8)
set(FSIM_SEEN)
set(FSIM_PREVIOUS)
foreach(FSIM_PATH IN LISTS FSIM_ARCHIVE_PATHS)
  if(FSIM_PATH STREQUAL ""
     OR FSIM_PATH MATCHES "^/"
     OR FSIM_PATH MATCHES "^[A-Za-z]:"
     OR FSIM_PATH MATCHES "\\\\"
     OR FSIM_PATH MATCHES "(^|/)\\.\\.?(/|$)"
     OR FSIM_PATH MATCHES "//")
    message(FATAL_ERROR "archive manifest contains unsafe path: ${FSIM_PATH}")
  endif()
  list(FIND FSIM_SEEN "${FSIM_PATH}" FSIM_DUPLICATE_INDEX)
  if(NOT FSIM_DUPLICATE_INDEX EQUAL -1)
    message(FATAL_ERROR "archive manifest contains duplicate path: ${FSIM_PATH}")
  endif()
  if(NOT "${FSIM_PREVIOUS}" STREQUAL ""
     AND "${FSIM_PREVIOUS}" STRGREATER "${FSIM_PATH}")
    message(FATAL_ERROR
      "archive manifest is not bytewise ordered: ${FSIM_PREVIOUS} before ${FSIM_PATH}")
  endif()
  if(NOT EXISTS "${FSIM_PATH}" AND NOT IS_SYMLINK "${FSIM_PATH}")
    message(FATAL_ERROR "archive input is missing: ${FSIM_PATH}")
  endif()
  list(APPEND FSIM_SEEN "${FSIM_PATH}")
  set(FSIM_PREVIOUS "${FSIM_PATH}")
endforeach()
if(NOT FSIM_ARCHIVE_PATHS)
  message(FATAL_ERROR "archive manifest must not be empty")
endif()

find_program(FSIM_PYTHON_EXECUTABLE NAMES python3 python)
if(NOT FSIM_PYTHON_EXECUTABLE)
  message(FATAL_ERROR
    "Python is required only for deterministic release-package creation")
endif()
execute_process(
  COMMAND "${FSIM_PYTHON_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/../scripts/create_deterministic_zip.py"
    --manifest "${FSIM_ARCHIVE_MANIFEST}"
    --output "${FSIM_ARCHIVE_OUTPUT}"
    --mtime "${FSIM_ARCHIVE_MTIME}"
  RESULT_VARIABLE FSIM_ZIP_RESULT
  OUTPUT_VARIABLE FSIM_ZIP_OUTPUT
  ERROR_VARIABLE FSIM_ZIP_ERROR)
if(NOT FSIM_ZIP_RESULT EQUAL 0)
  message(FATAL_ERROR
    "deterministic zip creation failed with ${FSIM_ZIP_RESULT}\n"
    "${FSIM_ZIP_OUTPUT}${FSIM_ZIP_ERROR}")
endif()

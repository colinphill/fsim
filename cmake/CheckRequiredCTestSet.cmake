# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(FSIM_REQUIRED_VARIABLE IN ITEMS
    FSIM_REQUIRED_CTESTS_FILE FSIM_REGISTERED_CTESTS_FILE)
  if(NOT DEFINED ${FSIM_REQUIRED_VARIABLE}
     OR "${${FSIM_REQUIRED_VARIABLE}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED_VARIABLE} is required")
  endif()
endforeach()

function(fsim_read_ctest_names FSIM_NAMES_FILE FSIM_NAMES_KIND FSIM_NAMES_OUT)
  if(NOT EXISTS "${FSIM_NAMES_FILE}" OR IS_DIRECTORY "${FSIM_NAMES_FILE}")
    message(FATAL_ERROR
      "${FSIM_NAMES_KIND} CTest-name file does not exist: ${FSIM_NAMES_FILE}")
  endif()

  file(READ "${FSIM_NAMES_FILE}" FSIM_NAMES_CONTENT)
  string(REPLACE "\r\n" "\n" FSIM_NAMES_CONTENT "${FSIM_NAMES_CONTENT}")
  if(FSIM_NAMES_CONTENT MATCHES "\r")
    message(FATAL_ERROR
      "${FSIM_NAMES_KIND} CTest-name file contains a bare carriage return")
  endif()
  if(FSIM_NAMES_CONTENT MATCHES ";" OR FSIM_NAMES_CONTENT MATCHES "\\\\")
    message(FATAL_ERROR
      "${FSIM_NAMES_KIND} CTest-name file contains an unsupported list or backslash character")
  endif()

  string(REPLACE "\n" ";" FSIM_NAME_LINES "${FSIM_NAMES_CONTENT}")
  set(FSIM_PARSED_NAMES "")
  foreach(FSIM_NAME IN LISTS FSIM_NAME_LINES)
    if(FSIM_NAME STREQUAL "" OR FSIM_NAME STREQUAL
        "# SPDX-License-Identifier: Apache-2.0")
      continue()
    endif()
    if(FSIM_NAME MATCHES "\t")
      message(FATAL_ERROR
        "${FSIM_NAMES_KIND} CTest name contains a tab, which is not supported by the line format")
    endif()
    string(STRIP "${FSIM_NAME}" FSIM_TRIMMED_NAME)
    if(FSIM_TRIMMED_NAME STREQUAL "")
      message(FATAL_ERROR
        "${FSIM_NAMES_KIND} CTest-name file contains a whitespace-only name")
    endif()
    if(NOT "${FSIM_TRIMMED_NAME}" STREQUAL "${FSIM_NAME}")
      message(FATAL_ERROR
        "${FSIM_NAMES_KIND} CTest name has leading or trailing whitespace: '${FSIM_NAME}'")
    endif()
    list(APPEND FSIM_PARSED_NAMES "${FSIM_NAME}")
  endforeach()

  set(${FSIM_NAMES_OUT} "${FSIM_PARSED_NAMES}" PARENT_SCOPE)
endfunction()

fsim_read_ctest_names(
  "${FSIM_REQUIRED_CTESTS_FILE}" required FSIM_REQUIRED_CTEST_NAMES)
fsim_read_ctest_names(
  "${FSIM_REGISTERED_CTESTS_FILE}" registered FSIM_REGISTERED_CTEST_NAMES)

# The canonical V3 set retains Tcl coverage for enabled builds. Callers may
# condition only explicitly listed tests when the configured build disables
# that capability; all other required names continue through the strict check.
set(FSIM_OMITTED_REQUIRED_CTEST_NAMES)
foreach(FSIM_OPTIONAL_REQUIRED_CTEST IN LISTS FSIM_OPTIONAL_REQUIRED_CTESTS)
  set(FSIM_OPTIONAL_REQUIRED_CTEST_OCCURRENCES 0)
  foreach(FSIM_REQUIRED_CTEST IN LISTS FSIM_REQUIRED_CTEST_NAMES)
    if("${FSIM_REQUIRED_CTEST}" STREQUAL
       "${FSIM_OPTIONAL_REQUIRED_CTEST}")
      math(EXPR FSIM_OPTIONAL_REQUIRED_CTEST_OCCURRENCES
        "${FSIM_OPTIONAL_REQUIRED_CTEST_OCCURRENCES} + 1")
    endif()
  endforeach()
  if(FSIM_OPTIONAL_REQUIRED_CTEST_OCCURRENCES GREATER 1)
    message(FATAL_ERROR
      "conditioned required CTest is duplicated: ${FSIM_OPTIONAL_REQUIRED_CTEST}")
  endif()
  if(FSIM_OPTIONAL_REQUIRED_CTEST_OCCURRENCES EQUAL 1)
    list(REMOVE_ITEM FSIM_REQUIRED_CTEST_NAMES
      "${FSIM_OPTIONAL_REQUIRED_CTEST}")
    list(APPEND FSIM_OMITTED_REQUIRED_CTEST_NAMES
      "${FSIM_OPTIONAL_REQUIRED_CTEST}")
  endif()
endforeach()
if(FSIM_OMITTED_REQUIRED_CTEST_NAMES)
  list(JOIN FSIM_OMITTED_REQUIRED_CTEST_NAMES ", "
    FSIM_OMITTED_REQUIRED_CTEST_DESCRIPTION)
else()
  set(FSIM_OMITTED_REQUIRED_CTEST_DESCRIPTION "none")
endif()

if(NOT FSIM_REQUIRED_CTEST_NAMES)
  message(FATAL_ERROR "required CTest-name file contains no names")
endif()

set(FSIM_REGISTERED_CTEST_COUNT 0)
foreach(FSIM_TEST_NAME IN LISTS FSIM_REGISTERED_CTEST_NAMES)
  string(SHA256 FSIM_TEST_NAME_HASH "${FSIM_TEST_NAME}")
  set(FSIM_TEST_NAME_KEY "FSIM_REGISTERED_CTEST_${FSIM_TEST_NAME_HASH}")
  if(DEFINED ${FSIM_TEST_NAME_KEY})
    message(FATAL_ERROR
      "duplicate registered CTest name: ${FSIM_TEST_NAME}")
  endif()
  set(${FSIM_TEST_NAME_KEY} "${FSIM_TEST_NAME}")
  math(EXPR FSIM_REGISTERED_CTEST_COUNT
    "${FSIM_REGISTERED_CTEST_COUNT} + 1")
endforeach()

set(FSIM_REQUIRED_CTEST_COUNT 0)
foreach(FSIM_TEST_NAME IN LISTS FSIM_REQUIRED_CTEST_NAMES)
  string(SHA256 FSIM_TEST_NAME_HASH "${FSIM_TEST_NAME}")
  set(FSIM_TEST_NAME_KEY "FSIM_REQUIRED_CTEST_${FSIM_TEST_NAME_HASH}")
  if(DEFINED ${FSIM_TEST_NAME_KEY})
    message(FATAL_ERROR
      "duplicate required CTest name: ${FSIM_TEST_NAME}")
  endif()
  set(${FSIM_TEST_NAME_KEY} "${FSIM_TEST_NAME}")

  set(FSIM_REGISTERED_TEST_NAME_KEY
    "FSIM_REGISTERED_CTEST_${FSIM_TEST_NAME_HASH}")
  if(NOT DEFINED ${FSIM_REGISTERED_TEST_NAME_KEY})
    message(FATAL_ERROR
      "required CTest is not registered: ${FSIM_TEST_NAME}")
  endif()
  math(EXPR FSIM_REQUIRED_CTEST_COUNT
    "${FSIM_REQUIRED_CTEST_COUNT} + 1")
endforeach()

message(
  STATUS
  "required CTest set: ${FSIM_REQUIRED_CTEST_COUNT} required names are registered exactly once; "
  "${FSIM_REGISTERED_CTEST_COUNT} total registered names allow additions; "
  "conditioned names omitted: ${FSIM_OMITTED_REQUIRED_CTEST_DESCRIPTION}")

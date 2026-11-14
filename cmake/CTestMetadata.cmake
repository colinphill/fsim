# SPDX-License-Identifier: Apache-2.0
#
# The tests of a `ctest --show-only=json-v1` document as one JSON object each.
#
# string(JSON) parses its whole input on every call, so reading the document
# (megabytes: every registered test with its command and properties) by index
# costs one parse of all of it per field read, minutes over every test.
# fsim_ctest_test_objects parses the document once and cuts its `tests` array
# into the objects of the tests, which the caller then reads one by one.
#
# The cut follows the layout of the array as string(JSON GET) writes it: each
# element starts on a line of its own at one indentation level, and nested
# values are indented deeper, so an element ends at the first line that holds
# only `},` at that level (a JSON string holds no raw line break). Every cut
# must parse as one JSON object and the cuts must number the array's length;
# otherwise the script stops.
#
# fsim_ctest_test_objects(<json> <count-out> <prefix>) sets <count-out> to the
# number of tests and <prefix>_0 ... <prefix>_<count-1> to their objects.

include_guard(GLOBAL)

function(fsim_ctest_test_objects FSIM_CTEST_JSON FSIM_COUNT_OUT FSIM_PREFIX)
  string(JSON FSIM_COUNT ERROR_VARIABLE FSIM_JSON_ERROR
    LENGTH "${FSIM_CTEST_JSON}" tests)
  if(NOT FSIM_JSON_ERROR STREQUAL "NOTFOUND")
    message(FATAL_ERROR
      "CTest metadata has no usable test array: ${FSIM_JSON_ERROR}")
  endif()
  set(${FSIM_COUNT_OUT} ${FSIM_COUNT} PARENT_SCOPE)
  if(FSIM_COUNT EQUAL 0)
    return()
  endif()
  string(JSON FSIM_ARRAY GET "${FSIM_CTEST_JSON}" tests)
  # "[\n  {...},\n  {...}\n]": the elements without the brackets.
  string(FIND "${FSIM_ARRAY}" "\n  {" FSIM_FIRST)
  string(LENGTH "${FSIM_ARRAY}" FSIM_LENGTH)
  if(FSIM_FIRST EQUAL -1)
    message(FATAL_ERROR "CTest metadata test array has no element objects")
  endif()
  math(EXPR FSIM_BODY_LENGTH "${FSIM_LENGTH} - ${FSIM_FIRST} - 2")
  string(SUBSTRING "${FSIM_ARRAY}" ${FSIM_FIRST} ${FSIM_BODY_LENGTH}
    FSIM_REST)
  set(FSIM_INDEX 0)
  set(FSIM_DONE FALSE)
  while(NOT FSIM_DONE)
    string(FIND "${FSIM_REST}" "\n  },\n  {" FSIM_END)
    if(FSIM_END EQUAL -1)
      set(FSIM_OBJECT "${FSIM_REST}")
      set(FSIM_DONE TRUE)
    else()
      # Keep "\n  }"; the next element starts at its "\n  {".
      math(EXPR FSIM_OBJECT_LENGTH "${FSIM_END} + 4")
      string(SUBSTRING "${FSIM_REST}" 0 ${FSIM_OBJECT_LENGTH} FSIM_OBJECT)
      math(EXPR FSIM_NEXT "${FSIM_END} + 5")
      string(SUBSTRING "${FSIM_REST}" ${FSIM_NEXT} -1 FSIM_REST)
    endif()
    string(JSON FSIM_TYPE ERROR_VARIABLE FSIM_JSON_ERROR
      TYPE "${FSIM_OBJECT}")
    if(NOT FSIM_JSON_ERROR STREQUAL "NOTFOUND"
       OR NOT FSIM_TYPE STREQUAL "OBJECT")
      message(FATAL_ERROR
        "CTest metadata test ${FSIM_INDEX} is not one JSON object")
    endif()
    set(${FSIM_PREFIX}_${FSIM_INDEX} "${FSIM_OBJECT}" PARENT_SCOPE)
    math(EXPR FSIM_INDEX "${FSIM_INDEX} + 1")
  endwhile()
  if(NOT FSIM_INDEX EQUAL FSIM_COUNT)
    message(FATAL_ERROR
      "CTest metadata holds ${FSIM_COUNT} tests but ${FSIM_INDEX} objects")
  endif()
endfunction()

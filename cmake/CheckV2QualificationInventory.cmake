# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)
cmake_policy(SET CMP0057 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v2_qualification_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v2.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS "${FSIM_INVENTORY}" "${FSIM_PLAN}" "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "v2 qualification inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_INVENTORY}" FSIM_INVENTORY_TEXT)
string(REPLACE "\r\n" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(REPLACE "\r" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(SHA256 FSIM_ACTUAL_DIGEST "${FSIM_INVENTORY_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "75acbbdda0d791c887286b3ae3587b693dfd9c069d930bf4ad8d12f4a71c33db")
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "v2 qualification inventory digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

string(REPLACE "\n" ";" FSIM_LINES "${FSIM_INVENTORY_TEXT}")
list(GET FSIM_LINES 1 FSIM_HEADER)
set(FSIM_EXPECTED_HEADER
  "id\tstate\tchange\tdomain\tplatform\ttoolchain\tconfiguration\timplementation_owner\tpositive_owner\tnegative_owner\tperformance_owner\tresource_owner\tretained_log\tvalidation_boundary")
if(NOT FSIM_HEADER STREQUAL FSIM_EXPECTED_HEADER)
  message(FATAL_ERROR "v2 qualification inventory header changed")
endif()

set(FSIM_ROW_COUNT 0)
set(FSIM_ACTIVE_COUNT 0)
set(FSIM_PRESERVED_COUNT 0)
set(FSIM_DEFERRED_COUNT 0)
set(FSIM_IDS)
set(FSIM_CHANGES)
set(FSIM_PRESERVED_CHANGES
  B175-C02 B175-C03 B175-C06 B175-C07 B175-C08 B175-C19 B175-C20)
set(FSIM_DEFERRED_CHANGES
  B175-C04 B175-C05 B175-C09 B175-C10 B175-C11 B175-C12 B175-C13
  B175-C14 B175-C15 B175-C16 B175-C17 B175-C18)
foreach(FSIM_LINE IN LISTS FSIM_LINES)
  if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#" OR
     FSIM_LINE STREQUAL FSIM_EXPECTED_HEADER)
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 14)
    message(FATAL_ERROR "v2 qualification inventory row has ${FSIM_FIELD_COUNT} fields: ${FSIM_LINE}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_STATE)
  list(GET FSIM_FIELDS 2 FSIM_CHANGE)
  list(GET FSIM_FIELDS 4 FSIM_PLATFORM)
  list(GET FSIM_FIELDS 5 FSIM_TOOLCHAIN)
  list(GET FSIM_FIELDS 7 FSIM_IMPLEMENTATION_OWNER)
  list(GET FSIM_FIELDS 8 FSIM_POSITIVE_OWNER)
  list(GET FSIM_FIELDS 9 FSIM_NEGATIVE_OWNER)
  list(GET FSIM_FIELDS 10 FSIM_PERFORMANCE_OWNER)
  list(GET FSIM_FIELDS 11 FSIM_RESOURCE_OWNER)
  list(GET FSIM_FIELDS 12 FSIM_RETAINED_LOG)
  if(NOT FSIM_ID MATCHES "^Q175-[0-9][0-9]$" OR
     NOT FSIM_CHANGE MATCHES "^B175-C[0-9][0-9]$")
    message(FATAL_ERROR "v2 qualification inventory identity is malformed: ${FSIM_ID}/${FSIM_CHANGE}")
  endif()
  if(NOT FSIM_STATE STREQUAL "active" AND
     NOT FSIM_STATE STREQUAL "preserved" AND
     NOT FSIM_STATE STREQUAL "deferred")
    message(FATAL_ERROR "v2 qualification inventory state is invalid: ${FSIM_STATE}")
  endif()
  if((FSIM_CHANGE IN_LIST FSIM_PRESERVED_CHANGES AND
      NOT FSIM_STATE STREQUAL "preserved") OR
     (FSIM_CHANGE IN_LIST FSIM_DEFERRED_CHANGES AND
      NOT FSIM_STATE STREQUAL "deferred") OR
     (NOT FSIM_CHANGE IN_LIST FSIM_PRESERVED_CHANGES AND
      NOT FSIM_CHANGE IN_LIST FSIM_DEFERRED_CHANGES AND
      NOT FSIM_STATE STREQUAL "active"))
    message(FATAL_ERROR
      "v2 qualification inventory state disagrees with the Change 19 disposition: ${FSIM_CHANGE}/${FSIM_STATE}")
  endif()
  if(FSIM_CHANGE IN_LIST FSIM_DEFERRED_CHANGES)
    if(NOT FSIM_PLATFORM STREQUAL "windows" OR
       NOT FSIM_TOOLCHAIN MATCHES "^llvm-mingw-20260616")
      message(FATAL_ERROR
        "deferred Windows row is not mapped to current LLVM-MinGW: ${FSIM_CHANGE}")
    endif()
  endif()
  foreach(FSIM_OWNER IN ITEMS
      "${FSIM_IMPLEMENTATION_OWNER}" "${FSIM_POSITIVE_OWNER}"
      "${FSIM_NEGATIVE_OWNER}" "${FSIM_PERFORMANCE_OWNER}"
      "${FSIM_RESOURCE_OWNER}")
    if(NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_OWNER}" AND
       NOT FSIM_OWNER MATCHES "^build/")
      message(FATAL_ERROR "v2 qualification owner is missing: ${FSIM_OWNER}")
    endif()
  endforeach()
  if(NOT FSIM_RETAINED_LOG MATCHES "^build/.+\\.log$")
    message(FATAL_ERROR "v2 qualification retained log is not build-local: ${FSIM_RETAINED_LOG}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_CHANGES "${FSIM_CHANGE}")
  math(EXPR FSIM_ROW_COUNT "${FSIM_ROW_COUNT} + 1")
  if(FSIM_STATE STREQUAL "active")
    math(EXPR FSIM_ACTIVE_COUNT "${FSIM_ACTIVE_COUNT} + 1")
  elseif(FSIM_STATE STREQUAL "preserved")
    math(EXPR FSIM_PRESERVED_COUNT "${FSIM_PRESERVED_COUNT} + 1")
  else()
    math(EXPR FSIM_DEFERRED_COUNT "${FSIM_DEFERRED_COUNT} + 1")
  endif()
endforeach()
list(REMOVE_DUPLICATES FSIM_IDS)
list(REMOVE_DUPLICATES FSIM_CHANGES)
list(LENGTH FSIM_IDS FSIM_ID_COUNT)
list(LENGTH FSIM_CHANGES FSIM_CHANGE_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 19 OR NOT FSIM_ID_COUNT EQUAL 19 OR
   NOT FSIM_CHANGE_COUNT EQUAL 19 OR NOT FSIM_ACTIVE_COUNT EQUAL 0 OR
   NOT FSIM_PRESERVED_COUNT EQUAL 7 OR NOT FSIM_DEFERRED_COUNT EQUAL 12)
  message(FATAL_ERROR
    "v2 qualification inventory requires 19 unique rows, zero active, seven preserved and twelve deferred")
endif()
set(FSIM_EXPECTED_IDS
  Q175-02 Q175-03 Q175-04 Q175-05 Q175-06 Q175-07 Q175-08 Q175-09
  Q175-10 Q175-11 Q175-12 Q175-13 Q175-14 Q175-15 Q175-16 Q175-17
  Q175-18 Q175-19 Q175-20)
foreach(FSIM_EXPECTED_ID IN LISTS FSIM_EXPECTED_IDS)
  string(REPLACE "Q175-" "B175-C" FSIM_EXPECTED_CHANGE "${FSIM_EXPECTED_ID}")
  if(NOT FSIM_EXPECTED_ID IN_LIST FSIM_IDS OR
     NOT FSIM_EXPECTED_CHANGE IN_LIST FSIM_CHANGES)
    message(FATAL_ERROR
      "v2 qualification inventory lost ${FSIM_EXPECTED_CHANGE} ownership")
  endif()
endforeach()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 175 - Cross-platform conformance and performance qualification"
  FSIM_BATCH_START)
string(FIND "${FSIM_PLAN_TEXT}" "### Batch 176 -" FSIM_BATCH_END)
if(FSIM_BATCH_START EQUAL -1 OR FSIM_BATCH_END LESS_EQUAL FSIM_BATCH_START)
  message(FATAL_ERROR "authoritative Batch 175 plan boundary is missing")
endif()
math(EXPR FSIM_BATCH_LENGTH "${FSIM_BATCH_END} - ${FSIM_BATCH_START}")
string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_BATCH_START}
  ${FSIM_BATCH_LENGTH} FSIM_BATCH_TEXT)
foreach(FSIM_CHANGE RANGE 1 20)
  set(FSIM_CHANGE_TOKEN "\n${FSIM_CHANGE}.")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_CHANGE_TOKEN}" FSIM_CHANGE_OFFSET)
  if(FSIM_CHANGE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 175 plan lost exact Change ${FSIM_CHANGE}")
  endif()
  math(EXPR FSIM_REMAINDER_START "${FSIM_CHANGE_OFFSET} + 1")
  string(SUBSTRING "${FSIM_BATCH_TEXT}" ${FSIM_REMAINDER_START} -1 FSIM_REMAINDER)
  string(FIND "${FSIM_REMAINDER}" "${FSIM_CHANGE_TOKEN}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "Batch 175 plan duplicates Change ${FSIM_CHANGE}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "Planning and ownership only"
    "120-minute command timeouts"
    "nineteen active rows"
    "without hosted CI monitoring")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 175 plan lost required token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.v2-qualification-inventory"
    "CheckV2QualificationInventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "v2 qualification inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "v2 qualification inventory passed: rows=${FSIM_ROW_COUNT} active=${FSIM_ACTIVE_COUNT} preserved=${FSIM_PRESERVED_COUNT} deferred=${FSIM_DEFERRED_COUNT} digest=${FSIM_ACTUAL_DIGEST}")

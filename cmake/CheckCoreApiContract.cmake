# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/core_api_contract.tsv")
set(FSIM_HEADER "${FSIM_SOURCE_DIR}/include/fsim/api.h")
set(FSIM_IMPLEMENTATION "${FSIM_SOURCE_DIR}/src/api/api.cpp")
set(FSIM_SESSION_IMPLEMENTATION "${FSIM_SOURCE_DIR}/src/api/api_session.cpp")
set(FSIM_EXPORT_MAP "${FSIM_SOURCE_DIR}/cmake/fsim_api.map")
set(FSIM_PACKAGE_CONFIG "${FSIM_SOURCE_DIR}/cmake/fsimConfig.cmake.in")
set(FSIM_BUILD "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_C_PROBE "${FSIM_SOURCE_DIR}/tests/api/api_abi_c_test.c")
set(FSIM_CPP_PROBE "${FSIM_SOURCE_DIR}/tests/api/api_abi_cpp_test.cpp")
set(FSIM_LAYOUT_PROBE "${FSIM_SOURCE_DIR}/tests/api/api_abi_contract.h")
set(FSIM_INSTALLED_CONSUMER
  "${FSIM_SOURCE_DIR}/tests/api/installed_consumer/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_HEADER}"
    "${FSIM_IMPLEMENTATION}"
    "${FSIM_SESSION_IMPLEMENTATION}"
    "${FSIM_EXPORT_MAP}"
    "${FSIM_PACKAGE_CONFIG}"
    "${FSIM_BUILD}"
    "${FSIM_TEST_BUILD}"
    "${FSIM_C_PROBE}"
    "${FSIM_CPP_PROBE}"
    "${FSIM_LAYOUT_PROBE}"
    "${FSIM_INSTALLED_CONSUMER}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "core API contract input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "ad6f2dfa02f48610006c5296fec42db39a6969a29ac5549056c59baa2e14bc8e")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "core API contract digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()

file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 43)
  message(FATAL_ERROR "core API contract requires 43 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER_ROW)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER_ROW STREQUAL "kind\tname\tcontract")
  message(FATAL_ERROR "core API contract header or SPDX policy changed")
endif()

file(READ "${FSIM_HEADER}" FSIM_HEADER_TEXT)
file(READ "${FSIM_IMPLEMENTATION}" FSIM_IMPLEMENTATION_TEXT)
file(READ "${FSIM_SESSION_IMPLEMENTATION}" FSIM_SESSION_TEXT)
file(READ "${FSIM_EXPORT_MAP}" FSIM_EXPORT_TEXT)
file(READ "${FSIM_PACKAGE_CONFIG}" FSIM_PACKAGE_TEXT)
file(READ "${FSIM_BUILD}" FSIM_BUILD_TEXT)
file(READ "${FSIM_TEST_BUILD}" FSIM_TEST_BUILD_TEXT)
file(READ "${FSIM_C_PROBE}" FSIM_C_PROBE_TEXT)
file(READ "${FSIM_CPP_PROBE}" FSIM_CPP_PROBE_TEXT)
file(READ "${FSIM_LAYOUT_PROBE}" FSIM_LAYOUT_TEXT)
file(READ "${FSIM_INSTALLED_CONSUMER}" FSIM_CONSUMER_TEXT)

set(FSIM_SYMBOLS)
foreach(FSIM_INDEX RANGE 2 42)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "core API row ${FSIM_INDEX} requires three fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_KIND)
  list(GET FSIM_FIELDS 1 FSIM_NAME)
  list(GET FSIM_FIELDS 2 FSIM_VALUE)
  if(FSIM_NAME STREQUAL "" OR FSIM_VALUE STREQUAL "")
    message(FATAL_ERROR "core API row ${FSIM_INDEX} has an empty contract")
  endif()
  if(FSIM_KIND STREQUAL "symbol")
    list(FIND FSIM_SYMBOLS "${FSIM_NAME}" FSIM_DUPLICATE)
    if(NOT FSIM_DUPLICATE EQUAL -1)
      message(FATAL_ERROR "duplicate core API symbol: ${FSIM_NAME}")
    endif()
    list(APPEND FSIM_SYMBOLS "${FSIM_NAME}")
  endif()
endforeach()
list(LENGTH FSIM_SYMBOLS FSIM_SYMBOL_COUNT)
if(NOT FSIM_SYMBOL_COUNT EQUAL 31)
  message(FATAL_ERROR "core API contract requires 31 symbols")
endif()

string(FIND "${FSIM_EXPORT_TEXT}" "fsim_*;" FSIM_WILDCARD_EXPORT)
if(NOT FSIM_WILDCARD_EXPORT EQUAL -1)
  message(FATAL_ERROR "core API export map must not use a wildcard")
endif()
string(REGEX MATCHALL "[ \t]+fsim_[a-z_]+" FSIM_MAP_ENTRIES
  "${FSIM_EXPORT_TEXT}")
list(LENGTH FSIM_MAP_ENTRIES FSIM_MAP_COUNT)
if(NOT FSIM_MAP_COUNT EQUAL FSIM_SYMBOL_COUNT)
  message(FATAL_ERROR
    "core API export count changed: contract=${FSIM_SYMBOL_COUNT}, map=${FSIM_MAP_COUNT}")
endif()
foreach(FSIM_SYMBOL IN LISTS FSIM_SYMBOLS)
  foreach(FSIM_SURFACE_NAME IN ITEMS header implementation export-map c-probe cpp-probe)
    if(FSIM_SURFACE_NAME STREQUAL "header")
      set(FSIM_SURFACE "${FSIM_HEADER_TEXT}")
      set(FSIM_TOKEN "${FSIM_SYMBOL}(")
    elseif(FSIM_SURFACE_NAME STREQUAL "implementation")
      set(FSIM_SURFACE "${FSIM_IMPLEMENTATION_TEXT}")
      set(FSIM_TOKEN "${FSIM_SYMBOL}(")
    elseif(FSIM_SURFACE_NAME STREQUAL "export-map")
      set(FSIM_SURFACE "${FSIM_EXPORT_TEXT}")
      set(FSIM_TOKEN "${FSIM_SYMBOL};")
    elseif(FSIM_SURFACE_NAME STREQUAL "c-probe")
      set(FSIM_SURFACE "${FSIM_C_PROBE_TEXT}")
      set(FSIM_TOKEN "${FSIM_SYMBOL}")
    else()
      set(FSIM_SURFACE "${FSIM_CPP_PROBE_TEXT}")
      set(FSIM_TOKEN "${FSIM_SYMBOL}")
    endif()
    string(FIND "${FSIM_SURFACE}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "core API ${FSIM_SURFACE_NAME} lost ${FSIM_SYMBOL}")
    endif()
  endforeach()
endforeach()

foreach(FSIM_TOKEN IN ITEMS
    "extern \"C\""
    "FSIM_API_VERSION UINT32_C(1)"
    "Returned string views remain valid until the next call that mutates")
  string(FIND "${FSIM_HEADER_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "core API header lost policy: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "with_session("
    "noexcept"
    "catch (const std::bad_alloc&)"
    "catch (...)")
  string(FIND "${FSIM_SESSION_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "core API exception boundary lost: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "EXPORT_NAME api"
    "fsimConfig.cmake"
    "target_link_libraries(fsim_api PRIVATE")
  string(FIND "${FSIM_BUILD_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "installed core API target lost: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "add_library(fsim::api SHARED IMPORTED)"
    "INTERFACE_COMPILE_DEFINITIONS FSIM_SHARED"
    "INTERFACE_INCLUDE_DIRECTORIES"
    "IMPORTED_LOCATION"
    "check_required_components(fsim)")
  string(FIND "${FSIM_PACKAGE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "core API package config lost: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "find_package(fsim CONFIG REQUIRED)"
    "fsim::api"
    "api_abi_c_test.c"
    "api_abi_cpp_test.cpp")
  string(FIND "${FSIM_CONSUMER_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "installed core API consumer lost: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.api.abi"
    "NAME fsim.api.contract"
    "CheckCoreApiContract.cmake")
  string(FIND "${FSIM_TEST_BUILD_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "core API test registration lost: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ABI_LAYOUT(fsim_string_view_t, 16, 8)"
    "FSIM_ABI_LAYOUT(fsim_object_info_t, 208, 8)"
    "FSIM_ABI_LAYOUT(fsim_safe_point_info_t, 168, 8)"
    "FSIM_ABI_LAYOUT(fsim_callbacks_t, 56, 8)")
  string(FIND "${FSIM_LAYOUT_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "core API layout probe lost: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "core API contract passed: symbols=${FSIM_SYMBOL_COUNT} digest=${FSIM_CONTRACT_DIGEST}")

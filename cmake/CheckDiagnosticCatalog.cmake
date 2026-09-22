# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0057 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_DIAGNOSTIC_CATALOG "${FSIM_SOURCE_DIR}/docs/diagnostics.md")
if(NOT EXISTS "${FSIM_DIAGNOSTIC_CATALOG}")
  message(FATAL_ERROR "diagnostic catalog not found: ${FSIM_DIAGNOSTIC_CATALOG}")
endif()

set(
  FSIM_NON_DIAGNOSTIC_MARKERS
  "FSIM-OBJECT-CACHE-V1"
  "FSIM-CACHE-LOCK-V1"
  "FSIM-DESIGN-CACHE-V1"
  "FSIM-DESIGN-CACHE-V3"
  "FSIM-COMPILED-HIR-CACHE-V2"
)
set(FSIM_CODE_PATTERN "FSIM-[A-Z][A-Z0-9]*(-[A-Z0-9]+)+")
set(
  FSIM_COMPILED_HIR_COMPATIBILITY_REGISTRY
  "${FSIM_SOURCE_DIR}/src/diagnostic/compiled_hir_compatibility_codes.inc"
)
if(NOT EXISTS "${FSIM_COMPILED_HIR_COMPATIBILITY_REGISTRY}")
  message(
    FATAL_ERROR
    "compiled-HIR diagnostic compatibility registry not found: "
    "${FSIM_COMPILED_HIR_COMPATIBILITY_REGISTRY}"
  )
endif()

function(fsim_find_duplicate_codes input_codes output_variable)
  set(FSIM_SEEN_CODES)
  set(FSIM_DUPLICATE_CODES)
  foreach(FSIM_CODE IN LISTS input_codes)
    list(FIND FSIM_SEEN_CODES "${FSIM_CODE}" FSIM_CODE_INDEX)
    if(FSIM_CODE_INDEX EQUAL -1)
      list(APPEND FSIM_SEEN_CODES "${FSIM_CODE}")
    else()
      list(APPEND FSIM_DUPLICATE_CODES "${FSIM_CODE}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES FSIM_DUPLICATE_CODES)
  list(SORT FSIM_DUPLICATE_CODES)
  set(${output_variable} "${FSIM_DUPLICATE_CODES}" PARENT_SCOPE)
endfunction()

function(
  fsim_validate_diagnostic_contract
  active_codes
  compatibility_codes
  documented_codes
  output_code
  output_detail
)
  fsim_find_duplicate_codes(
    "${compatibility_codes}" FSIM_DUPLICATE_COMPATIBILITY_CODES)
  if(FSIM_DUPLICATE_COMPATIBILITY_CODES)
    set(${output_code} "DUPLICATE_COMPATIBILITY" PARENT_SCOPE)
    set(
      ${output_detail}
      "${FSIM_DUPLICATE_COMPATIBILITY_CODES}"
      PARENT_SCOPE
    )
    return()
  endif()

  fsim_find_duplicate_codes(
    "${documented_codes}" FSIM_DUPLICATE_DOCUMENTED_CODES)
  if(FSIM_DUPLICATE_DOCUMENTED_CODES)
    set(${output_code} "DUPLICATE_DOCUMENTED" PARENT_SCOPE)
    set(${output_detail} "${FSIM_DUPLICATE_DOCUMENTED_CODES}" PARENT_SCOPE)
    return()
  endif()

  set(FSIM_ACTIVE_CODES ${active_codes})
  set(FSIM_COMPATIBILITY_CODES ${compatibility_codes})
  set(FSIM_DOCUMENTED_CODES ${documented_codes})
  list(REMOVE_DUPLICATES FSIM_ACTIVE_CODES)
  list(REMOVE_DUPLICATES FSIM_COMPATIBILITY_CODES)
  list(REMOVE_DUPLICATES FSIM_DOCUMENTED_CODES)

  set(FSIM_OVERLAPPING_CODES)
  foreach(FSIM_CODE IN LISTS FSIM_COMPATIBILITY_CODES)
    if(FSIM_CODE IN_LIST FSIM_ACTIVE_CODES)
      list(APPEND FSIM_OVERLAPPING_CODES "${FSIM_CODE}")
    endif()
  endforeach()
  if(FSIM_OVERLAPPING_CODES)
    list(SORT FSIM_OVERLAPPING_CODES)
    set(${output_code} "ACTIVE_COMPATIBILITY_OVERLAP" PARENT_SCOPE)
    set(${output_detail} "${FSIM_OVERLAPPING_CODES}" PARENT_SCOPE)
    return()
  endif()

  set(FSIM_OWNED_CODES ${FSIM_ACTIVE_CODES} ${FSIM_COMPATIBILITY_CODES})
  list(REMOVE_DUPLICATES FSIM_OWNED_CODES)
  set(FSIM_UNDOCUMENTED_CODES ${FSIM_OWNED_CODES})
  list(REMOVE_ITEM FSIM_UNDOCUMENTED_CODES ${FSIM_DOCUMENTED_CODES})
  if(FSIM_UNDOCUMENTED_CODES)
    list(SORT FSIM_UNDOCUMENTED_CODES)
    set(${output_code} "UNDOCUMENTED" PARENT_SCOPE)
    set(${output_detail} "${FSIM_UNDOCUMENTED_CODES}" PARENT_SCOPE)
    return()
  endif()

  set(FSIM_STALE_CODES ${FSIM_DOCUMENTED_CODES})
  list(REMOVE_ITEM FSIM_STALE_CODES ${FSIM_OWNED_CODES})
  if(FSIM_STALE_CODES)
    list(SORT FSIM_STALE_CODES)
    set(${output_code} "STALE" PARENT_SCOPE)
    set(${output_detail} "${FSIM_STALE_CODES}" PARENT_SCOPE)
    return()
  endif()

  set(${output_code} "OK" PARENT_SCOPE)
  set(${output_detail} "" PARENT_SCOPE)
endfunction()

function(
  fsim_expect_diagnostic_contract_failure
  expected_code
  active_codes
  compatibility_codes
  documented_codes
)
  fsim_validate_diagnostic_contract(
    "${active_codes}"
    "${compatibility_codes}"
    "${documented_codes}"
    FSIM_ACTUAL_CODE
    FSIM_ACTUAL_DETAIL
  )
  if(NOT FSIM_ACTUAL_CODE STREQUAL expected_code)
    message(
      FATAL_ERROR
      "diagnostic-contract negative expected ${expected_code}, got "
      "${FSIM_ACTUAL_CODE}: ${FSIM_ACTUAL_DETAIL}"
    )
  endif()
endfunction()

file(
  GLOB_RECURSE
  FSIM_PRODUCTION_SOURCES
  LIST_DIRECTORIES FALSE
  "${FSIM_SOURCE_DIR}/src/*.c"
  "${FSIM_SOURCE_DIR}/src/*.cc"
  "${FSIM_SOURCE_DIR}/src/*.cpp"
  "${FSIM_SOURCE_DIR}/src/*.h"
  "${FSIM_SOURCE_DIR}/src/*.hpp"
  "${FSIM_SOURCE_DIR}/src/*.tpp"
  "${FSIM_SOURCE_DIR}/include/*.h"
  "${FSIM_SOURCE_DIR}/include/*.hpp"
  "${FSIM_SOURCE_DIR}/include/*.tpp"
)

set(FSIM_ACTIVE_CODES)
foreach(FSIM_SOURCE IN LISTS FSIM_PRODUCTION_SOURCES)
  file(READ "${FSIM_SOURCE}" FSIM_SOURCE_CONTENTS)
  string(
    REGEX MATCHALL
    "${FSIM_CODE_PATTERN}"
    FSIM_SOURCE_CODES
    "${FSIM_SOURCE_CONTENTS}"
  )
  list(APPEND FSIM_ACTIVE_CODES ${FSIM_SOURCE_CODES})
endforeach()
list(REMOVE_ITEM FSIM_ACTIVE_CODES ${FSIM_NON_DIAGNOSTIC_MARKERS})
list(REMOVE_DUPLICATES FSIM_ACTIVE_CODES)
list(SORT FSIM_ACTIVE_CODES)

file(
  STRINGS
  "${FSIM_COMPILED_HIR_COMPATIBILITY_REGISTRY}"
  FSIM_COMPATIBILITY_REGISTRY_LINES
  ENCODING UTF-8
)
set(FSIM_COMPATIBILITY_CODES)
foreach(FSIM_REGISTRY_LINE IN LISTS FSIM_COMPATIBILITY_REGISTRY_LINES)
  string(STRIP "${FSIM_REGISTRY_LINE}" FSIM_REGISTRY_LINE)
  if(FSIM_REGISTRY_LINE STREQUAL "" OR FSIM_REGISTRY_LINE MATCHES "^//")
    continue()
  endif()
  if(
    NOT FSIM_REGISTRY_LINE MATCHES
      "^FSIM_COMPILED_HIR_COMPATIBILITY_CODE\\(\"(${FSIM_CODE_PATTERN})\"\\)$"
  )
    message(
      FATAL_ERROR
      "malformed compiled-HIR diagnostic compatibility registry row: "
      "${FSIM_REGISTRY_LINE}"
    )
  endif()
  list(APPEND FSIM_COMPATIBILITY_CODES "${CMAKE_MATCH_1}")
endforeach()

file(READ "${FSIM_DIAGNOSTIC_CATALOG}" FSIM_CATALOG_CONTENTS)
string(
  REGEX MATCHALL
  "${FSIM_CODE_PATTERN}"
  FSIM_DOCUMENTED_CODES
  "${FSIM_CATALOG_CONTENTS}"
)
list(REMOVE_ITEM FSIM_DOCUMENTED_CODES ${FSIM_NON_DIAGNOSTIC_MARKERS})
fsim_validate_diagnostic_contract(
  "${FSIM_ACTIVE_CODES}"
  "${FSIM_COMPATIBILITY_CODES}"
  "${FSIM_DOCUMENTED_CODES}"
  FSIM_CONTRACT_CODE
  FSIM_CONTRACT_DETAIL
)
if(NOT FSIM_CONTRACT_CODE STREQUAL "OK")
  string(REPLACE ";" ", " FSIM_CONTRACT_DETAIL "${FSIM_CONTRACT_DETAIL}")
  message(
    FATAL_ERROR
    "diagnostic catalog contract failed (${FSIM_CONTRACT_CODE}): "
    "${FSIM_CONTRACT_DETAIL}"
  )
endif()

# Keep the validator failure modes live. These fixtures ensure the registry
# cannot become a blanket exemption from catalog and emitter ownership.
fsim_expect_diagnostic_contract_failure(
  "DUPLICATE_COMPATIBILITY"
  "FSIM-TEST-001"
  "FSIM-TEST-002;FSIM-TEST-002"
  "FSIM-TEST-001;FSIM-TEST-002"
)
fsim_expect_diagnostic_contract_failure(
  "DUPLICATE_DOCUMENTED"
  "FSIM-TEST-001"
  ""
  "FSIM-TEST-001;FSIM-TEST-001"
)
fsim_expect_diagnostic_contract_failure(
  "ACTIVE_COMPATIBILITY_OVERLAP"
  "FSIM-TEST-001"
  "FSIM-TEST-001"
  "FSIM-TEST-001"
)
fsim_expect_diagnostic_contract_failure(
  "UNDOCUMENTED"
  "FSIM-TEST-001"
  "FSIM-TEST-002"
  "FSIM-TEST-001"
)
fsim_expect_diagnostic_contract_failure(
  "STALE"
  "FSIM-TEST-001"
  ""
  "FSIM-TEST-001;FSIM-TEST-002"
)

list(LENGTH FSIM_ACTIVE_CODES FSIM_ACTIVE_CODE_COUNT)
list(LENGTH FSIM_COMPATIBILITY_CODES FSIM_COMPATIBILITY_CODE_COUNT)
math(
  EXPR
  FSIM_DIAGNOSTIC_CODE_COUNT
  "${FSIM_ACTIVE_CODE_COUNT} + ${FSIM_COMPATIBILITY_CODE_COUNT}"
)
message(
  STATUS
  "diagnostic catalog covers ${FSIM_DIAGNOSTIC_CODE_COUNT} production codes "
  "(${FSIM_ACTIVE_CODE_COUNT} active, "
  "${FSIM_COMPATIBILITY_CODE_COUNT} compiled-HIR compatibility)"
)

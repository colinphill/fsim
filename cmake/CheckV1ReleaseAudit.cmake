# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MATRIX "${FSIM_SOURCE_DIR}/docs/feature-matrix.md")
set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v1-release-audit.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS "${FSIM_MATRIX}" "${FSIM_AUDIT}" "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "v1 release-audit input not found: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_COMPOSED_GATES
  CheckDiagnosticCatalog.cmake
  CheckSourceLineBudget.cmake
  CheckV1LegalityAudit.cmake
  CheckV1ConformanceAudit.cmake
  CheckV1ConformanceCorpus.cmake
  CheckV1PortabilityAudit.cmake
  CheckV1PortabilityCorpus.cmake
  CheckIeeePackageInventory.cmake
)
foreach(FSIM_GATE IN LISTS FSIM_COMPOSED_GATES)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}"
    RESULT_VARIABLE FSIM_GATE_RESULT
    OUTPUT_VARIABLE FSIM_GATE_OUTPUT
    ERROR_VARIABLE FSIM_GATE_ERROR
  )
  if(NOT FSIM_GATE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "composed v1 release gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
endforeach()

file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
file(READ "${FSIM_AUDIT}" FSIM_AUDIT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
string(REPLACE "\r\n" "\n" FSIM_MATRIX_CONTENTS "${FSIM_MATRIX_CONTENTS}")
string(SHA256 FSIM_MATRIX_DIGEST "${FSIM_MATRIX_CONTENTS}")

set(FSIM_EXPECTED_MATRIX_DIGEST
  "dddb62ed63296b973dfbc6aa649162a9f2b634645e88098645acc6e4ba905e3e")
if(NOT FSIM_MATRIX_DIGEST STREQUAL FSIM_EXPECTED_MATRIX_DIGEST)
  message(FATAL_ERROR
    "feature-matrix digest changed: expected ${FSIM_EXPECTED_MATRIX_DIGEST}, "
    "found ${FSIM_MATRIX_DIGEST}; review rows and update the release audit")
endif()
string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_MATRIX_DIGEST}`" FSIM_DIGEST_INDEX)
if(FSIM_DIGEST_INDEX EQUAL -1)
  message(FATAL_ERROR "release audit omits the reviewed feature-matrix digest")
endif()

string(REPLACE ";" "<SEMICOLON>" FSIM_MATRIX_LINES "${FSIM_MATRIX_CONTENTS}")
string(REPLACE "\n" ";" FSIM_MATRIX_LINES "${FSIM_MATRIX_LINES}")
set(FSIM_REQUIRED_IDS)
set(FSIM_REQUIRED_COUNT 0)
set(FSIM_SV_COUNT 0)
set(FSIM_V1_SV_COUNT 0)
set(FSIM_VH_COUNT 0)
set(FSIM_V1_VH_COUNT 0)
set(FSIM_ML_COUNT 0)
set(FSIM_SC_COUNT 0)
set(FSIM_CM_COUNT 0)
set(FSIM_V1_CM_COUNT 0)

foreach(FSIM_LINE IN LISTS FSIM_MATRIX_LINES)
  if(NOT FSIM_LINE MATCHES
      "^\\| ((VH|SV|CM|SC|ML|V1-CM|V1-SV|V1-VH)-[0-9]+) \\|")
    continue()
  endif()
  set(FSIM_ID "${CMAKE_MATCH_1}")
  list(FIND FSIM_REQUIRED_IDS "${FSIM_ID}" FSIM_DUPLICATE_INDEX)
  if(NOT FSIM_DUPLICATE_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate final feature-matrix ID: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_REQUIRED_IDS "${FSIM_ID}")
  math(EXPR FSIM_REQUIRED_COUNT "${FSIM_REQUIRED_COUNT} + 1")

  if(FSIM_ID MATCHES "^V1-SV-")
    math(EXPR FSIM_V1_SV_COUNT "${FSIM_V1_SV_COUNT} + 1")
  elseif(FSIM_ID MATCHES "^V1-VH-")
    math(EXPR FSIM_V1_VH_COUNT "${FSIM_V1_VH_COUNT} + 1")
  elseif(FSIM_ID MATCHES "^V1-CM-")
    math(EXPR FSIM_V1_CM_COUNT "${FSIM_V1_CM_COUNT} + 1")
  elseif(FSIM_ID MATCHES "^SV-")
    math(EXPR FSIM_SV_COUNT "${FSIM_SV_COUNT} + 1")
  elseif(FSIM_ID MATCHES "^VH-")
    math(EXPR FSIM_VH_COUNT "${FSIM_VH_COUNT} + 1")
  elseif(FSIM_ID MATCHES "^ML-")
    math(EXPR FSIM_ML_COUNT "${FSIM_ML_COUNT} + 1")
  elseif(FSIM_ID MATCHES "^SC-")
    math(EXPR FSIM_SC_COUNT "${FSIM_SC_COUNT} + 1")
  elseif(FSIM_ID MATCHES "^CM-")
    math(EXPR FSIM_CM_COUNT "${FSIM_CM_COUNT} + 1")
  else()
    message(FATAL_ERROR "unowned final feature-matrix ID: ${FSIM_ID}")
  endif()
endforeach()

set(FSIM_EXPECTED_COUNTS
  FSIM_REQUIRED_COUNT 1294
  FSIM_SV_COUNT 854
  FSIM_V1_SV_COUNT 9
  FSIM_VH_COUNT 279
  FSIM_V1_VH_COUNT 8
  FSIM_ML_COUNT 17
  FSIM_SC_COUNT 28
  FSIM_CM_COUNT 89
  FSIM_V1_CM_COUNT 10
)
while(FSIM_EXPECTED_COUNTS)
  list(POP_FRONT FSIM_EXPECTED_COUNTS FSIM_COUNT_NAME FSIM_COUNT_EXPECTED)
  if(NOT ${FSIM_COUNT_NAME} EQUAL FSIM_COUNT_EXPECTED)
    message(FATAL_ERROR
      "final release baseline changed for ${FSIM_COUNT_NAME}: expected "
      "${FSIM_COUNT_EXPECTED}, found ${${FSIM_COUNT_NAME}}")
  endif()
endwhile()

set(FSIM_QUEUE_IDS
  B130-T2-SV
  B130-T3-VHDL
  B130-T4-MIXED-SYSTEMC
  B130-T5-DIFFERENTIAL
  B130-T6-PUBLIC
  B130-T7-INVENTORIES
  B130-T8-RESOURCES
  B130-T9-RECLASSIFICATION
)
foreach(FSIM_QUEUE_ID IN LISTS FSIM_QUEUE_IDS)
  string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_QUEUE_ID}`" FSIM_QUEUE_INDEX)
  if(FSIM_QUEUE_INDEX EQUAL -1)
    message(FATAL_ERROR "release audit omits closure queue ${FSIM_QUEUE_ID}")
  endif()
endforeach()
list(LENGTH FSIM_QUEUE_IDS FSIM_QUEUE_COUNT)

foreach(FSIM_INVARIANT IN ITEMS
    "Task 1 is a static local audit"
    "Every row is classified"
    "mandatory non-documentation CI inspection"
    "Stop before CI monitoring")
  string(FIND "${FSIM_AUDIT_CONTENTS}" "${FSIM_INVARIANT}" FSIM_INVARIANT_INDEX)
  if(FSIM_INVARIANT_INDEX EQUAL -1)
    message(FATAL_ERROR "release audit lost invariant: ${FSIM_INVARIANT}")
  endif()
endforeach()

string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "NAME fsim.v1-release-audit"
  FSIM_REGISTRATION_INDEX
)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "fsim.v1-release-audit is not registered with CTest")
endif()

message(STATUS
  "v1 release audit covers ${FSIM_REQUIRED_COUNT} rows across 8 surfaces; "
  "${FSIM_QUEUE_COUNT} final closure queues")

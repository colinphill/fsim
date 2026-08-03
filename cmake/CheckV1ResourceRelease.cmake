# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v1-resource-release-audit.md")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
foreach(FSIM_INPUT IN ITEMS "${FSIM_AUDIT}" "${FSIM_WORKFLOW}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "resource release-audit input not found: ${FSIM_INPUT}")
  endif()
endforeach()

set(FSIM_COMPOSED_OUTPUT)
foreach(FSIM_GATE IN ITEMS
    CheckV1PortabilityAudit.cmake
    CheckResourcePortabilityContract.cmake
    CheckMsvcDebugContract.cmake
    CheckMsvcReleaseContract.cmake
    CheckWindowsLlvmContract.cmake
    CheckToolPortabilityContract.cmake
    CheckSystemCPortabilityContract.cmake
    CheckV1PortabilityCorpus.cmake)
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
      "composed resource gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
  string(APPEND FSIM_COMPOSED_OUTPUT
    "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}\n")
endforeach()

foreach(FSIM_EXACT_OUTPUT IN ITEMS
    "v1 portability audit: 12 hosted configurations, 6 four-worker build steps, 16 explicit platform files, 8 repair queues"
    "six four-worker hosted builds, eight-link pool, compact Debug objects, 8 MiB Windows stacks"
    "MSVC Debug contract: common 8 MiB stack policy covers C/C++ test hosts"
    "MSVC Release contract: assertions stay live"
    "Windows LLVM contract: x64 MSVC ABI target"
    "v1 portability corpus: 20 exact rows cover Debug/Release")
  string(FIND
    "${FSIM_COMPOSED_OUTPUT}" "${FSIM_EXACT_OUTPUT}" FSIM_OUTPUT_INDEX)
  if(FSIM_OUTPUT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "release resource contract changed: ${FSIM_EXACT_OUTPUT}")
  endif()
endforeach()

file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
foreach(FSIM_TIMEOUT IN ITEMS
    "timeout-minutes: 20"
    "timeout-minutes: 45"
    "timeout-minutes: 70")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_TIMEOUT}" FSIM_TIMEOUT_INDEX)
  if(FSIM_TIMEOUT_INDEX EQUAL -1)
    message(FATAL_ERROR "hosted job lost bound: ${FSIM_TIMEOUT}")
  endif()
endforeach()
foreach(FSIM_COMPILER IN ITEMS
    "-DCMAKE_C_COMPILER=gcc"
    "-DCMAKE_CXX_COMPILER=g++"
    "\"clang-cl\""
    "\"cl\"")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_COMPILER}" FSIM_COMPILER_INDEX)
  if(FSIM_COMPILER_INDEX EQUAL -1)
    message(FATAL_ERROR "hosted matrix lost compiler: ${FSIM_COMPILER}")
  endif()
endforeach()
string(REGEX MATCHALL "22\\.1\\.8" FSIM_LLVM_PINS "${FSIM_WORKFLOW_CONTENTS}")
list(LENGTH FSIM_LLVM_PINS FSIM_LLVM_PIN_COUNT)
if(FSIM_LLVM_PIN_COUNT LESS 5)
  message(FATAL_ERROR
    "hosted matrix no longer pins exact LLVM 22.1.8 at every install/check seam")
endif()

file(READ "${FSIM_AUDIT}" FSIM_AUDIT_CONTENTS)
foreach(FSIM_REVIEW_ID IN ITEMS
    B130-T8-MATRIX
    B130-T8-BUILD
    B130-T8-MEMORY
    B130-T8-TESTS
    B130-T8-TRACE
    B130-T8-PLATFORM)
  string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_REVIEW_ID}`" FSIM_ID_INDEX)
  if(FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "resource audit omits review ${FSIM_REVIEW_ID}")
  endif()
endforeach()

message(STATUS
  "final resource audit: 12 hosted configurations, four-worker CI builds, "
  "eight-link local pool, bounded stacks/timeouts, traces, and 20 portability rows")

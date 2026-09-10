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
    "v1 portability audit: 9 hosted configurations plus one local sanitizer configuration, 5 two-worker build/test steps, 14 explicit platform files, 8 repair queues"
    "resource portability contract: five two-worker build/test steps, 120-minute hosted jobs, eight-link pool, compact Debug objects, 128 MiB Windows stacks"
    "MSVC Debug contract: /bigobj covers every target and the common 128 MiB stack policy covers C/C++ test hosts"
    "MSVC Release contract: assertions stay live"
    "Windows LLVM contract: x64 GNU Windows ABI target"
    "v1 portability corpus: 20 exact rows cover Debug/Release")
  string(FIND
    "${FSIM_COMPOSED_OUTPUT}" "${FSIM_EXACT_OUTPUT}" FSIM_OUTPUT_INDEX)
  if(FSIM_OUTPUT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "release resource contract changed: ${FSIM_EXACT_OUTPUT}")
  endif()
endforeach()

file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
string(REGEX MATCHALL
  "timeout-minutes:[ ]*120([ \t\r\n]|$)"
  FSIM_120_MINUTE_TIMEOUTS
  "${FSIM_WORKFLOW_CONTENTS}")
string(REGEX MATCHALL
  "timeout-minutes:"
  FSIM_HOSTED_TIMEOUTS
  "${FSIM_WORKFLOW_CONTENTS}")
list(LENGTH FSIM_120_MINUTE_TIMEOUTS FSIM_120_MINUTE_TIMEOUT_COUNT)
list(LENGTH FSIM_HOSTED_TIMEOUTS FSIM_HOSTED_TIMEOUT_COUNT)
if(NOT FSIM_HOSTED_TIMEOUT_COUNT EQUAL 4
    OR NOT FSIM_120_MINUTE_TIMEOUT_COUNT EQUAL FSIM_HOSTED_TIMEOUT_COUNT)
  message(FATAL_ERROR
    "expected all four hosted job timeouts to be 120 minutes")
endif()
foreach(FSIM_COMPILER IN ITEMS
    "-DCMAKE_C_COMPILER=clang-22"
    "-DCMAKE_CXX_COMPILER=clang++-22"
    "LLVM_MINGW_ROOT/bin/clang.exe"
    "LLVM_MINGW_ROOT/bin/clang++.exe")
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
  "final resource audit: 9 hosted configurations, two-worker Windows CI, "
  "eight-link local pool, bounded stacks/timeouts, traces, and 20 portability rows")

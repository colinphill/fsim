# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v1-portability-audit.md")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_PRESETS "${FSIM_SOURCE_DIR}/CMakePresets.json")
set(FSIM_ROOT_CMAKE "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_FUZZ_CMAKE "${FSIM_SOURCE_DIR}/tests/fuzz/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_AUDIT}"
    "${FSIM_WORKFLOW}"
    "${FSIM_PRESETS}"
    "${FSIM_ROOT_CMAKE}"
    "${FSIM_FUZZ_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "v1 portability audit input not found: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_AUDIT}" FSIM_AUDIT_CONTENTS)
file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
file(READ "${FSIM_PRESETS}" FSIM_PRESET_CONTENTS)
file(READ "${FSIM_ROOT_CMAKE}" FSIM_ROOT_CMAKE_CONTENTS)
file(READ "${FSIM_FUZZ_CMAKE}" FSIM_FUZZ_CMAKE_CONTENTS)

set(FSIM_MATRIX_IDS
  PORT-CI-LINUX-GCC
  PORT-CI-LINUX-LLVM
  PORT-CI-SANITIZERS
  PORT-CI-FUZZ
  PORT-CI-WINDOWS-MSVC
  PORT-CI-WINDOWS-LLVM
)
set(FSIM_QUEUE_IDS
  B129-T2-GNU
  B129-T3-MSVC-DEBUG
  B129-T4-MSVC-RELEASE
  B129-T5-WINLLVM
  B129-T6-SYSTEMC
  B129-T7-TOOLS
  B129-T8-RESOURCES
  B129-T9-DIFFERENTIAL
)
foreach(FSIM_ID IN LISTS FSIM_MATRIX_IDS FSIM_QUEUE_IDS)
  string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_ID}`" FSIM_ID_INDEX)
  if(FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR
      "v1 portability audit is missing required inventory ID ${FSIM_ID}")
  endif()
endforeach()

foreach(FSIM_HOSTED_SANITIZER_MARKER IN ITEMS
    "ci-sanitizers"
    "ASAN_OPTIONS"
    "UBSAN_OPTIONS")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}"
    "${FSIM_HOSTED_SANITIZER_MARKER}" FSIM_SANITIZER_INDEX)
  if(NOT FSIM_SANITIZER_INDEX EQUAL -1)
    message(FATAL_ERROR
      "hosted CI contains sanitizer marker: ${FSIM_HOSTED_SANITIZER_MARKER}")
  endif()
endforeach()
if(FSIM_FUZZ_CMAKE_CONTENTS MATCHES
    "-fsanitize=[^\r\n]*(address|undefined)")
  message(FATAL_ERROR "hosted libFuzzer target contains ASan/UBSan instrumentation")
endif()

set(FSIM_WORKFLOW_JOBS
  linux-gcc
  linux-llvm22
  linux-fuzz
  windows-msvc
  windows-llvm22
)
foreach(FSIM_JOB IN LISTS FSIM_WORKFLOW_JOBS)
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "  ${FSIM_JOB}:" FSIM_JOB_INDEX)
  if(FSIM_JOB_INDEX EQUAL -1)
    message(FATAL_ERROR "CI portability job is missing: ${FSIM_JOB}")
  endif()
endforeach()

string(REGEX MATCHALL "--parallel 4" FSIM_CI_PARALLEL_MATCHES
  "${FSIM_WORKFLOW_CONTENTS}")
list(LENGTH FSIM_CI_PARALLEL_MATCHES FSIM_CI_PARALLEL_COUNT)
if(NOT FSIM_CI_PARALLEL_COUNT EQUAL 5)
  message(FATAL_ERROR
    "expected five four-worker CI build steps, found ${FSIM_CI_PARALLEL_COUNT}")
endif()
string(REGEX MATCH "--parallel ([0-35-9]|[1-9][0-9]+)" FSIM_OTHER_PARALLEL
  "${FSIM_WORKFLOW_CONTENTS}")
if(FSIM_OTHER_PARALLEL)
  message(FATAL_ERROR
    "CI build parallelism drifted from four workers: ${FSIM_OTHER_PARALLEL}")
endif()

set(FSIM_REQUIRED_PRESETS
  ci-linux
  ci-linux-release
  ci-sanitizers
  ci-fuzz
  ci-windows
  ci-windows-release
)
foreach(FSIM_PRESET IN LISTS FSIM_REQUIRED_PRESETS)
  string(FIND
    "${FSIM_PRESET_CONTENTS}" "\"name\": \"${FSIM_PRESET}\"" FSIM_PRESET_INDEX)
  if(FSIM_PRESET_INDEX EQUAL -1)
    message(FATAL_ERROR "portability preset is missing: ${FSIM_PRESET}")
  endif()
endforeach()

foreach(FSIM_CONTRACT IN ITEMS
    "fsim currently supports only Windows and Linux x86-64"
    "CMAKE_SIZEOF_VOID_P EQUAL 8"
    "^(x86_64|amd64|x64)$"
    "/STACK:33554432")
  string(FIND "${FSIM_ROOT_CMAKE_CONTENTS}" "${FSIM_CONTRACT}" FSIM_CONTRACT_INDEX)
  if(FSIM_CONTRACT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "root build lost portability contract: ${FSIM_CONTRACT}")
  endif()
endforeach()

set(FSIM_PLATFORM_FILES
  include/fsim/api.h
  include/fsim/systemc_abi.h
  src/app/application_analysis.cpp
  src/app/tcl.cpp
  src/compiler/object_cache.cpp
  src/platform/dynamic_library.cpp
  src/support/environment.cpp
  src/systemc/plugin_compiler.cpp
  src/systemc/plugin_compiler_common.cpp
  src/systemc/plugin_compiler_dependencies.cpp
  src/systemc/plugin_compiler_internal.hpp
  src/systemc/plugin_compiler_process.cpp
  tests/systemc/plugin_compiler_test.cpp
  tests/systemc/plugin_matrix_test.cpp
)
foreach(FSIM_RELATIVE IN LISTS FSIM_PLATFORM_FILES)
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "platform boundary file is missing: ${FSIM_RELATIVE}")
  endif()
  file(READ "${FSIM_PATH}" FSIM_PLATFORM_CONTENTS)
  if(NOT FSIM_PLATFORM_CONTENTS MATCHES "_WIN32|_MSC_VER")
    message(FATAL_ERROR
      "inventoried platform file no longer has an explicit branch: ${FSIM_RELATIVE}")
  endif()
  string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_RELATIVE}`" FSIM_AUDIT_PATH_INDEX)
  if(FSIM_AUDIT_PATH_INDEX EQUAL -1)
    message(FATAL_ERROR
      "v1 portability audit omits platform file: ${FSIM_RELATIVE}")
  endif()
endforeach()
list(LENGTH FSIM_PLATFORM_FILES FSIM_PLATFORM_FILE_COUNT)

foreach(FSIM_INVARIANT IN ITEMS
    "Text fixtures compare logical `\\n`"
    "exceptions do not cross the public C or SystemC C ABI"
    "Task 1 is a static, local audit"
    "mandatory CI-inspection boundary.")
  string(FIND "${FSIM_AUDIT_CONTENTS}" "${FSIM_INVARIANT}" FSIM_INVARIANT_INDEX)
  if(FSIM_INVARIANT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "v1 portability audit lost required policy: ${FSIM_INVARIANT}")
  endif()
endforeach()

message(STATUS
  "v1 portability audit: 11 hosted configurations plus one local sanitizer configuration, "
  "${FSIM_CI_PARALLEL_COUNT} four-worker build steps, "
  "${FSIM_PLATFORM_FILE_COUNT} explicit platform files, 8 repair queues")

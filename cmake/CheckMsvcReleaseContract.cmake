# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_ROOT_CMAKE "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_PRESETS "${FSIM_SOURCE_DIR}/CMakePresets.json")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_COMPILER "${FSIM_SOURCE_DIR}/src/systemc/plugin_compiler_common.cpp")
set(FSIM_COMPILER_TEST "${FSIM_SOURCE_DIR}/tests/systemc/plugin_compiler_test.cpp")
set(FSIM_INSTALLED_CONTRACT
  "${FSIM_SOURCE_DIR}/cmake/CheckInstalledPublicContract.cmake")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_ROOT_CMAKE}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_PRESETS}"
    "${FSIM_WORKFLOW}"
    "${FSIM_COMPILER}"
    "${FSIM_COMPILER_TEST}"
    "${FSIM_INSTALLED_CONTRACT}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "MSVC Release contract input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_ROOT_CMAKE}" FSIM_ROOT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_PRESETS}" FSIM_PRESET_CONTENTS)
file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
file(READ "${FSIM_COMPILER}" FSIM_COMPILER_CONTENTS)
file(READ "${FSIM_COMPILER_TEST}" FSIM_COMPILER_TEST_CONTENTS)
file(READ "${FSIM_INSTALLED_CONTRACT}" FSIM_INSTALLED_CONTRACT_CONTENTS)

foreach(FSIM_ASSERT_POLICY IN ITEMS
    "CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL \"MSVC\""
    "FSIM_TEST_ASSERTIONS_HEADER"
    "\"/FI\${FSIM_TEST_ASSERTIONS_HEADER}\""
    "#undef NDEBUG"
    "target_compile_options(\${target} PRIVATE -UNDEBUG)")
  string(FIND "${FSIM_ROOT_CONTENTS}" "${FSIM_ASSERT_POLICY}" FSIM_POLICY_INDEX)
  if(FSIM_POLICY_INDEX EQUAL -1)
    message(FATAL_ERROR
      "test build lost Release assertion policy: ${FSIM_ASSERT_POLICY}")
  endif()
endforeach()

string(FIND "${FSIM_ROOT_CONTENTS}"
  "PRIVATE /UNDEBUG" FSIM_MSVC_UNDEBUG_INDEX)
if(NOT FSIM_MSVC_UNDEBUG_INDEX EQUAL -1)
  message(FATAL_ERROR
    "test build restored the conflicting MSVC /DNDEBUG /UNDEBUG policy")
endif()

foreach(FSIM_RUNTIME_PROPAGATION IN ITEMS
    "-DFSIM_MSVC_RUNTIME_LIBRARY=\${CMAKE_MSVC_RUNTIME_LIBRARY}")
  string(FIND
    "${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_RUNTIME_PROPAGATION}"
    FSIM_POLICY_INDEX)
  if(FSIM_POLICY_INDEX EQUAL -1)
    message(FATAL_ERROR
      "installed test lost MSVC runtime propagation: ${FSIM_RUNTIME_PROPAGATION}")
  endif()
endforeach()

string(FIND
  "${FSIM_INSTALLED_CONTRACT_CONTENTS}"
  "-DCMAKE_MSVC_RUNTIME_LIBRARY=\${FSIM_MSVC_RUNTIME_LIBRARY}"
  FSIM_POLICY_INDEX)
if(FSIM_POLICY_INDEX EQUAL -1)
  message(FATAL_ERROR
    "installed consumer lost MSVC runtime propagation")
endif()

foreach(FSIM_PRESET_POLICY IN ITEMS
    "\"name\": \"ci-windows-release\""
    "\"CMAKE_BUILD_TYPE\": \"Release\""
    "\"CMAKE_CXX_COMPILER\": \"\$env{LLVM_MINGW_ROOT}/bin/clang++.exe\""
    "\"FSIM_TCL_MODE\": \"ON\"")
  string(FIND "${FSIM_PRESET_CONTENTS}" "${FSIM_PRESET_POLICY}" FSIM_POLICY_INDEX)
  if(FSIM_POLICY_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Windows Release preset lost policy: ${FSIM_PRESET_POLICY}")
  endif()
endforeach()

foreach(FSIM_JOB_POLICY IN ITEMS
    "windows-llvm-mingw:"
    "llvm-mingw-20260616-ucrt-x86_64.zip"
    "-DFSIM_TCL_MODE=ON"
    "-DFSIM_LLVM_MODE=\${{ matrix.llvm_mode }}"
    "configuration: Release"
    "--parallel 2")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_JOB_POLICY}" FSIM_POLICY_INDEX)
  if(FSIM_POLICY_INDEX EQUAL -1)
    message(FATAL_ERROR
      "workflow lost MSVC Release policy: ${FSIM_JOB_POLICY}")
  endif()
endforeach()

foreach(FSIM_COMPILER_POLICY IN ITEMS
    "msvc_runtime_option()"
    "msvc_debug_mode()"
    "argv.emplace_back(\"/EHsc\")"
    "argv.emplace_back(\"/utf-8\")"
    "argv.emplace_back(\"/Zc:__cplusplus\")"
    "argv.emplace_back(\"/FC\")"
    "argv.emplace_back(\"/Od\")"
    "argv.emplace_back(\"/Z7\")"
    "argv.emplace_back(\"/O2\")"
    "link_argv.emplace_back(\"/INCREMENTAL:NO\")"
    "link_argv.emplace_back(\"/MACHINE:X64\")"
    "\"/PDB:\" + path_argument(link_program_database)"
    "\"/IMPLIB:\" + path_argument(import_library)")
  string(FIND
    "${FSIM_COMPILER_CONTENTS}" "${FSIM_COMPILER_POLICY}" FSIM_POLICY_INDEX)
  if(FSIM_POLICY_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC compiler lost MSVC Release policy: ${FSIM_COMPILER_POLICY}")
  endif()
endforeach()

foreach(FSIM_EVIDENCE IN ITEMS
    "cl/clang-cl must use unique object outputs"
    "runtime_arguments == 1"
    "expected_msvc_runtime_option()"
    "command_has_argument(msvc_plan->commands[index], \"/O2\")"
    "!command_has_argument(msvc_plan->commands[index], \"/Od\")"
    "!command_has_argument(msvc_plan->commands.back(), \"/DEBUG:FULL\")")
  string(FIND
    "${FSIM_COMPILER_TEST_CONTENTS}" "${FSIM_EVIDENCE}" FSIM_POLICY_INDEX)
  if(FSIM_POLICY_INDEX EQUAL -1)
    message(FATAL_ERROR
      "MSVC command-plan test lost Release evidence: ${FSIM_EVIDENCE}")
  endif()
endforeach()

string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "fsim.msvc-release-contract"
  FSIM_REGISTRATION_INDEX)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "MSVC Release contract CTest is not registered")
endif()

message(STATUS
  "MSVC Release contract: assertions stay live; retained MSVC-compatible "
  "source support keeps explicit CRT/iterator policy while hosted Windows "
  "uses LLVM-MinGW; plug-in compile/link "
  "plans retain exact optimization, runtime, and deterministic output flags")

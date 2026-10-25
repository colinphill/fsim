# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_ROOT "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_APPLICATION "${FSIM_SOURCE_DIR}/src/app/application_analysis.cpp")
set(FSIM_CACHE "${FSIM_SOURCE_DIR}/src/compiler/object_cache.cpp")
set(FSIM_LIBRARY "${FSIM_SOURCE_DIR}/src/platform/dynamic_library.cpp")
# The per-process JIT sources, its C ABI headers and their tests were removed
# in the engine-v4 migration; their policy checks went with them.
set(FSIM_SCV_ADAPTER "${FSIM_SOURCE_DIR}/cmake/FsimScv.cmake")
set(FSIM_TCL_MINGW_ADAPTER "${FSIM_SOURCE_DIR}/cmake/BuildTclMinGW.cmake")
set(FSIM_SCV_PLUGIN_TEST
    "${FSIM_SOURCE_DIR}/tests/scv/scv_plugin_compiler_test.cpp")
set(FSIM_SCV_ARTIFACT_TEST
    "${FSIM_SOURCE_DIR}/tests/scv/scv_artifact_test.cpp")
set(FSIM_PKG_CONFIG_CONSUMER
    "${FSIM_SOURCE_DIR}/cmake/CheckInstalledPkgConfigConsumer.cmake")
set(FSIM_DETERMINISTIC_PACKAGING
    "${FSIM_SOURCE_DIR}/cmake/CheckDeterministicPackaging.cmake")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_ROOT}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_WORKFLOW}"
    "${FSIM_APPLICATION}"
    "${FSIM_CACHE}"
    "${FSIM_LIBRARY}"
    "${FSIM_SCV_ADAPTER}"
    "${FSIM_TCL_MINGW_ADAPTER}"
    "${FSIM_SCV_PLUGIN_TEST}"
    "${FSIM_SCV_ARTIFACT_TEST}"
    "${FSIM_PKG_CONFIG_CONSUMER}"
    "${FSIM_DETERMINISTIC_PACKAGING}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "Windows LLVM contract input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_ROOT}" FSIM_ROOT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
file(READ "${FSIM_APPLICATION}" FSIM_APPLICATION_CONTENTS)
file(READ "${FSIM_CACHE}" FSIM_CACHE_CONTENTS)
file(READ "${FSIM_LIBRARY}" FSIM_LIBRARY_CONTENTS)
file(READ "${FSIM_SCV_ADAPTER}" FSIM_SCV_ADAPTER_CONTENTS)
file(READ "${FSIM_TCL_MINGW_ADAPTER}" FSIM_TCL_MINGW_ADAPTER_CONTENTS)
file(READ "${FSIM_SCV_PLUGIN_TEST}" FSIM_SCV_PLUGIN_TEST_CONTENTS)
file(READ "${FSIM_SCV_ARTIFACT_TEST}" FSIM_SCV_ARTIFACT_TEST_CONTENTS)
file(READ "${FSIM_PKG_CONFIG_CONSUMER}" FSIM_PKG_CONFIG_CONSUMER_CONTENTS)
file(READ "${FSIM_DETERMINISTIC_PACKAGING}"
  FSIM_DETERMINISTIC_PACKAGING_CONTENTS)

foreach(FSIM_HOST_POLICY IN ITEMS
    "NOT CMAKE_SIZEOF_VOID_P EQUAL 8"
    "^(x86_64|amd64|x64)$")
  string(FIND "${FSIM_ROOT_CONTENTS}" "${FSIM_HOST_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "host lost Windows LLVM x64 policy: ${FSIM_HOST_POLICY}")
  endif()
endforeach()

foreach(FSIM_WARNING_POLICY IN ITEMS
    "-Wno-format"
    "-Wno-inconsistent-dllimport")
  string(FIND
    "${FSIM_SCV_ADAPTER_CONTENTS}" "${FSIM_WARNING_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SCV lost Windows third-party warning isolation: ${FSIM_WARNING_POLICY}")
  endif()
endforeach()
string(FIND
  "${FSIM_TCL_MINGW_ADAPTER_CONTENTS}"
  "override CFLAGS_WARNING += -Wno-c++-keyword" FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "Tcl lost Windows third-party warning isolation")
endif()
string(FIND
  "${FSIM_SCV_ADAPTER_CONTENTS}" "LINKER:--export-all-symbols" FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "SCV lost its MinGW DLL export policy")
endif()
string(FIND
  "${FSIM_SCV_PLUGIN_TEST_CONTENTS}"
  "#if !defined(_WIN32)\nvoid make_writable" FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "SCV plugin cleanup helper lost its Windows guard")
endif()
string(FIND "${FSIM_PKG_CONFIG_CONSUMER_CONTENTS}"
  "if(FSIM_HOST_WINDOWS)\n  file(READ" FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "Windows pkg-config consumer lost metadata-first policy")
endif()
string(FIND "${FSIM_SCV_ARTIFACT_TEST_CONTENTS}"
  "relocated_bytes.assign(" FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "SCV artifact reader lost its scoped Windows lifetime")
endif()
foreach(FSIM_PACKAGE_ROOT IN ITEMS
    FSIM_SOURCE_DIR FSIM_WORK_DIR FSIM_BINARY_DIR)
  string(FIND "${FSIM_DETERMINISTIC_PACKAGING_CONTENTS}"
    "cmake_path(ABSOLUTE_PATH ${FSIM_PACKAGE_ROOT}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "deterministic packaging lost absolute ${FSIM_PACKAGE_ROOT}")
  endif()
endforeach()
foreach(FSIM_TARGET IN ITEMS
    "x86_64-w64-windows-gnu"
    "x86_64-pc-windows-msvc"
    "x86_64-unknown-linux-gnu")
  string(FIND "${FSIM_APPLICATION_CONTENTS}" "${FSIM_TARGET}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "application cache lost ABI target: ${FSIM_TARGET}")
  endif()
endforeach()

foreach(FSIM_CACHE_POLICY IN ITEMS
    "OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION"
    "MoveFileExW("
    "MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH"
    "ERROR_SHARING_VIOLATION"
    "ERROR_LOCK_VIOLATION"
    "Sleep(5)")
  string(FIND "${FSIM_CACHE_CONTENTS}" "${FSIM_CACHE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "native cache lost Windows policy: ${FSIM_CACHE_POLICY}")
  endif()
endforeach()
foreach(FSIM_LIBRARY_POLICY IN ITEMS
    "LoadLibraryExW("
    "LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR"
    "LOAD_LIBRARY_SEARCH_DEFAULT_DIRS"
    "GetProcAddress("
    "FreeLibrary(")
  string(FIND "${FSIM_LIBRARY_CONTENTS}" "${FSIM_LIBRARY_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "dynamic loader lost Windows policy: ${FSIM_LIBRARY_POLICY}")
  endif()
endforeach()

foreach(FSIM_JOB_POLICY IN ITEMS
    "windows-llvm-mingw:"
    "timeout-minutes: 120"
    "configuration: Debug"
    "configuration: Release"
    "llvm_mode:"
    "llvm_mode: 'ON'"
    "fsim-toolchain-22.1.8-1-ucrt-x86_64.zip"
    "fsim-toolchain-22.1.8-1-ucrt-x86_64-llvm-dev.zip"
    "-DLLVM_DIR=\$env:LLVM_MINGW_ROOT/lib/cmake/llvm"
    "github-windows-debug-llvm-on"
    "github-windows-release-llvm-on"
    "fsim-v3.0.0-windows-x86_64-llvm-mingw-llvm22.zip"
    "-DFSIM_BINARY_ONLY=ON"
    "cmake/CheckV3InstalledArchive.cmake"
    "actions/upload-artifact@v7"
    "--parallel 2")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_JOB_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "workflow lost Windows LLVM policy: ${FSIM_JOB_POLICY}")
  endif()
endforeach()

string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "fsim.windows-llvm-contract"
  FSIM_REGISTRATION_INDEX)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "Windows LLVM contract CTest is not registered")
endif()

message(STATUS
  "Windows LLVM contract: x64 GNU Windows ABI target, native PE/COFF target and "
  "cache identity, safe DLL ownership, atomic cache replace, "
  "third-party warning isolation, two retained "
  "LLVM-MinGW hosted artifacts and one Release binary archive lane are present")

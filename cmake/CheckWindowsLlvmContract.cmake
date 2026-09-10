# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_ROOT "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_APPLICATION "${FSIM_SOURCE_DIR}/src/app/application_analysis.cpp")
set(FSIM_JIT "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit.cpp")
set(FSIM_JIT_KEY "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_cache_key.cpp")
set(FSIM_CACHE "${FSIM_SOURCE_DIR}/src/compiler/object_cache.cpp")
set(FSIM_LIBRARY "${FSIM_SOURCE_DIR}/src/platform/dynamic_library.cpp")
set(FSIM_ABI "${FSIM_SOURCE_DIR}/include/fsim/compiler/jit_runtime.h")
set(FSIM_ABI_TEST "${FSIM_SOURCE_DIR}/tests/compiler/jit_runtime_c_test.c")
set(FSIM_JIT_TEST "${FSIM_SOURCE_DIR}/tests/compiler/llvm_jit_cache_test.cpp")
set(FSIM_SCV_ADAPTER "${FSIM_SOURCE_DIR}/cmake/FsimScv.cmake")
set(FSIM_TCL_MINGW_ADAPTER "${FSIM_SOURCE_DIR}/cmake/BuildTclMinGW.cmake")
set(FSIM_SCV_PLUGIN_TEST
    "${FSIM_SOURCE_DIR}/tests/scv/scv_plugin_compiler_test.cpp")
set(FSIM_SCV_ARTIFACT_TEST
    "${FSIM_SOURCE_DIR}/tests/scv/scv_artifact_test.cpp")
set(FSIM_PKG_CONFIG_CONSUMER
    "${FSIM_SOURCE_DIR}/cmake/CheckInstalledPkgConfigConsumer.cmake")
set(FSIM_RELEASE_RECORDS
    "${FSIM_SOURCE_DIR}/cmake/CheckV2ReleaseRecords.cmake")
set(FSIM_PERFORMANCE_BASELINES
    "${FSIM_SOURCE_DIR}/cmake/CheckV2PerformanceBaselines.cmake")
set(FSIM_ABI_EVIDENCE
    "${FSIM_SOURCE_DIR}/cmake/CheckAbiSchemaEvidenceMatrix.cmake")
set(FSIM_DETERMINISTIC_PACKAGING
    "${FSIM_SOURCE_DIR}/cmake/CheckDeterministicPackaging.cmake")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_ROOT}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_WORKFLOW}"
    "${FSIM_APPLICATION}"
    "${FSIM_JIT}"
    "${FSIM_JIT_KEY}"
    "${FSIM_CACHE}"
    "${FSIM_LIBRARY}"
    "${FSIM_ABI}"
    "${FSIM_ABI_TEST}"
    "${FSIM_JIT_TEST}"
    "${FSIM_SCV_ADAPTER}"
    "${FSIM_TCL_MINGW_ADAPTER}"
    "${FSIM_SCV_PLUGIN_TEST}"
    "${FSIM_SCV_ARTIFACT_TEST}"
    "${FSIM_PKG_CONFIG_CONSUMER}"
    "${FSIM_RELEASE_RECORDS}"
    "${FSIM_PERFORMANCE_BASELINES}"
    "${FSIM_ABI_EVIDENCE}"
    "${FSIM_DETERMINISTIC_PACKAGING}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "Windows LLVM contract input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_ROOT}" FSIM_ROOT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
file(READ "${FSIM_APPLICATION}" FSIM_APPLICATION_CONTENTS)
file(READ "${FSIM_JIT}" FSIM_JIT_CONTENTS)
file(GLOB FSIM_JIT_FRAGMENTS
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_*.cpp"
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_*.hpp")
foreach(FSIM_JIT_FRAGMENT IN LISTS FSIM_JIT_FRAGMENTS)
  file(READ "${FSIM_JIT_FRAGMENT}" FSIM_JIT_FRAGMENT_CONTENTS)
  string(APPEND FSIM_JIT_CONTENTS "\n${FSIM_JIT_FRAGMENT_CONTENTS}")
endforeach()
file(READ "${FSIM_JIT_KEY}" FSIM_JIT_KEY_CONTENTS)
file(READ "${FSIM_CACHE}" FSIM_CACHE_CONTENTS)
file(READ "${FSIM_LIBRARY}" FSIM_LIBRARY_CONTENTS)
file(READ "${FSIM_ABI}" FSIM_ABI_CONTENTS)
file(READ "${FSIM_ABI_TEST}" FSIM_ABI_TEST_CONTENTS)
file(READ "${FSIM_JIT_TEST}" FSIM_JIT_TEST_CONTENTS)
file(READ "${FSIM_SCV_ADAPTER}" FSIM_SCV_ADAPTER_CONTENTS)
file(READ "${FSIM_TCL_MINGW_ADAPTER}" FSIM_TCL_MINGW_ADAPTER_CONTENTS)
file(READ "${FSIM_SCV_PLUGIN_TEST}" FSIM_SCV_PLUGIN_TEST_CONTENTS)
file(READ "${FSIM_SCV_ARTIFACT_TEST}" FSIM_SCV_ARTIFACT_TEST_CONTENTS)
file(READ "${FSIM_PKG_CONFIG_CONSUMER}" FSIM_PKG_CONFIG_CONSUMER_CONTENTS)
file(READ "${FSIM_RELEASE_RECORDS}" FSIM_RELEASE_RECORDS_CONTENTS)
file(READ "${FSIM_PERFORMANCE_BASELINES}"
  FSIM_PERFORMANCE_BASELINES_CONTENTS)
file(READ "${FSIM_ABI_EVIDENCE}" FSIM_ABI_EVIDENCE_CONTENTS)
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
foreach(FSIM_NORMALIZED_DIGEST_CONTENTS IN ITEMS
    FSIM_RELEASE_RECORDS_CONTENTS
    FSIM_PERFORMANCE_BASELINES_CONTENTS
    FSIM_ABI_EVIDENCE_CONTENTS)
  foreach(FSIM_NORMALIZED_DIGEST_POLICY IN ITEMS
      "string(REPLACE \"\\r\\n\" \"\\n\""
      "string(SHA256")
    string(FIND "${${FSIM_NORMALIZED_DIGEST_CONTENTS}}"
      "${FSIM_NORMALIZED_DIGEST_POLICY}" FSIM_INDEX)
    if(FSIM_INDEX EQUAL -1)
      message(FATAL_ERROR
        "Windows text digest lost ${FSIM_NORMALIZED_DIGEST_POLICY}")
    endif()
  endforeach()
endforeach()
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

foreach(FSIM_JIT_POLICY IN ITEMS
    "JITTargetMachineBuilder::detectHost()"
    "target_builder.getTargetTriple()"
    "target_builder.getCPU()"
    "target_builder.getFeatures().getFeatures()"
    "std::sort(impl_->target_features.begin(), impl_->target_features.end())"
    "module->setDataLayout(impl_->jit->getDataLayout())"
    "module->setTargetTriple(impl_->jit->getTargetTriple())"
    "impl_->jit->lookup(owned_symbol)"
    "address.template toPtr<NativeProcess>()")
  string(FIND "${FSIM_JIT_CONTENTS}" "${FSIM_JIT_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "JIT lost native target/symbol policy: ${FSIM_JIT_POLICY}")
  endif()
endforeach()
foreach(FSIM_KEY_POLICY IN ITEMS
    "target_triple.str()"
    "data_layout.getStringRepresentation()"
    "target_cpu"
    "target_features")
  string(FIND "${FSIM_JIT_KEY_CONTENTS}" "${FSIM_KEY_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "native cache key lost target input: ${FSIM_KEY_POLICY}")
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

foreach(FSIM_ABI_POLICY IN ITEMS
    "Callbacks must"
    "not unwind across this boundary"
    "FSIM_JIT_RUNTIME_ABI_VERSION_V1"
    "FSIM_JIT_FRAME_ABI_VERSION_V1")
  string(FIND "${FSIM_ABI_CONTENTS}" "${FSIM_ABI_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "JIT C ABI lost boundary policy: ${FSIM_ABI_POLICY}")
  endif()
endforeach()
foreach(FSIM_ABI_EVIDENCE IN ITEMS
    "offsetof(fsim_jit_runtime_v1, vital_delay) == 560"
    "offsetof(fsim_jit_runtime_v1, force_driver_signal_slice) == 568"
    "offsetof(fsim_jit_runtime_v1, force_driver_signal_slice_logic9) == 576"
    "offsetof(fsim_jit_runtime_v1, release_driver_signal_slice) == 584"
    "offsetof(fsim_jit_runtime_v1, execute_signal_operation) == 592"
    "offsetof(fsim_jit_runtime_v1, code_coverage_hit_counters) == 816"
    "offsetof(fsim_jit_runtime_v1, code_coverage_counter_values) == 824"
    "offsetof(fsim_jit_runtime_v1, code_coverage_hit_count) == 832"
    "offsetof(fsim_jit_runtime_v1, code_coverage_counter_count) == 836"
    "offsetof(fsim_jit_runtime_v1, record_code_coverage_counter) == 840"
    "sizeof(fsim_jit_runtime_v1) == 848"
    "sizeof(fsim_jit_frame_v1) == 344"
    "sizeof(fsim_jit_resume_result_v1) == 24")
  string(FIND "${FSIM_ABI_TEST_CONTENTS}" "${FSIM_ABI_EVIDENCE}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "strict C ABI test lost layout evidence: ${FSIM_ABI_EVIDENCE}")
  endif()
endforeach()

foreach(FSIM_TEST_POLICY IN ITEMS
    "JitOptimizationLevel::o0"
    "JitOptimizationLevel::o2"
    "test_optimization_cache_invalidation"
    "DebugPointKind::statement"
    "source location are unchanged")
  string(FIND "${FSIM_JIT_TEST_CONTENTS}" "${FSIM_TEST_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "JIT test lost portability evidence: ${FSIM_TEST_POLICY}")
  endif()
endforeach()
foreach(FSIM_JOB_POLICY IN ITEMS
    "windows-llvm-mingw:"
    "timeout-minutes: 120"
    "configuration: Debug"
    "configuration: Release"
    "llvm_mode:"
    "llvm_mode: 'ON'"
    "llvm_mode: 'OFF'"
    "llvm-mingw-20260616-ucrt-x86_64.zip"
    "mingw-w64-clang-x86_64-llvm-22.1.8-2"
    "/clang64/bin/lli.exe --version"
    "id: msys2"
    "steps.msys2.outputs.msys2-location"
    "LLVMConfig.cmake was not installed at"
    "github-windows-debug-llvm-off"
    "github-windows-debug-llvm-on"
    "github-windows-release-llvm-off"
    "github-windows-release-llvm-on"
    "fsim-v2.0.0-windows-x86_64-llvm-mingw-no-llvm.zip"
    "fsim-v2.0.0-windows-x86_64-llvm-mingw-llvm22.zip"
    "-DFSIM_BINARY_ONLY=ON"
    "actions/upload-artifact@v7"
    "--parallel 4")
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
  "cache identity, strict C layout, safe DLL ownership, atomic cache replace, "
  "O0/O2/debug provenance, third-party warning isolation, four retained "
  "LLVM-MinGW hosted artifacts and two Release binary archive lanes are present")

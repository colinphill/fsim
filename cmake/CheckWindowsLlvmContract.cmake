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
    "${FSIM_JIT_TEST}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "Windows LLVM contract input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_ROOT}" FSIM_ROOT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
file(READ "${FSIM_APPLICATION}" FSIM_APPLICATION_CONTENTS)
file(READ "${FSIM_JIT}" FSIM_JIT_CONTENTS)
file(READ "${FSIM_JIT_KEY}" FSIM_JIT_KEY_CONTENTS)
file(READ "${FSIM_CACHE}" FSIM_CACHE_CONTENTS)
file(READ "${FSIM_LIBRARY}" FSIM_LIBRARY_CONTENTS)
file(READ "${FSIM_ABI}" FSIM_ABI_CONTENTS)
file(READ "${FSIM_ABI_TEST}" FSIM_ABI_TEST_CONTENTS)
file(READ "${FSIM_JIT_TEST}" FSIM_JIT_TEST_CONTENTS)

foreach(FSIM_HOST_POLICY IN ITEMS
    "NOT CMAKE_SIZEOF_VOID_P EQUAL 8"
    "^(x86_64|amd64|x64)$")
  string(FIND "${FSIM_ROOT_CONTENTS}" "${FSIM_HOST_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "host lost Windows LLVM x64 policy: ${FSIM_HOST_POLICY}")
  endif()
endforeach()
foreach(FSIM_TARGET IN ITEMS
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
    "MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH")
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
    "sizeof(fsim_jit_runtime_v1) == 512"
    "sizeof(fsim_jit_frame_v1) == 80"
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
    "windows-llvm22:"
    "bounded 900-second SystemC matrix and 1,200-second"
    "timeout-minutes: 70"
    "- MSVC"
    "- Clang"
    "- Debug"
    "- Release"
    "CL: /D_ITERATOR_DEBUG_LEVEL=0"
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
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
  "Windows LLVM contract: x64 MSVC ABI target, native PE/COFF target and "
  "cache identity, strict C layout, safe DLL ownership, atomic cache replace, "
  "O0/O2/debug provenance, and MSVC/clang-cl hosted matrix are present")

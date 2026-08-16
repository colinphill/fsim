# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_WRAPPER "${FSIM_SOURCE_DIR}/tests/systemc/test_cxx_compiler.in")
set(FSIM_MATRIX "${FSIM_SOURCE_DIR}/tests/systemc/plugin_matrix_test.cpp")
set(FSIM_COMPILER "${FSIM_SOURCE_DIR}/src/systemc/plugin_compiler.cpp")
set(FSIM_COMMON "${FSIM_SOURCE_DIR}/src/systemc/plugin_compiler_common.cpp")
set(FSIM_DEPENDENCIES "${FSIM_SOURCE_DIR}/src/systemc/plugin_compiler_dependencies.cpp")
set(FSIM_PROCESS "${FSIM_SOURCE_DIR}/src/systemc/plugin_compiler_process.cpp")
set(FSIM_INCREMENTAL "${FSIM_SOURCE_DIR}/src/systemc/incremental_compiler.cpp")
set(FSIM_LOADER "${FSIM_SOURCE_DIR}/src/systemc/plugin_loader.cpp")
set(FSIM_CALLBACKS "${FSIM_SOURCE_DIR}/src/systemc/hierarchy_callbacks.cpp")
set(FSIM_EXPORTS "${FSIM_SOURCE_DIR}/src/systemc/systemc_exports.cpp")
set(FSIM_ABI_TEST "${FSIM_SOURCE_DIR}/tests/systemc/systemc_abi_c_test.c")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_TEST_CMAKE}"
    "${FSIM_WRAPPER}"
    "${FSIM_MATRIX}"
    "${FSIM_COMPILER}"
    "${FSIM_COMMON}"
    "${FSIM_DEPENDENCIES}"
    "${FSIM_PROCESS}"
    "${FSIM_INCREMENTAL}"
    "${FSIM_LOADER}"
    "${FSIM_CALLBACKS}"
    "${FSIM_EXPORTS}"
    "${FSIM_ABI_TEST}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "SystemC portability input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_WRAPPER}" FSIM_WRAPPER_CONTENTS)
file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
file(READ "${FSIM_COMPILER}" FSIM_COMPILER_CONTENTS)
file(READ "${FSIM_COMMON}" FSIM_COMMON_CONTENTS)
file(READ "${FSIM_DEPENDENCIES}" FSIM_DEPENDENCY_CONTENTS)
file(READ "${FSIM_PROCESS}" FSIM_PROCESS_CONTENTS)
file(READ "${FSIM_INCREMENTAL}" FSIM_INCREMENTAL_CONTENTS)
file(READ "${FSIM_LOADER}" FSIM_LOADER_CONTENTS)
file(READ "${FSIM_CALLBACKS}" FSIM_CALLBACK_CONTENTS)
file(READ "${FSIM_EXPORTS}" FSIM_EXPORT_CONTENTS)
file(READ "${FSIM_ABI_TEST}" FSIM_ABI_TEST_CONTENTS)

foreach(FSIM_PARENT_POLICY IN ITEMS
    "CMAKE_CXX_COMPILER_ARG1"
    "CMAKE_CXX_FLAGS"
    "FSIM_TEST_CXX_COMPILER_PATH"
    "test_cxx_compiler.in"
    "file("
    "CHMOD")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_PARENT_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC matrix lost parent-toolchain policy: ${FSIM_PARENT_POLICY}")
  endif()
endforeach()
foreach(FSIM_EXPORT_POLICY IN ITEMS
    "std::sort("
    "std::adjacent_find("
    "FSIM_SC_RUNTIME_ERROR"
    "fsim_plugin_init_v1")
  string(FIND "${FSIM_EXPORT_CONTENTS}" "${FSIM_EXPORT_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC macro export registry lost policy: ${FSIM_EXPORT_POLICY}")
  endif()
endforeach()
foreach(FSIM_WRAPPER_POLICY IN ITEMS
    "#!/bin/sh"
    "@FSIM_TEST_CXX_WRAPPER_COMMAND@"
    "@FSIM_TEST_CXX_WRAPPER_ARGUMENTS@"
    "\"$@\"")
  string(FIND "${FSIM_WRAPPER_CONTENTS}" "${FSIM_WRAPPER_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "SystemC wrapper lost argv policy: ${FSIM_WRAPPER_POLICY}")
  endif()
endforeach()
foreach(FSIM_MATRIX_POLICY IN ITEMS
    "FSIM_TEST_CXX_COMPILER"
    "warm.success && warm.cache_hit"
    "edited.cache_key != cold.cache_key"
    "repaired.cache_key == edited.cache_key"
    "hits == builds.size() - 1"
    "DynamicLibrary::is_loaded")
  string(FIND "${FSIM_MATRIX_CONTENTS}" "${FSIM_MATRIX_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "SystemC matrix lost cache/lifecycle policy: ${FSIM_MATRIX_POLICY}")
  endif()
endforeach()

foreach(FSIM_COMPILER_POLICY IN ITEMS
    "systemc-compiler-v2"
    "host-format"
    "windows_compile_mutex"
    "const std::lock_guard compile_guard"
    "add_compiler_identity"
    "add_compiler_environment_to_key"
    "add_compiler_dependencies_to_key"
    "add_linked_library_contents_to_key")
  string(FIND "${FSIM_COMPILER_CONTENTS}" "${FSIM_COMPILER_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "SystemC compiler lost key policy: ${FSIM_COMPILER_POLICY}")
  endif()
endforeach()
foreach(FSIM_LEGACY_WHOLE_ARCHIVE IN ITEMS
    "/WHOLEARCHIVE:"
    "-Wl,--whole-archive"
    "-Wl,--no-whole-archive")
  string(FIND "${FSIM_INCREMENTAL_CONTENTS}"
    "${FSIM_LEGACY_WHOLE_ARCHIVE}" FSIM_INDEX)
  if(NOT FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Incremental SystemC link still imports the legacy support archive wholesale")
  endif()
endforeach()
foreach(FSIM_COMMAND_POLICY IN ITEMS
    "msvc_runtime_option()"
    "SC_WIN_DLL"
    "/bigobj"
    "-fPIC"
    "-shared"
    "/INCREMENTAL:NO"
    "/MACHINE:X64")
  string(FIND "${FSIM_COMMON_CONTENTS}" "${FSIM_COMMAND_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "SystemC compiler lost command policy: ${FSIM_COMMAND_POLICY}")
  endif()
endforeach()
foreach(FSIM_DEPENDENCY_POLICY IN ITEMS
    "Clang's Windows Make depfiles"
    "escaped == '#'"
    "token.push_back(character)")
  string(FIND
    "${FSIM_DEPENDENCY_CONTENTS}" "${FSIM_DEPENDENCY_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC compiler lost Windows depfile policy: ${FSIM_DEPENDENCY_POLICY}")
  endif()
endforeach()
foreach(FSIM_PROCESS_POLICY IN ITEMS
    "quote_windows_argument"
    "TemporaryResponseFile"
    "CreateProcessW("
    "WaitForSingleObject(process.hProcess, INFINITE)")
  string(FIND "${FSIM_PROCESS_CONTENTS}" "${FSIM_PROCESS_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "SystemC compiler lost Windows process policy: ${FSIM_PROCESS_POLICY}")
  endif()
endforeach()
foreach(FSIM_INCREMENTAL_POLICY IN ITEMS
    "CompilerCommand command"
    "run_process(command)"
    "/sourceDependencies"
    "/INCREMENTAL:NO"
    "accellera_runtime"
    "inputs_unchanged"
    "compiler_fingerprint")
  string(FIND
    "${FSIM_INCREMENTAL_CONTENTS}" "${FSIM_INCREMENTAL_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "incremental SystemC phases lost safe cross-platform policy: "
      "${FSIM_INCREMENTAL_POLICY}")
  endif()
endforeach()

foreach(FSIM_ABI_POLICY IN ITEMS
    "host.abi_version != FSIM_SYSTEMC_ABI_VERSION"
    "host.struct_size < sizeof(fsim_sc_host_v1)"
    "registrar.struct_size < sizeof(fsim_sc_registrar_v1)"
    "initialization threw an exception"
    "factory registration threw an exception")
  string(FIND "${FSIM_LOADER_CONTENTS}" "${FSIM_ABI_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "SystemC loader lost ABI policy: ${FSIM_ABI_POLICY}")
  endif()
endforeach()
foreach(FSIM_LIFETIME_POLICY IN ITEMS
    "new RetainedPluginResourceStore"
    "store->resources.push_back")
  string(FIND "${FSIM_LOADER_CONTENTS}" "${FSIM_LIFETIME_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC loader lost process-teardown image retention: ${FSIM_LIFETIME_POLICY}")
  endif()
endforeach()
string(FIND "${FSIM_CALLBACK_CONTENTS}" "callback escaped with an exception" FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "SystemC facade lost callback exception containment")
endif()
foreach(FSIM_C_EVIDENCE IN ITEMS
    "FSIM_SYSTEMC_ABI_VERSION == 3u"
    "offsetof(fsim_sc_host_v1, current_time_femtoseconds)"
    "offsetof(fsim_sc_host_v1, wait_for_input_or_native_activity)"
    "host.struct_size = (uint32_t)sizeof(host)"
    "registrar.struct_size = (uint32_t)sizeof(registrar)")
  string(FIND "${FSIM_ABI_TEST_CONTENTS}" "${FSIM_C_EVIDENCE}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "strict SystemC C test lost evidence: ${FSIM_C_EVIDENCE}")
  endif()
endforeach()
foreach(FSIM_REMOVED_LEGACY_TOKEN IN ITEMS
    "mark_hdl_module"
    "set_hdl_module_actual"
    "set_hdl_module_implementation"
    "SC_FSIM_HDL_MODULE")
  foreach(FSIM_REMOVED_LEGACY_PATH IN ITEMS
      include/fsim/systemc_abi.h
      include/fsim/systemc/accellera.hpp
      tests/systemc/systemc_abi_c_test.c)
    file(READ
      "${FSIM_SOURCE_DIR}/${FSIM_REMOVED_LEGACY_PATH}"
      FSIM_REMOVED_LEGACY_CONTENTS)
    string(FIND "${FSIM_REMOVED_LEGACY_CONTENTS}"
      "${FSIM_REMOVED_LEGACY_TOKEN}" FSIM_INDEX)
    if(NOT FSIM_INDEX EQUAL -1)
      message(FATAL_ERROR
        "legacy SystemC facade token remains: ${FSIM_REMOVED_LEGACY_TOKEN}")
    endif()
  endforeach()
endforeach()

string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "fsim.systemc-portability-contract"
  FSIM_REGISTRATION_INDEX)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "SystemC portability contract CTest is not registered")
endif()

message(STATUS
  "SystemC portability contract: parent compiler discovery options are "
  "preserved by a fingerprinted launcher; cache, compiler, Windows process, "
  "incremental compile/link, strict C ABI, exception, thread, and loader "
  "lifetime policies are present")

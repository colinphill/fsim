# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v1-public-release-audit.md")
set(FSIM_ROOT_CMAKE "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_API_HEADER "${FSIM_SOURCE_DIR}/include/fsim/api.h")
set(FSIM_SYSTEMC_HEADER "${FSIM_SOURCE_DIR}/include/fsim/systemc_abi.h")
set(FSIM_SYSTEMC_PLUGIN_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/accellera.hpp")
set(FSIM_MAIN "${FSIM_SOURCE_DIR}/src/main.cpp")
set(FSIM_CLI "${FSIM_SOURCE_DIR}/src/cli/driver.cpp")
set(FSIM_PATH_HEADER "${FSIM_SOURCE_DIR}/include/fsim/support/path.hpp")
set(FSIM_ENVIRONMENT "${FSIM_SOURCE_DIR}/src/support/environment.cpp")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_AUDIT}"
    "${FSIM_ROOT_CMAKE}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_API_HEADER}"
    "${FSIM_SYSTEMC_HEADER}"
    "${FSIM_SYSTEMC_PLUGIN_HEADER}"
    "${FSIM_MAIN}"
    "${FSIM_CLI}"
    "${FSIM_PATH_HEADER}"
    "${FSIM_ENVIRONMENT}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "public release-audit input not found: ${FSIM_INPUT}")
  endif()
endforeach()

foreach(FSIM_GATE IN ITEMS
    CheckV1ReleaseAudit.cmake
    CheckToolPortabilityContract.cmake
    CheckSystemCPortabilityContract.cmake)
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
      "composed public gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
endforeach()

file(READ "${FSIM_AUDIT}" FSIM_AUDIT_CONTENTS)
file(READ "${FSIM_ROOT_CMAKE}" FSIM_ROOT_CMAKE_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_API_HEADER}" FSIM_API_CONTENTS)
file(READ "${FSIM_SYSTEMC_HEADER}" FSIM_SYSTEMC_CONTENTS)
file(READ "${FSIM_SYSTEMC_PLUGIN_HEADER}" FSIM_SYSTEMC_PLUGIN_CONTENTS)
file(READ "${FSIM_MAIN}" FSIM_MAIN_CONTENTS)
file(READ "${FSIM_CLI}" FSIM_CLI_CONTENTS)
file(READ "${FSIM_PATH_HEADER}" FSIM_PATH_CONTENTS)
file(READ "${FSIM_ENVIRONMENT}" FSIM_ENVIRONMENT_CONTENTS)

foreach(FSIM_TARGET IN ITEMS fsim fsim-vhdl fsim-sv fsim-elab fsim-run)
  string(FIND "${FSIM_ROOT_CMAKE_CONTENTS}" "${FSIM_TARGET}" FSIM_TARGET_INDEX)
  if(FSIM_TARGET_INDEX EQUAL -1)
    message(FATAL_ERROR "install contract omits command target ${FSIM_TARGET}")
  endif()
endforeach()
foreach(FSIM_SYSTEMC_FACADE IN ITEMS
    "SC_FSIM_EXPORT_AS"
    "make_factory_parameters")
  string(FIND
    "${FSIM_SYSTEMC_PLUGIN_CONTENTS}"
    "${FSIM_SYSTEMC_FACADE}"
    FSIM_SYSTEMC_FACADE_INDEX)
  if(FSIM_SYSTEMC_FACADE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "public SystemC facade lost ${FSIM_SYSTEMC_FACADE}")
  endif()
endforeach()
foreach(FSIM_REMOVED_SYSTEMC_FACADE IN ITEMS
    "class hdl_module"
    "SC_FSIM_HDL_MODULE")
  string(FIND
    "${FSIM_SYSTEMC_PLUGIN_CONTENTS}"
    "${FSIM_REMOVED_SYSTEMC_FACADE}"
    FSIM_SYSTEMC_FACADE_INDEX)
  if(NOT FSIM_SYSTEMC_FACADE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "removed SystemC facade remains ${FSIM_REMOVED_SYSTEMC_FACADE}")
  endif()
endforeach()
foreach(FSIM_HEADER IN ITEMS
    "include/fsim/api.h"
    "include/fsim/systemc.hpp"
    "include/fsim/systemc_abi.h"
    "include/fsim/version.hpp")
  string(FIND "${FSIM_ROOT_CMAKE_CONTENTS}" "${FSIM_HEADER}" FSIM_HEADER_INDEX)
  if(FSIM_HEADER_INDEX EQUAL -1)
    message(FATAL_ERROR "install contract omits public header ${FSIM_HEADER}")
  endif()
endforeach()
foreach(FSIM_LIBRARY IN ITEMS fsim_api fsim_systemc_plugin_exports)
  string(FIND "${FSIM_ROOT_CMAKE_CONTENTS}" "${FSIM_LIBRARY}" FSIM_LIBRARY_INDEX)
  if(FSIM_LIBRARY_INDEX EQUAL -1)
    message(FATAL_ERROR "install contract omits public library ${FSIM_LIBRARY}")
  endif()
endforeach()

foreach(FSIM_INVARIANT IN ITEMS
    "#define FSIM_API_VERSION UINT32_C(1)"
    "#define FSIM_SYSTEMC_ABI_VERSION 4u")
  string(FIND
    "${FSIM_API_CONTENTS}${FSIM_SYSTEMC_CONTENTS}"
    "${FSIM_INVARIANT}"
    FSIM_INVARIANT_INDEX)
  if(FSIM_INVARIANT_INDEX EQUAL -1)
    message(FATAL_ERROR "public version contract changed: ${FSIM_INVARIANT}")
  endif()
endforeach()
foreach(FSIM_INVARIANT IN ITEMS
    "int wmain("
    "WideCharToMultiByte("
    "return 2"
    "return 3")
  string(FIND "${FSIM_MAIN_CONTENTS}" "${FSIM_INVARIANT}" FSIM_INVARIANT_INDEX)
  if(FSIM_INVARIANT_INDEX EQUAL -1)
    message(FATAL_ERROR "CLI exit contract changed: ${FSIM_INVARIANT}")
  endif()
endforeach()
foreach(FSIM_INVARIANT IN ITEMS
    "constexpr int kSuccess = 0"
    "constexpr int kUserError = 1"
    "constexpr int kUsageError = 2"
    "constexpr int kUnavailable = 3")
  string(FIND "${FSIM_CLI_CONTENTS}" "${FSIM_INVARIANT}" FSIM_INVARIANT_INDEX)
  if(FSIM_INVARIANT_INDEX EQUAL -1)
    message(FATAL_ERROR "CLI status contract changed: ${FSIM_INVARIANT}")
  endif()
endforeach()
foreach(FSIM_INVARIANT IN ITEMS path_from_utf8 path_to_utf8)
  string(FIND "${FSIM_PATH_CONTENTS}" "${FSIM_INVARIANT}" FSIM_INVARIANT_INDEX)
  if(FSIM_INVARIANT_INDEX EQUAL -1)
    message(FATAL_ERROR "public path seam changed: ${FSIM_INVARIANT}")
  endif()
endforeach()
foreach(FSIM_INVARIANT IN ITEMS
    GetEnvironmentVariableW MultiByteToWideChar WideCharToMultiByte)
  string(FIND
    "${FSIM_ENVIRONMENT_CONTENTS}"
    "${FSIM_INVARIANT}"
    FSIM_INVARIANT_INDEX)
  if(FSIM_INVARIANT_INDEX EQUAL -1)
    message(FATAL_ERROR "Windows environment seam changed: ${FSIM_INVARIANT}")
  endif()
endforeach()

set(FSIM_REVIEW_IDS
  B130-T6-INSTALL
  B130-T6-CLI
  B130-T6-API
  B130-T6-SYSTEMC
  B130-T6-TCL-RUNTIME
)
foreach(FSIM_REVIEW_ID IN LISTS FSIM_REVIEW_IDS)
  string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_REVIEW_ID}`" FSIM_REVIEW_INDEX)
  if(FSIM_REVIEW_INDEX EQUAL -1)
    message(FATAL_ERROR "public audit omits review ${FSIM_REVIEW_ID}")
  endif()
endforeach()
foreach(FSIM_TEST_NAME IN ITEMS
    fsim.v1-public-release
    fsim.installed-public-contract)
  string(FIND
    "${FSIM_TEST_CMAKE_CONTENTS}"
    "NAME ${FSIM_TEST_NAME}"
    FSIM_REGISTRATION_INDEX)
  if(FSIM_REGISTRATION_INDEX EQUAL -1)
    message(FATAL_ERROR "${FSIM_TEST_NAME} is not registered")
  endif()
endforeach()

message(STATUS
  "final public release audit protects 5 commands, 5 header groups, "
  "2 libraries, Unicode install paths, API version 1, SystemC ABI version 3, and CLI statuses")

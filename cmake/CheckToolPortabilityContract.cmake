# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_PATH "${FSIM_SOURCE_DIR}/include/fsim/support/path.hpp")
set(FSIM_MAIN "${FSIM_SOURCE_DIR}/src/main.cpp")
set(FSIM_CLI "${FSIM_SOURCE_DIR}/src/cli/driver.cpp")
set(FSIM_API "${FSIM_SOURCE_DIR}/src/api/api.cpp")
set(FSIM_PROJECT "${FSIM_SOURCE_DIR}/src/project/project.cpp")
set(FSIM_PREPROCESSOR
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_preprocessor.cpp")
set(FSIM_APPLICATION_ANALYSIS
  "${FSIM_SOURCE_DIR}/src/app/application_analysis.cpp")
set(FSIM_APPLICATION_RUN
  "${FSIM_SOURCE_DIR}/src/app/application_run.cpp")
set(FSIM_ENV "${FSIM_SOURCE_DIR}/src/support/environment.cpp")
set(FSIM_FILES "${FSIM_SOURCE_DIR}/src/runtime/simir_files.cpp")
set(FSIM_TCL "${FSIM_SOURCE_DIR}/src/app/tcl.cpp")
set(FSIM_API_TEST "${FSIM_SOURCE_DIR}/tests/api/api_test.cpp")
set(FSIM_API_SYSTEMC_TEST
  "${FSIM_SOURCE_DIR}/tests/api/api_systemc_test.cpp")
set(FSIM_CLI_TEST "${FSIM_SOURCE_DIR}/tests/app/application_test_cli.cpp")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_TEST_CMAKE}"
    "${FSIM_PATH}"
    "${FSIM_MAIN}"
    "${FSIM_CLI}"
    "${FSIM_API}"
    "${FSIM_PROJECT}"
    "${FSIM_PREPROCESSOR}"
    "${FSIM_APPLICATION_ANALYSIS}"
    "${FSIM_APPLICATION_RUN}"
    "${FSIM_ENV}"
    "${FSIM_FILES}"
    "${FSIM_TCL}"
    "${FSIM_API_TEST}"
    "${FSIM_API_SYSTEMC_TEST}"
    "${FSIM_CLI_TEST}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "tool portability input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_PATH}" FSIM_PATH_CONTENTS)
file(READ "${FSIM_MAIN}" FSIM_MAIN_CONTENTS)
file(READ "${FSIM_CLI}" FSIM_CLI_CONTENTS)
file(READ "${FSIM_API}" FSIM_API_CONTENTS)
file(READ "${FSIM_PROJECT}" FSIM_PROJECT_CONTENTS)
file(READ "${FSIM_PREPROCESSOR}" FSIM_PREPROCESSOR_CONTENTS)
file(READ
  "${FSIM_APPLICATION_ANALYSIS}"
  FSIM_APPLICATION_ANALYSIS_CONTENTS)
file(READ "${FSIM_APPLICATION_RUN}" FSIM_APPLICATION_RUN_CONTENTS)
file(READ "${FSIM_ENV}" FSIM_ENV_CONTENTS)
file(READ "${FSIM_FILES}" FSIM_FILE_CONTENTS)
file(READ "${FSIM_TCL}" FSIM_TCL_CONTENTS)
file(READ "${FSIM_API_TEST}" FSIM_API_TEST_CONTENTS)
file(READ "${FSIM_API_SYSTEMC_TEST}" FSIM_API_SYSTEMC_TEST_CONTENTS)
string(APPEND FSIM_API_TEST_CONTENTS
  "\n${FSIM_API_SYSTEMC_TEST_CONTENTS}")
file(READ "${FSIM_CLI_TEST}" FSIM_CLI_TEST_CONTENTS)

foreach(FSIM_PATH_POLICY IN ITEMS
    "path_from_utf8("
    "path_to_utf8("
    "generic_u8string()"
    "static_cast<unsigned char>(byte)")
  string(FIND "${FSIM_PATH_CONTENTS}" "${FSIM_PATH_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "UTF-8 path seam lost policy: ${FSIM_PATH_POLICY}")
  endif()
endforeach()
foreach(FSIM_MAIN_POLICY IN ITEMS
    "int wmain("
    "WideCharToMultiByte("
    "CP_UTF8"
    "WC_ERR_INVALID_CHARS"
    "return 2"
    "return 3")
  string(FIND "${FSIM_MAIN_CONTENTS}" "${FSIM_MAIN_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "Windows CLI entry lost policy: ${FSIM_MAIN_POLICY}")
  endif()
endforeach()
foreach(FSIM_CLI_POLICY IN ITEMS
    "constexpr int kSuccess = 0"
    "constexpr int kUserError = 1"
    "constexpr int kUsageError = 2"
    "constexpr int kUnavailable = 3"
    "path_from_utf8(argv[0])"
    "path_from_utf8(*value)"
    "path_from_utf8(argument)")
  string(FIND "${FSIM_CLI_CONTENTS}" "${FSIM_CLI_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "CLI lost boundary policy: ${FSIM_CLI_POLICY}")
  endif()
endforeach()
string(FIND
  "${FSIM_API_CONTENTS}"
  "path_from_utf8(manifest_path)"
  FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "public C API lost UTF-8 manifest path conversion")
endif()
foreach(FSIM_PROJECT_POLICY IN ITEMS
    "path_from_utf8(item)"
    "path_from_utf8(value.text)"
    "path_from_utf8(source_name)")
  string(FIND
    "${FSIM_PROJECT_CONTENTS}"
    "${FSIM_PROJECT_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "project parser lost UTF-8 path policy: ${FSIM_PROJECT_POLICY}")
  endif()
endforeach()
foreach(FSIM_SOURCE_POLICY IN ITEMS
    "path_from_utf8(source.name)"
    "path_from_utf8(requested)"
    "path_to_utf8(normalized)")
  string(FIND
    "${FSIM_PREPROCESSOR_CONTENTS}"
    "${FSIM_SOURCE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "preprocessor lost UTF-8 path policy: ${FSIM_SOURCE_POLICY}")
  endif()
endforeach()
string(FIND
  "${FSIM_APPLICATION_ANALYSIS_CONTENTS}"
  "path_from_utf8(physical_source(unit.span))"
  FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "analysis lost UTF-8 physical-source conversion")
endif()
string(FIND
  "${FSIM_APPLICATION_RUN_CONTENTS}"
  "path_from_utf8(source)"
  FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "application cache lost UTF-8 source conversion")
endif()
foreach(FSIM_ENV_POLICY IN ITEMS
    "GetEnvironmentVariableW("
    "MultiByteToWideChar("
    "WideCharToMultiByte(")
  string(FIND "${FSIM_ENV_CONTENTS}" "${FSIM_ENV_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "environment seam lost Unicode policy: ${FSIM_ENV_POLICY}")
  endif()
endforeach()
foreach(FSIM_FILE_POLICY IN ITEMS
    "path_from_utf8(path_text)"
    "ios::binary")
  string(FIND "${FSIM_FILE_CONTENTS}" "${FSIM_FILE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "runtime file seam lost policy: ${FSIM_FILE_POLICY}")
  endif()
endforeach()
string(FIND "${FSIM_TCL_CONTENTS}" "path_to_utf8(" FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "Tcl seam lost UTF-8 path conversion")
endif()

foreach(FSIM_API_EVIDENCE IN ITEMS
    "fsim-api-test-\\xC3\\xA9-"
    "path_to_utf8(manifest_path)"
    "path_to_utf8(assertion_manifest_path)"
    "path_to_utf8(systemc_manifest_path)")
  string(FIND "${FSIM_API_TEST_CONTENTS}" "${FSIM_API_EVIDENCE}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "API test lost UTF-8 evidence: ${FSIM_API_EVIDENCE}")
  endif()
endforeach()
foreach(FSIM_CLI_EVIDENCE IN ITEMS
    "tool-path-\\xC3\\xA9"
    "source-\\xCE\\xBB.sv"
    "trace-\\xCE\\xBB.vcd"
    "unicode_invocation->files"
    "unicode_error.str().empty()")
  string(FIND "${FSIM_CLI_TEST_CONTENTS}" "${FSIM_CLI_EVIDENCE}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "CLI test lost UTF-8 evidence: ${FSIM_CLI_EVIDENCE}")
  endif()
endforeach()

string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "fsim.tool-portability-contract"
  FSIM_REGISTRATION_INDEX)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "tool portability contract CTest is not registered")
endif()

message(STATUS
  "tool portability contract: UTF-16 argv/environment convert to UTF-8; "
  "native paths round-trip at CLI/API/Tcl/runtime seams; binary I/O, exit "
  "status, Unicode API and direct-source evidence are present")

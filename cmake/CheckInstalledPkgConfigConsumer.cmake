# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_BINARY_DIR
    FSIM_WORK_DIR
    FSIM_BINDIR
    FSIM_LIBDIR
    FSIM_C_COMPILER
    FSIM_EXECUTABLE_SUFFIX)
  if(NOT DEFINED ${FSIM_REQUIRED})
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${FSIM_WORK_DIR}")
file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")
set(FSIM_INITIAL_STAGE "${FSIM_WORK_DIR}/initial")
set(FSIM_STAGE "${FSIM_WORK_DIR}/pkg-config install é")
set(FSIM_INSTALL_COMMAND
    "${CMAKE_COMMAND}" --install "${FSIM_BINARY_DIR}"
    --prefix "${FSIM_INITIAL_STAGE}")
if(DEFINED FSIM_CONFIG AND NOT FSIM_CONFIG STREQUAL "")
  list(APPEND FSIM_INSTALL_COMMAND --config "${FSIM_CONFIG}")
endif()
execute_process(
  COMMAND ${FSIM_INSTALL_COMMAND}
  RESULT_VARIABLE FSIM_INSTALL_RESULT
  OUTPUT_VARIABLE FSIM_INSTALL_OUTPUT
  ERROR_VARIABLE FSIM_INSTALL_ERROR)
if(NOT FSIM_INSTALL_RESULT EQUAL 0)
  message(FATAL_ERROR
    "pkg-config staged install failed with ${FSIM_INSTALL_RESULT}\n"
    "${FSIM_INSTALL_OUTPUT}${FSIM_INSTALL_ERROR}")
endif()
file(RENAME "${FSIM_INITIAL_STAGE}" "${FSIM_STAGE}")

set(FSIM_PKGCONFIG_DIR "${FSIM_STAGE}/${FSIM_LIBDIR}/pkgconfig")
set(FSIM_PC "${FSIM_PKGCONFIG_DIR}/fsim.pc")
if(NOT EXISTS "${FSIM_PC}")
  message(FATAL_ERROR "installed fsim.pc is missing: ${FSIM_PC}")
endif()

find_program(FSIM_PKG_CONFIG_EXECUTABLE NAMES pkg-config pkgconf)
if(NOT FSIM_PKG_CONFIG_EXECUTABLE)
  if(FSIM_HOST_WINDOWS)
    file(READ "${FSIM_PC}" FSIM_PC_CONTENTS)
    foreach(FSIM_TOKEN IN ITEMS
        "prefix=\${pcfiledir}/../.."
        "Cflags: -I\${includedir}"
        "Libs: -L\${libdir} -lfsim_api")
      string(FIND "${FSIM_PC_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
      if(FSIM_TOKEN_INDEX EQUAL -1)
        message(FATAL_ERROR "Windows fsim.pc omits ${FSIM_TOKEN}")
      endif()
    endforeach()
    message(STATUS
      "installed pkg-config consumer: metadata-only on Windows without pkg-config; a real Windows claim requires its own hosted result")
    return()
  endif()
  message(FATAL_ERROR "pkg-config or pkgconf is required for this consumer")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PKG_CONFIG_PATH=${FSIM_PKGCONFIG_DIR}"
    "PKG_CONFIG_LIBDIR=${FSIM_PKGCONFIG_DIR}"
    "http_proxy=http://127.0.0.1:9"
    "https_proxy=http://127.0.0.1:9"
    "NO_PROXY=*"
    "${FSIM_PKG_CONFIG_EXECUTABLE}" --cflags --libs fsim
  RESULT_VARIABLE FSIM_PKG_RESULT
  OUTPUT_VARIABLE FSIM_PKG_FLAGS_TEXT
  ERROR_VARIABLE FSIM_PKG_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT FSIM_PKG_RESULT EQUAL 0)
  message(FATAL_ERROR
    "installed pkg-config discovery failed with ${FSIM_PKG_RESULT}\n"
    "${FSIM_PKG_ERROR}")
endif()
if(NOT FSIM_PKG_FLAGS_TEXT MATCHES "(^| )-I"
   OR NOT FSIM_PKG_FLAGS_TEXT MATCHES "(^| )-L"
   OR NOT FSIM_PKG_FLAGS_TEXT MATCHES "(^| )-lfsim_api($| )")
  message(FATAL_ERROR
    "installed pkg-config flags omit include, library or fsim_api ownership: "
    "${FSIM_PKG_FLAGS_TEXT}")
endif()

set(FSIM_SOURCE "${FSIM_WORK_DIR}/consumer.c")
set(FSIM_CONSUMER
    "${FSIM_WORK_DIR}/fsim-pkg-config-consumer${FSIM_EXECUTABLE_SUFFIX}")
file(WRITE "${FSIM_SOURCE}" [=[
// SPDX-License-Identifier: Apache-2.0
#include <fsim/api.h>
#include <stdio.h>
int main(void)
{
    const unsigned version = fsim_get_api_version();
    printf("api=%u\n", version);
    return version == FSIM_API_VERSION ? 0 : 1;
}
]=])
separate_arguments(FSIM_PKG_FLAGS NATIVE_COMMAND "${FSIM_PKG_FLAGS_TEXT}")
set(FSIM_COMPILE_COMMAND "${FSIM_C_COMPILER}")
if(DEFINED FSIM_C_FLAGS AND NOT FSIM_C_FLAGS STREQUAL "")
  separate_arguments(FSIM_PARSED_C_FLAGS NATIVE_COMMAND "${FSIM_C_FLAGS}")
  list(APPEND FSIM_COMPILE_COMMAND ${FSIM_PARSED_C_FLAGS})
endif()
list(APPEND FSIM_COMPILE_COMMAND "${FSIM_SOURCE}" -o "${FSIM_CONSUMER}")
list(APPEND FSIM_COMPILE_COMMAND ${FSIM_PKG_FLAGS})
if(DEFINED FSIM_EXE_LINKER_FLAGS AND NOT FSIM_EXE_LINKER_FLAGS STREQUAL "")
  separate_arguments(
    FSIM_PARSED_LINKER_FLAGS NATIVE_COMMAND "${FSIM_EXE_LINKER_FLAGS}")
  list(APPEND FSIM_COMPILE_COMMAND ${FSIM_PARSED_LINKER_FLAGS})
endif()
execute_process(
  COMMAND ${FSIM_COMPILE_COMMAND}
  RESULT_VARIABLE FSIM_COMPILE_RESULT
  OUTPUT_VARIABLE FSIM_COMPILE_OUTPUT
  ERROR_VARIABLE FSIM_COMPILE_ERROR)
if(NOT FSIM_COMPILE_RESULT EQUAL 0)
  message(FATAL_ERROR
    "installed pkg-config consumer compile failed with ${FSIM_COMPILE_RESULT}\n"
    "${FSIM_COMPILE_OUTPUT}${FSIM_COMPILE_ERROR}")
endif()

if(FSIM_HOST_WINDOWS)
  set(FSIM_RUNTIME_VARIABLE
      "PATH=${FSIM_STAGE}/${FSIM_BINDIR};$ENV{PATH}")
elseif(APPLE)
  set(FSIM_RUNTIME_VARIABLE
      "DYLD_LIBRARY_PATH=${FSIM_STAGE}/${FSIM_LIBDIR}:$ENV{DYLD_LIBRARY_PATH}")
else()
  set(FSIM_RUNTIME_VARIABLE
      "LD_LIBRARY_PATH=${FSIM_STAGE}/${FSIM_LIBDIR}:$ENV{LD_LIBRARY_PATH}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "${FSIM_RUNTIME_VARIABLE}"
    "http_proxy=http://127.0.0.1:9"
    "https_proxy=http://127.0.0.1:9"
    "NO_PROXY=*"
    "${FSIM_CONSUMER}"
  RESULT_VARIABLE FSIM_RUN_RESULT
  OUTPUT_VARIABLE FSIM_RUN_OUTPUT
  ERROR_VARIABLE FSIM_RUN_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT FSIM_RUN_RESULT EQUAL 0 OR NOT FSIM_RUN_OUTPUT STREQUAL "api=1")
  message(FATAL_ERROR
    "installed pkg-config consumer failed with ${FSIM_RUN_RESULT}\n"
    "${FSIM_RUN_OUTPUT}${FSIM_RUN_ERROR}")
endif()

message(STATUS
  "installed pkg-config consumer: relocated offline compile/run passed with ${FSIM_PKG_CONFIG_EXECUTABLE}")

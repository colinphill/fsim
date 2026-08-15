# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_SOURCE_DIR
    FSIM_BINARY_DIR
    FSIM_WORK_DIR
    FSIM_BINDIR
    FSIM_LIBDIR
    FSIM_INCLUDEDIR
    FSIM_DOCDIR
    FSIM_EXECUTABLE_NAME
    FSIM_VHDL_EXECUTABLE_NAME
    FSIM_SV_EXECUTABLE_NAME
    FSIM_ELAB_EXECUTABLE_NAME
    FSIM_RUN_EXECUTABLE_NAME
    FSIM_API_LIBRARY_NAME
    FSIM_SYSTEMC_PLUGIN_EXPORT_LIBRARY_NAME
    FSIM_SYSTEMC_ACCELERA_LIBRARY_NAME
    FSIM_SYSTEMC_UPSTREAM_LIBRARY_NAME
    FSIM_SYSTEMC_RUNTIME_DIR
    FSIM_SYSTEMC_SOURCE_ROOT
    FSIM_API_LIBRARY_DIR
    FSIM_CMAKE_GENERATOR
    FSIM_CXX_COMPILER)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${FSIM_WORK_DIR}")
set(FSIM_STAGE "${FSIM_WORK_DIR}/staged-é")
file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")
set(FSIM_INSTALL_COMMAND
  "${CMAKE_COMMAND}"
  --install "${FSIM_BINARY_DIR}"
  --prefix "${FSIM_STAGE}"
)
if(DEFINED FSIM_CONFIG AND NOT FSIM_CONFIG STREQUAL "")
  list(APPEND FSIM_INSTALL_COMMAND --config "${FSIM_CONFIG}")
endif()
execute_process(
  COMMAND ${FSIM_INSTALL_COMMAND}
  RESULT_VARIABLE FSIM_INSTALL_RESULT
  OUTPUT_VARIABLE FSIM_INSTALL_OUTPUT
  ERROR_VARIABLE FSIM_INSTALL_ERROR
)
if(NOT FSIM_INSTALL_RESULT EQUAL 0)
  message(FATAL_ERROR
    "staged public install failed with ${FSIM_INSTALL_RESULT}\n"
    "${FSIM_INSTALL_OUTPUT}${FSIM_INSTALL_ERROR}")
endif()

set(FSIM_EXPECTED_PATHS
  "${FSIM_STAGE}/${FSIM_BINDIR}/${FSIM_EXECUTABLE_NAME}"
  "${FSIM_STAGE}/${FSIM_BINDIR}/${FSIM_VHDL_EXECUTABLE_NAME}"
  "${FSIM_STAGE}/${FSIM_BINDIR}/${FSIM_SV_EXECUTABLE_NAME}"
  "${FSIM_STAGE}/${FSIM_BINDIR}/${FSIM_ELAB_EXECUTABLE_NAME}"
  "${FSIM_STAGE}/${FSIM_BINDIR}/${FSIM_RUN_EXECUTABLE_NAME}"
  "${FSIM_STAGE}/${FSIM_API_LIBRARY_DIR}/${FSIM_API_LIBRARY_NAME}"
  "${FSIM_STAGE}/${FSIM_LIBDIR}/${FSIM_SYSTEMC_PLUGIN_EXPORT_LIBRARY_NAME}"
  "${FSIM_STAGE}/${FSIM_SYSTEMC_RUNTIME_DIR}/${FSIM_SYSTEMC_ACCELERA_LIBRARY_NAME}"
  "${FSIM_STAGE}/${FSIM_SYSTEMC_RUNTIME_DIR}/${FSIM_SYSTEMC_UPSTREAM_LIBRARY_NAME}"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/systemc"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/systemc.h"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/tlm"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/tlm.h"
  "${FSIM_STAGE}/${FSIM_LIBDIR}/cmake/SystemCLanguage/SystemCLanguageConfig.cmake"
  "${FSIM_STAGE}/${FSIM_LIBDIR}/cmake/SystemCLanguage/SystemCLanguageConfigVersion.cmake"
  "${FSIM_STAGE}/${FSIM_LIBDIR}/cmake/SystemCTLM/SystemCTLMConfig.cmake"
  "${FSIM_STAGE}/${FSIM_LIBDIR}/cmake/SystemCTLM/SystemCTLMConfigVersion.cmake"
  "${FSIM_STAGE}/${FSIM_LIBDIR}/pkgconfig/systemc.pc"
  "${FSIM_STAGE}/${FSIM_LIBDIR}/pkgconfig/tlm.pc"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/api.h"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/accellera.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_protocol.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_session.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_execution.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_loopback.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_synchronization.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_value_codec.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_value_endpoint.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_tlm1.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_tlm2.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_inventory.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_inventory_accellera.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_binding_inventory.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_binding_inventory_accellera.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_observation.hpp"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc_abi.h"
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/version.hpp"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/LICENSE"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/README.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/architecture.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/systemverilog-uvm.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/uvm-tutorial.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/uvm-closure-audit.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/vhdl-psl.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/vhdl-psl-tutorial.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/vhdl-psl-closure-audit.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/verilog-2005.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/verilog-2005-tutorial.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/verilog-2005-closure-audit.md"
  "${FSIM_STAGE}/${FSIM_DOCDIR}/v1-release-audit.md"
  "${FSIM_STAGE}/share/fsim/vhdl/ieee-1076-2019/SHA256SUMS"
)
foreach(FSIM_PATH IN LISTS FSIM_EXPECTED_PATHS)
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "staged public artifact is missing: ${FSIM_PATH}")
  endif()
endforeach()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/accellera.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/accellera.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR "installed public header changed: systemc/accellera.hpp")
endif()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_protocol.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_protocol.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR
    "installed public header changed: systemc/kernel_backend_protocol.hpp")
endif()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_session.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_session.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR
    "installed public header changed: systemc/kernel_backend_session.hpp")
endif()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_execution.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_execution.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR
    "installed public header changed: systemc/kernel_backend_execution.hpp")
endif()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_loopback.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_loopback.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR
    "installed public header changed: systemc/kernel_backend_loopback.hpp")
endif()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_synchronization.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_synchronization.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR
    "installed public header changed: systemc/kernel_backend_synchronization.hpp")
endif()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_value_codec.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_value_codec.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR
    "installed public header changed: systemc/kernel_backend_value_codec.hpp")
endif()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_value_endpoint.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_value_endpoint.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR
    "installed public header changed: systemc/kernel_backend_value_endpoint.hpp")
endif()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_tlm1.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_tlm1.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR
    "installed public header changed: systemc/kernel_backend_tlm1.hpp")
endif()

file(SHA256
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_tlm2.hpp"
  FSIM_SOURCE_DIGEST)
file(SHA256
  "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/kernel_backend_tlm2.hpp"
  FSIM_INSTALLED_DIGEST)
if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
  message(FATAL_ERROR
    "installed public header changed: systemc/kernel_backend_tlm2.hpp")
endif()

foreach(FSIM_INVENTORY_HEADER IN ITEMS
    kernel_backend_inventory.hpp
    kernel_backend_inventory_accellera.hpp
    kernel_backend_binding_inventory.hpp
    kernel_backend_binding_inventory_accellera.hpp
    kernel_backend_observation.hpp)
  file(SHA256
    "${FSIM_SOURCE_DIR}/include/fsim/systemc/${FSIM_INVENTORY_HEADER}"
    FSIM_SOURCE_DIGEST)
  file(SHA256
    "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/systemc/${FSIM_INVENTORY_HEADER}"
    FSIM_INSTALLED_DIGEST)
  if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
    message(FATAL_ERROR
      "installed public header changed: systemc/${FSIM_INVENTORY_HEADER}")
  endif()
endforeach()

foreach(FSIM_UPSTREAM_HEADER IN ITEMS systemc systemc.h tlm tlm.h)
  file(SHA256
    "${FSIM_SYSTEMC_SOURCE_ROOT}/src/${FSIM_UPSTREAM_HEADER}"
    FSIM_SOURCE_DIGEST)
  file(SHA256
    "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/${FSIM_UPSTREAM_HEADER}"
    FSIM_INSTALLED_DIGEST)
  if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
    message(FATAL_ERROR
      "installed official SystemC header changed: ${FSIM_UPSTREAM_HEADER}")
  endif()
endforeach()

file(READ
  "${FSIM_STAGE}/${FSIM_LIBDIR}/pkgconfig/systemc.pc"
  FSIM_SYSTEMC_PC)
file(READ
  "${FSIM_STAGE}/${FSIM_LIBDIR}/pkgconfig/tlm.pc"
  FSIM_TLM_PC)
foreach(FSIM_TOKEN IN ITEMS
    "prefix=\${pcfiledir}/../.."
    "Name: SystemC"
    "Version: 3.0.2"
    "Libs: -L\${libarchdir} -lsystemc")
  string(FIND "${FSIM_SYSTEMC_PC}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "installed systemc.pc omits ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "prefix=\${pcfiledir}/../.."
    "Name: TLM-2.0"
    "Version: 2.0.6"
    "Requires: systemc")
  string(FIND "${FSIM_TLM_PC}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "installed tlm.pc omits ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_LEAK IN ITEMS "${FSIM_SOURCE_DIR}" "${FSIM_BINARY_DIR}" "/usr/local")
  string(FIND "${FSIM_SYSTEMC_PC}${FSIM_TLM_PC}" "${FSIM_LEAK}" FSIM_LEAK_INDEX)
  if(NOT FSIM_LEAK_INDEX EQUAL -1)
    message(FATAL_ERROR "installed pkg-config metadata leaks ${FSIM_LEAK}")
  endif()
endforeach()

set(FSIM_CONSUMER_BUILD "${FSIM_WORK_DIR}/systemc-consumer-build")
set(FSIM_CONSUMER_CONFIGURE_COMMAND
  "${CMAKE_COMMAND}"
  -S "${FSIM_SOURCE_DIR}/tests/systemc/installed_consumer"
  -B "${FSIM_CONSUMER_BUILD}"
  -G "${FSIM_CMAKE_GENERATOR}"
  "-DCMAKE_PREFIX_PATH=${FSIM_STAGE}"
  "-DCMAKE_CXX_COMPILER=${FSIM_CXX_COMPILER}"
)
if(DEFINED FSIM_CXX_FLAGS AND NOT FSIM_CXX_FLAGS STREQUAL "")
  list(APPEND FSIM_CONSUMER_CONFIGURE_COMMAND
    "-DCMAKE_CXX_FLAGS=${FSIM_CXX_FLAGS}")
endif()
if(DEFINED FSIM_EXE_LINKER_FLAGS AND NOT FSIM_EXE_LINKER_FLAGS STREQUAL "")
  list(APPEND FSIM_CONSUMER_CONFIGURE_COMMAND
    "-DCMAKE_EXE_LINKER_FLAGS=${FSIM_EXE_LINKER_FLAGS}")
endif()
if(DEFINED FSIM_CMAKE_GENERATOR_PLATFORM
    AND NOT FSIM_CMAKE_GENERATOR_PLATFORM STREQUAL "")
  list(APPEND FSIM_CONSUMER_CONFIGURE_COMMAND
    -A "${FSIM_CMAKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED FSIM_CMAKE_GENERATOR_TOOLSET
    AND NOT FSIM_CMAKE_GENERATOR_TOOLSET STREQUAL "")
  list(APPEND FSIM_CONSUMER_CONFIGURE_COMMAND
    -T "${FSIM_CMAKE_GENERATOR_TOOLSET}")
endif()
if(DEFINED FSIM_CMAKE_MAKE_PROGRAM
    AND NOT FSIM_CMAKE_MAKE_PROGRAM STREQUAL "")
  list(APPEND FSIM_CONSUMER_CONFIGURE_COMMAND
    "-DCMAKE_MAKE_PROGRAM=${FSIM_CMAKE_MAKE_PROGRAM}")
endif()
if(DEFINED FSIM_CONFIG AND NOT FSIM_CONFIG STREQUAL "")
  list(APPEND FSIM_CONSUMER_CONFIGURE_COMMAND
    "-DCMAKE_BUILD_TYPE=${FSIM_CONFIG}")
endif()
execute_process(
  COMMAND ${FSIM_CONSUMER_CONFIGURE_COMMAND}
  RESULT_VARIABLE FSIM_CONSUMER_CONFIGURE_RESULT
  OUTPUT_VARIABLE FSIM_CONSUMER_CONFIGURE_OUTPUT
  ERROR_VARIABLE FSIM_CONSUMER_CONFIGURE_ERROR
)
if(NOT FSIM_CONSUMER_CONFIGURE_RESULT EQUAL 0)
  message(FATAL_ERROR
    "installed SystemC consumer configure failed with "
    "${FSIM_CONSUMER_CONFIGURE_RESULT}\n"
    "${FSIM_CONSUMER_CONFIGURE_OUTPUT}${FSIM_CONSUMER_CONFIGURE_ERROR}")
endif()
set(FSIM_CONSUMER_BUILD_COMMAND
  "${CMAKE_COMMAND}" --build "${FSIM_CONSUMER_BUILD}" --parallel 8)
if(DEFINED FSIM_CONFIG AND NOT FSIM_CONFIG STREQUAL "")
  list(APPEND FSIM_CONSUMER_BUILD_COMMAND --config "${FSIM_CONFIG}")
endif()
execute_process(
  COMMAND ${FSIM_CONSUMER_BUILD_COMMAND}
  RESULT_VARIABLE FSIM_CONSUMER_BUILD_RESULT
  OUTPUT_VARIABLE FSIM_CONSUMER_BUILD_OUTPUT
  ERROR_VARIABLE FSIM_CONSUMER_BUILD_ERROR
)
if(NOT FSIM_CONSUMER_BUILD_RESULT EQUAL 0)
  message(FATAL_ERROR
    "installed SystemC consumer build failed with ${FSIM_CONSUMER_BUILD_RESULT}\n"
    "${FSIM_CONSUMER_BUILD_OUTPUT}${FSIM_CONSUMER_BUILD_ERROR}")
endif()
set(FSIM_CONSUMER_PATH_FILE
  "${FSIM_CONSUMER_BUILD}/consumer-path-${FSIM_CONFIG}.txt")
if(NOT EXISTS "${FSIM_CONSUMER_PATH_FILE}")
  message(FATAL_ERROR
    "installed SystemC consumer path is missing: ${FSIM_CONSUMER_PATH_FILE}")
endif()
file(READ "${FSIM_CONSUMER_PATH_FILE}" FSIM_CONSUMER_EXECUTABLE)
set(FSIM_INSTALLED_BRIDGE
  "${FSIM_STAGE}/${FSIM_SYSTEMC_RUNTIME_DIR}/${FSIM_SYSTEMC_ACCELERA_LIBRARY_NAME}")
set(FSIM_INSTALLED_UPSTREAM
  "${FSIM_STAGE}/${FSIM_SYSTEMC_RUNTIME_DIR}/${FSIM_SYSTEMC_UPSTREAM_LIBRARY_NAME}")
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${FSIM_CONSUMER_EXECUTABLE}"
  LIBRARIES "${FSIM_INSTALLED_BRIDGE}"
  DIRECTORIES
    "${FSIM_STAGE}/${FSIM_BINDIR}"
    "${FSIM_STAGE}/${FSIM_LIBDIR}"
  RESOLVED_DEPENDENCIES_VAR FSIM_RESOLVED_DEPENDENCIES
  UNRESOLVED_DEPENDENCIES_VAR FSIM_UNRESOLVED_DEPENDENCIES)
set(FSIM_SYSTEMC_RUNTIME_PATHS)
foreach(FSIM_DEPENDENCY IN LISTS FSIM_RESOLVED_DEPENDENCIES)
  get_filename_component(FSIM_DEPENDENCY_NAME "${FSIM_DEPENDENCY}" NAME)
  if(FSIM_DEPENDENCY_NAME MATCHES "systemc"
      AND NOT FSIM_DEPENDENCY_NAME MATCHES "fsim_systemc")
    file(REAL_PATH "${FSIM_DEPENDENCY}" FSIM_DEPENDENCY_REAL)
    list(APPEND FSIM_SYSTEMC_RUNTIME_PATHS "${FSIM_DEPENDENCY_REAL}")
  endif()
endforeach()
list(REMOVE_DUPLICATES FSIM_SYSTEMC_RUNTIME_PATHS)
list(LENGTH FSIM_SYSTEMC_RUNTIME_PATHS FSIM_SYSTEMC_RUNTIME_COUNT)
if(NOT FSIM_SYSTEMC_RUNTIME_COUNT EQUAL 1)
  message(FATAL_ERROR
    "installed consumers resolve ${FSIM_SYSTEMC_RUNTIME_COUNT} SystemC runtimes: "
    "${FSIM_SYSTEMC_RUNTIME_PATHS}")
endif()
file(REAL_PATH "${FSIM_INSTALLED_UPSTREAM}" FSIM_INSTALLED_UPSTREAM_REAL)
list(GET FSIM_SYSTEMC_RUNTIME_PATHS 0 FSIM_RESOLVED_SYSTEMC_RUNTIME)
if(NOT FSIM_RESOLVED_SYSTEMC_RUNTIME STREQUAL FSIM_INSTALLED_UPSTREAM_REAL)
  message(FATAL_ERROR
    "installed consumers resolve the wrong SystemC runtime: "
    "${FSIM_RESOLVED_SYSTEMC_RUNTIME}")
endif()
execute_process(
  COMMAND "${FSIM_CONSUMER_EXECUTABLE}"
  RESULT_VARIABLE FSIM_CONSUMER_RESULT
  OUTPUT_VARIABLE FSIM_CONSUMER_OUTPUT
  ERROR_VARIABLE FSIM_CONSUMER_ERROR
)
if(NOT FSIM_CONSUMER_RESULT EQUAL 0)
  message(FATAL_ERROR
    "installed SystemC consumer failed with ${FSIM_CONSUMER_RESULT}\n"
    "${FSIM_CONSUMER_OUTPUT}${FSIM_CONSUMER_ERROR}")
endif()

foreach(FSIM_HEADER IN ITEMS api.h systemc.hpp systemc_abi.h version.hpp)
  file(SHA256
    "${FSIM_SOURCE_DIR}/include/fsim/${FSIM_HEADER}"
    FSIM_SOURCE_DIGEST)
  file(SHA256
    "${FSIM_STAGE}/${FSIM_INCLUDEDIR}/fsim/${FSIM_HEADER}"
    FSIM_INSTALLED_DIGEST)
  if(NOT FSIM_SOURCE_DIGEST STREQUAL FSIM_INSTALLED_DIGEST)
    message(FATAL_ERROR "installed public header changed: ${FSIM_HEADER}")
  endif()
endforeach()

set(FSIM_INSTALLED_EXECUTABLE
  "${FSIM_STAGE}/${FSIM_BINDIR}/${FSIM_EXECUTABLE_NAME}")
execute_process(
  COMMAND "${FSIM_INSTALLED_EXECUTABLE}" --version
  RESULT_VARIABLE FSIM_VERSION_RESULT
  OUTPUT_VARIABLE FSIM_VERSION_OUTPUT
  ERROR_VARIABLE FSIM_VERSION_ERROR
)
if(NOT FSIM_VERSION_RESULT EQUAL 0
    OR NOT FSIM_VERSION_OUTPUT MATCHES "^fsim 0\\.1\\.0-dev \\(C API 1\\)")
  message(FATAL_ERROR
    "installed version contract failed with ${FSIM_VERSION_RESULT}\n"
    "${FSIM_VERSION_OUTPUT}${FSIM_VERSION_ERROR}")
endif()

execute_process(
  COMMAND
    "${FSIM_STAGE}/${FSIM_BINDIR}/${FSIM_SV_EXECUTABLE_NAME}"
    --help
  RESULT_VARIABLE FSIM_HELP_RESULT
  OUTPUT_VARIABLE FSIM_HELP_OUTPUT
  ERROR_VARIABLE FSIM_HELP_ERROR
)
if(NOT FSIM_HELP_RESULT EQUAL 0
    OR NOT FSIM_HELP_OUTPUT MATCHES "^Usage: fsim-sv")
  message(FATAL_ERROR
    "installed alias help contract failed with ${FSIM_HELP_RESULT}\n"
    "${FSIM_HELP_OUTPUT}${FSIM_HELP_ERROR}")
endif()

execute_process(
  COMMAND
    "${FSIM_STAGE}/${FSIM_BINDIR}/${FSIM_VHDL_EXECUTABLE_NAME}"
    --help
  RESULT_VARIABLE FSIM_VHDL_HELP_RESULT
  OUTPUT_VARIABLE FSIM_VHDL_HELP_OUTPUT
  ERROR_VARIABLE FSIM_VHDL_HELP_ERROR
)
if(NOT FSIM_VHDL_HELP_RESULT EQUAL 0
    OR NOT FSIM_VHDL_HELP_OUTPUT MATCHES "^Usage: fsim-vhdl")
  message(FATAL_ERROR
    "installed VHDL alias help contract failed with ${FSIM_VHDL_HELP_RESULT}\n"
    "${FSIM_VHDL_HELP_OUTPUT}${FSIM_VHDL_HELP_ERROR}")
endif()

execute_process(
  COMMAND "${FSIM_INSTALLED_EXECUTABLE}" --definitely-invalid
  RESULT_VARIABLE FSIM_INVALID_RESULT
  OUTPUT_VARIABLE FSIM_INVALID_OUTPUT
  ERROR_VARIABLE FSIM_INVALID_ERROR
)
if(NOT FSIM_INVALID_RESULT EQUAL 2
    OR NOT FSIM_INVALID_ERROR MATCHES "unknown option")
  message(FATAL_ERROR
    "installed invalid-option contract failed with ${FSIM_INVALID_RESULT}\n"
    "${FSIM_INVALID_OUTPUT}${FSIM_INVALID_ERROR}")
endif()

message(STATUS
  "installed public contract passed in Unicode stage ${FSIM_STAGE}")

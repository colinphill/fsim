# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

project(
  fsim
  VERSION 2.0.0
  DESCRIPTION "Mixed-language compiled HDL simulator"
  LANGUAGES C CXX
)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(
    FATAL_ERROR
    "fsim currently supports only Windows and Linux x86-64 "
    "(detected ${CMAKE_SYSTEM_NAME})"
  )
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" FSIM_SYSTEM_PROCESSOR)
if(
  NOT CMAKE_SIZEOF_VOID_P EQUAL 8
  OR NOT FSIM_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|x64)$"
)
  message(
    FATAL_ERROR
    "fsim currently supports only Windows and Linux x86-64 "
    "(detected ${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_SIZEOF_VOID_P}-byte pointers)"
  )
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
include(cmake/FsimWarnings.cmake)
include(cmake/FsimDebugFootprint.cmake)
include(cmake/FsimNativePlugin.cmake)
include(cmake/FsimDependencies.cmake)
include(cmake/FsimTcl.cmake)
include(cmake/FsimUvmSources.cmake)
include(cmake/FsimSystemCAccellera.cmake)
include(cmake/FsimScv.cmake)

option(FSIM_BUILD_TESTS "Build fsim's unit and integration tests" ${BUILD_TESTING})
option(FSIM_BUILD_FUZZERS "Build Clang/libFuzzer targets" OFF)
option(FSIM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(
  FSIM_LLVM_MODE
  "AUTO"
  CACHE STRING
  "LLVM backend selection: AUTO discovers LLVM 22.1.8, ON requires it, OFF disables it"
)
set_property(CACHE FSIM_LLVM_MODE PROPERTY STRINGS AUTO ON OFF)
set(
  FSIM_TCL_MODE
  "AUTO"
  CACHE STRING
  "Tcl interface selection: AUTO/ON discovers or downloads Tcl, OFF disables it"
)
set_property(CACHE FSIM_TCL_MODE PROPERTY STRINGS AUTO ON OFF)
set(
  FSIM_UVM_SOURCE_MODE
  "OFF"
  CACHE STRING
  "External UVM sources: OFF, FETCH pinned Accellera archives, or ARCHIVE exact local archives"
)
set_property(CACHE FSIM_UVM_SOURCE_MODE PROPERTY STRINGS OFF FETCH ARCHIVE)
set(
  FSIM_UVM_WORK_ROOT
  "${CMAKE_BINARY_DIR}/_deps/fsim_uvm"
  CACHE PATH
  "Isolated download, extraction, and manifest root for governed UVM sources"
)
set(
  FSIM_UVM_1_2_ARCHIVE
  ""
  CACHE FILEPATH
  "Predownloaded exact UVM 1.2 archive for FSIM_UVM_SOURCE_MODE=ARCHIVE"
)
set(
  FSIM_UVM_2020_3_1_ARCHIVE
  ""
  CACHE FILEPATH
  "Predownloaded exact UVM 2020.3.1 archive for FSIM_UVM_SOURCE_MODE=ARCHIVE"
)
fsim_uvm_configure_sources()

set(
  FSIM_SYSTEMC_3_0_2_ARCHIVE
  "${PROJECT_SOURCE_DIR}/third_party/systemc-3.0.2/systemc-3.0.2.tar.gz"
  CACHE FILEPATH
  "Offline official Accellera SystemC 3.0.2 source archive"
)
fsim_systemc_validate_archive("${FSIM_SYSTEMC_3_0_2_ARCHIVE}")
set(
  FSIM_SYSTEMC_WORK_ROOT
  "${CMAKE_BINARY_DIR}/_deps/fsim_systemc_3_0_2"
  CACHE PATH
  "Isolated extraction and manifest root for official SystemC 3.0.2"
)
fsim_systemc_add_official_runtime(
  "${FSIM_SYSTEMC_3_0_2_ARCHIVE}" "${FSIM_SYSTEMC_WORK_ROOT}")

set(
  FSIM_SCV_2_0_1_ARCHIVE
  "${PROJECT_SOURCE_DIR}/third_party/scv-2.0.1/scv-2.0.1.tar.gz"
  CACHE FILEPATH
  "Offline official Accellera SCV 2.0.1 source archive"
)
fsim_scv_validate_archive("${FSIM_SCV_2_0_1_ARCHIVE}")
set(
  FSIM_SCV_WORK_ROOT
  "${CMAKE_BINARY_DIR}/_deps/fsim_scv_2_0_1"
  CACHE PATH
  "Isolated extraction and manifest root for official SCV 2.0.1"
)
# The Windows SystemC package deliberately exposes SystemC::systemc as a
# static convenience launcher containing main(). fsim and its libraries own
# their entrypoints, so they must consume only the shared core/import target.
# Native sc_main executables continue to link SystemC::systemc explicitly.
if(WIN32)
  set(FSIM_SYSTEMC_INTERNAL_RUNTIME_TARGET
      "${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}")
else()
  set(FSIM_SYSTEMC_INTERNAL_RUNTIME_TARGET SystemC::systemc)
endif()

fsim_scv_add_official_runtime(
  "${FSIM_SCV_2_0_1_ARCHIVE}" "${FSIM_SCV_WORK_ROOT}")

add_library(
  fsim_systemc_accellera_runtime
  SHARED
  src/systemc/accellera_runtime.cpp
  src/systemc/accellera_compatibility.cpp
)
add_library(
  fsim::systemc_accellera_runtime
  ALIAS fsim_systemc_accellera_runtime
)
target_compile_features(fsim_systemc_accellera_runtime PRIVATE cxx_std_20)
target_compile_definitions(
  fsim_systemc_accellera_runtime
  PRIVATE
    "FSIM_SYSTEMC_RUNTIME_IDENTITY=\"${FSIM_SYSTEMC_RUNTIME_IDENTITY}\""
)
target_include_directories(
  fsim_systemc_accellera_runtime
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
target_include_directories(
  fsim_systemc_accellera_runtime
  SYSTEM PRIVATE "${FSIM_SYSTEMC_OFFICIAL_SOURCE_DIR}/src"
)
target_link_libraries(
  fsim_systemc_accellera_runtime
  PRIVATE "${FSIM_SYSTEMC_INTERNAL_RUNTIME_TARGET}"
)
if(
  MSVC
  OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
)
  target_link_options(
    fsim_systemc_accellera_runtime
    INTERFACE /INCLUDE:fsim_systemc_accellera_runtime_identity
  )
else()
  target_link_options(
    fsim_systemc_accellera_runtime
    INTERFACE "LINKER:-u,fsim_systemc_accellera_runtime_identity"
  )
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(
      fsim_systemc_accellera_runtime
      INTERFACE "LINKER:--no-as-needed"
    )
  endif()
endif()
set_target_properties(
  fsim_systemc_accellera_runtime
  PROPERTIES
    VERSION "${FSIM_SYSTEMC_VERSION}"
    SOVERSION 3
)
if(WIN32)
  set_target_properties(
    fsim_systemc_accellera_runtime
    PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON
  )
endif()
if(NOT WIN32)
  set_target_properties(
    fsim_systemc_accellera_runtime
    PROPERTIES INSTALL_RPATH "$ORIGIN"
  )
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
fsim_enable_warnings(fsim_systemc_accellera_runtime)
fsim_configure_debug_footprint(fsim_systemc_accellera_runtime)

fsim_add_tf_link_surface(fsim_tf)

# Targets created below all carry the same process-wide Accellera runtime
# dependency. Upstream itself was created first, so the dependency cannot form
# a private or recursive runtime copy.
link_libraries(fsim_systemc_accellera_runtime)

string(TOUPPER "${FSIM_LLVM_MODE}" FSIM_LLVM_MODE_NORMALIZED)
if(NOT FSIM_LLVM_MODE_NORMALIZED MATCHES "^(AUTO|ON|OFF)$")
  message(
    FATAL_ERROR
    "FSIM_LLVM_MODE must be AUTO, ON, or OFF (received '${FSIM_LLVM_MODE}')"
  )
endif()

set(FSIM_PRODUCTION_LLVM_VERSION "${FSIM_LLVM_VERSION}")
set(FSIM_WITH_LLVM OFF)
if(NOT FSIM_LLVM_MODE_NORMALIZED STREQUAL "OFF")
  if(FSIM_LLVM_MODE_NORMALIZED STREQUAL "ON")
    find_package(
      LLVM ${FSIM_PRODUCTION_LLVM_VERSION}
      EXACT
      CONFIG
      REQUIRED
    )
  else()
    find_package(
      LLVM ${FSIM_PRODUCTION_LLVM_VERSION}
      EXACT
      CONFIG
      QUIET
    )
  endif()

  if(LLVM_FOUND)
    if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL FSIM_PRODUCTION_LLVM_VERSION)
      message(
        FATAL_ERROR
        "fsim requires LLVM ${FSIM_PRODUCTION_LLVM_VERSION}; "
        "LLVMConfig.cmake reported ${LLVM_PACKAGE_VERSION}"
      )
    endif()
    set(FSIM_WITH_LLVM ON)
    message(STATUS "fsim LLVM backend: LLVM ${LLVM_PACKAGE_VERSION}")
  elseif(FSIM_LLVM_MODE_NORMALIZED STREQUAL "AUTO")
    message(
      STATUS
      "fsim LLVM backend disabled: LLVM ${FSIM_PRODUCTION_LLVM_VERSION} "
      "was not found (set LLVM_DIR or use -DFSIM_LLVM_MODE=ON to require it)"
    )
  endif()
else()
  message(STATUS "fsim LLVM backend disabled by FSIM_LLVM_MODE=OFF")
endif()

# Portable owning-unit serialization uses Boost.PFR only as a compile-time
# aggregate field enumerator. It contributes no runtime ABI and field order is
# fixed by the fsim portable schema, so any installed PFR is sufficient.
find_path(FSIM_BOOST_PFR_INCLUDE_DIR boost/pfr/core.hpp)
if(NOT FSIM_BOOST_PFR_INCLUDE_DIR)
  include(FetchContent)
  string(
    REPLACE "." "_"
    FSIM_BOOST_ARCHIVE_VERSION
    "${FSIM_BOOST_VERSION}"
  )
  FetchContent_Declare(
    fsim_boost
    URL
      "https://archives.boost.io/release/${FSIM_BOOST_VERSION}/source/boost_${FSIM_BOOST_ARCHIVE_VERSION}.tar.bz2"
    URL_HASH
      "SHA256=${FSIM_BOOST_SOURCE_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  message(
    STATUS
    "Boost.PFR was not found; fetching pinned Boost ${FSIM_BOOST_VERSION}"
  )
  FetchContent_MakeAvailable(fsim_boost)
  set(FSIM_BOOST_PFR_INCLUDE_DIR "${fsim_boost_SOURCE_DIR}")
endif()
add_library(fsim_boost_pfr_headers INTERFACE)
target_include_directories(
  fsim_boost_pfr_headers
  SYSTEM INTERFACE "${FSIM_BOOST_PFR_INCLUDE_DIR}"
)

string(TOUPPER "${FSIM_TCL_MODE}" FSIM_TCL_MODE_NORMALIZED)
if(NOT FSIM_TCL_MODE_NORMALIZED MATCHES "^(AUTO|ON|OFF)$")
  message(
    FATAL_ERROR
    "FSIM_TCL_MODE must be AUTO, ON, or OFF "
    "(received '${FSIM_TCL_MODE}')"
  )
endif()

set(FSIM_WITH_TCL OFF)
set(FSIM_WITH_BUNDLED_TCL OFF)
if(NOT FSIM_TCL_MODE_NORMALIZED STREQUAL "OFF")
  find_package(TCL QUIET)
  set(FSIM_SYSTEM_TCL_SUPPORTED OFF)
  set(FSIM_SYSTEM_TCL_VERSION "")
  if(TCL_FOUND)
    list(GET TCL_INCLUDE_PATH 0 FSIM_SYSTEM_TCL_INCLUDE_PATH)
    fsim_tcl_version_is_supported(
      "${FSIM_SYSTEM_TCL_INCLUDE_PATH}"
      FSIM_SYSTEM_TCL_SUPPORTED
      FSIM_SYSTEM_TCL_VERSION
    )
    if(NOT FSIM_SYSTEM_TCL_SUPPORTED)
      message(
        STATUS
        "Ignoring incompatible system Tcl "
        "${FSIM_SYSTEM_TCL_VERSION}; fsim requires Tcl "
        "${FSIM_TCL_VERSION} or newer in the 9.0 release series"
      )
    endif()
  endif()
  if(TCL_FOUND AND FSIM_SYSTEM_TCL_SUPPORTED)
    add_library(fsim_tcl_dependency UNKNOWN IMPORTED GLOBAL)
    set_target_properties(
      fsim_tcl_dependency
      PROPERTIES
        IMPORTED_LOCATION "${TCL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TCL_INCLUDE_PATH}"
    )
    if(NOT WIN32 AND TCL_LIBRARY MATCHES "\\.a$")
      find_package(Threads REQUIRED)
      find_package(ZLIB REQUIRED)
      target_link_libraries(
        fsim_tcl_dependency
        INTERFACE
          Threads::Threads
          ZLIB::ZLIB
          "${CMAKE_DL_LIBS}"
          m
      )
    endif()
    message(
      STATUS
      "fsim Tcl interface: ${TCL_LIBRARY} "
      "(Tcl ${FSIM_SYSTEM_TCL_VERSION})"
    )
  else()
    message(
      STATUS
      "A compatible Tcl development package was not found; fetching Tcl "
      "${FSIM_TCL_VERSION}"
    )
    fsim_add_fetched_tcl()
    set(FSIM_WITH_BUNDLED_TCL ON)
  endif()
  set(FSIM_WITH_TCL ON)
else()
  message(STATUS "fsim Tcl interface disabled by FSIM_TCL_MODE=OFF")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

if(
  MSVC
  OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
)
  # Rich generated and template-heavy translation units throughout the
  # project can exceed COFF's default section limit in unoptimized builds.
  # Extended section numbering changes only the object container.
  add_compile_options(/bigobj)
endif()

function(fsim_configure_library target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_include_directories(
    ${target}
    PUBLIC
      "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )
  fsim_enable_warnings(${target})
  fsim_configure_debug_footprint(${target})
endfunction()

function(fsim_configure_executable target)
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include")
  fsim_enable_warnings(${target})
  fsim_configure_debug_footprint(${target})
endfunction()

function(fsim_configure_command_entrypoint target)
  if(
    WIN32
    AND NOT MSVC
    AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
  )
    # GNU-compatible Windows CRTs select the wide-character startup object
    # explicitly; MSVC discovers wmain without an additional linker option.
    target_link_options(${target} PRIVATE -municode)
  endif()
endfunction()

function(fsim_configure_test_platform target)
  if(WIN32)
    if(
      MSVC
      OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
    )
      target_link_options(${target} PRIVATE /STACK:134217728)
    else()
      target_link_options(${target} PRIVATE "LINKER:--stack,134217728")
    endif()
    # Accumulated feature-matrix and API fixtures can exceed Windows' 1 MiB
    # default stack in both optimized and unoptimized Windows builds.
    # Reserve address space uniformly so newly split or merged test hosts do
    # not silently lose the protection carried by an older executable.
  endif()
endfunction()

set(FSIM_TEST_ASSERTIONS_HEADER
    "${PROJECT_BINARY_DIR}/generated/fsim_test_assertions.h")
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
file(GENERATE
  OUTPUT "${FSIM_TEST_ASSERTIONS_HEADER}"
  CONTENT "#pragma once\n#undef NDEBUG\n")

function(fsim_configure_test target)
  fsim_configure_executable(${target})
  # Several small, dependency-free tests intentionally use assert(). Keep
  # those assertions live in multi-config and Release test builds. A forced
  # header avoids the MSVC D9025 command-line warning produced by pairing
  # CMake's Release `/DNDEBUG` with a later `/UNDEBUG`.
  if(
    MSVC
    OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
  )
    target_compile_options(
      ${target} PRIVATE "/FI${FSIM_TEST_ASSERTIONS_HEADER}")
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
  fsim_configure_test_platform(${target})
endfunction()

add_library(fsim_diagnostic STATIC src/diagnostic/diagnostic.cpp)
add_library(fsim::diagnostic ALIAS fsim_diagnostic)
fsim_configure_library(fsim_diagnostic)

add_library(
  fsim_project STATIC
  src/project/project.cpp
)
add_library(fsim::project ALIAS fsim_project)
fsim_configure_library(fsim_project)
target_link_libraries(fsim_project PUBLIC fsim_diagnostic)

add_library(fsim_semantic STATIC
  src/semantic/design_ir.cpp
  src/semantic/model.cpp
  src/semantic/systemverilog_hir.cpp
  src/semantic/vhdl_hir.cpp)
add_library(fsim::semantic ALIAS fsim_semantic)
fsim_configure_library(fsim_semantic)

set(
  FSIM_FRONTEND_SOURCES
  "${PROJECT_SOURCE_DIR}/src/frontend/common.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/class_expression_resolution.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/class_resolution.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/class_inheritance.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/class_specialization.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_execution.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_database_systemverilog.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_limits.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_observation.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_persistence.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_resolution.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_percentage.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_point_identity.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_report.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_sampling.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_source_control.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/coverage_source_identity.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/lexer.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/sdf_lexer.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/sdf21_adapter.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/sdf30_adapter.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/sdf_construct_parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/sdf_ir.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/sdf_normalization.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/sdf_parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_preprocessor.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_core.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_hierarchy.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_classes.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_assertions.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_coverage.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_dpi.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_declarations.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_types.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_switch.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_specify.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_strength.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_expressions.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_functions.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_process_helpers.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_processes.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_udp.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_events.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_fork.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_units.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/systemverilog_scalar_folding.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_udp.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_conditional_analysis.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_concurrent.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_components.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_core.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_configurations.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_declarations.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_expressions.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_functions.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_packages.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_psl.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_psl_analysis.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_statements.cpp"
)
add_library(fsim_frontend STATIC ${FSIM_FRONTEND_SOURCES})
add_library(fsim::frontend ALIAS fsim_frontend)
fsim_configure_library(fsim_frontend)
target_link_libraries(
  fsim_frontend
  PUBLIC fsim_semantic
  PRIVATE fsim_boost_pfr_headers fsim_support
)

add_library(
  fsim_runtime
  STATIC
    src/runtime/class_heap.cpp
    src/runtime/class_randomize.cpp
    src/runtime/class_containers.cpp
    src/runtime/class_methods.cpp
    src/runtime/class_static.cpp
    src/runtime/coverage_aggregation.cpp
    src/runtime/coverage_condition_evaluation.cpp
    src/runtime/coverage_condition_outcomes.cpp
    src/runtime/coverage_expression.cpp
    src/runtime/coverage_fsm.cpp
    src/runtime/coverage_line_state.cpp
    src/runtime/coverage_toggle.cpp
  src/runtime/native_plugin.cpp
  src/runtime/tf_argument.cpp
  src/runtime/tf_call.cpp
  src/runtime/tf_context.cpp
  src/runtime/tf_control.cpp
  src/runtime/tf_containment.cpp
  src/runtime/tf_instance.cpp
  src/runtime/tf_misc.cpp
  src/runtime/tf_plugin.cpp
  src/runtime/tf_registration.cpp
  src/runtime/tf_synchronization.cpp
  src/runtime/tf_time.cpp
  src/runtime/tf_value.cpp
  src/runtime/uvm_object.cpp
  src/runtime/uvm_object_copy.cpp
  src/runtime/uvm_object_policy.cpp
  src/runtime/uvm_packer.cpp
  src/runtime/uvm_synchronization.cpp
  src/runtime/uvm_test_runner.cpp
    src/runtime/uvm_component.cpp
    src/runtime/uvm_phase.cpp
    src/runtime/uvm_activity.cpp
    src/runtime/uvm_callback.cpp
    src/runtime/uvm_register_model.cpp
    src/runtime/uvm_register_model_values.cpp
    src/runtime/uvm_register_model_maps.cpp
  src/runtime/uvm_register_model_frontdoor.cpp
  src/runtime/uvm_register_model_backdoor.cpp
  src/runtime/uvm_register_model_sequences.cpp
  src/runtime/uvm_register_model_coverage.cpp
    src/runtime/uvm_checkpoint.cpp
    src/runtime/uvm_foreign.cpp
    src/runtime/uvm_foreign_integration.cpp
    src/runtime/uvm_objection.cpp
    src/runtime/uvm_tlm1.cpp
    src/runtime/uvm_tlm1_execution.cpp
    src/runtime/uvm_tlm1_analysis.cpp
    src/runtime/uvm_tlm2.cpp
  src/runtime/uvm_tlm2_transport.cpp
  src/runtime/uvm_transaction_policy.cpp
    src/runtime/uvm_sequence.cpp
    src/runtime/uvm_sequence_execution.cpp
    src/runtime/uvm_sequence_arbitration.cpp
    src/runtime/uvm_sequence_access.cpp
    src/runtime/uvm_sequence_macros.cpp
    src/runtime/uvm_sequence_handshake.cpp
    src/runtime/uvm_sequence_roles.cpp
    src/runtime/uvm_sequence_virtual.cpp
  src/runtime/uvm_registry.cpp
  src/runtime/uvm_factory.cpp
  src/runtime/uvm_resource.cpp
  src/runtime/uvm_config_db.cpp
  src/runtime/uvm_command_line.cpp
  src/runtime/uvm_report.cpp
  src/runtime/uvm_report_catcher.cpp
  src/runtime/uvm_report_handler.cpp
  src/runtime/uvm_report_server.cpp
    src/runtime/constraint_expression.cpp
    src/runtime/constraint_solver.cpp
  src/runtime/vhpi_composite.cpp
  src/runtime/vhpi_special.cpp
  src/runtime/vhpi_driver.cpp
  src/runtime/vhpi_write.cpp
  src/runtime/vhpi_time.cpp
  src/runtime/vhpi_callback.cpp
  src/runtime/vhpi_foreign.cpp
  src/runtime/vhpi_association.cpp
  src/runtime/vhpi_io.cpp
  src/runtime/vhpi_checkpoint.cpp
    src/runtime/dpi_composite.cpp
    src/runtime/dpi_callback.cpp
    src/runtime/dpi_marshalling.cpp
    src/runtime/dpi_open_array.cpp
    src/runtime/dpi_plugin.cpp
    src/runtime/dpi_scope.cpp
    src/runtime/dpi_task.cpp
    src/runtime/vhpi_error.cpp
    src/runtime/vhpi_object.cpp
    src/runtime/vhpi_plugin.cpp
    src/runtime/vhpi_type.cpp
    src/runtime/vhpi_value.cpp
    src/runtime/vpi_plugin.cpp
    src/runtime/vpi_error.cpp
    src/runtime/vpi_object.cpp
    src/runtime/vpi_callback.cpp
    src/runtime/vpi_checkpoint.cpp
    src/runtime/vpi_type_descriptor.cpp
    src/runtime/vpi_control.cpp
    src/runtime/vpi_coverage.cpp
    src/runtime/vpi_io.cpp
    src/runtime/vpi_system.cpp
    src/runtime/vpi_value.cpp
    src/runtime/vpi_value_control.cpp
    src/runtime/vpi_time.cpp
    src/runtime/packed_value.cpp
    src/runtime/scheduler.cpp
    src/runtime/vhdl_psl.cpp
    src/runtime/scope_randomize.cpp
    src/runtime/systemverilog_scalar.cpp
    src/runtime/systemverilog_scalar_text.cpp
    src/runtime/systemverilog_chandle.cpp
    src/runtime/systemverilog_string.cpp
    src/runtime/simir.cpp
    src/runtime/simir_coverage.cpp
    src/runtime/simir_arithmetic.cpp
    src/runtime/simir_callable_context.cpp
    src/runtime/simir_container_algorithms.cpp
    src/runtime/simir_container_bridge.cpp
    src/runtime/simir_containers.cpp
    src/runtime/simir_container_values.cpp
    src/runtime/simir_execution.cpp
    src/runtime/simir_sampled_values.cpp
    src/runtime/simir_file_binary.cpp
    src/runtime/simir_file_scanning.cpp
    src/runtime/simir_fork.cpp
    src/runtime/simir_files.cpp
    src/runtime/simir_format.cpp
    src/runtime/simir_interpreter.cpp
    src/runtime/simir_logic.cpp
      src/runtime/simir_random_distribution.cpp
      src/runtime/simir_module_path.cpp
    src/runtime/simir_scheduling.cpp
    src/runtime/simir_synchronization.cpp
      src/runtime/simir_string_methods.cpp
      src/runtime/simir_vital.cpp
      src/runtime/simir_vital_memory_path.cpp
      src/runtime/simir_state.cpp
      src/runtime/fst_change_encoder.cpp
      src/runtime/fst_compression.cpp
      src/runtime/fst_reader.cpp
      src/runtime/fst_value_encoder.cpp
      src/runtime/fst_writer.cpp
    src/runtime/trace_model.cpp
    src/runtime/transaction_record.cpp
    src/runtime/vcd_writer.cpp
)
add_library(fsim::runtime ALIAS fsim_runtime)
fsim_configure_library(fsim_runtime)
target_link_libraries(fsim_runtime PRIVATE fsim_tf)

  add_library(
    fsim_elaboration
    STATIC
      src/elaboration/coverage_conditions.cpp
      src/elaboration/coverage_branches.cpp
      src/elaboration/coverage_exclusion_persistence.cpp
      src/elaboration/coverage_external_exclusions.cpp
      src/elaboration/coverage_fsm_inference.cpp
      src/elaboration/coverage_fsm_hints.cpp
      src/elaboration/coverage_fsm_validation.cpp
      src/elaboration/coverage_memory_toggle.cpp
      src/elaboration/coverage_toggle_selection.cpp
      src/elaboration/coverage_instance_identity.cpp
      src/elaboration/coverage_inventory.cpp
      src/elaboration/coverage_points.cpp
      src/elaboration/elaborated_design.cpp
      src/elaboration/elaborate.cpp
      src/elaboration/verilog_coverage_points.cpp
      src/elaboration/verilog_coverage_conditions.cpp
      src/elaboration/verilog_toggle_inventory.cpp
      src/elaboration/vhdl_coverage_conditions.cpp
      src/elaboration/vhdl_coverage_points.cpp
      src/elaboration/vhdl_toggle_inventory.cpp
      src/elaboration/elaboration_analysis.cpp
      src/elaboration/elaboration_generate_substitution.cpp
      src/elaboration/elaboration_constants.cpp
      src/elaboration/elaboration_defaults.cpp
      src/elaboration/elaboration_vhdl_array_layout.cpp
      src/elaboration/elaboration_literals.cpp
  src/elaboration/elaboration_specialization.cpp
  src/elaboration/elaboration_systemc_specialization.cpp
      src/elaboration/elaboration_vhdl_static.cpp
      src/elaboration/elaboration_vhdl_values.cpp
      src/elaboration/elaboration_vhdl_blocks.cpp
      src/elaboration/elaboration_vhdl_generate_choice.cpp
      src/elaboration/elaboration_vhdl_local.cpp
  src/elaboration/elaboration_sv_constants.cpp
  src/elaboration/elaboration_sv_constant_services.cpp
  src/elaboration/elaboration_sv_lets.cpp
  src/elaboration/elaboration_sv_scalars.cpp
      src/elaboration/elaboration_sv_functions.cpp
      src/elaboration/elaboration_sv_generate.cpp
      src/elaboration/elaboration_sv_strings.cpp
      src/elaboration/elaboration_sv_container_type.cpp
      src/elaboration/elaboration_sv_type_parameters.cpp
      src/elaboration/elaboration_type_generics.cpp
      src/elaboration/elaboration_vhdl_components.cpp
      src/elaboration/elaboration_vhdl_configurations.cpp
      src/elaboration/elaboration_vhdl_packages.cpp
      src/elaboration/elaboration_vhdl_subprograms.cpp
      src/elaboration/elaboration_targets.cpp
      src/elaboration/hierarchy_generic_interfaces.cpp
      src/elaboration/hierarchy_packages.cpp
      src/elaboration/hierarchy_root_globals.cpp
      src/elaboration/hierarchy_builder.cpp
      src/elaboration/hierarchy_vhdl_protected.cpp
      src/elaboration/hierarchy_ports.cpp
      src/elaboration/hierarchy_sv_package_qualified.cpp
      src/elaboration/hierarchy_sv_interface_callables.cpp
      src/elaboration/hierarchy_resolution.cpp
      src/elaboration/hierarchy_specify.cpp
      src/elaboration/hierarchy_specify_expression.cpp
  src/elaboration/hierarchy_sv_container_bridge.cpp
  src/elaboration/hierarchy_sv_aliases.cpp
  src/elaboration/hierarchy_sv_configuration.cpp
      src/elaboration/hierarchy_sv_ports.cpp
      src/elaboration/hierarchy_instantiate.cpp
      src/elaboration/hierarchy_systemc.cpp
      src/elaboration/hierarchy_udp.cpp
      src/elaboration/hierarchy_type_resolution.cpp
      src/elaboration/hierarchy_types.cpp
      src/elaboration/lowerer_assignment.cpp
      src/elaboration/lowerer_assert.cpp
      src/elaboration/lowerer_condition.cpp
      src/elaboration/lowerer_procedural_updates.cpp
      src/elaboration/lowerer_case_qualified.cpp
      src/elaboration/lowerer_vhdl_case.cpp
      src/elaboration/lowerer_vhdl_conversion.cpp
      src/elaboration/lowerer_vhdl_composite_operations.cpp
      src/elaboration/lowerer_vhdl_attributes.cpp
      src/elaboration/lowerer_vhdl_matching.cpp
      src/elaboration/lowerer_vhdl_logic.cpp
  src/elaboration/lowerer_vhdl_vital.cpp
  src/elaboration/lowerer_vhdl_vital_delay.cpp
  src/elaboration/lowerer_vhdl_vital_memory.cpp
  src/elaboration/lowerer_vhdl_vital_state.cpp
  src/elaboration/lowerer_vhdl_vital_timing.cpp
      src/elaboration/lowerer_vhdl_numeric.cpp
      src/elaboration/lowerer_vhdl_fixed.cpp
      src/elaboration/lowerer_vhdl_float.cpp
      src/elaboration/lowerer_vhdl_physical.cpp
      src/elaboration/lowerer_vhdl_signal_attributes.cpp
      src/elaboration/lowerer_vhdl_protected.cpp
      src/elaboration/lowerer_control.cpp
      src/elaboration/lowerer_expression.cpp
      src/elaboration/lowerer_sv_classes.cpp
      src/elaboration/lowerer_expression_binary.cpp
      src/elaboration/lowerer_expression_membership.cpp
      src/elaboration/lowerer_expression_system.cpp
      src/elaboration/lowerer_expression_unary.cpp
      src/elaboration/lowerer_functions.cpp
      src/elaboration/lowerer_callables.cpp
      src/elaboration/lowerer_overloads.cpp
      src/elaboration/lowerer_procedures.cpp
      src/elaboration/lowerer_vhdl_textio.cpp
      src/elaboration/lowerer_concurrent.cpp
      src/elaboration/lowerer_process.cpp
      src/elaboration/lowerer_fork.cpp
      src/elaboration/lowerer_pla.cpp
      src/elaboration/lowerer_sv_synchronization.cpp
      src/elaboration/lowerer_events.cpp
      src/elaboration/lowerer_tasks.cpp
      src/elaboration/lowerer_types.cpp
      src/elaboration/lowerer_sv_string_format.cpp
      src/elaboration/lowerer_sv_strings.cpp
      src/elaboration/lowerer_sv_container_slices.cpp
      src/elaboration/lowerer_sv_container_types.cpp
      src/elaboration/lowerer_sv_containers.cpp
      src/elaboration/lowerer_sv_file_binary.cpp
      src/elaboration/lowerer_sv_file_scan.cpp
      src/elaboration/lowerer_sv_multidimensional.cpp
      src/elaboration/lowerer_sv_unpacked_aggregates.cpp
      src/elaboration/lowerer_vhdl_array_selection.cpp
      src/elaboration/lowerer_vhdl_access.cpp
  )
add_library(fsim::elaboration ALIAS fsim_elaboration)
fsim_configure_library(fsim_elaboration)
target_link_libraries(
  fsim_elaboration
  PUBLIC fsim_frontend fsim_runtime
  PRIVATE fsim_boost_pfr_headers fsim_support
)

add_library(
  fsim_support
  STATIC
    src/support/environment.cpp
    src/support/sha256.cpp
)
add_library(fsim::support ALIAS fsim_support)
fsim_configure_library(fsim_support)

add_library(
  fsim_library STATIC
  src/artifact/coverage_database.cpp
  src/artifact/coverage_database_codec.cpp
  src/artifact/coverage_exclusion_report.cpp
  src/artifact/coverage_report_model.cpp
  src/artifact/coverage_report_projection.cpp
  src/artifact/coverage_report_render.cpp
  src/artifact/coverage_report_render_html.cpp
  src/artifact/coverage_report_render_json.cpp
  src/artifact/coverage_report_render_text.cpp
  src/artifact/coverage_database_merge.cpp
  src/artifact/coverage_database_model.cpp
  src/artifact/coverage_database_partial_merge.cpp
  src/artifact/coverage_identity.cpp
  src/artifact/design.cpp
  src/artifact/object.cpp
  src/library/artifact.cpp
  src/library/portable_unit.cpp
)
add_library(fsim::library ALIAS fsim_library)
fsim_configure_library(fsim_library)
target_link_libraries(fsim_library PUBLIC fsim_diagnostic fsim_support)
target_link_libraries(fsim_library PRIVATE fsim_boost_pfr_headers fsim_frontend)

add_library(fsim_object_cache STATIC src/compiler/object_cache.cpp)
add_library(fsim::object_cache ALIAS fsim_object_cache)
fsim_configure_library(fsim_object_cache)
target_link_libraries(fsim_object_cache PUBLIC fsim_support)

add_library(fsim_platform STATIC src/platform/dynamic_library.cpp)
add_library(fsim::platform ALIAS fsim_platform)
fsim_configure_library(fsim_platform)
target_link_libraries(fsim_platform PRIVATE ${CMAKE_DL_LIBS})
target_link_libraries(
  fsim_runtime
  PUBLIC fsim_diagnostic
  PRIVATE fsim_boost_pfr_headers fsim_platform fsim_support
)

add_library(fsim_systemc_headers INTERFACE)
add_library(fsim::systemc_headers ALIAS fsim_systemc_headers)
target_compile_features(fsim_systemc_headers INTERFACE cxx_std_20)
target_include_directories(
  fsim_systemc_headers
  INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(
  fsim_systemc_headers
  INTERFACE "${FSIM_SYSTEMC_INTERNAL_RUNTIME_TARGET}"
)

add_library(
  fsim_systemc_plugin_exports
  STATIC
    src/systemc/systemc_exports.cpp
)
add_library(
  fsim::systemc_plugin_exports
  ALIAS fsim_systemc_plugin_exports
)
fsim_configure_library(fsim_systemc_plugin_exports)
target_link_libraries(
  fsim_systemc_plugin_exports
  PUBLIC "${FSIM_SYSTEMC_INTERNAL_RUNTIME_TARGET}"
  PRIVATE scv
)
target_link_libraries(
  fsim_systemc_headers
  INTERFACE fsim_systemc_plugin_exports
)

add_library(
  fsim_systemc
  STATIC
    src/systemc/hierarchy.cpp
    src/systemc/hierarchy_callbacks.cpp
    src/systemc/incremental_artifact.cpp
    src/systemc/incremental_compiler.cpp
    src/systemc/kernel_backend_execution.cpp
    src/systemc/kernel_backend_loopback.cpp
    src/systemc/kernel_backend_protocol.cpp
    src/systemc/kernel_backend_session.cpp
    src/systemc/kernel_backend_synchronization.cpp
    src/systemc/kernel_backend_value_codec.cpp
    src/systemc/tlm1_backend.cpp
    src/systemc/tlm2_backend.cpp
    src/systemc/kernel_backend_inventory.cpp
    src/systemc/kernel_backend_binding_inventory.cpp
    src/systemc/kernel_backend_observation.cpp
    src/systemc/plugin_compiler.cpp
    src/systemc/plugin_compiler_common.cpp
    src/systemc/plugin_compiler_dependencies.cpp
    src/systemc/plugin_compiler_process.cpp
    src/systemc/plugin_loader.cpp
    src/systemc/scv_artifact.cpp
    src/systemc/scv_backend_protocol.cpp
    src/systemc/scv_backend_transport.cpp
    src/systemc/scv_constraints.cpp
    src/systemc/scv_extensions.cpp
    src/systemc/scv_random.cpp
    src/systemc/scv_recording.cpp
    src/systemc/scv_resources.cpp
    src/systemc/scv_smart_ptr.cpp
)
add_library(fsim::systemc ALIAS fsim_systemc)
fsim_configure_library(fsim_systemc)
if(WIN32)
  set(FSIM_SYSTEMC_INSTALLED_ACCELERA_LINK_LIBRARY
      "${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_IMPORT_LIBRARY_PREFIX}fsim_systemc_accellera_runtime${CMAKE_IMPORT_LIBRARY_SUFFIX}")
else()
  set(FSIM_SYSTEMC_INSTALLED_ACCELERA_LINK_LIBRARY
      "${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}fsim_systemc_accellera_runtime${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
set(FSIM_SYSTEMC_INSTALLED_OFFICIAL_LINK_LIBRARY
    "${CMAKE_INSTALL_FULL_LIBDIR}/$<TARGET_LINKER_FILE_NAME:${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}>")
separate_arguments(
  FSIM_SYSTEMC_DEFAULT_COMPILE_OPTIONS
  NATIVE_COMMAND "${CMAKE_CXX_FLAGS}"
)
set(FSIM_SYSTEMC_DEFAULT_DEFINES)
set(FSIM_SYSTEMC_FILTERED_COMPILE_OPTIONS)
foreach(FSIM_SYSTEMC_DEFAULT_OPTION IN LISTS FSIM_SYSTEMC_DEFAULT_COMPILE_OPTIONS)
  if(FSIM_SYSTEMC_DEFAULT_OPTION MATCHES "^[-/]D(.+)$")
    list(APPEND FSIM_SYSTEMC_DEFAULT_DEFINES "${CMAKE_MATCH_1}")
  else()
    list(APPEND FSIM_SYSTEMC_FILTERED_COMPILE_OPTIONS
         "${FSIM_SYSTEMC_DEFAULT_OPTION}")
  endif()
endforeach()
if(WIN32)
  # Match the public definitions carried by the official shared SystemC
  # target even when a project names its compiler explicitly.
  list(APPEND FSIM_SYSTEMC_DEFAULT_DEFINES WIN32 SC_WIN_DLL)
endif()
list(REMOVE_DUPLICATES FSIM_SYSTEMC_DEFAULT_DEFINES)
list(
  JOIN FSIM_SYSTEMC_FILTERED_COMPILE_OPTIONS "|"
  FSIM_SYSTEMC_DEFAULT_COMPILE_OPTIONS_ENCODED
)
list(
  JOIN FSIM_SYSTEMC_DEFAULT_DEFINES "|"
  FSIM_SYSTEMC_DEFAULT_DEFINES_ENCODED
)
target_compile_definitions(
  fsim_systemc
  PRIVATE
    FSIM_BUILD_CONFIGURATION="$<CONFIG>"
    FSIM_SYSTEMC_HEADER_PATH="${PROJECT_SOURCE_DIR}/include"
    FSIM_SYSTEMC_INSTALLED_HEADER_PATH="${CMAKE_INSTALL_FULL_INCLUDEDIR}"
    FSIM_SYSTEMC_UPSTREAM_HEADER_PATH="${FSIM_SYSTEMC_OFFICIAL_SOURCE_DIR}/src"
    FSIM_SYSTEMC_INSTALLED_UPSTREAM_HEADER_PATH="${CMAKE_INSTALL_FULL_INCLUDEDIR}"
    FSIM_SCV_HEADER_PATH="${FSIM_SCV_BUILD_SOURCE_DIR}/src"
    FSIM_INSTALLED_SCV_HEADER_PATH="${CMAKE_INSTALL_FULL_INCLUDEDIR}"
    FSIM_SYSTEMC_DEFAULT_COMPILER="${CMAKE_CXX_COMPILER}"
    FSIM_SYSTEMC_DEFAULT_COMPILE_OPTIONS="${FSIM_SYSTEMC_DEFAULT_COMPILE_OPTIONS_ENCODED}"
    FSIM_SYSTEMC_DEFAULT_DEFINES="${FSIM_SYSTEMC_DEFAULT_DEFINES_ENCODED}"
    FSIM_SYSTEMC_PLUGIN_EXPORT_LIBRARY_PATH="$<TARGET_FILE:fsim_systemc_plugin_exports>"
    FSIM_SYSTEMC_INSTALLED_PLUGIN_EXPORT_LIBRARY_PATH="${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}fsim_systemc_plugin_exports${CMAKE_STATIC_LIBRARY_SUFFIX}"
    FSIM_SYSTEMC_ACCELERA_LIBRARY_PATH="$<TARGET_LINKER_FILE:fsim_systemc_accellera_runtime>"
    FSIM_SYSTEMC_INSTALLED_ACCELERA_LIBRARY_PATH="${FSIM_SYSTEMC_INSTALLED_ACCELERA_LINK_LIBRARY}"
    FSIM_SYSTEMC_OFFICIAL_LIBRARY_PATH="$<TARGET_LINKER_FILE:${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}>"
    FSIM_SYSTEMC_INSTALLED_OFFICIAL_LIBRARY_PATH="${FSIM_SYSTEMC_INSTALLED_OFFICIAL_LINK_LIBRARY}"
    FSIM_SCV_LIBRARY_PATH="$<TARGET_LINKER_FILE:scv>"
    FSIM_INSTALLED_SCV_LIBRARY_PATH="${CMAKE_INSTALL_FULL_LIBDIR}/$<TARGET_LINKER_FILE_NAME:scv>"
    FSIM_SYSTEMC_RUNTIME_IDENTITY="${FSIM_SYSTEMC_RUNTIME_IDENTITY}"
)
target_link_libraries(
  fsim_systemc
  PUBLIC
    fsim_platform
    fsim_systemc_headers
    fsim_project
    fsim_diagnostic
    fsim_object_cache
    fsim_runtime
    scv
)
add_library(fsim_api SHARED src/api/api.cpp src/api/api_session.cpp)
add_library(fsim::api ALIAS fsim_api)
fsim_configure_library(fsim_api)
target_link_libraries(fsim_api PRIVATE fsim_project fsim_diagnostic)
target_compile_definitions(
  fsim_api
  PRIVATE FSIM_SHARED FSIM_BUILDING_LIBRARY
  INTERFACE FSIM_SHARED
)
set_target_properties(
  fsim_api
  PROPERTIES
    EXPORT_NAME api
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
if(UNIX AND NOT APPLE)
  target_link_options(
    fsim_api
    PRIVATE
      "LINKER:--version-script=${PROJECT_SOURCE_DIR}/cmake/fsim_api.map"
  )
  set_property(
    TARGET fsim_api
    APPEND
    PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/fsim_api.map"
  )
endif()

add_library(fsim_cli STATIC src/cli/driver.cpp)
add_library(fsim::cli ALIAS fsim_cli)
fsim_configure_library(fsim_cli)
target_link_libraries(fsim_cli PUBLIC fsim_project fsim_diagnostic)

if(FSIM_WITH_LLVM)
  add_library(
    fsim_llvm
    STATIC
      src/compiler/llvm_jit.cpp
      src/compiler/llvm_jit_cache_key.cpp
      src/compiler/llvm_jit_coverage.cpp
      src/compiler/llvm_jit_lowering.cpp
      src/compiler/llvm_jit_dynamic_part.cpp
      src/compiler/llvm_jit_module.cpp
      src/compiler/llvm_jit_operations_control.cpp
      src/compiler/llvm_jit_operations_logic.cpp
      src/compiler/llvm_jit_operations_file.cpp
      src/compiler/llvm_jit_operations_output.cpp
      src/compiler/llvm_jit_operations_signal.cpp
      src/compiler/llvm_jit_operations_string.cpp
      src/compiler/llvm_jit_operations_container.cpp
      src/compiler/llvm_jit_operations_value.cpp
      src/compiler/llvm_jit_validation.cpp
      src/compiler/llvm_jit_validation_selection.cpp
      src/compiler/llvm_jit_validation_support.cpp
      src/compiler/llvm_jit_value_lowering.cpp
  )
  add_library(fsim::llvm ALIAS fsim_llvm)
  fsim_configure_library(fsim_llvm)
  target_link_libraries(fsim_llvm PUBLIC fsim_runtime PRIVATE fsim_object_cache)
  target_include_directories(fsim_llvm SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
  target_compile_definitions(
    fsim_llvm
    PRIVATE FSIM_BUILD_CONFIGURATION="$<CONFIG>"
    PUBLIC FSIM_HAS_LLVM=1
  )
  if(MSVC)
    # LLVM's DenseMap templates trigger C4702 in MSVC after instantiation,
    # even when their headers are marked external.
    target_compile_options(fsim_llvm PRIVATE /wd4702)
  endif()

  # Prefer a distribution's monolithic imported target. Static LLVM builds
  # expose components instead; map only the narrow ORC adapter's requirements.
  if(TARGET LLVM)
    target_link_libraries(fsim_llvm PRIVATE LLVM)
  else()
    llvm_map_components_to_libnames(
      FSIM_LLVM_LIBRARIES
      core
      executionengine
      native
      nativecodegen
      object
      orcjit
      passes
      runtimedyld
      support
      targetparser
    )
    target_link_libraries(fsim_llvm PRIVATE ${FSIM_LLVM_LIBRARIES})
  endif()
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/src/app/application.cpp")
  add_library(
    fsim_application
    STATIC
      src/app/application.cpp
      src/app/application_analysis.cpp
      src/app/application_sdf_annotation_scope.cpp
      src/app/application_sdf_cell_resolution.cpp
      src/app/application_sdf_endpoint_resolution.cpp
      src/app/application_sdf_mapping_validation.cpp
      src/app/application_sdf_value_policy.cpp
      src/app/application_sdf_target_plan.cpp
      src/app/application_sdf_vital_target_plan.cpp
      src/app/application_sdf_vital_path_timing.cpp
      src/app/application_sdf_vital_models.cpp
      src/app/application_sdf_vital_precedence.cpp
      src/runtime/sdf_vital_scheduling.cpp
      src/runtime/sdf_vital_timing_checks.cpp
      src/app/application_sdf_vital_reannotation.cpp
      src/app/application_sdf_path_timing.cpp
      src/app/application_sdf_interconnect_timing.cpp
      src/app/application_sdf_mixed_verilog.cpp
      src/app/application_sdf_mixed_systemverilog.cpp
      src/app/application_sdf_mixed_systemc.cpp
      src/app/application_sdf_mixed_resolution.cpp
      src/app/application_sdf_foreign_interfaces.cpp
      src/app/application_sdf_vital_observability.cpp
      src/app/application_sdf_vital_archive.cpp
      src/app/application_sdf_vital_phases.cpp
      src/app/application_sdf_delay_modes.cpp
      src/app/application_sdf_primary_timing_checks.cpp
      src/app/application_sdf_secondary_timing_checks.cpp
      src/app/application_sdf_condition_timing.cpp
      src/app/application_sdf_pulse_timing.cpp
      src/app/application_sdf_precedence.cpp
      src/app/application_sdf_reannotation.cpp
      src/app/application_sdf_control.cpp
      src/app/application_sdf_effective_archive.cpp
      src/app/application_sdf_observability.cpp
      src/runtime/sdf_scheduling.cpp
      src/runtime/sdf_drive_timing.cpp
      src/app/application_sdf_schema.cpp
      src/app/application_sdf_artifact_identity.cpp
  src/app/application_sdf_portable_archive.cpp
  src/app/application_sdf_phase_persistence.cpp
      src/app/application_build.cpp
      src/app/application_coverage_command.cpp
      src/app/application_coverage_database_psl.cpp
      src/app/application_coverage_control.cpp
      src/app/application_check.cpp
      src/app/application_standard_library.cpp
      src/app/application_vhdl_analysis.cpp
      src/app/application_debug_control.cpp
      src/app/application_debug_coverage.cpp
      src/app/application_debugger.cpp
      src/app/application_uvm_inspection.cpp
      src/app/application_design_ir.cpp
      src/app/application_design_artifact_codec.cpp
      src/app/application_design_artifact_codec_class.cpp
      src/app/application_design_artifact_codec_sv_hir.cpp
      src/app/application_design_artifact_codec_coverage_uvm.cpp
      src/app/application_design_artifact_codec_vhdl_hir.cpp
      src/app/application_design_artifact_codec_semantic.cpp
      src/app/application_design_artifact_codec_design_ir.cpp
      src/app/application_design_artifact_codec_operations.cpp
      src/app/application_design_artifact_codec_operation_value.cpp
      src/app/application_design_artifact_codec_operation_signal.cpp
      src/app/application_design_artifact_codec_operation_string.cpp
      src/app/application_design_artifact_codec_operation_container.cpp
      src/app/application_design_artifact_codec_operation_file.cpp
      src/app/application_design_artifact_codec_operation_scheduling.cpp
      src/app/application_design_artifact_codec_operation_control.cpp
      src/app/application_design_artifact_codec_operation_output.cpp
      src/app/application_design_artifact_codec_operation_class.cpp
      src/app/application_executor_frame.cpp
      src/app/application_executors.cpp
      src/app/application_executor_vital.cpp
      src/app/application_executor_signal_attributes.cpp
      src/app/application_executor_container_operation.cpp
      src/app/application_executor_services.cpp
      src/app/application_executor_signals.cpp
      src/app/application_executor_values.cpp
      src/app/application_library_export.cpp
      src/app/application_library_import.cpp
      src/app/application_phase_compile.cpp
      src/app/application_phase_design.cpp
      src/app/application_phase_object.cpp
      src/app/application_trace_archive.cpp
      src/app/application_trace_api.cpp
      src/app/application_trace_format.cpp
      src/app/application_trace_control.cpp
      src/app/application_trace_hierarchy.cpp
      src/app/application_trace_observation.cpp
      src/app/application_scv_trace.cpp
      src/app/application_systemc_trace.cpp
      src/app/application_run.cpp
      src/app/application_semantic.cpp
      src/app/application_constraint_lowering.cpp
      src/app/application_class_randomization.cpp
      src/app/application_systemverilog_hir.cpp
      src/app/application_systemverilog_executable_hir.cpp
      src/app/application_vhdl_hir.cpp
      src/app/application_vhdl_mode_view.cpp
  src/app/application_vhdl_psl.cpp
  src/app/application_vhdl_debug.cpp
      src/app/application_vpi.cpp
      src/app/application_vhdl_executable_hir.cpp
      src/app/application_services.cpp
      src/app/application_class_inspection.cpp
      src/app/application_simulation.cpp
      src/app/application_uvm_registry.cpp
      src/app/application_uvm_command_line.cpp
      src/app/application_time.cpp
  src/app/application_tf.cpp
  src/app/application_acc_scheduler.cpp
  src/app/application_tf_scheduler.cpp
      src/app/tcl.cpp
  )
  add_library(fsim::application ALIAS fsim_application)
  fsim_configure_library(fsim_application)
  file(
    RELATIVE_PATH
    FSIM_IEEE_LIBRARY_RELATIVE_TO_BINDIR
    "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_DATADIR}/fsim/vhdl/ieee-1076-2019"
  )
  file(
    TO_CMAKE_PATH
    "${FSIM_IEEE_LIBRARY_RELATIVE_TO_BINDIR}"
    FSIM_IEEE_LIBRARY_RELATIVE_TO_BINDIR
  )
  target_compile_definitions(
    fsim_application
    PRIVATE
      "FSIM_IEEE_LIBRARY_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}/third_party/ieee-1076-2019\""
      "FSIM_IEEE_LIBRARY_RELATIVE_TO_BINDIR=\"${FSIM_IEEE_LIBRARY_RELATIVE_TO_BINDIR}\""
  )
  target_link_libraries(
    fsim_application
    PUBLIC
      fsim_cli
      fsim_elaboration
      fsim_library
      fsim_object_cache
      fsim_systemc
    PRIVATE
      fsim_boost_pfr_headers
  )
  if(FSIM_WITH_LLVM)
    target_link_libraries(fsim_application PRIVATE fsim_llvm)
    target_compile_definitions(fsim_application PUBLIC FSIM_HAS_LLVM=1)
  endif()
  if(FSIM_WITH_TCL)
    target_link_libraries(fsim_application PRIVATE fsim_tcl_dependency)
    target_compile_definitions(fsim_application PUBLIC FSIM_HAS_TCL=1)
    if(FSIM_WITH_BUNDLED_TCL)
      file(
        RELATIVE_PATH
        FSIM_TCL_LIBRARY_RELATIVE_TO_BINDIR
        "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_DATADIR}/fsim/tcl${FSIM_TCL_LIBRARY_VERSION}"
      )
      file(
        TO_CMAKE_PATH
        "${FSIM_TCL_LIBRARY_RELATIVE_TO_BINDIR}"
        FSIM_TCL_LIBRARY_RELATIVE_TO_BINDIR
      )
      target_compile_definitions(
        fsim_application
        PRIVATE
          "FSIM_BUNDLED_TCL_LIBRARY_RELATIVE_PATH=\"${FSIM_TCL_LIBRARY_RELATIVE_TO_BINDIR}\""
      )
    endif()
  endif()
  target_link_libraries(fsim_api PRIVATE fsim_application)
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/src/main.cpp")
  add_executable(fsim src/main.cpp)
  fsim_configure_executable(fsim)
  fsim_configure_command_entrypoint(fsim)
  if(TARGET fsim_application)
    target_link_libraries(fsim PRIVATE fsim_application)
  else()
    target_link_libraries(fsim PRIVATE fsim_cli)
  endif()

  set(FSIM_COMMAND_TARGETS fsim)
  foreach(alias_name IN ITEMS fsim-vhdl fsim-sv fsim-elab fsim-run)
    add_executable(${alias_name} src/main.cpp)
    fsim_configure_executable(${alias_name})
    fsim_configure_command_entrypoint(${alias_name})
    if(TARGET fsim_application)
      target_link_libraries(${alias_name} PRIVATE fsim_application)
    else()
      target_link_libraries(${alias_name} PRIVATE fsim_cli)
    endif()
    list(APPEND FSIM_COMMAND_TARGETS ${alias_name})
  endforeach()
endif()

set(FSIM_SYSTEMC_RUNTIME_TARGET_MANIFEST
    "${CMAKE_BINARY_DIR}/systemc-accellera-targets.txt")

if(FSIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(FSIM_BUILD_FUZZERS)
  add_subdirectory(tests/fuzz)
endif()

fsim_systemc_write_runtime_target_manifest(
  "${FSIM_SYSTEMC_RUNTIME_TARGET_MANIFEST}")

install(
  FILES
    include/fsim/api.h
    include/fsim/systemc.hpp
    include/fsim/systemc_abi.h
    include/fsim/version.hpp
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fsim"
)
install(
  FILES
    include/fsim/runtime/native_plugin_abi.h
    include/fsim/runtime/tf_plugin_abi.h
    include/fsim/runtime/veriuser.h
    include/fsim/runtime/acc_user.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fsim/runtime"
)
install(
  DIRECTORY include/fsim/systemc/
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fsim/systemc"
  PATTERN "scv.hpp" EXCLUDE
)
if(FSIM_COMMAND_TARGETS)
  if(WIN32)
    install(
      TARGETS ${FSIM_COMMAND_TARGETS}
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
  else()
    install(
      TARGETS fsim
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    foreach(alias_name IN ITEMS fsim-vhdl fsim-sv fsim-elab fsim-run)
      install(CODE "
        set(FSIM_ALIAS_PATH
          \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${alias_name}\")
        file(REMOVE \"\${FSIM_ALIAS_PATH}\")
        file(CREATE_LINK fsim \"\${FSIM_ALIAS_PATH}\" SYMBOLIC)
        list(APPEND CMAKE_INSTALL_MANIFEST_FILES
          \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${alias_name}\")
      ")
    endforeach()
  endif()
endif()
if(FSIM_WITH_BUNDLED_TCL)
  install(
    DIRECTORY "${FSIM_FETCHED_TCL_LIBRARY_DIR}/"
    DESTINATION
      "${CMAKE_INSTALL_DATADIR}/fsim/tcl${FSIM_TCL_LIBRARY_VERSION}"
  )
  install(
    FILES "${FSIM_FETCHED_TCL_LICENSE_FILE}"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/third-party"
    RENAME "tcl-license.terms"
  )
endif()
install(
  TARGETS fsim_tf
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(
  TARGETS fsim_api
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(
  TARGETS
    fsim_systemc_plugin_exports
    fsim_systemc_accellera_runtime
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/fsimConfig.cmake.in"
  "${PROJECT_BINARY_DIR}/fsimConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fsim"
)
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/fsimConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/fsim.pc.in"
  "${PROJECT_BINARY_DIR}/fsim.pc"
  @ONLY
)
install(
  FILES
    "${PROJECT_BINARY_DIR}/fsimConfig.cmake"
    "${PROJECT_BINARY_DIR}/fsimConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fsim"
)
install(
  FILES "${PROJECT_BINARY_DIR}/fsim.pc"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)
install(
  FILES LICENSE README.md
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(
  DIRECTORY docs/
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
  FILES_MATCHING PATTERN "*.md"
)
install(
  DIRECTORY third_party/ieee-1076-2019/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/fsim/vhdl/ieee-1076-2019"
)
install(
  FILES
    third_party/systemc-3.0.2/SOURCE_MANIFEST.txt
    third_party/systemc-3.0.2/systemc-3.0.2.spdx.json
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/third-party/systemc-3.0.2"
)
install(
  FILES
    third_party/scv-2.0.1/PATCHES.txt
    third_party/scv-2.0.1/SOURCE_MANIFEST.txt
    third_party/scv-2.0.1/scv-2.0.1.spdx.json
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/third-party/scv-2.0.1"
)
install(
  DIRECTORY examples/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/fsim/examples"
  PATTERN ".fsim-cache" EXCLUDE
  PATTERN "*.dll" EXCLUDE
  PATTERN "*.dylib" EXCLUDE
  PATTERN "*.fst" EXCLUDE
  PATTERN "*.o" EXCLUDE
  PATTERN "*.obj" EXCLUDE
  PATTERN "*.so" EXCLUDE
  PATTERN "*.vcd" EXCLUDE
)

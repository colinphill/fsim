# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

project(
  fsim
  VERSION 0.1.0
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
include(CTest)
include(cmake/FsimWarnings.cmake)
include(cmake/FsimDebugFootprint.cmake)
include(cmake/FsimDependencies.cmake)
include(cmake/FsimBoostContext.cmake)
include(cmake/FsimTcl.cmake)

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
  FSIM_SYSTEMC_FIBER_MODE
  "AUTO"
  CACHE STRING
  "SystemC fiber selection: AUTO discovers Boost.Context 1.91.0, ON requires it, OFF disables it"
)
set_property(CACHE FSIM_SYSTEMC_FIBER_MODE PROPERTY STRINGS AUTO ON OFF)
set(
  FSIM_TCL_MODE
  "AUTO"
  CACHE STRING
  "Tcl interface selection: AUTO/ON discovers or downloads Tcl, OFF disables it"
)
set_property(CACHE FSIM_TCL_MODE PROPERTY STRINGS AUTO ON OFF)

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

string(
  TOUPPER
  "${FSIM_SYSTEMC_FIBER_MODE}"
  FSIM_SYSTEMC_FIBER_MODE_NORMALIZED
)
if(NOT FSIM_SYSTEMC_FIBER_MODE_NORMALIZED MATCHES "^(AUTO|ON|OFF)$")
  message(
    FATAL_ERROR
    "FSIM_SYSTEMC_FIBER_MODE must be AUTO, ON, or OFF "
    "(received '${FSIM_SYSTEMC_FIBER_MODE}')"
  )
endif()

# Portable owning-unit serialization uses Boost.PFR only as a compile-time
# aggregate field enumerator. It contributes no runtime ABI and field order is
# fixed by the fsim portable schema, so any installed PFR is sufficient. Fall
# back to the same checksum-pinned Boost source used by SystemC fibers.
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

set(FSIM_WITH_SYSTEMC_FIBERS OFF)
if(NOT FSIM_SYSTEMC_FIBER_MODE_NORMALIZED STREQUAL "OFF")
  find_package(
    Boost ${FSIM_BOOST_VERSION}
    EXACT
    CONFIG
    QUIET
    COMPONENTS context
  )
  if(NOT Boost_FOUND OR NOT TARGET Boost::context)
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
      "Boost.Context ${FSIM_BOOST_VERSION} was not found; "
      "fetching the pinned Boost source archive"
    )
    FetchContent_MakeAvailable(fsim_boost)
    if(WIN32)
      enable_language(ASM_MASM)
    else()
      enable_language(ASM)
    endif()
    fsim_add_fetched_boost_context("${fsim_boost_SOURCE_DIR}")
  endif()
  if(TARGET Boost::context)
    set(FSIM_WITH_SYSTEMC_FIBERS ON)
    message(
      STATUS
      "fsim SystemC fibers: Boost.Context ${FSIM_BOOST_VERSION}"
    )
  else()
    message(
      FATAL_ERROR
      "Boost.Context ${FSIM_BOOST_VERSION} did not provide Boost::context"
    )
  endif()
else()
  message(STATUS "fsim SystemC fibers disabled by FSIM_SYSTEMC_FIBER_MODE=OFF")
endif()

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

function(fsim_configure_test_platform target)
  if(
    WIN32
    AND (
      MSVC
      OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
    )
  )
    # Accumulated feature-matrix and API fixtures can exceed Windows' 1 MiB
    # default stack in both optimized and unoptimized MSVC-compatible builds.
    # Reserve address space uniformly so newly split or merged test hosts do
    # not silently lose the protection carried by an older executable.
    target_link_options(${target} PRIVATE /STACK:8388608)
  endif()
endfunction()

function(fsim_configure_test target)
  fsim_configure_executable(${target})
  # Several small, dependency-free tests intentionally use assert(). Keep
  # those assertions live in multi-config and Release test builds.
  if(
    MSVC
    OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
  )
    target_compile_options(${target} PRIVATE /UNDEBUG)
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
  src/project/project_migration.cpp
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
  "${PROJECT_SOURCE_DIR}/src/frontend/lexer.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_preprocessor.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_core.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_declarations.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_expressions.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_functions.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_processes.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_events.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_fork.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser_units.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_concurrent.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_components.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_core.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_configurations.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_declarations.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_expressions.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_functions.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_packages.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser_statements.cpp"
)
add_library(fsim_frontend STATIC ${FSIM_FRONTEND_SOURCES})
add_library(fsim::frontend ALIAS fsim_frontend)
fsim_configure_library(fsim_frontend)
target_link_libraries(fsim_frontend PUBLIC fsim_semantic)

add_library(
  fsim_runtime
  STATIC
    src/runtime/packed_value.cpp
    src/runtime/scheduler.cpp
    src/runtime/simir.cpp
    src/runtime/simir_arithmetic.cpp
    src/runtime/simir_containers.cpp
    src/runtime/simir_execution.cpp
    src/runtime/simir_file_binary.cpp
    src/runtime/simir_file_scanning.cpp
    src/runtime/simir_fork.cpp
    src/runtime/simir_files.cpp
    src/runtime/simir_format.cpp
    src/runtime/simir_interpreter.cpp
    src/runtime/simir_logic.cpp
      src/runtime/simir_scheduling.cpp
      src/runtime/simir_string_methods.cpp
      src/runtime/simir_vital.cpp
      src/runtime/simir_state.cpp
    src/runtime/vcd_writer.cpp
)
add_library(fsim::runtime ALIAS fsim_runtime)
fsim_configure_library(fsim_runtime)

  add_library(
    fsim_elaboration
    STATIC
      src/elaboration/elaborated_design.cpp
      src/elaboration/elaborate.cpp
      src/elaboration/elaboration_analysis.cpp
      src/elaboration/elaboration_generate_substitution.cpp
      src/elaboration/elaboration_constants.cpp
      src/elaboration/elaboration_defaults.cpp
      src/elaboration/elaboration_vhdl_array_layout.cpp
      src/elaboration/elaboration_literals.cpp
      src/elaboration/elaboration_specialization.cpp
      src/elaboration/elaboration_vhdl_static.cpp
      src/elaboration/elaboration_vhdl_values.cpp
      src/elaboration/elaboration_vhdl_blocks.cpp
      src/elaboration/elaboration_vhdl_generate_choice.cpp
      src/elaboration/elaboration_vhdl_local.cpp
      src/elaboration/elaboration_sv_constants.cpp
      src/elaboration/elaboration_sv_functions.cpp
      src/elaboration/elaboration_sv_generate.cpp
      src/elaboration/elaboration_sv_strings.cpp
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
      src/elaboration/hierarchy_sv_ports.cpp
      src/elaboration/hierarchy_systemc.cpp
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
      src/elaboration/lowerer_events.cpp
      src/elaboration/lowerer_tasks.cpp
      src/elaboration/lowerer_types.cpp
      src/elaboration/lowerer_sv_string_format.cpp
      src/elaboration/lowerer_sv_strings.cpp
      src/elaboration/lowerer_sv_container_slices.cpp
      src/elaboration/lowerer_sv_containers.cpp
      src/elaboration/lowerer_sv_file_binary.cpp
      src/elaboration/lowerer_sv_file_scan.cpp
      src/elaboration/lowerer_sv_multidimensional.cpp
      src/elaboration/lowerer_vhdl_array_selection.cpp
      src/elaboration/lowerer_vhdl_access.cpp
  )
add_library(fsim::elaboration ALIAS fsim_elaboration)
fsim_configure_library(fsim_elaboration)
target_link_libraries(fsim_elaboration PUBLIC fsim_frontend fsim_runtime)

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

add_library(fsim_systemc_headers INTERFACE)
add_library(fsim::systemc_headers ALIAS fsim_systemc_headers)
target_compile_features(fsim_systemc_headers INTERFACE cxx_std_20)
target_include_directories(
  fsim_systemc_headers
  INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

add_library(
  fsim_systemc_support
  STATIC
    src/systemc/systemc_api.cpp
    src/systemc/systemc_core.cpp
    src/systemc/systemc_exports.cpp
    src/systemc/systemc_foreign.cpp
)
add_library(fsim::systemc_support ALIAS fsim_systemc_support)
fsim_configure_library(fsim_systemc_support)
target_include_directories(
  fsim_systemc_support
  PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  fsim_systemc_headers
  INTERFACE fsim_systemc_support
)

add_library(
  fsim_systemc
  STATIC
    src/systemc/hierarchy.cpp
    src/systemc/hierarchy_callbacks.cpp
    src/systemc/incremental_artifact.cpp
    src/systemc/incremental_compiler.cpp
    src/systemc/plugin_compiler.cpp
    src/systemc/plugin_compiler_common.cpp
    src/systemc/plugin_compiler_dependencies.cpp
    src/systemc/plugin_compiler_process.cpp
    src/systemc/plugin_loader.cpp
)
add_library(fsim::systemc ALIAS fsim_systemc)
fsim_configure_library(fsim_systemc)
set(FSIM_SYSTEMC_FIBER_IDENTITY "disabled")
if(FSIM_WITH_SYSTEMC_FIBERS)
  if(DEFINED FSIM_BOOST_CONTEXT_IMPLEMENTATION)
    set(
      FSIM_SYSTEMC_FIBER_IDENTITY
      "boost-${FSIM_BOOST_VERSION}-${FSIM_BOOST_CONTEXT_IMPLEMENTATION}"
    )
  else()
    set(
      FSIM_SYSTEMC_FIBER_IDENTITY
      "boost-${FSIM_BOOST_VERSION}-installed"
    )
  endif()
endif()
target_compile_definitions(
  fsim_systemc
  PRIVATE
    FSIM_SYSTEMC_HEADER_PATH="${PROJECT_SOURCE_DIR}/include"
    FSIM_SYSTEMC_INSTALLED_HEADER_PATH="${CMAKE_INSTALL_FULL_INCLUDEDIR}"
    FSIM_SYSTEMC_SUPPORT_LIBRARY_PATH="$<TARGET_FILE:fsim_systemc_support>"
    FSIM_SYSTEMC_INSTALLED_SUPPORT_LIBRARY_PATH="${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}fsim_systemc_support${CMAKE_STATIC_LIBRARY_SUFFIX}"
    FSIM_SYSTEMC_FIBER_IDENTITY="${FSIM_SYSTEMC_FIBER_IDENTITY}"
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
)
if(FSIM_WITH_SYSTEMC_FIBERS)
  target_link_libraries(fsim_systemc PRIVATE Boost::context)
  target_compile_definitions(
    fsim_systemc
    PUBLIC FSIM_HAS_BOOST_CONTEXT=1
  )
  target_compile_definitions(
    fsim_elaboration
    PUBLIC FSIM_HAS_BOOST_CONTEXT=1
  )
endif()

add_library(fsim_api SHARED src/api/api.cpp src/api/api_session.cpp)
add_library(fsim::api ALIAS fsim_api)
fsim_configure_library(fsim_api)
target_link_libraries(fsim_api PUBLIC fsim_project fsim_diagnostic)
target_compile_definitions(
  fsim_api
  PRIVATE FSIM_SHARED FSIM_BUILDING_LIBRARY
  INTERFACE FSIM_SHARED
)
set_target_properties(
  fsim_api
  PROPERTIES
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
  target_compile_definitions(fsim_llvm PUBLIC FSIM_HAS_LLVM=1)
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
      src/app/application_build.cpp
      src/app/application_check.cpp
      src/app/application_standard_library.cpp
      src/app/application_vhdl_analysis.cpp
      src/app/application_debug_control.cpp
      src/app/application_debugger.cpp
      src/app/application_design_ir.cpp
      src/app/application_design_artifact_codec.cpp
      src/app/application_executor_frame.cpp
      src/app/application_executors.cpp
      src/app/application_executor_vital.cpp
      src/app/application_executor_signal_attributes.cpp
      src/app/application_executor_services.cpp
      src/app/application_library_export.cpp
      src/app/application_library_import.cpp
      src/app/application_phase_compile.cpp
      src/app/application_phase_design.cpp
      src/app/application_phase_object.cpp
      src/app/application_run.cpp
      src/app/application_semantic.cpp
      src/app/application_systemverilog_hir.cpp
      src/app/application_systemverilog_executable_hir.cpp
      src/app/application_vhdl_hir.cpp
      src/app/application_vhdl_executable_hir.cpp
      src/app/application_services.cpp
      src/app/application_simulation.cpp
      src/app/application_time.cpp
      src/app/tcl.cpp
  )
  add_library(fsim::application ALIAS fsim_application)
  fsim_configure_library(fsim_application)
  if(
    MSVC
    OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
  )
    # Rich archived DesignIR aggregates can exceed COFF's default 65,279
    # section limit in unoptimized builds. Extended section numbering changes
    # only the object container and avoids forcing a less useful Debug mode.
    target_compile_options(fsim_application PRIVATE /bigobj)
  endif()
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
  target_link_libraries(fsim_api PUBLIC fsim_application)
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/src/main.cpp")
  add_executable(fsim src/main.cpp)
  fsim_configure_executable(fsim)
  if(TARGET fsim_application)
    target_link_libraries(fsim PRIVATE fsim_application)
  else()
    target_link_libraries(fsim PRIVATE fsim_cli)
  endif()

  set(FSIM_COMMAND_TARGETS fsim)
  foreach(alias_name IN ITEMS fsim-vhdl fsim-sv fsim-elab fsim-run)
    add_executable(${alias_name} src/main.cpp)
    fsim_configure_executable(${alias_name})
    if(TARGET fsim_application)
      target_link_libraries(${alias_name} PRIVATE fsim_application)
    else()
      target_link_libraries(${alias_name} PRIVATE fsim_cli)
    endif()
    list(APPEND FSIM_COMMAND_TARGETS ${alias_name})
  endforeach()
endif()

if(FSIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(FSIM_BUILD_FUZZERS)
  add_subdirectory(tests/fuzz)
endif()

install(
  FILES
    include/systemc
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
  FILES
    include/fsim/api.h
    include/fsim/systemc.hpp
    include/fsim/systemc_abi.h
    include/fsim/version.hpp
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fsim"
)
install(
  DIRECTORY include/fsim/systemc/
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fsim/systemc"
)
if(FSIM_COMMAND_TARGETS)
  install(
    TARGETS ${FSIM_COMMAND_TARGETS}
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
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
  TARGETS fsim_api fsim_systemc_support
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
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

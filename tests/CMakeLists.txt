# SPDX-License-Identifier: Apache-2.0

add_subdirectory(runtime)

add_executable(
  fsim_semantic_tests
  semantic/semantic_model_test.cpp
)
fsim_configure_test(fsim_semantic_tests)
target_link_libraries(fsim_semantic_tests PRIVATE fsim_semantic)
add_test(NAME fsim.semantic COMMAND fsim_semantic_tests)
set_tests_properties(
  fsim.semantic
  PROPERTIES LABELS "fast;semantic;hir;designir;source"
)

add_executable(
  fsim_systemverilog_hir_application_tests
  app/systemverilog_hir_application_test.cpp
)
fsim_configure_test(fsim_systemverilog_hir_application_tests)
target_link_libraries(
  fsim_systemverilog_hir_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.systemverilog_hir
  COMMAND fsim_systemverilog_hir_application_tests
)
set_tests_properties(
  fsim.application.systemverilog_hir
  PROPERTIES LABELS "fast;semantic;hir;systemverilog"
)

add_executable(
  fsim_frontend_tests
  frontend/frontend_tests.cpp
  frontend/frontend_preprocessing_tests.cpp
  frontend/frontend_sv_conformance_tests.cpp
  frontend/frontend_vhdl_type_tests.cpp
  frontend/frontend_vhdl_statement_tests.cpp
  frontend/frontend_vhdl_attribute_tests.cpp
  frontend/frontend_vhdl_time_file_tests.cpp
  frontend/frontend_declaration_tests.cpp
  frontend/frontend_interface_tests.cpp
  frontend/frontend_package_tests.cpp
  frontend/frontend_callable_tests.cpp
  frontend/frontend_vhdl_subprogram_tests.cpp
  frontend/frontend_vhdl_generic_subprogram_tests.cpp
  frontend/frontend_vhdl_component_tests.cpp
  frontend/frontend_vhdl_configuration_tests.cpp
  frontend/frontend_vhdl_package_generic_tests.cpp
  frontend/frontend_statement_tests.cpp
  frontend/frontend_udp_tests.cpp
  frontend/frontend_generate_output_tests.cpp
  frontend/frontend_file_tests.cpp
  frontend/frontend_container_tests.cpp
)
fsim_configure_test(fsim_frontend_tests)
target_link_libraries(fsim_frontend_tests PRIVATE fsim_frontend)
add_test(NAME fsim.frontend COMMAND fsim_frontend_tests)

add_executable(fsim_project_tests project/project_config_test.cpp)
fsim_configure_test(fsim_project_tests)
target_link_libraries(fsim_project_tests PRIVATE fsim_project)
add_test(NAME fsim.project COMMAND fsim_project_tests)

add_executable(fsim_library_tests library/library_artifact_test.cpp)
fsim_configure_test(fsim_library_tests)
target_link_libraries(fsim_library_tests PRIVATE fsim_library)
add_test(NAME fsim.library.artifact COMMAND fsim_library_tests)
set_tests_properties(
  fsim.library.artifact
  PROPERTIES LABELS "fast;library;artifact;source"
)

add_executable(fsim_object_artifact_tests artifact/object_artifact_test.cpp)
fsim_configure_test(fsim_object_artifact_tests)
target_link_libraries(fsim_object_artifact_tests PRIVATE fsim_library)
add_test(NAME fsim.artifact.object COMMAND fsim_object_artifact_tests)
set_tests_properties(
  fsim.artifact.object
  PROPERTIES LABELS "fast;artifact;compile;source"
)

add_executable(fsim_design_artifact_tests artifact/design_artifact_test.cpp)
fsim_configure_test(fsim_design_artifact_tests)
target_link_libraries(fsim_design_artifact_tests PRIVATE fsim_library)
add_test(NAME fsim.artifact.design COMMAND fsim_design_artifact_tests)
set_tests_properties(
  fsim.artifact.design
  PROPERTIES LABELS "fast;artifact;elaborate"
)

add_executable(fsim_cache_tests compiler/cache_test.cpp)
fsim_configure_test(fsim_cache_tests)
target_link_libraries(fsim_cache_tests PRIVATE fsim_object_cache)
add_test(NAME fsim.cache COMMAND fsim_cache_tests)

add_test(
  NAME fsim.diagnostics-catalog
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckDiagnosticCatalog.cmake"
)

add_test(
  NAME fsim.source-line-budget
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSourceLineBudget.cmake"
)

add_test(
  NAME fsim.ieee-package-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckIeeePackageInventory.cmake"
)
add_test(
  NAME fsim.v1-vhdl-matrix
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1VhdlMatrix.cmake"
)
add_test(
  NAME fsim.v1-mixed-conversion-matrix
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1MixedConversionMatrix.cmake"
)
add_test(
  NAME fsim.v1-legality-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1LegalityAudit.cmake"
)
add_test(
  NAME fsim.v1-conformance-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ConformanceAudit.cmake"
)
add_test(
  NAME fsim.v1-conformance-corpus
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ConformanceCorpus.cmake"
)
add_test(
  NAME fsim.v1-portability-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1PortabilityAudit.cmake"
)
add_test(
  NAME fsim.v1-portability-corpus
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1PortabilityCorpus.cmake"
)
add_test(
  NAME fsim.v1-release-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ReleaseAudit.cmake"
)
add_test(
  NAME fsim.v1-systemverilog-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1SystemVerilogRelease.cmake"
)
add_test(
  NAME fsim.v1-vhdl-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1VhdlRelease.cmake"
)
add_test(
  NAME fsim.v1-mixed-systemc-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1MixedSystemCRelease.cmake"
)
add_test(
  NAME fsim.v1-differential-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1DifferentialRelease.cmake"
)
add_test(
  NAME fsim.v1-public-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1PublicRelease.cmake"
)
add_test(
  NAME fsim.v1-inventory-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1InventoryRelease.cmake"
)
add_test(
  NAME fsim.v1-resource-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ResourceRelease.cmake"
)
add_test(
  NAME fsim.v1-release-candidate
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ReleaseCandidate.cmake"
)
if(WIN32)
  set(FSIM_INSTALLED_API_LIBRARY_DIR "${CMAKE_INSTALL_BINDIR}")
else()
  set(FSIM_INSTALLED_API_LIBRARY_DIR "${CMAKE_INSTALL_LIBDIR}")
endif()
add_test(
  NAME fsim.installed-public-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DFSIM_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-public-contract"
    "-DFSIM_CONFIG=$<CONFIG>"
    "-DFSIM_BINDIR=${CMAKE_INSTALL_BINDIR}"
    "-DFSIM_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
    "-DFSIM_INCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}"
    "-DFSIM_DOCDIR=${CMAKE_INSTALL_DOCDIR}"
    "-DFSIM_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim>"
    "-DFSIM_VHDL_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim-vhdl>"
    "-DFSIM_SV_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim-sv>"
    "-DFSIM_ELAB_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim-elab>"
    "-DFSIM_RUN_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim-run>"
    "-DFSIM_API_LIBRARY_NAME=$<TARGET_FILE_NAME:fsim_api>"
    "-DFSIM_SYSTEMC_LIBRARY_NAME=$<TARGET_FILE_NAME:fsim_systemc_support>"
    "-DFSIM_API_LIBRARY_DIR=${FSIM_INSTALLED_API_LIBRARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckInstalledPublicContract.cmake"
)
add_test(
  NAME fsim.msvc-debug-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckMsvcDebugContract.cmake"
)
add_test(
  NAME fsim.msvc-release-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckMsvcReleaseContract.cmake"
)
add_test(
  NAME fsim.windows-llvm-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckWindowsLlvmContract.cmake"
)
add_test(
  NAME fsim.systemc-portability-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSystemCPortabilityContract.cmake"
)
add_test(
  NAME fsim.tool-portability-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckToolPortabilityContract.cmake"
)
add_test(
  NAME fsim.resource-portability-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckResourcePortabilityContract.cmake"
)
set_tests_properties(
  fsim.v1-vhdl-matrix
  PROPERTIES LABELS "fast;vhdl;matrix;cmake"
)
set_tests_properties(
  fsim.v1-mixed-conversion-matrix
  PROPERTIES LABELS "fast;mixed-language;conversions;matrix;cmake"
)
set_tests_properties(
  fsim.v1-legality-audit
  PROPERTIES LABELS "fast;legality;matrix;cmake"
)
set_tests_properties(
  fsim.v1-conformance-audit
  PROPERTIES LABELS "fast;conformance;license;provenance;cmake"
)
set_tests_properties(
  fsim.v1-conformance-corpus
  PROPERTIES LABELS "fast;conformance;coverage;provenance;cmake"
)
set_tests_properties(
  fsim.v1-portability-audit
  PROPERTIES LABELS "fast;portability;windows;linux;cmake"
)
set_tests_properties(
  fsim.v1-portability-corpus
  PROPERTIES LABELS "fast;portability;windows;linux;matrix;differential;cmake"
)
set_tests_properties(
  fsim.v1-release-audit
  PROPERTIES LABELS "fast;release;legality;conformance;portability;matrix;cmake"
)
set_tests_properties(
  fsim.v1-systemverilog-release
  PROPERTIES LABELS "fast;release;systemverilog;legality;matrix;cmake"
)
set_tests_properties(
  fsim.v1-vhdl-release
  PROPERTIES LABELS "fast;release;vhdl;legality;matrix;cmake"
)
set_tests_properties(
  fsim.v1-mixed-systemc-release
  PROPERTIES LABELS "fast;release;mixed-language;systemc;abi;matrix;cmake"
)
set_tests_properties(
  fsim.v1-differential-release
  PROPERTIES LABELS "fast;release;differential;llvm;cache;debugger;vcd;cmake"
)
set_tests_properties(
  fsim.v1-public-release
  PROPERTIES LABELS "fast;release;api;abi;cli;tcl;files;portability;cmake"
)
set_tests_properties(
  fsim.v1-inventory-release
  PROPERTIES LABELS "fast;release;diagnostics;source;license;conformance;provenance;cmake"
)
set_tests_properties(
  fsim.v1-resource-release
  PROPERTIES LABELS "fast;release;portability;windows;linux;resources;cmake"
)
set_tests_properties(
  fsim.v1-release-candidate
  PROPERTIES LABELS "fast;release;matrix;coverage;evidence;cmake"
)
set_tests_properties(
  fsim.installed-public-contract
  PROPERTIES LABELS "fast;release;install;api;abi;cli;packages;portability;cmake"
)
set_tests_properties(
  fsim.msvc-debug-contract
  PROPERTIES LABELS "fast;portability;windows;msvc;debug;cmake"
)
set_tests_properties(
  fsim.msvc-release-contract
  PROPERTIES LABELS "fast;portability;windows;msvc;release;cmake"
)
set_tests_properties(
  fsim.windows-llvm-contract
  PROPERTIES LABELS "fast;portability;windows;llvm;jit;cache;debugger;cmake"
)
set_tests_properties(
  fsim.systemc-portability-contract
  PROPERTIES LABELS "fast;portability;windows;linux;systemc;abi;compiler;cache;cmake"
)
set_tests_properties(
  fsim.tool-portability-contract
  PROPERTIES LABELS "fast;portability;windows;linux;api;cli;tcl;debugger;vcd;files;cmake"
)
set_tests_properties(
  fsim.resource-portability-contract
  PROPERTIES LABELS "fast;portability;windows;linux;resources;memory;timeouts;cmake"
)
set_tests_properties(
  fsim.ieee-package-inventory
  PROPERTIES LABELS "fast;vhdl;packages;license;cmake"
)

add_test(
  NAME fsim.tcl.version-selection
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/tcl-version-selection"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckTclVersionSelection.cmake"
)
set_tests_properties(
  fsim.tcl.version-selection
  PROPERTIES LABELS "fast;tcl;cmake"
)

add_executable(
  fsim_jit_runtime_c_tests
  compiler/jit_runtime_c_test.c
)
target_compile_features(fsim_jit_runtime_c_tests PRIVATE c_std_11)
set_target_properties(fsim_jit_runtime_c_tests PROPERTIES C_EXTENSIONS OFF)
target_include_directories(
  fsim_jit_runtime_c_tests
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_jit_runtime_c_tests)
fsim_configure_test_platform(fsim_jit_runtime_c_tests)
add_test(NAME fsim.jit-runtime-c COMMAND fsim_jit_runtime_c_tests)

add_executable(
  fsim_elaboration_tests
  elaboration/elaborator_test.cpp
  elaboration/elaborator_specialization_test.cpp
  elaboration/elaborator_sv_constant_test.cpp
  elaboration/elaborator_sv_string_test.cpp
  elaboration/elaborator_sv_type_parameter_test.cpp
  elaboration/elaborator_generate_test.cpp
  elaboration/elaborator_mixed_test.cpp
  elaboration/elaborator_library_search_test.cpp
  elaboration/elaborator_udp_test.cpp
  elaboration/elaborator_multiple_root_test.cpp
  elaboration/elaborator_mixed_conversion_test.cpp
  elaboration/elaborator_mixed_construction_test.cpp
  elaboration/elaborator_mixed_driver_test.cpp
  elaboration/elaborator_process_test.cpp
  elaboration/elaborator_fork_test.cpp
  elaboration/elaborator_function_test.cpp
  elaboration/elaborator_task_test.cpp
  elaboration/elaborator_case_qualifier_test.cpp
  elaboration/elaborator_case_match_test.cpp
  elaboration/elaborator_expression_test.cpp
  elaboration/elaborator_numeric_test.cpp
  elaboration/elaborator_selection_test.cpp
  elaboration/elaborator_vhdl_dynamic_slice_test.cpp
  elaboration/elaborator_vhdl_composite_test.cpp
  elaboration/elaborator_vhdl_conversion_test.cpp
  elaboration/elaborator_vhdl_aggregate_choice_test.cpp
  elaboration/elaborator_vhdl_attribute_test.cpp
  elaboration/elaborator_vhdl_composite_operation_test.cpp
  elaboration/elaborator_types_test.cpp
  elaboration/elaborator_vhdl_textio_test.cpp
  elaboration/elaborator_type_generic_test.cpp
  elaboration/elaborator_vhdl_function_test.cpp
  elaboration/elaborator_vhdl_overload_test.cpp
  elaboration/elaborator_vhdl_procedure_test.cpp
  elaboration/elaborator_vhdl_wait_test.cpp
  elaboration/elaborator_vhdl_package_test.cpp
  elaboration/elaborator_vhdl_generic_subprogram_test.cpp
  elaboration/elaborator_vhdl_generic_association_test.cpp
  elaboration/elaborator_vhdl_component_test.cpp
  elaboration/elaborator_vhdl_configuration_test.cpp
  elaboration/elaborator_vhdl_matching_test.cpp
  elaboration/elaborator_vhdl_case_test.cpp
  elaboration/elaborator_vhdl_concurrent_test.cpp
  elaboration/elaborator_sv_file_test.cpp
  elaboration/elaborator_sv_container_test.cpp
  elaboration/elaborator_sv_aggregate_container_test.cpp
  elaboration/elaborator_sv_slice_call_test.cpp
  elaboration/elaborator_sv_slice_order_test.cpp
  elaboration/elaborator_sv_slice_port_test.cpp
  elaboration/elaborator_sv_interface_test.cpp
  elaboration/elaborator_sv_conformance_test.cpp
)
fsim_configure_test(fsim_elaboration_tests)
target_link_libraries(fsim_elaboration_tests PRIVATE fsim_elaboration)
add_test(NAME fsim.elaboration COMMAND fsim_elaboration_tests)

add_executable(
  fsim_sv_container_elaboration_tests
  elaboration/elaborator_sv_container_main.cpp
  elaboration/elaborator_sv_container_test.cpp
  elaboration/elaborator_sv_aggregate_container_test.cpp
  elaboration/elaborator_sv_slice_call_test.cpp
  elaboration/elaborator_sv_slice_order_test.cpp
  elaboration/elaborator_sv_slice_port_test.cpp
)
fsim_configure_test(fsim_sv_container_elaboration_tests)
target_link_libraries(
  fsim_sv_container_elaboration_tests PRIVATE fsim_elaboration)
add_test(
  NAME fsim.elaboration.sv-container
  COMMAND fsim_sv_container_elaboration_tests)

add_executable(fsim_systemc_header_tests systemc/systemc_header_test.cpp)
fsim_configure_test(fsim_systemc_header_tests)
target_link_libraries(fsim_systemc_header_tests PRIVATE fsim_systemc_headers)
add_test(NAME fsim.systemc.header COMMAND fsim_systemc_header_tests)

add_executable(
  fsim_systemc_abi_c_tests
  systemc/systemc_abi_c_test.c
)
target_compile_features(fsim_systemc_abi_c_tests PRIVATE c_std_11)
set_target_properties(
  fsim_systemc_abi_c_tests
  PROPERTIES C_EXTENSIONS OFF
)
target_include_directories(
  fsim_systemc_abi_c_tests
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_systemc_abi_c_tests)
fsim_configure_test_platform(fsim_systemc_abi_c_tests)
add_test(NAME fsim.systemc.abi-c COMMAND fsim_systemc_abi_c_tests)

add_library(fsim_test_systemc_plugin MODULE systemc/sample_plugin.cpp)
target_compile_features(fsim_test_systemc_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_test_systemc_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_test_systemc_plugin)
set_target_properties(
  fsim_test_systemc_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

function(fsim_configure_macro_systemc_plugin target)
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_link_libraries(${target} PRIVATE fsim_systemc_support)
  fsim_enable_warnings(${target})
  set_target_properties(
    ${target}
    PROPERTIES
      PREFIX ""
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES
  )
endfunction()

add_library(
  fsim_test_macro_systemc_plugin
  MODULE
    systemc/macro_plugin.cpp
    systemc/macro_plugin_extra.cpp
)
fsim_configure_macro_systemc_plugin(
  fsim_test_macro_systemc_plugin)

add_library(
  fsim_test_duplicate_macro_systemc_plugin
  MODULE
    systemc/macro_plugin.cpp
    systemc/macro_plugin_extra.cpp
)
target_compile_definitions(
  fsim_test_duplicate_macro_systemc_plugin
  PRIVATE FSIM_TEST_DUPLICATE_EXPORT=1
)
fsim_configure_macro_systemc_plugin(
  fsim_test_duplicate_macro_systemc_plugin)

add_library(
  fsim_test_throwing_systemc_plugin
  MODULE
  systemc/throwing_plugin.cpp
)

add_library(
  fsim_test_error_systemc_plugin
  MODULE
  systemc/error_plugin.cpp
)
target_compile_features(fsim_test_error_systemc_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_test_error_systemc_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_test_error_systemc_plugin)
set_target_properties(
  fsim_test_error_systemc_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_library(
  fsim_test_empty_systemc_plugin
  MODULE
  systemc/empty_plugin.cpp
)

add_library(
  fsim_test_no_factory_systemc_plugin
  MODULE
  systemc/no_factory_plugin.cpp
)
target_compile_features(
  fsim_test_no_factory_systemc_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_test_no_factory_systemc_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_test_no_factory_systemc_plugin)
set_target_properties(
  fsim_test_no_factory_systemc_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
target_compile_features(fsim_test_empty_systemc_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_test_empty_systemc_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_test_empty_systemc_plugin)
set_target_properties(
  fsim_test_empty_systemc_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
target_compile_features(fsim_test_throwing_systemc_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_test_throwing_systemc_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_test_throwing_systemc_plugin)
set_target_properties(
  fsim_test_throwing_systemc_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_executable(fsim_systemc_plugin_tests systemc/plugin_loader_test.cpp)
fsim_configure_test(fsim_systemc_plugin_tests)
target_link_libraries(fsim_systemc_plugin_tests PRIVATE fsim_systemc)
add_test(
  NAME fsim.systemc.plugin
  COMMAND
    fsim_systemc_plugin_tests
    "$<TARGET_FILE:fsim_test_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_throwing_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_error_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_empty_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_macro_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_duplicate_macro_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_no_factory_systemc_plugin>"
)

add_executable(
  fsim_systemc_compiler_tests
  systemc/plugin_compiler_test.cpp
)
fsim_configure_test(fsim_systemc_compiler_tests)
target_link_libraries(fsim_systemc_compiler_tests PRIVATE fsim_systemc)
add_test(NAME fsim.systemc.compiler COMMAND fsim_systemc_compiler_tests)

add_executable(
  fsim_systemc_incremental_tests
  systemc/incremental_compiler_test.cpp
)
fsim_configure_test(fsim_systemc_incremental_tests)
target_link_libraries(fsim_systemc_incremental_tests PRIVATE fsim_systemc)
add_test(
  NAME fsim.systemc.incremental
  COMMAND fsim_systemc_incremental_tests
)

add_executable(
  fsim_systemc_matrix_tests
  systemc/plugin_matrix_test.cpp
)
fsim_configure_test(fsim_systemc_matrix_tests)
set(FSIM_TEST_CXX_OPTIONS "")
if(CMAKE_CXX_COMPILER_ARG1)
  separate_arguments(
    FSIM_TEST_CXX_COMPILER_ARG1_OPTIONS
    NATIVE_COMMAND "${CMAKE_CXX_COMPILER_ARG1}"
  )
  list(APPEND
    FSIM_TEST_CXX_OPTIONS
    ${FSIM_TEST_CXX_COMPILER_ARG1_OPTIONS}
  )
endif()
if(CMAKE_CXX_FLAGS)
  separate_arguments(
    FSIM_TEST_CXX_GLOBAL_OPTIONS
    NATIVE_COMMAND "${CMAKE_CXX_FLAGS}"
  )
  list(APPEND FSIM_TEST_CXX_OPTIONS ${FSIM_TEST_CXX_GLOBAL_OPTIONS})
endif()
set(FSIM_TEST_CXX_COMPILER_PATH "${CMAKE_CXX_COMPILER}")
if(NOT WIN32 AND FSIM_TEST_CXX_OPTIONS)
  set(FSIM_TEST_CXX_WRAPPER_COMMAND "${CMAKE_CXX_COMPILER}")
  set(FSIM_TEST_CXX_WRAPPER_ARGUMENTS "")
  foreach(FSIM_TEST_CXX_OPTION IN LISTS FSIM_TEST_CXX_OPTIONS)
    string(REPLACE "\\" "\\\\" FSIM_TEST_CXX_OPTION_ESCAPED "${FSIM_TEST_CXX_OPTION}")
    string(REPLACE "\"" "\\\"" FSIM_TEST_CXX_OPTION_ESCAPED "${FSIM_TEST_CXX_OPTION_ESCAPED}")
    string(REPLACE "$" "\\$" FSIM_TEST_CXX_OPTION_ESCAPED "${FSIM_TEST_CXX_OPTION_ESCAPED}")
    string(REPLACE "`" "\\`" FSIM_TEST_CXX_OPTION_ESCAPED "${FSIM_TEST_CXX_OPTION_ESCAPED}")
    string(APPEND
      FSIM_TEST_CXX_WRAPPER_ARGUMENTS
      " \"${FSIM_TEST_CXX_OPTION_ESCAPED}\""
    )
  endforeach()
  set(
    FSIM_TEST_CXX_COMPILER_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/fsim-test-cxx-compiler"
  )
  configure_file(
    "${PROJECT_SOURCE_DIR}/tests/systemc/test_cxx_compiler.in"
    "${FSIM_TEST_CXX_COMPILER_PATH}"
    @ONLY
  )
  file(
    CHMOD "${FSIM_TEST_CXX_COMPILER_PATH}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE
  )
endif()
target_compile_definitions(
  fsim_systemc_matrix_tests
  PRIVATE
    FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    FSIM_TEST_CXX_COMPILER="${FSIM_TEST_CXX_COMPILER_PATH}"
)
target_link_libraries(fsim_systemc_matrix_tests PRIVATE fsim_systemc)
add_test(NAME fsim.systemc.matrix COMMAND fsim_systemc_matrix_tests)
set_tests_properties(
  fsim.systemc.matrix
  PROPERTIES LABELS "systemc;compiler;cache;lifecycle;threads;portability"
)

if(FSIM_WITH_LLVM)
  add_executable(fsim_llvm_tests compiler/llvm_jit_test.cpp)
  target_sources(
    fsim_llvm_tests
    PRIVATE
      compiler/llvm_jit_scheduling_test.cpp
      compiler/llvm_jit_value_test.cpp
      compiler/llvm_jit_cache_test.cpp
      compiler/llvm_jit_control_cache_test.cpp
      compiler/llvm_jit_slice_cache_test.cpp
      compiler/llvm_jit_logic9_test.cpp
      compiler/llvm_jit_display_test.cpp
      compiler/llvm_jit_string_test.cpp
  )
  fsim_configure_test(fsim_llvm_tests)
  target_link_libraries(
    fsim_llvm_tests
    PRIVATE
      fsim_llvm
      fsim_object_cache
  )
  add_test(NAME fsim.llvm COMMAND fsim_llvm_tests)
endif()

if(TARGET fsim_application)
  option(
    FSIM_MERGE_APPLICATION_TESTS
    "Merge application cases into a small number of selectable test hosts"
    ON
  )

  if(FSIM_MERGE_APPLICATION_TESTS)
    set(
      fsim_application_test_hosts
      fsim_application_tests
      fsim_application_tests_1
      fsim_application_tests_2
      fsim_application_tests_3
    )
    foreach(application_shard RANGE 0 3)
      list(GET fsim_application_test_hosts ${application_shard} test_host)
      add_executable(
        ${test_host}
        app/application_case_dispatch.cpp
      )
      fsim_configure_test(${test_host})
      target_compile_definitions(
        ${test_host}
        PRIVATE
          FSIM_APPLICATION_SHARD=${application_shard}
          FSIM_APPLICATION_WITH_TCL=$<BOOL:${FSIM_WITH_TCL}>
          FSIM_MERGED_APPLICATION_TESTS=1
          FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
      )
      target_link_libraries(
        ${test_host}
        PRIVATE
          fsim_application
          fsim_cli
      )
    endforeach()

    function(
      fsim_add_application_case
      application_shard
      case_name
      case_source
      test_name
    )
      list(GET fsim_application_test_hosts ${application_shard} test_host)
      set_property(
        SOURCE "${case_source}"
        APPEND PROPERTY
          COMPILE_DEFINITIONS
            "main=fsim_application_case_${case_name}"
      )
      target_sources(${test_host} PRIVATE "${case_source}")
      add_test(
        NAME "${test_name}"
        COMMAND ${test_host} "${case_name}"
      )
      if(ARGN)
        set_tests_properties(
          "${test_name}"
          PROPERTIES LABELS "${ARGN}"
        )
      endif()
    endfunction()

    fsim_add_application_case(
      0 core app/application_test.cpp fsim.application)
    fsim_add_application_case(
      0 systemc_matrix app/application_systemc_matrix_test.cpp
      fsim.application.systemc_matrix
      systemc compiler cache lifecycle threads llvm debug portability)
    target_sources(
      fsim_application_tests
      PRIVATE
        app/application_test_cli.cpp
        app/application_test_artifact_phases.cpp
        app/application_test_fixture.cpp
        app/application_test_mixed.cpp
        app/application_test_multiple_roots.cpp
        app/application_test_non_project_cli.cpp
        app/application_test_simulation.cpp
        app/application_test_source_updates.cpp
        app/application_test_sources_common.cpp
        app/application_test_sources_mixed.cpp
        app/application_test_sources_systemc.cpp
        app/application_test_specialization.cpp
        app/application_test_support.cpp
        app/application_test_systemc.cpp
        app/application_test_systemc_scheduling.cpp
    )
    fsim_add_application_case(
      0 expressions app/expression_application_test.cpp
      fsim.application.expressions fast expressions)
    fsim_add_application_case(
      0 scoped_locals app/scoped_local_application_test.cpp
      fsim.application.scoped_locals fast scoped-locals)
    fsim_add_application_case(
      0 call_safe_points app/call_safe_point_application_test.cpp
      fsim.application.call_safe_points fast call-safe-points)
    fsim_add_application_case(
      0 named_events app/named_event_application_test.cpp
      fsim.application.named_events fast named-events)
    fsim_add_application_case(
      0 fork app/fork_application_test.cpp
      fsim.application.fork fast fork systemverilog cache debugger)
    fsim_add_application_case(
      0 display app/display_application_test.cpp
      fsim.application.display fast display)
    fsim_add_application_case(
      0 random app/random_application_test.cpp
      fsim.application.random fast random)
    fsim_add_application_case(
      0 assertions app/assertion_application_test.cpp
      fsim.application.assertions fast assertions)
    fsim_add_application_case(
      0 line_directives app/line_directive_application_test.cpp
      fsim.application.line_directives fast line-directives)
    fsim_add_application_case(
      0 time app/time_application_test.cpp
      fsim.application.time fast time)
    fsim_add_application_case(
      0 delay_modes app/delay_mode_application_test.cpp
      fsim.application.delay_modes fast delay-modes)
    fsim_add_application_case(
      0 transition_delays app/transition_delay_application_test.cpp
      fsim.application.transition_delays fast transition-delays)
    fsim_add_application_case(
      0 procedural_assignments app/procedural_assignment_application_test.cpp
      fsim.application.procedural_assignments fast procedural-assignments)
    fsim_add_application_case(
      0 resolution app/resolution_application_test.cpp
      fsim.application.resolution fast resolution mixed-language)

    fsim_add_application_case(
      1 sv_parameter_sizing app/sv_parameter_sizing_application_test.cpp
      fsim.application.sv_parameter_sizing fast parameters systemverilog)
    fsim_add_application_case(
      1 sv_type_parameters app/sv_type_parameter_application_test.cpp
      fsim.application.sv_type_parameters fast parameters systemverilog)
    fsim_add_application_case(
      1 sv_string_parameters app/sv_string_parameter_application_test.cpp
      fsim.application.sv_string_parameters fast parameters strings systemverilog)
    fsim_add_application_case(
      1 sv_interfaces app/sv_interface_application_test.cpp
      fsim.application.sv_interfaces
      fast interfaces packages systemverilog cache debugger vcd)
    fsim_add_application_case(
      1 sv_functions app/sv_function_application_test.cpp
      fsim.application.sv_functions fast functions systemverilog cache)
    fsim_add_application_case(
      1 sv_tasks app/sv_task_application_test.cpp
      fsim.application.sv_tasks fast tasks systemverilog cache)
    fsim_add_application_case(
      1 sv_callable_closure app/sv_callable_closure_application_test.cpp
      fsim.application.sv_callable_closure
      fast functions tasks systemverilog cache debugger vcd)
    fsim_add_application_case(
      1 sv_suspending_tasks app/sv_suspending_task_application_test.cpp
      fsim.application.sv_suspending_tasks
      fast tasks timing systemverilog cache)
    fsim_add_application_case(
      1 sv_mutable_strings app/sv_mutable_string_application_test.cpp
      fsim.application.sv_mutable_strings
      fast strings tasks timing systemverilog cache debugger)
    fsim_add_application_case(
      1 sv_containers app/sv_container_application_test.cpp
      fsim.application.sv_containers
      fast containers tasks timing systemverilog cache)
    add_test(
      NAME fsim.application.sv_container_capacity
      COMMAND fsim_application_tests_1 sv_container_capacity)
    set_tests_properties(
      fsim.application.sv_container_capacity
      PROPERTIES
        LABELS "fast;containers;systemverilog;llvm"
        TIMEOUT 120)
    fsim_add_application_case(
      1 sv_files app/sv_file_application_test.cpp
      fsim.application.sv_files
      fast files strings tasks timing systemverilog cache debugger)
    fsim_add_application_case(
      1 sv_preprocessor_generate
      app/sv_preprocessor_generate_application_test.cpp
      fsim.application.sv_preprocessor_generate
      fast preprocessor generate systemverilog)
    fsim_add_application_case(
      1 sv_aggregate_multidimensional
      app/sv_aggregate_multidimensional_application_test.cpp
      fsim.application.sv_aggregate_multidimensional
      fast aggregates containers systemverilog cache debugger)
    fsim_add_application_case(
      1 sv_conformance app/sv_conformance_application_test.cpp
      fsim.application.sv_conformance
      fast conformance systemverilog expressions statements processes
      functions tasks timing assertions strings files containers memories
      cache llvm)

    fsim_add_application_case(
      2 vhdl_type_generics app/vhdl_type_generic_application_test.cpp
      fsim.application.vhdl_type_generics fast generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_function_generics app/vhdl_function_generic_application_test.cpp
      fsim.application.vhdl_function_generics
      fast functions generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_overloads app/vhdl_overload_application_test.cpp
      fsim.application.vhdl_overloads
      fast functions procedures packages vhdl cache)
    fsim_add_application_case(
      2 vhdl_procedure_generics app/vhdl_procedure_generic_application_test.cpp
      fsim.application.vhdl_procedure_generics
      fast procedures generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_procedure_waits app/vhdl_procedure_wait_application_test.cpp
      fsim.application.vhdl_procedure_waits
      fast procedures timing vhdl llvm)
    fsim_add_application_case(
      2 vhdl_package_generics app/vhdl_package_generic_application_test.cpp
      fsim.application.vhdl_package_generics
      fast packages functions procedures generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_generic_subprograms
      app/vhdl_generic_subprogram_application_test.cpp
      fsim.application.vhdl_generic_subprograms
      fast functions procedures generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_configurations app/vhdl_configuration_application_test.cpp
      fsim.application.vhdl_configurations
      fast configurations vhdl cache hierarchy debugger)
    fsim_add_application_case(
      2 vhdl_analysis_order app/vhdl_analysis_order_application_test.cpp
      fsim.application.vhdl_analysis_order
      fast analysis-order packages contexts configurations vhdl)
    fsim_add_application_case(
      2 vhdl_components app/vhdl_component_application_test.cpp
      fsim.application.vhdl_components
      fast components vhdl cache hierarchy debugger)
    fsim_add_application_case(
      2 vhdl_integer_shifts app/vhdl_integer_shift_application_test.cpp
      fsim.application.vhdl_integer_shifts fast vhdl integer expressions)
    fsim_add_application_case(
      2 vhdl_logic9 app/vhdl_logic9_application_test.cpp
      fsim.application.vhdl_logic9 fast vhdl logic9 resolution)
    fsim_add_application_case(
      2 vhdl_numeric app/vhdl_numeric_application_test.cpp
      fsim.application.vhdl_numeric fast vhdl packages numeric)
    fsim_add_application_case(
      2 vhdl_fixed app/vhdl_fixed_application_test.cpp
      fsim.application.vhdl_fixed fast vhdl packages fixed)
    fsim_add_application_case(
      2 vhdl_float app/vhdl_float_application_test.cpp
      fsim.application.vhdl_float fast vhdl packages floating)
    fsim_add_application_case(
      2 vhdl_ieee_integration app/vhdl_ieee_integration_application_test.cpp
      fsim.application.vhdl_ieee_integration
      fast vhdl packages contexts integration)
    fsim_add_application_case(
      2 vhdl_vital_delays app/vhdl_vital_delay_application_test.cpp
      fsim.application.vhdl_vital_delays
      fast vhdl vital timing delay llvm cache vcd)
    fsim_add_application_case(
      2 vhdl_records app/vhdl_record_application_test.cpp
      fsim.application.vhdl_records fast vhdl records logic9)
    fsim_add_application_case(
      2 vhdl_package_records app/vhdl_package_record_application_test.cpp
      fsim.application.vhdl_package_records fast vhdl records packages logic9)

    fsim_add_application_case(
      3 vhdl_record_aggregates app/vhdl_record_aggregate_application_test.cpp
      fsim.application.vhdl_record_aggregates
      fast vhdl records aggregates packages logic9)
    fsim_add_application_case(
      3 vhdl_subtypes app/vhdl_subtype_application_test.cpp
      fsim.application.vhdl_subtypes
      fast vhdl subtypes packages hierarchy integer logic9)
    fsim_add_application_case(
      3 vhdl_enumerations app/vhdl_enumeration_application_test.cpp
      fsim.application.vhdl_enumerations
      fast vhdl enumerations packages hierarchy cache vcd)
    fsim_add_application_case(
      3 vhdl_arrays app/vhdl_array_application_test.cpp
      fsim.application.vhdl_arrays
      fast vhdl arrays packages hierarchy cache vcd)
    fsim_add_application_case(
      3 vhdl_attributes app/vhdl_attribute_application_test.cpp
      fsim.application.vhdl_attributes
      fast vhdl attributes arrays enumerations integer cache)
    fsim_add_application_case(
      3 vhdl_composite_operations
      app/vhdl_composite_operation_application_test.cpp
      fsim.application.vhdl_composite_operations
      fast vhdl records arrays expressions cache)
    fsim_add_application_case(
      3 vhdl_advanced_types
      app/vhdl_advanced_type_application_test.cpp
      fsim.application.vhdl_advanced_types
      fast vhdl access protected physical cache debugger vcd)
    fsim_add_application_case(
      3 vhdl_projected app/vhdl_projected_application_test.cpp
      fsim.application.vhdl_projected fast vhdl projected-waveforms)
    fsim_add_application_case(
      3 systemc_datatypes app/systemc_datatype_application_test.cpp
      fsim.application.systemc_datatypes fast systemc datatypes)
    fsim_add_application_case(
      3 mixed_conversions app/mixed_conversion_application_test.cpp
      fsim.application.mixed_conversions
      fast mixed-language conversions cache debugger vcd)
    fsim_add_application_case(
      3 typed_boundaries app/typed_boundary_application_test.cpp
      fsim.application.typed_boundaries
      fast semantic hir designir vhdl systemverilog mixed-language systemc
      cache debugger vcd portability)

    set_tests_properties(
      fsim.application.scoped_locals
      PROPERTIES TIMEOUT 60
    )
    set_tests_properties(
      fsim.application.systemc_matrix
      PROPERTIES TIMEOUT 1200
    )
    set_tests_properties(
      fsim.application.sv_containers
      PROPERTIES TIMEOUT 1200
    )
    if(
      WIN32
      AND (
        MSVC
        OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
      )
    )
      set_tests_properties(
        fsim.application
        PROPERTIES TIMEOUT 600
      )
    endif()

    if(FSIM_WITH_TCL)
      fsim_add_application_case(
        3 tcl app/tcl_application_test.cpp
        fsim.application.tcl fast tcl)
      if(FSIM_WITH_BUNDLED_TCL)
        set_tests_properties(
          fsim.application.tcl
          PROPERTIES
            ENVIRONMENT
              "FSIM_TCL_LIBRARY=${FSIM_FETCHED_TCL_LIBRARY_DIR}"
        )
        add_test(
          NAME fsim.tcl.fetched-relocation
          COMMAND
            "${CMAKE_COMMAND}"
            "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
            "-DFSIM_TCL_LIBRARY_DIR=${FSIM_FETCHED_TCL_LIBRARY_DIR}"
            "-DFSIM_TCL_LIBRARY_VERSION=${FSIM_TCL_LIBRARY_VERSION}"
            "-DFSIM_STAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/fetched-tcl-stage"
            -P
            "${PROJECT_SOURCE_DIR}/cmake/CheckFetchedTclRelocation.cmake"
        )
        set_tests_properties(
          fsim.tcl.fetched-relocation
          PROPERTIES LABELS "fast;tcl;cmake"
        )
      endif()
    endif()
  else()
  add_executable(
    fsim_expression_application_tests
    app/expression_application_test.cpp
  )
  fsim_configure_test(fsim_expression_application_tests)
  target_link_libraries(
    fsim_expression_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.expressions
    COMMAND fsim_expression_application_tests
  )
  set_tests_properties(
    fsim.application.expressions
    PROPERTIES LABELS "fast;expressions"
  )

  add_executable(
    fsim_sv_parameter_sizing_application_tests
    app/sv_parameter_sizing_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_parameter_sizing_application_tests
  )
  target_link_libraries(
    fsim_sv_parameter_sizing_application_tests
    PRIVATE fsim_application
  )
add_test(
  NAME fsim.application.sv_parameter_sizing
  COMMAND fsim_sv_parameter_sizing_application_tests
)
  set_tests_properties(
    fsim.application.sv_parameter_sizing
  PROPERTIES LABELS "fast;parameters;systemverilog"
)

add_executable(
  fsim_sv_type_parameter_application_tests
  app/sv_type_parameter_application_test.cpp
)
fsim_configure_test(fsim_sv_type_parameter_application_tests)
target_link_libraries(
  fsim_sv_type_parameter_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sv_type_parameters
  COMMAND fsim_sv_type_parameter_application_tests
)
set_tests_properties(
  fsim.application.sv_type_parameters
  PROPERTIES LABELS "fast;parameters;systemverilog"
)

add_executable(
  fsim_sv_string_parameter_application_tests
  app/sv_string_parameter_application_test.cpp
)
fsim_configure_test(fsim_sv_string_parameter_application_tests)
target_link_libraries(
  fsim_sv_string_parameter_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sv_string_parameters
  COMMAND fsim_sv_string_parameter_application_tests
)
set_tests_properties(
  fsim.application.sv_string_parameters
  PROPERTIES LABELS "fast;parameters;strings;systemverilog"
)

add_executable(
  fsim_vhdl_type_generic_application_tests
  app/vhdl_type_generic_application_test.cpp
)
fsim_configure_test(fsim_vhdl_type_generic_application_tests)
target_link_libraries(
  fsim_vhdl_type_generic_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_type_generics
  COMMAND fsim_vhdl_type_generic_application_tests
)
set_tests_properties(
  fsim.application.vhdl_type_generics
  PROPERTIES LABELS "fast;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_function_generic_application_tests
  app/vhdl_function_generic_application_test.cpp
)
fsim_configure_test(fsim_vhdl_function_generic_application_tests)
target_link_libraries(
  fsim_vhdl_function_generic_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_function_generics
  COMMAND fsim_vhdl_function_generic_application_tests
)
set_tests_properties(
  fsim.application.vhdl_function_generics
  PROPERTIES LABELS "fast;functions;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_overload_application_tests
  app/vhdl_overload_application_test.cpp
)
fsim_configure_test(fsim_vhdl_overload_application_tests)
target_link_libraries(
  fsim_vhdl_overload_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_overloads
  COMMAND fsim_vhdl_overload_application_tests
)
set_tests_properties(
  fsim.application.vhdl_overloads
  PROPERTIES LABELS "fast;functions;procedures;packages;vhdl;cache"
)

add_executable(
  fsim_vhdl_procedure_generic_application_tests
  app/vhdl_procedure_generic_application_test.cpp
)
fsim_configure_test(fsim_vhdl_procedure_generic_application_tests)
target_link_libraries(
  fsim_vhdl_procedure_generic_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_procedure_generics
  COMMAND fsim_vhdl_procedure_generic_application_tests
)
set_tests_properties(
  fsim.application.vhdl_procedure_generics
  PROPERTIES LABELS "fast;procedures;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_procedure_wait_application_tests
  app/vhdl_procedure_wait_application_test.cpp
)
fsim_configure_test(fsim_vhdl_procedure_wait_application_tests)
target_link_libraries(
  fsim_vhdl_procedure_wait_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_procedure_waits
  COMMAND fsim_vhdl_procedure_wait_application_tests
)
set_tests_properties(
  fsim.application.vhdl_procedure_waits
  PROPERTIES LABELS "fast;procedures;timing;vhdl;llvm"
)

add_executable(
  fsim_vhdl_package_generic_application_tests
  app/vhdl_package_generic_application_test.cpp
)
fsim_configure_test(fsim_vhdl_package_generic_application_tests)
target_link_libraries(
  fsim_vhdl_package_generic_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_package_generics
  COMMAND fsim_vhdl_package_generic_application_tests
)
set_tests_properties(
  fsim.application.vhdl_package_generics
  PROPERTIES
    LABELS "fast;packages;functions;procedures;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_generic_subprogram_application_tests
  app/vhdl_generic_subprogram_application_test.cpp
)
fsim_configure_test(
  fsim_vhdl_generic_subprogram_application_tests)
target_link_libraries(
  fsim_vhdl_generic_subprogram_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_generic_subprograms
  COMMAND fsim_vhdl_generic_subprogram_application_tests
)
set_tests_properties(
  fsim.application.vhdl_generic_subprograms
  PROPERTIES
    LABELS "fast;functions;procedures;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_configuration_application_tests
  app/vhdl_configuration_application_test.cpp
)
fsim_configure_test(
  fsim_vhdl_configuration_application_tests)
target_link_libraries(
  fsim_vhdl_configuration_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_configurations
  COMMAND fsim_vhdl_configuration_application_tests
)
set_tests_properties(
  fsim.application.vhdl_configurations
  PROPERTIES
    LABELS "fast;configurations;vhdl;cache;hierarchy;debugger"
)

add_executable(
  fsim_vhdl_component_application_tests
  app/vhdl_component_application_test.cpp
)
fsim_configure_test(
  fsim_vhdl_component_application_tests)
target_link_libraries(
  fsim_vhdl_component_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_components
  COMMAND fsim_vhdl_component_application_tests
)
set_tests_properties(
  fsim.application.vhdl_components
  PROPERTIES
    LABELS "fast;components;vhdl;cache;hierarchy;debugger"
)

  add_executable(
    fsim_scoped_local_application_tests
    app/scoped_local_application_test.cpp
  )
  fsim_configure_test(fsim_scoped_local_application_tests)
  target_link_libraries(
    fsim_scoped_local_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.scoped_locals
    COMMAND fsim_scoped_local_application_tests
  )
  set_tests_properties(
    fsim.application.scoped_locals
    PROPERTIES
      LABELS "fast;scoped-locals"
      TIMEOUT 120
  )

  add_executable(
    fsim_call_safe_point_application_tests
    app/call_safe_point_application_test.cpp
  )
  fsim_configure_test(fsim_call_safe_point_application_tests)
  target_link_libraries(
    fsim_call_safe_point_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.call_safe_points
    COMMAND fsim_call_safe_point_application_tests
  )
  set_tests_properties(
    fsim.application.call_safe_points
    PROPERTIES LABELS "fast;call-safe-points"
  )

  add_executable(
    fsim_sv_interface_application_tests
    app/sv_interface_application_test.cpp
  )
  fsim_configure_test(fsim_sv_interface_application_tests)
  target_link_libraries(
    fsim_sv_interface_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_interfaces
    COMMAND fsim_sv_interface_application_tests
  )
  set_tests_properties(
    fsim.application.sv_interfaces
    PROPERTIES
      LABELS "fast;interfaces;packages;systemverilog;cache;debugger;vcd"
  )

  add_executable(
    fsim_sv_function_application_tests
    app/sv_function_application_test.cpp
  )
  fsim_configure_test(fsim_sv_function_application_tests)
  target_link_libraries(
    fsim_sv_function_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_functions
    COMMAND fsim_sv_function_application_tests
  )
  set_tests_properties(
    fsim.application.sv_functions
    PROPERTIES
      LABELS "fast;functions;systemverilog;cache"
  )

  add_executable(
    fsim_sv_task_application_tests
    app/sv_task_application_test.cpp
  )
  fsim_configure_test(fsim_sv_task_application_tests)
  target_link_libraries(
    fsim_sv_task_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_tasks
    COMMAND fsim_sv_task_application_tests
  )
  set_tests_properties(
    fsim.application.sv_tasks
    PROPERTIES
      LABELS "fast;tasks;systemverilog;cache"
  )

  add_executable(
    fsim_sv_callable_closure_application_tests
    app/sv_callable_closure_application_test.cpp
  )
  fsim_configure_test(fsim_sv_callable_closure_application_tests)
  target_link_libraries(
    fsim_sv_callable_closure_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_callable_closure
    COMMAND fsim_sv_callable_closure_application_tests
  )
  set_tests_properties(
    fsim.application.sv_callable_closure
    PROPERTIES
      LABELS "fast;functions;tasks;systemverilog;cache;debugger;vcd"
  )

  add_executable(
    fsim_sv_suspending_task_application_tests
    app/sv_suspending_task_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_suspending_task_application_tests
  )
  target_link_libraries(
    fsim_sv_suspending_task_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_suspending_tasks
    COMMAND fsim_sv_suspending_task_application_tests
  )
  set_tests_properties(
    fsim.application.sv_suspending_tasks
    PROPERTIES
      LABELS "fast;tasks;timing;systemverilog;cache"
  )

  add_executable(
    fsim_sv_mutable_string_application_tests
    app/sv_mutable_string_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_mutable_string_application_tests
  )
  target_link_libraries(
    fsim_sv_mutable_string_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_mutable_strings
    COMMAND fsim_sv_mutable_string_application_tests
  )
  set_tests_properties(
    fsim.application.sv_mutable_strings
    PROPERTIES
      LABELS "fast;strings;tasks;timing;systemverilog;cache;debugger"
  )

  add_executable(
    fsim_sv_container_application_tests
    app/sv_container_application_test.cpp
  )
  fsim_configure_test(fsim_sv_container_application_tests)
  target_link_libraries(
    fsim_sv_container_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_containers
    COMMAND fsim_sv_container_application_tests
  )
  add_test(
    NAME fsim.application.sv_container_capacity
    COMMAND fsim_sv_container_application_tests capacity
  )
  set_tests_properties(
    fsim.application.sv_container_capacity
    PROPERTIES
      LABELS "fast;containers;systemverilog;llvm"
      TIMEOUT 60
  )
  set_tests_properties(
    fsim.application.sv_containers
    PROPERTIES
      LABELS "fast;containers;tasks;timing;systemverilog;cache"
      # LLVM-disabled sanitizer builds retain one complete semantic/debugger
      # pass and avoid redundant compiled/O2 aliases. Twenty minutes preserves
      # hosted-runner headroom while keeping future stalls diagnostic.
      TIMEOUT 1200
  )

  add_executable(
    fsim_sv_file_application_tests
    app/sv_file_application_test.cpp
  )
  fsim_configure_test(fsim_sv_file_application_tests)
  target_link_libraries(
    fsim_sv_file_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_files
    COMMAND fsim_sv_file_application_tests
  )
  set_tests_properties(
    fsim.application.sv_files
    PROPERTIES
      LABELS "fast;files;strings;tasks;timing;systemverilog;cache;debugger"
  )

  add_executable(
    fsim_named_event_application_tests
    app/named_event_application_test.cpp
  )
  fsim_configure_test(fsim_named_event_application_tests)
  target_link_libraries(
    fsim_named_event_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.named_events
    COMMAND fsim_named_event_application_tests
  )
  set_tests_properties(
    fsim.application.named_events
    PROPERTIES LABELS "fast;named-events"
  )

  add_executable(
    fsim_fork_application_tests
    app/fork_application_test.cpp
  )
  fsim_configure_test(fsim_fork_application_tests)
  target_link_libraries(
    fsim_fork_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.fork
    COMMAND fsim_fork_application_tests
  )
  set_tests_properties(
    fsim.application.fork
    PROPERTIES LABELS "fast;fork;systemverilog;cache;debugger"
  )

  add_executable(
    fsim_display_application_tests
    app/display_application_test.cpp
  )
  fsim_configure_test(fsim_display_application_tests)
  target_link_libraries(
    fsim_display_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.display
    COMMAND fsim_display_application_tests
  )
  set_tests_properties(
    fsim.application.display
    PROPERTIES LABELS "fast;display"
  )

  add_executable(
    fsim_random_application_tests
    app/random_application_test.cpp
  )
  fsim_configure_test(fsim_random_application_tests)
  target_link_libraries(
    fsim_random_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.random
    COMMAND fsim_random_application_tests
  )
  set_tests_properties(
    fsim.application.random
    PROPERTIES LABELS "fast;random"
  )

  add_executable(
    fsim_assertion_application_tests
    app/assertion_application_test.cpp
  )
  fsim_configure_test(fsim_assertion_application_tests)
  target_link_libraries(
    fsim_assertion_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.assertions
    COMMAND fsim_assertion_application_tests
  )
  set_tests_properties(
    fsim.application.assertions
    PROPERTIES LABELS "fast;assertions"
  )

  add_executable(
    fsim_line_directive_application_tests
    app/line_directive_application_test.cpp
  )
  fsim_configure_test(fsim_line_directive_application_tests)
  target_link_libraries(
    fsim_line_directive_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.line_directives
    COMMAND fsim_line_directive_application_tests
  )
  set_tests_properties(
    fsim.application.line_directives
    PROPERTIES LABELS "fast;line-directives"
  )

  add_executable(
    fsim_sv_preprocessor_generate_application_tests
    app/sv_preprocessor_generate_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_preprocessor_generate_application_tests
  )
  target_link_libraries(
    fsim_sv_preprocessor_generate_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_preprocessor_generate
    COMMAND fsim_sv_preprocessor_generate_application_tests
  )
  set_tests_properties(
    fsim.application.sv_preprocessor_generate
    PROPERTIES LABELS "fast;preprocessor;generate;systemverilog"
  )

  add_executable(
    fsim_sv_aggregate_multidimensional_application_tests
    app/sv_aggregate_multidimensional_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_aggregate_multidimensional_application_tests
  )
  target_link_libraries(
    fsim_sv_aggregate_multidimensional_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_aggregate_multidimensional
    COMMAND fsim_sv_aggregate_multidimensional_application_tests
  )
  set_tests_properties(
    fsim.application.sv_aggregate_multidimensional
    PROPERTIES LABELS "fast;aggregates;containers;systemverilog;cache;debugger"
  )

  add_executable(
    fsim_time_application_tests
    app/time_application_test.cpp
  )
  fsim_configure_test(fsim_time_application_tests)
  target_link_libraries(
    fsim_time_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.time
    COMMAND fsim_time_application_tests
  )
  set_tests_properties(
    fsim.application.time
    PROPERTIES LABELS "fast;time"
  )

  add_executable(
    fsim_delay_mode_application_tests
    app/delay_mode_application_test.cpp
  )
  fsim_configure_test(fsim_delay_mode_application_tests)
  target_link_libraries(
    fsim_delay_mode_application_tests
    PRIVATE
      fsim_application
      fsim_cli
  )
  add_test(
    NAME fsim.application.delay_modes
    COMMAND fsim_delay_mode_application_tests
  )
  set_tests_properties(
    fsim.application.delay_modes
    PROPERTIES LABELS "fast;delay-modes"
  )

  add_executable(
    fsim_transition_delay_application_tests
    app/transition_delay_application_test.cpp
  )
  fsim_configure_test(fsim_transition_delay_application_tests)
  target_link_libraries(
    fsim_transition_delay_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.transition_delays
    COMMAND fsim_transition_delay_application_tests
  )
  set_tests_properties(
    fsim.application.transition_delays
    PROPERTIES LABELS "fast;transition-delays"
  )

  add_executable(
    fsim_procedural_assignment_application_tests
    app/procedural_assignment_application_test.cpp
  )
  fsim_configure_test(fsim_procedural_assignment_application_tests)
  target_link_libraries(
    fsim_procedural_assignment_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.procedural_assignments
    COMMAND fsim_procedural_assignment_application_tests
  )
  set_tests_properties(
    fsim.application.procedural_assignments
    PROPERTIES LABELS "fast;procedural-assignments"
  )

  add_executable(
    fsim_resolution_application_tests
    app/resolution_application_test.cpp
  )
  fsim_configure_test(fsim_resolution_application_tests)
  target_link_libraries(
    fsim_resolution_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.resolution
    COMMAND fsim_resolution_application_tests
  )
  set_tests_properties(
    fsim.application.resolution
    PROPERTIES LABELS "fast;resolution;mixed-language"
  )

  add_executable(
    fsim_mixed_conversion_application_tests
    app/mixed_conversion_application_test.cpp
  )
  fsim_configure_test(fsim_mixed_conversion_application_tests)
  target_link_libraries(
    fsim_mixed_conversion_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.mixed_conversions
    COMMAND fsim_mixed_conversion_application_tests
  )
  set_tests_properties(
    fsim.application.mixed_conversions
    PROPERTIES
      LABELS "fast;mixed-language;conversions;cache;debugger;vcd"
  )

  add_executable(
    fsim_typed_boundary_application_tests
    app/typed_boundary_application_test.cpp
  )
  fsim_configure_test(fsim_typed_boundary_application_tests)
  target_compile_definitions(
    fsim_typed_boundary_application_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(
    fsim_typed_boundary_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.typed_boundaries
    COMMAND fsim_typed_boundary_application_tests
  )
  set_tests_properties(
    fsim.application.typed_boundaries
    PROPERTIES
      LABELS
        "fast;semantic;hir;designir;vhdl;systemverilog;mixed-language;systemc;cache;debugger;vcd;portability"
  )

  add_executable(
    fsim_systemc_datatype_application_tests
    app/systemc_datatype_application_test.cpp
  )
  fsim_configure_test(fsim_systemc_datatype_application_tests)
  target_compile_definitions(
    fsim_systemc_datatype_application_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(
    fsim_systemc_datatype_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.systemc_datatypes
    COMMAND fsim_systemc_datatype_application_tests
  )
  set_tests_properties(
    fsim.application.systemc_datatypes
    PROPERTIES LABELS "fast;systemc;datatypes"
  )

  add_executable(
    fsim_vhdl_integer_shift_application_tests
    app/vhdl_integer_shift_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_integer_shift_application_tests
  )
  target_link_libraries(
    fsim_vhdl_integer_shift_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_integer_shifts
    COMMAND fsim_vhdl_integer_shift_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_integer_shifts
    PROPERTIES LABELS "fast;vhdl;integer;expressions"
  )

  add_executable(
    fsim_vhdl_logic9_application_tests
    app/vhdl_logic9_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_logic9_application_tests
  )
  target_link_libraries(
    fsim_vhdl_logic9_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_logic9
    COMMAND fsim_vhdl_logic9_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_logic9
    PROPERTIES LABELS "fast;vhdl;logic9;resolution"
  )

  add_executable(
    fsim_vhdl_record_application_tests
    app/vhdl_record_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_record_application_tests
  )
  target_link_libraries(
    fsim_vhdl_record_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_records
    COMMAND fsim_vhdl_record_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_records
    PROPERTIES LABELS "fast;vhdl;records;logic9"
  )

  add_executable(
    fsim_vhdl_package_record_application_tests
    app/vhdl_package_record_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_package_record_application_tests
  )
  target_link_libraries(
    fsim_vhdl_package_record_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_package_records
    COMMAND fsim_vhdl_package_record_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_package_records
    PROPERTIES LABELS "fast;vhdl;records;packages;logic9"
  )

  add_executable(
    fsim_vhdl_record_aggregate_application_tests
    app/vhdl_record_aggregate_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_record_aggregate_application_tests
  )
  target_link_libraries(
    fsim_vhdl_record_aggregate_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_record_aggregates
    COMMAND fsim_vhdl_record_aggregate_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_record_aggregates
    PROPERTIES LABELS "fast;vhdl;records;aggregates;packages;logic9"
  )

  add_executable(
    fsim_vhdl_subtype_application_tests
    app/vhdl_subtype_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_subtype_application_tests
  )
  target_link_libraries(
    fsim_vhdl_subtype_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_subtypes
    COMMAND fsim_vhdl_subtype_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_subtypes
    PROPERTIES LABELS "fast;vhdl;subtypes;packages;hierarchy;integer;logic9"
  )

  add_executable(
    fsim_vhdl_enumeration_application_tests
    app/vhdl_enumeration_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_enumeration_application_tests
  )
  target_link_libraries(
    fsim_vhdl_enumeration_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_enumerations
    COMMAND fsim_vhdl_enumeration_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_enumerations
    PROPERTIES LABELS "fast;vhdl;enumerations;packages;hierarchy;cache;vcd"
  )

  add_executable(
    fsim_vhdl_array_application_tests
    app/vhdl_array_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_array_application_tests
  )
  target_link_libraries(
    fsim_vhdl_array_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_arrays
    COMMAND fsim_vhdl_array_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_arrays
    PROPERTIES LABELS "fast;vhdl;arrays;packages;hierarchy;cache;vcd"
  )

  add_executable(
    fsim_vhdl_attribute_application_tests
    app/vhdl_attribute_application_test.cpp
  )
  fsim_configure_test(fsim_vhdl_attribute_application_tests)
  target_link_libraries(
    fsim_vhdl_attribute_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_attributes
    COMMAND fsim_vhdl_attribute_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_attributes
    PROPERTIES LABELS "fast;vhdl;attributes;arrays;enumerations;integer;cache"
  )

  add_executable(
    fsim_vhdl_composite_operation_application_tests
    app/vhdl_composite_operation_application_test.cpp
  )
  fsim_configure_test(fsim_vhdl_composite_operation_application_tests)
  target_link_libraries(
    fsim_vhdl_composite_operation_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_composite_operations
    COMMAND fsim_vhdl_composite_operation_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_composite_operations
    PROPERTIES LABELS "fast;vhdl;records;arrays;expressions;cache"
  )

  add_executable(
    fsim_vhdl_projected_application_tests
    app/vhdl_projected_application_test.cpp
  )
  fsim_configure_test(fsim_vhdl_projected_application_tests)
  target_link_libraries(
    fsim_vhdl_projected_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_projected
    COMMAND fsim_vhdl_projected_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_projected
    PROPERTIES LABELS "fast;vhdl;projected-waveforms"
  )

  add_executable(
    fsim_vhdl_advanced_type_application_tests
    app/vhdl_advanced_type_application_test.cpp
  )
  fsim_configure_test(fsim_vhdl_advanced_type_application_tests)
  target_link_libraries(
    fsim_vhdl_advanced_type_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_advanced_types
    COMMAND fsim_vhdl_advanced_type_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_advanced_types
    PROPERTIES
      LABELS "fast;vhdl;access;protected;physical;cache;debugger;vcd"
  )

  add_executable(
    fsim_application_tests
    app/application_test.cpp
    app/application_test_artifact_phases.cpp
    app/application_test_cli.cpp
    app/application_test_fixture.cpp
    app/application_test_mixed.cpp
    app/application_test_multiple_roots.cpp
    app/application_test_non_project_cli.cpp
    app/application_test_simulation.cpp
    app/application_test_source_updates.cpp
    app/application_test_sources_common.cpp
    app/application_test_sources_mixed.cpp
    app/application_test_sources_systemc.cpp
    app/application_test_specialization.cpp
    app/application_test_support.cpp
    app/application_test_systemc.cpp
    app/application_test_systemc_scheduling.cpp
  )
  fsim_configure_test(fsim_application_tests)
  if(
    WIN32
    AND (
      MSVC
      OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
    )
  )
    # The monolithic application regression retains many large project and
    # type values in one unoptimized fixture frame. Give MSVC-compatible Debug
    # builds the same bounded stack budget as the accumulated frontend and
    # elaboration matrices.
    target_link_options(
      fsim_application_tests
      PRIVATE "$<$<CONFIG:Debug>:/STACK:8388608>"
    )
  endif()
  target_compile_definitions(
    fsim_application_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(fsim_application_tests PRIVATE fsim_application)
  add_test(NAME fsim.application COMMAND fsim_application_tests)
  if(
    WIN32
    AND (
      MSVC
      OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
    )
  )
    set_tests_properties(
      fsim.application
      PROPERTIES TIMEOUT 600
    )
  endif()

  if(FSIM_WITH_TCL)
    add_executable(
      fsim_tcl_application_tests
      app/tcl_application_test.cpp
    )
    fsim_configure_test(fsim_tcl_application_tests)
    target_link_libraries(
      fsim_tcl_application_tests
      PRIVATE fsim_application
    )
    add_test(
      NAME fsim.application.tcl
      COMMAND fsim_tcl_application_tests
    )
    set_tests_properties(
      fsim.application.tcl
      PROPERTIES LABELS "fast;tcl"
    )
    if(FSIM_WITH_BUNDLED_TCL)
      set_tests_properties(
        fsim.application.tcl
        PROPERTIES
          ENVIRONMENT
            "FSIM_TCL_LIBRARY=${FSIM_FETCHED_TCL_LIBRARY_DIR}"
      )
      add_test(
        NAME fsim.tcl.fetched-relocation
        COMMAND
          "${CMAKE_COMMAND}"
          "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
          "-DFSIM_TCL_LIBRARY_DIR=${FSIM_FETCHED_TCL_LIBRARY_DIR}"
          "-DFSIM_TCL_LIBRARY_VERSION=${FSIM_TCL_LIBRARY_VERSION}"
          "-DFSIM_STAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/fetched-tcl-stage"
          -P
          "${PROJECT_SOURCE_DIR}/cmake/CheckFetchedTclRelocation.cmake"
      )
      set_tests_properties(
        fsim.tcl.fetched-relocation
        PROPERTIES LABELS "fast;tcl;cmake"
      )
    endif()
  endif()

  endif()

  add_executable(
    fsim_api_tests
    api/api_test.cpp
    api/api_mapped_library_test.cpp
    api/api_multiple_root_test.cpp
  )
  fsim_configure_test(fsim_api_tests)
  target_compile_definitions(
    fsim_api_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(
    fsim_api_tests PRIVATE fsim_api fsim_application)
  add_test(NAME fsim.api COMMAND fsim_api_tests)
  set_tests_properties(fsim.api PROPERTIES TIMEOUT 120)
  if(WIN32)
    add_custom_command(
      TARGET fsim_api_tests
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:fsim_api>"
        "$<TARGET_FILE_DIR:fsim_api_tests>"
      VERBATIM
    )
  endif()

  add_executable(fsim_api_header_c_test api/api_header_c_test.c)
  target_compile_features(fsim_api_header_c_test PRIVATE c_std_11)
  target_link_libraries(fsim_api_header_c_test PRIVATE fsim_api)
  set_target_properties(
    fsim_api_header_c_test
    PROPERTIES LINKER_LANGUAGE CXX
  )
  fsim_configure_test_platform(fsim_api_header_c_test)
  add_test(NAME fsim.api.c_header COMMAND fsim_api_header_c_test)
  if(WIN32)
    add_custom_command(
      TARGET fsim_api_header_c_test
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:fsim_api>"
        "$<TARGET_FILE_DIR:fsim_api_header_c_test>"
      VERBATIM
    )
  endif()
endif()

# SPDX-License-Identifier: Apache-2.0

add_subdirectory(runtime)

add_executable(
  fsim_frontend_tests
  frontend/frontend_tests.cpp
  frontend/frontend_preprocessing_tests.cpp
  frontend/frontend_vhdl_type_tests.cpp
  frontend/frontend_vhdl_statement_tests.cpp
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
  elaboration/elaborator_process_test.cpp
  elaboration/elaborator_fork_test.cpp
  elaboration/elaborator_function_test.cpp
  elaboration/elaborator_task_test.cpp
  elaboration/elaborator_case_qualifier_test.cpp
  elaboration/elaborator_case_match_test.cpp
  elaboration/elaborator_expression_test.cpp
  elaboration/elaborator_numeric_test.cpp
  elaboration/elaborator_selection_test.cpp
  elaboration/elaborator_types_test.cpp
  elaboration/elaborator_type_generic_test.cpp
  elaboration/elaborator_vhdl_function_test.cpp
  elaboration/elaborator_vhdl_overload_test.cpp
  elaboration/elaborator_vhdl_procedure_test.cpp
  elaboration/elaborator_vhdl_package_test.cpp
  elaboration/elaborator_vhdl_generic_subprogram_test.cpp
  elaboration/elaborator_vhdl_generic_association_test.cpp
  elaboration/elaborator_vhdl_component_test.cpp
  elaboration/elaborator_vhdl_configuration_test.cpp
  elaboration/elaborator_sv_file_test.cpp
  elaboration/elaborator_sv_container_test.cpp
  elaboration/elaborator_sv_aggregate_container_test.cpp
  elaboration/elaborator_sv_slice_call_test.cpp
  elaboration/elaborator_sv_slice_order_test.cpp
  elaboration/elaborator_sv_slice_port_test.cpp
  elaboration/elaborator_sv_interface_test.cpp
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

add_library(
  fsim_test_throwing_systemc_plugin
  MODULE
  systemc/throwing_plugin.cpp
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
)

add_executable(
  fsim_systemc_compiler_tests
  systemc/plugin_compiler_test.cpp
)
fsim_configure_test(fsim_systemc_compiler_tests)
target_link_libraries(fsim_systemc_compiler_tests PRIVATE fsim_systemc)
add_test(NAME fsim.systemc.compiler COMMAND fsim_systemc_compiler_tests)

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
    target_sources(
      fsim_application_tests
      PRIVATE
        app/application_test_cli.cpp
        app/application_test_fixture.cpp
        app/application_test_mixed.cpp
        app/application_test_simulation.cpp
        app/application_test_source_updates.cpp
        app/application_test_sources_common.cpp
        app/application_test_sources_mixed.cpp
        app/application_test_sources_systemc.cpp
        app/application_test_specialization.cpp
        app/application_test_support.cpp
        app/application_test_systemc.cpp
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
      3 vhdl_projected app/vhdl_projected_application_test.cpp
      fsim.application.vhdl_projected fast vhdl projected-waveforms)
    fsim_add_application_case(
      3 systemc_datatypes app/systemc_datatype_application_test.cpp
      fsim.application.systemc_datatypes fast systemc datatypes)

    set_tests_properties(
      fsim.application.scoped_locals
      PROPERTIES TIMEOUT 60
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
      TIMEOUT 60
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
    fsim_application_tests
    app/application_test.cpp
    app/application_test_cli.cpp
    app/application_test_fixture.cpp
    app/application_test_mixed.cpp
    app/application_test_simulation.cpp
    app/application_test_source_updates.cpp
    app/application_test_sources_common.cpp
    app/application_test_sources_mixed.cpp
    app/application_test_sources_systemc.cpp
    app/application_test_specialization.cpp
    app/application_test_support.cpp
    app/application_test_systemc.cpp
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
  if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
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

  add_executable(fsim_api_tests api/api_test.cpp)
  fsim_configure_test(fsim_api_tests)
  target_link_libraries(fsim_api_tests PRIVATE fsim_api)
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

# SPDX-License-Identifier: Apache-2.0

add_subdirectory(runtime)

add_executable(fsim_frontend_tests frontend/frontend_tests.cpp)
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

add_executable(fsim_elaboration_tests elaboration/elaborator_test.cpp)
fsim_configure_test(fsim_elaboration_tests)
target_link_libraries(fsim_elaboration_tests PRIVATE fsim_elaboration)
if(
  WIN32
  AND (
    MSVC
    OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
  )
)
  # This accumulated feature-matrix test intentionally retains hundreds of
  # independent fixtures in one translation unit. Unoptimized MSVC-compatible
  # builds otherwise exhaust Windows' 1 MiB default stack on entry.
  target_link_options(
    fsim_elaboration_tests
    PRIVATE "$<$<CONFIG:Debug>:/STACK:8388608>"
  )
endif()
add_test(NAME fsim.elaboration COMMAND fsim_elaboration_tests)

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
    PROPERTIES LABELS "fast;scoped-locals"
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

  add_executable(fsim_application_tests app/application_test.cpp)
  fsim_configure_test(fsim_application_tests)
  target_compile_definitions(
    fsim_application_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(fsim_application_tests PRIVATE fsim_application)
  add_test(NAME fsim.application COMMAND fsim_application_tests)

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

  add_executable(fsim_api_tests api/api_test.cpp)
  fsim_configure_test(fsim_api_tests)
  target_link_libraries(fsim_api_tests PRIVATE fsim_api)
  add_test(NAME fsim.api COMMAND fsim_api_tests)
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

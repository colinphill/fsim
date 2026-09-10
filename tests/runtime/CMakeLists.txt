# SPDX-License-Identifier: Apache-2.0

# This directory is intentionally usable from a root CMakeLists.txt with:
#   add_subdirectory(tests/runtime)
# It expects the runtime target to be named fsim_runtime.
add_executable(
  fsim_runtime_tests
  runtime_tests.cpp
  runtime_value_tests.cpp
  runtime_value_primitives_tests.cpp
  runtime_dpi_tests.cpp
  runtime_dpi_abi_c_test.c
  runtime_foreign_abi_cpp_test.cpp
  runtime_vhpi_tests.cpp
  runtime_vhpi_abi_c_test.c
  runtime_vhpi_object_tests.cpp
  runtime_vhpi_hierarchy_tests.cpp
  runtime_vhpi_type_tests.cpp
  runtime_vhpi_value_tests.cpp
  runtime_vhpi_composite_tests.cpp
  runtime_vhpi_special_tests.cpp
  runtime_vhpi_driver_tests.cpp
  runtime_vhpi_write_tests.cpp
  runtime_vhpi_time_tests.cpp
  runtime_vhpi_callback_tests.cpp
  runtime_vhpi_foreign_tests.cpp
  runtime_vhpi_association_tests.cpp
  runtime_vhpi_io_tests.cpp
  runtime_vhpi_checkpoint_tests.cpp
  runtime_vhpi_reference_plugin_tests.cpp
  runtime_vhpi_negative_tests.cpp
  runtime_vpi_tests.cpp
  runtime_vpi_abi_c_test.c
  runtime_vpi_object_tests.cpp
  runtime_vpi_type_tests.cpp
  runtime_vpi_descriptor_tests.cpp
  runtime_vpi_value_tests.cpp
  runtime_vpi_write_tests.cpp
  runtime_vpi_time_tests.cpp
  runtime_vpi_callback_tests.cpp
  runtime_vpi_checkpoint_tests.cpp
  runtime_vpi_reference_plugin_tests.cpp
  runtime_vpi_control_tests.cpp
  runtime_vpi_io_tests.cpp
  runtime_vpi_system_tests.cpp
  runtime_scalar_text_tests.cpp
  runtime_chandle_tests.cpp
  runtime_unicode_string_tests.cpp
  runtime_vhdl_psl_tests.cpp
  runtime_execution_tests.cpp
  runtime_execution_services_tests.cpp
  runtime_execution_lifecycle.cpp
  runtime_synchronization_tests.cpp
  runtime_file_tests.cpp
  runtime_container_tests.cpp
  runtime_class_heap_tests.cpp
  runtime_uvm_object_policy_tests.cpp
  runtime_uvm_packer_tests.cpp
  runtime_uvm_synchronization_policy_tests.cpp
  runtime_uvm_test_runner_tests.cpp
  runtime_uvm_component_tests.cpp
  runtime_uvm_phase_tests.cpp
  runtime_uvm_quiescence_tests.cpp
  runtime_uvm_objection_tests.cpp
  runtime_uvm_tlm1_tests.cpp
  runtime_uvm_tlm1_execution_tests.cpp
  runtime_uvm_analysis_tests.cpp
  runtime_uvm_tlm2_tests.cpp
  runtime_uvm_sequence_tests.cpp
  runtime_uvm_sequence_access_tests.cpp
  runtime_uvm_sequence_macro_tests.cpp
  runtime_uvm_sequence_handshake_tests.cpp
  runtime_uvm_sequence_role_tests.cpp
  runtime_uvm_sequence_virtual_tests.cpp
  runtime_uvm_activity_tests.cpp
  runtime_uvm_callback_tests.cpp
  runtime_uvm_register_model_tests.cpp
  runtime_uvm_register_value_tests.cpp
  runtime_uvm_register_map_tests.cpp
  runtime_uvm_register_frontdoor_tests.cpp
  runtime_uvm_register_backdoor_tests.cpp
  runtime_uvm_register_sequence_tests.cpp
  runtime_uvm_register_coverage_tests.cpp
  runtime_uvm_checkpoint_tests.cpp
  runtime_uvm_foreign_abi_c_test.c
  runtime_uvm_registry_tests.cpp
  runtime_uvm_factory_tests.cpp
  runtime_uvm_resource_tests.cpp
  runtime_uvm_config_db_tests.cpp
  runtime_uvm_command_line_tests.cpp
  runtime_uvm_report_tests.cpp
  runtime_constraint_solver_tests.cpp
  runtime_executor_tests.cpp
  runtime_specify_tests.cpp
  runtime_timing_tests.cpp
  runtime_vital_memory_path_tests.cpp
)
target_compile_features(fsim_runtime_tests PRIVATE c_std_11)
target_link_libraries(fsim_runtime_tests PRIVATE fsim_runtime)
add_library(
  fsim_dpi_test_plugin SHARED dpi_test_plugin.cpp dpi_test_plugin_c.c)
fsim_configure_library(fsim_dpi_test_plugin)
target_include_directories(
  fsim_dpi_test_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fsim_dpi_test_plugin PRIVATE fsim_tf)
add_library(fsim_vpi_test_plugin SHARED vpi_test_plugin.cpp)
fsim_configure_library(fsim_vpi_test_plugin)
target_include_directories(
  fsim_vpi_test_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(
  fsim_vpi_standard_startup_plugin SHARED vpi_standard_startup_plugin.c)
fsim_configure_library(fsim_vpi_standard_startup_plugin)
target_link_libraries(fsim_vpi_standard_startup_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_vpi_standard_startup_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(fsim_vhpi_test_plugin SHARED vhpi_test_plugin.cpp)
fsim_configure_library(fsim_vhpi_test_plugin)
target_include_directories(
  fsim_vhpi_test_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(
  fsim_vhpi_reference_c_plugin SHARED vhpi_reference_plugin_c.c)
fsim_configure_library(fsim_vhpi_reference_c_plugin)
target_include_directories(
  fsim_vhpi_reference_c_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(
  fsim_vhpi_reference_cpp_plugin SHARED vhpi_reference_plugin_cpp.cpp)
fsim_configure_library(fsim_vhpi_reference_cpp_plugin)
target_include_directories(
  fsim_vhpi_reference_cpp_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(fsim_vpi_reference_c_plugin SHARED vpi_reference_plugin_c.c)
fsim_configure_library(fsim_vpi_reference_c_plugin)
target_include_directories(
  fsim_vpi_reference_c_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(
  fsim_vpi_reference_cpp_plugin SHARED vpi_reference_plugin_cpp.cpp)
fsim_configure_library(fsim_vpi_reference_cpp_plugin)
target_include_directories(
  fsim_vpi_reference_cpp_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(fsim_dpi_bad_abi_plugin SHARED dpi_bad_abi_plugin.cpp)
fsim_configure_library(fsim_dpi_bad_abi_plugin)
target_include_directories(
  fsim_dpi_bad_abi_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(
  fsim_runtime_tests
  PRIVATE FSIM_DPI_TEST_PLUGIN_PATH="$<TARGET_FILE:fsim_dpi_test_plugin>"
          FSIM_DPI_BAD_ABI_PLUGIN_PATH="$<TARGET_FILE:fsim_dpi_bad_abi_plugin>"
          FSIM_VHPI_TEST_PLUGIN_PATH="$<TARGET_FILE:fsim_vhpi_test_plugin>"
          FSIM_VHPI_REFERENCE_C_PLUGIN_PATH="$<TARGET_FILE:fsim_vhpi_reference_c_plugin>"
          FSIM_VHPI_REFERENCE_CPP_PLUGIN_PATH="$<TARGET_FILE:fsim_vhpi_reference_cpp_plugin>"
          FSIM_VPI_TEST_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_test_plugin>"
          FSIM_VPI_STANDARD_STARTUP_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_standard_startup_plugin>"
          FSIM_VPI_REFERENCE_C_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_reference_c_plugin>"
          FSIM_VPI_REFERENCE_CPP_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_reference_cpp_plugin>"
)
add_dependencies(
  fsim_runtime_tests fsim_dpi_test_plugin fsim_dpi_bad_abi_plugin
  fsim_vhpi_test_plugin fsim_vpi_test_plugin
  fsim_vpi_standard_startup_plugin)
add_dependencies(
  fsim_runtime_tests fsim_vhpi_reference_c_plugin
  fsim_vhpi_reference_cpp_plugin)
add_dependencies(
  fsim_runtime_tests fsim_vpi_reference_c_plugin
  fsim_vpi_reference_cpp_plugin)
fsim_configure_test(fsim_runtime_tests)
add_test(NAME fsim.runtime COMMAND fsim_runtime_tests)
add_test(
  NAME fsim.runtime.vpi_reference_plugins
  COMMAND fsim_runtime_tests vpi_reference_plugins)
set_tests_properties(
  fsim.runtime.vpi_reference_plugins
  PROPERTIES
    LABELS
      "fast;runtime;systemverilog2023;vpi;foreign;c;cxx;plugin;linux;windows;interpreter;llvm;o0;o2;artifact;relocation;negative"
    RESOURCE_LOCK fsim_vpi_reference_plugins)
# These recursive closure drivers are registered by the parent test directory,
# but their runtime witness is registered here. Attach the setup ownership in
# the creating directory so selecting any driver executes this witness exactly
# once before the driver's de-duplicated completion check.
set_tests_properties(
  fsim.runtime
  PROPERTIES
    FIXTURES_SETUP
      "fsim_compatibility_smoke_closure_witnesses;fsim_verilog_systemverilog_standard_mode_closure_witnesses;fsim_systemverilog_closure_witnesses;fsim_verilog_closure_witnesses"
    RESOURCE_LOCK fsim_vpi_reference_plugins
)

add_executable(
  fsim_vpi_coverage_tests
  vpi_coverage_test.cpp
)
target_link_libraries(fsim_vpi_coverage_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_vpi_coverage_tests)
add_test(NAME fsim.runtime.vpi_coverage COMMAND fsim_vpi_coverage_tests)
set_tests_properties(
  fsim.runtime.vpi_coverage
  PROPERTIES LABELS "fast;runtime;vpi;coverage;control;property;fsm;traversal;positive;negative;resource"
)

add_executable(
  fsim_vpi_data_read_tests
  vpi_data_read_test.cpp
)
target_link_libraries(fsim_vpi_data_read_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_vpi_data_read_tests)
add_test(NAME fsim.runtime.vpi_data_read COMMAND fsim_vpi_data_read_tests)
set_tests_properties(
  fsim.runtime.vpi_data_read
  PROPERTIES LABELS
    "fast;runtime;vpi;data-read;collection;traversal;positive;negative;resource"
)

add_executable(
  fsim_native_plugin_abi_tests
  native_plugin_abi_test.cpp
  native_plugin_abi_c_test.c
)
target_compile_features(fsim_native_plugin_abi_tests PRIVATE c_std_11)
target_link_libraries(fsim_native_plugin_abi_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_native_plugin_abi_tests)
add_test(
  NAME fsim.runtime.native_plugin_abi
  COMMAND fsim_native_plugin_abi_tests
)
set_tests_properties(
  fsim.runtime.native_plugin_abi
  PROPERTIES LABELS
    "fast;runtime;native-plugin;abi;tf;acc;positive;negative;portability"
)

add_executable(
  fsim_veriuser_abi_tests
  veriuser_abi_test.cpp
  veriuser_abi_c_test.c
)
target_compile_features(fsim_veriuser_abi_tests PRIVATE c_std_11)
target_link_libraries(fsim_veriuser_abi_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_veriuser_abi_tests)
add_test(NAME fsim.runtime.veriuser_abi COMMAND fsim_veriuser_abi_tests)
set_tests_properties(
  fsim.runtime.veriuser_abi
  PROPERTIES LABELS
    "fast;runtime;tf;veriuser;abi;c;cxx;positive;portability"
)

add_executable(
  fsim_acc_user_abi_tests
  acc_user_abi_test.cpp
  acc_user_abi_c_test.c
)
target_compile_features(fsim_acc_user_abi_tests PRIVATE c_std_11)
target_link_libraries(fsim_acc_user_abi_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_user_abi_tests)
add_test(NAME fsim.runtime.acc_user_abi COMMAND fsim_acc_user_abi_tests)
set_tests_properties(
  fsim.runtime.acc_user_abi
  PROPERTIES LABELS
    "fast;runtime;acc;ieee1364;abi;c;cxx;positive;portability"
)

add_executable(fsim_acc_lifecycle_tests acc_lifecycle_test.cpp)
target_link_libraries(fsim_acc_lifecycle_tests PRIVATE fsim_tf)
fsim_configure_test(fsim_acc_lifecycle_tests)
add_test(NAME fsim.runtime.acc_lifecycle COMMAND fsim_acc_lifecycle_tests)
set_tests_properties(
  fsim.runtime.acc_lifecycle
  PROPERTIES LABELS
    "fast;runtime;acc;ieee1364;lifecycle;configuration;error;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_handle_tests acc_handle_test.cpp)
target_link_libraries(fsim_acc_handle_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_handle_tests)
add_test(NAME fsim.runtime.acc_handle COMMAND fsim_acc_handle_tests)
set_tests_properties(
  fsim.runtime.acc_handle
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;handle;generation;hierarchy;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_lookup_tests acc_lookup_test.cpp)
target_link_libraries(fsim_acc_lookup_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_lookup_tests)
add_test(NAME fsim.runtime.acc_lookup COMMAND fsim_acc_lookup_tests)
set_tests_properties(
  fsim.runtime.acc_lookup
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;lookup;hierarchy;scope;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_traversal_tests acc_traversal_test.cpp)
target_link_libraries(fsim_acc_traversal_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_traversal_tests)
add_test(NAME fsim.runtime.acc_traversal COMMAND fsim_acc_traversal_tests)
set_tests_properties(
  fsim.runtime.acc_traversal
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;traversal;hierarchy;iterator;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_object_tests acc_object_test.cpp)
target_link_libraries(fsim_acc_object_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_object_tests)
add_test(NAME fsim.runtime.acc_object COMMAND fsim_acc_object_tests)
set_tests_properties(
  fsim.runtime.acc_object
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;object;connectivity;timing;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_read_tests acc_read_test.cpp)
target_link_libraries(fsim_acc_read_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_read_tests)
add_test(NAME fsim.runtime.acc_read COMMAND fsim_acc_read_tests)
set_tests_properties(
  fsim.runtime.acc_read
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;read;value;four-state;property;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_write_tests acc_write_test.cpp)
target_link_libraries(fsim_acc_write_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_write_tests)
add_test(NAME fsim.runtime.acc_write COMMAND fsim_acc_write_tests)
set_tests_properties(
  fsim.runtime.acc_write
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;write;value;four-state;delay;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_iterator_tests acc_iterator_test.cpp)
target_link_libraries(fsim_acc_iterator_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_iterator_tests)
add_test(NAME fsim.runtime.acc_iterator COMMAND fsim_acc_iterator_tests)
set_tests_properties(
  fsim.runtime.acc_iterator
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;iterator;relation;hierarchy;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_timing_tests acc_timing_test.cpp)
target_link_libraries(fsim_acc_timing_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_timing_tests)
add_test(NAME fsim.runtime.acc_timing COMMAND fsim_acc_timing_tests)
set_tests_properties(
  fsim.runtime.acc_timing
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;timing;delay;pulse;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_vcl_tests acc_vcl_test.cpp)
target_link_libraries(fsim_acc_vcl_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_vcl_tests)
add_test(NAME fsim.runtime.acc_vcl COMMAND fsim_acc_vcl_tests)
set_tests_properties(
  fsim.runtime.acc_vcl
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;callback;value-change;time;four-state;strength;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_callback_tests acc_callback_test.cpp)
target_link_libraries(fsim_acc_callback_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_callback_tests)
add_test(NAME fsim.runtime.acc_callback COMMAND fsim_acc_callback_tests)
set_tests_properties(
  fsim.runtime.acc_callback
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;callback;cancellation;ordering;reentry;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_handle_lifetime_tests acc_handle_lifetime_test.cpp)
target_link_libraries(fsim_acc_handle_lifetime_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_handle_lifetime_tests)
add_test(
  NAME fsim.runtime.acc_handle_lifetime
  COMMAND fsim_acc_handle_lifetime_tests
)
set_tests_properties(
  fsim.runtime.acc_handle_lifetime
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;handle;iterator;callback;safe-point;lifetime;generation;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_tf_coherence_tests acc_tf_coherence_test.cpp)
target_link_libraries(fsim_acc_tf_coherence_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_tf_coherence_tests)
add_test(NAME fsim.runtime.acc_tf_coherence COMMAND fsim_acc_tf_coherence_tests)
set_tests_properties(
  fsim.runtime.acc_tf_coherence
  PROPERTIES LABELS
    "fast;runtime;acc;tf;dpi;vpi;systemverilog2023;coherence;argument;scope;work-area;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_vpi_coherence_tests acc_vpi_coherence_test.cpp)
target_link_libraries(fsim_acc_vpi_coherence_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_vpi_coherence_tests)
add_test(NAME fsim.runtime.acc_vpi_coherence COMMAND fsim_acc_vpi_coherence_tests)
set_tests_properties(
  fsim.runtime.acc_vpi_coherence
  PROPERTIES LABELS
    "fast;runtime;acc;vpi;coherence;object;generation;value;hierarchy;connectivity;timing;transactional;positive;negative;resource;portability"
)

add_executable(fsim_acc_vendor_rejection_tests acc_vendor_rejection_test.cpp)
target_link_libraries(fsim_acc_vendor_rejection_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_acc_vendor_rejection_tests)
add_test(
  NAME fsim.runtime.acc_vendor_rejection
  COMMAND fsim_acc_vendor_rejection_tests
)
set_tests_properties(
  fsim.runtime.acc_vendor_rejection
  PROPERTIES LABELS
    "fast;runtime;acc;vendor;name;routine;object;behavior;diagnostic;dispatch;containment;transactional;positive;negative;resource;portability"
)

add_library(fsim_tf_link_probe_plugin SHARED tf_link_probe_plugin.c)
target_compile_features(fsim_tf_link_probe_plugin PRIVATE c_std_11)
target_include_directories(
  fsim_tf_link_probe_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fsim_tf_link_probe_plugin PRIVATE fsim_tf)
fsim_enable_warnings(fsim_tf_link_probe_plugin)
fsim_configure_debug_footprint(fsim_tf_link_probe_plugin)
add_custom_command(
  TARGET fsim_tf_link_probe_plugin
  POST_BUILD
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:fsim_tf>"
    "$<TARGET_FILE_DIR:fsim_tf_link_probe_plugin>"
)

add_library(fsim_tf_cpp_probe_plugin SHARED tf_cpp_probe_plugin.cpp)
target_compile_features(fsim_tf_cpp_probe_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_tf_cpp_probe_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fsim_tf_cpp_probe_plugin PRIVATE fsim_tf)
fsim_enable_warnings(fsim_tf_cpp_probe_plugin)
fsim_configure_debug_footprint(fsim_tf_cpp_probe_plugin)
add_custom_command(
  TARGET fsim_tf_cpp_probe_plugin
  POST_BUILD
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:fsim_tf>"
    "$<TARGET_FILE_DIR:fsim_tf_cpp_probe_plugin>"
)

add_library(fsim_acc_link_probe_plugin SHARED acc_link_probe_plugin.c)
target_compile_features(fsim_acc_link_probe_plugin PRIVATE c_std_11)
target_include_directories(
  fsim_acc_link_probe_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fsim_acc_link_probe_plugin PRIVATE fsim_tf)
fsim_enable_warnings(fsim_acc_link_probe_plugin)
fsim_configure_debug_footprint(fsim_acc_link_probe_plugin)
add_custom_command(
  TARGET fsim_acc_link_probe_plugin
  POST_BUILD
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:fsim_tf>"
    "$<TARGET_FILE_DIR:fsim_acc_link_probe_plugin>"
)

add_library(fsim_acc_cpp_probe_plugin SHARED acc_cpp_probe_plugin.cpp)
target_compile_features(fsim_acc_cpp_probe_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_acc_cpp_probe_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fsim_acc_cpp_probe_plugin PRIVATE fsim_tf)
fsim_enable_warnings(fsim_acc_cpp_probe_plugin)
fsim_configure_debug_footprint(fsim_acc_cpp_probe_plugin)
add_custom_command(
  TARGET fsim_acc_cpp_probe_plugin
  POST_BUILD
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:fsim_tf>"
    "$<TARGET_FILE_DIR:fsim_acc_cpp_probe_plugin>"
)

add_executable(fsim_tf_plugin_link_tests tf_plugin_link_test.cpp)
target_compile_definitions(
  fsim_tf_plugin_link_tests
  PRIVATE
    FSIM_TF_LINK_PROBE_PLUGIN_PATH="$<TARGET_FILE:fsim_tf_link_probe_plugin>"
    FSIM_TF_LINK_LIBRARY_PATH="$<TARGET_FILE:fsim_tf>"
)
target_link_libraries(fsim_tf_plugin_link_tests PRIVATE fsim_platform)
add_dependencies(fsim_tf_plugin_link_tests fsim_tf_link_probe_plugin)
fsim_configure_test(fsim_tf_plugin_link_tests)
add_test(NAME fsim.runtime.tf_plugin_link COMMAND fsim_tf_plugin_link_tests)
set_tests_properties(
  fsim.runtime.tf_plugin_link
  PROPERTIES LABELS
    "fast;runtime;tf;native-plugin;link;shared-library;import-library;linux;windows;positive;portability"
)

add_executable(fsim_tf_plugin_tests tf_plugin_test.cpp)
target_compile_definitions(
  fsim_tf_plugin_tests
  PRIVATE
    FSIM_TF_LINK_PROBE_PLUGIN_PATH="$<TARGET_FILE:fsim_tf_link_probe_plugin>"
    FSIM_TF_LINK_LIBRARY_PATH="$<TARGET_FILE:fsim_tf>"
)
target_link_libraries(fsim_tf_plugin_tests PRIVATE fsim_runtime)
add_dependencies(fsim_tf_plugin_tests fsim_tf_link_probe_plugin)
fsim_configure_test(fsim_tf_plugin_tests)
add_test(NAME fsim.runtime.tf_plugin COMMAND fsim_tf_plugin_tests)
set_tests_properties(
  fsim.runtime.tf_plugin
  PROPERTIES LABELS
    "fast;runtime;tf;native-plugin;registration;discovery;abi;positive;negative;resource;portability"
)

add_executable(fsim_tf_registration_tests tf_registration_test.cpp)
target_link_libraries(fsim_tf_registration_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_tf_registration_tests)
add_test(
  NAME fsim.runtime.tf_registration
  COMMAND fsim_tf_registration_tests)
set_tests_properties(
  fsim.runtime.tf_registration
  PROPERTIES LABELS
    "fast;runtime;tf;native-plugin;registration;descriptor;transaction;positive;negative;resource;portability"
)

add_executable(fsim_tf_call_tests tf_call_test.cpp)
target_compile_definitions(
  fsim_tf_call_tests
  PRIVATE
    FSIM_TF_LINK_PROBE_PLUGIN_PATH="$<TARGET_FILE:fsim_tf_link_probe_plugin>"
)
target_link_libraries(fsim_tf_call_tests PRIVATE fsim_runtime)
add_dependencies(fsim_tf_call_tests fsim_tf_link_probe_plugin)
fsim_configure_test(fsim_tf_call_tests)
add_test(NAME fsim.runtime.tf_call COMMAND fsim_tf_call_tests)
set_tests_properties(
  fsim.runtime.tf_call
  PROPERTIES LABELS
    "fast;runtime;tf;native-plugin;checktf;sizetf;calltf;result;containment;positive;negative;resource;portability"
)

add_executable(fsim_tf_misc_tests tf_misc_test.cpp)
target_compile_definitions(
  fsim_tf_misc_tests
  PRIVATE
    FSIM_TF_LINK_PROBE_PLUGIN_PATH="$<TARGET_FILE:fsim_tf_link_probe_plugin>"
)
target_link_libraries(fsim_tf_misc_tests PRIVATE fsim_runtime)
add_dependencies(fsim_tf_misc_tests fsim_tf_link_probe_plugin)
fsim_configure_test(fsim_tf_misc_tests)
add_test(NAME fsim.runtime.tf_misc COMMAND fsim_tf_misc_tests)
set_tests_properties(
  fsim.runtime.tf_misc
  PROPERTIES LABELS
    "fast;runtime;tf;native-plugin;misctf;lifecycle;synchronization;ordering;containment;positive;negative;resource;portability"
)

add_executable(fsim_tf_argument_tests tf_argument_test.cpp)
target_link_libraries(fsim_tf_argument_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_tf_argument_tests)
add_test(NAME fsim.runtime.tf_argument COMMAND fsim_tf_argument_tests)
set_tests_properties(
  fsim.runtime.tf_argument
  PROPERTIES LABELS
    "fast;runtime;tf;argument;count;type;direction;expression;width;context;positive;negative;resource;portability"
)

add_executable(fsim_tf_value_tests tf_value_test.cpp)
target_link_libraries(fsim_tf_value_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_tf_value_tests)
add_test(NAME fsim.runtime.tf_value COMMAND fsim_tf_value_tests)
set_tests_properties(
  fsim.runtime.tf_value
  PROPERTIES LABELS
    "fast;runtime;tf;value;integer;real;string;vector;expression;xz;transactional;positive;negative;resource;portability"
)

add_executable(fsim_tf_instance_tests tf_instance_test.cpp)
target_link_libraries(fsim_tf_instance_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_tf_instance_tests)
add_test(NAME fsim.runtime.tf_instance COMMAND fsim_tf_instance_tests)
set_tests_properties(
  fsim.runtime.tf_instance
  PROPERTIES LABELS
    "fast;runtime;tf;parameter;instance;generation;identity;explicit;transactional;positive;negative;resource;portability"
)

add_executable(fsim_tf_time_tests tf_time_test.cpp)
target_link_libraries(fsim_tf_time_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_tf_time_tests)
add_test(NAME fsim.runtime.tf_time COMMAND fsim_tf_time_tests)
set_tests_properties(
  fsim.runtime.tf_time
  PROPERTIES LABELS
    "fast;runtime;tf;time;delay;timescale;rounding;instance;transactional;positive;negative;resource;portability"
)

add_executable(fsim_tf_context_tests tf_context_test.cpp)
target_link_libraries(fsim_tf_context_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_tf_context_tests)
add_test(NAME fsim.runtime.tf_context COMMAND fsim_tf_context_tests)
set_tests_properties(
  fsim.runtime.tf_context
  PROPERTIES LABELS
    "fast;runtime;tf;scope;instance;work-area;user-data;lifetime;transactional;positive;negative;resource;portability"
)

add_executable(fsim_tf_control_tests tf_control_test.cpp)
target_link_libraries(fsim_tf_control_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_tf_control_tests)
add_test(NAME fsim.runtime.tf_control COMMAND fsim_tf_control_tests)
set_tests_properties(
  fsim.runtime.tf_control
  PROPERTIES LABELS
    "fast;runtime;tf;output;warning;error;message;finish;stop;format;transactional;positive;negative;resource;portability"
)

add_executable(fsim_tf_synchronization_tests tf_synchronization_test.cpp)
target_link_libraries(fsim_tf_synchronization_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_tf_synchronization_tests)
add_test(
  NAME fsim.runtime.tf_synchronization
  COMMAND fsim_tf_synchronization_tests
)
set_tests_properties(
  fsim.runtime.tf_synchronization
  PROPERTIES LABELS
    "fast;runtime;tf;synchronization;read-only;read-write;misctf;instance;ordering;transactional;positive;negative;resource;portability"
)

add_executable(fsim_tf_containment_tests tf_containment_test.cpp)
target_compile_definitions(
  fsim_tf_containment_tests
  PRIVATE
    FSIM_TF_LINK_PROBE_PLUGIN_PATH="$<TARGET_FILE:fsim_tf_link_probe_plugin>"
)
target_link_libraries(fsim_tf_containment_tests PRIVATE fsim_runtime)
add_dependencies(fsim_tf_containment_tests fsim_tf_link_probe_plugin)
fsim_configure_test(fsim_tf_containment_tests)
add_test(NAME fsim.runtime.tf_containment COMMAND fsim_tf_containment_tests)
set_tests_properties(
  fsim.runtime.tf_containment
  PROPERTIES LABELS
    "fast;runtime;tf;native-plugin;containment;pointer;exception;unload;reentry;transactional;positive;negative;resource;portability"
)

add_executable(
  fsim_tf_cross_platform_plugins_tests
  tf_cross_platform_plugins_test.cpp)
target_compile_definitions(
  fsim_tf_cross_platform_plugins_tests
  PRIVATE
    FSIM_TF_C_PLUGIN_PATH="$<TARGET_FILE:fsim_tf_link_probe_plugin>"
    FSIM_TF_CPP_PLUGIN_PATH="$<TARGET_FILE:fsim_tf_cpp_probe_plugin>"
)
target_link_libraries(fsim_tf_cross_platform_plugins_tests PRIVATE fsim_runtime)
add_dependencies(
  fsim_tf_cross_platform_plugins_tests
  fsim_tf_link_probe_plugin fsim_tf_cpp_probe_plugin)
fsim_configure_test(fsim_tf_cross_platform_plugins_tests)
add_test(
  NAME fsim.runtime.tf_cross_platform_plugins
  COMMAND fsim_tf_cross_platform_plugins_tests)
set_tests_properties(
  fsim.runtime.tf_cross_platform_plugins
  PROPERTIES LABELS
    "fast;runtime;tf;native-plugin;c;cxx;linux;windows;load;execute;unload;equivalence;positive;resource;portability"
)

add_executable(
  fsim_acc_cross_platform_plugins_tests
  acc_cross_platform_plugins_test.cpp)
target_compile_definitions(
  fsim_acc_cross_platform_plugins_tests
  PRIVATE
    FSIM_ACC_LINK_LIBRARY_PATH="$<TARGET_FILE:fsim_tf>"
    FSIM_ACC_C_PLUGIN_PATH="$<TARGET_FILE:fsim_acc_link_probe_plugin>"
    FSIM_ACC_CPP_PLUGIN_PATH="$<TARGET_FILE:fsim_acc_cpp_probe_plugin>"
)
target_link_libraries(
  fsim_acc_cross_platform_plugins_tests PRIVATE fsim_platform)
add_dependencies(
  fsim_acc_cross_platform_plugins_tests
  fsim_acc_link_probe_plugin fsim_acc_cpp_probe_plugin)
fsim_configure_test(fsim_acc_cross_platform_plugins_tests)
add_test(
  NAME fsim.runtime.acc_cross_platform_plugins
  COMMAND fsim_acc_cross_platform_plugins_tests)
set_tests_properties(
  fsim.runtime.acc_cross_platform_plugins
  PROPERTIES LABELS
    "fast;runtime;acc;tf;native-plugin;c;cxx;linux;windows;engine;artifact;cache;load;execute;unload;equivalence;positive;negative;resource;portability"
)

add_executable(fsim_trace_model_tests trace_model_tests.cpp)
target_link_libraries(fsim_trace_model_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_trace_model_tests)
add_test(NAME fsim.runtime.trace_model COMMAND fsim_trace_model_tests)
set_tests_properties(
  fsim.runtime.trace_model
  PROPERTIES LABELS "fast;runtime;trace;vcd;model;positive;negative;equivalence"
)

add_executable(
  fsim_code_coverage_model_tests code_coverage_model_test.cpp)
target_link_libraries(fsim_code_coverage_model_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_code_coverage_model_tests)
add_test(
  NAME fsim.runtime.code_coverage_model
  COMMAND fsim_code_coverage_model_tests
)
set_tests_properties(
  fsim.runtime.code_coverage_model
  PROPERTIES LABELS "fast;runtime;coverage;model;positive;negative;resource"
)

add_executable(
  fsim_coverage_aggregation_tests coverage_aggregation_test.cpp)
target_link_libraries(fsim_coverage_aggregation_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_coverage_aggregation_tests)
add_test(
  NAME fsim.runtime.coverage_aggregation
  COMMAND fsim_coverage_aggregation_tests
)
set_tests_properties(
  fsim.runtime.coverage_aggregation
  PROPERTIES LABELS "fast;runtime;coverage;aggregation;source;instance;union;determinism;saturation;positive;negative;resource"
)

add_executable(
  fsim_coverage_line_state_tests coverage_line_state_test.cpp)
target_link_libraries(fsim_coverage_line_state_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_coverage_line_state_tests)
add_test(
  NAME fsim.runtime.coverage_line_state
  COMMAND fsim_coverage_line_state_tests
)
set_tests_properties(
  fsim.runtime.coverage_line_state
  PROPERTIES LABELS "fast;runtime;coverage;line;derived;determinism;positive;negative;resource"
)

add_executable(
  fsim_coverage_condition_evaluation_tests
  coverage_condition_evaluation_test.cpp)
target_link_libraries(
  fsim_coverage_condition_evaluation_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_coverage_condition_evaluation_tests)
add_test(
  NAME fsim.runtime.coverage_condition_evaluation
  COMMAND fsim_coverage_condition_evaluation_tests
)
set_tests_properties(
  fsim.runtime.coverage_condition_evaluation
  PROPERTIES LABELS "fast;runtime;coverage;condition;short-circuit;verilog;systemverilog;vhdl;positive;negative;resource"
)

add_executable(
  fsim_coverage_condition_outcomes_tests
  coverage_condition_outcomes_test.cpp)
target_link_libraries(
  fsim_coverage_condition_outcomes_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_coverage_condition_outcomes_tests)
add_test(
  NAME fsim.runtime.coverage_condition_outcomes
  COMMAND fsim_coverage_condition_outcomes_tests
)
set_tests_properties(
  fsim.runtime.coverage_condition_outcomes
  PROPERTIES LABELS "fast;runtime;coverage;condition;outcomes;four-state;scoring;saturation;positive;negative;resource"
)

add_executable(
  fsim_coverage_expression_tests coverage_expression_test.cpp)
target_link_libraries(fsim_coverage_expression_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_coverage_expression_tests)
add_test(
  NAME fsim.runtime.coverage_expression
  COMMAND fsim_coverage_expression_tests
)
set_tests_properties(
  fsim.runtime.coverage_expression
  PROPERTIES LABELS "fast;runtime;coverage;condition;expression;combination;omission;determinism;positive;negative;resource"
)

add_executable(fsim_coverage_toggle_tests coverage_toggle_test.cpp)
target_link_libraries(fsim_coverage_toggle_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_coverage_toggle_tests)
add_test(
  NAME fsim.runtime.coverage_toggle
  COMMAND fsim_coverage_toggle_tests
)
set_tests_properties(
  fsim.runtime.coverage_toggle
  PROPERTIES LABELS "fast;runtime;coverage;toggle;binary;direction;identity;saturation;positive;negative;resource"
)

add_executable(fsim_coverage_fsm_tests coverage_fsm_test.cpp)
target_link_libraries(fsim_coverage_fsm_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_coverage_fsm_tests)
add_test(
  NAME fsim.runtime.coverage_fsm
  COMMAND fsim_coverage_fsm_tests
)
set_tests_properties(
  fsim.runtime.coverage_fsm
  PROPERTIES LABELS "fast;runtime;coverage;fsm;state;transition;identity;saturation;positive;negative;resource"
)

add_executable(
  fsim_simir_coverage_tests simir_coverage_test.cpp)
target_link_libraries(fsim_simir_coverage_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_simir_coverage_tests)
add_test(
  NAME fsim.runtime.simir_coverage
  COMMAND fsim_simir_coverage_tests
)
set_tests_properties(
  fsim.runtime.simir_coverage
  PROPERTIES LABELS "fast;runtime;coverage;simir;ownership;sharing;positive;negative;resource"
)

add_executable(fsim_fst_writer_tests fst_writer_tests.cpp)
target_link_libraries(fsim_fst_writer_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_fst_writer_tests)
add_test(NAME fsim.runtime.fst_writer COMMAND fsim_fst_writer_tests)
set_tests_properties(
  fsim.runtime.fst_writer
  PROPERTIES LABELS "fast;runtime;fst;writer;hierarchy;positive;negative;determinism"
)

add_executable(fsim_fst_compression_tests fst_compression_tests.cpp)
target_link_libraries(fsim_fst_compression_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_fst_compression_tests)
add_test(NAME fsim.runtime.fst_compression COMMAND fsim_fst_compression_tests)
set_tests_properties(
  fsim.runtime.fst_compression
  PROPERTIES LABELS "fast;runtime;fst;compression;positive;negative;determinism;portability"
)

add_executable(fsim_fst_value_encoder_tests fst_value_encoder_tests.cpp)
target_link_libraries(fsim_fst_value_encoder_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_fst_value_encoder_tests)
add_test(
  NAME fsim.runtime.fst_value_encoder
  COMMAND fsim_fst_value_encoder_tests
)
set_tests_properties(
  fsim.runtime.fst_value_encoder
  PROPERTIES LABELS "fast;runtime;fst;value;positive;negative;equivalence;resource"
)

add_executable(fsim_fst_change_encoder_tests fst_change_encoder_tests.cpp)
target_link_libraries(fsim_fst_change_encoder_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_fst_change_encoder_tests)
add_test(
  NAME fsim.runtime.fst_change_encoder
  COMMAND fsim_fst_change_encoder_tests
)
set_tests_properties(
  fsim.runtime.fst_change_encoder
  PROPERTIES LABELS "fast;runtime;fst;change;positive;negative;determinism;resource"
)

add_executable(fsim_fst_reader_tests fst_reader_tests.cpp)
target_link_libraries(fsim_fst_reader_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_fst_reader_tests)
add_test(NAME fsim.runtime.fst_reader COMMAND fsim_fst_reader_tests)
set_tests_properties(
  fsim.runtime.fst_reader
  PROPERTIES
    FIXTURES_SETUP "fsim_fst_closure_witnesses"
    LABELS
    "fast;runtime;fst;reader;value;positive;negative;equivalence;determinism;compression;metadata;alias;lifecycle;resource"
)

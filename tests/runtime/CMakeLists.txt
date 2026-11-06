# SPDX-License-Identifier: Apache-2.0

# This directory is intentionally usable from a root CMakeLists.txt with:
#   add_subdirectory(tests/runtime)
# It expects the runtime target to be named fsim_runtime.
add_executable(
  fsim_runtime_tests
  runtime_tests.cpp
  runtime_value_tests.cpp
  runtime_sensitivity_range_tests.cpp
  runtime_region_graph_tests.cpp
  runtime_process_storage_tests.cpp
  runtime_executor_provenance_tests.cpp
  runtime_scheduler_trace_tests.cpp
  runtime_scheduler_sv_tests.cpp
  runtime_private_region_scheduler_tests.cpp
  runtime_value_primitives_tests.cpp
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
  runtime_vpi_observation_preparation_tests.cpp
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
  runtime_package_static_startup_tests.cpp
  runtime_execution_services_tests.cpp
  runtime_execution_lifecycle.cpp
  runtime_synchronization_tests.cpp
  runtime_file_tests.cpp
  runtime_container_tests.cpp
  runtime_element_alias_observation_tests.cpp
  runtime_aggregate_proxy_native_tests.cpp
  runtime_aggregate_proxy_driver_family_tests.cpp
  runtime_aggregate_proxy_driver_registration_tests.cpp
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
  runtime_run_state_image_tests.cpp
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
target_compile_definitions(
  fsim_runtime_tests
  PRIVATE FSIM_DPI_ONLY_PLUGIN_PATH="$<TARGET_FILE:fsim_foreign_dpi_types_plugin>"
          FSIM_VHPI_TEST_PLUGIN_PATH="$<TARGET_FILE:fsim_vhpi_test_plugin>"
          FSIM_VHPI_REFERENCE_C_PLUGIN_PATH="$<TARGET_FILE:fsim_vhpi_reference_c_plugin>"
          FSIM_VHPI_REFERENCE_CPP_PLUGIN_PATH="$<TARGET_FILE:fsim_vhpi_reference_cpp_plugin>"
          FSIM_VPI_TEST_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_test_plugin>"
          FSIM_VPI_STANDARD_STARTUP_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_standard_startup_plugin>"
          FSIM_VPI_REFERENCE_C_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_reference_c_plugin>"
          FSIM_VPI_REFERENCE_CPP_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_reference_cpp_plugin>"
)
add_dependencies(
  fsim_runtime_tests fsim_foreign_dpi_types_plugin
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
add_test(NAME fsim.runtime.text_files COMMAND fsim_runtime_tests text_files)
set_tests_properties(
  fsim.runtime.text_files
  PROPERTIES LABELS "fast;runtime;files;systemverilog;vhdl;windows;linux")
add_test(
  NAME fsim.runtime.package_static_startup
  COMMAND fsim_runtime_tests package_static_startup)
set_tests_properties(
  fsim.runtime.package_static_startup
  PROPERTIES LABELS "fast;runtime;simir;startup;windows;linux")
add_executable(
  fsim_runtime_packed_inline_tests
  runtime_packed_inline_tests.cpp
  runtime_fused_staging_failure_support.cpp)
target_link_libraries(fsim_runtime_packed_inline_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_packed_inline_tests)
add_test(NAME fsim.runtime.packed_inline COMMAND fsim_runtime_packed_inline_tests)
set_tests_properties(
  fsim.runtime.packed_inline
  PROPERTIES LABELS "fast;runtime;wide;logic9;allocation;failure;windows;linux")
add_test(
  NAME fsim.runtime.executor_provenance_contract
  COMMAND fsim_runtime_tests executor_provenance_contract)
set_tests_properties(
  fsim.runtime.executor_provenance_contract
  PROPERTIES LABELS "fast;runtime;provenance;callbacks;negative;windows;linux")
add_test(
  NAME fsim.runtime.aggregate_proxy_sampled_data
  COMMAND
    fsim_runtime_tests aggregate_proxy_sampled_data_matches_packed_reference
)
set_tests_properties(
  fsim.runtime.aggregate_proxy_sampled_data
  PROPERTIES
    LABELS "fast;runtime;sampled;aggregate-alias;systemverilog;windows;linux"
)
add_test(
  NAME fsim.runtime.aggregate_proxy_leaf_write_differential
  COMMAND fsim_runtime_tests aggregate_proxy_leaf_write_differential)
set_tests_properties(
  fsim.runtime.aggregate_proxy_leaf_write_differential
  PROPERTIES LABELS "fast;runtime;transactions;aggregate-alias;systemverilog;windows;linux")
add_test(
  NAME fsim.runtime.aggregate_proxy_driver_family
  COMMAND fsim_runtime_tests aggregate_proxy_driver_family)
set_tests_properties(
  fsim.runtime.aggregate_proxy_driver_family
  PROPERTIES LABELS "fast;runtime;systemverilog;drivers;aggregate-alias;windows;linux")
add_executable(
  fsim_runtime_aggregate_proxy_driver_family_failure_tests
  runtime_aggregate_proxy_driver_family_failure_tests.cpp
  runtime_fused_staging_failure_support.cpp)
target_link_libraries(
  fsim_runtime_aggregate_proxy_driver_family_failure_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_aggregate_proxy_driver_family_failure_tests)
add_test(
  NAME fsim.runtime.aggregate_proxy_driver_family_failure
  COMMAND fsim_runtime_aggregate_proxy_driver_family_failure_tests)
set_tests_properties(
  fsim.runtime.aggregate_proxy_driver_family_failure
  PROPERTIES
    LABELS "fast;runtime;allocation;failure;drivers;aggregate-alias;windows;linux")
add_test(
  NAME fsim.runtime.aggregate_proxy_driver_region_registration
  COMMAND fsim_runtime_tests aggregate_proxy_driver_region_registration)
set_tests_properties(
  fsim.runtime.aggregate_proxy_driver_region_registration
  PROPERTIES LABELS "fast;runtime;systemverilog;drivers;aggregate-alias;windows;linux")
add_executable(
  fsim_runtime_aggregate_alias_allocation_failure_tests
  runtime_aggregate_alias_allocation_failure_tests.cpp
  runtime_fused_staging_failure_support.cpp)
target_link_libraries(
  fsim_runtime_aggregate_alias_allocation_failure_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_aggregate_alias_allocation_failure_tests)
add_test(
  NAME fsim.runtime.aggregate_alias_allocation_failure
  COMMAND fsim_runtime_aggregate_alias_allocation_failure_tests)
set_tests_properties(
  fsim.runtime.aggregate_alias_allocation_failure
  PROPERTIES
    LABELS "fast;runtime;allocation;failure;interpreter;windows;linux")
add_executable(
  fsim_runtime_aggregate_alias_force_failure_tests
  runtime_aggregate_alias_force_failure_tests.cpp
  runtime_fused_staging_failure_support.cpp)
target_link_libraries(
  fsim_runtime_aggregate_alias_force_failure_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_aggregate_alias_force_failure_tests)
add_test(
  NAME fsim.runtime.aggregate_alias_force_failure
  COMMAND fsim_runtime_aggregate_alias_force_failure_tests)
set_tests_properties(
  fsim.runtime.aggregate_alias_force_failure
  PROPERTIES
    LABELS "fast;runtime;allocation;failure;interpreter;force;aggregate-alias;windows;linux")
add_executable(
  fsim_runtime_callback_observation_failure_tests
  runtime_callback_observation_failure_tests.cpp
  runtime_fused_staging_failure_support.cpp)
target_link_libraries(
  fsim_runtime_callback_observation_failure_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_callback_observation_failure_tests)
add_test(
  NAME fsim.runtime.callback_observation_failure
  COMMAND fsim_runtime_callback_observation_failure_tests)
set_tests_properties(
  fsim.runtime.callback_observation_failure
  PROPERTIES LABELS "fast;runtime;allocation;failure;callbacks;windows;linux")
add_executable(
  fsim_runtime_systemverilog_publication_pool_tests
  runtime_systemverilog_publication_pool_tests.cpp
  runtime_fused_staging_failure_support.cpp)
target_link_libraries(
  fsim_runtime_systemverilog_publication_pool_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_systemverilog_publication_pool_tests)
add_test(
  NAME fsim.runtime.systemverilog_publication_pool
  COMMAND fsim_runtime_systemverilog_publication_pool_tests)
set_tests_properties(
  fsim.runtime.systemverilog_publication_pool
  PROPERTIES
    LABELS
      "fast;runtime;systemverilog;allocation;failure;interpreter;windows;linux")
add_executable(
  fsim_runtime_systemverilog_publication_allocation_tests
  runtime_systemverilog_publication_allocation_tests.cpp
  runtime_fused_staging_failure_support.cpp)
target_link_libraries(
  fsim_runtime_systemverilog_publication_allocation_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_systemverilog_publication_allocation_tests)
add_test(
  NAME fsim.runtime.systemverilog_publication_allocation
  COMMAND fsim_runtime_systemverilog_publication_allocation_tests)
set_tests_properties(
  fsim.runtime.systemverilog_publication_allocation
  PROPERTIES LABELS "fast;runtime;systemverilog;allocation;interpreter;windows;linux")
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

add_library(fsim_tf_cpp_probe_plugin SHARED tf_cpp_probe_plugin.cpp)
target_compile_features(fsim_tf_cpp_probe_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_tf_cpp_probe_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fsim_tf_cpp_probe_plugin PRIVATE fsim_tf)
fsim_enable_warnings(fsim_tf_cpp_probe_plugin)
fsim_configure_debug_footprint(fsim_tf_cpp_probe_plugin)

add_library(fsim_acc_link_probe_plugin SHARED acc_link_probe_plugin.c)
target_compile_features(fsim_acc_link_probe_plugin PRIVATE c_std_11)
target_include_directories(
  fsim_acc_link_probe_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fsim_acc_link_probe_plugin PRIVATE fsim_tf)
fsim_enable_warnings(fsim_acc_link_probe_plugin)
fsim_configure_debug_footprint(fsim_acc_link_probe_plugin)

add_library(fsim_acc_cpp_probe_plugin SHARED acc_cpp_probe_plugin.cpp)
target_compile_features(fsim_acc_cpp_probe_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_acc_cpp_probe_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fsim_acc_cpp_probe_plugin PRIVATE fsim_tf)
fsim_enable_warnings(fsim_acc_cpp_probe_plugin)
fsim_configure_debug_footprint(fsim_acc_cpp_probe_plugin)

# The tests and plug-ins here load libfsim_tf from beside themselves.
fsim_copy_library_beside(fsim_tf fsim_tf_link_probe_plugin
  fsim_tf_cpp_probe_plugin fsim_acc_link_probe_plugin
  fsim_acc_cpp_probe_plugin fsim_acc_lifecycle_tests
  fsim_vpi_standard_startup_plugin)

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

add_executable(
  fsim_runtime_constant_driver_bank_tests
  runtime_constant_driver_bank_tests.cpp)
target_link_libraries(
  fsim_runtime_constant_driver_bank_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_constant_driver_bank_tests)
add_test(
  NAME fsim.runtime.constant_driver_startup_bank
  COMMAND fsim_runtime_constant_driver_bank_tests)
set_tests_properties(
  fsim.runtime.constant_driver_startup_bank
  PROPERTIES
    LABELS
    "fast;runtime;systemverilog;constant-driver;startup;positive;negative;windows;linux")

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

add_executable(
  fsim_runtime_kernel_word_ops_tests
  runtime_kernel_word_ops_tests.cpp)
target_link_libraries(
  fsim_runtime_kernel_word_ops_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_kernel_word_ops_tests)
add_test(
  NAME fsim.runtime.kernel_word_ops
  COMMAND fsim_runtime_kernel_word_ops_tests)
set_tests_properties(
  fsim.runtime.kernel_word_ops
  PROPERTIES LABELS "fast;runtime;static-kernel;linux;windows")
add_executable(
  fsim_runtime_region_graph_read_range_tests
  runtime_region_graph_read_range_tests.cpp)
target_link_libraries(
  fsim_runtime_region_graph_read_range_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_region_graph_read_range_tests)
add_test(
  NAME fsim.runtime.region_graph_read_ranges
  COMMAND fsim_runtime_region_graph_read_range_tests)
set_tests_properties(
  fsim.runtime.region_graph_read_ranges
  PROPERTIES LABELS "fast;runtime;region-graph;range;linux;windows")
# VPI and VHPI runtime conformance plug-ins, run through the command line on
# small designs; each trace is checked against its expected file.
foreach(FSIM_FOREIGN_INTERFACE IN ITEMS vpi vhpi)
  add_library(
    fsim_foreign_${FSIM_FOREIGN_INTERFACE}_runtime_plugin SHARED
    foreign_${FSIM_FOREIGN_INTERFACE}_runtime_plugin.c)
  fsim_configure_library(fsim_foreign_${FSIM_FOREIGN_INTERFACE}_runtime_plugin)
  target_link_libraries(
    fsim_foreign_${FSIM_FOREIGN_INTERFACE}_runtime_plugin PRIVATE fsim_tf)
  target_include_directories(
    fsim_foreign_${FSIM_FOREIGN_INTERFACE}_runtime_plugin
    PRIVATE "${PROJECT_SOURCE_DIR}/include")
endforeach()
# The VHPI information-model plug-in (IEEE 1076-2019 19, 22.2, 23.9), the
# VPI data-model plug-in (IEEE 1800-2023 37.16-37.18, 37.26) and the
# mixed-language hierarchy plug-in, which is both.
foreach(FSIM_FOREIGN_INTERFACE IN ITEMS vpi vhpi mixed)
  add_library(
    fsim_foreign_${FSIM_FOREIGN_INTERFACE}_model_plugin SHARED
    foreign_${FSIM_FOREIGN_INTERFACE}_model_plugin.c)
  fsim_configure_library(fsim_foreign_${FSIM_FOREIGN_INTERFACE}_model_plugin)
  target_link_libraries(
    fsim_foreign_${FSIM_FOREIGN_INTERFACE}_model_plugin PRIVATE fsim_tf)
  target_include_directories(
    fsim_foreign_${FSIM_FOREIGN_INTERFACE}_model_plugin
    PRIVATE "${PROJECT_SOURCE_DIR}/include")
endforeach()
set(FSIM_FOREIGN_FIXTURES "${PROJECT_SOURCE_DIR}/tests/fixtures/foreign")
# An optional sixth argument names the plug-in target.
function(fsim_add_foreign_conformance_test FSIM_NAME FSIM_INTERFACE
    FSIM_SOURCE FSIM_EXPECTED FSIM_SIMULATE_ARGS)
  if(ARGC GREATER 5 AND NOT "${ARGV5}" STREQUAL "")
    set(FSIM_PLUGIN_TARGET "${ARGV5}")
  else()
    set(FSIM_PLUGIN_TARGET "fsim_foreign_${FSIM_INTERFACE}_runtime_plugin")
  endif()
  # An optional seventh argument is the exit status simulate must return;
  # a true eighth one loads the plug-in at elaboration too.
  set(FSIM_SIMULATE_STATUS 0)
  if(ARGC GREATER 6 AND NOT "${ARGV6}" STREQUAL "")
    set(FSIM_SIMULATE_STATUS "${ARGV6}")
  endif()
  set(FSIM_ELABORATE_PLUGIN OFF)
  if(ARGC GREATER 7)
    set(FSIM_ELABORATE_PLUGIN "${ARGV7}")
  endif()
  # Several sources are separated by "|".
  string(REPLACE "|" "|${FSIM_FOREIGN_FIXTURES}/" FSIM_SOURCE_PATHS
    "${FSIM_FOREIGN_FIXTURES}/${FSIM_SOURCE}")
  # A ninth argument "v4" runs the case on the compiled kernel only.
  set(FSIM_ENGINES v4 interpreter)
  if(ARGC GREATER 8 AND "${ARGV8}" STREQUAL "v4")
    set(FSIM_ENGINES v4)
  endif()
  # After those, MESSAGE gives text `fsim simulate` must report and SDF names
  # an SDF file annotated at elaboration; ELABORATE_MESSAGE is text a failing
  # `fsim elaborate` must report, DPI_HEADER the header `fsim compile
  # --dpi-header` must write and ELABORATE_ARGS more `fsim elaborate`
  # arguments. DEBUG_COMMANDS names a Tcl script that `fsim debug`
  # evaluates in place of `fsim simulate`; the case then runs on the debug
  # engine only.
  cmake_parse_arguments(PARSE_ARGV 9 FSIM_OPTION ""
    "MESSAGE;SDF;ELABORATE_MESSAGE;DPI_HEADER;ELABORATE_ARGS;DEBUG_COMMANDS" "")
  set(FSIM_DEBUG_COMMANDS_PATH "")
  if(NOT "${FSIM_OPTION_DEBUG_COMMANDS}" STREQUAL "")
    set(FSIM_DEBUG_COMMANDS_PATH
      "${FSIM_FOREIGN_FIXTURES}/${FSIM_OPTION_DEBUG_COMMANDS}")
    set(FSIM_ENGINES debug)
  endif()
  set(FSIM_SIMULATE_MESSAGE "${FSIM_OPTION_MESSAGE}")
  set(FSIM_SDF_PATH "")
  if(NOT "${FSIM_OPTION_SDF}" STREQUAL "")
    set(FSIM_SDF_PATH "${FSIM_FOREIGN_FIXTURES}/${FSIM_OPTION_SDF}")
  endif()
  set(FSIM_DPI_HEADER_PATH "")
  if(NOT "${FSIM_OPTION_DPI_HEADER}" STREQUAL "")
    set(FSIM_DPI_HEADER_PATH
      "${FSIM_FOREIGN_FIXTURES}/${FSIM_OPTION_DPI_HEADER}")
  endif()
  foreach(FSIM_ENGINE IN LISTS FSIM_ENGINES)
    add_test(
      NAME fsim.foreign.${FSIM_NAME}.${FSIM_ENGINE}
      COMMAND
        "${CMAKE_COMMAND}"
        "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
        "-DFSIM_PLUGIN=$<TARGET_FILE:${FSIM_PLUGIN_TARGET}>"
        "-DFSIM_INTERFACE=${FSIM_INTERFACE}"
        "-DFSIM_SOURCE=${FSIM_SOURCE_PATHS}"
        "-DFSIM_TOP=top"
        "-DFSIM_ENGINE=${FSIM_ENGINE}"
        "-DFSIM_EXPECTED=${FSIM_FOREIGN_FIXTURES}/${FSIM_EXPECTED}"
        "-DFSIM_SIMULATE_ARGS=${FSIM_SIMULATE_ARGS}"
        "-DFSIM_SIMULATE_STATUS=${FSIM_SIMULATE_STATUS}"
        "-DFSIM_SIMULATE_MESSAGE=${FSIM_SIMULATE_MESSAGE}"
        "-DFSIM_ELABORATE_PLUGIN=${FSIM_ELABORATE_PLUGIN}"
        "-DFSIM_SDF=${FSIM_SDF_PATH}"
        "-DFSIM_ELABORATE_MESSAGE=${FSIM_OPTION_ELABORATE_MESSAGE}"
        "-DFSIM_DPI_HEADER=${FSIM_DPI_HEADER_PATH}"
        "-DFSIM_ELABORATE_ARGS=${FSIM_OPTION_ELABORATE_ARGS}"
        "-DFSIM_DEBUG_COMMANDS=${FSIM_DEBUG_COMMANDS_PATH}"
        "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/foreign/${FSIM_NAME}-${FSIM_ENGINE}"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunForeignConformanceCase.cmake")
    set_tests_properties(
      fsim.foreign.${FSIM_NAME}.${FSIM_ENGINE}
      PROPERTIES LABELS "fast;foreign;${FSIM_INTERFACE};conformance;linux")
  endforeach()
endfunction()
fsim_add_foreign_conformance_test(
  vpi-runtime vpi foreign_vpi_runtime.sv foreign_vpi_runtime.expected "")
fsim_add_foreign_conformance_test(
  vpi-runtime-duration vpi foreign_vpi_runtime.sv
  foreign_vpi_runtime_duration.expected "--duration 30")
fsim_add_foreign_conformance_test(
  vhpi-runtime vhpi foreign_vhpi_runtime.vhd foreign_vhpi_runtime.expected "")
fsim_add_foreign_conformance_test(
  vhpi-model vhpi foreign_vhpi_model.vhd foreign_vhpi_model.expected ""
  fsim_foreign_vhpi_model_plugin)
fsim_add_foreign_conformance_test(
  vpi-model vpi foreign_vpi_model.sv foreign_vpi_model.expected ""
  fsim_foreign_vpi_model_plugin)
# Gates, switches, UDPs and their prim terms (IEEE 1800-2023 37.35, 37.36).
add_library(fsim_foreign_vpi_primitives_plugin SHARED
  foreign_vpi_primitives_plugin.c)
fsim_configure_library(fsim_foreign_vpi_primitives_plugin)
target_link_libraries(fsim_foreign_vpi_primitives_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_primitives_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-primitives vpi foreign_vpi_primitives.sv foreign_vpi_primitives.expected
  "" fsim_foreign_vpi_primitives_plugin)
# Delays of gates, a UDP instance, continuous assignments, a path and a timing
# check through vpi_get_delays and vpi_put_delays (IEEE 1800-2023 38.10, 38.32).
add_library(fsim_foreign_vpi_delays_plugin SHARED
  foreign_vpi_delays_plugin.c)
fsim_configure_library(fsim_foreign_vpi_delays_plugin)
target_link_libraries(fsim_foreign_vpi_delays_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_delays_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-delays vpi foreign_vpi_delays.sv foreign_vpi_delays.expected ""
  fsim_foreign_vpi_delays_plugin)
# Intermodule paths, delay devices and delay terms that SDF annotation
# creates (IEEE 1800-2023 37.37, 37.45, 38.22).
add_library(fsim_foreign_vpi_sdf_delays_plugin SHARED
  foreign_vpi_sdf_delays_plugin.c)
fsim_configure_library(fsim_foreign_vpi_sdf_delays_plugin)
target_link_libraries(fsim_foreign_vpi_sdf_delays_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_sdf_delays_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-sdf-delays vpi foreign_vpi_sdf_delays.sv foreign_vpi_sdf_delays.expected
  "" fsim_foreign_vpi_sdf_delays_plugin "" OFF ""
  SDF foreign_vpi_sdf_delays.sdf)
# Clocking blocks and nettype declarations (IEEE 1800-2023 37.23, 37.48).
add_library(fsim_foreign_vpi_clocking_plugin SHARED
  foreign_vpi_clocking_plugin.c)
fsim_configure_library(fsim_foreign_vpi_clocking_plugin)
target_link_libraries(fsim_foreign_vpi_clocking_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_clocking_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-clocking vpi foreign_vpi_clocking.sv foreign_vpi_clocking.expected ""
  fsim_foreign_vpi_clocking_plugin)
# Class definitions, class typespecs and class variables (IEEE 1800-2023
# 37.31, 37.32, 37.33).
add_library(fsim_foreign_vpi_classes_plugin SHARED
  foreign_vpi_classes_plugin.c)
fsim_configure_library(fsim_foreign_vpi_classes_plugin)
target_link_libraries(fsim_foreign_vpi_classes_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_classes_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-classes vpi foreign_vpi_classes.sv foreign_vpi_classes.expected ""
  fsim_foreign_vpi_classes_plugin)
# Virtual interface variables and interface typespecs (IEEE 1800-2023
# 37.29, 37.30).
add_library(fsim_foreign_vpi_virtual_interfaces_plugin SHARED
  foreign_vpi_virtual_interfaces_plugin.c)
fsim_configure_library(fsim_foreign_vpi_virtual_interfaces_plugin)
target_link_libraries(fsim_foreign_vpi_virtual_interfaces_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_virtual_interfaces_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-virtual-interfaces vpi foreign_vpi_virtual_interfaces.sv
  foreign_vpi_virtual_interfaces.expected ""
  fsim_foreign_vpi_virtual_interfaces_plugin)
# An array of virtual interfaces (IEEE 1800-2023 37.29 example 1).
add_library(fsim_foreign_vpi_virtual_array_plugin SHARED
  foreign_vpi_virtual_array_plugin.c)
fsim_configure_library(fsim_foreign_vpi_virtual_array_plugin)
target_link_libraries(fsim_foreign_vpi_virtual_array_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_virtual_array_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-virtual-array vpi foreign_vpi_virtual_array.sv
  foreign_vpi_virtual_array.expected ""
  fsim_foreign_vpi_virtual_array_plugin)
# Live class objects, constraints, size changes and reclamation (IEEE
# 1800-2023 37.33, 37.34, 37.38, 38.36.1).
add_library(fsim_foreign_vpi_class_objects_plugin SHARED
  foreign_vpi_class_objects_plugin.c)
fsim_configure_library(fsim_foreign_vpi_class_objects_plugin)
target_link_libraries(fsim_foreign_vpi_class_objects_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_class_objects_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-class-objects vpi foreign_vpi_class_objects.sv
  foreign_vpi_class_objects.expected "" fsim_foreign_vpi_class_objects_plugin)
# DPI time and scope service (IEEE 1800-2023 35.5.3, H.9.3, H.13).
add_library(fsim_foreign_dpi_time_scope_plugin SHARED
  foreign_dpi_time_scope_plugin.c)
fsim_configure_library(fsim_foreign_dpi_time_scope_plugin)
target_link_libraries(fsim_foreign_dpi_time_scope_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_time_scope_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-time-scope dpi foreign_dpi_time_scope.sv foreign_dpi_time_scope.expected
  "" fsim_foreign_dpi_time_scope_plugin)
# DPI export dispatch (IEEE 1800-2023 35.5.3, 35.7, 35.8); a noncontext
# import calling an export ends the simulation with an error.
add_library(fsim_foreign_dpi_scope_export_plugin SHARED
  foreign_dpi_scope_export_plugin.c)
fsim_configure_library(fsim_foreign_dpi_scope_export_plugin)
target_link_libraries(fsim_foreign_dpi_scope_export_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_scope_export_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-scope-export dpi foreign_dpi_scope_export.sv
  foreign_dpi_scope_export.expected "" fsim_foreign_dpi_scope_export_plugin 1)
# DPI export dispatch by scope (IEEE 1800-2023 35.5.3, 35.7): exports of
# other instances and of a package after svSetScope; a scope without the
# export ends the simulation with an error.
add_library(fsim_foreign_dpi_scope_dispatch_plugin SHARED
  foreign_dpi_scope_dispatch_plugin.c)
fsim_configure_library(fsim_foreign_dpi_scope_dispatch_plugin)
target_link_libraries(fsim_foreign_dpi_scope_dispatch_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_scope_dispatch_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-scope-dispatch dpi foreign_dpi_scope_dispatch.sv
  foreign_dpi_scope_dispatch.expected "" fsim_foreign_dpi_scope_dispatch_plugin 1)
# The DPI disable protocol (IEEE 1800-2023 35.9), on the compiled kernel.
add_library(fsim_foreign_dpi_disable_plugin SHARED
  foreign_dpi_disable_plugin.c)
fsim_configure_library(fsim_foreign_dpi_disable_plugin)
target_link_libraries(fsim_foreign_dpi_disable_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_disable_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-disable dpi foreign_dpi_disable.sv foreign_dpi_disable.expected ""
  fsim_foreign_dpi_disable_plugin "" OFF v4)
# The DPI disable protocol when the export is the disable target (IEEE
# 1800-2023 35.9), on the compiled kernel.
add_library(fsim_foreign_dpi_disable_export_plugin SHARED
  foreign_dpi_disable_export_plugin.c)
fsim_configure_library(fsim_foreign_dpi_disable_export_plugin)
target_link_libraries(fsim_foreign_dpi_disable_export_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_disable_export_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-disable-export dpi foreign_dpi_disable_export.sv
  foreign_dpi_disable_export.expected "" fsim_foreign_dpi_disable_export_plugin
  "" OFF v4)
# DPI exports of the compilation unit (IEEE 1800-2023 35.5.2, 35.7), on the
# compiled kernel.
add_library(fsim_foreign_dpi_unit_export_plugin SHARED
  foreign_dpi_unit_export_plugin.c)
fsim_configure_library(fsim_foreign_dpi_unit_export_plugin)
target_link_libraries(fsim_foreign_dpi_unit_export_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_unit_export_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-unit-export dpi foreign_dpi_unit_export.sv
  foreign_dpi_unit_export.expected "" fsim_foreign_dpi_unit_export_plugin
  "" OFF v4)
# DPI exports declared in generate blocks (IEEE 1800-2023 35.5.3, 35.7), on
# the compiled kernel.
add_library(fsim_foreign_dpi_generate_export_plugin SHARED
  foreign_dpi_generate_export_plugin.c)
fsim_configure_library(fsim_foreign_dpi_generate_export_plugin)
target_link_libraries(fsim_foreign_dpi_generate_export_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_generate_export_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-generate-export dpi foreign_dpi_generate_export.sv
  foreign_dpi_generate_export.expected ""
  fsim_foreign_dpi_generate_export_plugin "" OFF v4)
# DPI argument types (IEEE 1800-2023 35.5.6, 35.5.6.1, H.7.6, H.12):
# constant-expression ranges, open packed formals, sized unpacked arrays,
# and real, shortreal, chandle and enumeration arrays.
add_library(fsim_foreign_dpi_types_plugin SHARED foreign_dpi_types_plugin.c)
fsim_configure_library(fsim_foreign_dpi_types_plugin)
target_link_libraries(fsim_foreign_dpi_types_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_types_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-types dpi foreign_dpi_types.sv foreign_dpi_types.expected ""
  fsim_foreign_dpi_types_plugin)
# DPI scalar, string and packed vector arguments (IEEE 1800-2023 35.5.5,
# H.7.4, H.10.1): the types a compiled run marshals straight from the
# registers into the C argument buffer, in every direction.
add_library(fsim_foreign_dpi_scalars_plugin SHARED
  foreign_dpi_scalars_plugin.c)
fsim_configure_library(fsim_foreign_dpi_scalars_plugin)
target_link_libraries(fsim_foreign_dpi_scalars_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_scalars_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-scalars dpi foreign_dpi_scalars.sv foreign_dpi_scalars.expected ""
  fsim_foreign_dpi_scalars_plugin)
# DPI unpacked aggregates (IEEE 1800-2023 35.5.6, 35.5.6.1, H.7.8):
# structures, arrays of them, output and inout open arrays with dynamic and
# queue actuals, and exports with sized and structure formals.
add_library(fsim_foreign_dpi_aggregates_plugin SHARED
  foreign_dpi_aggregates_plugin.c)
fsim_configure_library(fsim_foreign_dpi_aggregates_plugin)
target_link_libraries(fsim_foreign_dpi_aggregates_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_aggregates_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-aggregates dpi foreign_dpi_aggregates.sv foreign_dpi_aggregates.expected
  "" fsim_foreign_dpi_aggregates_plugin)
# DPI arrays of strings (IEEE 1800-2023 35.5.6, 35.5.6.1, H.7.4): sized and
# open arrays in input, output and inout directions.
add_library(fsim_foreign_dpi_strings_plugin SHARED
  foreign_dpi_strings_plugin.c)
fsim_configure_library(fsim_foreign_dpi_strings_plugin)
target_link_libraries(fsim_foreign_dpi_strings_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_strings_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-strings dpi foreign_dpi_strings.sv foreign_dpi_strings.expected
  "" fsim_foreign_dpi_strings_plugin)
# A DPI function result is a small value, never a packed vector, even of
# one bit (IEEE 1800-2023 35.5.5).
fsim_add_foreign_conformance_test(
  dpi-result-vector dpi foreign_dpi_result_vector.sv
  foreign_dpi_result_vector.expected "" fsim_foreign_dpi_types_plugin ""
  OFF v4 ELABORATE_MESSAGE
  "the DPI function result type 'bit [ 7 : 0 ]' is not a small value")
fsim_add_foreign_conformance_test(
  dpi-result-bit1 dpi foreign_dpi_result_vector.sv
  foreign_dpi_result_vector.expected "" fsim_foreign_dpi_types_plugin ""
  OFF v4 ELABORATE_MESSAGE
  "the DPI function result type 'bit [ 0 : 0 ]' is not a small value")
# `fsim compile --dpi-header` declares an unpacked structure a formal uses
# as a C structure of its members (IEEE 1800-2023 H.7.8, H.10.1).
fsim_add_foreign_conformance_test(
  dpi-header dpi foreign_dpi_header.sv foreign_dpi_header.expected ""
  fsim_foreign_dpi_types_plugin "" OFF v4
  DPI_HEADER foreign_dpi_header.expected)
# The exports of generate blocks are in the header (35.5.4, 35.7).
fsim_add_foreign_conformance_test(
  dpi-generate-header dpi foreign_dpi_generate_header.sv
  foreign_dpi_generate_header.expected ""
  fsim_foreign_dpi_types_plugin "" OFF v4
  DPI_HEADER foreign_dpi_generate_header.expected)
fsim_add_foreign_conformance_test(
  mixed-model mixed "foreign_mixed_model.vhd|foreign_mixed_model.sv"
  foreign_mixed_model.expected "" fsim_foreign_mixed_model_plugin)
# PLI 1.0 TF routines of IEEE 1364-2001 clause 25, registered through
# the PLI 1.0 task table; the plusarg feeds mc_scan_plusargs.
add_library(fsim_foreign_pli_tf_core_plugin SHARED
  foreign_pli_tf_core_plugin.c)
fsim_configure_library(fsim_foreign_pli_tf_core_plugin)
target_link_libraries(fsim_foreign_pli_tf_core_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_tf_core_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  pli-tf-core vpi foreign_pli_tf_core.sv foreign_pli_tf_core.expected
  "+tfcore=abc" fsim_foreign_pli_tf_core_plugin "" TRUE)
# PLI 1.0 association mechanism: the library exports the PLI 1.0 task
# array and endofcompile_routines[] and calls no registration routine; misctf hears
# synch, rosynch, reactivate, force, release, disable and finish (IEEE
# 1364-2001 21.1.4, Tables 87 and 88). reason_disable needs the probes.
add_library(fsim_foreign_pli_mechanism_plugin SHARED
  foreign_pli_mechanism_plugin.c)
fsim_configure_library(fsim_foreign_pli_mechanism_plugin)
target_link_libraries(fsim_foreign_pli_mechanism_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_mechanism_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  pli-mechanism vpi foreign_pli_mechanism.sv foreign_pli_mechanism.expected
  "--foreign-probes" fsim_foreign_pli_mechanism_plugin "" TRUE)
# PLI 1.0 parameter value change flags and delayed string puts (25.9, 25.22,
# 25.33, 25.52, 25.55, 25.56, 25.59).
add_library(fsim_foreign_pli_tf_pvc_plugin SHARED
  foreign_pli_tf_pvc_plugin.c)
fsim_configure_library(fsim_foreign_pli_tf_pvc_plugin)
target_link_libraries(fsim_foreign_pli_tf_pvc_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_tf_pvc_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  pli-tf-pvc vpi foreign_pli_tf_pvc.sv foreign_pli_tf_pvc.expected ""
  fsim_foreign_pli_tf_pvc_plugin "" TRUE)
# PLI 1.0 argument description: tf_strgetp of any width, tf_typep, tf_sizep
# and tf_nump over every argument class, tf_nodeinfo classes, memories and
# scalar net strength, tf_exprinfo selects, the module's time scale and
# reason_paramdrc (25.15, 25.26, 25.27, 25.35, 25.36, 25.50, 25.53, 25.61,
# 21.3.2).
add_library(fsim_foreign_pli_tf_args_plugin SHARED
  foreign_pli_tf_args_plugin.c)
fsim_configure_library(fsim_foreign_pli_tf_args_plugin)
target_link_libraries(fsim_foreign_pli_tf_args_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_tf_args_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  pli-tf-args vpi foreign_pli_tf_args.sv foreign_pli_tf_args.expected ""
  fsim_foreign_pli_tf_args_plugin "" TRUE "" ELABORATE_ARGS "slow drc drc2 drc3")
# A run-time error runs the cbError callbacks before the end of simulation
# (IEEE 1800-2023 38.36.3); simulate fails.
add_library(fsim_foreign_vpi_error_plugin SHARED foreign_vpi_error_plugin.c)
fsim_configure_library(fsim_foreign_vpi_error_plugin)
target_link_libraries(fsim_foreign_vpi_error_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_error_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-error vpi foreign_vpi_error.sv foreign_vpi_error.expected ""
  fsim_foreign_vpi_error_plugin 1)
# The same with the looping assignment among partitioned ones.
fsim_add_foreign_conformance_test(
  vpi-error-partition vpi foreign_vpi_error_partition.sv
  foreign_vpi_error_partition.expected "" fsim_foreign_vpi_error_plugin 1)
# Multichannel descriptors shared by $fopen and the VPI mcd routines (IEEE
# 1800-2023 38.24-38.29).
add_library(fsim_foreign_vpi_mcd_plugin SHARED foreign_vpi_mcd_plugin.c)
fsim_configure_library(fsim_foreign_vpi_mcd_plugin)
target_link_libraries(fsim_foreign_vpi_mcd_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_mcd_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-mcd vpi foreign_vpi_mcd.sv foreign_vpi_mcd.expected ""
  fsim_foreign_vpi_mcd_plugin)
# The save/restart data routines and the save, restart and reset reasons
# outside a save or restart (IEEE 1800-2023 38.9, 38.31, 38.36.3).
add_library(fsim_foreign_vpi_save_restart_plugin SHARED
  foreign_vpi_save_restart_plugin.c)
fsim_configure_library(fsim_foreign_vpi_save_restart_plugin)
target_link_libraries(fsim_foreign_vpi_save_restart_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_save_restart_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-save-restart vpi foreign_vpi_save_restart.sv
  foreign_vpi_save_restart.expected "" fsim_foreign_vpi_save_restart_plugin)
# The save/restart data functions and properties outside a save or restart
# (IEEE 1076-2019 20.7, 20.8, 21.3.8, 23.12, 23.27).
add_library(fsim_foreign_vhpi_save_restart_plugin SHARED
  foreign_vhpi_save_restart_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_save_restart_plugin)
target_link_libraries(fsim_foreign_vhpi_save_restart_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_save_restart_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-save-restart vhpi foreign_vhpi_save_restart.vhd
  foreign_vhpi_save_restart.expected "" fsim_foreign_vhpi_save_restart_plugin)
# Zero-delay scheduled writes made before time 0 to variables no process
# writes take effect at time 0 (IEEE 1800-2023 38.35).
add_library(fsim_foreign_vpi_scheduled_plugin SHARED
  foreign_vpi_scheduled_plugin.c)
fsim_configure_library(fsim_foreign_vpi_scheduled_plugin)
target_link_libraries(fsim_foreign_vpi_scheduled_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_scheduled_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-scheduled vpi foreign_vpi_scheduled.sv foreign_vpi_scheduled.expected ""
  fsim_foreign_vpi_scheduled_plugin)
# A value-change watch registered after time 0 on an output a continuous
# assignment drives, and on one of its bits (IEEE 1800-2023 38.36.1).
add_library(fsim_foreign_vpi_late_watch_plugin SHARED
  foreign_vpi_late_watch_plugin.c)
fsim_configure_library(fsim_foreign_vpi_late_watch_plugin)
target_link_libraries(fsim_foreign_vpi_late_watch_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_late_watch_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-late-watch vpi foreign_vpi_late_watch.sv foreign_vpi_late_watch.expected
  "" fsim_foreign_vpi_late_watch_plugin)
# The connection list of an instance array, typedef parameters of a generate
# scope, and value-change watches on string array elements and string class
# properties (IEEE 1800-2023 37.11, 37.28, 37.85, 38.36.1).
add_library(fsim_foreign_vpi_listop_strings_plugin SHARED foreign_vpi_listop_strings_plugin.c)
fsim_configure_library(fsim_foreign_vpi_listop_strings_plugin)
target_link_libraries(fsim_foreign_vpi_listop_strings_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_listop_strings_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-listop-strings vpi foreign_vpi_listop_strings.sv foreign_vpi_listop_strings.expected ""
  fsim_foreign_vpi_listop_strings_plugin)
# PLI 1.0 access routines run from a PLI 1.0 task table (IEEE 1364-2001
# Clauses 22 and 23).
add_library(fsim_foreign_pli_acc_plugin SHARED foreign_pli_acc_plugin.c)
fsim_configure_library(fsim_foreign_pli_acc_plugin)
target_link_libraries(fsim_foreign_pli_acc_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_acc_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  pli-acc vpi foreign_pli_acc.sv foreign_pli_acc.expected ""
  fsim_foreign_pli_acc_plugin "" TRUE)
# A force and release of a net through acc_set_value (IEEE 1364-2005 9.3.2).
fsim_add_foreign_conformance_test(
  pli-acc-force vpi foreign_pli_acc_force.sv foreign_pli_acc_force.expected ""
  fsim_foreign_pli_acc_plugin "" TRUE)
# PLI 1.0 attributes named as parameters and specparams, and acc_handle_path
# (IEEE 1364-2001 23.10-23.12, 23.55).
add_library(fsim_foreign_pli_acc_names_plugin SHARED
  foreign_pli_acc_names_plugin.c)
fsim_configure_library(fsim_foreign_pli_acc_names_plugin)
target_link_libraries(fsim_foreign_pli_acc_names_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_acc_names_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  pli-acc-names vpi foreign_pli_acc_names.sv foreign_pli_acc_names.expected
  "" fsim_foreign_pli_acc_names_plugin "" TRUE)
# PLI 1.0 delay and pulse routines (IEEE 1364-2001 22.8, Clause 23).
add_library(fsim_foreign_pli_acc_delays_plugin SHARED
  foreign_pli_acc_delays_plugin.c)
fsim_configure_library(fsim_foreign_pli_acc_delays_plugin)
target_link_libraries(fsim_foreign_pli_acc_delays_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_acc_delays_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  pli-acc-delays vpi foreign_pli_acc_delays.sv foreign_pli_acc_delays.expected
  "" fsim_foreign_pli_acc_delays_plugin "" TRUE)
# PLI 1.0 module input port delays and accMapToMipd (IEEE 1364-2005 22.5.2,
# 23.1, 23.14, 23.90, Table 130).
add_library(fsim_foreign_pli_acc_mipd_plugin SHARED
  foreign_pli_acc_mipd_plugin.c)
fsim_configure_library(fsim_foreign_pli_acc_mipd_plugin)
target_link_libraries(fsim_foreign_pli_acc_mipd_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_acc_mipd_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  pli-acc-mipd vpi foreign_pli_acc_mipd.sv foreign_pli_acc_mipd.expected
  "" fsim_foreign_pli_acc_mipd_plugin "" TRUE ""
  SDF foreign_pli_acc_mipd.sdf)
# Module paths and timing checks (IEEE 1800-2023 37.39, 37.40, 38.10) and
# cbTchkViolation (38.36.3).
add_library(fsim_foreign_vpi_timing_plugin SHARED
  foreign_vpi_timing_plugin.c)
fsim_configure_library(fsim_foreign_vpi_timing_plugin)
target_link_libraries(fsim_foreign_vpi_timing_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_timing_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-timing vpi foreign_vpi_timing.sv foreign_vpi_timing.expected ""
  fsim_foreign_vpi_timing_plugin)
# A deposit into the output of a combinational block fused into a kernel
# partition lasts until the block's own input changes (IEEE 1800-2023
# 38.34).
add_library(fsim_foreign_vpi_partition_deposit_plugin SHARED
  foreign_vpi_partition_deposit_plugin.c)
fsim_configure_library(fsim_foreign_vpi_partition_deposit_plugin)
target_link_libraries(fsim_foreign_vpi_partition_deposit_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_partition_deposit_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-partition-deposit vpi foreign_vpi_partition_deposit.sv
  foreign_vpi_partition_deposit.expected "" fsim_foreign_vpi_partition_deposit_plugin)
# A system task registered only by a cbUnresolvedSystf callback
# (IEEE 1800-2023 38.36.2), at elaboration and at simulation.
add_library(fsim_foreign_vpi_unresolved_plugin SHARED
  foreign_vpi_unresolved_plugin.c)
fsim_configure_library(fsim_foreign_vpi_unresolved_plugin)
target_link_libraries(fsim_foreign_vpi_unresolved_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_unresolved_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-unresolved vpi foreign_vpi_unresolved.sv
  foreign_vpi_unresolved.expected "" fsim_foreign_vpi_unresolved_plugin ""
  ON)
# Startup restrictions (IEEE 1800-2023 36.10.2), the handles of fired one-shot
# callbacks (38.8, 38.39), multiple indexes (38.20), scoped names (38.21),
# iterators (38.23, 38.40) and vpi_release_handle (38.38).
add_library(fsim_foreign_vpi_lifecycle_plugin SHARED
  foreign_vpi_lifecycle_plugin.c)
fsim_configure_library(fsim_foreign_vpi_lifecycle_plugin)
target_link_libraries(fsim_foreign_vpi_lifecycle_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_lifecycle_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-lifecycle vpi foreign_vpi_lifecycle.sv foreign_vpi_lifecycle.expected ""
  fsim_foreign_vpi_lifecycle_plugin)
# Interactive scope and interactive-mode callbacks (IEEE 1800-2023 38.4,
# 38.36.3); a batch $stop does not enter interactive mode.
add_library(fsim_foreign_vpi_interactive_plugin SHARED
  foreign_vpi_interactive_plugin.c)
fsim_configure_library(fsim_foreign_vpi_interactive_plugin)
target_link_libraries(fsim_foreign_vpi_interactive_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_interactive_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-interactive vpi foreign_vpi_interactive.sv
  foreign_vpi_interactive.expected "" fsim_foreign_vpi_interactive_plugin)
# The debugger session is interactive mode: it enters at a stop and leaves
# when a command resumes the run, and its scope follows vpi_control.
fsim_add_foreign_conformance_test(
  vpi-interactive-debug vpi foreign_vpi_interactive.sv
  foreign_vpi_interactive_debug.expected "" fsim_foreign_vpi_interactive_plugin
  0 OFF "" DEBUG_COMMANDS foreign_vpi_interactive_debug.tcl)
# Processes, statements and expressions (IEEE 1800-2023 37.43-37.79).
add_library(fsim_foreign_vpi_statements_plugin SHARED
  foreign_vpi_statements_plugin.c)
fsim_configure_library(fsim_foreign_vpi_statements_plugin)
target_link_libraries(fsim_foreign_vpi_statements_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_statements_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-statements vpi foreign_vpi_statements.sv
  foreign_vpi_statements.expected "" fsim_foreign_vpi_statements_plugin)
# Drivers and loads, var selects, task and function declarations and calls
# (IEEE 1800-2023 37.21, 37.41, 37.42, 37.46, 37.58).
add_library(fsim_foreign_vpi_connectivity_plugin SHARED
  foreign_vpi_connectivity_plugin.c)
fsim_configure_library(fsim_foreign_vpi_connectivity_plugin)
target_link_libraries(fsim_foreign_vpi_connectivity_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_connectivity_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-connectivity vpi foreign_vpi_connectivity.sv
  foreign_vpi_connectivity.expected
  "--sv-lib $<TARGET_FILE:fsim_foreign_vpi_connectivity_plugin>"
  fsim_foreign_vpi_connectivity_plugin)
# Assertions and the assertion API (IEEE 1800-2023 39).
add_library(fsim_foreign_vpi_assertions_plugin SHARED
  foreign_vpi_assertions_plugin.c)
fsim_configure_library(fsim_foreign_vpi_assertions_plugin)
target_link_libraries(fsim_foreign_vpi_assertions_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_assertions_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-assertions vpi foreign_vpi_assertions.sv
  foreign_vpi_assertions.expected "" fsim_foreign_vpi_assertions_plugin)
# cbStmt with and without the execution probes (IEEE 1800-2023 38.36.1.1).
add_library(fsim_foreign_vpi_stmt_plugin SHARED foreign_vpi_stmt_plugin.c)
fsim_configure_library(fsim_foreign_vpi_stmt_plugin)
target_link_libraries(fsim_foreign_vpi_stmt_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_stmt_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-stmt vpi foreign_vpi_stmt.sv foreign_vpi_stmt.expected
  "--foreign-probes" fsim_foreign_vpi_stmt_plugin)
fsim_add_foreign_conformance_test(
  vpi-stmt-noprobe vpi foreign_vpi_stmt.sv foreign_vpi_stmt_noprobe.expected
  "" fsim_foreign_vpi_stmt_plugin)
# Frames, threads and cbCreateObj, on the compiled kernel with the execution
# probes, and their refusal without (IEEE 1800-2023 37.43, 37.44, 38.36.1).
add_library(fsim_foreign_vpi_frames_plugin SHARED foreign_vpi_frames_plugin.c)
fsim_configure_library(fsim_foreign_vpi_frames_plugin)
target_link_libraries(fsim_foreign_vpi_frames_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_frames_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-frames-threads vpi foreign_vpi_frames.sv foreign_vpi_frames.expected
  "--foreign-probes" fsim_foreign_vpi_frames_plugin)
fsim_add_foreign_conformance_test(
  vpi-frames-threads-noprobe vpi foreign_vpi_frames.sv
  foreign_vpi_frames_noprobe.expected "" fsim_foreign_vpi_frames_plugin)
# The automatic variables of a frame and cbStmt on a task of another instance
# called by a hierarchical name, on both engines (IEEE 1800-2023 37.43,
# 38.36.1.1).
add_library(fsim_foreign_vpi_frame_vars_plugin SHARED
  foreign_vpi_frame_vars_plugin.c)
fsim_configure_library(fsim_foreign_vpi_frame_vars_plugin)
target_link_libraries(fsim_foreign_vpi_frame_vars_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_frame_vars_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-frame-vars vpi foreign_vpi_frame_vars.sv foreign_vpi_frame_vars.expected
  "--foreign-probes" fsim_foreign_vpi_frame_vars_plugin)
# vhpiCbStmt, vhpiCbResume and vhpiCbSuspend with and without the execution
# probes (IEEE 1076-2019 21.3.4).
add_library(fsim_foreign_vhpi_stmt_plugin SHARED foreign_vhpi_stmt_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_stmt_plugin)
target_link_libraries(fsim_foreign_vhpi_stmt_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_stmt_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-stmt vhpi foreign_vhpi_stmt.vhd foreign_vhpi_stmt.expected
  "--foreign-probes" fsim_foreign_vhpi_stmt_plugin)
fsim_add_foreign_conformance_test(
  vhpi-stmt-noprobe vhpi foreign_vhpi_stmt.vhd
  foreign_vhpi_stmt_noprobe.expected "" fsim_foreign_vhpi_stmt_plugin)
# Subprogram call stack frames: vhpiCurStackFrame, vhpiUpStack, vhpiFrameLevelP
# and the declarations a running call elaborates (IEEE 1076-2019 14.6, 19.10).
add_library(fsim_foreign_vhpi_dynamic_elab_plugin SHARED
  foreign_vhpi_dynamic_elab_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_dynamic_elab_plugin)
target_link_libraries(fsim_foreign_vhpi_dynamic_elab_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_dynamic_elab_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-dynamic-elab vhpi foreign_vhpi_dynamic_elab.vhd
  foreign_vhpi_dynamic_elab.expected "--foreign-probes"
  fsim_foreign_vhpi_dynamic_elab_plugin)
# Errors, control requests and foreign model registration (IEEE 1076-2019
# 23.3, 23.5, 23.18, 23.30).
add_library(fsim_foreign_vhpi_errors_plugin SHARED
  foreign_vhpi_errors_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_errors_plugin)
target_link_libraries(fsim_foreign_vhpi_errors_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_errors_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-errors vhpi foreign_vhpi_errors.vhd foreign_vhpi_errors.expected ""
  fsim_foreign_vhpi_errors_plugin)
# Foreign function results, access values and cycle counts (IEEE 1076-2019
# 22.4, 22.5.5, 23.18).
add_library(fsim_foreign_vhpi_values_plugin SHARED
  foreign_vhpi_values_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_values_plugin)
target_link_libraries(fsim_foreign_vhpi_values_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_values_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-values vhpi foreign_vhpi_values.vhd foreign_vhpi_values.expected ""
  fsim_foreign_vhpi_values_plugin)
# A foreign architecture (IEEE 1076-2019 14.5.1, 20.2.4, 20.4, 20.5, 22.5.4,
# 22.6, 23.6, 23.34), and the error of a design whose foreign architecture no
# loaded library registers.
add_library(fsim_foreign_vhpi_arch_plugin SHARED foreign_vhpi_arch_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_arch_plugin)
target_link_libraries(fsim_foreign_vhpi_arch_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_arch_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(
  fsim_foreign_vhpi_arch_missing_plugin SHARED
  foreign_vhpi_arch_missing_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_arch_missing_plugin)
target_link_libraries(fsim_foreign_vhpi_arch_missing_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_arch_missing_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-foreign-arch vhpi foreign_vhpi_arch.vhd foreign_vhpi_arch.expected ""
  fsim_foreign_vhpi_arch_plugin 0 OFF v4)
fsim_add_foreign_conformance_test(
  vhpi-foreign-arch-missing vhpi foreign_vhpi_arch.vhd
  foreign_vhpi_arch_missing.expected ""
  fsim_foreign_vhpi_arch_missing_plugin 1 OFF v4
  MESSAGE "has no registered foreign architecture with an execution function")
# Top-level generic and parameter overrides (IEEE 1076-2019 14.2, IEEE
# 1800-2023 23.10): REAL, physical, enumeration, character and BIT or
# STD_ULOGIC array generics, and real SystemVerilog parameters.
add_library(fsim_foreign_generic_override_plugin SHARED
  foreign_generic_override_plugin.c)
fsim_configure_library(fsim_foreign_generic_override_plugin)
target_link_libraries(fsim_foreign_generic_override_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_generic_override_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhdl-generic-override vhpi foreign_generic_override.vhd
  foreign_generic_override.expected "" fsim_foreign_generic_override_plugin
  "" OFF "" ELABORATE_ARGS
  "--generic R=2.5 --generic T=1.5ns --generic D=3cm --generic E=Blue --generic \"C='z'\" --generic \"B='1'\" --generic \"S='Z'\" --generic 'V=x\"A5\"' --generic W=1010 --generic I=-7")
fsim_add_foreign_conformance_test(
  sv-parameter-override vpi foreign_parameter_override.sv
  foreign_parameter_override.expected "" fsim_foreign_generic_override_plugin
  "" OFF "" ELABORATE_ARGS
  "--generic R=2.5 --generic S=0.5 --generic U=1.25 --generic N=2.5 --generic RT=4")
# Registration through a tabular registry (IEEE 1076-2019 20.2.2).
add_library(fsim_foreign_vhpi_registry_plugin SHARED
  foreign_vhpi_registry_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_registry_plugin)
target_link_libraries(fsim_foreign_vhpi_registry_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_registry_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-registry vhpi foreign_vhpi_registry.vhd foreign_vhpi_registry.expected
  "--vhpi-registry ${FSIM_FOREIGN_FIXTURES}/foreign_vhpi_registry.reg"
  fsim_foreign_vhpi_registry_plugin 0 OFF v4)
fsim_add_foreign_conformance_test(
  vhpi-registry-bad vhpi foreign_vhpi_registry.vhd
  foreign_vhpi_registry_bad.expected
  "--vhpi-registry ${FSIM_FOREIGN_FIXTURES}/foreign_vhpi_registry_bad.reg"
  fsim_foreign_vhpi_registry_plugin 1 OFF v4
  MESSAGE "line 4: 'vhpiarchf' is not vhpiArchF")
# Standard direct bindings (IEEE 1076-2019 20.2.4.3), and the error of one
# whose execution function the object library lacks.
add_library(fsim_foreign_vhpi_direct_plugin SHARED
  foreign_vhpi_direct_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_direct_plugin)
target_link_libraries(fsim_foreign_vhpi_direct_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_direct_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-direct vhpi foreign_vhpi_direct.vhd foreign_vhpi_direct.expected ""
  fsim_foreign_vhpi_direct_plugin 0 OFF v4)
fsim_add_foreign_conformance_test(
  vhpi-direct-missing vhpi foreign_vhpi_direct_missing.vhd
  foreign_vhpi_direct_missing.expected ""
  fsim_foreign_vhpi_direct_plugin 1 OFF v4
  MESSAGE "no execution function 'no_such_function'")
# Processes and statements (IEEE 1076-2019 19.4.2, 19.11).
add_library(fsim_foreign_vhpi_statements_plugin SHARED
  foreign_vhpi_statements_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_statements_plugin)
target_link_libraries(fsim_foreign_vhpi_statements_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_statements_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-statements vhpi foreign_vhpi_statements.vhd
  foreign_vhpi_statements.expected "" fsim_foreign_vhpi_statements_plugin)
# Drivers, basic signals, contributors and loads (IEEE 1076-2019 19.12).
add_library(fsim_foreign_vhpi_connectivity_plugin SHARED
  foreign_vhpi_connectivity_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_connectivity_plugin)
target_link_libraries(fsim_foreign_vhpi_connectivity_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_connectivity_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-connectivity vhpi foreign_vhpi_connectivity.vhd
  foreign_vhpi_connectivity.expected "" fsim_foreign_vhpi_connectivity_plugin)
# Relations of the class diagrams: component and subprogram declarations of
# calls, driven signals, range bounds, simulated nets, libraries and design
# units (IEEE 1076-2019 19.4, 19.10, 19.12).
add_library(fsim_foreign_vhpi_relations_plugin SHARED
  foreign_vhpi_relations_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_relations_plugin)
target_link_libraries(fsim_foreign_vhpi_relations_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_relations_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-relations vhpi foreign_vhpi_relations.vhd
  foreign_vhpi_relations.expected "" fsim_foreign_vhpi_relations_plugin)
# S'STABLE and S'QUIET of a port SystemVerilog work drives (IEEE 1076-2019
# 14.7.4): FALSE in the cycle of the change whichever language writes it.
add_library(fsim_foreign_vhpi_stable_mixed_plugin SHARED
  foreign_vhpi_stable_mixed_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_stable_mixed_plugin)
target_link_libraries(fsim_foreign_vhpi_stable_mixed_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_stable_mixed_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-stable-mixed vhpi
  "foreign_vhpi_stable_mixed.vhd|foreign_vhpi_stable_mixed.sv"
  foreign_vhpi_stable_mixed.expected "" fsim_foreign_vhpi_stable_mixed_plugin)

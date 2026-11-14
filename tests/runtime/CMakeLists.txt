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
  runtime_reset_executor_tests.cpp
  runtime_run_state_tests.cpp
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
# runtime_run_state_tests.cpp includes the private archive header, which
# walks aggregates with Boost.PFR.
target_link_libraries(fsim_runtime_tests PRIVATE fsim_runtime fsim_boost_pfr_headers)
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
# Plug-in paths lie in this build tree, which differs between checkouts: only
# the sources that load the plug-ins get them, so the other runtime test
# sources compile the same in each checkout and share the compiler cache.
set_property(
  SOURCE runtime_vhpi_tests.cpp runtime_vpi_tests.cpp
  APPEND PROPERTY COMPILE_DEFINITIONS
    FSIM_DPI_ONLY_PLUGIN_PATH="$<TARGET_FILE:fsim_foreign_dpi_types_plugin>"
)
set_property(
  SOURCE runtime_vhpi_tests.cpp
  APPEND PROPERTY COMPILE_DEFINITIONS
    FSIM_VHPI_TEST_PLUGIN_PATH="$<TARGET_FILE:fsim_vhpi_test_plugin>"
)
set_property(
  SOURCE runtime_vhpi_reference_plugin_tests.cpp
  APPEND PROPERTY COMPILE_DEFINITIONS
    FSIM_VHPI_REFERENCE_C_PLUGIN_PATH="$<TARGET_FILE:fsim_vhpi_reference_c_plugin>"
    FSIM_VHPI_REFERENCE_CPP_PLUGIN_PATH="$<TARGET_FILE:fsim_vhpi_reference_cpp_plugin>"
)
set_property(
  SOURCE runtime_vpi_tests.cpp
  APPEND PROPERTY COMPILE_DEFINITIONS
    FSIM_VPI_TEST_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_test_plugin>"
    FSIM_VPI_STANDARD_STARTUP_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_standard_startup_plugin>"
)
set_property(
  SOURCE runtime_vpi_reference_plugin_tests.cpp
  APPEND PROPERTY COMPILE_DEFINITIONS
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
  # --dpi-header` must write, ELABORATE_ARGS more `fsim elaborate`
  # arguments, COMPILE_ARGS more `fsim compile` arguments and COMPILE_MESSAGE
  # text the single, failing `fsim compile` must report. DEBUG_COMMANDS
  # names a Tcl script that `fsim debug` evaluates in place of `fsim
  # simulate`; the case then runs on the debug
  # engine only. ENVIRONMENT lists NAME=value entries every step runs with,
  # and RERUN_ENVIRONMENT more entries for a second `fsim simulate` that must
  # print the same trace. UVM_ROOT names a UVM 1.2 kit whose uvm_pkg compiles
  # into library `uvm` before the sources, which see its src directory; the
  # case is then labelled uvm.
  cmake_parse_arguments(PARSE_ARGV 9 FSIM_OPTION ""
    "MESSAGE;SDF;ELABORATE_MESSAGE;COMPILE_MESSAGE;DPI_HEADER;ELABORATE_ARGS;COMPILE_ARGS;DEBUG_COMMANDS;TOP;UVM_ROOT"
    "ENVIRONMENT;RERUN_ENVIRONMENT")
  # TOP names the unit to elaborate (default `top`), e.g. a configuration.
  set(FSIM_TOP_UNIT top)
  if(NOT "${FSIM_OPTION_TOP}" STREQUAL "")
    set(FSIM_TOP_UNIT "${FSIM_OPTION_TOP}")
  endif()
  string(REPLACE ";" "|" FSIM_ENVIRONMENT_TEXT "${FSIM_OPTION_ENVIRONMENT}")
  string(REPLACE ";" "|" FSIM_RERUN_ENVIRONMENT_TEXT
    "${FSIM_OPTION_RERUN_ENVIRONMENT}")
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
        "-DFSIM_TOP=${FSIM_TOP_UNIT}"
        "-DFSIM_ENGINE=${FSIM_ENGINE}"
        "-DFSIM_EXPECTED=${FSIM_FOREIGN_FIXTURES}/${FSIM_EXPECTED}"
        "-DFSIM_SIMULATE_ARGS=${FSIM_SIMULATE_ARGS}"
        "-DFSIM_SIMULATE_STATUS=${FSIM_SIMULATE_STATUS}"
        "-DFSIM_SIMULATE_MESSAGE=${FSIM_SIMULATE_MESSAGE}"
        "-DFSIM_ELABORATE_PLUGIN=${FSIM_ELABORATE_PLUGIN}"
        "-DFSIM_SDF=${FSIM_SDF_PATH}"
        "-DFSIM_ELABORATE_MESSAGE=${FSIM_OPTION_ELABORATE_MESSAGE}"
        "-DFSIM_COMPILE_MESSAGE=${FSIM_OPTION_COMPILE_MESSAGE}"
        "-DFSIM_DPI_HEADER=${FSIM_DPI_HEADER_PATH}"
        "-DFSIM_ELABORATE_ARGS=${FSIM_OPTION_ELABORATE_ARGS}"
        "-DFSIM_COMPILE_ARGS=${FSIM_OPTION_COMPILE_ARGS}"
        "-DFSIM_DEBUG_COMMANDS=${FSIM_DEBUG_COMMANDS_PATH}"
        "-DFSIM_ENVIRONMENT=${FSIM_ENVIRONMENT_TEXT}"
        "-DFSIM_RERUN_ENVIRONMENT=${FSIM_RERUN_ENVIRONMENT_TEXT}"
        "-DFSIM_UVM_ROOT=${FSIM_OPTION_UVM_ROOT}"
        "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/foreign/${FSIM_NAME}-${FSIM_ENGINE}"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunForeignConformanceCase.cmake")
    set_tests_properties(
      fsim.foreign.${FSIM_NAME}.${FSIM_ENGINE}
      PROPERTIES LABELS "fast;foreign;${FSIM_INTERFACE};conformance;linux")
    if(NOT "${FSIM_OPTION_UVM_ROOT}" STREQUAL "")
      set_property(TEST fsim.foreign.${FSIM_NAME}.${FSIM_ENGINE}
        APPEND PROPERTY LABELS uvm)
    endif()
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
# An event control through the name of a delayed load port waits on its
# delayed copy (IEEE 1364-2005 16.2.4).
add_library(fsim_foreign_vpi_hier_event_plugin SHARED
  foreign_vpi_hier_event_plugin.c)
fsim_configure_library(fsim_foreign_vpi_hier_event_plugin)
target_link_libraries(fsim_foreign_vpi_hier_event_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_hier_event_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-sdf-hier-event vpi foreign_vpi_sdf_hier_event.sv
  foreign_vpi_sdf_hier_event.expected "" fsim_foreign_vpi_hier_event_plugin ""
  OFF "" SDF foreign_vpi_sdf_hier_event.sdf)
# A module input port delay put on a port no SDF annotation split: the first
# put splits the port's net at a safe point, the second retimes the copy (IEEE
# 1800-2023 38.32, IEEE 1364-2005 16.2.4), on a port and on a port bit.
add_library(fsim_foreign_vpi_mipd_split_plugin SHARED
  foreign_vpi_mipd_split_plugin.c)
fsim_configure_library(fsim_foreign_vpi_mipd_split_plugin)
target_link_libraries(fsim_foreign_vpi_mipd_split_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_mipd_split_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-mipd-split vpi foreign_vpi_mipd_split.sv foreign_vpi_mipd_split.expected
  "" fsim_foreign_vpi_mipd_split_plugin)
# The same split on the kernel's other shapes: parallel batches, a persisted
# kernel image (the second run restores it and splits afresh), no dispatch
# groups, and a load of edge-triggered processes (foreign_vpi_mipd_split_groups).
fsim_add_foreign_conformance_test(
  vpi-mipd-split-mt vpi foreign_vpi_mipd_split.sv
  foreign_vpi_mipd_split.expected "" fsim_foreign_vpi_mipd_split_plugin "" ""
  v4 ENVIRONMENT FSIM_KERNEL_THREADS=4 FSIM_KERNEL_MT_FORCE=1)
if(FSIM_WITH_LLVM)
  fsim_add_foreign_conformance_test(
    vpi-mipd-split-aot vpi foreign_vpi_mipd_split.sv
    foreign_vpi_mipd_split.expected "" fsim_foreign_vpi_mipd_split_plugin "" ""
    v4 ELABORATE_ARGS "--aot" RERUN_ENVIRONMENT FSIM_KERNEL_IMAGE_REQUIRED=1)
endif()
fsim_add_foreign_conformance_test(
  vpi-mipd-split-nogroups vpi foreign_vpi_mipd_split.sv
  foreign_vpi_mipd_split.expected "" fsim_foreign_vpi_mipd_split_plugin "" ""
  v4 ENVIRONMENT FSIM_STATIC_KERNEL_DISPATCH_GROUPS=0)
fsim_add_foreign_conformance_test(
  vpi-mipd-split-groups vpi foreign_vpi_mipd_split_groups.sv
  foreign_vpi_mipd_split_groups.expected "" fsim_foreign_vpi_mipd_split_plugin)
fsim_add_foreign_conformance_test(
  vpi-mipd-split-groups-mt vpi foreign_vpi_mipd_split_groups.sv
  foreign_vpi_mipd_split_groups.expected "" fsim_foreign_vpi_mipd_split_plugin
  "" "" v4 ENVIRONMENT FSIM_KERNEL_THREADS=4 FSIM_KERNEL_MT_FORCE=1)
fsim_add_foreign_conformance_test(
  vpi-mipd-split-groups-nogroups vpi foreign_vpi_mipd_split_groups.sv
  foreign_vpi_mipd_split_groups.expected "" fsim_foreign_vpi_mipd_split_plugin
  "" "" v4 ENVIRONMENT FSIM_STATIC_KERNEL_DISPATCH_GROUPS=0)
# The lazy split equals the elaboration-time one: the plug-in puts 1 ps into
# the run, before any change, the delays that an SDF PORT annotation gives at
# elaboration, and both runs print the same trace.
add_library(fsim_foreign_vpi_mipd_split_early_plugin SHARED
  foreign_vpi_mipd_split_early_plugin.c)
fsim_configure_library(fsim_foreign_vpi_mipd_split_early_plugin)
target_link_libraries(
  fsim_foreign_vpi_mipd_split_early_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_mipd_split_early_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-mipd-split-early vpi foreign_vpi_mipd_split.sv
  foreign_vpi_mipd_split_early.expected "" fsim_foreign_vpi_mipd_split_early_plugin)
fsim_add_foreign_conformance_test(
  vpi-mipd-split-early-sdf vpi foreign_vpi_mipd_split.sv
  foreign_vpi_mipd_split_early.expected "" fsim_foreign_vpi_mipd_split_early_plugin
  "" "" "" SDF foreign_vpi_mipd_split_early.sdf)
# A foreign deposit, force and release of nets of a user-defined nettype
# (IEEE 1800-2023 6.6.7, 38.34): a deposit holds until the net resolves
# again, a force over driver changes, and a release restores the resolution.
add_library(fsim_foreign_vpi_nettype_put_plugin SHARED
  foreign_vpi_nettype_put_plugin.c)
fsim_configure_library(fsim_foreign_vpi_nettype_put_plugin)
target_link_libraries(fsim_foreign_vpi_nettype_put_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_nettype_put_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-nettype-put vpi foreign_vpi_nettype_put.sv
  foreign_vpi_nettype_put.expected "" fsim_foreign_vpi_nettype_put_plugin)
# A foreign write (deposit, force, release) of a net whose load has per-source
# INTERCONNECT delays reaches the load after the smallest delay of its sources
# for each bit's transition (the group-mode shadows carry only the sources'
# writes).
add_library(fsim_foreign_vpi_net_foreign_plugin SHARED
  foreign_vpi_net_foreign_plugin.c)
fsim_configure_library(fsim_foreign_vpi_net_foreign_plugin)
target_link_libraries(fsim_foreign_vpi_net_foreign_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_net_foreign_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-net-foreign vpi foreign_vpi_net_foreign.sv
  foreign_vpi_net_foreign.expected "" fsim_foreign_vpi_net_foreign_plugin
  "" OFF "" SDF foreign_vpi_net_foreign.sdf)
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
# Mailbox and semaphore objects, an inline constraint, a task through an
# interface port and a specialization's properties (IEEE 1800-2023 37.32,
# 37.33, 37.41, 37.42).
add_library(fsim_foreign_vpi_class_sync_plugin SHARED
  foreign_vpi_class_sync_plugin.c)
fsim_configure_library(fsim_foreign_vpi_class_sync_plugin)
target_link_libraries(fsim_foreign_vpi_class_sync_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_class_sync_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-class-sync vpi foreign_vpi_class_sync.sv foreign_vpi_class_sync.expected
  "--foreign-probes" fsim_foreign_vpi_class_sync_plugin)
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
  foreign_vpi_virtual_interfaces.expected "--foreign-probes"
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
# Time queue objects and the timed callbacks due in them (IEEE 1800-2023
# 37.80, 37.81, 38.36.2).
add_library(fsim_foreign_vpi_time_queue_plugin SHARED
  foreign_vpi_time_queue_plugin.c)
fsim_configure_library(fsim_foreign_vpi_time_queue_plugin)
target_link_libraries(fsim_foreign_vpi_time_queue_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_time_queue_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-time-queue vpi foreign_vpi_time_queue.sv
  foreign_vpi_time_queue.expected "" fsim_foreign_vpi_time_queue_plugin)
# Associative array words named by key, handles kept while the key exists
# (IEEE 1800-2023 37.3.8, 37.17).
add_library(fsim_foreign_vpi_assoc_words_plugin SHARED
  foreign_vpi_assoc_words_plugin.c)
fsim_configure_library(fsim_foreign_vpi_assoc_words_plugin)
target_link_libraries(fsim_foreign_vpi_assoc_words_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_assoc_words_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-assoc-words vpi foreign_vpi_assoc_words.sv
  foreign_vpi_assoc_words.expected "" fsim_foreign_vpi_assoc_words_plugin)
# Words of a variable array and of a net array put, read back and watched
# while the compiled kernel holds the arrays (IEEE 1800-2023 37.17, 38.34,
# 38.36.1), also under the debugger.
add_library(fsim_foreign_vpi_memory_words_plugin SHARED
  foreign_vpi_memory_words_plugin.c)
fsim_configure_library(fsim_foreign_vpi_memory_words_plugin)
target_link_libraries(fsim_foreign_vpi_memory_words_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_memory_words_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-memory-words vpi foreign_vpi_memory_words.sv
  foreign_vpi_memory_words.expected "" fsim_foreign_vpi_memory_words_plugin)
fsim_add_foreign_conformance_test(
  vpi-memory-words-debug vpi foreign_vpi_memory_words.sv
  foreign_vpi_memory_words.expected "" fsim_foreign_vpi_memory_words_plugin
  0 OFF "" DEBUG_COMMANDS foreign_vpi_memory_words_debug.tcl)
# Library binding of a configured design (IEEE 1800-2023 33.7).
add_library(fsim_foreign_vpi_config_plugin SHARED
  foreign_vpi_config_plugin.c)
fsim_configure_library(fsim_foreign_vpi_config_plugin)
target_link_libraries(fsim_foreign_vpi_config_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_config_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-config vpi foreign_vpi_config.sv foreign_vpi_config.expected ""
  fsim_foreign_vpi_config_plugin "" OFF "" TOP cfg)
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
# The DPI disable protocol (IEEE 1800-2023 35.9), on both engines.
add_library(fsim_foreign_dpi_disable_plugin SHARED
  foreign_dpi_disable_plugin.c)
fsim_configure_library(fsim_foreign_dpi_disable_plugin)
target_link_libraries(fsim_foreign_dpi_disable_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_disable_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-disable dpi foreign_dpi_disable.sv foreign_dpi_disable.expected ""
  fsim_foreign_dpi_disable_plugin "" OFF)
# The DPI disable protocol when the export is the disable target (IEEE
# 1800-2023 35.9), on both engines.
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
  "" OFF)
# $reset with a DPI import call chain in flight (IEEE 1800-2023 D.8, 35.9):
# the chain is disabled and the svPutUserData areas are empty afterwards.
add_library(fsim_foreign_dpi_reset_plugin SHARED
  foreign_dpi_reset_plugin.c)
fsim_configure_library(fsim_foreign_dpi_reset_plugin)
target_link_libraries(fsim_foreign_dpi_reset_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_reset_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-reset dpi foreign_dpi_reset.sv foreign_dpi_reset.expected ""
  fsim_foreign_dpi_reset_plugin "" OFF)
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
# DPI exports of instances elaborated after the calling instance, inside a
# loop generate and an instance array (IEEE 1800-2023 35.5.3, 35.7).
add_library(fsim_foreign_dpi_export_order_plugin SHARED
  foreign_dpi_export_order_plugin.c)
fsim_configure_library(fsim_foreign_dpi_export_order_plugin)
target_link_libraries(fsim_foreign_dpi_export_order_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_export_order_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-export-order dpi foreign_dpi_export_order.sv
  foreign_dpi_export_order.expected "" fsim_foreign_dpi_export_order_plugin)
# A module and a generate block exporting functions of one name (IEEE
# 1800-2023 35.5.3, 35.7), on the compiled kernel.
add_library(fsim_foreign_dpi_export_same_name_plugin SHARED
  foreign_dpi_export_same_name_plugin.c)
fsim_configure_library(fsim_foreign_dpi_export_same_name_plugin)
target_link_libraries(fsim_foreign_dpi_export_same_name_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_dpi_export_same_name_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  dpi-export-same-name dpi foreign_dpi_export_same_name.sv
  foreign_dpi_export_same_name.expected ""
  fsim_foreign_dpi_export_same_name_plugin "" OFF v4)
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
# A VPI plug-in drives a mixed design's clock through top-level ports nothing
# in the design drives, with zero-delay inertial puts as cocotb does: the
# VHDL register stage woken by the edge reads the SystemVerilog stage's value
# from before the edge's nonblocking update (IEEE 1800-2023 4.4.2, 38.34;
# IEEE 1076-2019 14.7.5.3).
add_library(fsim_foreign_vpi_mixed_clock_plugin SHARED
  foreign_vpi_mixed_clock_plugin.c)
fsim_configure_library(fsim_foreign_vpi_mixed_clock_plugin)
target_link_libraries(fsim_foreign_vpi_mixed_clock_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_mixed_clock_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-mixed-clock vpi
  "foreign_vpi_mixed_clock.vhd@2008|foreign_vpi_mixed_clock.sv"
  foreign_vpi_mixed_clock.expected "" fsim_foreign_vpi_mixed_clock_plugin)
# Zero-delay inertial puts of a string variable, as cocotb writes it: each
# put that changes the value wakes its event control (IEEE 1800-2023 9.4.2,
# 38.34).
add_library(fsim_foreign_vpi_string_put_plugin SHARED
  foreign_vpi_string_put_plugin.c)
fsim_configure_library(fsim_foreign_vpi_string_put_plugin)
target_link_libraries(fsim_foreign_vpi_string_put_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_string_put_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-string-put vpi foreign_vpi_string_put.sv foreign_vpi_string_put.expected
  "" fsim_foreign_vpi_string_put_plugin)
# A variable with an escaped identifier and an initializer does not end the
# iteration of its module's scopes (IEEE 1800-2023 5.6.1, 37.10).
add_library(fsim_foreign_vpi_escaped_scope_plugin SHARED
  foreign_vpi_escaped_scope_plugin.c)
fsim_configure_library(fsim_foreign_vpi_escaped_scope_plugin)
target_link_libraries(fsim_foreign_vpi_escaped_scope_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_escaped_scope_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-escaped-scope vpi foreign_vpi_escaped_scope.sv
  foreign_vpi_escaped_scope.expected "" fsim_foreign_vpi_escaped_scope_plugin)
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
# reason_paramdrc for the drivers a force, a procedural continuous assign and
# a concatenation target make (IEEE 1364-2001 21.3.2, 9.3, Table 88).
fsim_add_foreign_conformance_test(
  pli-tf-drivers vpi foreign_pli_tf_drivers.sv foreign_pli_tf_drivers.expected ""
  fsim_foreign_pli_tf_args_plugin "" TRUE "" ELABORATE_ARGS "drcf drca drcc drcm drcg drcd drcp drcq")
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
# Each blocking write that changes a watched variable is its own value
# change, a write of a wide variable one change (IEEE 1800-2023 38.36.1).
add_library(fsim_foreign_vpi_each_write_plugin SHARED
  foreign_vpi_each_write_plugin.c)
fsim_configure_library(fsim_foreign_vpi_each_write_plugin)
target_link_libraries(fsim_foreign_vpi_each_write_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_each_write_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-each-write vpi foreign_vpi_each_write.sv foreign_vpi_each_write.expected
  "" fsim_foreign_vpi_each_write_plugin)
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
# The interactive scope starts at the first top-level module (IEEE 1364-2001
# 23.46, 23.94).
add_library(fsim_foreign_pli_acc_iscope_plugin SHARED
  foreign_pli_acc_iscope_plugin.c)
fsim_configure_library(fsim_foreign_pli_acc_iscope_plugin)
target_link_libraries(fsim_foreign_pli_acc_iscope_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_acc_iscope_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  pli-acc-iscope vpi foreign_pli_acc_iscope.sv foreign_pli_acc_iscope.expected
  "" fsim_foreign_pli_acc_iscope_plugin "" TRUE)
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
# Module path delays written through specparam triples, the delay devices of
# module paths (IEEE 1800-2023 30.4.1, 37.45, 37.46, 38.10) and a put on a
# tranif's turn-on and turn-off delays (28.16, 38.32).
add_library(fsim_foreign_vpi_modpath_delay_plugin SHARED
  foreign_vpi_modpath_delay_plugin.c)
fsim_configure_library(fsim_foreign_vpi_modpath_delay_plugin)
target_link_libraries(fsim_foreign_vpi_modpath_delay_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_modpath_delay_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-modpath-delay vpi foreign_vpi_modpath_delay.sv
  foreign_vpi_modpath_delay.expected "" fsim_foreign_vpi_modpath_delay_plugin)
# vpi_put_delays on a gate declared without a delay splits it into a delay
# slot at the next safe point (IEEE 1800-2023 38.32, 28.16).
add_library(fsim_foreign_vpi_gate_split_plugin SHARED
  foreign_vpi_gate_split_plugin.c)
fsim_configure_library(fsim_foreign_vpi_gate_split_plugin)
target_link_libraries(fsim_foreign_vpi_gate_split_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_gate_split_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-gate-split vpi foreign_vpi_gate_split.sv
  foreign_vpi_gate_split.expected "" fsim_foreign_vpi_gate_split_plugin)
# The same put on a gate whose output net has a weak driver (a nine-valued
# slot) and on gates that drive bits of a net alias family (IEEE 1800-2023
# 28.12, 10.11, 38.32).
add_library(fsim_foreign_vpi_gate_split_wide_plugin SHARED
  foreign_vpi_gate_split_wide_plugin.c)
fsim_configure_library(fsim_foreign_vpi_gate_split_wide_plugin)
target_link_libraries(fsim_foreign_vpi_gate_split_wide_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_gate_split_wide_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-gate-split-nine vpi foreign_vpi_gate_split_nine.sv
  foreign_vpi_gate_split_nine.expected "" fsim_foreign_vpi_gate_split_wide_plugin)
fsim_add_foreign_conformance_test(
  vpi-gate-split-alias vpi foreign_vpi_gate_split_alias.sv
  foreign_vpi_gate_split_alias.expected "" fsim_foreign_vpi_gate_split_wide_plugin)
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
# Typespecs of tasks' and functions' formals declared with a typedef, resolved
# through the import visibility of IEEE 1800-2023 26.3 (37.25, 37.41).
add_library(fsim_foreign_vpi_formal_types_plugin SHARED
  foreign_vpi_formal_types_plugin.c)
fsim_configure_library(fsim_foreign_vpi_formal_types_plugin)
target_link_libraries(fsim_foreign_vpi_formal_types_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_formal_types_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-formal-types vpi foreign_vpi_formal_types.sv
  foreign_vpi_formal_types.expected ""
  fsim_foreign_vpi_formal_types_plugin)
# The typespec of a formal or result of a class or container type, and the
# ranges of one declared with a typedef (IEEE 1800-2023 37.41, 37.25).
add_library(fsim_foreign_vpi_formal_containers_plugin SHARED
  foreign_vpi_formal_containers_plugin.c)
fsim_configure_library(fsim_foreign_vpi_formal_containers_plugin)
target_link_libraries(
  fsim_foreign_vpi_formal_containers_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_formal_containers_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-formal-containers vpi foreign_vpi_formal_containers.sv
  foreign_vpi_formal_containers.expected ""
  fsim_foreign_vpi_formal_containers_plugin)
# The class typespec of a handle whose parameterized class type reads the
# parameters of its module is the instance's specialization (IEEE 1800-2023
# 8.25, 23.2.2, 37.32).
add_library(fsim_foreign_vpi_class_instance_plugin SHARED
  foreign_vpi_class_instance_plugin.c)
fsim_configure_library(fsim_foreign_vpi_class_instance_plugin)
target_link_libraries(fsim_foreign_vpi_class_instance_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_class_instance_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-class-instance vpi foreign_vpi_class_instance.sv
  foreign_vpi_class_instance.expected ""
  fsim_foreign_vpi_class_instance_plugin)
# The typespec chain of packed dimensions declared over a typedef keeps the
# typedef's name (IEEE 1800-2023 7.4.1, 37.25).
add_library(fsim_foreign_vpi_typedef_chain_plugin SHARED
  foreign_vpi_typedef_chain_plugin.c)
fsim_configure_library(fsim_foreign_vpi_typedef_chain_plugin)
target_link_libraries(fsim_foreign_vpi_typedef_chain_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_typedef_chain_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-typedef-chain vpi foreign_vpi_typedef_chain.sv
  foreign_vpi_typedef_chain.expected ""
  fsim_foreign_vpi_typedef_chain_plugin)
# The constructor arguments of `extends B(args)` as the vpiArgument objects
# of the extends object (IEEE 1800-2023 8.17, 37.32).
add_library(fsim_foreign_vpi_extends_args_plugin SHARED
  foreign_vpi_extends_args_plugin.c)
fsim_configure_library(fsim_foreign_vpi_extends_args_plugin)
target_link_libraries(fsim_foreign_vpi_extends_args_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_extends_args_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-extends-args vpi foreign_vpi_extends_args.sv
  foreign_vpi_extends_args.expected ""
  fsim_foreign_vpi_extends_args_plugin)
# Container properties of a live class object: elements, indexes and size
# changes (IEEE 1800-2023 37.17, 37.20, 37.33, 38.36.1).
add_library(fsim_foreign_vpi_class_containers_plugin SHARED
  foreign_vpi_class_containers_plugin.c)
fsim_configure_library(fsim_foreign_vpi_class_containers_plugin)
target_link_libraries(fsim_foreign_vpi_class_containers_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_class_containers_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-class-containers vpi foreign_vpi_class_containers.sv
  foreign_vpi_class_containers.expected ""
  fsim_foreign_vpi_class_containers_plugin)
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
# The VPI callbacks of the design's assertion control tasks (IEEE 1800-2023
# 20.11 Table 20-8, 39.4.1, 39.4.2).
add_library(fsim_foreign_vpi_assertion_control_tasks_plugin SHARED
  foreign_vpi_assertion_control_tasks_plugin.c)
fsim_configure_library(fsim_foreign_vpi_assertion_control_tasks_plugin)
target_link_libraries(fsim_foreign_vpi_assertion_control_tasks_plugin
  PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_assertion_control_tasks_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-assertion-control-tasks vpi foreign_vpi_assertion_control_tasks.sv
  foreign_vpi_assertion_control_tasks.expected ""
  fsim_foreign_vpi_assertion_control_tasks_plugin)
# The failing expression of a rejected attempt (IEEE 1800-2023 39.4.2).
add_library(fsim_foreign_vpi_assertions_failexpr_plugin SHARED
  foreign_vpi_assertions_failexpr_plugin.c)
fsim_configure_library(fsim_foreign_vpi_assertions_failexpr_plugin)
target_link_libraries(fsim_foreign_vpi_assertions_failexpr_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_assertions_failexpr_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-assertions-failexpr vpi foreign_vpi_assertions_failexpr.sv
  foreign_vpi_assertions_failexpr.expected ""
  fsim_foreign_vpi_assertions_failexpr_plugin)
# Inferred clocking events of concurrent assertions (IEEE 1800-2023 16.14.6).
add_library(fsim_foreign_vpi_assertions_inferred_plugin SHARED
  foreign_vpi_assertions_inferred_plugin.c)
fsim_configure_library(fsim_foreign_vpi_assertions_inferred_plugin)
target_link_libraries(fsim_foreign_vpi_assertions_inferred_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_assertions_inferred_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-assertions-inferred vpi foreign_vpi_assertions_inferred.sv
  foreign_vpi_assertions_inferred.expected ""
  fsim_foreign_vpi_assertions_inferred_plugin)
# Interface tf decls: extern prototypes and the bodies of an extern forkjoin
# task (IEEE 1800-2023 37.6, 37.8, 25.7.3).
add_library(fsim_foreign_vpi_interface_tf_plugin SHARED
  foreign_vpi_interface_tf_plugin.c)
fsim_configure_library(fsim_foreign_vpi_interface_tf_plugin)
target_link_libraries(fsim_foreign_vpi_interface_tf_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_interface_tf_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-interface-tf vpi foreign_vpi_interface_tf.sv
  foreign_vpi_interface_tf.expected ""
  fsim_foreign_vpi_interface_tf_plugin)
# Attempt callbacks: one disabled outcome per running attempt with its own
# start, and the start of an attempt the end of simulation fails (39.4.2).
add_library(fsim_foreign_vpi_assertions_attempts_plugin SHARED
  foreign_vpi_assertions_attempts_plugin.c)
fsim_configure_library(fsim_foreign_vpi_assertions_attempts_plugin)
target_link_libraries(fsim_foreign_vpi_assertions_attempts_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_assertions_attempts_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-assertions-attempts vpi foreign_vpi_assertions_attempts.sv
  foreign_vpi_assertions_attempts.expected ""
  fsim_foreign_vpi_assertions_attempts_plugin)
# A cbAssertionStart registered mid-run binds the attempt-start reports then
# (39.4.2); the attempts that started earlier are not reported.
add_library(fsim_foreign_vpi_assertions_late_start_plugin SHARED
  foreign_vpi_assertions_late_start_plugin.c)
fsim_configure_library(fsim_foreign_vpi_assertions_late_start_plugin)
target_link_libraries(fsim_foreign_vpi_assertions_late_start_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_assertions_late_start_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-assertions-late-start vpi foreign_vpi_assertions_late_start.sv
  foreign_vpi_assertions_late_start.expected ""
  fsim_foreign_vpi_assertions_late_start_plugin)
# Step callbacks of enabled attempts: vpiAssertionEnableStep for attempts that
# have not started, vpiAssertionDisableStep for one that runs (39.4.2, 39.5.2).
add_library(fsim_foreign_vpi_assertions_steps_plugin SHARED
  foreign_vpi_assertions_steps_plugin.c)
fsim_configure_library(fsim_foreign_vpi_assertions_steps_plugin)
target_link_libraries(fsim_foreign_vpi_assertions_steps_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_assertions_steps_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-assertions-steps vpi foreign_vpi_assertions_steps.sv
  foreign_vpi_assertions_steps.expected ""
  fsim_foreign_vpi_assertions_steps_plugin)
# Concurrent assertions of generate blocks are assertion objects (27.3, 37.49).
add_library(fsim_foreign_vpi_assertions_generate_plugin SHARED
  foreign_vpi_assertions_generate_plugin.c)
fsim_configure_library(fsim_foreign_vpi_assertions_generate_plugin)
target_link_libraries(fsim_foreign_vpi_assertions_generate_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_assertions_generate_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-assertions-generate vpi foreign_vpi_assertions_generate.sv
  foreign_vpi_assertions_generate.expected ""
  fsim_foreign_vpi_assertions_generate_plugin)
# Assertion callbacks on immediate assertions, only with the execution probes
# (IEEE 1800-2023 39.4.1).
add_library(fsim_foreign_vpi_imm_assert_plugin SHARED
  foreign_vpi_imm_assert_plugin.c)
fsim_configure_library(fsim_foreign_vpi_imm_assert_plugin)
target_link_libraries(fsim_foreign_vpi_imm_assert_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_imm_assert_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-imm-assert vpi foreign_vpi_imm_assert.sv foreign_vpi_imm_assert.expected
  "--foreign-probes" fsim_foreign_vpi_imm_assert_plugin)
fsim_add_foreign_conformance_test(
  vpi-imm-assert-noprobe vpi foreign_vpi_imm_assert.sv
  foreign_vpi_imm_assert_noprobe.expected "" fsim_foreign_vpi_imm_assert_plugin)
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
# cbValueChange on class object properties and its exclusions (IEEE
# 1800-2023 37.33, 38.36.1).
add_library(fsim_foreign_vpi_class_watch_plugin SHARED
  foreign_vpi_class_watch_plugin.c)
fsim_configure_library(fsim_foreign_vpi_class_watch_plugin)
target_link_libraries(fsim_foreign_vpi_class_watch_plugin PRIVATE fsim_tf)
target_include_directories(fsim_foreign_vpi_class_watch_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-class-watch vpi foreign_vpi_class_watch.sv
  foreign_vpi_class_watch.expected "" fsim_foreign_vpi_class_watch_plugin)
# cbStmt and frames of the tasks and functions of other instances (IEEE
# 1800-2023 38.36.1.1, 37.43).
add_library(fsim_foreign_vpi_stmt_instances_plugin SHARED
  foreign_vpi_stmt_instances_plugin.c)
fsim_configure_library(fsim_foreign_vpi_stmt_instances_plugin)
target_link_libraries(fsim_foreign_vpi_stmt_instances_plugin PRIVATE fsim_tf)
target_include_directories(fsim_foreign_vpi_stmt_instances_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-stmt-instances vpi foreign_vpi_stmt_instances.sv
  foreign_vpi_stmt_instances.expected "--foreign-probes"
  fsim_foreign_vpi_stmt_instances_plugin)
# The value of a cast operation (IEEE 1800-2023 6.24.1, 37.78 vpiCastOp).
add_library(fsim_foreign_vpi_cast_values_plugin SHARED
  foreign_vpi_cast_values_plugin.c)
fsim_configure_library(fsim_foreign_vpi_cast_values_plugin)
target_link_libraries(fsim_foreign_vpi_cast_values_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_cast_values_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-cast-values vpi foreign_vpi_cast_values.sv
  foreign_vpi_cast_values.expected "" fsim_foreign_vpi_cast_values_plugin)
# Casts to typedefs, increments, streaming, struct member selects, alias
# statements, unpacked formals and primitive terminals (IEEE 1800-2023
# 37.35, 37.41, 37.59, 37.76).
add_library(fsim_foreign_vpi_expr_rows_plugin SHARED
  foreign_vpi_expr_rows_plugin.c)
fsim_configure_library(fsim_foreign_vpi_expr_rows_plugin)
target_link_libraries(fsim_foreign_vpi_expr_rows_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_expr_rows_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-expr-rows vpi foreign_vpi_expr_rows.sv
  foreign_vpi_expr_rows.expected "" fsim_foreign_vpi_expr_rows_plugin)
# `default disable iff and `default_decay_time of a module (IEEE 1800-2023
# 16.15, E.2, 37.5).
add_library(fsim_foreign_vpi_directives_plugin SHARED
  foreign_vpi_directives_plugin.c)
fsim_configure_library(fsim_foreign_vpi_directives_plugin)
target_link_libraries(fsim_foreign_vpi_directives_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_directives_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-directives vpi foreign_vpi_directives.sv
  foreign_vpi_directives.expected "" fsim_foreign_vpi_directives_plugin)
# A clocking block named through a virtual interface (IEEE 1800-2023 37.48).
add_library(fsim_foreign_vpi_vif_clocking_plugin SHARED
  foreign_vpi_vif_clocking_plugin.c)
fsim_configure_library(fsim_foreign_vpi_vif_clocking_plugin)
target_link_libraries(fsim_foreign_vpi_vif_clocking_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_vif_clocking_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-vif-clocking vpi foreign_vpi_vif_clocking.sv
  foreign_vpi_vif_clocking.expected "" fsim_foreign_vpi_vif_clocking_plugin)
# A DPI import of the compilation-unit scope (IEEE 1800-2023 3.12.1, 37.42).
add_library(fsim_foreign_vpi_unit_dpi_plugin SHARED
  foreign_vpi_unit_dpi_plugin.c)
fsim_configure_library(fsim_foreign_vpi_unit_dpi_plugin)
target_link_libraries(fsim_foreign_vpi_unit_dpi_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_unit_dpi_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-unit-dpi vpi foreign_vpi_unit_dpi.sv
  foreign_vpi_unit_dpi.expected "" fsim_foreign_vpi_unit_dpi_plugin)
# The typespec of a cast to a typedef is the visible typedef's (IEEE
# 1800-2023 6.24.1, 26.3, 37.59).
add_library(fsim_foreign_vpi_cast_types_plugin SHARED
  foreign_vpi_cast_types_plugin.c)
fsim_configure_library(fsim_foreign_vpi_cast_types_plugin)
target_link_libraries(fsim_foreign_vpi_cast_types_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_cast_types_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-cast-types vpi foreign_vpi_cast_types.sv
  foreign_vpi_cast_types.expected "" fsim_foreign_vpi_cast_types_plugin)
# cbDisable on a block a process left and waits outside of (IEEE 1800-2023
# 9.6.2, 38.36.1): only an active block runs the callback.
add_library(fsim_foreign_vpi_disable_exit_plugin SHARED
  foreign_vpi_disable_exit_plugin.c)
fsim_configure_library(fsim_foreign_vpi_disable_exit_plugin)
target_link_libraries(fsim_foreign_vpi_disable_exit_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_disable_exit_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-disable-exit vpi foreign_vpi_disable_exit.sv
  foreign_vpi_disable_exit.expected "--foreign-probes"
  fsim_foreign_vpi_disable_exit_plugin)
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
# The frames of calls in continuous assignments (IEEE 1800-2023 37.43
# detail 1, 38.36.1).
add_library(fsim_foreign_vpi_frames_assign_plugin SHARED
  foreign_vpi_frames_assign_plugin.c)
fsim_configure_library(fsim_foreign_vpi_frames_assign_plugin)
target_link_libraries(fsim_foreign_vpi_frames_assign_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_frames_assign_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-frames-assign vpi foreign_vpi_frames_assign.sv
  foreign_vpi_frames_assign.expected "--foreign-probes"
  fsim_foreign_vpi_frames_assign_plugin)
# The frames of a nettype's resolution function, whose origin is the net
# (IEEE 1800-2023 37.43 detail 6, 6.6.7).
add_library(fsim_foreign_vpi_frames_nettype_plugin SHARED
  foreign_vpi_frames_nettype_plugin.c)
fsim_configure_library(fsim_foreign_vpi_frames_nettype_plugin)
target_link_libraries(fsim_foreign_vpi_frames_nettype_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_frames_nettype_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-frames-nettype vpi foreign_vpi_frames_nettype.sv
  foreign_vpi_frames_nettype.expected "--foreign-probes"
  fsim_foreign_vpi_frames_nettype_plugin)
# A user function call with constant arguments runs, so its frame is seen
# (IEEE 1800-2023 13.4.3, 37.43).
fsim_add_foreign_conformance_test(
  vpi-frame-folded vpi foreign_vpi_frame_folded.sv
  foreign_vpi_frame_folded.expected "--foreign-probes"
  fsim_foreign_vpi_frames_plugin)
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
# The frames of methods the simulation's UVM services run (static methods of
# a class derived from the UVM library): starts, ends, relations, automatic
# variables read and written while live, on both engines (IEEE 1800-2023
# 37.43, 38.36.1); without --foreign-probes no frame is reported.
add_library(fsim_foreign_vpi_frames_host_plugin SHARED
  foreign_vpi_frames_host_plugin.c)
fsim_configure_library(fsim_foreign_vpi_frames_host_plugin)
target_link_libraries(fsim_foreign_vpi_frames_host_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_frames_host_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
# The same on the frames of a plain class's static methods, which run as
# generated code: vpiStmt and vpiOrigin in method bodies, cbStmt on a method's
# statements (IEEE 1800-2023 37.31, 37.41, 37.43).
fsim_add_foreign_conformance_test(
  vpi-frames-class vpi foreign_vpi_frames_class.sv
  foreign_vpi_frames_class.expected "--foreign-probes"
  fsim_foreign_vpi_frames_host_plugin)
if(FSIM_UVM_1_2_SOURCE_DIR)
  fsim_add_foreign_conformance_test(
    vpi-frames-host vpi foreign_vpi_frames_host.sv
    foreign_vpi_frames_host.expected "--foreign-probes"
    fsim_foreign_vpi_frames_host_plugin "" "" ""
    COMPILE_ARGS "--standard 2017 -I ${FSIM_UVM_1_2_SOURCE_DIR}/src"
    UVM_ROOT "${FSIM_UVM_1_2_SOURCE_DIR}")
  fsim_add_foreign_conformance_test(
    vpi-frames-host-noprobe vpi foreign_vpi_frames_host.sv
    foreign_vpi_frames_host_noprobe.expected ""
    fsim_foreign_vpi_frames_host_plugin "" "" ""
    COMPILE_ARGS "--standard 2017 -I ${FSIM_UVM_1_2_SOURCE_DIR}/src"
    UVM_ROOT "${FSIM_UVM_1_2_SOURCE_DIR}")
endif()
# An automatic event triggered by a put, and the strings of the outer
# activations of a recursive task (IEEE 1800-2023 37.43, 38.34).
add_library(fsim_foreign_vpi_frame_outer_plugin SHARED
  foreign_vpi_frame_outer_plugin.c)
fsim_configure_library(fsim_foreign_vpi_frame_outer_plugin)
target_link_libraries(fsim_foreign_vpi_frame_outer_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_frame_outer_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-frame-outer vpi foreign_vpi_frame_outer.sv foreign_vpi_frame_outer.expected
  "--foreign-probes" fsim_foreign_vpi_frame_outer_plugin)
# An automatic event array and the call a frame was activated from (IEEE
# 1800-2023 37.43).
add_library(fsim_foreign_vpi_frame_events_plugin SHARED
  foreign_vpi_frame_events_plugin.c)
fsim_configure_library(fsim_foreign_vpi_frame_events_plugin)
target_link_libraries(fsim_foreign_vpi_frame_events_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_frame_events_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-frame-events vpi foreign_vpi_frame_events.sv
  foreign_vpi_frame_events.expected "--foreign-probes"
  fsim_foreign_vpi_frame_events_plugin)
# Arrays of events, in the language (IEEE 1800-2023 6.17, 7.4, 15.5): the
# plug-in only loads.
add_library(fsim_foreign_event_arrays_plugin SHARED
  foreign_event_arrays_plugin.c)
fsim_configure_library(fsim_foreign_event_arrays_plugin)
target_link_libraries(fsim_foreign_event_arrays_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_event_arrays_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  sv-event-arrays vpi foreign_event_arrays.sv foreign_event_arrays.expected
  "" fsim_foreign_event_arrays_plugin)
# An array of events with far bounds and a class-scoped typedef event property
# through VPI (IEEE 1800-2023 7.4, 6.18, 37.43).
add_library(fsim_foreign_vpi_event_arrays_plugin SHARED
  foreign_vpi_event_arrays_plugin.c)
fsim_configure_library(fsim_foreign_vpi_event_arrays_plugin)
target_link_libraries(fsim_foreign_vpi_event_arrays_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_event_arrays_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-event-arrays vpi foreign_vpi_event_arrays.sv
  foreign_vpi_event_arrays.expected "" fsim_foreign_vpi_event_arrays_plugin)
# The arguments of a system function call over automatic variables, read on
# demand (IEEE 1800-2023 37.42 detail 7, 37.43).
add_library(fsim_foreign_vpi_sysfunc_args_plugin SHARED
  foreign_vpi_sysfunc_args_plugin.c)
fsim_configure_library(fsim_foreign_vpi_sysfunc_args_plugin)
target_link_libraries(fsim_foreign_vpi_sysfunc_args_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_sysfunc_args_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-sysfunc-args vpi foreign_vpi_sysfunc_args.sv
  foreign_vpi_sysfunc_args.expected "--foreign-probes"
  fsim_foreign_vpi_sysfunc_args_plugin)
# vpiWith of an array locator method call (IEEE 1800-2023 7.12.1, 37.42).
add_library(fsim_foreign_vpi_with_plugin SHARED foreign_vpi_with_plugin.c)
fsim_configure_library(fsim_foreign_vpi_with_plugin)
target_link_libraries(fsim_foreign_vpi_with_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_with_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-with vpi foreign_vpi_with.sv foreign_vpi_with.expected ""
  fsim_foreign_vpi_with_plugin)
# The value of a user function call: what it returned at its last evaluation,
# only with the execution probes (IEEE 1800-2023 37.42, 38.15).
add_library(fsim_foreign_vpi_func_values_plugin SHARED
  foreign_vpi_func_values_plugin.c)
fsim_configure_library(fsim_foreign_vpi_func_values_plugin)
target_link_libraries(fsim_foreign_vpi_func_values_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_func_values_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-func-values vpi foreign_vpi_func_values.sv
  foreign_vpi_func_values.expected "--foreign-probes"
  fsim_foreign_vpi_func_values_plugin)
fsim_add_foreign_conformance_test(
  vpi-func-values-noprobe vpi foreign_vpi_func_values.sv
  foreign_vpi_func_values_noprobe.expected ""
  fsim_foreign_vpi_func_values_plugin)
# The value of a system function with state: what it returned at its last
# evaluation, only with the execution probes (IEEE 1800-2023 38.15, 37.58).
add_library(fsim_foreign_vpi_sysfunc_values_plugin SHARED
  foreign_vpi_sysfunc_values_plugin.c)
fsim_configure_library(fsim_foreign_vpi_sysfunc_values_plugin)
target_link_libraries(fsim_foreign_vpi_sysfunc_values_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_sysfunc_values_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-sysfunc-values vpi foreign_vpi_sysfunc_values.sv
  foreign_vpi_sysfunc_values.expected "--foreign-probes"
  fsim_foreign_vpi_sysfunc_values_plugin)
fsim_add_foreign_conformance_test(
  vpi-sysfunc-values-noprobe vpi foreign_vpi_sysfunc_values.sv
  foreign_vpi_sysfunc_values_noprobe.expected ""
  fsim_foreign_vpi_sysfunc_values_plugin)
# Virtual interface variables of generate scopes, packages and classes, arrays,
# typedefs and initializers (IEEE 1800-2023 37.29, 37.30).
add_library(fsim_foreign_vpi_virtual_more_plugin SHARED
  foreign_vpi_virtual_more_plugin.c)
fsim_configure_library(fsim_foreign_vpi_virtual_more_plugin)
target_link_libraries(fsim_foreign_vpi_virtual_more_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_virtual_more_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-virtual-more vpi foreign_vpi_virtual_more.sv
  foreign_vpi_virtual_more.expected ""
  fsim_foreign_vpi_virtual_more_plugin)
# The parameter assigns of an interface typespec: every parameter of the
# interface, the actuals evaluated where the type is written (IEEE 1800-2023
# 37.30, 37.32 detail 3).
add_library(fsim_foreign_vpi_virtual_params_plugin SHARED
  foreign_vpi_virtual_params_plugin.c)
fsim_configure_library(fsim_foreign_vpi_virtual_params_plugin)
target_link_libraries(fsim_foreign_vpi_virtual_params_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_virtual_params_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-virtual-params vpi foreign_vpi_virtual_params.sv
  foreign_vpi_virtual_params.expected ""
  fsim_foreign_vpi_virtual_params_plugin)
# A virtual interface type designates the interface of the library of the
# unit that writes it, not another library's interface of the same name
# (IEEE 1800-2023 3.13, 25.9, 33).
add_library(fsim_foreign_vpi_virtual_lib_plugin SHARED
  foreign_vpi_virtual_lib_plugin.c)
fsim_configure_library(fsim_foreign_vpi_virtual_lib_plugin)
target_link_libraries(fsim_foreign_vpi_virtual_lib_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_virtual_lib_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-virtual-lib vpi
  "foreign_vpi_virtual_lib_a.sv#liba|foreign_vpi_virtual_lib_b.sv#libb"
  foreign_vpi_virtual_lib.expected ""
  fsim_foreign_vpi_virtual_lib_plugin "" OFF "" TOP libb.cfg)
# The variables of a package: found by name, listed, read, written and
# watched (IEEE 1800-2023 37.10, 37.17, 37.29).
add_library(fsim_foreign_vpi_package_vars_plugin SHARED
  foreign_vpi_package_vars_plugin.c)
fsim_configure_library(fsim_foreign_vpi_package_vars_plugin)
target_link_libraries(fsim_foreign_vpi_package_vars_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_package_vars_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-package-vars vpi foreign_vpi_package_vars.sv
  foreign_vpi_package_vars.expected ""
  fsim_foreign_vpi_package_vars_plugin)
# Work a plug-in schedules from cbStartOfSimulation runs in the time-zero
# cycle, after static initialization (IEEE 1800-2023 38.36.3, 6.21, 10.5).
add_library(fsim_foreign_vpi_start_work_plugin SHARED
  foreign_vpi_start_work_plugin.c)
fsim_configure_library(fsim_foreign_vpi_start_work_plugin)
target_link_libraries(fsim_foreign_vpi_start_work_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_start_work_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-start-work vpi foreign_vpi_start_work.sv
  foreign_vpi_start_work.expected ""
  fsim_foreign_vpi_start_work_plugin)
# The drivers of nets driven by primitive outputs, continuous assignments and
# module output ports (IEEE 1800-2023 37.21, 37.35).
add_library(fsim_foreign_vpi_primitive_drivers_plugin SHARED
  foreign_vpi_primitive_drivers_plugin.c)
fsim_configure_library(fsim_foreign_vpi_primitive_drivers_plugin)
target_link_libraries(fsim_foreign_vpi_primitive_drivers_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_primitive_drivers_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-primitive-drivers vpi foreign_vpi_primitive_drivers.sv
  foreign_vpi_primitive_drivers.expected ""
  fsim_foreign_vpi_primitive_drivers_plugin)
# Gate instance arrays with parameter bounds (IEEE 1800-2023 28.3.6, 37.35).
add_library(fsim_foreign_vpi_gate_arrays_plugin SHARED
  foreign_vpi_gate_arrays_plugin.c)
fsim_configure_library(fsim_foreign_vpi_gate_arrays_plugin)
target_link_libraries(fsim_foreign_vpi_gate_arrays_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_gate_arrays_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-gate-arrays vpi foreign_vpi_gate_arrays.sv
  foreign_vpi_gate_arrays.expected ""
  fsim_foreign_vpi_gate_arrays_plugin)
# Stateful system function calls at the same line and column of two files.
add_library(fsim_foreign_vpi_sysfunc_files_plugin SHARED
  foreign_vpi_sysfunc_files_plugin.c)
fsim_configure_library(fsim_foreign_vpi_sysfunc_files_plugin)
target_link_libraries(fsim_foreign_vpi_sysfunc_files_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_sysfunc_files_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-sysfunc-paths vpi
  "foreign_vpi_sysfunc_files_a.sv|foreign_vpi_sysfunc_files_b.sv|lib_a/sysfunc_dir.sv>lib_a/sysfunc_dir.sv|lib_b/sysfunc_dir.sv>lib_b/sysfunc_dir.sv"
  foreign_vpi_sysfunc_files.expected "--foreign-probes"
  fsim_foreign_vpi_sysfunc_files_plugin)
# Reference objects: ref ports, their uses and selects, ref formals (IEEE
# 1800-2023 37.15).
add_library(fsim_foreign_vpi_ref_objs_plugin SHARED
  foreign_vpi_ref_objs_plugin.c)
fsim_configure_library(fsim_foreign_vpi_ref_objs_plugin)
target_link_libraries(fsim_foreign_vpi_ref_objs_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_ref_objs_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-ref-objs vpi foreign_vpi_ref_objs.sv foreign_vpi_ref_objs.expected
  "--foreign-probes" fsim_foreign_vpi_ref_objs_plugin)
# A select of a user function call's result (IEEE 1800-2023 37.59, 37.42).
add_library(fsim_foreign_vpi_call_select_plugin SHARED
  foreign_vpi_call_select_plugin.c)
fsim_configure_library(fsim_foreign_vpi_call_select_plugin)
target_link_libraries(fsim_foreign_vpi_call_select_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_call_select_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-call-select vpi foreign_vpi_call_select.sv
  foreign_vpi_call_select.expected "--foreign-probes"
  fsim_foreign_vpi_call_select_plugin)
fsim_add_foreign_conformance_test(
  vpi-call-select-noprobe vpi foreign_vpi_call_select.sv
  foreign_vpi_call_select_noprobe.expected ""
  fsim_foreign_vpi_call_select_plugin)
# Property and sequence declarations, formals, specs, instances and the
# operations of their expressions (IEEE 1800-2023 37.50-37.54).
add_library(fsim_foreign_vpi_properties_plugin SHARED
  foreign_vpi_properties_plugin.c)
fsim_configure_library(fsim_foreign_vpi_properties_plugin)
target_link_libraries(fsim_foreign_vpi_properties_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_properties_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-properties vpi foreign_vpi_properties.sv foreign_vpi_properties.expected
  "" fsim_foreign_vpi_properties_plugin)
# Let declarations and let expressions (IEEE 1800-2023 37.12, 37.57).
add_library(fsim_foreign_vpi_let_plugin SHARED foreign_vpi_let_plugin.c)
fsim_configure_library(fsim_foreign_vpi_let_plugin)
target_link_libraries(fsim_foreign_vpi_let_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_let_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-let vpi foreign_vpi_let.sv foreign_vpi_let.expected ""
  fsim_foreign_vpi_let_plugin)
# Expression values and vpiDecompile through the same dump: array query
# functions, string operations (IEEE 1800-2023 20.7, 6.16, 37.59).
fsim_add_foreign_conformance_test(
  vpi-expression-values vpi foreign_vpi_expression_values.sv
  foreign_vpi_expression_values.expected "" fsim_foreign_vpi_let_plugin)
# The match items of sequence operands (IEEE 1800-2023 16.10, 37.54).
add_library(fsim_foreign_vpi_match_items_plugin SHARED
  foreign_vpi_match_items_plugin.c)
fsim_configure_library(fsim_foreign_vpi_match_items_plugin)
target_link_libraries(fsim_foreign_vpi_match_items_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_match_items_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-match-items vpi foreign_vpi_match_items.sv
  foreign_vpi_match_items.expected "" fsim_foreign_vpi_match_items_plugin)
# Clocked properties, multiclock sequence expressions and case properties
# (IEEE 1800-2023 37.52, 37.56).
add_library(fsim_foreign_vpi_properties_clocked_plugin SHARED
  foreign_vpi_properties_clocked_plugin.c)
fsim_configure_library(fsim_foreign_vpi_properties_clocked_plugin)
target_link_libraries(fsim_foreign_vpi_properties_clocked_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_properties_clocked_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vpi-properties-clocked vpi foreign_vpi_properties_clocked.sv
  foreign_vpi_properties_clocked.expected "" fsim_foreign_vpi_properties_clocked_plugin)
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
# vhpiCbStmt on a concurrent procedure call whose body is in a package body.
add_library(fsim_foreign_vhpi_stmt_pack_plugin SHARED
  foreign_vhpi_stmt_pack_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_stmt_pack_plugin)
target_link_libraries(fsim_foreign_vhpi_stmt_pack_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_stmt_pack_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-stmt-pack vhpi foreign_vhpi_stmt_pack.vhd
  foreign_vhpi_stmt_pack.expected "--foreign-probes"
  fsim_foreign_vhpi_stmt_pack_plugin)
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
# vhpiDerefObj of a variable of an access type (IEEE 1076-2019 19.8, 22.3).
add_library(fsim_foreign_vhpi_deref_obj_plugin SHARED
  foreign_vhpi_deref_obj_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_deref_obj_plugin)
target_link_libraries(fsim_foreign_vhpi_deref_obj_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_deref_obj_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-deref-obj vhpi foreign_vhpi_deref_obj.vhd
  foreign_vhpi_deref_obj.expected "" fsim_foreign_vhpi_deref_obj_plugin)
# Protected type declarations, bodies and instances (IEEE 1076-2019 5.6,
# 19.4.3).
add_library(fsim_foreign_vhpi_protected_model_plugin SHARED
  foreign_vhpi_protected_model_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_protected_model_plugin)
target_link_libraries(fsim_foreign_vhpi_protected_model_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_protected_model_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-protected-model vhpi foreign_vhpi_protected_model.vhd
  foreign_vhpi_protected_model.expected "" fsim_foreign_vhpi_protected_model_plugin)
# The logical name of a file declaration (IEEE 1076-2019 6.4.2.5, 19.4.3).
add_library(fsim_foreign_vhpi_file_names_plugin SHARED
  foreign_vhpi_file_names_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_file_names_plugin)
target_link_libraries(fsim_foreign_vhpi_file_names_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_file_names_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-file-names vhpi foreign_vhpi_file_names.vhd
  foreign_vhpi_file_names.expected "" fsim_foreign_vhpi_file_names_plugin)
# vhpiGenerateIndexP of a for-generate whose range is not locally static
# (IEEE 1076-2019 11.8, 19.4.2).
add_library(fsim_foreign_vhpi_gen_index_dynamic_plugin SHARED
  foreign_vhpi_gen_index_dynamic_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_gen_index_dynamic_plugin)
target_link_libraries(fsim_foreign_vhpi_gen_index_dynamic_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_gen_index_dynamic_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-gen-index-dynamic vhpi foreign_vhpi_gen_index_dynamic.vhd
  foreign_vhpi_gen_index_dynamic.expected ""
  fsim_foreign_vhpi_gen_index_dynamic_plugin)
# Full names of the declarations a running call elaborated (IEEE 1076-2019
# 14.6 b, 19.4.3).
add_library(fsim_foreign_vhpi_frame_names_plugin SHARED
  foreign_vhpi_frame_names_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_frame_names_plugin)
target_link_libraries(fsim_foreign_vhpi_frame_names_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_frame_names_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-frame-names vhpi foreign_vhpi_frame_names.vhd
  foreign_vhpi_frame_names.expected "--foreign-probes"
  fsim_foreign_vhpi_frame_names_plugin)
# Transactions of a driver, their time, IsNull and value component (IEEE
# 1076-2019 14.7.2, 19.12.1).
add_library(fsim_foreign_vhpi_transactions_plugin SHARED
  foreign_vhpi_transactions_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_transactions_plugin)
target_link_libraries(fsim_foreign_vhpi_transactions_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_transactions_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-transactions vhpi foreign_vhpi_transactions.vhd
  foreign_vhpi_transactions.expected "" fsim_foreign_vhpi_transactions_plugin)
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
# VHDL-2019 INTEGER values through vhpiLongIntVal and vhpiLongIntVecVal
# (IEEE 1076-2019 5.2.3.1, 22.2.3, 22.4).
add_library(fsim_foreign_vhpi_long_int_plugin SHARED
  foreign_vhpi_long_int_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_long_int_plugin)
target_link_libraries(fsim_foreign_vhpi_long_int_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_long_int_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-long-int vhpi foreign_vhpi_long_int.vhd foreign_vhpi_long_int.expected
  "" fsim_foreign_vhpi_long_int_plugin "" "" "" COMPILE_ARGS "--standard 2019")
# VHDL units of different standards in one design. Units of one package
# family (1987-2002, 2008-2019) share one STD/IEEE environment, the newest
# standard's; a design with a VHDL-2019 unit has a 64-bit INTEGER for every
# unit, a VHDL-2008 unit's included (IEEE 1076-2019 5.2.3.1). Families do not
# mix (FSIM-ART-VHDEP-001): STD_LOGIC_VECTOR is a distinct type in 1993 and
# a subtype of STD_ULOGIC_VECTOR from 2008.
add_library(fsim_foreign_vhpi_mixed_std_plugin SHARED
  foreign_vhpi_mixed_std_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_mixed_std_plugin)
target_link_libraries(fsim_foreign_vhpi_mixed_std_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_mixed_std_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-mixed-std-2008-2019 vhpi
  "foreign_vhpi_mixed_std_leaf_2008.vhd@2008|foreign_vhpi_mixed_std_top_2019.vhd@2019"
  foreign_vhpi_mixed_std_2008_2019.expected "" fsim_foreign_vhpi_mixed_std_plugin)
fsim_add_foreign_conformance_test(
  vhpi-mixed-std-2008-alone vhpi
  "foreign_vhpi_mixed_std_leaf_2008.vhd@2008|foreign_vhpi_mixed_std_top_2008.vhd@2008"
  foreign_vhpi_mixed_std_2008_alone.expected "" fsim_foreign_vhpi_mixed_std_plugin)
fsim_add_foreign_conformance_test(
  vhpi-mixed-std-1993-2002 vhpi
  "foreign_vhpi_mixed_std_leaf_1993.vhd@1993|foreign_vhpi_mixed_std_top_2002.vhd@2002"
  foreign_vhpi_mixed_std_1993_2002.expected "" fsim_foreign_vhpi_mixed_std_plugin)
fsim_add_foreign_conformance_test(
  vhpi-mixed-std-1987-1993 vhpi
  "foreign_vhpi_mixed_std_leaf_1987.vhd@1987|foreign_vhpi_mixed_std_top_1993.vhd@1993"
  foreign_vhpi_mixed_std_1987_1993.expected "" fsim_foreign_vhpi_mixed_std_plugin)
fsim_add_foreign_conformance_test(
  vhpi-mixed-std-families vhpi
  "foreign_vhpi_mixed_std_leaf_2008.vhd@1993|foreign_vhpi_mixed_std_top_2008.vhd@2008"
  foreign_vhpi_mixed_std_2008_alone.expected "" fsim_foreign_vhpi_mixed_std_plugin
  "" "" "" ELABORATE_MESSAGE "units cannot be mixed: IEEE 1164/numeric_std changed type relations")
# A VHDL-2008 unit compiled alone still has no VHDL-2019 RANGE_DIRECTION.
fsim_add_foreign_conformance_test(
  vhpi-mixed-std-range-direction vhpi
  "foreign_vhpi_mixed_std_range_direction.vhd@2008"
  foreign_vhpi_mixed_std_2008_alone.expected "" fsim_foreign_vhpi_mixed_std_plugin
  "" "" "" COMPILE_MESSAGE
  "the predefined range_direction type requires VHDL-2019")
# A VHPI deposit of 'H' into a design with no weak value of its own: the
# kernel's registers carry it as the foreign library wrote it (IEEE 1164).
add_library(fsim_foreign_vhpi_weak_plugin SHARED
  foreign_vhpi_weak_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_weak_plugin)
target_link_libraries(fsim_foreign_vhpi_weak_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_weak_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-weak vhpi foreign_vhpi_weak.vhd foreign_vhpi_weak.expected ""
  fsim_foreign_vhpi_weak_plugin)
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
# A foreign architecture's signal has no source from the instance until a
# driver for it is created (IEEE 1076-2019 14.7.3.1, 20.4.2, 23.6).
add_library(fsim_foreign_vhpi_dormant_plugin SHARED
  foreign_vhpi_dormant_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_dormant_plugin)
target_link_libraries(fsim_foreign_vhpi_dormant_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_dormant_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-foreign-dormant vhpi foreign_vhpi_dormant.vhd
  foreign_vhpi_dormant.expected "" fsim_foreign_vhpi_dormant_plugin 0 OFF v4)
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
# Local loads and contributors of implicit signals (IEEE 1076-2019 16.2.4,
# 19.12.2.2, 19.12.2.3).
add_library(fsim_foreign_vhpi_implicit_loads_plugin SHARED
  foreign_vhpi_implicit_loads_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_implicit_loads_plugin)
target_link_libraries(fsim_foreign_vhpi_implicit_loads_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_implicit_loads_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-implicit-loads vhpi foreign_vhpi_implicit_loads.vhd
  foreign_vhpi_implicit_loads.expected ""
  fsim_foreign_vhpi_implicit_loads_plugin)
# Transaction and value-change callbacks on drivers of scalar subelements
# (IEEE 1076-2019 14.7.2, 14.7.3.1, 21.3.2.2, 21.3.2.5).
add_library(fsim_foreign_vhpi_driver_callbacks_plugin SHARED
  foreign_vhpi_driver_callbacks_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_driver_callbacks_plugin)
target_link_libraries(fsim_foreign_vhpi_driver_callbacks_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_driver_callbacks_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-driver-callbacks vhpi foreign_vhpi_driver_callbacks.vhd
  foreign_vhpi_driver_callbacks.expected ""
  fsim_foreign_vhpi_driver_callbacks_plugin)
# The drivers of a guarded block's unlabeled concurrent statements (IEEE
# 1076-2019 11.6, 14.7.2), past the block's implicit GUARD driver.
fsim_add_foreign_conformance_test(
  vhpi-guarded-drivers vhpi foreign_vhpi_guarded_drivers.vhd
  foreign_vhpi_guarded_drivers.expected ""
  fsim_foreign_vhpi_driver_callbacks_plugin)
# Forcing, releasing and watching variables (IEEE 1076-2019 22.5.2,
# 21.3.2.2-4).
add_library(fsim_foreign_vhpi_variable_force_plugin SHARED
  foreign_vhpi_variable_force_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_variable_force_plugin)
target_link_libraries(fsim_foreign_vhpi_variable_force_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_variable_force_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-variable-force vhpi foreign_vhpi_variable_force.vhd
  foreign_vhpi_variable_force.expected ""
  fsim_foreign_vhpi_variable_force_plugin)
# The frames of two call sites of one recursive procedure in a process, read
# through the call objects (IEEE 1076-2019 14.6 b).
add_library(fsim_foreign_vhpi_call_sites_plugin SHARED
  foreign_vhpi_call_sites_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_call_sites_plugin)
target_link_libraries(fsim_foreign_vhpi_call_sites_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_call_sites_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-call-sites vhpi foreign_vhpi_call_sites.vhd
  foreign_vhpi_call_sites.expected "--foreign-probes"
  fsim_foreign_vhpi_call_sites_plugin)
# The frames of call sites that run the same frame, or different ones, and of
# calls inside the body of another subprogram (IEEE 1076-2019 14.6 b).
add_library(fsim_foreign_vhpi_call_sites_shared_plugin SHARED
  foreign_vhpi_call_sites_shared_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_call_sites_shared_plugin)
target_link_libraries(fsim_foreign_vhpi_call_sites_shared_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_call_sites_shared_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-call-sites-shared vhpi foreign_vhpi_call_sites_shared.vhd
  foreign_vhpi_call_sites_shared.expected "--foreign-probes"
  fsim_foreign_vhpi_call_sites_shared_plugin)
add_library(fsim_foreign_vhpi_call_nested_plugin SHARED
  foreign_vhpi_call_nested_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_call_nested_plugin)
target_link_libraries(fsim_foreign_vhpi_call_nested_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_call_nested_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-call-nested vhpi foreign_vhpi_call_nested.vhd
  foreign_vhpi_call_nested.expected "--foreign-probes"
  fsim_foreign_vhpi_call_nested_plugin)
# The frames of calls of a package function from two processes, the second
# running the replayed body (IEEE 1076-2019 14.6 b).
add_library(fsim_foreign_vhpi_call_memo_plugin SHARED
  foreign_vhpi_call_memo_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_call_memo_plugin)
target_link_libraries(fsim_foreign_vhpi_call_memo_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_call_memo_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-call-memo vhpi foreign_vhpi_call_memo.vhd
  foreign_vhpi_call_memo.expected "--foreign-probes"
  fsim_foreign_vhpi_call_memo_plugin)
# A deposit of a 100-bit vector into the local of an outer activation of a
# recursive procedure (IEEE 1076-2019 14.6 b, 22.5.2).
add_library(fsim_foreign_vhpi_call_wide_plugin SHARED
  foreign_vhpi_call_wide_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_call_wide_plugin)
target_link_libraries(fsim_foreign_vhpi_call_wide_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_call_wide_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-call-wide vhpi foreign_vhpi_call_wide.vhd
  foreign_vhpi_call_wide.expected "--foreign-probes"
  fsim_foreign_vhpi_call_wide_plugin)
# Calls the simulation cycle makes (resolution functions) and calls of a
# concurrent assignment (IEEE 1076-2019 14.7.3.2, 19.4.3, 19.10).
add_library(fsim_foreign_vhpi_call_implicit_plugin SHARED
  foreign_vhpi_call_implicit_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_call_implicit_plugin)
target_link_libraries(fsim_foreign_vhpi_call_implicit_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_call_implicit_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-call-implicit vhpi foreign_vhpi_call_implicit.vhd
  foreign_vhpi_call_implicit.expected "--foreign-probes"
  fsim_foreign_vhpi_call_implicit_plugin)
# The parameter of a `for` loop of a process (IEEE 1076-2019 10.10, 14.6 a).
add_library(fsim_foreign_vhpi_process_loop_plugin SHARED
  foreign_vhpi_process_loop_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_process_loop_plugin)
target_link_libraries(fsim_foreign_vhpi_process_loop_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_process_loop_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-process-loop vhpi foreign_vhpi_process_loop.vhd
  foreign_vhpi_process_loop.expected "--foreign-probes"
  fsim_foreign_vhpi_process_loop_plugin)
# Transactions and changes of the first simulation cycle reach callbacks
# registered at the start of the simulation (IEEE 1076-2019 14.7.5.3,
# 21.3.2.5), also an assignment of the value a signal holds.
add_library(fsim_foreign_vhpi_transaction_time_zero_plugin SHARED
  foreign_vhpi_transaction_time_zero_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_transaction_time_zero_plugin)
target_link_libraries(
  fsim_foreign_vhpi_transaction_time_zero_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_transaction_time_zero_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-transaction-time-zero vhpi foreign_vhpi_transaction_time_zero.vhd
  foreign_vhpi_transaction_time_zero.expected ""
  fsim_foreign_vhpi_transaction_time_zero_plugin)
# The frame of a call of a procedure whose body is declared apart from its
# specification (IEEE 1076-2019 4.8, 14.6 b).
add_library(fsim_foreign_vhpi_call_body_apart_plugin SHARED
  foreign_vhpi_call_body_apart_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_call_body_apart_plugin)
target_link_libraries(fsim_foreign_vhpi_call_body_apart_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_call_body_apart_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-call-body-apart vhpi foreign_vhpi_call_body_apart.vhd
  foreign_vhpi_call_body_apart.expected "--foreign-probes"
  fsim_foreign_vhpi_call_body_apart_plugin)
# The frame of a concurrent procedure call (IEEE 1076-2019 11.4, 14.6 b).
add_library(fsim_foreign_vhpi_conc_call_frame_plugin SHARED
  foreign_vhpi_conc_call_frame_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_conc_call_frame_plugin)
target_link_libraries(fsim_foreign_vhpi_conc_call_frame_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_conc_call_frame_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-conc-call-frame vhpi foreign_vhpi_conc_call_frame.vhd
  foreign_vhpi_conc_call_frame.expected "--foreign-probes"
  fsim_foreign_vhpi_conc_call_frame_plugin)
# A force of a variable a subprogram call elaborated lasts as long as its
# activation (IEEE 1076-2019 14.6, 22.5.2).
add_library(fsim_foreign_vhpi_call_force_plugin SHARED
  foreign_vhpi_call_force_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_call_force_plugin)
target_link_libraries(fsim_foreign_vhpi_call_force_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_call_force_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-call-force vhpi foreign_vhpi_call_force.vhd
  foreign_vhpi_call_force.expected "--foreign-probes"
  fsim_foreign_vhpi_call_force_plugin)
# Value-change callbacks on the variables of a running process: the watch
# lasts until the last callback on a variable is removed, and a watch or a
# deposit made by a callback while the process runs takes effect at once
# (IEEE 1076-2019 21.3.2.2, 22.5.2).
add_library(fsim_foreign_vhpi_variable_hooks_plugin SHARED
  foreign_vhpi_variable_hooks_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_variable_hooks_plugin)
target_link_libraries(fsim_foreign_vhpi_variable_hooks_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_variable_hooks_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-variable-hooks vhpi foreign_vhpi_variable_hooks.vhd
  foreign_vhpi_variable_hooks.expected ""
  fsim_foreign_vhpi_variable_hooks_plugin)
# Generate statement occurrences that hold no signal, instance or process
# (IEEE 1076-2019 14.5.3, 19.4.2).
add_library(fsim_foreign_vhpi_empty_generate_plugin SHARED
  foreign_vhpi_empty_generate_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_empty_generate_plugin)
target_link_libraries(fsim_foreign_vhpi_empty_generate_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_empty_generate_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-empty-generate vhpi foreign_vhpi_empty_generate.vhd
  foreign_vhpi_empty_generate.expected ""
  fsim_foreign_vhpi_empty_generate_plugin)
# The bookkeeping behind 'LAST_VALUE adds no signal VHPI can list or name
# (IEEE 1076-2019 16.2.4, 23.2).
add_library(fsim_foreign_vhpi_history_hidden_plugin SHARED
  foreign_vhpi_history_hidden_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_history_hidden_plugin)
target_link_libraries(fsim_foreign_vhpi_history_hidden_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_history_hidden_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-history-hidden vhpi foreign_vhpi_history_hidden.vhd
  foreign_vhpi_history_hidden.expected ""
  fsim_foreign_vhpi_history_hidden_plugin)
# A VHPI deposit makes the signal active, also when it leaves the value (IEEE
# 1076-2019 14.7.3.1, 22.5.3).
add_library(fsim_foreign_vhpi_deposit_history_plugin SHARED
  foreign_vhpi_deposit_history_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_deposit_history_plugin)
target_link_libraries(fsim_foreign_vhpi_deposit_history_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_deposit_history_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-deposit-history vhpi foreign_vhpi_deposit_history.vhd
  foreign_vhpi_deposit_history.expected ""
  fsim_foreign_vhpi_deposit_history_plugin)
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
# Range bounds of enumeration, physical and floating point subtypes, groups,
# attribute specifications and configuration declarations (IEEE 1076-2019
# 19.4, 19.5).
add_library(fsim_foreign_vhpi_config_groups_plugin SHARED
  foreign_vhpi_config_groups_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_config_groups_plugin)
target_link_libraries(fsim_foreign_vhpi_config_groups_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_config_groups_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
# The configuration is the elaborated unit, so it is a design unit of the
# library the root instance's design unit belongs to.
fsim_add_foreign_conformance_test(
  vhpi-config-groups vhpi foreign_vhpi_config_groups.vhd
  foreign_vhpi_config_groups.expected "" fsim_foreign_vhpi_config_groups_plugin
  "" "" "" TOP cfg)
# Package bodies as design units, vhpiPrimaryUnit, vhpiDepUnits and vhpiUses
# (IEEE 1076-2019 19.4.5, Figure 4).
add_library(fsim_foreign_vhpi_pack_body_plugin SHARED
  foreign_vhpi_pack_body_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_pack_body_plugin)
target_link_libraries(fsim_foreign_vhpi_pack_body_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_pack_body_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-pack-body vhpi foreign_vhpi_pack_body.vhd
  foreign_vhpi_pack_body.expected "" fsim_foreign_vhpi_pack_body_plugin)
# The design units of a library include every unit analyzed into it, read from
# the workspace library catalog (IEEE 1076-2019 19.4.5).
add_library(fsim_foreign_vhpi_library_catalog_plugin SHARED
  foreign_vhpi_library_catalog_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_library_catalog_plugin)
target_link_libraries(fsim_foreign_vhpi_library_catalog_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_library_catalog_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-library-catalog vhpi foreign_vhpi_library_catalog.vhd
  foreign_vhpi_library_catalog.expected "" fsim_foreign_vhpi_library_catalog_plugin)
# The declarations of package STD.STANDARD (IEEE 1076-2019 16.3, 12.4,
# 19.4.5) for each VHDL standard: types, implicit operations, NOW and the
# attribute FOREIGN. The 2019 counts and checksum come from the 16.3 text; the
# earlier ones from the editions' revision history (scripts/
# gen_vhdl_standard_catalog.py).
add_library(fsim_foreign_vhpi_standard_catalog_plugin SHARED
  foreign_vhpi_standard_catalog_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_standard_catalog_plugin)
target_link_libraries(fsim_foreign_vhpi_standard_catalog_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_standard_catalog_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
foreach(FSIM_STANDARD_TAG 87 93 08 19)
  fsim_add_foreign_conformance_test(
    vhpi-standard-catalog-${FSIM_STANDARD_TAG} vhpi
    foreign_vhpi_standard_catalog.vhd
    foreign_vhpi_standard_catalog_${FSIM_STANDARD_TAG}.expected ""
    fsim_foreign_vhpi_standard_catalog_plugin "" "" ""
    COMPILE_ARGS "--standard ${FSIM_STANDARD_TAG}")
endforeach()
# A process holds a driver for each signal the procedures it calls assign
# directly (IEEE 1076-2019 14.7.2).
add_library(fsim_foreign_vhpi_callee_drivers_plugin SHARED
  foreign_vhpi_callee_drivers_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_callee_drivers_plugin)
target_link_libraries(fsim_foreign_vhpi_callee_drivers_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_callee_drivers_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-callee-drivers vhpi foreign_vhpi_callee_drivers.vhd
  foreign_vhpi_callee_drivers.expected "" fsim_foreign_vhpi_callee_drivers_plugin)
# A formal part of a port map that selects an element of a record port or of
# an element of an array port (IEEE 1076-2019 6.5.7.1, 19.12.2.2).
add_library(fsim_foreign_vhpi_formal_parts_plugin SHARED
  foreign_vhpi_formal_parts_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_formal_parts_plugin)
target_link_libraries(fsim_foreign_vhpi_formal_parts_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_formal_parts_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-formal-parts vhpi foreign_vhpi_formal_parts.vhd
  foreign_vhpi_formal_parts.expected "" fsim_foreign_vhpi_formal_parts_plugin)
# A signal parameter passed on by the body of one subprogram to another
# (IEEE 1076-2019 4.2.2.3, 19.12.2.2 c).
add_library(fsim_foreign_vhpi_nested_params_plugin SHARED
  foreign_vhpi_nested_params_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_nested_params_plugin)
target_link_libraries(fsim_foreign_vhpi_nested_params_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_nested_params_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-nested-params vhpi foreign_vhpi_nested_params.vhd
  foreign_vhpi_nested_params.expected "--foreign-probes"
  fsim_foreign_vhpi_nested_params_plugin)
# Operators with literal operands, which the design folds, stay calls of the
# predefined operator in the static model (IEEE 1076-2019 19.8, 9.4.2).
add_library(fsim_foreign_vhpi_folded_operators_plugin SHARED
  foreign_vhpi_folded_operators_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_folded_operators_plugin)
target_link_libraries(fsim_foreign_vhpi_folded_operators_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_folded_operators_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-folded-operators vhpi foreign_vhpi_folded_operators.vhd
  foreign_vhpi_folded_operators.expected ""
  fsim_foreign_vhpi_folded_operators_plugin)
# The value of a shared variable of a VHDL-93 design (IEEE 1076-1993 4.3.1.3,
# IEEE 1076-2019 22.3).
add_library(fsim_foreign_vhpi_shared_variable_plugin SHARED
  foreign_vhpi_shared_variable_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_shared_variable_plugin)
target_link_libraries(fsim_foreign_vhpi_shared_variable_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_shared_variable_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-shared-variable vhpi foreign_vhpi_shared_variable.vhd
  foreign_vhpi_shared_variable.expected ""
  fsim_foreign_vhpi_shared_variable_plugin "" "" "" COMPILE_ARGS "--standard 93")
# A shared variable of a VHDL-93 design is read and updated while it runs
# (IEEE 1076-1993 4.3.1.3, IEEE 1076-2019 22.5.2).
add_library(fsim_foreign_vhpi_shared_variable_live_plugin SHARED
  foreign_vhpi_shared_variable_live_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_shared_variable_live_plugin)
target_link_libraries(
  fsim_foreign_vhpi_shared_variable_live_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_shared_variable_live_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-shared-variable-live vhpi foreign_vhpi_shared_variable_live.vhd
  foreign_vhpi_shared_variable_live.expected ""
  fsim_foreign_vhpi_shared_variable_live_plugin "" "" "" COMPILE_ARGS "--standard 93")
# The port map of a binding indication with parts of ports as formal and
# actual parts (IEEE 1076-2019 7.3.2.3, 6.5.7.1).
add_library(fsim_foreign_vhpi_binding_parts_plugin SHARED
  foreign_vhpi_binding_parts_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_binding_parts_plugin)
target_link_libraries(fsim_foreign_vhpi_binding_parts_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_binding_parts_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-binding-parts vhpi foreign_vhpi_binding_parts.vhd
  foreign_vhpi_binding_parts.expected "" fsim_foreign_vhpi_binding_parts_plugin)
# The port map of a binding indication with a conversion function in the
# actual part and in the formal part (IEEE 1076-2019 7.3.2.1, 6.5.7.1,
# 14.5.4, 19.12.2.2).
add_library(fsim_foreign_vhpi_binding_conversions_plugin SHARED
  foreign_vhpi_binding_conversions_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_binding_conversions_plugin)
target_link_libraries(
  fsim_foreign_vhpi_binding_conversions_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_binding_conversions_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-binding-conversions vhpi foreign_vhpi_binding_conversions.vhd
  foreign_vhpi_binding_conversions.expected ""
  fsim_foreign_vhpi_binding_conversions_plugin)
# One component default-bound and bound with a binding port map: the same
# model of both instances (IEEE 1076-2019 7.3.2.1, 7.3.3, 14.5.4).
add_library(fsim_foreign_vhpi_binding_uniform_plugin SHARED
  foreign_vhpi_binding_uniform_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_binding_uniform_plugin)
target_link_libraries(fsim_foreign_vhpi_binding_uniform_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_binding_uniform_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-binding-uniform vhpi foreign_vhpi_binding_uniform.vhd
  foreign_vhpi_binding_uniform.expected ""
  fsim_foreign_vhpi_binding_uniform_plugin)
# The formals and locals of a running function call and the parameter of a
# `for` loop of a running procedure (IEEE 1076-2019 14.6 a, b, 22.3).
add_library(fsim_foreign_vhpi_frame_locals_plugin SHARED
  foreign_vhpi_frame_locals_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_frame_locals_plugin)
target_link_libraries(fsim_foreign_vhpi_frame_locals_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_frame_locals_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-frame-locals vhpi foreign_vhpi_frame_locals.vhd
  foreign_vhpi_frame_locals.expected "--foreign-probes"
  fsim_foreign_vhpi_frame_locals_plugin)
# The design unit classes of the library model: vhpiDecls, vhpiGenericDecls,
# vhpiPortDecls, vhpiStmts and vhpiDepUnits of entity declarations,
# architecture bodies and packages, and the library STD (IEEE 1076-2019
# 19.4.5, 12.4, Figure 4).
add_library(fsim_foreign_vhpi_unit_model_plugin SHARED
  foreign_vhpi_unit_model_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_unit_model_plugin)
target_link_libraries(fsim_foreign_vhpi_unit_model_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_unit_model_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-unit-model vhpi foreign_vhpi_unit_model.vhd
  foreign_vhpi_unit_model.expected "" fsim_foreign_vhpi_unit_model_plugin)
# vhpiGenIndex of a block configuration whose index is a static expression of
# constants (IEEE 1076-2019 3.4.2, 19.5).
add_library(fsim_foreign_vhpi_gen_index_plugin SHARED
  foreign_vhpi_gen_index_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_gen_index_plugin)
target_link_libraries(fsim_foreign_vhpi_gen_index_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_gen_index_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-gen-index vhpi foreign_vhpi_gen_index.vhd
  foreign_vhpi_gen_index.expected "" fsim_foreign_vhpi_gen_index_plugin
  "" "" "" TOP cfg)
# vhpiResolFunc of an STD_LOGIC signal (the function RESOLVED), a user
# function and an unresolved signal (IEEE 1076-2019 19.4.3, 16.7).
add_library(fsim_foreign_vhpi_resol_func_plugin SHARED
  foreign_vhpi_resol_func_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_resol_func_plugin)
target_link_libraries(fsim_foreign_vhpi_resol_func_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_resol_func_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-resol-func vhpi foreign_vhpi_resol_func.vhd
  foreign_vhpi_resol_func.expected "" fsim_foreign_vhpi_resol_func_plugin)
# Disconnection specifications as objects of class disconnectSpec (IEEE
# 1076-2019 7.4, Figure 32).
add_library(fsim_foreign_vhpi_disconnect_plugin SHARED
  foreign_vhpi_disconnect_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_disconnect_plugin)
target_link_libraries(fsim_foreign_vhpi_disconnect_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_disconnect_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-disconnect vhpi foreign_vhpi_disconnect.vhd
  foreign_vhpi_disconnect.expected "" fsim_foreign_vhpi_disconnect_plugin)
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

# The initialization phase of a VHDL top level over a SystemVerilog leaf
# (IEEE 1076-2019 14.7.5.2, 14.7.5.3, 21.3.7.5, 21.3.7.6): the callbacks that
# open the simulation see the initial values, as in a VHDL-only design.
add_library(fsim_foreign_vhpi_mixed_init_plugin SHARED
  foreign_vhpi_mixed_init_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_mixed_init_plugin)
target_link_libraries(fsim_foreign_vhpi_mixed_init_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_mixed_init_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
fsim_add_foreign_conformance_test(
  vhpi-mixed-init vhpi
  "foreign_vhpi_mixed_init.sv|foreign_vhpi_mixed_init.vhd"
  foreign_vhpi_mixed_init.expected "" fsim_foreign_vhpi_mixed_init_plugin)

# Save and restart (IEEE 1800-2023 D.9) on the compiled kernel and the
# reference interpreter: `simulate --save-at/--save-file` and `--restart`, and
# $save and $incsave from the design. The saving run prints what an
# uninterrupted run prints; a restart prints the rest. A restart under another
# command line, from a damaged file or from a missing base is refused.
set(FSIM_SAVE_RESTART_FIXTURES
  "${PROJECT_SOURCE_DIR}/tests/fixtures/systemverilog/save_restart")
function(fsim_add_save_restart_test FSIM_NAME FSIM_ENGINE FSIM_SOURCE
    FSIM_EXPECTED)
  cmake_parse_arguments(PARSE_ARGV 4 FSIM_OPTION "TRACE_ONLY;ELABORATE_PLUGIN"
    "TOP" "SAVE_ARGS;RESTARTS;REJECT;COMMON_ARGS;COMPILE_ARGS")
  if(NOT FSIM_OPTION_TOP)
    set(FSIM_OPTION_TOP top)
  endif()
  list(JOIN FSIM_OPTION_COMPILE_ARGS "," FSIM_COMPILE_ARGS_TEXT)
  # Lists reach the runner joined by commas, so no semicolon is split.
  list(JOIN FSIM_OPTION_SAVE_ARGS "," FSIM_SAVE_ARGS_TEXT)
  list(JOIN FSIM_OPTION_RESTARTS "," FSIM_RESTARTS_TEXT)
  list(JOIN FSIM_OPTION_REJECT "," FSIM_REJECT_TEXT)
  list(JOIN FSIM_OPTION_COMMON_ARGS "," FSIM_COMMON_ARGS_TEXT)
  add_test(
    NAME fsim.systemverilog.save-restart.${FSIM_NAME}.${FSIM_ENGINE}
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
      "-DFSIM_SOURCE=${FSIM_SAVE_RESTART_FIXTURES}/${FSIM_SOURCE}"
      "-DFSIM_TOP=${FSIM_OPTION_TOP}"
      "-DFSIM_COMPILE_ARGS=${FSIM_COMPILE_ARGS_TEXT}"
      "-DFSIM_ENGINE=${FSIM_ENGINE}"
      "-DFSIM_EXPECTED=${FSIM_SAVE_RESTART_FIXTURES}/${FSIM_EXPECTED}"
      "-DFSIM_SAVE_ARGS=${FSIM_SAVE_ARGS_TEXT}"
      "-DFSIM_RESTARTS=${FSIM_RESTARTS_TEXT}"
      "-DFSIM_REJECT=${FSIM_REJECT_TEXT}"
      "-DFSIM_COMMON_ARGS=${FSIM_COMMON_ARGS_TEXT}"
      "-DFSIM_TRACE_ONLY=${FSIM_OPTION_TRACE_ONLY}"
      "-DFSIM_ELABORATE_PLUGIN=${FSIM_OPTION_ELABORATE_PLUGIN}"
      "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/save-restart/${FSIM_NAME}-${FSIM_ENGINE}"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunSaveRestartCase.cmake")
  set_tests_properties(
    fsim.systemverilog.save-restart.${FSIM_NAME}.${FSIM_ENGINE}
    PROPERTIES LABELS "fast;systemverilog;save-restart")
endfunction()
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  fsim_add_save_restart_test(cli ${FSIM_ENGINE}
    save_restart_cli.sv save_restart_cli.expected
    SAVE_ARGS --save-at 50ns --save-file cli.state
    RESTARTS
      "cli.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_cli.restart.expected")
  # Nets of user-defined nettypes (IEEE 1800-2023 6.6.7): a save at the end
  # of time zero holds their time-zero resolutions, which a restart does not
  # repeat.
  fsim_add_save_restart_test(nettype ${FSIM_ENGINE}
    save_restart_nettype.sv save_restart_nettype.expected
    SAVE_ARGS --save-at 0ns --save-file nettype.state
    RESTARTS
      "nettype.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_nettype.restart.expected")
  # Associative arrays with a default (IEEE 1800-2023 7.9.11): the default
  # from a declaration and one assigned before the save, read after a
  # restart.
  fsim_add_save_restart_test(assoc-default ${FSIM_ENGINE}
    save_restart_assoc_default.sv save_restart_assoc_default.expected
    SAVE_ARGS --save-at 50ns --save-file assoc_default.state
    RESTARTS
      "assoc_default.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_assoc_default.restart.expected")
  # A live object of a class specialization bound to a pooled constant
  # (8.25, 6.22.1) survives a save and a restart in a fresh process.
  fsim_add_save_restart_test(pool-class ${FSIM_ENGINE}
    save_restart_pool_class.sv save_restart_pool_class.expected
    SAVE_ARGS --save-at 50ns --save-file pool_class.state
    RESTARTS
      "pool_class.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_pool_class.restart.expected")
  fsim_add_save_restart_test(task ${FSIM_ENGINE}
    save_restart_task.sv save_restart_task.expected
    RESTARTS
      "task_full.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_task.full.expected"
      "task_inc.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_task.inc.expected")
  # --save-at without --save-file, and a restart from a file that is no
  # run-state file, are refused.
  fsim_add_save_restart_test(refused ${FSIM_ENGINE}
    save_restart_cli.sv save_restart_cli.expected
    SAVE_ARGS --save-at 50ns --save-file cli.state
    REJECT
      "--save-at|50ns=FSIM-SAVE-0020"
      "--restart|save_restart_cli.sv=FSIM-SAVE-0015")
  # The history of a kernel-owned VHDL signal (IEEE 1076-2019 16.2.4, 20.7):
  # 'LAST_EVENT, 'LAST_ACTIVE and 'LAST_VALUE read after a restart count from
  # the events and transactions before the save.
  fsim_add_save_restart_test(vhdl-history ${FSIM_ENGINE}
    save_restart_vhdl_history.vhd save_restart_vhdl_history.expected
    TOP work.top COMPILE_ARGS --standard 2008
    SAVE_ARGS --save-at 20ns --save-file vhdl_history.state
    RESTARTS
      "vhdl_history.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_vhdl_history.restart.expected")
endforeach()

# Foreign libraries across a save and a restart (IEEE 1800-2023 38.9, 38.31,
# 38.36.3; IEEE 1076-2019 20.7, 20.8; IEEE 1364-2001 25.41, 25.65): the saving
# run and a fresh-process restart each load the library, and the trace of its
# "#c " lines is compared.
add_library(fsim_foreign_vpi_save_session_plugin SHARED
  foreign_vpi_save_session_plugin.c)
fsim_configure_library(fsim_foreign_vpi_save_session_plugin)
target_link_libraries(fsim_foreign_vpi_save_session_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_save_session_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(fsim_foreign_vhpi_save_session_plugin SHARED
  foreign_vhpi_save_session_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_save_session_plugin)
target_link_libraries(fsim_foreign_vhpi_save_session_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_save_session_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(fsim_foreign_vhpi_save_variable_plugin SHARED
  foreign_vhpi_save_variable_plugin.c)
fsim_configure_library(fsim_foreign_vhpi_save_variable_plugin)
target_link_libraries(fsim_foreign_vhpi_save_variable_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vhpi_save_variable_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(fsim_foreign_pli_tf_save_plugin SHARED
  foreign_pli_tf_save_plugin.c)
fsim_configure_library(fsim_foreign_pli_tf_save_plugin)
target_link_libraries(fsim_foreign_pli_tf_save_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_pli_tf_save_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  fsim_add_save_restart_test(foreign-vpi ${FSIM_ENGINE}
    save_restart_foreign.sv save_restart_foreign_vpi.expected TRACE_ONLY
    COMMON_ARGS --vpi $<TARGET_FILE:fsim_foreign_vpi_save_session_plugin>
    RESTARTS
      "foreign.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_foreign_vpi.restart.expected")
  fsim_add_save_restart_test(foreign-pli-tf ${FSIM_ENGINE}
    save_restart_foreign_tf.sv save_restart_foreign_tf.expected TRACE_ONLY
    ELABORATE_PLUGIN
    COMMON_ARGS --vpi $<TARGET_FILE:fsim_foreign_pli_tf_save_plugin>
    RESTARTS
      "foreign.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_foreign_tf.restart.expected")
  fsim_add_save_restart_test(foreign-vhpi ${FSIM_ENGINE}
    save_restart_foreign.vhd save_restart_foreign_vhpi.expected TRACE_ONLY
    COMMON_ARGS --vhpi $<TARGET_FILE:fsim_foreign_vhpi_save_session_plugin>
    SAVE_ARGS --save-at 50ns --save-file foreign.state
    RESTARTS
      "foreign.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_foreign_vhpi.restart.expected")
  # A variable forced through VHPI stays forced across a save and a restart
  # (IEEE 1076-2019 22.5.2): the force is state of the simulation.
  fsim_add_save_restart_test(foreign-vhpi-variable ${FSIM_ENGINE}
    save_restart_foreign_variable.vhd save_restart_foreign_variable.expected
    TRACE_ONLY
    COMMON_ARGS --vhpi $<TARGET_FILE:fsim_foreign_vhpi_save_variable_plugin>
    SAVE_ARGS --save-at 50ns --save-file variable.state
    RESTARTS
      "variable.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_foreign_variable.restart.expected")
  # $reset(1, 7): the design starts over at time zero with the reset count and
  # value kept (IEEE 1800-2023 D.8).
  fsim_add_save_restart_test(reset ${FSIM_ENGINE}
    reset_task.sv reset_task.expected)
  # A package initializer that prints runs on the reset's twin with its output
  # discarded: "pkg init" appears once.
  fsim_add_save_restart_test(reset-package-init ${FSIM_ENGINE}
    reset_package_init.sv reset_package_init.expected)
  # $reset of a design whose initial procedure starts in the compiled
  # kernel's late activation of time zero: it starts again after the reset.
  fsim_add_save_restart_test(reset-late-start ${FSIM_ENGINE}
    reset_late_start.sv reset_late_start.expected TRACE_ONLY)
endforeach()

# A delayed vpi_put_value that is still pending at the save lands at its time
# in a fresh-process restart (IEEE 1800-2023 38.34, D.9); $reset after a watch
# registered once the run had started (the reset's twin takes the same plan,
# D.8); and a save of a run with concurrent assertion attempts in flight,
# whose attempt storage (task #186) is saved and restored with the run.
add_library(fsim_foreign_vpi_save_write_plugin SHARED
  foreign_vpi_save_write_plugin.c)
fsim_configure_library(fsim_foreign_vpi_save_write_plugin)
target_link_libraries(fsim_foreign_vpi_save_write_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_save_write_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(fsim_foreign_vpi_reset_watch_plugin SHARED
  foreign_vpi_reset_watch_plugin.c)
fsim_configure_library(fsim_foreign_vpi_reset_watch_plugin)
target_link_libraries(fsim_foreign_vpi_reset_watch_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_reset_watch_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
# A gate split by vpi_put_delays (IEEE 1800-2023 38.32, 28.16) across a
# fresh-process restart (the saved plan is replayed before the state is
# restored, and the delay readbacks come back with the foreign.vpi section) and
# across $reset (the twin takes the same split).
add_library(fsim_foreign_vpi_save_gate_plugin SHARED
  foreign_vpi_save_gate_plugin.c)
fsim_configure_library(fsim_foreign_vpi_save_gate_plugin)
target_link_libraries(fsim_foreign_vpi_save_gate_plugin PRIVATE fsim_tf)
target_include_directories(
  fsim_foreign_vpi_save_gate_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  fsim_add_save_restart_test(vpi-scheduled-write ${FSIM_ENGINE}
    save_restart_vpi_write.sv save_restart_vpi_write.expected TRACE_ONLY
    COMMON_ARGS --vpi $<TARGET_FILE:fsim_foreign_vpi_save_write_plugin>
    RESTARTS
      "write.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_vpi_write.restart.expected")
  fsim_add_save_restart_test(reset-after-watch ${FSIM_ENGINE}
    reset_watch.sv reset_watch.expected TRACE_ONLY
    COMMON_ARGS --vpi $<TARGET_FILE:fsim_foreign_vpi_reset_watch_plugin>)
  fsim_add_save_restart_test(gate-split ${FSIM_ENGINE}
    save_restart_gate_split.sv save_restart_gate_split.expected TRACE_ONLY
    COMMON_ARGS --vpi $<TARGET_FILE:fsim_foreign_vpi_save_gate_plugin>
    RESTARTS
      "gate.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_gate_split.restart.expected")
  fsim_add_save_restart_test(reset-gate-split ${FSIM_ENGINE}
    reset_gate_split.sv reset_gate_split.expected TRACE_ONLY
    COMMON_ARGS --vpi $<TARGET_FILE:fsim_foreign_vpi_save_gate_plugin>)
  fsim_add_save_restart_test(sva-attempts ${FSIM_ENGINE}
    save_restart_sva.sv save_restart_sva.expected
    SAVE_ARGS --save-at 30ns --save-file sva.state
    RESTARTS
      "sva.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_sva.restart.expected")
  # The monitor a disable net disables is saved: after the restart the
  # disable iff still ends the attempt in flight between the edges (16.12).
  fsim_add_save_restart_test(sva-disable ${FSIM_ENGINE}
    save_restart_sva_disable.sv save_restart_sva_disable.expected
    SAVE_ARGS --save-at 30ns --save-file sva_disable.state
    RESTARTS
      "sva_disable.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_sva_disable.restart.expected")
  # A save while attempts of a property with local variables are in flight
  # (IEEE 1800-2023 16.10): the threads' locals are saved with the monitor,
  # and the restart's attempts pass and fail on the same values.
  fsim_add_save_restart_test(sva-locals ${FSIM_ENGINE}
    save_restart_sva_locals.sv save_restart_sva_locals.expected
    SAVE_ARGS --save-at 30ns --save-file sva_locals.state
    RESTARTS
      "sva_locals.state=${FSIM_SAVE_RESTART_FIXTURES}/save_restart_sva_locals.restart.expected")
endforeach()

# A UVM testbench across $save, $restart and $reset (IEEE 1800-2023 D.8, D.9;
# task #188). The services' state (components, phases, objections,
# configuration, report counts, the sequencer handshake) is written in the
# "app.uvm" section; the governed UVM library is compiled once per release, by
# the release's uvm-shared-library fixture, into a library every test of that
# release maps. The source saves at t=1 and t=18 of the run phase, with the
# objection raised and the sequence running, and each restart must print the
# rest of the uninterrupted run; the reset test restarts the whole test at t=18
# and must print a fresh run's report.
function(fsim_add_uvm_save_restart_test FSIM_NAME FSIM_ENGINE FSIM_RELEASE
    FSIM_ROOT FSIM_SOURCE FSIM_EXPECTED)
  cmake_parse_arguments(
    PARSE_ARGV 6 FSIM_OPTION "NORMALIZE_IDS;NO_EXECUTOR" "" "RESTARTS")
  list(JOIN FSIM_OPTION_RESTARTS "," FSIM_RESTARTS_TEXT)
  set(FSIM_TEST_NAME
    fsim.systemverilog.save-restart.${FSIM_NAME}-${FSIM_RELEASE}.${FSIM_ENGINE})
  string(MAKE_C_IDENTIFIER "fsim_uvm_library_${FSIM_RELEASE}" FSIM_LIBRARY_FIXTURE)
  add_test(
    NAME ${FSIM_TEST_NAME}
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
      "-DFSIM_SOURCE=${FSIM_SAVE_RESTART_FIXTURES}/${FSIM_SOURCE}"
      "-DFSIM_TOP=top"
      "-DFSIM_ENGINE=${FSIM_ENGINE}"
      "-DFSIM_EXPECTED=${FSIM_SAVE_RESTART_FIXTURES}/${FSIM_EXPECTED}"
      "-DFSIM_RESTARTS=${FSIM_RESTARTS_TEXT}"
      "-DFSIM_UVM_ROOT=${FSIM_ROOT}"
      "-DFSIM_UVM_RELEASE=${FSIM_RELEASE}"
      "-DFSIM_NORMALIZE_IDS=${FSIM_OPTION_NORMALIZE_IDS}"
      "-DFSIM_NO_EXECUTOR=${FSIM_OPTION_NO_EXECUTOR}"
      "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/save-restart/${FSIM_NAME}-${FSIM_RELEASE}-${FSIM_ENGINE}"
      "-DFSIM_UVM_LIBRARY_DIR=${PROJECT_BINARY_DIR}/tests/save-restart/uvm-shared-library-${FSIM_RELEASE}"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunUvmSaveRestartCase.cmake")
  set_tests_properties(${FSIM_TEST_NAME}
    PROPERTIES LABELS "slow;systemverilog;uvm;save-restart" TIMEOUT 1800
      FIXTURES_REQUIRED "${FSIM_LIBRARY_FIXTURE}")
endfunction()
# Each governed release, with its library compiled on its own and mapped
# (the separate-library flow of the corpus runner). The native services print
# the report, so the expected text is the same for both releases.
foreach(FSIM_UVM_RELEASE_NAME IN ITEMS 1.2 2020.3.1)
  if(FSIM_UVM_RELEASE_NAME STREQUAL "1.2")
    set(FSIM_UVM_RELEASE_ROOT "${FSIM_UVM_1_2_SOURCE_DIR}")
  else()
    set(FSIM_UVM_RELEASE_ROOT "${FSIM_UVM_2020_3_1_SOURCE_DIR}")
  endif()
  if(NOT FSIM_UVM_RELEASE_ROOT)
    continue()
  endif()
  # The release's library, compiled once for all of its cases.
  string(MAKE_C_IDENTIFIER "fsim_uvm_library_${FSIM_UVM_RELEASE_NAME}"
    FSIM_LIBRARY_FIXTURE)
  add_test(
    NAME fsim.systemverilog.save-restart.uvm-shared-library-${FSIM_UVM_RELEASE_NAME}
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
      "-DFSIM_UVM_ROOT=${FSIM_UVM_RELEASE_ROOT}"
      "-DFSIM_UVM_RELEASE=${FSIM_UVM_RELEASE_NAME}"
      "-DFSIM_UVM_LIBRARY_ONLY=ON"
      "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/save-restart/uvm-shared-library-${FSIM_UVM_RELEASE_NAME}"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunUvmSaveRestartCase.cmake")
  set_tests_properties(
    fsim.systemverilog.save-restart.uvm-shared-library-${FSIM_UVM_RELEASE_NAME}
    PROPERTIES LABELS "slow;systemverilog;uvm;save-restart" TIMEOUT 1800
      FIXTURES_SETUP "${FSIM_LIBRARY_FIXTURE}")
  foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
    fsim_add_uvm_save_restart_test(uvm ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_save_restart.sv uvm_save_restart.expected
      RESTARTS
        "early.state=${FSIM_SAVE_RESTART_FIXTURES}/uvm_save_restart.early.expected"
        "late.state=${FSIM_SAVE_RESTART_FIXTURES}/uvm_save_restart.late.expected")
    fsim_add_uvm_save_restart_test(uvm-reset ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_reset.sv uvm_reset.expected)
    fsim_add_uvm_save_restart_test(uvm-root-queues ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_root_queues.sv uvm_root_queues.expected)
    fsim_add_uvm_save_restart_test(uvm-local-queues ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_local_queues.sv uvm_local_queues.expected)
    fsim_add_uvm_save_restart_test(uvm-module-queues ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_module_queues.sv uvm_module_queues.expected)
    # uvm_top itself, called from module code.
    fsim_add_uvm_save_restart_test(uvm-module-queues-top ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_module_queues_top.sv uvm_module_queues_top.expected)
    fsim_add_uvm_save_restart_test(uvm-root-methods ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_root_methods.sv uvm_root_methods.expected NORMALIZE_IDS)
    # print_topology with a tree, a line and a derived tree printer.
    fsim_add_uvm_save_restart_test(uvm-printer-kinds ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_printer_kinds.sv uvm_printer_kinds.expected NORMALIZE_IDS)
    fsim_add_uvm_save_restart_test(uvm-singleton-getters ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_singleton_getters.sv uvm_singleton_getters.expected)
    # The constructors of classes deriving from the library, run by the
    # creating process: run_test, factory creates and `new`.
    fsim_add_uvm_save_restart_test(uvm-constructors ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_constructors.sv uvm_constructors.expected)
    # Functions of classes deriving from the library run as compiled code:
    # the host class executor runs none of the design's class code.
    fsim_add_uvm_save_restart_test(uvm-user-functions ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_user_functions.sv uvm_user_functions.expected NO_EXECUTOR)
    # A user driver calls methods on `req`, the REQ property it inherits from
    # uvm_driver #(pkt): the receiver's class is the one the extends clause
    # binds REQ to (IEEE 1800-2023 8.25, 8.13).
    fsim_add_uvm_save_restart_test(uvm-driver-req ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_driver_req.sv uvm_driver_req.expected NO_EXECUTOR)
  endforeach()
endforeach()
# The separate-library flow alone, UVM 1.2: uvm_top = uvm_root::get() and the
# other singletons of the library are the native services' objects.
if(FSIM_UVM_1_2_SOURCE_DIR)
  foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
    fsim_add_uvm_save_restart_test(uvm-library ${FSIM_ENGINE} 1.2
      "${FSIM_UVM_1_2_SOURCE_DIR}" uvm_library.sv uvm_library.expected)
    # uvm_top and its siblings, read directly: UVM 1.2 initializes them with
    # their getters.
    fsim_add_uvm_save_restart_test(uvm-singleton-variables ${FSIM_ENGINE} 1.2
      "${FSIM_UVM_1_2_SOURCE_DIR}" uvm_singleton_variables.sv
      uvm_singleton_variables.expected)
  endforeach()
endif()
# Factory type overrides applied by type_id::create, with the constructors of
# the overriding classes run by the creating process. uvm_factory::get() reads
# the core service the library keeps (IEEE 1800.2-2020 F.4.1.3), which is the
# default one of the native services in both releases.
foreach(FSIM_UVM_RELEASE_NAME IN ITEMS 1.2 2020.3.1)
  if(FSIM_UVM_RELEASE_NAME STREQUAL "1.2")
    set(FSIM_UVM_RELEASE_ROOT "${FSIM_UVM_1_2_SOURCE_DIR}")
  else()
    set(FSIM_UVM_RELEASE_ROOT "${FSIM_UVM_2020_3_1_SOURCE_DIR}")
  endif()
  if(NOT FSIM_UVM_RELEASE_ROOT)
    continue()
  endif()
  foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
    fsim_add_uvm_save_restart_test(uvm-factory-overrides ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_factory_overrides.sv uvm_factory_overrides.expected)
    # A create through a parameterized class's type_id makes the
    # specialization the type expression denotes.
    fsim_add_uvm_save_restart_test(uvm-param-registry-create ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_param_registry_create.sv uvm_param_registry_create.expected)
    # The bare name denotes the default specialization (IEEE 1800-2023 8.25).
    fsim_add_uvm_save_restart_test(uvm-param-registry-bare ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_param_registry_bare.sv uvm_param_registry_bare.expected)
    # The create is recognized by what X's type_id denotes, not by its
    # spelling: a typedef of a class, and a user class with its own type_id.
    fsim_add_uvm_save_restart_test(uvm-registry-typedef-nested ${FSIM_ENGINE}
      ${FSIM_UVM_RELEASE_NAME} "${FSIM_UVM_RELEASE_ROOT}"
      uvm_registry_typedef_nested.sv uvm_registry_typedef_nested.expected)
  endforeach()
endforeach()
# 1800.2-2020.3.1 declares uvm_top and uvm_cmdline_proc as plain variables
# that the getters assign.
if(FSIM_UVM_2020_3_1_SOURCE_DIR)
  foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
    fsim_add_uvm_save_restart_test(uvm-singleton-variables ${FSIM_ENGINE}
      2020.3.1 "${FSIM_UVM_2020_3_1_SOURCE_DIR}"
      uvm_singleton_variables_2020.sv uvm_singleton_variables_2020.expected)
  endforeach()
endif()

# $scope, $showscopes and $showvars (IEEE 1800-2023 D.11-D.13) on the
# reference interpreter and the compiled kernel, and the refusals: Annex D's
# $list (D.6) and a $scope argument that is not a name stop elaboration, a
# $scope name that is no scope stops the run.
set(FSIM_SCOPE_TASK_FIXTURES
  "${PROJECT_SOURCE_DIR}/tests/fixtures/systemverilog/scope_tasks")
function(fsim_add_scope_task_test FSIM_NAME FSIM_ENGINE FSIM_SOURCE)
  cmake_parse_arguments(PARSE_ARGV 3 FSIM_OPTION ""
    "EXPECTED;ELABORATE_MESSAGE;SIMULATE_MESSAGE" "")
  add_test(
    NAME fsim.systemverilog.scope-tasks.${FSIM_NAME}.${FSIM_ENGINE}
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
      "-DFSIM_SOURCE=${FSIM_SCOPE_TASK_FIXTURES}/${FSIM_SOURCE}"
      "-DFSIM_TOP=top"
      "-DFSIM_ENGINE=${FSIM_ENGINE}"
      "-DFSIM_EXPECTED=${FSIM_OPTION_EXPECTED}"
      "-DFSIM_ELABORATE_MESSAGE=${FSIM_OPTION_ELABORATE_MESSAGE}"
      "-DFSIM_SIMULATE_MESSAGE=${FSIM_OPTION_SIMULATE_MESSAGE}"
      "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/scope-tasks/${FSIM_NAME}-${FSIM_ENGINE}"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunScopeTaskCase.cmake")
  set_tests_properties(
    fsim.systemverilog.scope-tasks.${FSIM_NAME}.${FSIM_ENGINE}
    PROPERTIES LABELS "fast;systemverilog;scope-tasks")
endfunction()
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  fsim_add_scope_task_test(listing ${FSIM_ENGINE} scope_tasks.sv
    EXPECTED "${FSIM_SCOPE_TASK_FIXTURES}/scope_tasks.expected")
  fsim_add_scope_task_test(not-scope ${FSIM_ENGINE} scope_tasks_not_scope.sv
    SIMULATE_MESSAGE
    "is not a module instance, task, function or named block")
  fsim_add_scope_task_test(lexical ${FSIM_ENGINE} scope_tasks_lexical.sv
    EXPECTED "${FSIM_SCOPE_TASK_FIXTURES}/scope_tasks_lexical.expected")
  fsim_add_scope_task_test(generate ${FSIM_ENGINE} scope_tasks_generate.sv
    SIMULATE_MESSAGE
    "is not a module instance, task, function or named block")
  fsim_add_scope_task_test(unknown ${FSIM_ENGINE} scope_tasks_unknown.sv
    SIMULATE_MESSAGE "does not name a scope of the design")
endforeach()
fsim_add_scope_task_test(list elaboration scope_tasks_list.sv
  ELABORATE_MESSAGE "FSIM-ELAB-SVSCOPE-002")
fsim_add_scope_task_test(literal elaboration scope_tasks_literal.sv
  ELABORATE_MESSAGE "FSIM-ELAB-SVSCOPE-001")

# Nets of user-defined nettypes (IEEE 1800-2023 6.6.7, 6.7.3): resolution
# functions of packed, real, struct and unpacked-array types, nettypes of a
# package, the compilation unit and a class static function, undriven nets,
# ports, force, and arrays of nettype nets, on the reference interpreter and
# the compiled kernel.
set(FSIM_NETTYPE_FIXTURES
  "${PROJECT_SOURCE_DIR}/tests/fixtures/systemverilog/nettype")
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  foreach(FSIM_CASE IN ITEMS
      array_type_ports_force
      class_static_function
      driver_view
      function_result_fields
      function_result_reads
      net_array
      net_array_event
      package_scope
      packed_struct
      packed_struct_drivers
      packed_sum
      ports
      real_array
      real_array_force
      real_average
      struct_array
      struct_drivers
      struct_lrm
      struct_or
      two_state_array
      undriven
      unit_scope
      unresolved_array
    )
    string(REPLACE "_" "-" FSIM_CASE_NAME "${FSIM_CASE}")
    set(FSIM_NAME fsim.systemverilog.nettype.${FSIM_CASE_NAME}.${FSIM_ENGINE})
    add_test(
      NAME ${FSIM_NAME}
      COMMAND
        "${CMAKE_COMMAND}"
        "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
        "-DFSIM_SOURCE=${FSIM_NETTYPE_FIXTURES}/${FSIM_CASE}.sv"
        "-DFSIM_TOP=top"
        "-DFSIM_ENGINE=${FSIM_ENGINE}"
        "-DFSIM_EXPECTED=${FSIM_NETTYPE_FIXTURES}/${FSIM_CASE}.expected"
        "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/nettype/${FSIM_CASE}-${FSIM_ENGINE}"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunScopeTaskCase.cmake")
    set_tests_properties(${FSIM_NAME}
      PROPERTIES LABELS "fast;systemverilog;nettype")
  endforeach()
endforeach()

# A pre_randomize or post_randomize that cannot complete is an error of the
# run, not a randomization failure (IEEE 1800-2023 18.6.1, 18.6.3): the run
# stops naming the class, the callback and the cause, on the reference
# interpreter and the compiled kernel.
set(FSIM_RANDOMIZE_CALLBACK_FIXTURES
  "${PROJECT_SOURCE_DIR}/tests/fixtures/systemverilog/randomize_callback")
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  foreach(FSIM_CALLBACK IN ITEMS pre post)
    set(FSIM_NAME fsim.systemverilog.randomize-callback-error.${FSIM_CALLBACK}.${FSIM_ENGINE})
    add_test(
      NAME ${FSIM_NAME}
      COMMAND
        "${CMAKE_COMMAND}"
        "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
        "-DFSIM_SOURCE=${FSIM_RANDOMIZE_CALLBACK_FIXTURES}/${FSIM_CALLBACK}_randomize_error.sv"
        "-DFSIM_TOP=top"
        "-DFSIM_ENGINE=${FSIM_ENGINE}"
        "-DFSIM_SIMULATE_MESSAGE=error[FSIM-RUN-0002]: ${FSIM_CALLBACK}_randomize of class 'work::$unit::item' failed in randomize(): null or stale SystemVerilog class handle"
        "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/randomize-callback-error/${FSIM_CALLBACK}-${FSIM_ENGINE}"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunScopeTaskCase.cmake")
    set_tests_properties(${FSIM_NAME}
      PROPERTIES LABELS "fast;systemverilog;randomize;negative")
  endforeach()
endforeach()
# A callback whose process killed itself there (IEEE 1800-2023 9.7) has
# ended: a later run error of another process names no callback (18.6.1).
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  set(FSIM_NAME fsim.systemverilog.randomize-callback-error.killed.${FSIM_ENGINE})
  add_test(
    NAME ${FSIM_NAME}
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
      "-DFSIM_SOURCE=${FSIM_RANDOMIZE_CALLBACK_FIXTURES}/killed_callback_error.sv"
      "-DFSIM_TOP=top"
      "-DFSIM_ENGINE=${FSIM_ENGINE}"
      "-DFSIM_SIMULATE_MESSAGE=error[FSIM-RUN-0002]: null or stale SystemVerilog class handle"
      "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/randomize-callback-error/killed-${FSIM_ENGINE}"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunScopeTaskCase.cmake")
  set_tests_properties(${FSIM_NAME}
    PROPERTIES LABELS "fast;systemverilog;randomize;negative")
endforeach()

# Statements and expressions in pre_randomize and post_randomize, which
# randomize() calls as generated code, behave as they do in a module: an immediate
# assertion (and one in a method they call) runs its action blocks, reports
# the default $error when it fails without an else block, follows
# $assertoff/$asserton/$assertcontrol, and the callback goes on after the
# failure, so randomize() still returns 1 (IEEE 1800-2023 16.3, 18.6.1,
# 20.11); pure integral expressions (selects, concatenation, ?:, ==?,
# inside, casez/casex, $bits, $clog2, the 20.9 bit vector functions) give
# the values the same expressions give in a module; so do case inside,
# select and member targets, disable, $cast, the enumeration and string
# methods (class_statements), and real values, streaming and packed member
# selects (class_values); the run's services (time, random numbers,
# plusargs, files, $monitoroff, $assertoff, event triggers, $sampled) act
# on the run (class_run_services, with plusargs).
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  foreach(FSIM_CASE IN ITEMS immediate_assertions pure_expressions
      class_statements class_values class_run_services)
    string(REPLACE "_" "-" FSIM_CASE_NAME "${FSIM_CASE}")
    set(FSIM_NAME fsim.systemverilog.randomize-${FSIM_CASE_NAME}.${FSIM_ENGINE})
    add_test(
      NAME ${FSIM_NAME}
      COMMAND
        "${CMAKE_COMMAND}"
        "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
        "-DFSIM_SOURCE=${FSIM_RANDOMIZE_CALLBACK_FIXTURES}/${FSIM_CASE}.sv"
        "-DFSIM_TOP=top"
        "-DFSIM_ENGINE=${FSIM_ENGINE}"
        "-DFSIM_EXPECTED=${FSIM_RANDOMIZE_CALLBACK_FIXTURES}/${FSIM_CASE}.expected"
        "-DFSIM_SIMULATE_ARGS=+fsim_bb5|+count=12|+name=widget"
        "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/randomize-${FSIM_CASE_NAME}/${FSIM_ENGINE}"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunScopeTaskCase.cmake")
    set_tests_properties(${FSIM_NAME}
      PROPERTIES LABELS "fast;systemverilog;randomize")
  endforeach()
endforeach()

# Classes of design element instances and generate scopes, and class types
# named through parameters: a specialization whose actuals read the
# instance's parameters is the instance's (IEEE 1800-2023 8.25, 23.2.2); the
# callbacks and methods of a class declared in a module or a generate block
# run as generated code in the scope that made the class (6.22, 27.4); a
# method called on a member of a class type parameter runs the bound class's
# (8.25). No call enters the host class executor.
set(FSIM_CLASS_SCOPE_FIXTURES
  "${PROJECT_SOURCE_DIR}/tests/fixtures/systemverilog/class_scopes")
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  foreach(FSIM_CASE IN ITEMS instance_specializations scope_callbacks
      type_parameter_member type_equivalence value_kinds block_import
      inherited_type_parameter negative_value real_value real_typedef
      real_functions real_package_scopes real_constant_functions
      hierarchical_handles default_specialization_typename implicit_typedef
      hierarchical_scopes class_members_lowered class_scope_typedefs)
    string(REPLACE "_" "-" FSIM_CASE_NAME "${FSIM_CASE}")
    set(FSIM_NAME fsim.systemverilog.class-scopes-${FSIM_CASE_NAME}.${FSIM_ENGINE})
    add_test(
      NAME ${FSIM_NAME}
      COMMAND
        "${CMAKE_COMMAND}"
        "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
        "-DFSIM_SOURCE=${FSIM_CLASS_SCOPE_FIXTURES}/${FSIM_CASE}.sv"
        "-DFSIM_TOP=top"
        "-DFSIM_ENGINE=${FSIM_ENGINE}"
        "-DFSIM_EXPECTED=${FSIM_CLASS_SCOPE_FIXTURES}/${FSIM_CASE}.expected"
        "-DFSIM_NO_CLASS_EXECUTOR=1"
        "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/class-scopes-${FSIM_CASE_NAME}/${FSIM_ENGINE}"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunScopeTaskCase.cmake")
    set_tests_properties(${FSIM_NAME}
      PROPERTIES LABELS "fast;systemverilog;class")
  endforeach()
endforeach()

# Values in module processes and automatic tasks follow the LRM: an
# enumeration $cast takes the source's own value (IEEE 1800-2023 6.24.2), a
# block-local real or integral initializer converts as an assignment does
# (6.12.2), and the sampled value of an automatic variable is its current
# value (16.5.1).
set(FSIM_PROCEDURAL_VALUE_FIXTURES
  "${PROJECT_SOURCE_DIR}/tests/fixtures/systemverilog/procedural_values")
foreach(FSIM_ENGINE IN ITEMS v4 interpreter)
  foreach(FSIM_CASE IN ITEMS module_typename module_values
      constant_function_selects)
    string(REPLACE "_" "-" FSIM_CASE_NAME "${FSIM_CASE}")
    set(FSIM_NAME fsim.systemverilog.procedural-${FSIM_CASE_NAME}.${FSIM_ENGINE})
    add_test(
      NAME ${FSIM_NAME}
      COMMAND
        "${CMAKE_COMMAND}"
        "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
        "-DFSIM_SOURCE=${FSIM_PROCEDURAL_VALUE_FIXTURES}/${FSIM_CASE}.sv"
        "-DFSIM_TOP=top"
        "-DFSIM_ENGINE=${FSIM_ENGINE}"
        "-DFSIM_EXPECTED=${FSIM_PROCEDURAL_VALUE_FIXTURES}/${FSIM_CASE}.expected"
        "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/tests/procedural-${FSIM_CASE_NAME}/${FSIM_ENGINE}"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunScopeTaskCase.cmake")
    set_tests_properties(${FSIM_NAME}
      PROPERTIES LABELS "fast;systemverilog")
  endforeach()
endforeach()

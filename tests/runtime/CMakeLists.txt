# SPDX-License-Identifier: Apache-2.0

# This directory is intentionally usable from a root CMakeLists.txt with:
#   add_subdirectory(tests/runtime)
# It expects the runtime target to be named fsim_runtime.
add_executable(
  fsim_runtime_tests
  runtime_tests.cpp
  runtime_value_tests.cpp
  runtime_dpi_tests.cpp
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
  runtime_execution_tests.cpp
  runtime_synchronization_tests.cpp
  runtime_file_tests.cpp
  runtime_container_tests.cpp
  runtime_class_heap_tests.cpp
  runtime_constraint_solver_tests.cpp
  runtime_executor_tests.cpp
  runtime_specify_tests.cpp
  runtime_timing_tests.cpp
  runtime_vital_memory_path_tests.cpp
)
target_link_libraries(fsim_runtime_tests PRIVATE fsim_runtime)
add_library(
  fsim_dpi_test_plugin SHARED dpi_test_plugin.cpp dpi_test_plugin_c.c)
fsim_configure_library(fsim_dpi_test_plugin)
target_include_directories(
  fsim_dpi_test_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_library(fsim_vpi_test_plugin SHARED vpi_test_plugin.cpp)
fsim_configure_library(fsim_vpi_test_plugin)
target_include_directories(
  fsim_vpi_test_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
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
          FSIM_VPI_TEST_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_test_plugin>"
          FSIM_VPI_REFERENCE_C_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_reference_c_plugin>"
          FSIM_VPI_REFERENCE_CPP_PLUGIN_PATH="$<TARGET_FILE:fsim_vpi_reference_cpp_plugin>"
)
add_dependencies(
  fsim_runtime_tests fsim_dpi_test_plugin fsim_dpi_bad_abi_plugin
  fsim_vpi_test_plugin)
add_dependencies(
  fsim_runtime_tests fsim_vpi_reference_c_plugin
  fsim_vpi_reference_cpp_plugin)
fsim_configure_test(fsim_runtime_tests)
add_test(NAME fsim.runtime COMMAND fsim_runtime_tests)

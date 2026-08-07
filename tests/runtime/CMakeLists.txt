# SPDX-License-Identifier: Apache-2.0

# This directory is intentionally usable from a root CMakeLists.txt with:
#   add_subdirectory(tests/runtime)
# It expects the runtime target to be named fsim_runtime.
add_executable(
  fsim_runtime_tests
  runtime_tests.cpp
  runtime_value_tests.cpp
  runtime_dpi_tests.cpp
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
add_library(fsim_dpi_bad_abi_plugin SHARED dpi_bad_abi_plugin.cpp)
fsim_configure_library(fsim_dpi_bad_abi_plugin)
target_include_directories(
  fsim_dpi_bad_abi_plugin PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(
  fsim_runtime_tests
  PRIVATE FSIM_DPI_TEST_PLUGIN_PATH="$<TARGET_FILE:fsim_dpi_test_plugin>"
          FSIM_DPI_BAD_ABI_PLUGIN_PATH="$<TARGET_FILE:fsim_dpi_bad_abi_plugin>"
)
add_dependencies(
  fsim_runtime_tests fsim_dpi_test_plugin fsim_dpi_bad_abi_plugin)
fsim_configure_test(fsim_runtime_tests)
add_test(NAME fsim.runtime COMMAND fsim_runtime_tests)

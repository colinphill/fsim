# Include inside the existing if(FSIM_WITH_LLVM) test block. This is a
# standalone application test because it constructs real LLVM process and
# region-frontier providers for interpreter/O0/O2 differential runs.
add_executable(
  fsim_application_native_frontier_generic_update_tests
  app/native_frontier_generic_update_test.cpp
  app/native_frontier_generic_update_test_support.cpp)
fsim_configure_test(
  fsim_application_native_frontier_generic_update_tests)
target_include_directories(
  fsim_application_native_frontier_generic_update_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app"
    "${PROJECT_SOURCE_DIR}/src/runtime"
    "${PROJECT_SOURCE_DIR}/tests/runtime")
target_link_libraries(
  fsim_application_native_frontier_generic_update_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-generic-update
  COMMAND fsim_application_native_frontier_generic_update_tests)
set_tests_properties(
  fsim.application.native-frontier-generic-update
  PROPERTIES LABELS "fast;application;native;frontier;generic;differential"
    RUN_SERIAL TRUE)

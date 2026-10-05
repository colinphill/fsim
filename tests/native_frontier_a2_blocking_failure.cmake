# Allocation-failure and exclusion controls for parsed A2 WriteBlocking.
add_executable(
  fsim_application_native_frontier_a2_blocking_failure_tests
  app/native_frontier_a2_blocking_failure_test.cpp
  app/native_frontier_generic_update_failure_interposer.cpp)
fsim_configure_test(
  fsim_application_native_frontier_a2_blocking_failure_tests)
target_include_directories(
  fsim_application_native_frontier_a2_blocking_failure_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app"
    "${PROJECT_SOURCE_DIR}/src/runtime"
    "${PROJECT_SOURCE_DIR}/tests/runtime")
target_link_libraries(
  fsim_application_native_frontier_a2_blocking_failure_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-a2-blocking-failure
  COMMAND fsim_application_native_frontier_a2_blocking_failure_tests)
set_tests_properties(
  fsim.application.native-frontier-a2-blocking-failure
  PROPERTIES LABELS "fast;application;systemverilog;native;frontier;blocking;allocation"
    RUN_SERIAL TRUE)

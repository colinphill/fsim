add_executable(
  fsim_application_native_frontier_generic_update_failure_tests
  app/native_frontier_generic_update_failure_test.cpp
  app/native_frontier_generic_update_logic9_failure_test.cpp
  app/native_frontier_generic_update_failure_interposer.cpp
  app/native_frontier_generic_update_test_support.cpp)
fsim_configure_test(
  fsim_application_native_frontier_generic_update_failure_tests)
target_include_directories(
  fsim_application_native_frontier_generic_update_failure_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app"
    "${PROJECT_SOURCE_DIR}/src/runtime"
    "${PROJECT_SOURCE_DIR}/tests/runtime")
target_link_libraries(
  fsim_application_native_frontier_generic_update_failure_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-generic-update-failure
  COMMAND fsim_application_native_frontier_generic_update_failure_tests)
set_tests_properties(
  fsim.application.native-frontier-generic-update-failure
  PROPERTIES LABELS "fast;application;generic;native;frontier;allocation;retry"
    ENVIRONMENT "FSIM_ENABLE_SV_REGION_KERNEL=1"
    RUN_SERIAL TRUE)

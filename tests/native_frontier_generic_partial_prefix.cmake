# Include inside the existing if(FSIM_WITH_LLVM) test block. The fixture
# delegates through the production LLVM providers and observes the generated
# entry only to record the borrowed scheduler prefix it receives.
add_executable(
  fsim_application_native_frontier_generic_partial_prefix_tests
  app/native_frontier_generic_partial_prefix_test.cpp
  app/native_frontier_generic_update_test_support.cpp)
fsim_configure_test(
  fsim_application_native_frontier_generic_partial_prefix_tests)
target_include_directories(
  fsim_application_native_frontier_generic_partial_prefix_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app"
    "${PROJECT_SOURCE_DIR}/src/runtime"
    "${PROJECT_SOURCE_DIR}/tests/runtime")
target_link_libraries(
  fsim_application_native_frontier_generic_partial_prefix_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-generic-partial-prefix
  COMMAND fsim_application_native_frontier_generic_partial_prefix_tests)
set_tests_properties(
  fsim.application.native-frontier-generic-partial-prefix
  PROPERTIES LABELS "fast;application;native;frontier;generic;differential"
    RUN_SERIAL TRUE)

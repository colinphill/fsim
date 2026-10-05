# Add this target inside the existing `if(FSIM_WITH_LLVM)` test block.
add_executable(
  fsim_application_native_frontier_a2_applied_prefix_tests
  app/native_frontier_a2_applied_prefix_test.cpp)
fsim_configure_test(
  fsim_application_native_frontier_a2_applied_prefix_tests)
target_link_libraries(
  fsim_application_native_frontier_a2_applied_prefix_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-a2-applied-prefix
  COMMAND fsim_application_native_frontier_a2_applied_prefix_tests)
set_tests_properties(
  fsim.application.native-frontier-a2-applied-prefix
  PROPERTIES LABELS "fast;application;systemverilog;native;frontier;observation"
    RUN_SERIAL TRUE)

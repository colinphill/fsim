# A2 single-root acyclic fanout role-journal witness.
add_executable(
  fsim_application_native_frontier_a2_single_root_fanout_tests
  app/native_frontier_a2_single_root_fanout_test.cpp)
fsim_configure_test(
  fsim_application_native_frontier_a2_single_root_fanout_tests)
target_link_libraries(
  fsim_application_native_frontier_a2_single_root_fanout_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-a2-single-root-fanout
  COMMAND fsim_application_native_frontier_a2_single_root_fanout_tests)
set_tests_properties(
  fsim.application.native-frontier-a2-single-root-fanout
  PROPERTIES LABELS "fast;application;systemverilog;native;frontier;observation"
    RUN_SERIAL TRUE)

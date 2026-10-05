# A2 original-key SystemVerilog WriteBlocking witness.
add_executable(
  fsim_application_native_frontier_a2_blocking_immediate_tests
  app/native_frontier_a2_blocking_immediate_test.cpp)
fsim_configure_test(
  fsim_application_native_frontier_a2_blocking_immediate_tests)
target_link_libraries(
  fsim_application_native_frontier_a2_blocking_immediate_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-a2-blocking-immediate
  COMMAND fsim_application_native_frontier_a2_blocking_immediate_tests)
set_tests_properties(
  fsim.application.native-frontier-a2-blocking-immediate
  PROPERTIES LABELS "fast;application;systemverilog;native;frontier;blocking"
    RUN_SERIAL TRUE)

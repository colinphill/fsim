# A2 parsed single-root balanced-DAG join role-journal witness.
add_executable(
  fsim_application_native_frontier_a2_dag_join_tests
  app/native_frontier_a2_dag_join_test.cpp)
fsim_configure_test(
  fsim_application_native_frontier_a2_dag_join_tests)
target_link_libraries(
  fsim_application_native_frontier_a2_dag_join_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-a2-dag-join
  COMMAND fsim_application_native_frontier_a2_dag_join_tests)
set_tests_properties(
  fsim.application.native-frontier-a2-dag-join
  PROPERTIES LABELS "fast;application;systemverilog;native;frontier;observation"
    RUN_SERIAL TRUE)

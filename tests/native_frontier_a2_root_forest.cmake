# A2 arbitrary-root DAG role-journal witness.
add_executable(
  fsim_application_native_frontier_a2_root_forest_tests
  app/native_frontier_a2_root_forest_test.cpp)
fsim_configure_test(
  fsim_application_native_frontier_a2_root_forest_tests)
target_link_libraries(
  fsim_application_native_frontier_a2_root_forest_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-a2-root-forest
  COMMAND fsim_application_native_frontier_a2_root_forest_tests)
set_tests_properties(
  fsim.application.native-frontier-a2-root-forest
  PROPERTIES LABELS "fast;application;systemverilog;native;frontier;observation"
    RUN_SERIAL TRUE)

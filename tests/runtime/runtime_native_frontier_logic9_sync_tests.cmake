add_executable(
  fsim_runtime_native_frontier_logic9_sync_tests
  runtime_native_frontier_logic9_sync_tests.cpp)
target_link_libraries(
  fsim_runtime_native_frontier_logic9_sync_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_native_frontier_logic9_sync_tests)
add_test(
  NAME fsim.runtime.native_frontier_logic9_sync
  COMMAND fsim_runtime_native_frontier_logic9_sync_tests)
set_tests_properties(
  fsim.runtime.native_frontier_logic9_sync
  PROPERTIES LABELS "fast;runtime;region-graph;frontier;logic9;a4;sync;windows;linux")

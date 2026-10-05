# A2 applied-role journal, cancellation, and checked-fallback witness.
add_executable(
  fsim_runtime_a2_role_journal_tests
  runtime_a2_role_journal_tests.cpp
  runtime_fused_staging_failure_support.cpp)
target_link_libraries(
  fsim_runtime_a2_role_journal_tests PRIVATE fsim_runtime)
fsim_configure_test(fsim_runtime_a2_role_journal_tests)
add_test(
  NAME fsim.runtime.a2_role_journal
  COMMAND fsim_runtime_a2_role_journal_tests)
set_tests_properties(
  fsim.runtime.a2_role_journal
  PROPERTIES LABELS "runtime;region-graph;a2;role-journal;cancellation;failure;windows;linux")

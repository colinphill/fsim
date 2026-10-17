# SPDX-License-Identifier: Apache-2.0

# Include inside the FSIM_WITH_LLVM native-frontier test block.
add_executable(
  fsim_application_native_frontier_selected_signal_history_tests
  app/selected_signal_frontier_history_test.cpp)
fsim_configure_test(
  fsim_application_native_frontier_selected_signal_history_tests)
target_link_libraries(
  fsim_application_native_frontier_selected_signal_history_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-selected-signal-history
  COMMAND fsim_application_native_frontier_selected_signal_history_tests)
set_tests_properties(
  fsim.application.native-frontier-selected-signal-history
  PROPERTIES
    LABELS "fast;application;mixed-language;vhdl;systemverilog;native;frontier;signal-history"
    ENVIRONMENT "FSIM_ENABLE_SV_REGION_KERNEL=1;FSIM_ENABLE_SV_LOCAL_WAVE=1;FSIM_PROFILE_SV_WAVES=1"
    RUN_SERIAL TRUE)

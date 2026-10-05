# Add this target inside the existing `if(FSIM_WITH_LLVM)` test block.
add_executable(
  fsim_application_native_frontier_retained_stop_resume_tests
  app/native_frontier_retained_stop_resume_test.cpp)
fsim_configure_test(
  fsim_application_native_frontier_retained_stop_resume_tests)
target_link_libraries(
  fsim_application_native_frontier_retained_stop_resume_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-retained-stop-resume
  COMMAND fsim_application_native_frontier_retained_stop_resume_tests)
set_tests_properties(
  fsim.application.native-frontier-retained-stop-resume
  PROPERTIES LABELS "fast;application;systemverilog;native;frontier;lifecycle"
    RUN_SERIAL TRUE)

# Verify V2-first Generic runtime construction with the real LLVM providers.
add_executable(
  fsim_application_native_frontier_v2_first_generic_tests
  app/native_frontier_v2_first_generic_test.cpp)
fsim_configure_test(
  fsim_application_native_frontier_v2_first_generic_tests)
target_include_directories(
  fsim_application_native_frontier_v2_first_generic_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app"
    "${PROJECT_SOURCE_DIR}/src/runtime"
    "${PROJECT_SOURCE_DIR}/tests/runtime")
target_link_libraries(
  fsim_application_native_frontier_v2_first_generic_tests
  PRIVATE fsim_application)
add_test(
  NAME fsim.application.native-frontier-v2-first-generic
  COMMAND fsim_application_native_frontier_v2_first_generic_tests)
set_tests_properties(
  fsim.application.native-frontier-v2-first-generic
  PROPERTIES LABELS "fast;application;native;frontier;generic;construction"
    RUN_SERIAL TRUE)

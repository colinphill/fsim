# Add from tests/CMakeLists.txt after the test helpers are defined.
add_executable(
  fsim_llvm_region_frontier_abi_v2_layout_tests
  "${PROJECT_SOURCE_DIR}/tests/compiler/simir_region_frontier_abi_v2_layout_test.cpp"
)
fsim_configure_test(fsim_llvm_region_frontier_abi_v2_layout_tests)
target_compile_features(
  fsim_llvm_region_frontier_abi_v2_layout_tests
  PRIVATE cxx_std_17
)
target_include_directories(
  fsim_llvm_region_frontier_abi_v2_layout_tests
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_register_simple_test(
  fsim.compiler.llvm-region-frontier-abi-v2-layout
  fsim_llvm_region_frontier_abi_v2_layout_tests
  "fast;compiler;llvm;abi;layout;portability"
)

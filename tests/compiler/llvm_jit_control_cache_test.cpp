// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void test_process_control_cache_identity() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path()
      / ("fsim-control-cache-" + std::to_string(serial));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  assert(!error);
  const std::array<std::uint32_t, 2> widths{1, 1};
  const auto make_process = [](const InstructionIndex equal_target) {
    Process process;
    process.id = 0;
    process.name = "control_cache";
    process.register_count = 3;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4};
    process.operations = {
        LoadConstant{0, PackedLogic4::from_msb_string("0")},
        WaitOn{{0, 1}, {EdgeKind::any, EdgeKind::any}},
        ReadSignal{1, 0},
        Binary{BinaryOperator::case_equal, 2, 0, 1},
        CopyRegister{0, 1},
        Branch{
            2, equal_target, 6,
            UnknownBranchPolicy::when_false},
        Halt{}};
    return process;
  };
  for (const auto optimization : {
           JitOptimizationLevel::o0,
           JitOptimizationLevel::o2}) {
    const auto directory = root
        / (optimization == JitOptimizationLevel::o0 ? "o0" : "o2");
    const auto options = LlvmJitOptions{optimization, directory};
    {
      LlvmJit cold{options};
      cold.add_process("control_cache", make_process(1), widths);
      assert(cold.lookup("control_cache"));
      const auto statistics = cold.cache_statistics();
      assert(statistics.misses == 1 && statistics.stores == 1);
    }
    {
      LlvmJit warm{options};
      warm.add_process("control_cache", make_process(1), widths);
      assert(warm.lookup("control_cache"));
      const auto statistics = warm.cache_statistics();
      assert(statistics.hits == 1 && statistics.misses == 0);
    }
    {
      LlvmJit changed{options};
      changed.add_process("control_cache", make_process(0), widths);
      assert(changed.lookup("control_cache"));
      const auto statistics = changed.cache_statistics();
      assert(statistics.misses == 1 && statistics.stores == 1);
    }
  }
  std::filesystem::remove_all(root, error);
  assert(!error);
}

}  // namespace fsim::tests::compiler

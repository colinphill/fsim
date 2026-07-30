// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void test_strings_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol) {
  Process process{
      .id = 0,
      .name = "strings",
      .register_count = 4,
      .string_register_count = 4,
      .debug_locals = {},
      .debug_string_locals = {},
      .static_sensitivity = {},
      .operations = {
          LoadStringConstant{0, "fsim"},
          LoadStringConstant{1, "-v1"},
          ConcatenateStrings{2, {0, 1}},
          LoadConstant{
              0, PackedLogic4::from_aval_bval(32, 0, 0)},
          LoadConstant{
              1, PackedLogic4::from_aval_bval(8, 'F', 0)},
          StringReplaceByte{2, 0, 1, true},
          StringLength{2, 2},
          LoadStringConstant{3, "Fsim-v1"},
          CompareStrings{3, 2, 3, false},
          WriteStringObject{0, 2},
          ReadStringObject{0, 0},
          StringDisplay{0, "[", "]", true, false},
          Halt{},
      },
      .register_value_kinds = {},
      .initialize = true,
      .final = false};

  LlvmJitOptions options;
  options.optimization = optimization;
  LlvmJit jit(options);
  assert(jit.supports_process(process, {}));
  jit.add_process(symbol, process, {});
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  assert(layout.register_count == 4);
  assert(layout.string_register_count == 4);

  TestRuntime state;
  auto runtime = abi(state);
  assert(jit.execute(handle, runtime) == JitExecutionStatus::completed);
  assert(state.string_objects[0] == "Fsim-v1");
  assert(state.strings[0] == "Fsim-v1");
  assert(state.output == std::vector<std::string>{"[Fsim-v1]"});
}

}  // namespace fsim::tests::compiler

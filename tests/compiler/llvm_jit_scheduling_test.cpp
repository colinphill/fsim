// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void run_at_level(const JitOptimizationLevel optimization,
                  const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const std::array<std::uint32_t, 8> widths{8, 8, 8, 8, 8, 8, 8, 1};
  const auto process = make_arithmetic_process();
  jit.add_process(symbol, process, widths);

  const auto handle = jit.lookup(symbol);
  assert(handle);
  assert(jit.lookup(symbol) == handle);
  expect_fatal_error(
      [&] { jit.add_process(symbol, process, widths); },
      "duplicate LLVM process symbol");

  TestRuntime runtime;
  runtime.signals[0] = {0x35, 0};
  runtime.signals[1] = {0x0f, 0};
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  assert(runtime.assertion_count == 0);
  assert((runtime.signals[2] == EncodedSignal{0x05, 0}));
  assert((runtime.signals[3] == EncodedSignal{0x3f, 0}));
  assert((runtime.signals[4] == EncodedSignal{0x3a, 0}));
  assert((runtime.signals[5] == EncodedSignal{0x44, 0}));
  assert((runtime.signals[6] == EncodedSignal{0xca, 0}));
  assert((runtime.signals[7] == EncodedSignal{0, 0}));

  // Bit zero of input zero is X. This checks four-state propagation in every
  // emitted operator and takes the generated assertion failure edge.
  runtime.signals[0] = {0x35, 0x01};
  runtime.assertion_count = 0;
  runtime.assertion_message.clear();
  assert(jit.execute(handle, descriptor) ==
         JitExecutionStatus::assertion_failed);
  assert(runtime.assertion_count == 1);
  assert(runtime.failed_process == process.id);
  assert(runtime.failed_instruction == 16);
  assert(runtime.assertion_message == "unexpected sum");
  assert((runtime.signals[2] == EncodedSignal{0x05, 0x01}));
  assert((runtime.signals[3] == EncodedSignal{0x3f, 0}));
  assert((runtime.signals[4] == EncodedSignal{0x3b, 0x01}));
  assert((runtime.signals[5] == EncodedSignal{0xff, 0xff}));
  assert((runtime.signals[6] == EncodedSignal{0xcb, 0x01}));
  assert((runtime.signals[7] == EncodedSignal{1, 1}));

  auto wrong_abi = descriptor;
  wrong_abi.abi_version = 99;
  expect_fatal_error([&] { (void)jit.execute(handle, wrong_abi); },
                     "ABI version mismatch");

  TestRuntime legacy_runtime;
  legacy_runtime.signals[0] = {0x35, 0};
  legacy_runtime.signals[1] = {0x0f, 0};
  auto legacy_descriptor = abi(legacy_runtime);
  legacy_descriptor.struct_size = static_cast<std::uint32_t>(
      offsetof(fsim_jit_runtime_v1, write_update));
  legacy_descriptor.write_update = nullptr;
  legacy_descriptor.write_after = nullptr;
  assert(jit.execute(handle, legacy_descriptor) ==
         JitExecutionStatus::completed);

  expect_error([&] { (void)jit.lookup("not_added"); }, "was not added");
  assert(jit.cache_statistics() ==
         fsim::compiler::LlvmJitCacheStatistics{});
}

[[nodiscard]] Process make_scheduled_callback_process() {
  Process process;
  process.id = 3;
  process.name = "scheduled_callbacks";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10XZ0101")},
      WriteUpdate{2, 0},
      WriteAfter{3, 0, 0},
      WriteAfter{4, 0, std::numeric_limits<std::uint64_t>::max()},
      Halt{},
  };
  return process;
}

void test_scheduled_callbacks_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const auto process = make_scheduled_callback_process();
  const std::array<std::uint32_t, 5> widths{8, 8, 8, 8, 8};
  const auto symbol = std::string{symbol_prefix} + "_scheduled_callbacks";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  const auto value =
      encode(PackedLogic4::from_msb_string("10XZ0101"));
  const std::vector<ScheduledWrite> expected{
      {ScheduledWriteKind::update, 2, value, 0, 0, 0},
      {ScheduledWriteKind::after, 3, value, 0, 0, 0},
      {ScheduledWriteKind::after, 4, value, 0,
       std::numeric_limits<std::uint64_t>::max(),
       std::numeric_limits<std::uint64_t>::max()},
  };
  assert(runtime.scheduled_writes == expected);
  assert(runtime.writes.empty());

  {
    auto too_short = descriptor;
    too_short.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_update));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, too_short); },
        "does not include write_update");
  }
  {
    auto no_delayed_tail = descriptor;
    no_delayed_tail.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_after));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, no_delayed_tail); },
        "does not include write_after");
  }
  {
    auto missing_update = descriptor;
    missing_update.write_update = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing_update); },
        "requires write_update");
  }
  {
    auto missing_after = descriptor;
    missing_after.write_after = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing_after); },
        "requires write_after");
  }

  Process update_only;
  update_only.id = 4;
  update_only.name = "update_only";
  update_only.register_count = 1;
  update_only.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
      WriteUpdate{0, 0},
      Halt{},
  };
  const auto update_symbol = std::string{symbol_prefix} + "_update_only";
  jit.add_process(update_symbol, update_only, widths);
  TestRuntime update_runtime;
  auto update_descriptor = abi(update_runtime);
  update_descriptor.struct_size = static_cast<std::uint32_t>(
      offsetof(fsim_jit_runtime_v1, write_after));
  update_descriptor.write_after = nullptr;
  assert(jit.execute(jit.lookup(update_symbol), update_descriptor) ==
         JitExecutionStatus::completed);
  assert(update_runtime.scheduled_writes.size() == 1);
  assert(update_runtime.scheduled_writes.front().kind ==
         ScheduledWriteKind::update);

  Process after_only;
  after_only.id = 5;
  after_only.name = "after_only";
  after_only.register_count = 1;
  after_only.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("00111100")},
      WriteAfter{1, 0, 7},
      Halt{},
  };
  const auto after_symbol = std::string{symbol_prefix} + "_after_only";
  jit.add_process(after_symbol, after_only, widths);
  TestRuntime after_runtime;
  auto after_descriptor = abi(after_runtime);
  after_descriptor.write_update = nullptr;
  assert(jit.execute(jit.lookup(after_symbol), after_descriptor) ==
         JitExecutionStatus::completed);
  assert(after_runtime.scheduled_writes.size() == 1);
  assert(after_runtime.scheduled_writes.front().kind ==
         ScheduledWriteKind::after);
  assert(after_runtime.scheduled_writes.front().delay == 7);
}

void test_inertial_callbacks_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  Process process;
  process.id = 6;
  process.name = "inertial_callbacks";
  process.register_count = 2;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10XZ0101")},
      WriteInertial{0, 0, {2, 3, 4}},
      LoadConstant{1, PackedLogic4::from_msb_string("XZ")},
      WriteInertialSlice{1, 1, 3, {5, 6, 7}},
      Halt{},
  };
  const std::array<std::uint32_t, 2> widths{8, 8};
  const auto symbol =
      std::string{symbol_prefix} + "_inertial_callbacks";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(handle, descriptor)
      == JitExecutionStatus::completed);
  const std::vector<InertialWrite> expected{
      {
          0,
          encode(PackedLogic4::from_msb_string("10XZ0101")),
          0,
          0,
          2,
          3,
          4},
      {
          1,
          encode(PackedLogic4::from_msb_string("XZ")),
          3,
          2,
          5,
          6,
          7},
  };
  assert(runtime.inertial_writes == expected);

  {
    auto too_short = descriptor;
    too_short.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_inertial));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, too_short); },
        "does not include write_inertial");
  }
  {
    auto missing = descriptor;
    missing.write_inertial = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_inertial");
  }
  {
    auto too_short = descriptor;
    too_short.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_inertial_slice));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, too_short); },
        "does not include write_inertial_slice");
  }
  {
    auto missing = descriptor;
    missing.write_inertial_slice = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_inertial_slice");
  }
}

void test_projected_callbacks_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  Process process;
  process.id = 7;
  process.name = "projected_callbacks";
  process.register_count = 6;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10XZ0101")},
      WriteProjected{
          0,
          0,
          11,
          3,
          ProjectedDelayMode::inertial},
      LoadConstant{1, PackedLogic4::from_msb_string("XZ")},
      WriteProjectedSlice{
          1,
          1,
          3,
          13,
          0,
          ProjectedDelayMode::transport},
      LoadConstant{2, PackedLogic4::from_msb_string("01010101")},
      LoadConstant{3, PackedLogic4::from_msb_string("10101010")},
      WriteProjectedWaveform{
          0,
          {{2, 17}, {3, 23}},
          4,
          ProjectedDelayMode::inertial},
      LoadConstant{4, PackedLogic4::from_msb_string("01")},
      LoadConstant{5, PackedLogic4::from_msb_string("10")},
      WriteProjectedWaveformSlice{
          1,
          {{4, 19}, {5, 29}},
          2,
          0,
          ProjectedDelayMode::transport},
      Halt{},
  };
  const std::array<std::uint32_t, 2> widths{8, 8};
  const auto symbol =
      std::string{symbol_prefix} + "_projected_callbacks";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(handle, descriptor)
      == JitExecutionStatus::completed);
  const std::vector<ProjectedWrite> expected{
      {
          0,
          encode(PackedLogic4::from_msb_string("10XZ0101")),
          0,
          0,
          11,
          3,
          FSIM_JIT_PROJECTED_INERTIAL},
      {
          1,
          encode(PackedLogic4::from_msb_string("XZ")),
          3,
          2,
          13,
          0,
          FSIM_JIT_PROJECTED_TRANSPORT},
      {
          0,
          encode(PackedLogic4::from_msb_string("01010101")),
          0,
          8,
          17,
          4,
          FSIM_JIT_PROJECTED_INERTIAL},
      {
          0,
          encode(PackedLogic4::from_msb_string("10101010")),
          0,
          8,
          23,
          4,
          FSIM_JIT_PROJECTED_INERTIAL},
      {
          1,
          encode(PackedLogic4::from_msb_string("01")),
          2,
          2,
          19,
          0,
          FSIM_JIT_PROJECTED_TRANSPORT},
      {
          1,
          encode(PackedLogic4::from_msb_string("10")),
          2,
          2,
          29,
          0,
          FSIM_JIT_PROJECTED_TRANSPORT},
  };
  assert(runtime.projected_writes == expected);

  {
    auto too_short = descriptor;
    too_short.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_projected));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, too_short); },
        "does not include write_projected");
  }
  {
    auto missing = descriptor;
    missing.write_projected = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_projected");
  }
  {
    auto too_short = descriptor;
    too_short.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_projected_slice));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, too_short); },
        "does not include write_projected_slice");
  }
  {
    auto missing = descriptor;
    missing.write_projected_slice = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_projected_slice");
  }
  {
    auto too_short = descriptor;
    too_short.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_projected_waveform));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, too_short); },
        "does not include write_projected_waveform");
  }
  {
    auto missing = descriptor;
    missing.write_projected_waveform = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_projected_waveform");
  }
  {
    auto too_short = descriptor;
    too_short.struct_size = static_cast<std::uint32_t>(
        offsetof(
            fsim_jit_runtime_v1,
            write_projected_waveform_slice));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, too_short); },
        "does not include write_projected_waveform_slice");
  }
  {
    auto missing = descriptor;
    missing.write_projected_waveform_slice = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_projected_waveform_slice");
  }
}

[[nodiscard]] Process make_scheduling_differential_process() {
  Process process;
  process.id = 0;
  process.name = "scheduled_differential";
  process.register_count = 3;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10XZ0101")},
      WriteUpdate{0, 0},
      WriteAfter{1, 0, 4},
      WaitFor{2},
      LoadConstant{1, PackedLogic4::from_msb_string("00111100")},
      WriteUpdate{2, 1},
      WriteAfter{3, 1, 1},
      WaitFor{2},
      Halt{},
  };
  return process;
}

void test_scheduling_differential_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  const auto process = make_scheduling_differential_process();
  const std::array<std::uint32_t, 4> widths{8, 8, 8, 8};
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const auto symbol = std::string{symbol_prefix} + "_scheduled_differential";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  std::vector<std::uint64_t> register_aval(layout.register_count);
  std::vector<std::uint64_t> register_bval(layout.register_count);
  std::vector<std::uint8_t> register_initialized(layout.register_count);
  fsim_jit_frame_v1 frame{};
  jit.initialize_frame(
      handle, frame, register_aval, register_bval,
      register_initialized);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  auto result = new_resume_result();
  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_for);
  assert(result.instruction == 3);
  assert(result.delay == 2);
  runtime.current_time += result.delay;
  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_for);
  assert(result.instruction == 7);
  assert(result.delay == 2);
  runtime.current_time += result.delay;
  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::completed);
  assert(result.instruction == 8);
  assert(runtime.current_time == 4);

  std::vector<ObservedWrite> generated;
  generated.reserve(runtime.scheduled_writes.size());
  for (const auto &write : runtime.scheduled_writes) {
    generated.push_back({write.due, write.signal, write.value});
  }
  const auto order = [](const ObservedWrite &lhs,
                        const ObservedWrite &rhs) {
    if (lhs.time != rhs.time) {
      return lhs.time < rhs.time;
    }
    return lhs.signal < rhs.signal;
  };
  std::sort(generated.begin(), generated.end(), order);

  fsim::runtime::simir::Interpreter interpreter;
  for (std::uint32_t signal = 0; signal < widths.size(); ++signal) {
    (void)interpreter.add_signal(
        {"scheduled" + std::to_string(signal),
         PackedLogic4::from_msb_string("XXXXXXXX")});
  }
  std::vector<ObservedWrite> reference;
  interpreter.set_signal_change_hook(
      [&](const SignalId signal, const PackedLogic4 &value,
          const fsim::runtime::SimulationTick time) {
        reference.push_back({time, signal, encode(value)});
      });
  (void)interpreter.add_process(process);
  const auto reference_result = interpreter.run();
  assert(reference_result.status == fsim::runtime::RunStatus::completed);
  assert(reference_result.time == 4);
  std::sort(reference.begin(), reference.end(), order);
  assert(generated == reference);
}

[[nodiscard]] Process
make_branch_process(const UnknownBranchPolicy unknown_policy) {
  Process process;
  process.id = 0;
  process.name = "branch";
  process.register_count = 2;
  process.operations = {
      ReadSignal{0, 0},
      Branch{0, 2, 4, unknown_policy},
      LoadConstant{1, PackedLogic4::from_msb_string("10100101")},
      Jump{5},
      LoadConstant{1, PackedLogic4::from_msb_string("00111100")},
      WriteBlocking{1, 1},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process make_nested_call_process() {
  const CallStack stack{0, 1, 2};
  Process process;
  process.id = 0;
  process.name = "nested_calls";
  process.register_count = 6;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{1, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{3, PackedLogic4::from_aval_bval(8, 10, 0)},
      LoadConstant{4, PackedLogic4::from_aval_bval(8, 0, 0)},
      Call{9, 6, stack},
      WriteBlocking{0, 4},
      Halt{},
      Halt{},
      DebugPoint{
          DebugPointKind::call,
          SourceLocation{"nested_calls.simir", 1, 1}},
      LoadConstant{5, PackedLogic4::from_aval_bval(8, 0, 0)},
      Call{15, 12, stack},
      CopyRegister{4, 5},
      Return{stack},
      Halt{},
      LoadConstant{5, PackedLogic4::from_aval_bval(8, 42, 0)},
      Return{stack},
  };
  return process;
}

[[nodiscard]] EncodedSignal
run_interpreter_branch(const UnknownBranchPolicy unknown_policy,
                       const Logic4 condition) {
  fsim::runtime::simir::Interpreter interpreter;
  (void)interpreter.add_signal(
      {"condition", PackedLogic4{1, condition}});
  const auto output = interpreter.add_signal(
      {"output", PackedLogic4::from_msb_string("00000000")});
  (void)interpreter.add_process(make_branch_process(unknown_policy));
  const auto result = interpreter.run();
  assert(result.status == fsim::runtime::RunStatus::completed);
  return encode(interpreter.signal_value(output));
}

void test_control_flow_at_level(const JitOptimizationLevel optimization,
                                const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const std::array<std::uint32_t, 2> widths{1, 8};
  const auto when_false_symbol =
      std::string{symbol_prefix} + "_unknown_false";
  const auto error_symbol = std::string{symbol_prefix} + "_unknown_error";
  jit.add_process(
      when_false_symbol,
      make_branch_process(UnknownBranchPolicy::when_false), widths);
  jit.add_process(
      error_symbol, make_branch_process(UnknownBranchPolicy::error), widths);
  const auto when_false_handle = jit.lookup(when_false_symbol);
  const auto error_handle = jit.lookup(error_symbol);

  const auto run_jit = [&](const JitProcessHandle handle,
                           const Logic4 condition) {
    TestRuntime runtime;
    runtime.signals[0] = encode(condition);
    auto descriptor = abi(runtime);
    assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
    assert(runtime.assertion_count == 0);
    return runtime.signals[1];
  };

  constexpr std::array known_conditions{Logic4::zero, Logic4::one};
  for (const auto condition : known_conditions) {
    const auto reference = run_interpreter_branch(
        UnknownBranchPolicy::when_false, condition);
    assert(run_jit(when_false_handle, condition) == reference);
    assert(run_jit(error_handle, condition) ==
           run_interpreter_branch(UnknownBranchPolicy::error, condition));
  }
  assert((run_jit(when_false_handle, Logic4::zero) ==
          EncodedSignal{UINT64_C(0x3c), 0}));
  assert((run_jit(when_false_handle, Logic4::one) ==
          EncodedSignal{UINT64_C(0xa5), 0}));

  // Verilog/SystemVerilog X and Z select the false edge only when the lowering
  // explicitly requested that language policy.
  constexpr std::array unknown_conditions{Logic4::x, Logic4::z};
  for (const auto condition : unknown_conditions) {
    const auto reference = run_interpreter_branch(
        UnknownBranchPolicy::when_false, condition);
    assert((reference == EncodedSignal{UINT64_C(0x3c), 0}));
    assert(run_jit(when_false_handle, condition) == reference);

    bool interpreter_rejected = false;
    try {
      (void)run_interpreter_branch(UnknownBranchPolicy::error, condition);
    } catch (const InterpreterError &error) {
      interpreter_rejected = true;
      assert(error.instruction() == 1);
      assert(std::string_view{error.what()}.find(
                 "branch condition is unknown or high impedance") !=
             std::string_view::npos);
    }
    assert(interpreter_rejected);

    TestRuntime runtime;
    runtime.signals[0] = encode(condition);
    auto descriptor = abi(runtime);
    expect_generated_runtime_error(
        [&] { (void)jit.execute(error_handle, descriptor); },
        1, JitGeneratedRuntimeErrorReason::unknown_branch_condition,
        "instruction 1: branch condition is unknown or high impedance");

    std::vector<std::uint64_t> register_aval(
        jit.frame_layout(error_handle).register_count);
    std::vector<std::uint64_t> register_bval(
        jit.frame_layout(error_handle).register_count);
    std::vector<std::uint8_t> register_initialized(
        jit.frame_layout(error_handle).register_count);
    fsim_jit_frame_v1 frame{};
    jit.initialize_frame(
        error_handle, frame, register_aval, register_bval,
        register_initialized);
    auto result = new_resume_result();
    expect_generated_runtime_error(
        [&] {
          (void)jit.resume(
              error_handle, descriptor, frame, result);
        },
        1, JitGeneratedRuntimeErrorReason::unknown_branch_condition,
        "instruction 1: branch condition is unknown or high impedance");
    assert(result.status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR);
    assert(frame.state == FSIM_JIT_FRAME_STATE_RUNTIME_ERROR);
    expect_generated_runtime_error(
        [&] {
          (void)jit.resume(
              error_handle, descriptor, frame, result);
        },
        1, JitGeneratedRuntimeErrorReason::unknown_branch_condition,
        "instruction 1: branch condition is unknown or high impedance");
  }

  LlvmJit call_jit{LlvmJitOptions{optimization, {}}};
  const auto call_symbol =
      std::string{symbol_prefix} + "_nested_calls";
  call_jit.add_process(
      call_symbol, make_nested_call_process(),
      std::array<std::uint32_t, 1>{8});
  const auto call_handle = call_jit.lookup(call_symbol);
  TestRuntime call_runtime;
  auto call_descriptor = abi(call_runtime);
  const auto call_layout = call_jit.frame_layout(call_handle);
  std::vector<std::uint64_t> call_aval(
      call_layout.register_count);
  std::vector<std::uint64_t> call_bval(
      call_layout.register_count);
  std::vector<std::uint8_t> call_initialized(
      call_layout.register_count);
  fsim_jit_frame_v1 call_frame{};
  call_jit.initialize_frame(
      call_handle,
      call_frame,
      call_aval,
      call_bval,
      call_initialized);
  auto call_result = new_resume_result();
  auto call_status = call_jit.resume(
      call_handle,
      call_descriptor,
      call_frame,
      call_result);
  if (call_status == JitResumeStatus::debug_point) {
    assert(call_result.instruction == 9);
    call_status = call_jit.resume(
        call_handle,
        call_descriptor,
        call_frame,
        call_result);
  }
  assert(call_status == JitResumeStatus::completed);
  assert((
      call_runtime.signals[0]
      == EncodedSignal{UINT64_C(42), UINT64_C(0)}));

  const auto verify_call_error =
      [&](const std::string_view suffix,
          Process process,
          const std::uint32_t instruction,
          const JitGeneratedRuntimeErrorReason reason,
          const std::string_view message) {
        const auto symbol =
            std::string{symbol_prefix} + "_" + std::string{suffix};
        call_jit.add_process(
            symbol,
            process,
            std::array<std::uint32_t, 0>{});
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        expect_generated_runtime_error(
            [&] {
              (void)call_jit.execute(
                  call_jit.lookup(symbol), descriptor);
            },
            instruction,
            reason,
            message);
      };
  const CallStack one_entry_stack{0, 1, 1};
  const auto word =
      [](const std::uint64_t aval, const std::uint64_t bval = 0) {
        return PackedLogic4::from_aval_bval(
            32, aval, bval);
      };

  Process unknown_stack;
  unknown_stack.name = "unknown_call_stack";
  unknown_stack.register_count = 2;
  unknown_stack.operations = {
      LoadConstant{0, word(0, 1)},
      LoadConstant{1, word(0)},
      Return{one_entry_stack},
      Halt{}};
  verify_call_error(
      "unknown_call_stack",
      std::move(unknown_stack),
      2,
      JitGeneratedRuntimeErrorReason::call_stack_unknown,
      "instruction 2: call-stack state contains an unknown");

  Process overflowing_stack;
  overflowing_stack.name = "overflowing_call_stack";
  overflowing_stack.register_count = 2;
  overflowing_stack.operations = {
      LoadConstant{0, word(1)},
      LoadConstant{1, word(0)},
      Call{3, 3, one_entry_stack},
      Halt{}};
  verify_call_error(
      "overflowing_call_stack",
      std::move(overflowing_stack),
      2,
      JitGeneratedRuntimeErrorReason::call_stack_overflow,
      "instruction 2: call-stack capacity is exhausted");

  Process underflowing_stack;
  underflowing_stack.name = "underflowing_call_stack";
  underflowing_stack.register_count = 2;
  underflowing_stack.operations = {
      LoadConstant{0, word(0)},
      LoadConstant{1, word(0)},
      Return{one_entry_stack},
      Halt{}};
  verify_call_error(
      "underflowing_call_stack",
      std::move(underflowing_stack),
      2,
      JitGeneratedRuntimeErrorReason::call_stack_underflow,
      "instruction 2: call-stack underflow");

  Process invalid_return_stack;
  invalid_return_stack.name = "invalid_return_call_stack";
  invalid_return_stack.register_count = 2;
  invalid_return_stack.operations = {
      LoadConstant{0, word(1)},
      LoadConstant{1, word(99)},
      Return{one_entry_stack},
      Halt{}};
  verify_call_error(
      "invalid_return_call_stack",
      std::move(invalid_return_stack),
      2,
      JitGeneratedRuntimeErrorReason::call_stack_target,
      "instruction 2: call-stack return target is invalid");
}

void test_checked_integer_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  const auto integer = [](const std::int32_t value) {
    return PackedLogic4::from_aval_bval(
        32,
        static_cast<std::uint32_t>(value),
        0);
  };
  LlvmJit jit{LlvmJitOptions{optimization, {}}};

  Process success;
  success.id = 31;
  success.name = "checked_integer_success";
  success.register_count = 7;
  success.operations = {
      LoadConstant{0, integer(-5)},
      LoadConstant{1, integer(3)},
      IntegerBinary{IntegerBinaryOperator::add, 2, 0, 1},
      IntegerBinary{IntegerBinaryOperator::modulo, 3, 0, 1},
      IntegerBinary{IntegerBinaryOperator::remainder, 4, 0, 1},
      IntegerUnary{IntegerUnaryOperator::absolute, 5, 0},
      IntegerBinary{IntegerBinaryOperator::power, 6, 1, 1},
      IntegerCheck{2, -2, 2},
      WriteBlocking{0, 2},
      WriteBlocking{1, 3},
      WriteBlocking{2, 4},
      WriteBlocking{3, 5},
      WriteBlocking{4, 6},
      Halt{}};
  const auto success_symbol =
      std::string{symbol_prefix} + "_success";
  const std::array<std::uint32_t, 5> output_widths{
      32, 32, 32, 32, 32};
  jit.add_process(success_symbol, success, output_widths);
  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(jit.lookup(success_symbol), descriptor)
      == JitExecutionStatus::completed);
  const std::array<std::int32_t, 5> expected{
      -2, 1, -2, 5, 27};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    assert((
        runtime.signals[index]
        == EncodedSignal{
            static_cast<std::uint32_t>(expected[index]), 0}));
  }

  const auto add_failure =
      [&](const std::string_view suffix,
          std::vector<Operation> operations,
          const std::uint32_t instruction,
          const JitGeneratedRuntimeErrorReason reason,
          const std::string_view message) {
        Process process;
        process.id = 32;
        process.name = "checked_integer_failure";
        process.register_count = 4;
        process.operations = std::move(operations);
        const auto symbol =
            std::string{symbol_prefix} + "_" + std::string{suffix};
        jit.add_process(symbol, process, {});
        TestRuntime failed_runtime;
        auto failed_descriptor = abi(failed_runtime);
        const auto handle = jit.lookup(symbol);
        expect_generated_runtime_error(
            [&] {
              (void)jit.execute(
                  handle, failed_descriptor);
            },
            instruction,
            reason,
            message);
        const auto layout = jit.frame_layout(handle);
        std::vector<std::uint64_t> aval(layout.register_count);
        std::vector<std::uint64_t> bval(layout.register_count);
        std::vector<std::uint8_t> initialized(
            layout.register_count);
        fsim_jit_frame_v1 frame{};
        jit.initialize_frame(
            handle, frame, aval, bval, initialized);
        auto resume_result = new_resume_result();
        expect_generated_runtime_error(
            [&] {
              (void)jit.resume(
                  handle,
                  failed_descriptor,
                  frame,
                  resume_result);
            },
            instruction,
            reason,
            message);
        assert(
            resume_result.status
            == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR);
        assert(
            resume_result.delay
            == static_cast<std::uint64_t>(reason));
        assert(
            frame.state
            == FSIM_JIT_FRAME_STATE_RUNTIME_ERROR);
        expect_generated_runtime_error(
            [&] {
              (void)jit.resume(
                  handle,
                  failed_descriptor,
                  frame,
                  resume_result);
            },
            instruction,
            reason,
            message);
      };
  add_failure(
      "overflow",
      {
          LoadConstant{
              0, integer(std::numeric_limits<std::int32_t>::max())},
          LoadConstant{1, integer(1)},
          IntegerBinary{IntegerBinaryOperator::add, 2, 0, 1},
          Halt{}},
      2,
      JitGeneratedRuntimeErrorReason::integer_overflow,
      "instruction 2: VHDL integer arithmetic overflow");
  add_failure(
      "division_zero",
      {
          LoadConstant{0, integer(7)},
          LoadConstant{1, integer(0)},
          IntegerBinary{IntegerBinaryOperator::divide, 2, 0, 1},
          Halt{}},
      2,
      JitGeneratedRuntimeErrorReason::integer_division_by_zero,
      "instruction 2: VHDL integer division by zero");
  add_failure(
      "negative_exponent",
      {
          LoadConstant{0, integer(2)},
          LoadConstant{1, integer(-1)},
          IntegerBinary{IntegerBinaryOperator::power, 2, 0, 1},
          Halt{}},
      2,
      JitGeneratedRuntimeErrorReason::integer_negative_exponent,
      "instruction 2: VHDL integer exponent must be nonnegative");
  add_failure(
      "range",
      {
          LoadConstant{0, integer(8)},
          IntegerCheck{0, -5, 7},
          Halt{}},
      1,
      JitGeneratedRuntimeErrorReason::integer_subtype_range,
      "instruction 1: VHDL integer subtype range check failed");
  auto unknown = integer(0);
  unknown.set(7, Logic4::x);
  add_failure(
      "unknown",
      {
          LoadConstant{0, std::move(unknown)},
          IntegerUnary{IntegerUnaryOperator::absolute, 1, 0},
          Halt{}},
      1,
      JitGeneratedRuntimeErrorReason::integer_operand_unknown,
      "instruction 1: VHDL integer operand contains an unknown or "
      "high-impedance value");
}

[[nodiscard]] Process make_resumable_process() {
  Process process;
  process.id = 0;
  process.name = "resumable";
  process.register_count = 2;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
      WriteBlocking{0, 0},
      WaitFor{5},
      UnaryNot{1, 0},
      WriteBlocking{1, 1},
      Yield{},
      Pause{},
      LoadConstant{0, PackedLogic4::from_msb_string("00111100")},
      WriteBlocking{0, 0},
      Stop{},
  };
  return process;
}

void test_resumable_at_level(const JitOptimizationLevel optimization,
                             const std::string_view symbol_prefix) {
  const auto symbol = std::string{symbol_prefix} + "_resumable";
  const auto process = make_resumable_process();
  const std::array<std::uint32_t, 2> widths{8, 8};
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  assert(layout.register_count == process.register_count);
  assert(layout.layout_id_low != 0 || layout.layout_id_high != 0);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  expect_unsupported([&] { (void)jit.execute(handle, descriptor); },
                     "compiled process can suspend");
  assert(runtime.writes.empty());

  std::vector<std::uint64_t> register_aval(layout.register_count,
                                           UINT64_MAX);
  std::vector<std::uint64_t> register_bval(layout.register_count,
                                           UINT64_MAX);
  std::vector<std::uint8_t> register_initialized(
      layout.register_count, UINT8_MAX);
  fsim_jit_frame_v1 frame{};
  jit.initialize_frame(
      handle, frame, register_aval, register_bval,
      register_initialized);
  assert(std::all_of(register_aval.begin(), register_aval.end(),
                     [](const auto value) { return value == 0; }));
  assert(std::all_of(register_bval.begin(), register_bval.end(),
                     [](const auto value) { return value == 0; }));
  assert(std::all_of(
      register_initialized.begin(), register_initialized.end(),
      [](const auto value) { return value == 0; }));
  assert(frame.layout_id_low == layout.layout_id_low);
  assert(frame.layout_id_high == layout.layout_id_high);
  assert(frame.program_counter == 0);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY);

  auto result = new_resume_result();
  {
    auto wrong = frame;
    wrong.abi_version = 99;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame ABI version mismatch");
  }
  {
    auto wrong = frame;
    wrong.struct_size =
        static_cast<std::uint32_t>(
            offsetof(fsim_jit_frame_v1, register_logic9_plane2) - 1U);
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame ABI structure is too small");
  }
  {
    auto wrong = frame;
    wrong.layout_id_low ^= UINT64_C(1);
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame layout mismatch");
  }
  {
    auto wrong = frame;
    ++wrong.register_count;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame layout mismatch");
  }
  {
    auto wrong = frame;
    wrong.register_aval = nullptr;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame register storage is null");
  }
  {
    auto wrong = frame;
    wrong.register_initialized = nullptr;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame register storage is null");
  }
  {
    auto wrong = frame;
    wrong.program_counter =
        static_cast<std::uint32_t>(process.operations.size());
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "program counter is outside");
  }
  {
    auto wrong = frame;
    wrong.state = 99;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame state is invalid");
  }
  {
    auto wrong_result = result;
    wrong_result.abi_version = 99;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, frame, wrong_result); },
        "resume-result ABI version mismatch");
  }
  {
    auto wrong_result = result;
    wrong_result.struct_size =
        static_cast<std::uint32_t>(
            sizeof(fsim_jit_resume_result_v1) - 1U);
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, frame, wrong_result); },
        "resume-result ABI structure is too small");
  }
  {
    std::array<std::uint64_t, 1> too_small_aval{};
    std::array<std::uint64_t, 1> too_small_bval{};
    std::array<std::uint8_t, 1> too_small_initialized{};
    fsim_jit_frame_v1 unused{};
    expect_error(
        [&] {
          jit.initialize_frame(
              handle, unused, too_small_aval, too_small_bval,
              too_small_initialized);
        },
        "register storage is smaller");
  }
  {
    std::vector<std::uint64_t> aliased(layout.register_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 unused{};
    expect_error(
        [&] {
          jit.initialize_frame(
              handle, unused, aliased, aliased, initialized);
        },
        "must be distinct");
  }

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_for);
  assert(result.status == FSIM_JIT_RESUME_STATUS_WAIT_FOR);
  assert(result.instruction == 2);
  assert(result.delay == 5);
  assert(frame.program_counter == 3);
  assert(frame.last_instruction == 2);
  assert(register_aval[0] == UINT64_C(0xa5));
  assert(register_bval[0] == 0);
  assert(register_initialized[0] == 1);
  assert(register_initialized[1] == 0);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::yielded);
  assert(result.status == FSIM_JIT_RESUME_STATUS_YIELDED);
  assert(result.instruction == 5);
  assert(result.delay == 0);
  assert(frame.program_counter == 6);
  assert(register_aval[1] == UINT64_C(0x5a));
  assert(register_bval[1] == 0);
  assert(register_initialized[0] == 1);
  assert(register_initialized[1] == 1);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::paused);
  assert(result.status == FSIM_JIT_RESUME_STATUS_PAUSED);
  assert(result.instruction == 6);
  assert(result.delay == 0);
  assert(frame.program_counter == 7);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::stopped);
  assert(result.status == FSIM_JIT_RESUME_STATUS_STOPPED);
  assert(result.instruction == 9);
  assert(frame.program_counter == process.operations.size());
  assert(frame.state == FSIM_JIT_FRAME_STATE_STOPPED);
  const auto writes_before_terminal_resume = runtime.writes.size();
  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::stopped);
  assert(runtime.writes.size() == writes_before_terminal_resume);

  fsim::runtime::simir::Interpreter interpreter;
  (void)interpreter.add_signal(
      {"result0", PackedLogic4::from_msb_string("00000000")});
  (void)interpreter.add_signal(
      {"result1", PackedLogic4::from_msb_string("00000000")});
  std::vector<std::pair<std::uint32_t, EncodedSignal>> reference_writes;
  interpreter.set_signal_change_hook(
      [&](const SignalId signal, const PackedLogic4 &value,
          const fsim::runtime::SimulationTick) {
        reference_writes.emplace_back(signal, encode(value));
      });
  (void)interpreter.add_process(process);
  const auto paused_result = interpreter.run();
  assert(paused_result.status == fsim::runtime::RunStatus::stopped);
  assert(paused_result.time == 5);
  assert(!interpreter.stopped_by_design());
  interpreter.scheduler().clear_stop();
  const auto reference_result = interpreter.run();
  assert(reference_result.status == fsim::runtime::RunStatus::stopped);
  assert(interpreter.stopped_by_design());
  assert(runtime.writes == reference_writes);
  assert(encode(interpreter.signal_value(0)) == runtime.signals[0]);
  assert(encode(interpreter.signal_value(1)) == runtime.signals[1]);

  Process terminal_stop;
  terminal_stop.id = 1;
  terminal_stop.name = "terminal_stop";
  terminal_stop.operations = {Stop{}};
  const auto stop_symbol = std::string{symbol_prefix} + "_stop";
  const std::array<std::uint32_t, 0> no_signals{};
  jit.add_process(stop_symbol, terminal_stop, no_signals);
  assert(jit.execute(jit.lookup(stop_symbol), descriptor) ==
         JitExecutionStatus::stopped);

  Process safe_loop;
  safe_loop.id = 2;
  safe_loop.name = "safe_loop";
  safe_loop.register_count = 1;
  safe_loop.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      WaitFor{1},
      UnaryNot{0, 0},
      WriteBlocking{0, 0},
      Yield{},
      Jump{1},
  };
  const auto loop_symbol = std::string{symbol_prefix} + "_safe_loop";
  const std::array<std::uint32_t, 1> scalar_width{1};
  jit.add_process(loop_symbol, safe_loop, scalar_width);
  const auto loop_handle = jit.lookup(loop_symbol);
  const auto loop_layout = jit.frame_layout(loop_handle);
  std::vector<std::uint64_t> loop_aval(loop_layout.register_count);
  std::vector<std::uint64_t> loop_bval(loop_layout.register_count);
  std::vector<std::uint8_t> loop_initialized(
      loop_layout.register_count);
  fsim_jit_frame_v1 loop_frame{};
  jit.initialize_frame(
      loop_handle, loop_frame, loop_aval, loop_bval,
      loop_initialized);
  auto loop_result = new_resume_result();
  TestRuntime loop_runtime;
  auto loop_descriptor = abi(loop_runtime);
  assert(jit.resume(
             loop_handle, loop_descriptor, loop_frame, loop_result) ==
         JitResumeStatus::wait_for);
  assert(jit.resume(
             loop_handle, loop_descriptor, loop_frame, loop_result) ==
         JitResumeStatus::yielded);
  assert((loop_runtime.signals[0] == EncodedSignal{1, 0}));
  assert(jit.resume(
             loop_handle, loop_descriptor, loop_frame, loop_result) ==
         JitResumeStatus::wait_for);
  assert(jit.resume(
             loop_handle, loop_descriptor, loop_frame, loop_result) ==
         JitResumeStatus::yielded);
  assert((loop_runtime.signals[0] == EncodedSignal{0, 0}));
}

[[nodiscard]] Process make_signal_wait_process() {
  Process process;
  process.id = 7;
  process.name = "signal_waits";
  process.static_sensitivity = {
      {0, EdgeKind::posedge},
      {1, EdgeKind::any},
  };
  process.operations = {
      WaitOn{
          {2, 0, 2},
          {EdgeKind::any, EdgeKind::posedge, EdgeKind::any}},
      WaitSensitivity{},
      WaitForever{},
      Halt{},
  };
  return process;
}

void test_signal_waits_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const auto process = make_signal_wait_process();
  const std::array<std::uint32_t, 3> widths{1, 128, 257};
  const auto symbol = std::string{symbol_prefix} + "_signal_waits";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  expect_unsupported(
      [&] { (void)jit.execute(handle, descriptor); },
      "compiled process can suspend");

  const auto layout = jit.frame_layout(handle);
  assert(layout.register_count == 0);
  std::vector<std::uint64_t> register_aval;
  std::vector<std::uint64_t> register_bval;
  std::vector<std::uint8_t> register_initialized;
  fsim_jit_frame_v1 frame{};
  jit.initialize_frame(
      handle, frame, register_aval, register_bval,
      register_initialized);
  auto result = new_resume_result();

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_on);
  assert(result.status == FSIM_JIT_RESUME_STATUS_WAIT_ON);
  assert(result.instruction == 0);
  assert(result.delay == 0);
  assert(frame.program_counter == 1);
  assert(frame.last_instruction == 0);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY);
  assert((std::get<WaitOn>(process.operations[0]).signals ==
          std::vector<SignalId>{2, 0, 2}));
  assert((
      std::get<WaitOn>(process.operations[0]).edges
      == std::vector<EdgeKind>{
          EdgeKind::any, EdgeKind::posedge, EdgeKind::any}));

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_sensitivity);
  assert(result.status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY);
  assert(result.instruction == 1);
  assert(result.delay == 0);
  assert(frame.program_counter == 2);
  assert(frame.last_instruction == 1);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY);
  assert(process.static_sensitivity.size() == 2);
  assert(process.static_sensitivity[0].signal == 0);
  assert(process.static_sensitivity[0].edge == EdgeKind::posedge);
  assert(process.static_sensitivity[1].signal == 1);
  assert(process.static_sensitivity[1].edge == EdgeKind::any);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_forever);
  assert(result.status == FSIM_JIT_RESUME_STATUS_WAIT_FOREVER);
  assert(result.instruction == 2);
  assert(result.delay == 0);
  assert(frame.program_counter == 3);
  assert(frame.last_instruction == 2);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::completed);
  assert(result.status == FSIM_JIT_RESUME_STATUS_COMPLETED);
  assert(result.instruction == 3);
  assert(frame.program_counter == process.operations.size());
  assert(frame.last_instruction == 3);
  assert(frame.state == FSIM_JIT_FRAME_STATE_COMPLETED);
  assert(runtime.writes.empty());
  assert(runtime.scheduled_writes.empty());

  Process wait_on_loop;
  wait_on_loop.id = 8;
  wait_on_loop.name = "wait_on_loop";
  wait_on_loop.operations = {
      WaitOn{{0}},
      Jump{0},
  };
  const auto wait_on_symbol = std::string{symbol_prefix} + "_wait_on_loop";
  jit.add_process(wait_on_symbol, wait_on_loop, widths);
  const auto wait_on_handle = jit.lookup(wait_on_symbol);
  fsim_jit_frame_v1 wait_on_frame{};
  jit.initialize_frame(
      wait_on_handle, wait_on_frame, register_aval, register_bval,
      register_initialized);
  auto wait_on_result = new_resume_result();
  assert(jit.resume(
             wait_on_handle, descriptor, wait_on_frame, wait_on_result) ==
         JitResumeStatus::wait_on);
  assert(wait_on_frame.program_counter == 1);
  assert(jit.resume(
             wait_on_handle, descriptor, wait_on_frame, wait_on_result) ==
         JitResumeStatus::wait_on);
  assert(wait_on_result.instruction == 0);
  assert(wait_on_frame.program_counter == 1);

  Process sensitivity_loop;
  sensitivity_loop.id = 9;
  sensitivity_loop.name = "sensitivity_loop";
  sensitivity_loop.static_sensitivity = {{1, EdgeKind::any}};
  sensitivity_loop.operations = {
      WaitSensitivity{},
      Jump{0},
  };
  const auto sensitivity_symbol =
      std::string{symbol_prefix} + "_sensitivity_loop";
  jit.add_process(sensitivity_symbol, sensitivity_loop, widths);
  const auto sensitivity_handle = jit.lookup(sensitivity_symbol);
  fsim_jit_frame_v1 sensitivity_frame{};
  jit.initialize_frame(
      sensitivity_handle, sensitivity_frame, register_aval, register_bval,
      register_initialized);
  auto sensitivity_result = new_resume_result();
  assert(jit.resume(
             sensitivity_handle, descriptor, sensitivity_frame,
             sensitivity_result) ==
         JitResumeStatus::wait_sensitivity);
  assert(sensitivity_frame.program_counter == 1);
  assert(jit.resume(
             sensitivity_handle, descriptor, sensitivity_frame,
             sensitivity_result) ==
         JitResumeStatus::wait_sensitivity);
  assert(sensitivity_result.instruction == 0);
  assert(sensitivity_frame.program_counter == 1);

  Process timed_wait;
  timed_wait.id = 10;
  timed_wait.name = "timed_wait";
  timed_wait.register_count = 1;
  WaitOn timed_boundary;
  timed_boundary.timeout = 7;
  timed_boundary.timeout_result = 0;
  timed_wait.operations = {
      std::move(timed_boundary),
      Halt{},
  };
  const auto timed_symbol =
      std::string{symbol_prefix} + "_timed_wait";
  jit.add_process(timed_symbol, timed_wait, widths);
  const auto timed_handle = jit.lookup(timed_symbol);
  std::vector<std::uint64_t> timed_aval(1);
  std::vector<std::uint64_t> timed_bval(1);
  std::vector<std::uint8_t> timed_initialized(1);
  fsim_jit_frame_v1 timed_frame{};
  jit.initialize_frame(
      timed_handle, timed_frame, timed_aval, timed_bval,
      timed_initialized);
  auto timed_result = new_resume_result();
  assert(
      jit.resume(
          timed_handle,
          descriptor,
          timed_frame,
          timed_result)
      == JitResumeStatus::wait_on);
  assert(timed_result.instruction == 0);
  assert(timed_result.delay == 7);
  assert(timed_frame.program_counter == 1);
}

} // namespace fsim::tests::compiler

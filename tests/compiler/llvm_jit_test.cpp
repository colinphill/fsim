// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/compiler/object_cache.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using fsim::compiler::JitExecutionStatus;
using fsim::compiler::JitGeneratedRuntimeErrorReason;
using fsim::compiler::JitOptimizationLevel;
using fsim::compiler::JitProcessHandle;
using fsim::compiler::JitResumeStatus;
using fsim::compiler::LlvmJit;
using fsim::compiler::LlvmJitError;
using fsim::compiler::LlvmJitGeneratedRuntimeError;
using fsim::compiler::LlvmJitOptions;
using fsim::compiler::LlvmJitUnsupportedError;
using fsim::runtime::PackedLogic4;
using fsim::runtime::Logic4;
using namespace fsim::runtime::simir;

struct EncodedSignal {
  std::uint64_t aval{};
  std::uint64_t bval{};

  friend bool operator==(EncodedSignal, EncodedSignal) = default;
};

struct TestRuntime {
  std::array<EncodedSignal, 8> signals{};
  std::uint32_t assertion_count{};
  std::uint32_t failed_process{};
  std::uint32_t failed_instruction{};
  std::string assertion_message;
  bool leave_bval_untouched{};
  std::vector<std::pair<std::uint32_t, EncodedSignal>> writes;
};

extern "C" std::uint64_t read_signal(void *opaque,
                                      const std::uint32_t signal,
                                      std::uint64_t *bval) {
  auto &runtime = *static_cast<TestRuntime *>(opaque);
  assert(signal < runtime.signals.size());
  if (!runtime.leave_bval_untouched) {
    *bval = runtime.signals[signal].bval;
  }
  return runtime.signals[signal].aval;
}

extern "C" void write_signal(void *opaque, const std::uint32_t signal,
                              const std::uint64_t aval,
                              const std::uint64_t bval) {
  auto &runtime = *static_cast<TestRuntime *>(opaque);
  assert(signal < runtime.signals.size());
  runtime.signals[signal] = {aval, bval};
  runtime.writes.emplace_back(signal, runtime.signals[signal]);
}

extern "C" void assert_failed(void *opaque, const std::uint32_t process,
                               const std::uint32_t instruction,
                               const char *message,
                               const std::uint64_t message_size) {
  auto &runtime = *static_cast<TestRuntime *>(opaque);
  ++runtime.assertion_count;
  runtime.failed_process = process;
  runtime.failed_instruction = instruction;
  runtime.assertion_message.assign(
      message, static_cast<std::size_t>(message_size));
}

[[nodiscard]] fsim_jit_runtime_v1 abi(TestRuntime &runtime) {
  return {
      FSIM_JIT_RUNTIME_ABI_VERSION_V1,
      static_cast<std::uint32_t>(sizeof(fsim_jit_runtime_v1)),
      &runtime,
      &read_signal,
      &write_signal,
      &assert_failed,
  };
}

[[nodiscard]] fsim_jit_resume_result_v1 new_resume_result() {
  return {
      FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1,
      static_cast<std::uint32_t>(sizeof(fsim_jit_resume_result_v1)),
      0,
      FSIM_JIT_INVALID_INSTRUCTION,
      0,
  };
}

[[nodiscard]] Process make_arithmetic_process() {
  Process process;
  process.id = 7;
  process.name = "arithmetic";
  process.register_count = 10;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::bit_and, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::bit_or, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::bit_xor, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::add_unsigned, 5, 0, 1},
      WriteBlocking{5, 5},
      UnaryNot{6, 0},
      WriteBlocking{6, 6},
      Binary{BinaryOperator::equal, 7, 0, 1},
      WriteBlocking{7, 7},
      LoadConstant{8, PackedLogic4::from_msb_string("01000100")},
      Binary{BinaryOperator::equal, 9, 5, 8},
      Assert{9, "unexpected sum"},
      Halt{},
  };
  return process;
}

template <class Function>
void expect_error(Function &&function, const std::string_view fragment) {
  bool rejected = false;
  try {
    std::forward<Function>(function)();
  } catch (const LlvmJitError &error) {
    rejected = true;
    assert(std::string_view{error.what()}.find(fragment) !=
           std::string_view::npos);
  }
  assert(rejected);
}

template <class Function>
void expect_unsupported(Function &&function,
                        const std::string_view fragment) {
  bool rejected = false;
  try {
    std::forward<Function>(function)();
  } catch (const LlvmJitUnsupportedError &error) {
    rejected = true;
    assert(std::string_view{error.what()}.find(fragment) !=
           std::string_view::npos);
  } catch (const LlvmJitError &) {
    assert(false && "capability miss was not typed as unsupported");
  }
  assert(rejected);
}

template <class Function>
void expect_fatal_error(Function &&function,
                        const std::string_view fragment) {
  bool rejected = false;
  try {
    std::forward<Function>(function)();
  } catch (const LlvmJitGeneratedRuntimeError &) {
    assert(false && "compiler or ABI failure was typed as generated runtime");
  } catch (const LlvmJitUnsupportedError &) {
    assert(false && "malformed IR or ABI failure was typed unsupported");
  } catch (const LlvmJitError &error) {
    rejected = true;
    assert(std::string_view{error.what()}.find(fragment) !=
           std::string_view::npos);
  }
  assert(rejected);
}

template <class Function>
void expect_generated_runtime_error(
    Function &&function, const std::uint32_t instruction,
    const JitGeneratedRuntimeErrorReason reason,
    const std::string_view fragment) {
  bool rejected = false;
  try {
    std::forward<Function>(function)();
  } catch (const LlvmJitGeneratedRuntimeError &error) {
    rejected = true;
    assert(error.instruction() == instruction);
    assert(error.reason() == reason);
    assert(std::string_view{error.what()}.find(fragment) !=
           std::string_view::npos);
  } catch (const LlvmJitError &) {
    assert(false && "generated runtime failure lost its typed metadata");
  }
  assert(rejected);
}

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
  assert((runtime.signals[5] == EncodedSignal{0x47, 0x07}));
  assert((runtime.signals[6] == EncodedSignal{0xcb, 0x01}));
  assert((runtime.signals[7] == EncodedSignal{1, 1}));

  auto wrong_abi = descriptor;
  wrong_abi.abi_version = 99;
  expect_fatal_error([&] { (void)jit.execute(handle, wrong_abi); },
                     "ABI version mismatch");
  expect_error([&] { (void)jit.lookup("not_added"); }, "was not added");
  assert(jit.cache_statistics() ==
         fsim::compiler::LlvmJitCacheStatistics{});
}

[[nodiscard]] EncodedSignal encode(const Logic4 value) {
  switch (value) {
  case Logic4::zero:
    return {0, 0};
  case Logic4::one:
    return {1, 0};
  case Logic4::x:
    return {1, 1};
  case Logic4::z:
    return {0, 1};
  }
  return {1, 1};
}

[[nodiscard]] EncodedSignal encode(const PackedLogic4 &value) {
  assert(value.width() > 0);
  assert(value.width() <= 64);
  return {value.aval_words().front(), value.bval_words().front()};
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
    fsim_jit_frame_v1 frame{};
    jit.initialize_frame(
        error_handle, frame, register_aval, register_bval);
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
  fsim_jit_frame_v1 frame{};
  jit.initialize_frame(handle, frame, register_aval, register_bval);
  assert(std::all_of(register_aval.begin(), register_aval.end(),
                     [](const auto value) { return value == 0; }));
  assert(std::all_of(register_bval.begin(), register_bval.end(),
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
        static_cast<std::uint32_t>(sizeof(fsim_jit_frame_v1) - 1U);
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
    fsim_jit_frame_v1 unused{};
    expect_error(
        [&] {
          jit.initialize_frame(
              handle, unused, too_small_aval, too_small_bval);
        },
        "register storage is smaller");
  }
  {
    std::vector<std::uint64_t> aliased(layout.register_count);
    fsim_jit_frame_v1 unused{};
    expect_error(
        [&] { jit.initialize_frame(handle, unused, aliased, aliased); },
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

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::yielded);
  assert(result.status == FSIM_JIT_RESUME_STATUS_YIELDED);
  assert(result.instruction == 5);
  assert(result.delay == 0);
  assert(frame.program_counter == 6);
  assert(register_aval[1] == UINT64_C(0x5a));
  assert(register_bval[1] == 0);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::stopped);
  assert(result.status == FSIM_JIT_RESUME_STATUS_STOPPED);
  assert(result.instruction == 8);
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
  const auto reference_result = interpreter.run();
  assert(reference_result.status == fsim::runtime::RunStatus::stopped);
  assert(reference_result.time == 5);
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
  fsim_jit_frame_v1 loop_frame{};
  jit.initialize_frame(
      loop_handle, loop_frame, loop_aval, loop_bval);
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

[[nodiscard]] Logic4 equality(const Logic4 lhs, const Logic4 rhs) {
  const auto known = [](const Logic4 value) {
    return value == Logic4::zero || value == Logic4::one;
  };
  if (!known(lhs) || !known(rhs)) {
    return Logic4::x;
  }
  return lhs == rhs ? Logic4::one : Logic4::zero;
}

void test_scalar_truth_tables_and_64_bits() {
  LlvmJit jit;
  Process scalar;
  scalar.id = 0;
  scalar.name = "scalar";
  scalar.register_count = 8;
  scalar.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::bit_and, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::bit_or, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::bit_xor, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::add_unsigned, 5, 0, 1},
      WriteBlocking{5, 5},
      UnaryNot{6, 0},
      WriteBlocking{6, 6},
      Binary{BinaryOperator::equal, 7, 0, 1},
      WriteBlocking{7, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 8> scalar_widths{1, 1, 1, 1, 1, 1, 1, 1};
  jit.add_process("scalar_truth_table", scalar, scalar_widths);
  const auto scalar_handle = jit.lookup("scalar_truth_table");

  constexpr std::array states{Logic4::zero, Logic4::one, Logic4::x,
                              Logic4::z};
  for (const auto lhs : states) {
    for (const auto rhs : states) {
      TestRuntime runtime;
      runtime.signals[0] = encode(lhs);
      runtime.signals[1] = encode(rhs);
      auto descriptor = abi(runtime);
      assert(jit.execute(scalar_handle, descriptor) ==
             JitExecutionStatus::completed);
      assert(runtime.signals[2] == encode(fsim::runtime::logic_and(lhs, rhs)));
      assert(runtime.signals[3] == encode(fsim::runtime::logic_or(lhs, rhs)));
      assert(runtime.signals[4] == encode(fsim::runtime::logic_xor(lhs, rhs)));
      // The carry is discarded for a one-bit unsigned addition.
      assert(runtime.signals[5] == encode(fsim::runtime::logic_xor(lhs, rhs)));
      assert(runtime.signals[6] == encode(fsim::runtime::logic_not(lhs)));
      assert(runtime.signals[7] == encode(equality(lhs, rhs)));
    }
  }

  Process wide;
  wide.id = 1;
  wide.name = "wide";
  wide.register_count = 2;
  wide.operations = {
      ReadSignal{0, 0},
      UnaryNot{1, 0},
      WriteBlocking{1, 1},
      Halt{},
  };
  const std::array<std::uint32_t, 2> wide_widths{64, 64};
  jit.add_process("wide_not", wide, wide_widths);
  const auto wide_handle = jit.lookup("wide_not");
  TestRuntime runtime;
  runtime.signals[0] = {UINT64_C(0x0123456789abcdef),
                        UINT64_C(0x8000000000000000)};
  auto descriptor = abi(runtime);
  assert(jit.execute(wide_handle, descriptor) ==
         JitExecutionStatus::completed);
  assert((runtime.signals[1] ==
          EncodedSignal{UINT64_C(0xfedcba9876543210) |
                            UINT64_C(0x8000000000000000),
                        UINT64_C(0x8000000000000000)}));
}

void test_initialized_bval_slot(const JitOptimizationLevel optimization,
                                const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  Process process;
  process.id = 2;
  process.name = "initialized_bval";
  process.register_count = 1;
  process.operations = {
      ReadSignal{0, 0},
      WriteBlocking{1, 0},
      Halt{},
  };
  const std::array<std::uint32_t, 2> widths{8, 8};
  jit.add_process(symbol, process, widths);

  TestRuntime runtime;
  runtime.signals[0] = {UINT64_C(0xa5), UINT64_C(0xff)};
  runtime.leave_bval_untouched = true;
  auto descriptor = abi(runtime);
  assert(jit.execute(jit.lookup(symbol), descriptor) ==
         JitExecutionStatus::completed);
  assert((runtime.signals[1] == EncodedSignal{UINT64_C(0xa5), 0}));
}

[[nodiscard]] Process make_cached_process(const std::string_view value) {
  Process process;
  process.id = 11;
  process.name = "cached_process";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string(value)},
      WriteBlocking{0, 0},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process
make_cached_signal_process(const SignalId signal) {
  Process process;
  process.id = 12;
  process.name = "cached_signal_process";
  process.register_count = 1;
  process.operations = {
      ReadSignal{0, signal},
      Halt{},
  };
  return process;
}

void expect_cache_statistics(const LlvmJit &jit, const std::uint64_t hits,
                             const std::uint64_t misses,
                             const std::uint64_t stores,
                             const std::uint64_t rejected_entries = 0) {
  const auto statistics = jit.cache_statistics();
  assert(statistics.hits == hits);
  assert(statistics.misses == misses);
  assert(statistics.stores == stores);
  assert(statistics.rejected_entries == rejected_entries);
  assert(statistics.load_failures == 0);
  assert(statistics.store_failures == 0);
}

void run_cached_process(LlvmJit &jit, const std::string_view symbol,
                        const EncodedSignal expected) {
  const auto handle = jit.lookup(symbol);
  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  assert(runtime.signals[0] == expected);
}

void run_cached_signal_process(LlvmJit &jit,
                               const std::string_view symbol) {
  const auto handle = jit.lookup(symbol);
  TestRuntime runtime;
  runtime.signals[0] = {UINT64_C(0xa5), 0};
  runtime.signals[1] = {UINT64_C(0x3c), 0};
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
}

[[nodiscard]] std::vector<std::filesystem::path>
cached_object_paths(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> result;
  if (!std::filesystem::exists(root)) {
    return result;
  }
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator{root}) {
    if (entry.is_regular_file() && entry.path().extension() == ".fobj") {
      result.push_back(entry.path());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

void test_object_cache_at_level(const JitOptimizationLevel optimization,
                                const std::filesystem::path &cache_directory) {
  const std::array<std::uint32_t, 2> widths{8, 1};
  constexpr std::string_view symbol = "persistent_cache_process";
  const auto options = LlvmJitOptions{optimization, cache_directory};

  {
    LlvmJit cold{options};
    cold.add_process(symbol, make_cached_process("10100101"), widths);
    expect_cache_statistics(cold, 0, 0, 0);
    run_cached_process(cold, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 1);

  {
    LlvmJit warm{options};
    warm.add_process(symbol, make_cached_process("10100101"), widths);
    run_cached_process(warm, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(warm, 1, 0, 0);
  }

  const auto cached_objects = cached_object_paths(cache_directory);
  assert(cached_objects.size() == 1);
  {
    std::ofstream stream{cached_objects.front(),
                         std::ios::binary | std::ios::trunc};
    assert(stream);
    stream << "corrupt";
    stream.flush();
    assert(stream);
  }
  {
    LlvmJit recovered{options};
    recovered.add_process(symbol, make_cached_process("10100101"), widths);
    run_cached_process(recovered, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(recovered, 0, 1, 1, 1);
  }
  {
    LlvmJit healed{options};
    healed.add_process(symbol, make_cached_process("10100101"), widths);
    run_cached_process(healed, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(healed, 1, 0, 0);
  }

  // A checksum-valid payload can still be an incompatible native object.
  // The LLVM adapter validates its object structure before handing it to ORC.
  {
    const std::array incompatible{
        std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};
    fsim::compiler::ObjectCache storage{
        cache_directory / "llvm" / "objects"};
    std::error_code error;
    assert(storage.store(cached_objects.front().stem().string(),
                         incompatible, error));
    assert(!error);
  }
  {
    LlvmJit recovered{options};
    recovered.add_process(symbol, make_cached_process("10100101"), widths);
    run_cached_process(recovered, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(recovered, 0, 1, 1, 1);
  }

  // Executable SimIR contents participate in the key.
  {
    LlvmJit changed_process{options};
    changed_process.add_process(
        symbol, make_cached_process("00111100"), widths);
    run_cached_process(changed_process, symbol,
                       EncodedSignal{UINT64_C(0x3c), 0});
    expect_cache_statistics(changed_process, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 2);

  // Unrelated elaborated signals do not invalidate a process-local object.
  {
    LlvmJit changed_widths{options};
    const std::array<std::uint32_t, 2> other_widths{8, 2};
    changed_widths.add_process(
        symbol, make_cached_process("10100101"), other_widths);
    run_cached_process(changed_widths, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(changed_widths, 1, 0, 0);
  }
  assert(cached_object_paths(cache_directory).size() == 2);

  constexpr std::string_view signal_symbol =
      "persistent_cache_signal_process";
  const std::array<std::uint32_t, 3> signal_widths{8, 8, 1};
  {
    LlvmJit cold{options};
    cold.add_process(
        signal_symbol, make_cached_signal_process(0), signal_widths);
    run_cached_signal_process(cold, signal_symbol);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 3);

  // Changing only an unreferenced signal width reuses the cached object.
  {
    LlvmJit unrelated_width{options};
    const std::array<std::uint32_t, 3> widths_with_unrelated_change{8, 8, 2};
    unrelated_width.add_process(
        signal_symbol, make_cached_signal_process(0),
        widths_with_unrelated_change);
    run_cached_signal_process(unrelated_width, signal_symbol);
    expect_cache_statistics(unrelated_width, 1, 0, 0);
  }
  assert(cached_object_paths(cache_directory).size() == 3);

  // A referenced signal's width remains part of the process cache key.
  {
    LlvmJit referenced_width{options};
    const std::array<std::uint32_t, 3> widths_with_referenced_change{9, 8, 1};
    referenced_width.add_process(
        signal_symbol, make_cached_signal_process(0),
        widths_with_referenced_change);
    run_cached_signal_process(referenced_width, signal_symbol);
    expect_cache_statistics(referenced_width, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 4);

  // Signal IDs remain process inputs even when the referenced widths match.
  {
    LlvmJit changed_signal{options};
    changed_signal.add_process(
        signal_symbol, make_cached_signal_process(1), signal_widths);
    run_cached_signal_process(changed_signal, signal_symbol);
    expect_cache_statistics(changed_signal, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 5);
}

void test_optimization_cache_invalidation(
    const std::filesystem::path &cache_directory) {
  const std::array<std::uint32_t, 2> widths{8, 1};
  constexpr std::string_view symbol = "optimization_cache_process";
  const auto process = make_cached_process("10100101");

  {
    LlvmJit o0{
        LlvmJitOptions{JitOptimizationLevel::o0, cache_directory}};
    o0.add_process(symbol, process, widths);
    run_cached_process(o0, symbol, EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(o0, 0, 1, 1);
  }
  {
    LlvmJit o2{
        LlvmJitOptions{JitOptimizationLevel::o2, cache_directory}};
    o2.add_process(symbol, process, widths);
    run_cached_process(o2, symbol, EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(o2, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 2);
  {
    LlvmJit warm_o2{
        LlvmJitOptions{JitOptimizationLevel::o2, cache_directory}};
    warm_o2.add_process(symbol, process, widths);
    run_cached_process(warm_o2, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(warm_o2, 1, 0, 0);
  }
}

void test_persistent_object_cache() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("fsim-llvm-object-cache-" + std::to_string(serial));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  assert(!error);

  test_object_cache_at_level(JitOptimizationLevel::o0, root / "o0");
  test_object_cache_at_level(JitOptimizationLevel::o2, root / "o2");
  test_optimization_cache_invalidation(root / "optimization");

  std::filesystem::remove_all(root, error);
  assert(!error);
}

void test_rejections() {
  LlvmJit jit;
  const std::vector<std::pair<Operation, std::string_view>> unsupported{
      {WriteUpdate{0, 0}, "WriteUpdate"},
      {WriteAfter{0, 0, 1}, "WriteAfter"},
      {WaitOn{{0}}, "WaitOn"},
      {WaitSensitivity{}, "WaitSensitivity"},
  };
  std::uint32_t suffix = 0;
  for (const auto &[operation, diagnostic] : unsupported) {
    Process rejected;
    rejected.id = 0;
    rejected.name = std::string{diagnostic};
    rejected.register_count = 1;
    rejected.operations = {
        LoadConstant{0, PackedLogic4::from_msb_string("0")},
        operation,
        Halt{},
    };
    if (std::holds_alternative<WaitSensitivity>(operation)) {
      rejected.static_sensitivity.push_back({0, EdgeKind::any});
    }
    const std::array<std::uint32_t, 1> one_signal{1};
    const auto symbol = "rejected_" + std::to_string(suffix++);
    expect_unsupported(
        [&] { jit.add_process(symbol, rejected, one_signal); }, diagnostic);
  }

  Process too_wide;
  too_wide.id = 0;
  too_wide.name = "wide";
  too_wide.register_count = 1;
  too_wide.operations = {ReadSignal{0, 0}, Halt{}};
  const std::array<std::uint32_t, 1> widths{65};
  expect_unsupported(
      [&] { jit.add_process("wide", too_wide, widths); },
      "widths in [1, 64]");

  Process zero_width;
  zero_width.id = 0;
  zero_width.name = "zero_width";
  zero_width.register_count = 1;
  zero_width.operations = {ReadSignal{0, 0}, Halt{}};
  const std::array<std::uint32_t, 1> invalid_widths{0};
  expect_fatal_error(
      [&] { jit.add_process("zero_width", zero_width, invalid_widths); },
      "signal width must be greater than zero");

  Process too_many_registers;
  too_many_registers.id = 0;
  too_many_registers.name = "too_many_registers";
  too_many_registers.register_count =
      static_cast<std::size_t>(
          std::numeric_limits<RegisterId>::max()) +
      1U;
  too_many_registers.operations = {Halt{}};
  const std::array<std::uint32_t, 0> no_signal_widths{};
  expect_unsupported(
      [&] {
        jit.add_process(
            "too_many_registers", too_many_registers, no_signal_widths);
      },
      "too many registers");

  Process unsupported_then_bad_signal;
  unsupported_then_bad_signal.id = 0;
  unsupported_then_bad_signal.name = "unsupported_then_bad_signal";
  unsupported_then_bad_signal.operations = {WaitOn{{0}}, WaitOn{{1}},
                                             Halt{}};
  const std::array<std::uint32_t, 1> one_signal{1};
  expect_fatal_error(
      [&] {
        jit.add_process("unsupported_then_bad_signal",
                        unsupported_then_bad_signal, one_signal);
      },
      "signal ID is outside signal_widths");

  Process unsupported_then_bad_target;
  unsupported_then_bad_target.id = 0;
  unsupported_then_bad_target.name = "unsupported_then_bad_target";
  unsupported_then_bad_target.operations = {WaitOn{{0}}, Jump{99}, Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process("unsupported_then_bad_target",
                        unsupported_then_bad_target, one_signal);
      },
      "jump target is outside");

  Process unsupported_then_undefined_use;
  unsupported_then_undefined_use.id = 0;
  unsupported_then_undefined_use.name = "unsupported_then_undefined_use";
  unsupported_then_undefined_use.register_count = 1;
  unsupported_then_undefined_use.operations = {
      WaitOn{{0}}, WriteBlocking{0, 0}, Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process("unsupported_then_undefined_use",
                        unsupported_then_undefined_use, one_signal);
      },
      "source register is never defined");

  Process unsupported_then_width_contradiction;
  unsupported_then_width_contradiction.id = 0;
  unsupported_then_width_contradiction.name =
      "unsupported_then_width_contradiction";
  unsupported_then_width_contradiction.register_count = 1;
  unsupported_then_width_contradiction.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WaitOn{{0}},
      WriteBlocking{0, 0},
      Halt{},
  };
  const std::array<std::uint32_t, 1> byte_signal_for_contradiction{8};
  expect_fatal_error(
      [&] {
        jit.add_process("unsupported_then_width_contradiction",
                        unsupported_then_width_contradiction,
                        byte_signal_for_contradiction);
      },
      "register width constraints are inconsistent");

  Process bad_jump;
  bad_jump.id = 0;
  bad_jump.name = "bad_jump";
  bad_jump.operations = {Jump{2}, Halt{}};
  const std::array<std::uint32_t, 0> no_signals{};
  expect_fatal_error(
      [&] { jit.add_process("bad_jump", bad_jump, no_signals); },
      "jump target is outside");

  Process bad_branch;
  bad_branch.id = 0;
  bad_branch.name = "bad_branch";
  bad_branch.register_count = 1;
  bad_branch.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      Branch{0, 2, 3, UnknownBranchPolicy::when_false},
      Halt{},
  };
  expect_fatal_error(
      [&] { jit.add_process("bad_branch", bad_branch, no_signals); },
      "branch false target is outside");

  Process path_use_before_definition;
  path_use_before_definition.id = 0;
  path_use_before_definition.name = "path_use_before_definition";
  path_use_before_definition.register_count = 2;
  path_use_before_definition.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      Branch{0, 2, 4, UnknownBranchPolicy::when_false},
      LoadConstant{1, PackedLogic4::from_msb_string("10100101")},
      Jump{5},
      Jump{5},
      WriteBlocking{0, 1},
      Halt{},
  };
  const std::array<std::uint32_t, 1> byte_signal{8};
  expect_fatal_error(
      [&] {
        jit.add_process("path_use_before_definition",
                        path_use_before_definition, byte_signal);
      },
      "register may be used before definition on a control-flow path");

  Process zero_time_cycle;
  zero_time_cycle.id = 0;
  zero_time_cycle.name = "zero_time_cycle";
  zero_time_cycle.operations = {Jump{0}, Halt{}};
  expect_unsupported(
      [&] {
        jit.add_process("zero_time_cycle", zero_time_cycle, no_signals);
      },
      "cycle has no WaitFor or Yield safe point");

  Process reachable_cycle;
  reachable_cycle.id = 0;
  reachable_cycle.name = "reachable_cycle";
  reachable_cycle.register_count = 1;
  reachable_cycle.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      Branch{0, 1, 2, UnknownBranchPolicy::when_false},
      Halt{},
  };
  expect_unsupported(
      [&] { jit.add_process("reachable_cycle", reachable_cycle, no_signals); },
      "cycle has no WaitFor or Yield safe point");

  expect_error(
      [&] {
        const std::array<std::uint32_t, 8> valid_widths{8, 8, 8, 8,
                                                        8, 8, 8, 1};
        jit.add_process("not-a-c-identifier", make_arithmetic_process(),
                        valid_widths);
      },
      "C identifier");
}

} // namespace

int main() {
  assert(!LlvmJit::llvm_version().empty());
  run_at_level(JitOptimizationLevel::o0, "arithmetic_o0");
  run_at_level(JitOptimizationLevel::o2, "arithmetic_o2");
  test_scalar_truth_tables_and_64_bits();
  test_initialized_bval_slot(JitOptimizationLevel::o0, "initialized_bval_o0");
  test_initialized_bval_slot(JitOptimizationLevel::o2, "initialized_bval_o2");
  test_control_flow_at_level(JitOptimizationLevel::o0, "control_flow_o0");
  test_control_flow_at_level(JitOptimizationLevel::o2, "control_flow_o2");
  test_resumable_at_level(JitOptimizationLevel::o0, "resume_o0");
  test_resumable_at_level(JitOptimizationLevel::o2, "resume_o2");
  test_persistent_object_cache();
  test_rejections();
  std::cout << "LLVM JIT tests passed with LLVM " << LlvmJit::llvm_version()
            << '\n';
}

// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/compiler/object_cache.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
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
using fsim::compiler::JitProcessModuleEntry;
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

enum class ScheduledWriteKind : std::uint8_t {
  update,
  after,
  slice_update,
  slice_after,
};

struct ScheduledWrite {
  ScheduledWriteKind kind = ScheduledWriteKind::update;
  std::uint32_t signal{};
  EncodedSignal value;
  std::uint64_t scheduled_at{};
  std::uint64_t delay{};
  std::uint64_t due{};
  std::uint32_t offset{};
  std::uint32_t width{};

  friend bool operator==(ScheduledWrite, ScheduledWrite) = default;
};

struct ObservedWrite {
  std::uint64_t time{};
  std::uint32_t signal{};
  EncodedSignal value;

  friend bool operator==(ObservedWrite, ObservedWrite) = default;
};

struct TestRuntime {
  std::array<EncodedSignal, 16> signals{};
  std::uint32_t assertion_count{};
  std::uint32_t failed_process{};
  std::uint32_t failed_instruction{};
  std::string assertion_message;
  bool leave_bval_untouched{};
  std::vector<std::pair<std::uint32_t, EncodedSignal>> writes;
  std::uint64_t current_time{};
  std::vector<ScheduledWrite> scheduled_writes;
  std::vector<std::string> output;
  std::vector<std::uint32_t> output_processes;
  std::vector<bool> output_newlines;
  std::vector<std::string> postponed_output;
  std::vector<std::uint32_t> report_instructions;
  std::vector<std::uint32_t> formatted_instructions;
  std::vector<EncodedSignal> formatted_values;
  std::vector<std::uint32_t> time_instructions;
  std::vector<std::uint32_t> monitor_install_instructions;
  std::vector<std::uint32_t> monitor_control_instructions;
  std::vector<std::uint32_t> random_instructions;
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

extern "C" void write_update(void *opaque, const std::uint32_t signal,
                              const std::uint64_t aval,
                              const std::uint64_t bval) {
  auto &runtime = *static_cast<TestRuntime *>(opaque);
  assert(signal < runtime.signals.size());
  runtime.scheduled_writes.push_back(
      {ScheduledWriteKind::update, signal, {aval, bval},
       runtime.current_time, 0, runtime.current_time});
}

extern "C" void write_after(void *opaque, const std::uint32_t signal,
                             const std::uint64_t aval,
                             const std::uint64_t bval,
                             const std::uint64_t delay) {
  auto &runtime = *static_cast<TestRuntime *>(opaque);
  assert(signal < runtime.signals.size());
  assert(delay <=
         std::numeric_limits<std::uint64_t>::max() - runtime.current_time);
  runtime.scheduled_writes.push_back(
      {ScheduledWriteKind::after, signal, {aval, bval},
      runtime.current_time, delay, runtime.current_time + delay});
}

[[nodiscard]] std::uint64_t low_mask(const std::uint32_t width) {
  assert(width > 0 && width <= 64);
  return width == 64
             ? std::numeric_limits<std::uint64_t>::max()
             : (UINT64_C(1) << width) - UINT64_C(1);
}

extern "C" void write_signal_slice(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.signals.size());
  assert(offset < 64 && width <= 64 - offset);
  const auto mask = low_mask(width) << offset;
  runtime.signals[signal].aval =
      (runtime.signals[signal].aval & ~mask)
      | ((aval << offset) & mask);
  runtime.signals[signal].bval =
      (runtime.signals[signal].bval & ~mask)
      | ((bval << offset) & mask);
  runtime.writes.emplace_back(
      signal, runtime.signals[signal]);
}

extern "C" void write_update_slice(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.signals.size());
  runtime.scheduled_writes.push_back(
      {ScheduledWriteKind::slice_update,
       signal,
       {aval, bval},
       runtime.current_time,
       0,
       runtime.current_time,
       offset,
       width});
}

extern "C" void write_after_slice(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.signals.size());
  assert(delay
         <= std::numeric_limits<std::uint64_t>::max()
                - runtime.current_time);
  runtime.scheduled_writes.push_back(
      {ScheduledWriteKind::slice_after,
       signal,
       {aval, bval},
       runtime.current_time,
       delay,
       runtime.current_time + delay,
       offset,
       width});
}

extern "C" std::uint32_t signal_event(
    void*, const std::uint32_t) {
  return 0;
}

extern "C" std::uint64_t signal_last_value(
    void* opaque,
    const std::uint32_t signal,
    std::uint64_t* bval) {
  return read_signal(opaque, signal, bval);
}

extern "C" std::uint64_t signal_last_event(
    void*, const std::uint32_t) {
  return 0;
}

extern "C" std::uint32_t signal_active(
    void*, const std::uint32_t) {
  return 0;
}

extern "C" void write_output(
    void* opaque,
    const std::uint32_t process,
    const char* text,
    const std::uint64_t text_size,
    const std::uint32_t newline) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(text != nullptr || text_size == 0);
  assert(
      text_size
      <= static_cast<std::uint64_t>(
          std::numeric_limits<std::size_t>::max()));
  assert(newline <= 1);
  runtime.output.emplace_back(
      text == nullptr ? "" : text,
      static_cast<std::size_t>(text_size));
  runtime.output_processes.push_back(process);
  runtime.output_newlines.push_back(newline != 0);
}

extern "C" void schedule_output(
    void* opaque,
    const std::uint32_t,
    const char* text,
    const std::uint64_t text_size,
    const std::uint32_t newline) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(text != nullptr || text_size == 0);
  assert(
      text_size
      <= static_cast<std::uint64_t>(
          std::numeric_limits<std::size_t>::max()));
  assert(newline <= 1);
  runtime.postponed_output.emplace_back(
      text == nullptr ? "" : text,
      static_cast<std::size_t>(text_size));
}

extern "C" void write_report(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.report_instructions.push_back(instruction);
}

extern "C" void write_formatted(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction,
    const std::uint32_t,
    const std::uint64_t aval,
    const std::uint64_t bval) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.formatted_instructions.push_back(instruction);
  runtime.formatted_values.push_back({aval, bval});
}

extern "C" void write_time(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.time_instructions.push_back(instruction);
}

extern "C" void install_monitor(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.monitor_install_instructions.push_back(instruction);
}

extern "C" void control_monitor(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.monitor_control_instructions.push_back(instruction);
}

extern "C" std::uint64_t random_value(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction,
    const std::uint64_t,
    const std::uint64_t,
    const std::uint64_t,
    const std::uint64_t,
    std::uint64_t* result_bval) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.random_instructions.push_back(instruction);
  *result_bval = 0;
  return UINT64_C(0x89abcdef);
}

[[nodiscard]] fsim_jit_runtime_v1 abi(TestRuntime &runtime) {
  return {
      FSIM_JIT_RUNTIME_ABI_VERSION_V1,
      static_cast<std::uint32_t>(sizeof(fsim_jit_runtime_v1)),
      &runtime,
      &read_signal,
      &write_signal,
      &assert_failed,
      &write_update,
      &write_after,
      0,
      0,
      &write_signal_slice,
      &write_update_slice,
      &write_after_slice,
      &signal_event,
      &signal_last_value,
      &signal_last_event,
      &signal_active,
      &write_output,
      &schedule_output,
      &write_report,
      &write_formatted,
      &write_time,
      &install_monitor,
      &control_monitor,
      &random_value,
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
      Assert{
          9,
          "unexpected sum",
          AssertionSeverity::failure,
          SourceLocation{}},
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
  scalar.register_count = 9;
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
      Binary{BinaryOperator::case_equal, 8, 0, 1},
      WriteBlocking{8, 8},
      Halt{},
  };
  const std::array<std::uint32_t, 9> scalar_widths{
      1, 1, 1, 1, 1, 1, 1, 1, 1};
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
      // The carry is discarded for a one-bit unsigned addition, while any
      // arithmetic X/Z operand makes the complete result unknown.
      const auto known =
          [](const Logic4 value) {
            return value == Logic4::zero || value == Logic4::one;
          };
      assert(
          runtime.signals[5]
          == encode(
              known(lhs) && known(rhs)
                  ? fsim::runtime::logic_xor(lhs, rhs)
                  : Logic4::x));
      assert(runtime.signals[6] == encode(fsim::runtime::logic_not(lhs)));
      assert(runtime.signals[7] == encode(equality(lhs, rhs)));
      assert(
          runtime.signals[8]
          == encode(
              lhs == rhs ? Logic4::one : Logic4::zero));
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

void test_wildcard_case_matching_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 5;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::casez_equal, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::casex_equal, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::wildcard_equal, 4, 0, 1},
      WriteBlocking{4, 4},
      Halt{},
  };
  const std::array<std::uint32_t, 5> widths{1, 1, 1, 1, 1};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  constexpr std::array states{
      Logic4::zero, Logic4::one, Logic4::x, Logic4::z};
  for (const auto lhs : states) {
    for (const auto rhs : states) {
      TestRuntime runtime;
      runtime.signals[0] = encode(lhs);
      runtime.signals[1] = encode(rhs);
      auto descriptor = abi(runtime);
      assert(
          jit.execute(handle, descriptor)
          == JitExecutionStatus::completed);
      const auto casez_match =
          lhs == Logic4::z || rhs == Logic4::z || lhs == rhs;
      const auto casex_match =
          lhs == Logic4::x || lhs == Logic4::z
          || rhs == Logic4::x || rhs == Logic4::z || lhs == rhs;
      const auto wildcard_match =
          rhs == Logic4::x || rhs == Logic4::z
              ? Logic4::one
              : lhs == Logic4::x || lhs == Logic4::z
                  ? Logic4::x
                  : lhs == rhs ? Logic4::one : Logic4::zero;
      assert(
          runtime.signals[2]
          == encode(casez_match ? Logic4::one : Logic4::zero));
      assert(
          runtime.signals[3]
          == encode(casex_match ? Logic4::one : Logic4::zero));
      assert(runtime.signals[4] == encode(wildcard_match));
    }
  }

  const auto vector_symbol = std::string{symbol} + "_vectors";
  Process vector;
  vector.id = 1;
  vector.name = vector_symbol;
  vector.register_count = 4;
  vector.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::equal, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::wildcard_equal, 3, 0, 1},
      WriteBlocking{3, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 4> vector_widths{2, 2, 1, 1};
  jit.add_process(vector_symbol, vector, vector_widths);
  const auto vector_handle = jit.lookup(vector_symbol);
  for (const auto& [lhs, rhs, wildcard_expected] :
       std::array{
           std::tuple{
               std::string_view{"X0"},
               std::string_view{"X1"},
               Logic4::zero},
           std::tuple{
               std::string_view{"X0"},
               std::string_view{"01"},
               Logic4::x}}) {
    TestRuntime runtime;
    runtime.signals[0] = encode(PackedLogic4::from_msb_string(lhs));
    runtime.signals[1] = encode(PackedLogic4::from_msb_string(rhs));
    auto descriptor = abi(runtime);
    assert(
        jit.execute(vector_handle, descriptor)
        == JitExecutionStatus::completed);
    assert(runtime.signals[2] == encode(Logic4::x));
    assert(runtime.signals[3] == encode(wildcard_expected));
  }
}

void test_conditional_select_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 4;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      ReadSignal{2, 2},
      ConditionalSelect{3, 0, 1, 2},
      WriteBlocking{3, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{1, 4, 4, 4};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  const auto when_true =
      PackedLogic4::from_msb_string("101Z").low_word();
  const auto when_false =
      PackedLogic4::from_msb_string("100Z").low_word();
  for (const auto& [condition, expected] :
       std::array{
           std::pair{Logic4::zero, std::string_view{"100Z"}},
           std::pair{Logic4::one, std::string_view{"101Z"}},
           std::pair{Logic4::x, std::string_view{"10XZ"}},
           std::pair{Logic4::z, std::string_view{"10XZ"}}}) {
    TestRuntime runtime;
    runtime.signals[0] = encode(condition);
    runtime.signals[1] = {when_true.aval, when_true.bval};
    runtime.signals[2] = {when_false.aval, when_false.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    const auto expected_word =
        PackedLogic4::from_msb_string(expected).low_word();
    assert((runtime.signals[3] == EncodedSignal{
        expected_word.aval, expected_word.bval}));
  }
}

void test_comparisons_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 8;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::not_equal, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::less_unsigned, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::less_equal_unsigned, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::greater_unsigned, 5, 0, 1},
      WriteBlocking{5, 5},
      Binary{BinaryOperator::greater_equal_unsigned, 6, 0, 1},
      WriteBlocking{6, 6},
      LogicalNot{7, 0},
      WriteBlocking{7, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 8> widths{
      4, 4, 1, 1, 1, 1, 1, 1};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    std::array<Logic4, 6> expected;
  };
  const std::array cases{
      TestCase{
          "0010",
          "0011",
          {Logic4::one, Logic4::one, Logic4::one,
           Logic4::zero, Logic4::zero, Logic4::zero}},
      TestCase{
          "0000",
          "0000",
          {Logic4::zero, Logic4::zero, Logic4::one,
           Logic4::zero, Logic4::one, Logic4::one}},
      TestCase{
          "00X0",
          "0011",
          {Logic4::x, Logic4::x, Logic4::x,
           Logic4::x, Logic4::x, Logic4::x}},
      TestCase{
          "01Z0",
          "0011",
          {Logic4::x, Logic4::x, Logic4::x,
           Logic4::x, Logic4::x, Logic4::zero}},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0; index < test.expected.size();
         ++index) {
      assert(
          runtime.signals[index + 2]
          == encode(test.expected[index]));
    }
  }
}

void test_logical_binary_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 4;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      LogicalBinary{
          LogicalBinaryOperator::logical_and, 2, 0, 1},
      WriteBlocking{2, 2},
      LogicalBinary{
          LogicalBinaryOperator::logical_or, 3, 0, 1},
      WriteBlocking{3, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{4, 2, 1, 1};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    Logic4 expected_and;
    Logic4 expected_or;
  };
  const std::array cases{
      TestCase{"0000", "X1", Logic4::zero, Logic4::one},
      TestCase{"00X0", "00", Logic4::zero, Logic4::x},
      TestCase{"00X0", "01", Logic4::x, Logic4::one},
      TestCase{"0010", "ZZ", Logic4::x, Logic4::one},
      TestCase{"0010", "01", Logic4::one, Logic4::one},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    assert(runtime.signals[2] == encode(test.expected_and));
    assert(runtime.signals[3] == encode(test.expected_or));
  }
}

void test_reduction_and_shift_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 11;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Reduction{ReductionOperator::bit_and, 2, 0},
      WriteBlocking{2, 2},
      Reduction{ReductionOperator::bit_or, 3, 0},
      WriteBlocking{3, 3},
      Reduction{ReductionOperator::bit_xor, 4, 0},
      WriteBlocking{4, 4},
      Shift{ShiftOperator::logical_left, 5, 0, 1},
      WriteBlocking{5, 5},
      Shift{ShiftOperator::logical_right, 6, 0, 1},
      WriteBlocking{6, 6},
      Shift{ShiftOperator::arithmetic_right, 7, 0, 1},
      WriteBlocking{7, 7},
      Shift{ShiftOperator::arithmetic_left, 8, 0, 1},
      WriteBlocking{8, 8},
      Shift{ShiftOperator::rotate_left, 9, 0, 1},
      WriteBlocking{9, 9},
      Shift{ShiftOperator::rotate_right, 10, 0, 1},
      WriteBlocking{10, 10},
      Halt{},
  };
  const std::array<std::uint32_t, 11> widths{
      4, 3, 1, 1, 1, 4, 4, 4, 4, 4, 4};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view value;
    std::string_view amount;
    std::string_view expected_and;
    std::string_view expected_or;
    std::string_view expected_xor;
    std::string_view expected_left;
    std::string_view expected_right;
    std::string_view expected_arithmetic_right;
    std::string_view expected_arithmetic_left;
    std::string_view expected_rotate_left;
    std::string_view expected_rotate_right;
  };
  const std::array cases{
      TestCase{
          "1111", "001", "1", "1", "0", "1110", "0111",
          "1111", "1111", "1111", "1111"},
      TestCase{
          "1011", "000", "0", "1", "1", "1011", "1011",
          "1011", "1011", "1011", "1011"},
      TestCase{
          "10X1", "001", "0", "1", "X", "0X10", "010X",
          "110X", "0X11", "0X11", "110X"},
      TestCase{
          "11X1", "011", "X", "1", "X", "1000", "0001",
          "1111", "1111", "111X", "1X11"},
      TestCase{
          "00X0", "0X1", "0", "X", "X", "XXXX", "XXXX",
          "XXXX", "XXXX", "XXXX", "XXXX"},
      TestCase{
          "Z001", "100", "0", "1", "X", "0000", "0000",
          "ZZZZ", "1111", "Z001", "Z001"},
      TestCase{
          "1001", "101", "0", "1", "0", "0000", "0000",
          "1111", "1111", "0011", "1100"},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto value =
        PackedLogic4::from_msb_string(test.value).low_word();
    const auto amount =
        PackedLogic4::from_msb_string(test.amount).low_word();
    runtime.signals[0] = {value.aval, value.bval};
    runtime.signals[1] = {amount.aval, amount.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    const std::array expected{
        test.expected_and,
        test.expected_or,
        test.expected_xor,
        test.expected_left,
        test.expected_right,
        test.expected_arithmetic_right,
        test.expected_arithmetic_left,
        test.expected_rotate_left,
        test.expected_rotate_right};
    for (std::size_t index = 0; index < expected.size();
         ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(expected[index]).low_word();
      assert((
          runtime.signals[index + 2]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }
}

void test_unsigned_arithmetic_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 8;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::add_unsigned, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::subtract_unsigned, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::multiply_unsigned, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::divide_unsigned, 5, 0, 1},
      WriteBlocking{5, 5},
      Binary{BinaryOperator::modulo_unsigned, 6, 0, 1},
      WriteBlocking{6, 6},
      Binary{BinaryOperator::power_unsigned, 7, 0, 1},
      WriteBlocking{7, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 8> widths{
      8, 8, 8, 8, 8, 8, 8, 8};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    std::array<std::string_view, 6> expected;
  };
  const std::array cases{
      TestCase{
          "11001000",
          "00000111",
          {"11001111", "11000001", "01111000",
           "00011100", "00000100", "00000000"}},
      TestCase{
          "00000000",
          "00000000",
          {"00000000", "00000000", "00000000",
           "XXXXXXXX", "XXXXXXXX", "00000001"}},
      TestCase{
          "10X01000",
          "00000111",
          {"XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "XXXXXXXX", "XXXXXXXX", "XXXXXXXX"}},
      TestCase{
          "11001000",
          "00000Z11",
          {"XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "XXXXXXXX", "XXXXXXXX", "XXXXXXXX"}},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0; index < test.expected.size();
         ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(
              test.expected[index]).low_word();
      assert((
          runtime.signals[index + 2]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }
}

void test_signed_arithmetic_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 13;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::add_signed, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::subtract_signed, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::multiply_signed, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::divide_signed, 5, 0, 1},
      WriteBlocking{5, 5},
      Binary{BinaryOperator::remainder_signed, 6, 0, 1},
      WriteBlocking{6, 6},
      Binary{BinaryOperator::modulo_signed, 7, 0, 1},
      WriteBlocking{7, 7},
      Binary{BinaryOperator::less_signed, 8, 0, 1},
      WriteBlocking{8, 8},
      Binary{BinaryOperator::less_equal_signed, 9, 0, 1},
      WriteBlocking{9, 9},
      Binary{BinaryOperator::greater_signed, 10, 0, 1},
      WriteBlocking{10, 10},
      Binary{
          BinaryOperator::greater_equal_signed, 11, 0, 1},
      WriteBlocking{11, 11},
      Binary{BinaryOperator::power_signed, 12, 0, 1},
      WriteBlocking{12, 12},
      Halt{},
  };
  const std::array<std::uint32_t, 13> widths{
      8, 8, 8, 8, 8, 8, 8, 8, 1, 1, 1, 1, 8};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    std::array<std::string_view, 11> expected;
  };
  const std::array cases{
      TestCase{
          "11111011",
          "00000011",
          {"11111110", "11111000", "11110001",
           "11111111", "11111110", "00000001",
           "1", "1", "0", "0", "10000011"}},
      TestCase{
          "00000101",
          "11111101",
          {"00000010", "00001000", "11110001",
           "11111111", "00000010", "11111111",
           "0", "0", "1", "1", "00000000"}},
      TestCase{
          "10000000",
          "11111111",
          {"01111111", "10000001", "10000000",
           "10000000", "00000000", "00000000",
           "1", "1", "0", "0", "00000000"}},
      TestCase{
          "10X01000",
          "00000111",
          {"XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "X", "X", "X", "X", "XXXXXXXX"}},
      TestCase{
          "00000101",
          "00000000",
          {"00000101", "00000101", "00000000",
           "XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "0", "0", "1", "1", "00000001"}},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0; index < test.expected.size();
         ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(
              test.expected[index]).low_word();
      assert((
          runtime.signals[index + 2]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }

  Process full_width;
  full_width.id = 1;
  full_width.name = std::string{symbol} + "_64";
  full_width.register_count = 6;
  full_width.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::divide_signed, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::remainder_signed, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::modulo_signed, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::less_signed, 5, 0, 1},
      WriteBlocking{5, 5},
      Halt{},
  };
  const std::array<std::uint32_t, 6> full_widths{
      64, 64, 64, 64, 64, 1};
  const auto full_symbol = std::string{symbol} + "_64";
  jit.add_process(full_symbol, full_width, full_widths);
  TestRuntime full_runtime;
  full_runtime.signals[0] = {
      UINT64_C(0x8000000000000000), 0};
  full_runtime.signals[1] = {
      std::numeric_limits<std::uint64_t>::max(), 0};
  auto full_descriptor = abi(full_runtime);
  assert(
      jit.execute(jit.lookup(full_symbol), full_descriptor)
      == JitExecutionStatus::completed);
  assert((
      full_runtime.signals[2]
      == EncodedSignal{UINT64_C(0x8000000000000000), 0}));
  assert((full_runtime.signals[3] == EncodedSignal{0, 0}));
  assert((full_runtime.signals[4] == EncodedSignal{0, 0}));
  assert((full_runtime.signals[5] == EncodedSignal{1, 0}));
}

void test_extract_and_concatenate_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 5;
  process.operations = {
      ReadSignal{0, 0},
      Extract{1, 0, 2, 1},
      WriteBlocking{1, 1},
      Extract{2, 0, 4, 4},
      WriteBlocking{2, 2},
      LoadConstant{
          3, PackedLogic4::from_msb_string("XZ")},
      Concatenate{4, {2, 1, 3}, 7},
      WriteBlocking{3, 4},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{8, 1, 4, 7};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view source;
    std::string_view expected_bit;
    std::string_view expected_part;
    std::string_view expected_joined;
  };
  const std::array cases{
      TestCase{"10XZ0110", "1", "10XZ", "10XZ1XZ"},
      TestCase{"Z10100X1", "0", "Z101", "Z1010XZ"},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto source =
        PackedLogic4::from_msb_string(test.source).low_word();
    runtime.signals[0] = {source.aval, source.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    const std::array expected{
        test.expected_bit,
        test.expected_part,
        test.expected_joined};
    for (std::size_t index = 0; index < expected.size();
         ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(expected[index]).low_word();
      assert((
          runtime.signals[index + 1]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }
}

void test_insert_and_partial_writes_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 4;
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string("11000011")},
      LoadConstant{1, PackedLogic4::from_msb_string("XZ")},
      Insert{2, 0, 1, 3},
      WriteBlocking{0, 2},
      LoadConstant{3, PackedLogic4::from_msb_string("10")},
      WriteBlockingSlice{1, 3, 1},
      WriteUpdateSlice{2, 1, 4},
      WriteAfterSlice{3, 3, 2, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{8, 8, 8, 8};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  runtime.signals[1] = {UINT64_C(0xff), 0};
  auto descriptor = abi(runtime);
  assert(
      jit.execute(handle, descriptor)
      == JitExecutionStatus::completed);
  const auto inserted =
      PackedLogic4::from_msb_string("110XZ011").low_word();
  assert((
      runtime.signals[0]
      == EncodedSignal{inserted.aval, inserted.bval}));
  assert((runtime.signals[1] == EncodedSignal{UINT64_C(0xfd), 0}));
  assert(runtime.scheduled_writes.size() == 2);
  assert((
      runtime.scheduled_writes[0]
      == ScheduledWrite{
          ScheduledWriteKind::slice_update,
          2,
          encode(PackedLogic4::from_msb_string("XZ")),
          0,
          0,
          0,
          4,
          2}));
  assert((
      runtime.scheduled_writes[1]
      == ScheduledWrite{
          ScheduledWriteKind::slice_after,
          3,
          encode(PackedLogic4::from_msb_string("10")),
          0,
          7,
          7,
          2,
          2}));

  {
    auto legacy = descriptor;
    legacy.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_signal_slice));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, legacy); },
        "does not include write_signal_slice");
  }
  {
    auto missing = descriptor;
    missing.write_update_slice = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_update_slice");
  }
  {
    auto missing = descriptor;
    missing.write_after_slice = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_after_slice");
  }
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

void test_debug_point_instrumentation() {
  const std::array<std::uint32_t, 0> no_signals{};
  const auto check_kind =
      [&](const DebugPointKind kind,
          const std::string_view kind_name) {
    Process process;
    process.id = 0;
    process.name = "debug_point";
    process.operations = {
        DebugPoint{
            kind,
            SourceLocation{"debug_point.sv", 7, 3}},
        Halt{},
    };
    const auto run =
        [&](const JitOptimizationLevel optimization,
            const std::string& symbol) {
        LlvmJit jit{LlvmJitOptions{optimization, {}}};
        jit.add_process(symbol, process, no_signals);
        const auto handle = jit.lookup(symbol);
        std::array<std::uint64_t, 0> aval{};
        std::array<std::uint64_t, 0> bval{};
        std::array<std::uint8_t, 0> initialized{};
        fsim_jit_frame_v1 frame{};
        jit.initialize_frame(
            handle, frame, aval, bval, initialized);
        fsim_jit_resume_result_v1 result{
            FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1,
            static_cast<std::uint32_t>(
                sizeof(fsim_jit_resume_result_v1)),
            0,
            FSIM_JIT_INVALID_INSTRUCTION,
            0,
        };
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        auto short_descriptor = descriptor;
        short_descriptor.struct_size =
            static_cast<std::uint32_t>(
                offsetof(fsim_jit_runtime_v1, flags));
        expect_error(
            [&] {
              (void)jit.resume(
                  handle, short_descriptor, frame, result);
            },
            "does not include debug-point flags");
        if (optimization == JitOptimizationLevel::o0) {
          assert(
              jit.resume(handle, descriptor, frame, result)
              == JitResumeStatus::debug_point);
          assert(result.instruction == 0);
          assert(frame.program_counter == 1);
          assert(frame.state == FSIM_JIT_FRAME_STATE_READY);
        }
        assert(
            jit.resume(handle, descriptor, frame, result)
            == JitResumeStatus::completed);
        assert(result.instruction == 1);
        if (optimization == JitOptimizationLevel::o2) {
          jit.initialize_frame(
              handle, frame, aval, bval, initialized);
          descriptor.flags = FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS;
          assert(
              jit.resume(handle, descriptor, frame, result)
              == JitResumeStatus::debug_point);
          assert(result.instruction == 0);
          assert(frame.program_counter == 1);
          assert(
              jit.resume(handle, descriptor, frame, result)
              == JitResumeStatus::completed);
        }
        };
    run(
        JitOptimizationLevel::o0,
        "debug_point_" + std::string{kind_name} + "_o0");
    run(
        JitOptimizationLevel::o2,
        "debug_point_" + std::string{kind_name} + "_o2");
  };
  check_kind(DebugPointKind::statement, "statement");
  check_kind(DebugPointKind::call, "call");
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

[[nodiscard]] Process
make_cached_scheduled_process(const bool delayed,
                              const std::uint64_t delay) {
  Process process;
  process.id = 13;
  process.name = "cached_scheduled_process";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
      delayed ? Operation{WriteAfter{0, 0, delay}}
              : Operation{WriteUpdate{0, 0}},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process
make_cached_wait_process(const bool static_wait,
                         std::vector<SignalId> signals,
                         std::vector<EdgeKind> edges = {}) {
  Process process;
  process.id = 14;
  process.name = "cached_wait_process";
  for (const auto signal : signals) {
    process.static_sensitivity.push_back({signal, EdgeKind::any});
  }
  if (static_wait) {
    process.operations = {WaitSensitivity{}, Halt{}};
  } else {
    process.operations = {
        WaitOn{std::move(signals), std::move(edges)}, Halt{}};
  }
  return process;
}

[[nodiscard]] Process make_cached_assertion_process(
    const AssertionSeverity severity,
    const std::uint32_t source_line) {
  Process process;
  process.id = 15;
  process.name = "cached_assertion_process";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      Assert{
          0,
          "cached assertion",
          severity,
          SourceLocation{"cache_assertion.sv", source_line, 3}},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process make_cached_debug_point_process(
    const DebugPointKind kind,
    const std::uint32_t source_line) {
  Process process;
  process.id = 16;
  process.name = "cached_debug_point_process";
  process.operations = {
      DebugPoint{
          kind,
          SourceLocation{"cache_debug_point.sv", source_line, 5}},
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
  assert(statistics.prune_failures == 0);
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

void run_cached_assertion_process(
    LlvmJit& jit, const std::string_view symbol) {
  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(jit.lookup(symbol), descriptor)
      == JitExecutionStatus::completed);
  assert(runtime.assertion_count == 0);
}

void run_cached_scheduled_process(
    LlvmJit &jit, const std::string_view symbol,
    const ScheduledWriteKind expected_kind,
    const std::uint64_t expected_delay) {
  const auto handle = jit.lookup(symbol);
  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  assert(runtime.scheduled_writes.size() == 1);
  assert(runtime.scheduled_writes.front().kind == expected_kind);
  assert(runtime.scheduled_writes.front().delay == expected_delay);
}

void materialize_cached_wait_process(
    LlvmJit &jit, const std::string_view symbol) {
  assert(jit.lookup(symbol));
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

void test_process_module_grouping_at_level(
    const JitOptimizationLevel optimization,
    const std::filesystem::path& cache_directory) {
  const auto make_process =
      [](const ProcessId id, const SignalId signal,
         const std::string_view value) {
        Process process;
        process.id = id;
        process.name =
            "grouped_process_" + std::to_string(id);
        process.register_count = 1;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string(value)},
            WriteBlocking{signal, 0},
            Halt{},
        };
        return process;
      };
  const std::array<std::uint32_t, 2> widths{8, 8};
  const auto first = make_process(20, 0, "10100101");
  const auto second = make_process(21, 1, "01011010");
  const auto add_group =
      [&](LlvmJit& jit, const Process& left,
          const Process& right) {
        const std::array entries{
            JitProcessModuleEntry{"grouped_first", &left},
            JitProcessModuleEntry{"grouped_second", &right},
        };
        jit.add_process_module(
            "work.grouped@top", entries, widths);
      };
  const auto execute_group =
      [](LlvmJit& jit) {
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        const auto first_handle = jit.lookup("grouped_first");
        const auto second_handle = jit.lookup("grouped_second");
        assert(first_handle != second_handle);
        assert(
            jit.execute(first_handle, descriptor)
            == JitExecutionStatus::completed);
        assert(
            jit.execute(second_handle, descriptor)
            == JitExecutionStatus::completed);
        assert((
            runtime.signals[0]
            == EncodedSignal{UINT64_C(0xa5), 0}));
        assert((
            runtime.signals[1]
            == EncodedSignal{UINT64_C(0x5a), 0}));
        return std::array{
            jit.frame_layout(first_handle),
            jit.frame_layout(second_handle),
        };
      };

  std::array<fsim::compiler::JitProcessFrameLayout, 2>
      original_layouts;
  {
    LlvmJit cold{
        LlvmJitOptions{optimization, cache_directory}};
    assert(cold.supports_process(first, widths));
    assert(cold.supports_process(second, widths));
    add_group(cold, first, second);
    original_layouts = execute_group(cold);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 1);

  {
    LlvmJit warm{
        LlvmJitOptions{optimization, cache_directory}};
    add_group(warm, first, second);
    const auto warm_layouts = execute_group(warm);
    assert(warm_layouts == original_layouts);
    expect_cache_statistics(warm, 1, 0, 0);
  }
  assert(cached_object_paths(cache_directory).size() == 1);

  // Any changed member invalidates the specialization object, while an
  // unchanged member retains its process-local frame identity.
  {
    LlvmJit changed{
        LlvmJitOptions{optimization, cache_directory}};
    const auto changed_second =
        make_process(21, 1, "00111100");
    add_group(changed, first, changed_second);
    const auto first_handle = changed.lookup("grouped_first");
    const auto second_handle = changed.lookup("grouped_second");
    assert(
        changed.frame_layout(first_handle)
        == original_layouts[0]);
    assert(
        changed.frame_layout(second_handle)
        != original_layouts[1]);
    expect_cache_statistics(changed, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 2);

  {
    LlvmJit rejected;
    const std::array<JitProcessModuleEntry, 0> empty{};
    expect_fatal_error(
        [&] {
          rejected.add_process_module(
              "empty", empty, widths);
        },
        "module cannot be empty");
    const std::array null_entry{
        JitProcessModuleEntry{"missing", nullptr}};
    expect_fatal_error(
        [&] {
          rejected.add_process_module(
              "null", null_entry, widths);
        },
        "has no SimIR process");
    const std::array duplicate_entries{
        JitProcessModuleEntry{"duplicate", &first},
        JitProcessModuleEntry{"duplicate", &second},
    };
    expect_fatal_error(
        [&] {
          rejected.add_process_module(
              "duplicates", duplicate_entries, widths);
        },
        "duplicate LLVM process symbol");

    Process supported;
    supported.id = 22;
    supported.name = "supported";
    supported.operations = {Halt{}};
    Process too_wide;
    too_wide.id = 23;
    too_wide.name = "too_wide";
    too_wide.register_count = 1;
    too_wide.operations = {ReadSignal{0, 0}, Halt{}};
    const std::array<std::uint32_t, 1> wide_widths{65};
    assert(rejected.supports_process(supported, wide_widths));
    assert(!rejected.supports_process(too_wide, wide_widths));
    const std::array unsupported_entries{
        JitProcessModuleEntry{"eligible", &supported},
        JitProcessModuleEntry{"unsupported", &too_wide},
    };
    expect_unsupported(
        [&] {
          rejected.add_process_module(
              "unsupported-member", unsupported_entries,
              wide_widths);
        },
        "widths in [1, 64]");
    expect_error(
        [&] { (void)rejected.lookup("eligible"); },
        "was not added");

    const std::array first_entry{
        JitProcessModuleEntry{"registered", &first}};
    rejected.add_process_module(
        "same-module", first_entry, widths);
    const std::array second_entry{
        JitProcessModuleEntry{"new_symbol", &second}};
    expect_fatal_error(
        [&] {
          rejected.add_process_module(
              "same-module", second_entry, widths);
        },
        "duplicate LLVM process module identity");
    expect_error(
        [&] { (void)rejected.lookup("new_symbol"); },
        "was not added");
  }
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

  constexpr std::string_view scheduled_symbol =
      "persistent_cache_scheduled_process";
  const std::array<std::uint32_t, 1> scheduled_widths{8};
  {
    LlvmJit cold{options};
    cold.add_process(
        scheduled_symbol, make_cached_scheduled_process(true, 3),
        scheduled_widths);
    run_cached_scheduled_process(
        cold, scheduled_symbol, ScheduledWriteKind::after, 3);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 6);
  {
    LlvmJit warm{options};
    warm.add_process(
        scheduled_symbol, make_cached_scheduled_process(true, 3),
        scheduled_widths);
    run_cached_scheduled_process(
        warm, scheduled_symbol, ScheduledWriteKind::after, 3);
    expect_cache_statistics(warm, 1, 0, 0);
  }

  // Delayed-write delay and operation kind both participate in identity.
  {
    LlvmJit changed_delay{options};
    changed_delay.add_process(
        scheduled_symbol, make_cached_scheduled_process(true, 4),
        scheduled_widths);
    run_cached_scheduled_process(
        changed_delay, scheduled_symbol, ScheduledWriteKind::after, 4);
    expect_cache_statistics(changed_delay, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 7);
  {
    LlvmJit changed_kind{options};
    changed_kind.add_process(
        scheduled_symbol, make_cached_scheduled_process(false, 0),
        scheduled_widths);
    run_cached_scheduled_process(
        changed_kind, scheduled_symbol, ScheduledWriteKind::update, 0);
    expect_cache_statistics(changed_kind, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 8);

  constexpr std::string_view wait_symbol =
      "persistent_cache_wait_process";
  const std::array<std::uint32_t, 2> wait_widths{1, 1};
  {
    LlvmJit cold{options};
    cold.add_process(
        wait_symbol, make_cached_wait_process(false, {0, 1}),
        wait_widths);
    materialize_cached_wait_process(cold, wait_symbol);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 9);
  {
    LlvmJit warm{options};
    warm.add_process(
        wait_symbol, make_cached_wait_process(false, {0, 1}),
        wait_widths);
    materialize_cached_wait_process(warm, wait_symbol);
    expect_cache_statistics(warm, 1, 0, 0);
  }

  // Wait operands, referenced widths, operation kind, and static edge rules
  // participate in native cache identity.
  {
    LlvmJit changed_operands{options};
    changed_operands.add_process(
        wait_symbol, make_cached_wait_process(false, {1, 0}),
        wait_widths);
    materialize_cached_wait_process(changed_operands, wait_symbol);
    expect_cache_statistics(changed_operands, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 10);
  {
    LlvmJit changed_kind{options};
    changed_kind.add_process(
        wait_symbol, make_cached_wait_process(true, {0, 1}),
        wait_widths);
    materialize_cached_wait_process(changed_kind, wait_symbol);
    expect_cache_statistics(changed_kind, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 11);
  {
    LlvmJit changed_width{options};
    const std::array<std::uint32_t, 2> wider_wait_signal{2, 1};
    changed_width.add_process(
        wait_symbol, make_cached_wait_process(false, {0, 1}),
        wider_wait_signal);
    materialize_cached_wait_process(changed_width, wait_symbol);
    expect_cache_statistics(changed_width, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 12);
  {
    LlvmJit changed_dynamic_edge{options};
    changed_dynamic_edge.add_process(
        wait_symbol,
        make_cached_wait_process(
            false, {0, 1},
            {EdgeKind::posedge, EdgeKind::any}),
        wait_widths);
    materialize_cached_wait_process(
        changed_dynamic_edge, wait_symbol);
    expect_cache_statistics(changed_dynamic_edge, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 13);
  {
    LlvmJit changed_edge{options};
    auto edge_process =
        make_cached_wait_process(true, {0, 1});
    edge_process.static_sensitivity.front().edge =
        EdgeKind::posedge;
    changed_edge.add_process(
        wait_symbol, edge_process, wait_widths);
    materialize_cached_wait_process(changed_edge, wait_symbol);
    expect_cache_statistics(changed_edge, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 14);

  constexpr std::string_view assertion_symbol =
      "persistent_cache_assertion_process";
  const std::array<std::uint32_t, 0> no_signals{};
  {
    LlvmJit cold{options};
    cold.add_process(
        assertion_symbol,
        make_cached_assertion_process(AssertionSeverity::error, 7),
        no_signals);
    run_cached_assertion_process(cold, assertion_symbol);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 15);
  {
    LlvmJit warm{options};
    warm.add_process(
        assertion_symbol,
        make_cached_assertion_process(AssertionSeverity::error, 7),
        no_signals);
    run_cached_assertion_process(warm, assertion_symbol);
    expect_cache_statistics(warm, 1, 0, 0);
  }

  // Assertion diagnostic metadata is immutable generated behavior and must
  // therefore participate in native object identity.
  {
    LlvmJit changed_metadata{options};
    changed_metadata.add_process(
        assertion_symbol,
        make_cached_assertion_process(AssertionSeverity::failure, 8),
        no_signals);
    run_cached_assertion_process(changed_metadata, assertion_symbol);
    expect_cache_statistics(changed_metadata, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 16);

  constexpr std::string_view debug_symbol =
      "persistent_cache_debug_point_process";
  {
    LlvmJit cold{options};
    cold.add_process(
        debug_symbol,
        make_cached_debug_point_process(DebugPointKind::statement, 11),
        no_signals);
    assert(cold.lookup(debug_symbol));
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 17);
  {
    LlvmJit warm{options};
    warm.add_process(
        debug_symbol,
        make_cached_debug_point_process(DebugPointKind::statement, 11),
        no_signals);
    assert(warm.lookup(debug_symbol));
    expect_cache_statistics(warm, 1, 0, 0);
  }

  // Debug-point kind and source location are generated behavior because the
  // returned instruction indexes immutable SimIR metadata.
  {
    LlvmJit changed_metadata{options};
    changed_metadata.add_process(
        debug_symbol,
        make_cached_debug_point_process(DebugPointKind::wait, 12),
        no_signals);
    assert(changed_metadata.lookup(debug_symbol));
    expect_cache_statistics(changed_metadata, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 18);

  // A WaitOn timeout changes generated boundary metadata and cache identity.
  {
    LlvmJit changed_timeout{options};
    auto timed_wait =
        make_cached_wait_process(false, {0, 1});
    std::get<WaitOn>(timed_wait.operations.front()).timeout = 5;
    changed_timeout.add_process(
        wait_symbol, timed_wait, wait_widths);
    materialize_cached_wait_process(
        changed_timeout, wait_symbol);
    expect_cache_statistics(changed_timeout, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 19);

  constexpr std::string_view wildcard_symbol =
      "persistent_cache_wildcard_case";
  const std::array<std::uint32_t, 1> wildcard_widths{1};
  const auto make_wildcard_process =
      [](const BinaryOperator operation) {
        Process process;
        process.id = 0;
        process.name = "cached_wildcard_case";
        process.register_count = 3;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("10X1")},
            LoadConstant{
                1, PackedLogic4::from_msb_string("1011")},
            Binary{operation, 2, 0, 1},
            WriteBlocking{0, 2},
            Halt{},
        };
        return process;
      };
  const auto run_wildcard_process =
      [&](LlvmJit& jit, const Logic4 expected) {
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        assert(
            jit.execute(jit.lookup(wildcard_symbol), descriptor)
            == JitExecutionStatus::completed);
        assert(runtime.signals[0] == encode(expected));
      };
  {
    LlvmJit cold{options};
    cold.add_process(
        wildcard_symbol,
        make_wildcard_process(BinaryOperator::casez_equal),
        wildcard_widths);
    run_wildcard_process(cold, Logic4::zero);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 20);
  {
    LlvmJit warm{options};
    warm.add_process(
        wildcard_symbol,
        make_wildcard_process(BinaryOperator::casez_equal),
        wildcard_widths);
    run_wildcard_process(warm, Logic4::zero);
    expect_cache_statistics(warm, 1, 0, 0);
  }
  // The wildcard matching policy is generated behavior and participates in
  // native object identity.
  {
    LlvmJit changed_operator{options};
    changed_operator.add_process(
        wildcard_symbol,
        make_wildcard_process(BinaryOperator::casex_equal),
        wildcard_widths);
    run_wildcard_process(changed_operator, Logic4::one);
    expect_cache_statistics(changed_operator, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 21);
  {
    LlvmJit one_sided_wildcard{options};
    one_sided_wildcard.add_process(
        wildcard_symbol,
        make_wildcard_process(BinaryOperator::wildcard_equal),
        wildcard_widths);
    run_wildcard_process(one_sided_wildcard, Logic4::x);
    expect_cache_statistics(one_sided_wildcard, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 22);

  constexpr std::string_view power_symbol =
      "persistent_cache_power_operator";
  const std::array<std::uint32_t, 1> power_widths{8};
  const auto make_power_process =
      [](const BinaryOperator operation) {
        Process process;
        process.id = 0;
        process.name = "cached_power_operator";
        process.register_count = 3;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("00000011")},
            LoadConstant{
                1, PackedLogic4::from_msb_string("00000100")},
            Binary{operation, 2, 0, 1},
            WriteBlocking{0, 2},
            Halt{},
        };
        return process;
      };
  const auto run_power_process =
      [&](LlvmJit& jit, const std::string_view expected) {
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        assert(
            jit.execute(jit.lookup(power_symbol), descriptor)
            == JitExecutionStatus::completed);
        const auto encoded =
            PackedLogic4::from_msb_string(expected).low_word();
        assert((
            runtime.signals[0]
            == EncodedSignal{encoded.aval, encoded.bval}));
      };
  {
    LlvmJit multiply{options};
    multiply.add_process(
        power_symbol,
        make_power_process(BinaryOperator::multiply_unsigned),
        power_widths);
    run_power_process(multiply, "00001100");
    expect_cache_statistics(multiply, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 23);
  {
    LlvmJit power{options};
    power.add_process(
        power_symbol,
        make_power_process(BinaryOperator::power_unsigned),
        power_widths);
    run_power_process(power, "01010001");
    expect_cache_statistics(power, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 24);
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

void test_cache_pruning_integration(
    const std::filesystem::path& cache_directory) {
  fsim::compiler::ObjectCache storage{
      cache_directory / "llvm" / "objects"};
  fsim::compiler::CacheKeyBuilder builder;
  const auto key = builder.add("seed", "prune-me").finish();
  const std::array payload{
      std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};
  std::error_code error;
  assert(storage.store(key, payload, error));
  const auto encoded_size =
      std::filesystem::file_size(storage.path_for(key), error);
  assert(!error);

  LlvmJitOptions options{JitOptimizationLevel::o2, cache_directory};
  options.cache_maximum_bytes.reset();
  options.cache_maximum_entries = 0;
  options.cache_maximum_age.reset();
  LlvmJit pruned{options};
  const auto statistics = pruned.cache_statistics();
  assert(statistics.pruned_entries == 1);
  assert(statistics.pruned_bytes == encoded_size);
  assert(statistics.prune_failures == 0);
  assert(!std::filesystem::exists(storage.path_for(key)));

  // Cache maintenance is best-effort: an unusable cache root is reflected in
  // telemetry but never prevents construction of a valid JIT.
  const auto broken_directory = cache_directory.parent_path() / "broken";
  std::filesystem::create_directories(broken_directory / "llvm", error);
  assert(!error);
  {
    std::ofstream file{
        broken_directory / "llvm" / "objects", std::ios::binary};
    file << "not a directory";
  }
  LlvmJit broken{
      LlvmJitOptions{JitOptimizationLevel::o2, broken_directory}};
  assert(broken.cache_statistics().prune_failures == 1);
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
  test_process_module_grouping_at_level(
      JitOptimizationLevel::o0, root / "group-o0");
  test_process_module_grouping_at_level(
      JitOptimizationLevel::o2, root / "group-o2");
  test_cache_pruning_integration(root / "pruning");

  std::filesystem::remove_all(root, error);
  assert(!error);
}

void test_rejections() {
  LlvmJit jit;
  const std::array<std::uint32_t, 1> one_signal{1};
  const std::array<std::uint32_t, 0> no_signals{};

  Process empty_wait_on;
  empty_wait_on.id = 0;
  empty_wait_on.name = "empty_wait_on";
  empty_wait_on.operations = {WaitOn{}, Halt{}};
  expect_fatal_error(
      [&] { jit.add_process("empty_wait_on", empty_wait_on, one_signal); },
      "WaitOn requires at least one signal");

  Process timeout_metadata_without_timeout;
  timeout_metadata_without_timeout.id = 0;
  timeout_metadata_without_timeout.name =
      "timeout_metadata_without_timeout";
  timeout_metadata_without_timeout.register_count = 1;
  WaitOn incomplete_timeout{{0}};
  incomplete_timeout.timeout_result = 0;
  timeout_metadata_without_timeout.operations = {
      std::move(incomplete_timeout), Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "timeout_metadata_without_timeout",
            timeout_metadata_without_timeout,
            one_signal);
      },
      "WaitOn timeout metadata requires a timeout");

  Process mismatched_timeout_rearm;
  mismatched_timeout_rearm.id = 0;
  mismatched_timeout_rearm.name =
      "mismatched_timeout_rearm";
  mismatched_timeout_rearm.register_count = 1;
  WaitOn timeout_origin{{0}};
  timeout_origin.timeout = 2;
  timeout_origin.timeout_result = 0;
  WaitOn timeout_rearm{{0}};
  timeout_rearm.timeout = 3;
  timeout_rearm.timeout_result = 0;
  timeout_rearm.timeout_origin = 0;
  mismatched_timeout_rearm.operations = {
      std::move(timeout_origin),
      std::move(timeout_rearm),
      Halt{},
  };
  expect_fatal_error(
      [&] {
        jit.add_process(
            "mismatched_timeout_rearm",
            mismatched_timeout_rearm,
            one_signal);
      },
      "WaitOn timeout rearm does not match its origin");

  Process mismatched_wait_edges;
  mismatched_wait_edges.id = 0;
  mismatched_wait_edges.name = "mismatched_wait_edges";
  mismatched_wait_edges.operations = {
      WaitOn{{0}, {EdgeKind::posedge, EdgeKind::negedge}}, Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "mismatched_wait_edges", mismatched_wait_edges,
            one_signal);
      },
      "WaitOn edge count must match its signal count");

  Process empty_static_wait;
  empty_static_wait.id = 0;
  empty_static_wait.name = "empty_static_wait";
  empty_static_wait.operations = {WaitSensitivity{}, Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "empty_static_wait", empty_static_wait, one_signal);
      },
      "WaitSensitivity requires a static sensitivity list");

  Process invalid_static_signal;
  invalid_static_signal.id = 0;
  invalid_static_signal.name = "invalid_static_signal";
  invalid_static_signal.static_sensitivity = {{1, EdgeKind::any}};
  invalid_static_signal.operations = {Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "invalid_static_signal", invalid_static_signal, one_signal);
      },
      "signal ID is outside signal_widths");

  Process vector_edge;
  vector_edge.id = 0;
  vector_edge.name = "vector_edge";
  vector_edge.static_sensitivity = {{0, EdgeKind::posedge}};
  vector_edge.operations = {WaitSensitivity{}, Halt{}};
  const std::array<std::uint32_t, 1> vector_signal{8};
  expect_fatal_error(
      [&] { jit.add_process("vector_edge", vector_edge, vector_signal); },
      "edge sensitivity requires a scalar signal");

  Process invalid_edge;
  invalid_edge.id = 0;
  invalid_edge.name = "invalid_edge";
  invalid_edge.static_sensitivity = {
      {0, static_cast<EdgeKind>(UINT8_MAX)}};
  invalid_edge.operations = {WaitSensitivity{}, Halt{}};
  expect_fatal_error(
      [&] { jit.add_process("invalid_edge", invalid_edge, one_signal); },
      "static sensitivity has an invalid edge kind");

  Process vector_dynamic_edge;
  vector_dynamic_edge.id = 0;
  vector_dynamic_edge.name = "vector_dynamic_edge";
  vector_dynamic_edge.operations = {
      WaitOn{{0}, {EdgeKind::posedge}}, Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "vector_dynamic_edge", vector_dynamic_edge,
            vector_signal);
      },
      "WaitOn edge requires a scalar signal");

  Process invalid_dynamic_edge;
  invalid_dynamic_edge.id = 0;
  invalid_dynamic_edge.name = "invalid_dynamic_edge";
  invalid_dynamic_edge.operations = {
      WaitOn{{0}, {static_cast<EdgeKind>(UINT8_MAX)}}, Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "invalid_dynamic_edge", invalid_dynamic_edge,
            one_signal);
      },
      "WaitOn has an invalid edge kind");

  Process zero_width_wait;
  zero_width_wait.id = 0;
  zero_width_wait.name = "zero_width_wait";
  zero_width_wait.operations = {WaitOn{{0}}, Halt{}};
  const std::array<std::uint32_t, 1> zero_width_signal{0};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "zero_width_wait", zero_width_wait, zero_width_signal);
      },
      "signal width must be greater than zero");

  const std::array<std::uint32_t, 1> scalar_signal{1};
  std::uint32_t scheduled_suffix = 0;
  for (const auto &operation :
       std::array<Operation, 2>{WriteUpdate{0, 1},
                                WriteAfter{0, 1, UINT64_MAX}}) {
    Process bad_source;
    bad_source.id = 0;
    bad_source.name = "scheduled_bad_source";
    bad_source.register_count = 1;
    bad_source.operations = {operation, Halt{}};
    expect_fatal_error(
        [&] {
          jit.add_process(
              "scheduled_bad_source_" +
                  std::to_string(scheduled_suffix++),
              bad_source, scalar_signal);
        },
        "source register ID is out of range");
  }
  for (const auto &operation :
       std::array<Operation, 2>{WriteUpdate{1, 0},
                                WriteAfter{1, 0, UINT64_MAX}}) {
    Process bad_signal;
    bad_signal.id = 0;
    bad_signal.name = "scheduled_bad_signal";
    bad_signal.register_count = 1;
    bad_signal.operations = {
        LoadConstant{0, PackedLogic4::from_msb_string("0")},
        operation,
        Halt{},
    };
    expect_fatal_error(
        [&] {
          jit.add_process(
              "scheduled_bad_signal_" +
                  std::to_string(scheduled_suffix++),
              bad_signal, scalar_signal);
        },
        "signal ID is outside signal_widths");
  }
  for (const auto &operation :
       std::array<Operation, 2>{WriteUpdate{0, 0},
                                WriteAfter{0, 0, UINT64_MAX}}) {
    Process bad_width;
    bad_width.id = 0;
    bad_width.name = "scheduled_bad_width";
    bad_width.register_count = 1;
    bad_width.operations = {
        LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
        operation,
        Halt{},
    };
    expect_fatal_error(
        [&] {
          jit.add_process(
              "scheduled_bad_width_" +
                  std::to_string(scheduled_suffix++),
              bad_width, scalar_signal);
        },
        "register width constraints are inconsistent");
  }

  Process invalid_extract;
  invalid_extract.id = 0;
  invalid_extract.name = "invalid_extract";
  invalid_extract.register_count = 2;
  invalid_extract.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string("1010")},
      Extract{1, 0, 3, 2},
      Halt{},
  };
  expect_fatal_error(
      [&] {
        jit.add_process(
            "invalid_extract", invalid_extract, no_signals);
      },
      "Extract range is outside its source register");

  Process invalid_insert;
  invalid_insert.id = 0;
  invalid_insert.name = "invalid_insert";
  invalid_insert.register_count = 3;
  invalid_insert.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string("1010")},
      LoadConstant{
          1, PackedLogic4::from_msb_string("11")},
      Insert{2, 0, 1, 3},
      Halt{},
  };
  expect_fatal_error(
      [&] {
        jit.add_process(
            "invalid_insert", invalid_insert, no_signals);
      },
      "Insert range is outside its target register");

  Process invalid_partial_write;
  invalid_partial_write.id = 0;
  invalid_partial_write.name = "invalid_partial_write";
  invalid_partial_write.register_count = 1;
  invalid_partial_write.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string("11")},
      WriteUpdateSlice{0, 0, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 1> nibble_signal{4};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "invalid_partial_write",
            invalid_partial_write,
            nibble_signal);
      },
      "partial write range is outside its target signal");

  Process invalid_concatenate;
  invalid_concatenate.id = 0;
  invalid_concatenate.name = "invalid_concatenate";
  invalid_concatenate.register_count = 3;
  invalid_concatenate.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      LoadConstant{
          1, PackedLogic4::from_msb_string("10")},
      Concatenate{2, {0, 1}, 4},
      Halt{},
  };
  expect_fatal_error(
      [&] {
        jit.add_process(
            "invalid_concatenate", invalid_concatenate, no_signals);
      },
      "Concatenate operand widths do not match its result width");

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
      "cycle has no suspension safe point");

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
      "cycle has no suspension safe point");

  Process debug_safe_cycle;
  debug_safe_cycle.id = 0;
  debug_safe_cycle.name = "debug_safe_cycle";
  debug_safe_cycle.register_count = 1;
  debug_safe_cycle.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      DebugPoint{
          DebugPointKind::statement,
          SourceLocation{"runtime_loop.sv", 4, 5}},
      Branch{0, 3, 5, UnknownBranchPolicy::when_false},
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      Jump{1},
      Halt{},
  };
  jit.add_process(
      "debug_safe_cycle", debug_safe_cycle, no_signals);
  TestRuntime debug_safe_runtime;
  auto debug_safe_descriptor = abi(debug_safe_runtime);
  assert(
      jit.execute(
          jit.lookup("debug_safe_cycle"),
          debug_safe_descriptor)
      == JitExecutionStatus::completed);

  expect_error(
      [&] {
        const std::array<std::uint32_t, 8> valid_widths{8, 8, 8, 8,
                                                        8, 8, 8, 1};
        jit.add_process("not-a-c-identifier", make_arithmetic_process(),
                        valid_widths);
      },
      "C identifier");
}

void test_display_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJitOptions options;
  options.optimization = level;
  LlvmJit jit(options);
  Process process;
  process.id = 13;
  process.name = std::string{symbol};
  process.register_count = 3;
  process.operations = {
      Display{"hello", true},
      Display{"tail", false},
      Display{"postponed", true, true},
      Report{
          "warning",
          AssertionSeverity::warning,
          SourceLocation{"report.vhd", 7, 5}},
      LoadConstant{0, PackedLogic4::from_msb_string("10xz")},
      FormatDisplay{
          0,
          OutputFormat::binary,
          "v=",
          "!",
          true,
          false},
      TimeDisplay{"time=", "", true, false, 4, false, true},
      MonitorInstall{
          {
              MonitorValue{
                  MonitorValueKind::signal,
                  0,
                  OutputFormat::hexadecimal,
                  "m="},
          },
          "",
          true},
      MonitorControl{false},
      LoadConstant{2, PackedLogic4::from_msb_string("0")},
      Assert{
          2,
          "nonfatal assertion",
          AssertionSeverity::error,
          SourceLocation{"assertion.sv", 9, 3}},
      RandomValue{
          1,
          RandomKind::urandom,
          std::nullopt,
          std::nullopt},
      Halt{},
  };
  const std::array<std::uint32_t, 1> signal_widths{8};
  jit.add_process(symbol, process, signal_widths);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(jit.lookup(symbol), descriptor)
      == JitExecutionStatus::completed);
  assert(
      runtime.output == std::vector<std::string>({"hello", "tail"}));
  assert(
      runtime.output_processes
      == std::vector<std::uint32_t>({13, 13}));
  assert(
      runtime.output_newlines == std::vector<bool>({true, false}));
  assert(
      runtime.postponed_output
      == std::vector<std::string>({"postponed"}));
  assert(
      runtime.report_instructions
      == std::vector<std::uint32_t>({3, 10}));
  assert(
      runtime.formatted_instructions
      == std::vector<std::uint32_t>({5}));
  assert(runtime.formatted_values.size() == 1);
  assert(
      runtime.time_instructions
      == std::vector<std::uint32_t>({6}));
  assert(
      runtime.monitor_install_instructions
      == std::vector<std::uint32_t>({7}));
  assert(
      runtime.monitor_control_instructions
      == std::vector<std::uint32_t>({8}));
  assert(
      runtime.random_instructions
      == std::vector<std::uint32_t>({11}));

  TestRuntime short_runtime;
  auto short_descriptor = abi(short_runtime);
  short_descriptor.struct_size = static_cast<std::uint32_t>(
      offsetof(fsim_jit_runtime_v1, write_output));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_descriptor);
      },
      "write_output");

  TestRuntime short_postponed_runtime;
  auto short_postponed_descriptor = abi(short_postponed_runtime);
  short_postponed_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, schedule_output));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_postponed_descriptor);
      },
      "schedule_output");

  TestRuntime short_report_runtime;
  auto short_report_descriptor = abi(short_report_runtime);
  short_report_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, write_report));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_report_descriptor);
      },
      "write_report");

  TestRuntime short_formatted_runtime;
  auto short_formatted_descriptor = abi(short_formatted_runtime);
  short_formatted_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, write_formatted));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_formatted_descriptor);
      },
      "write_formatted");

  TestRuntime short_time_runtime;
  auto short_time_descriptor = abi(short_time_runtime);
  short_time_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, write_time));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_time_descriptor);
      },
      "write_time");

  TestRuntime short_monitor_install_runtime;
  auto short_monitor_install_descriptor =
      abi(short_monitor_install_runtime);
  short_monitor_install_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, install_monitor));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_monitor_install_descriptor);
      },
      "install_monitor");

  TestRuntime short_monitor_control_runtime;
  auto short_monitor_control_descriptor =
      abi(short_monitor_control_runtime);
  short_monitor_control_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, control_monitor));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_monitor_control_descriptor);
      },
      "control_monitor");

  TestRuntime short_random_runtime;
  auto short_random_descriptor = abi(short_random_runtime);
  short_random_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, random_value));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_random_descriptor);
      },
      "random_value");
}

} // namespace

int main() {
  assert(!LlvmJit::llvm_version().empty());
  run_at_level(JitOptimizationLevel::o0, "arithmetic_o0");
  run_at_level(JitOptimizationLevel::o2, "arithmetic_o2");
  test_scalar_truth_tables_and_64_bits();
  test_conditional_select_at_level(
      JitOptimizationLevel::o0, "conditional_select_o0");
  test_conditional_select_at_level(
      JitOptimizationLevel::o2, "conditional_select_o2");
  test_wildcard_case_matching_at_level(
      JitOptimizationLevel::o0, "wildcard_case_o0");
  test_wildcard_case_matching_at_level(
      JitOptimizationLevel::o2, "wildcard_case_o2");
  test_comparisons_at_level(
      JitOptimizationLevel::o0, "comparisons_o0");
  test_comparisons_at_level(
      JitOptimizationLevel::o2, "comparisons_o2");
  test_logical_binary_at_level(
      JitOptimizationLevel::o0, "logical_binary_o0");
  test_logical_binary_at_level(
      JitOptimizationLevel::o2, "logical_binary_o2");
  test_reduction_and_shift_at_level(
      JitOptimizationLevel::o0, "reduction_shift_o0");
  test_reduction_and_shift_at_level(
      JitOptimizationLevel::o2, "reduction_shift_o2");
  test_unsigned_arithmetic_at_level(
      JitOptimizationLevel::o0, "unsigned_arithmetic_o0");
  test_unsigned_arithmetic_at_level(
      JitOptimizationLevel::o2, "unsigned_arithmetic_o2");
  test_signed_arithmetic_at_level(
      JitOptimizationLevel::o0, "signed_arithmetic_o0");
  test_signed_arithmetic_at_level(
      JitOptimizationLevel::o2, "signed_arithmetic_o2");
  test_extract_and_concatenate_at_level(
      JitOptimizationLevel::o0, "extract_concatenate_o0");
  test_extract_and_concatenate_at_level(
      JitOptimizationLevel::o2, "extract_concatenate_o2");
  test_insert_and_partial_writes_at_level(
      JitOptimizationLevel::o0, "insert_partial_writes_o0");
  test_insert_and_partial_writes_at_level(
      JitOptimizationLevel::o2, "insert_partial_writes_o2");
  test_initialized_bval_slot(JitOptimizationLevel::o0, "initialized_bval_o0");
  test_initialized_bval_slot(JitOptimizationLevel::o2, "initialized_bval_o2");
  test_debug_point_instrumentation();
  test_control_flow_at_level(JitOptimizationLevel::o0, "control_flow_o0");
  test_control_flow_at_level(JitOptimizationLevel::o2, "control_flow_o2");
  test_scheduled_callbacks_at_level(
      JitOptimizationLevel::o0, "scheduled_o0");
  test_scheduled_callbacks_at_level(
      JitOptimizationLevel::o2, "scheduled_o2");
  test_scheduling_differential_at_level(
      JitOptimizationLevel::o0, "scheduled_diff_o0");
  test_scheduling_differential_at_level(
      JitOptimizationLevel::o2, "scheduled_diff_o2");
  test_resumable_at_level(JitOptimizationLevel::o0, "resume_o0");
  test_resumable_at_level(JitOptimizationLevel::o2, "resume_o2");
  test_signal_waits_at_level(
      JitOptimizationLevel::o0, "signal_wait_o0");
  test_signal_waits_at_level(
      JitOptimizationLevel::o2, "signal_wait_o2");
  test_display_at_level(
      JitOptimizationLevel::o0, "display_o0");
  test_display_at_level(
      JitOptimizationLevel::o2, "display_o2");
  test_persistent_object_cache();
  test_rejections();
  std::cout << "LLVM JIT tests passed with LLVM " << LlvmJit::llvm_version()
            << '\n';
}

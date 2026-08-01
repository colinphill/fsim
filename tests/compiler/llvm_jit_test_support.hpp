// SPDX-License-Identifier: Apache-2.0
#pragma once
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

namespace fsim::tests::compiler {

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
using fsim::runtime::Logic9;
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

struct InertialWrite {
  std::uint32_t signal{};
  EncodedSignal value;
  std::uint32_t offset{};
  std::uint32_t width{};
  std::uint64_t rise{};
  std::uint64_t fall{};
  std::uint64_t turnoff{};

  friend bool operator==(InertialWrite, InertialWrite) = default;
};

struct ProjectedWrite {
  std::uint32_t signal{};
  EncodedSignal value;
  std::uint32_t offset{};
  std::uint32_t width{};
  std::uint64_t delay{};
  std::uint64_t rejection{};
  std::uint32_t mode{};

  friend bool operator==(ProjectedWrite, ProjectedWrite) = default;
};

struct TestRuntime {
  std::array<EncodedSignal, 16> signals{};
  std::array<std::array<std::uint64_t, 4>, 16> logic9_signals{};
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
  std::vector<std::array<std::uint64_t, 4>> formatted_logic9_values;
  std::vector<std::uint32_t> time_instructions;
  std::vector<std::uint32_t> monitor_install_instructions;
  std::vector<std::uint32_t> monitor_control_instructions;
  std::vector<std::uint32_t> random_instructions;
  std::vector<InertialWrite> inertial_writes;
  std::vector<ProjectedWrite> projected_writes;
  std::array<std::string, 32> strings;
  std::array<std::string, 8> string_objects;
};

[[nodiscard]] inline std::uint64_t low_mask(std::uint32_t width);

[[nodiscard]] inline std::array<std::uint64_t, 4> logic9_value(
    const fsim_jit_logic9_word_v1* value) {
  assert(value != nullptr);
  return {
      value->planes[0],
      value->planes[1],
      value->planes[2],
      value->planes[3]};
}

inline void store_logic9_value(
    fsim_jit_logic9_word_v1* destination,
    const std::array<std::uint64_t, 4>& value) {
  assert(destination != nullptr);
  std::copy(value.begin(), value.end(), destination->planes);
}

extern "C" inline void read_signal_logic9(
    void* opaque,
    const std::uint32_t signal,
    fsim_jit_logic9_word_v1* value) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.logic9_signals.size());
  store_logic9_value(value, runtime.logic9_signals[signal]);
}

extern "C" inline void write_signal_logic9(
    void* opaque,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.logic9_signals.size());
  runtime.logic9_signals[signal] = logic9_value(value);
}

extern "C" inline void write_update_logic9(
    void* opaque,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value) {
  write_signal_logic9(opaque, signal, value);
}

extern "C" inline void write_after_logic9(
    void* opaque,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t) {
  write_signal_logic9(opaque, signal, value);
}

inline void write_signal_slice_logic9_impl(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.logic9_signals.size());
  assert(offset < 64 && width <= 64 - offset);
  const auto mask = low_mask(width) << offset;
  const auto source = logic9_value(value);
  for (std::size_t plane = 0; plane < source.size(); ++plane) {
    runtime.logic9_signals[signal][plane] =
        (runtime.logic9_signals[signal][plane] & ~mask)
        | ((source[plane] << offset) & mask);
  }
}

extern "C" inline void write_signal_slice_logic9(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value) {
  write_signal_slice_logic9_impl(
      opaque, signal, offset, width, value);
}

extern "C" inline void write_update_slice_logic9(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value) {
  write_signal_slice_logic9_impl(
      opaque, signal, offset, width, value);
}

extern "C" inline void write_after_slice_logic9(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t) {
  write_signal_slice_logic9_impl(
      opaque, signal, offset, width, value);
}

extern "C" inline void signal_last_value_logic9(
    void* opaque,
    const std::uint32_t signal,
    fsim_jit_logic9_word_v1* value) {
  read_signal_logic9(opaque, signal, value);
}

extern "C" inline void write_inertial_logic9(
    void* opaque,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t,
    const std::uint64_t,
    const std::uint64_t) {
  write_signal_logic9(opaque, signal, value);
}

extern "C" inline void write_inertial_slice_logic9(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t,
    const std::uint64_t,
    const std::uint64_t) {
  write_signal_slice_logic9_impl(
      opaque, signal, offset, width, value);
}

extern "C" inline void write_projected_logic9(
    void* opaque,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t,
    const std::uint64_t,
    const std::uint32_t) {
  write_signal_logic9(opaque, signal, value);
}

extern "C" inline void write_projected_slice_logic9(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t,
    const std::uint64_t,
    const std::uint32_t) {
  write_signal_slice_logic9_impl(
      opaque, signal, offset, width, value);
}

extern "C" inline void write_projected_waveform_logic9(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t,
    const fsim_jit_logic9_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t,
    const std::uint32_t) {
  assert(elements != nullptr && count > 0);
  write_signal_logic9(opaque, signal, &elements[count - 1].value);
}

extern "C" inline void write_projected_waveform_slice_logic9(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t,
    const std::uint32_t) {
  assert(elements != nullptr && count > 0);
  write_signal_slice_logic9_impl(
      opaque, signal, offset, width, &elements[count - 1].value);
}

extern "C" inline void write_formatted_logic9(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t,
    const std::uint32_t,
    const fsim_jit_logic9_word_v1* value) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.formatted_logic9_values.push_back(logic9_value(value));
}

extern "C" inline std::uint64_t read_signal(void *opaque,
                                      const std::uint32_t signal,
                                      std::uint64_t *bval) {
  auto &runtime = *static_cast<TestRuntime *>(opaque);
  assert(signal < runtime.signals.size());
  if (!runtime.leave_bval_untouched) {
    *bval = runtime.signals[signal].bval;
  }
  return runtime.signals[signal].aval;
}

extern "C" inline void write_signal(void *opaque, const std::uint32_t signal,
                              const std::uint64_t aval,
                              const std::uint64_t bval) {
  auto &runtime = *static_cast<TestRuntime *>(opaque);
  assert(signal < runtime.signals.size());
  runtime.signals[signal] = {aval, bval};
  runtime.writes.emplace_back(signal, runtime.signals[signal]);
}

extern "C" inline void assert_failed(void *opaque, const std::uint32_t process,
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

extern "C" inline void write_update(void *opaque, const std::uint32_t signal,
                              const std::uint64_t aval,
                              const std::uint64_t bval) {
  auto &runtime = *static_cast<TestRuntime *>(opaque);
  assert(signal < runtime.signals.size());
  runtime.scheduled_writes.push_back(
      {ScheduledWriteKind::update, signal, {aval, bval},
       runtime.current_time, 0, runtime.current_time});
}

extern "C" inline void write_after(void *opaque, const std::uint32_t signal,
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

[[nodiscard]] inline std::uint64_t low_mask(const std::uint32_t width) {
  assert(width > 0 && width <= 64);
  return width == 64
             ? std::numeric_limits<std::uint64_t>::max()
             : (UINT64_C(1) << width) - UINT64_C(1);
}

extern "C" inline void write_signal_slice(
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

extern "C" inline void write_update_slice(
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

extern "C" inline void write_after_slice(
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

extern "C" inline std::uint32_t signal_event(
    void*, const std::uint32_t) {
  return 0;
}

extern "C" inline std::uint64_t signal_last_value(
    void* opaque,
    const std::uint32_t signal,
    std::uint64_t* bval) {
  return read_signal(opaque, signal, bval);
}

extern "C" inline std::uint64_t signal_last_event(
    void*, const std::uint32_t) {
  return 0;
}

extern "C" inline std::uint32_t signal_active(
    void*, const std::uint32_t) {
  return 0;
}

extern "C" inline void write_output(
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

extern "C" inline void schedule_output(
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

extern "C" inline void write_report(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.report_instructions.push_back(instruction);
}

extern "C" inline void write_formatted(
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

extern "C" inline void write_time(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.time_instructions.push_back(instruction);
}

extern "C" inline void install_monitor(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.monitor_install_instructions.push_back(instruction);
}

extern "C" inline void control_monitor(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t instruction) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.monitor_control_instructions.push_back(instruction);
}

extern "C" inline std::uint64_t random_value(
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

extern "C" inline void write_inertial(
    void* opaque,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t rise,
    const std::uint64_t fall,
    const std::uint64_t turnoff) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.signals.size());
  runtime.inertial_writes.push_back(
      {signal, {aval, bval}, 0, 0, rise, fall, turnoff});
}

extern "C" inline void write_inertial_slice(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t rise,
    const std::uint64_t fall,
    const std::uint64_t turnoff) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.signals.size());
  runtime.inertial_writes.push_back(
      {signal, {aval, bval}, offset, width, rise, fall, turnoff});
}

extern "C" inline void write_projected(
    void* opaque,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.signals.size());
  runtime.projected_writes.push_back(
      {signal, {aval, bval}, 0, 0, delay, rejection, mode});
}

extern "C" inline void write_projected_slice(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.signals.size());
  runtime.projected_writes.push_back(
      {signal, {aval, bval}, offset, width, delay, rejection, mode});
}

extern "C" inline void write_projected_waveform(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t width,
    const fsim_jit_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.signals.size());
  assert(elements != nullptr && count >= 2);
  for (std::uint32_t index = 0; index < count; ++index) {
    runtime.projected_writes.push_back(
        {signal,
         {elements[index].aval, elements[index].bval},
         0,
         width,
         elements[index].delay,
         rejection,
         mode});
  }
}

extern "C" inline void write_projected_waveform_slice(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(signal < runtime.signals.size());
  assert(elements != nullptr && count >= 2);
  for (std::uint32_t index = 0; index < count; ++index) {
    runtime.projected_writes.push_back(
        {signal,
         {elements[index].aval, elements[index].bval},
         offset,
         width,
         elements[index].delay,
         rejection,
         mode});
  }
}

extern "C" inline std::uint32_t load_string(
    void* opaque,
    const std::uint32_t destination,
    const char* bytes,
    const std::uint64_t size) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  assert(destination < runtime.strings.size());
  assert(size <= maximum_string_bytes);
  runtime.strings[destination].assign(
      bytes, static_cast<std::size_t>(size));
  return 0;
}

extern "C" inline std::uint32_t copy_string(
    void* opaque,
    const std::uint32_t destination,
    const std::uint32_t source) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.strings.at(destination) = runtime.strings.at(source);
  return 0;
}

extern "C" inline std::uint32_t read_string_object(
    void* opaque,
    const std::uint32_t destination,
    const std::uint32_t object) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.strings.at(destination) = runtime.string_objects.at(object);
  return 0;
}

extern "C" inline std::uint32_t write_string_object(
    void* opaque,
    const std::uint32_t object,
    const std::uint32_t source) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  runtime.string_objects.at(object) = runtime.strings.at(source);
  return 0;
}

extern "C" inline std::uint32_t concatenate_strings(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t,
    const std::uint32_t destination,
    const std::uint32_t* operands,
    const std::uint32_t count) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  std::string result;
  for (std::uint32_t index = 0; index < count; ++index) {
    result += runtime.strings.at(operands[index]);
  }
  if (result.size() > maximum_string_bytes) {
    return 1;
  }
  runtime.strings.at(destination) = std::move(result);
  return 0;
}

extern "C" inline std::uint32_t compare_strings(
    void* opaque,
    const std::uint32_t lhs,
    const std::uint32_t rhs,
    const std::uint32_t not_equal,
    std::uint32_t* result) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  *result = (runtime.strings.at(lhs) == runtime.strings.at(rhs))
          != (not_equal != 0);
  return 0;
}

extern "C" inline std::uint32_t string_length(
    void* opaque,
    const std::uint32_t source,
    std::uint32_t* result) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  *result = static_cast<std::uint32_t>(
      runtime.strings.at(source).size());
  return 0;
}

extern "C" inline std::uint32_t string_index(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t,
    const std::uint32_t source,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    const std::uint32_t signed_index,
    std::uint32_t* result) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  const auto raw = static_cast<std::uint32_t>(index_aval);
  const auto index = signed_index != 0
      ? static_cast<std::int64_t>(static_cast<std::int32_t>(raw))
      : static_cast<std::int64_t>(raw);
  if (index_bval != 0 || index < 0
      || static_cast<std::uint64_t>(index)
          >= runtime.strings.at(source).size()) {
    return 1;
  }
  *result = static_cast<unsigned char>(
      runtime.strings.at(source).at(index));
  return 0;
}

extern "C" inline std::uint32_t string_replace_byte(
    void* opaque,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t target,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    const std::uint32_t signed_index,
    const std::uint64_t source_aval,
    const std::uint64_t source_bval) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  std::uint32_t unused = 0;
  if (source_bval != 0
      || string_index(
             opaque,
             process,
             instruction,
             target,
             index_aval,
             index_bval,
             signed_index,
             &unused)
          != 0) {
    return 1;
  }
  const auto index = signed_index != 0
      ? static_cast<std::size_t>(
            static_cast<std::int32_t>(index_aval))
      : static_cast<std::size_t>(
            static_cast<std::uint32_t>(index_aval));
  runtime.strings.at(target).at(index) =
      static_cast<char>(source_aval & UINT64_C(0xff));
  return 0;
}

extern "C" inline std::uint32_t write_string_output(
    void* opaque,
    const std::uint32_t process,
    const std::uint32_t source,
    const char* prefix,
    const std::uint64_t prefix_size,
    const char* suffix,
    const std::uint64_t suffix_size,
    const std::uint32_t newline,
    const std::uint32_t postponed) {
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  std::string text{prefix, static_cast<std::size_t>(prefix_size)};
  text += runtime.strings.at(source);
  text.append(suffix, static_cast<std::size_t>(suffix_size));
  (postponed != 0 ? runtime.postponed_output : runtime.output)
      .push_back(std::move(text));
  runtime.output_processes.push_back(process);
  runtime.output_newlines.push_back(newline != 0);
  return 0;
}

[[nodiscard]] inline fsim_jit_runtime_v1 abi(TestRuntime &runtime) {
  fsim_jit_runtime_v1 result{};
  result.abi_version = FSIM_JIT_RUNTIME_ABI_VERSION_V1;
  result.struct_size =
      static_cast<std::uint32_t>(sizeof(fsim_jit_runtime_v1));
  result.context = &runtime;
  result.read_signal = &read_signal;
  result.write_signal = &write_signal;
  result.assert_failed = &assert_failed;
  result.write_update = &write_update;
  result.write_after = &write_after;
  result.write_signal_slice = &write_signal_slice;
  result.write_update_slice = &write_update_slice;
  result.write_after_slice = &write_after_slice;
  result.signal_event = &signal_event;
  result.signal_last_value = &signal_last_value;
  result.signal_last_event = &signal_last_event;
  result.signal_active = &signal_active;
  result.write_output = &write_output;
  result.schedule_output = &schedule_output;
  result.write_report = &write_report;
  result.write_formatted = &write_formatted;
  result.write_time = &write_time;
  result.install_monitor = &install_monitor;
  result.control_monitor = &control_monitor;
  result.random_value = &random_value;
  result.write_inertial = &write_inertial;
  result.write_inertial_slice = &write_inertial_slice;
  result.write_projected = &write_projected;
  result.write_projected_slice = &write_projected_slice;
  result.write_projected_waveform = &write_projected_waveform;
  result.write_projected_waveform_slice =
      &write_projected_waveform_slice;
  result.read_signal_logic9 = &read_signal_logic9;
  result.write_signal_logic9 = &write_signal_logic9;
  result.write_update_logic9 = &write_update_logic9;
  result.write_after_logic9 = &write_after_logic9;
  result.write_signal_slice_logic9 = &write_signal_slice_logic9;
  result.write_update_slice_logic9 = &write_update_slice_logic9;
  result.write_after_slice_logic9 = &write_after_slice_logic9;
  result.signal_last_value_logic9 = &signal_last_value_logic9;
  result.write_inertial_logic9 = &write_inertial_logic9;
  result.write_inertial_slice_logic9 =
      &write_inertial_slice_logic9;
  result.write_projected_logic9 = &write_projected_logic9;
  result.write_projected_slice_logic9 =
      &write_projected_slice_logic9;
  result.write_projected_waveform_logic9 =
      &write_projected_waveform_logic9;
  result.write_projected_waveform_slice_logic9 =
      &write_projected_waveform_slice_logic9;
  result.write_formatted_logic9 = &write_formatted_logic9;
  result.load_string = &load_string;
  result.copy_string = &copy_string;
  result.read_string_object = &read_string_object;
  result.write_string_object = &write_string_object;
  result.concatenate_strings = &concatenate_strings;
  result.compare_strings = &compare_strings;
  result.string_length = &string_length;
  result.string_index = &string_index;
  result.string_replace_byte = &string_replace_byte;
  result.write_string_output = &write_string_output;
  return result;
}

[[nodiscard]] inline fsim_jit_resume_result_v1 new_resume_result() {
  return {
      FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1,
      static_cast<std::uint32_t>(sizeof(fsim_jit_resume_result_v1)),
      0,
      FSIM_JIT_INVALID_INSTRUCTION,
      0,
  };
}

[[nodiscard]] inline Process make_arithmetic_process() {
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
inline void expect_error(Function &&function, const std::string_view fragment) {
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
inline void expect_unsupported(Function &&function,
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
inline void expect_fatal_error(Function &&function,
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
inline void expect_generated_runtime_error(
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

[[nodiscard]] inline EncodedSignal encode(const Logic4 value) {
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

[[nodiscard]] inline EncodedSignal encode(
    const PackedLogic4& value) {
  assert(value.width() > 0);
  assert(value.width() <= 64);
  return {
      value.aval_words().front(),
      value.bval_words().front()};
}

[[nodiscard]] inline Logic4 equality(
    const Logic4 lhs,
    const Logic4 rhs) {
  const auto known = [](const Logic4 value) {
    return value == Logic4::zero || value == Logic4::one;
  };
  if (!known(lhs) || !known(rhs)) {
    return Logic4::x;
  }
  return lhs == rhs ? Logic4::one : Logic4::zero;
}

[[nodiscard]] inline std::array<std::uint64_t, 4> planes(
    const PackedLogic4& value) {
  return value.logic9_low_word().planes;
}

} // namespace

void run_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_scheduled_callbacks_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_inertial_callbacks_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_projected_callbacks_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_scheduling_differential_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_control_flow_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_checked_integer_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_resumable_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_signal_waits_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_scalar_truth_tables_and_64_bits();
void test_wildcard_case_matching_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_conditional_select_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_comparisons_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_logical_binary_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_reduction_and_shift_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_signed_shift_counts_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_unsigned_arithmetic_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_signed_arithmetic_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_extract_and_concatenate_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_insert_and_partial_writes_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_dynamic_packed_indices_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_initialized_bval_slot(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_debug_point_instrumentation();
void test_logic9_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_persistent_object_cache();
void test_process_control_cache_identity();
void test_static_slice_consumer_cache_identity(
    const std::filesystem::path& cache_directory);
void test_static_slice_call_cache_identity(
    const std::filesystem::path& cache_directory);
void test_static_slice_ordering_cache_identity(
    const std::filesystem::path& cache_directory);
void test_static_slice_port_cache_identity(
    const std::filesystem::path& cache_directory);
void test_static_indexed_slice_cache_identity(
    const std::filesystem::path& cache_directory);
void test_fixed_array_function_return_cache_identity(
    const std::filesystem::path& cache_directory);
void test_nonstatic_function_return_cache_identity(
    const std::filesystem::path& cache_directory);
void test_expression_selection_cache_identity(
    const std::filesystem::path& cache_directory);
void test_procedural_update_cache_identity(
    const std::filesystem::path& cache_directory);
void test_container_construction_cache_identity(
    const std::filesystem::path& cache_directory);
void test_rejections();
void test_display_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_strings_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);

} // namespace fsim::tests::compiler

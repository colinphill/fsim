// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"

#include "fsim/compiler/object_cache.hpp"

#include <llvm/Config/llvm-config.h>
#include <llvm/Support/ErrorHandling.h>

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

namespace fsim::compiler::llvm_detail {


using runtime::Logic9;
using runtime::simir::Assert;
using runtime::simir::Binary;
using runtime::simir::BinaryOperator;
using runtime::simir::Branch;
using runtime::simir::Call;
using runtime::simir::Concatenate;
using runtime::simir::ConcatenateStrings;
using runtime::simir::CompareStrings;
using runtime::simir::ConditionalSelect;
using runtime::simir::CountOnes;
using runtime::simir::CountBits;
using runtime::simir::CopyRegister;
using runtime::simir::CopyStringRegister;
using runtime::simir::DebugPoint;
using runtime::simir::Display;
using runtime::simir::DynamicExtract;
using runtime::simir::DynamicPartIndex;
using runtime::simir::DynamicPartInsert;
using runtime::simir::DynamicPartSelect;
using runtime::simir::DynamicIndex;
using runtime::simir::DynamicInsert;
using runtime::simir::EdgeKind;
using runtime::simir::Extract;
using runtime::simir::ExpressionSizingKind;
using runtime::simir::ExpressionValueDomain;
using runtime::simir::FileClose;
using runtime::simir::FileBinaryRead;
using runtime::simir::FileEndOfFile;
using runtime::simir::FileErrorStatus;
using runtime::simir::FileFlush;
using runtime::simir::FileOpen;
using runtime::simir::FilePosition;
using runtime::simir::FileReadLine;
using runtime::simir::FileScan;
using runtime::simir::FileWriteFormatted;
using runtime::simir::FileWriteLiteral;
using runtime::simir::FileWriteString;
using runtime::simir::ForceSignalSlice;
using runtime::simir::FormatDisplay;
using runtime::simir::Halt;
using runtime::simir::InstructionIndex;
using runtime::simir::Insert;
using runtime::simir::IntegerBinary;
using runtime::simir::IntegerBinaryOperator;
using runtime::simir::IntegerCheck;
using runtime::simir::IntegerUnary;
using runtime::simir::IntegerUnaryOperator;
using runtime::simir::Jump;
using runtime::simir::LoadConstant;
using runtime::simir::LoadStringConstant;
using runtime::simir::LogicalBinary;
using runtime::simir::LogicalBinaryOperator;
using runtime::simir::LogicalNot;
using runtime::simir::MonitorControl;
using runtime::simir::MonitorInstall;
using runtime::simir::MonitorValueKind;
using runtime::simir::Operation;
using runtime::simir::Pause;
using runtime::simir::Process;
using runtime::simir::ReadSignal;
using runtime::simir::ReadSimulationTime;
using runtime::simir::VitalTimingCheck;
using runtime::simir::VitalDelay;
using runtime::simir::ReadStringObject;
using runtime::simir::Reduction;
using runtime::simir::ReductionOperator;
using runtime::simir::ReleaseSignalSlice;
using runtime::simir::RegisterId;
using runtime::simir::RandomValue;
using runtime::simir::Report;
using runtime::simir::Return;
using runtime::simir::Shift;
using runtime::simir::ShiftOperator;
using runtime::simir::SignalActive;
using runtime::simir::SignalEvent;
using runtime::simir::SignalLastEvent;
using runtime::simir::SignalLastActive;
using runtime::simir::SignalDriving;
using runtime::simir::SignalDrivingValue;
using runtime::simir::SignalLastValue;
using runtime::simir::Stop;
using runtime::simir::StringDisplay;
using runtime::simir::StringReport;
using runtime::simir::StringIndex;
using runtime::simir::StringLength;
using runtime::simir::StringMethod;
using runtime::simir::StringReplaceByte;
using runtime::simir::TimeDisplay;
using runtime::simir::UnaryNot;
using runtime::simir::UnknownBranchPolicy;
using runtime::simir::ValueKind;
using runtime::simir::WaitFor;
using runtime::simir::WaitOn;
using runtime::simir::WaitSensitivity;
using runtime::simir::WaitForever;
using runtime::simir::WriteAfter;
using runtime::simir::WriteAfterDynamicSlice;
using runtime::simir::WriteAfterDynamicPartSlice;
using runtime::simir::WriteAfterSlice;
using runtime::simir::WriteBlocking;
using runtime::simir::WriteBlockingDynamicSlice;
using runtime::simir::WriteBlockingDynamicPartSlice;
using runtime::simir::WriteBlockingSlice;
using runtime::simir::WriteInertial;
using runtime::simir::WriteInertialDynamicSlice;
using runtime::simir::WriteInertialSlice;
using runtime::simir::WriteProjected;
using runtime::simir::WriteProjectedDynamicSlice;
using runtime::simir::WriteProjectedWaveform;
using runtime::simir::WriteProjectedWaveformDynamicSlice;
using runtime::simir::WriteProjectedSlice;
using runtime::simir::WriteProjectedWaveformSlice;
using runtime::simir::WriteUpdate;
using runtime::simir::WriteUpdateDynamicSlice;
using runtime::simir::WriteUpdateDynamicPartSlice;
using runtime::simir::WriteUpdateSlice;
using runtime::simir::WriteStringObject;
using runtime::simir::Yield;
using runtime::simir::StrengthRank;
using runtime::simir::Fork;
using runtime::simir::ForkEnd;
using runtime::simir::ForkJoinKind;
using runtime::simir::WaitFork;
using runtime::simir::DisableFork;
using runtime::simir::ClassAllocate;
using runtime::simir::ClassMethodCall;
using runtime::simir::ClassPropertyRead;
using runtime::simir::ClassPropertyWrite;
using runtime::simir::ClassStaticMethodCall;
using runtime::simir::ClassStaticPropertyRead;
using runtime::simir::ClassStaticPropertyWrite;


constexpr std::string_view kNativeObjectCacheSchema =
    "fsim-llvm-native-object-v81";

void add_key_u64(CacheKeyBuilder &builder, const std::string_view label,
                 const std::uint64_t value) {
  std::array<std::byte, sizeof(value)> encoded{};
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    encoded[index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
  builder.add_bytes(label, encoded);
}

void add_dynamic_index_key(
    CacheKeyBuilder& builder,
    const DynamicIndex& selection) {
  add_key_u64(builder, "index", selection.index);
  add_key_u64(
      builder,
      "index-left",
      static_cast<std::uint64_t>(selection.left));
  add_key_u64(
      builder,
      "index-right",
      static_cast<std::uint64_t>(selection.right));
  add_key_u64(
      builder, "index-base-offset", selection.base_offset);
}

void add_dynamic_part_index_key(
    CacheKeyBuilder& builder,
    const DynamicPartIndex& selection) {
  add_key_u64(builder, "part-base", selection.base);
  add_key_u64(
      builder,
      "part-left",
      static_cast<std::uint64_t>(selection.left));
  add_key_u64(
      builder,
      "part-right",
      static_cast<std::uint64_t>(selection.right));
  add_key_u64(builder, "part-base-offset", selection.base_offset);
  add_key_u64(builder, "part-width", selection.width);
  add_key_u64(
      builder, "part-increasing", selection.increasing ? 1U : 0U);
  add_key_u64(
      builder,
      "part-source-descending",
      selection.source_descending ? 1U : 0U);
}

[[nodiscard]] std::string make_native_object_cache_key(
    const std::string_view symbol, const Process &process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const JitOptimizationLevel optimization,
    const llvm::Triple &target_triple, const llvm::DataLayout &data_layout,
    const std::string_view target_cpu,
    const std::span<const std::string> target_features) {
  CacheKeyBuilder builder;
  builder.add("llvm-object-schema", kNativeObjectCacheSchema);
  builder.add("llvm-version", LLVM_VERSION_STRING);
  builder.add("optimization",
              optimization == JitOptimizationLevel::o0 ? "o0" : "o2");
  add_key_u64(builder, "runtime-abi-version",
              FSIM_JIT_RUNTIME_ABI_VERSION_V1);
  add_key_u64(builder, "runtime-abi-structure-size",
              sizeof(fsim_jit_runtime_v1));
  add_key_u64(builder, "frame-abi-version",
              FSIM_JIT_FRAME_ABI_VERSION_V1);
  add_key_u64(builder, "frame-abi-structure-size",
              sizeof(fsim_jit_frame_v1));
  add_key_u64(builder, "resume-result-abi-version",
              FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1);
  add_key_u64(builder, "resume-result-abi-structure-size",
              sizeof(fsim_jit_resume_result_v1));
  builder.add("target-triple", target_triple.str());
  builder.add("data-layout", data_layout.getStringRepresentation());
  builder.add("target-cpu", target_cpu);
  add_key_u64(builder, "target-feature-count", target_features.size());
  for (const auto &feature : target_features) {
    builder.add("target-feature", feature);
  }

  builder.add("symbol", symbol);
  add_key_u64(builder, "process-id", process.id);
  builder.add("process-name", process.name);
  add_key_u64(builder, "register-count", process.register_count);
  add_key_u64(
      builder, "string-register-count", process.string_register_count);
  add_key_u64(
      builder, "container-register-count",
      process.container_register_count);
  for (const auto& type : process.container_register_types) {
    add_key_u64(builder, "container-element-width", type.element_width);
    builder.add("container-element-nominal-type", type.element_nominal_type);
    add_key_u64(builder, "container-two-state", type.two_state ? 1U : 0U);
    add_key_u64(
        builder, "container-signed", type.signed_elements ? 1U : 0U);
    add_key_u64(builder, "container-queue", type.queue ? 1U : 0U);
    add_key_u64(
        builder, "container-associative",
        type.associative ? 1U : 0U);
    add_key_u64(
        builder, "container-fixed", type.fixed ? 1U : 0U);
    add_key_u64(
        builder, "container-index-width", type.index_width);
    add_key_u64(
        builder, "container-index-two-state",
        type.two_state_indices ? 1U : 0U);
    add_key_u64(
        builder, "container-index-signed",
        type.signed_indices ? 1U : 0U);
    add_key_u64(
        builder, "container-index-left",
        static_cast<std::uint32_t>(type.index_left));
    add_key_u64(
        builder, "container-index-right",
        static_cast<std::uint32_t>(type.index_right));
    add_key_u64(
        builder, "container-dimension-count", type.dimensions.size());
    for (const auto& dimension : type.dimensions) {
      add_key_u64(
          builder, "container-dimension-left",
          static_cast<std::uint32_t>(dimension.first));
      add_key_u64(
          builder, "container-dimension-right",
          static_cast<std::uint32_t>(dimension.second));
    }
    add_key_u64(
        builder, "container-has-maximum",
        type.maximum_elements ? 1U : 0U);
    add_key_u64(
        builder, "container-maximum",
        type.maximum_elements.value_or(0));
  }
  builder.add(
      "container-semantics",
      "resource-budgeted-static-associative-v29-aggregate-elements");
  add_key_u64(
      builder,
      "container-storage-byte-budget",
      runtime::simir::maximum_container_storage_bytes);
  add_key_u64(
      builder,
      "read-memory-byte-limit",
      runtime::simir::maximum_memory_file_bytes);
  builder.add("mutable-string-semantics", "simir-string-layout-v1");
  builder.add("text-file-semantics", "simir-text-file-v3-position-flush");
  add_key_u64(
      builder,
      "mutable-string-byte-limit",
      runtime::simir::maximum_string_bytes);
  add_key_u64(
      builder,
      "register-value-kind-count",
      process.register_value_kinds.size());
  for (const auto kind : process.register_value_kinds) {
    add_key_u64(
        builder,
        "register-value-kind",
        static_cast<std::underlying_type_t<ValueKind>>(kind));
  }
  add_key_u64(
      builder, "drive-strength-zero",
      static_cast<std::underlying_type_t<StrengthRank>>(
          process.drive_strength.zero));
  add_key_u64(
      builder, "drive-strength-one",
      static_cast<std::underlying_type_t<StrengthRank>>(
          process.drive_strength.one));
  add_key_u64(
      builder, "switch-source-present",
      process.switch_source.has_value() ? 1U : 0U);
  if (process.switch_source) {
    add_key_u64(builder, "switch-source", *process.switch_source);
  }
  add_key_u64(
      builder, "switch-target-present",
      process.switch_target.has_value() ? 1U : 0U);
  if (process.switch_target) {
    add_key_u64(builder, "switch-target", *process.switch_target);
  }
  add_key_u64(
      builder, "switch-control-present",
      process.switch_control.has_value() ? 1U : 0U);
  if (process.switch_control) {
    add_key_u64(builder, "switch-control", *process.switch_control);
  }
  add_key_u64(
      builder, "switch-active-high",
      process.switch_active_high ? 1U : 0U);
  add_key_u64(
      builder, "switch-bidirectional",
      process.switch_bidirectional ? 1U : 0U);
  add_key_u64(
      builder, "switch-resistive", process.switch_resistive ? 1U : 0U);
  add_key_u64(
      builder,
      "expression-profile-count",
      process.expression_profiles.size());
  for (const auto& profile : process.expression_profiles) {
    builder.add("expression-source-path", profile.source.path);
    add_key_u64(
        builder, "expression-source-line", profile.source.line);
    add_key_u64(
        builder, "expression-source-column", profile.source.column);
    add_key_u64(builder, "expression-width", profile.width);
    add_key_u64(
        builder, "expression-signed", profile.is_signed ? 1U : 0U);
    add_key_u64(
        builder,
        "expression-sizing",
        static_cast<std::underlying_type_t<ExpressionSizingKind>>(
            profile.sizing));
    add_key_u64(
        builder,
        "expression-domain",
        static_cast<std::underlying_type_t<ExpressionValueDomain>>(
            profile.domain));
  }
  std::vector<runtime::simir::SignalId> referenced_signals;
  for (const auto& operation : process.operations) {
    fsim::runtime::simir::visit_operation(
        [&](const auto& value) {
          using OperationType = std::decay_t<decltype(value)>;
          if constexpr (requires { value.signal; }) {
            referenced_signals.push_back(value.signal);
          } else if constexpr (
              std::is_same_v<OperationType, WaitOn>) {
            referenced_signals.insert(
                referenced_signals.end(),
                value.signals.begin(),
                value.signals.end());
          }
        },
        operation);
  }
  std::ranges::sort(referenced_signals);
  const auto unique_end = std::ranges::unique(referenced_signals).begin();
  referenced_signals.erase(unique_end, referenced_signals.end());
  add_key_u64(
      builder, "referenced-signal-count", referenced_signals.size());
  for (const auto signal : referenced_signals) {
    add_key_u64(builder, "referenced-signal", signal);
    add_key_u64(
        builder, "referenced-signal-width", signal_widths[signal]);
    const auto kind =
        signal_value_kinds.empty()
            ? ValueKind::logic4
            : signal_value_kinds[signal];
    add_key_u64(
        builder,
        "referenced-signal-value-kind",
        static_cast<std::underlying_type_t<ValueKind>>(kind));
  }
  add_key_u64(builder, "sensitivity-count",
              process.static_sensitivity.size());
  for (const auto &sensitivity : process.static_sensitivity) {
    add_key_u64(builder, "sensitivity-signal", sensitivity.signal);
    add_key_u64(
        builder, "sensitivity-edge",
        static_cast<std::underlying_type_t<runtime::simir::EdgeKind>>(
            sensitivity.edge));
  }
  add_key_u64(builder, "operation-count", process.operations.size());
  for (const auto &operation : process.operations) {
    fsim::runtime::simir::visit_operation(
        [&](const auto& value) {
          using OperationType = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<OperationType, LoadConstant>) {
            builder.add("operation", "LoadConstant");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "width", value.value.width());
            add_key_u64(builder, "aval",
                        value.value.aval_words().front());
            add_key_u64(builder, "bval",
                        value.value.bval_words().front());
          } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
            builder.add("operation", "ReadSignal");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
          } else if constexpr (std::is_same_v<OperationType, SignalEvent>) {
            builder.add("operation", "SignalEvent");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "signal", value.signal);
          } else if constexpr (std::is_same_v<OperationType, SignalLastValue>) {
            builder.add("operation", "SignalLastValue");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
          } else if constexpr (std::is_same_v<OperationType, SignalLastEvent>) {
            builder.add("operation", "SignalLastEvent");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "signal", value.signal);
          } else if constexpr (
              std::is_same_v<OperationType, ReadSimulationTime>) {
            builder.add("operation", "ReadSimulationTime");
            add_key_u64(builder, "destination", value.destination);
          } else if constexpr (
              std::is_same_v<OperationType, VitalTimingCheck>) {
            builder.add("operation", "VitalTimingCheck");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "kind", static_cast<std::uint8_t>(value.kind));
            add_key_u64(builder, "test-signal", value.test_signal);
            add_key_u64(builder, "test-offset", value.test_offset);
            add_key_u64(builder, "has-reference", value.reference_signal.has_value());
            if (value.reference_signal) {
              add_key_u64(builder, "reference-signal", *value.reference_signal);
            }
            add_key_u64(builder, "reference-offset", value.reference_offset);
            add_key_u64(
                builder, "has-trigger", value.trigger_signal.has_value());
            if (value.trigger_signal) {
              add_key_u64(builder, "trigger-signal", *value.trigger_signal);
            }
            for (const auto limit : value.limits) {
              add_key_u64(builder, "limit", limit);
            }
            add_key_u64(builder, "reference-edges", value.reference_edges);
            add_key_u64(builder, "active-low", value.active_low);
            add_key_u64(builder, "check-enabled", value.check_enabled);
            for (const auto enabled : value.enables) {
              add_key_u64(builder, "direction-enabled", enabled);
            }
            add_key_u64(builder, "x-on", value.x_on);
            add_key_u64(builder, "message-on", value.message_on);
            add_key_u64(builder, "severity", static_cast<std::uint8_t>(value.severity));
            builder.add("message", value.message);
            builder.add("source-path", value.source.path);
            add_key_u64(builder, "source-line", value.source.line);
            add_key_u64(builder, "source-column", value.source.column);
          } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
            builder.add("operation", "VitalDelay");
            add_key_u64(builder, "kind", static_cast<std::uint8_t>(value.kind));
            add_key_u64(builder, "shape", static_cast<std::uint8_t>(value.shape));
            add_key_u64(builder, "output", value.output);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "has-glitch-data", value.glitch_data.has_value());
            if (value.glitch_data) {
              add_key_u64(builder, "glitch-data", *value.glitch_data);
            }
            for (const auto delay : value.default_delays) {
              add_key_u64(builder, "default-delay", delay);
            }
            add_key_u64(builder, "path-count", value.paths.size());
            for (const auto& path : value.paths) {
              add_key_u64(builder, "input-change", path.input_change_time);
              add_key_u64(builder, "condition", path.condition);
              for (const auto delay : path.delays) {
                add_key_u64(builder, "path-delay", delay);
              }
            }
            add_key_u64(builder, "mode", static_cast<std::uint8_t>(value.mode));
            add_key_u64(builder, "output-map", value.output_map);
            add_key_u64(builder, "x-on", value.x_on);
            add_key_u64(builder, "message-on", value.message_on);
            add_key_u64(builder, "negative-preemption", value.negative_preemption);
            add_key_u64(builder, "ignore-default", value.ignore_default_delay);
            add_key_u64(builder, "reject-fast", value.reject_fast_path);
            add_key_u64(builder, "severity", static_cast<std::uint8_t>(value.severity));
            builder.add("message", value.message);
            builder.add("source-path", value.source_location.path);
            add_key_u64(builder, "source-line", value.source_location.line);
            add_key_u64(builder, "source-column", value.source_location.column);
          } else if constexpr (std::is_same_v<OperationType, SignalActive>) {
            builder.add("operation", "SignalActive");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "signal", value.signal);
          } else if constexpr (
              std::is_same_v<OperationType, SignalLastActive>) {
            builder.add("operation", "SignalLastActive");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "signal", value.signal);
          } else if constexpr (
              std::is_same_v<OperationType, SignalDriving>) {
            builder.add("operation", "SignalDriving");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "signal", value.signal);
          } else if constexpr (
              std::is_same_v<OperationType, SignalDrivingValue>) {
            builder.add("operation", "SignalDrivingValue");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
          } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
            builder.add("operation", "CopyRegister");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, LoadStringConstant>) {
            builder.add("operation", "LoadStringConstant");
            add_key_u64(builder, "destination", value.destination);
            builder.add("literal-bytes", value.value);
          } else if constexpr (std::is_same_v<OperationType, CopyStringRegister>) {
            builder.add("operation", "CopyStringRegister");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, ReadStringObject>) {
            builder.add("operation", "ReadStringObject");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "object", value.object);
          } else if constexpr (std::is_same_v<OperationType, WriteStringObject>) {
            builder.add("operation", "WriteStringObject");
            add_key_u64(builder, "object", value.object);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, ConcatenateStrings>) {
            builder.add("operation", "ConcatenateStrings");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(
                builder, "operand-count", value.operands.size());
            for (const auto operand : value.operands) {
              add_key_u64(builder, "operand", operand);
            }
          } else if constexpr (std::is_same_v<OperationType, CompareStrings>) {
            builder.add("operation", "CompareStrings");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "lhs", value.lhs);
            add_key_u64(builder, "rhs", value.rhs);
            add_key_u64(
                builder, "not-equal", value.not_equal ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, StringLength>) {
            builder.add("operation", "StringLength");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, StringIndex>) {
            builder.add("operation", "StringIndex");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "index", value.index);
            add_key_u64(
                builder,
                "signed-index",
                value.signed_index ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, StringReplaceByte>) {
            builder.add("operation", "StringReplaceByte");
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "index", value.index);
            add_key_u64(builder, "source", value.source);
            add_key_u64(
                builder,
                "signed-index",
                value.signed_index ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, StringMethod>) {
            builder.add("operation", "StringMethod");
            add_key_u64(
                builder, "kind",
                static_cast<std::uint8_t>(value.operation));
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(
                builder, "string-destination",
                value.string_destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "argument", value.argument);
            add_key_u64(builder, "first", value.first);
            add_key_u64(builder, "second", value.second);
            add_key_u64(
                builder, "format",
                static_cast<std::uint8_t>(value.format));
            add_key_u64(
                builder, "minimum-width", value.minimum_width);
            add_key_u64(
                builder, "signed-decimal",
                value.signed_decimal ? 1U : 0U);
            add_key_u64(
                builder, "suppress-leading-zero",
                value.suppress_leading_zero ? 1U : 0U);
            add_key_u64(
                builder, "left-justify",
                value.left_justify ? 1U : 0U);
            add_key_u64(
                builder, "zero-pad", value.zero_pad ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::ResizeContainer>) {
            builder.add("operation", "ResizeContainer");
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "size", value.size);
            add_key_u64(
                builder, "has-initializer",
                value.initializer ? 1U : 0U);
            add_key_u64(
                builder, "initializer",
                value.initializer.value_or(0));
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::CopyContainerRegister>) {
            builder.add("operation", "CopyContainerRegister");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::ConditionalContainerSelect>) {
            builder.add("operation", "ConditionalContainerSelect");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "condition", value.condition);
            add_key_u64(builder, "when-true", value.when_true);
            add_key_u64(builder, "when-false", value.when_false);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::CompareContainers>) {
            builder.add("operation", "CompareContainers");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "lhs", value.lhs);
            add_key_u64(builder, "rhs", value.rhs);
            add_key_u64(
                builder, "case-equal", value.case_equal ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::ReadContainerObject>) {
            builder.add("operation", "ReadContainerObject");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "object", value.object);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::WriteContainerObject>) {
            builder.add("operation", "WriteContainerObject");
            add_key_u64(builder, "object", value.object);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerSize>) {
            builder.add("operation", "ContainerSize");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerReduction>) {
            builder.add("operation", "ContainerReduction");
            add_key_u64(
                builder, "reduction",
                static_cast<std::uint64_t>(value.operation));
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(
                builder, "transformation-count",
                value.transformation.size());
            for (const auto& node : value.transformation) {
              add_key_u64(
                  builder, "transformation-operation",
                  static_cast<std::uint64_t>(node.operation));
              add_key_u64(
                  builder, "transformation-value-kind",
                  static_cast<std::uint64_t>(node.value_kind));
              add_key_u64(
                  builder, "transformation-left", node.left);
              add_key_u64(
                  builder, "transformation-right", node.right);
              add_key_u64(
                  builder, "transformation-third", node.third);
              add_key_u64(
                  builder, "transformation-constant-width",
                  node.constant.width());
              const auto word =
                  node.constant.empty()
                      ? runtime::Logic4Word{}
                      : node.constant.low_word();
              add_key_u64(
                  builder, "transformation-constant-aval",
                  word.aval);
              add_key_u64(
                  builder, "transformation-constant-bval",
                  word.bval);
            }
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::OrderContainer>) {
            builder.add("operation", "OrderContainer");
            add_key_u64(
                builder, "ordering",
                static_cast<std::uint64_t>(value.operation));
            add_key_u64(builder, "target", value.target);
            add_key_u64(
                builder, "ordering-key-count",
                value.key.size());
            for (const auto& node : value.key) {
              add_key_u64(
                  builder, "ordering-key-operation",
                  static_cast<std::uint64_t>(node.operation));
              add_key_u64(
                  builder, "ordering-key-value-kind",
                  static_cast<std::uint64_t>(node.value_kind));
              add_key_u64(
                  builder, "ordering-key-left", node.left);
              add_key_u64(
                  builder, "ordering-key-right", node.right);
              add_key_u64(
                  builder, "ordering-key-third", node.third);
              add_key_u64(
                  builder, "ordering-key-constant-width",
                  node.constant.width());
              const auto word =
                  node.constant.empty()
                      ? runtime::Logic4Word{}
                      : node.constant.low_word();
              add_key_u64(
                  builder, "ordering-key-constant-aval",
                  word.aval);
              add_key_u64(
                  builder, "ordering-key-constant-bval",
                  word.bval);
            }
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::LocateContainer>) {
            builder.add("operation", "LocateContainer");
            add_key_u64(
                builder, "locator",
                static_cast<std::uint64_t>(value.operation));
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(
                builder, "predicate-count",
                value.predicate.size());
            for (const auto& node : value.predicate) {
              add_key_u64(
                  builder, "predicate-operation",
                  static_cast<std::uint64_t>(node.operation));
              add_key_u64(
                  builder, "predicate-value-kind",
                  static_cast<std::uint64_t>(node.value_kind));
              add_key_u64(
                  builder, "predicate-left", node.left);
              add_key_u64(
                  builder, "predicate-right", node.right);
              add_key_u64(
                  builder, "predicate-third", node.third);
              add_key_u64(
                  builder, "predicate-constant-width",
                  node.constant.width());
              const auto word =
                  node.constant.empty()
                      ? runtime::Logic4Word{}
                      : node.constant.low_word();
              add_key_u64(
                  builder, "predicate-constant-aval",
                  word.aval);
              add_key_u64(
                  builder, "predicate-constant-bval",
                  word.bval);
            }
            add_key_u64(
                builder, "locator-transformation-count",
                value.transformation.size());
            for (const auto& node : value.transformation) {
              add_key_u64(
                  builder, "locator-transformation-operation",
                  static_cast<std::uint64_t>(node.operation));
              add_key_u64(
                  builder, "locator-transformation-value-kind",
                  static_cast<std::uint64_t>(node.value_kind));
              add_key_u64(
                  builder, "locator-transformation-left",
                  node.left);
              add_key_u64(
                  builder, "locator-transformation-right",
                  node.right);
              add_key_u64(
                  builder, "locator-transformation-third",
                  node.third);
              add_key_u64(
                  builder, "locator-transformation-constant-width",
                  node.constant.width());
              const auto word =
                  node.constant.empty()
                      ? runtime::Logic4Word{}
                      : node.constant.low_word();
              add_key_u64(
                  builder, "locator-transformation-constant-aval",
                  word.aval);
              add_key_u64(
                  builder, "locator-transformation-constant-bval",
                  word.bval);
            }
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerRead>) {
            builder.add("operation", "ContainerRead");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "index", value.index);
            add_key_u64(
                builder, "linear-index",
                value.linear_index ? 1U : 0U);
            add_key_u64(
                builder, "signed-index",
                value.signed_index ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerWrite>) {
            builder.add("operation", "ContainerWrite");
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "index", value.index);
            add_key_u64(builder, "source", value.source);
            add_key_u64(
                builder, "linear-index",
                value.linear_index ? 1U : 0U);
            add_key_u64(
                builder, "signed-index",
                value.signed_index ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::DeleteContainer>) {
            builder.add("operation", "DeleteContainer");
            add_key_u64(builder, "target", value.target);
            add_key_u64(
                builder, "has-index", value.index ? 1U : 0U);
            add_key_u64(
                builder, "index", value.index.value_or(0));
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerExists>) {
            builder.add("operation", "ContainerExists");
            add_key_u64(
                builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "index", value.index);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::TraverseContainer>) {
            builder.add("operation", "TraverseContainer");
            add_key_u64(
                builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "index", value.index);
            add_key_u64(
                builder, "traversal",
                static_cast<std::uint64_t>(value.traversal));
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::LoadMemory>) {
            builder.add("operation", "LoadMemory");
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "path", value.path);
            add_key_u64(
                builder, "has-start",
                value.start ? 1U : 0U);
            add_key_u64(
                builder, "start", value.start.value_or(0));
            add_key_u64(
                builder, "has-finish",
                value.finish ? 1U : 0U);
            add_key_u64(
                builder, "finish", value.finish.value_or(0));
            add_key_u64(
                builder, "hexadecimal",
                value.hexadecimal ? 1U : 0U);
            add_key_u64(
                builder, "write", value.write ? 1U : 0U);
          } else if constexpr (std::is_same_v<
                                   OperationType,
                                   runtime::simir::VitalMemoryDeclare>) {
            builder.add("operation", "VitalMemoryDeclare");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "word-count", value.word_count);
            add_key_u64(builder, "word-width", value.word_width);
            add_key_u64(builder, "subword-width", value.subword_width);
            add_key_u64(builder, "load-file", value.load_file);
            add_key_u64(builder, "binary", value.binary ? 1U : 0U);
            add_key_u64(
                builder, "embedded-load", value.embedded_load ? 1U : 0U);
            builder.add("embedded-load-text", value.embedded_load_text);
            builder.add("source", value.source.path);
            add_key_u64(builder, "source-line", value.source.line);
            add_key_u64(builder, "source-column", value.source.column);
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::PushContainer>) {
            builder.add("operation", "PushContainer");
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "front", value.front ? 1U : 0U);
            add_key_u64(
                builder, "has-index", value.index ? 1U : 0U);
            add_key_u64(
                builder, "index", value.index.value_or(0));
          } else if constexpr (std::is_same_v<OperationType, runtime::simir::PopContainer>) {
            builder.add("operation", "PopContainer");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "front", value.front ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FileOpen>) {
            builder.add("operation", "FileOpen");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "path", value.path);
            add_key_u64(builder, "mode", value.mode);
            add_key_u64(
                builder, "has-status", value.status ? 1U : 0U);
            add_key_u64(
                builder, "status", value.status.value_or(0));
            add_key_u64(builder, "vhdl", value.vhdl ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FileClose>) {
            builder.add("operation", "FileClose");
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(
                builder, "clear", value.clear_handle ? 1U : 0U);
            add_key_u64(
                builder, "ignore-zero", value.ignore_zero ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FileWriteLiteral>) {
            builder.add("operation", "FileWriteLiteral");
            add_key_u64(builder, "handle", value.handle);
            builder.add("text", value.text);
            add_key_u64(
                builder, "newline", value.newline ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FileWriteFormatted>) {
            builder.add("operation", "FileWriteFormatted");
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "width", value.width);
            add_key_u64(
                builder, "format",
                static_cast<std::uint64_t>(value.format));
            builder.add("prefix", value.prefix);
            builder.add("suffix", value.suffix);
            add_key_u64(
                builder, "newline", value.newline ? 1U : 0U);
            add_key_u64(
                builder,
                "signed-decimal",
                value.signed_decimal ? 1U : 0U);
            add_key_u64(
                builder,
                "suppress-leading-zero",
                value.suppress_leading_zero ? 1U : 0U);
            add_key_u64(
                builder, "minimum-width", value.minimum_width);
            add_key_u64(
                builder,
                "left-justify",
                value.left_justify ? 1U : 0U);
            add_key_u64(
                builder, "zero-pad", value.zero_pad ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FileWriteString>) {
            builder.add("operation", "FileWriteString");
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(builder, "source", value.source);
            builder.add("prefix", value.prefix);
            builder.add("suffix", value.suffix);
            add_key_u64(
                builder, "newline", value.newline ? 1U : 0U);
            add_key_u64(
                builder, "clear-source", value.clear_source ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FileReadLine>) {
            builder.add("operation", "FileReadLine");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "source", value.source);
            add_key_u64(
                builder, "kind",
                static_cast<std::uint8_t>(value.kind));
            add_key_u64(
                builder, "vhdl-textio", value.vhdl_textio ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FileEndOfFile>) {
            builder.add("operation", "FileEndOfFile");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(
                builder, "lookahead", value.lookahead ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FileErrorStatus>) {
            builder.add("operation", "FileErrorStatus");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(builder, "target", value.target);
          } else if constexpr (std::is_same_v<OperationType, FileScan>) {
            builder.add("operation", "FileScan");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(builder, "source", value.source);
            add_key_u64(
                builder, "string-source", value.string_source ? 1U : 0U);
            add_key_u64(
                builder, "conversion-count", value.conversions.size());
            for (const auto& conversion : value.conversions) {
              builder.add("scan-prefix", conversion.prefix);
              add_key_u64(builder, "scan-format",
                  static_cast<std::uint8_t>(conversion.format));
              add_key_u64(builder, "scan-maximum",
                  conversion.maximum_characters);
              add_key_u64(builder, "scan-suppress",
                  conversion.suppress ? 1U : 0U);
              add_key_u64(builder, "scan-target-kind",
                  static_cast<std::uint8_t>(conversion.target.kind));
              add_key_u64(builder, "scan-target-id", conversion.target.id);
              add_key_u64(
                  builder, "scan-target-width", conversion.target.width);
              add_key_u64(builder, "scan-target-two-state",
                  conversion.target.two_state ? 1U : 0U);
            }
            builder.add("scan-trailing", value.trailing_text);
            add_key_u64(
                builder,
                "scan-require-assignments",
                value.require_assignments ? 1U : 0U);
            add_key_u64(
                builder, "scan-has-success", value.success ? 1U : 0U);
            if (value.success) {
              add_key_u64(builder, "scan-success", *value.success);
            }
            add_key_u64(
                builder,
                "scan-consume-string-source",
                value.consume_string_source ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FileBinaryRead>) {
            builder.add("operation", "FileBinaryRead");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "target-kind",
                static_cast<std::uint8_t>(value.target_kind));
            add_key_u64(builder, "width", value.width);
            add_key_u64(builder, "two-state", value.two_state ? 1U : 0U);
            add_key_u64(builder, "start", value.start);
            add_key_u64(builder, "count", value.count);
            add_key_u64(builder, "has-start", value.has_start ? 1U : 0U);
            add_key_u64(builder, "has-count", value.has_count ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FilePosition>) {
            builder.add("operation", "FilePosition");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(builder, "offset", value.offset);
            add_key_u64(builder, "origin", value.origin);
            add_key_u64(builder, "kind",
                static_cast<std::uint8_t>(value.kind));
          } else if constexpr (std::is_same_v<OperationType, FileFlush>) {
            builder.add("operation", "FileFlush");
            add_key_u64(builder, "handle", value.handle);
            add_key_u64(builder, "all", value.all ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, UnaryNot>) {
            builder.add("operation", "UnaryNot");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, LogicalNot>) {
            builder.add("operation", "LogicalNot");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, LogicalBinary>) {
            builder.add("operation", "LogicalBinary");
            add_key_u64(
                builder, "logical-binary-operator",
                static_cast<
                    std::underlying_type_t<LogicalBinaryOperator>>(
                    value.operation));
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "lhs", value.lhs);
            add_key_u64(builder, "rhs", value.rhs);
          } else if constexpr (std::is_same_v<OperationType, Reduction>) {
            builder.add("operation", "Reduction");
            add_key_u64(
                builder, "reduction-operator",
                static_cast<
                    std::underlying_type_t<ReductionOperator>>(
                    value.operation));
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, CountOnes>) {
            builder.add("operation", "CountOnes");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, CountBits>) {
            builder.add("operation", "CountBits");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "state-mask", value.state_mask);
          } else if constexpr (std::is_same_v<OperationType, Shift>) {
            builder.add("operation", "Shift");
            add_key_u64(
                builder, "shift-operator",
                static_cast<std::underlying_type_t<ShiftOperator>>(
                    value.operation));
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "value", value.value);
            add_key_u64(builder, "amount", value.amount);
            add_key_u64(
                builder, "signed-amount", value.signed_amount ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, Extract>) {
            builder.add("operation", "Extract");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "offset", value.offset);
            add_key_u64(builder, "width", value.width);
          } else if constexpr (std::is_same_v<OperationType, DynamicExtract>) {
            builder.add("operation", "DynamicExtract");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_dynamic_index_key(builder, value.selection);
          } else if constexpr (std::is_same_v<OperationType, DynamicPartSelect>) {
            builder.add("operation", "DynamicPartSelect");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "base", value.base);
            add_key_u64(
                builder,
                "left",
                static_cast<std::uint64_t>(value.left));
            add_key_u64(
                builder,
                "right",
                static_cast<std::uint64_t>(value.right));
            add_key_u64(builder, "base-offset", value.base_offset);
            add_key_u64(builder, "width", value.width);
            add_key_u64(
                builder, "increasing", value.increasing ? 1U : 0U);
            add_key_u64(
                builder,
                "source-descending",
                value.source_descending ? 1U : 0U);
            add_key_u64(
                builder, "two-state", value.two_state ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, Insert>) {
            builder.add("operation", "Insert");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "offset", value.offset);
          } else if constexpr (std::is_same_v<OperationType, DynamicInsert>) {
            builder.add("operation", "DynamicInsert");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "source", value.source);
            add_dynamic_index_key(builder, value.selection);
          } else if constexpr (std::is_same_v<OperationType, DynamicPartInsert>) {
            builder.add("operation", "DynamicPartInsert");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "target", value.target);
            add_key_u64(builder, "source", value.source);
            add_dynamic_part_index_key(builder, value.selection);
          } else if constexpr (std::is_same_v<OperationType, Concatenate>) {
            builder.add("operation", "Concatenate");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "width", value.width);
            add_key_u64(
                builder, "operand-count", value.operands.size());
            for (const auto operand : value.operands) {
              add_key_u64(builder, "operand", operand);
            }
          } else if constexpr (std::is_same_v<OperationType, Binary>) {
            builder.add("operation", "Binary");
            add_key_u64(
                builder, "binary-operator",
                static_cast<std::underlying_type_t<BinaryOperator>>(
                    value.operation));
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "lhs", value.lhs);
            add_key_u64(builder, "rhs", value.rhs);
          } else if constexpr (std::is_same_v<OperationType, IntegerUnary>) {
            builder.add("operation", "IntegerUnary");
            add_key_u64(
                builder, "integer-unary-operator",
                static_cast<
                    std::underlying_type_t<IntegerUnaryOperator>>(
                    value.operation));
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, IntegerBinary>) {
            builder.add("operation", "IntegerBinary");
            add_key_u64(
                builder, "integer-binary-operator",
                static_cast<
                    std::underlying_type_t<IntegerBinaryOperator>>(
                    value.operation));
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "lhs", value.lhs);
            add_key_u64(builder, "rhs", value.rhs);
          } else if constexpr (std::is_same_v<OperationType, IntegerCheck>) {
            builder.add("operation", "IntegerCheck");
            add_key_u64(builder, "source", value.source);
            add_key_u64(
                builder, "lower",
                static_cast<std::uint32_t>(value.lower));
            add_key_u64(
                builder, "upper",
                static_cast<std::uint32_t>(value.upper));
          } else if constexpr (std::is_same_v<OperationType, ConditionalSelect>) {
            builder.add("operation", "ConditionalSelect");
            add_key_u64(builder, "destination", value.destination);
            add_key_u64(builder, "condition", value.condition);
            add_key_u64(builder, "when-true", value.when_true);
            add_key_u64(builder, "when-false", value.when_false);
          } else if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
            builder.add("operation", "WriteBlocking");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
            builder.add("operation", "WriteUpdate");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
          } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
            builder.add("operation", "WriteAfter");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "delay", value.delay);
          } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
            builder.add("operation", "WriteInertial");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "rise-delay", value.delays.rise);
            add_key_u64(builder, "fall-delay", value.delays.fall);
            add_key_u64(
                builder, "turnoff-delay", value.delays.turnoff);
          } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
            builder.add("operation", "WriteProjected");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "delay", value.delay);
            add_key_u64(builder, "rejection", value.rejection);
            add_key_u64(
                builder,
                "mode",
                static_cast<std::uint8_t>(value.mode));
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
            builder.add("operation", "WriteProjectedWaveform");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(
                builder, "element-count", value.elements.size());
            for (const auto& element : value.elements) {
              add_key_u64(builder, "source", element.source);
              add_key_u64(builder, "delay", element.delay);
            }
            add_key_u64(builder, "rejection", value.rejection);
            add_key_u64(
                builder,
                "mode",
                static_cast<std::uint8_t>(value.mode));
          } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
            builder.add("operation", "WriteBlockingSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "offset", value.offset);
          } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
            builder.add("operation", "WriteUpdateSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "offset", value.offset);
          } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
            builder.add("operation", "WriteAfterSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "offset", value.offset);
            add_key_u64(builder, "delay", value.delay);
          } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
            builder.add("operation", "WriteInertialSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "offset", value.offset);
            add_key_u64(builder, "rise-delay", value.delays.rise);
            add_key_u64(builder, "fall-delay", value.delays.fall);
            add_key_u64(
                builder, "turnoff-delay", value.delays.turnoff);
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
            builder.add("operation", "WriteProjectedSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "offset", value.offset);
            add_key_u64(builder, "delay", value.delay);
            add_key_u64(builder, "rejection", value.rejection);
            add_key_u64(
                builder,
                "mode",
                static_cast<std::uint8_t>(value.mode));
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
            builder.add("operation", "WriteProjectedWaveformSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "offset", value.offset);
            add_key_u64(
                builder, "element-count", value.elements.size());
            for (const auto& element : value.elements) {
              add_key_u64(builder, "source", element.source);
              add_key_u64(builder, "delay", element.delay);
            }
            add_key_u64(builder, "rejection", value.rejection);
            add_key_u64(
                builder,
                "mode",
                static_cast<std::uint8_t>(value.mode));
          } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
            builder.add(
                "operation", "WriteBlockingDynamicSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_dynamic_index_key(builder, value.selection);
          } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
            builder.add(
                "operation", "WriteUpdateDynamicSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_dynamic_index_key(builder, value.selection);
          } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
            builder.add(
                "operation", "WriteAfterDynamicSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_dynamic_index_key(builder, value.selection);
            add_key_u64(builder, "delay", value.delay);
          } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
            builder.add(
                "operation", "WriteBlockingDynamicPartSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_dynamic_part_index_key(builder, value.selection);
          } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
            builder.add(
                "operation", "WriteUpdateDynamicPartSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_dynamic_part_index_key(builder, value.selection);
          } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
            builder.add(
                "operation", "WriteAfterDynamicPartSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_dynamic_part_index_key(builder, value.selection);
            add_key_u64(builder, "delay", value.delay);
          } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
            builder.add("operation", "ForceSignalSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_key_u64(builder, "offset", value.offset);
          } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
            builder.add("operation", "ReleaseSignalSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "offset", value.offset);
            add_key_u64(builder, "width", value.width);
          } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
            builder.add(
                "operation", "WriteInertialDynamicSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_dynamic_index_key(builder, value.selection);
            add_key_u64(builder, "rise-delay", value.delays.rise);
            add_key_u64(builder, "fall-delay", value.delays.fall);
            add_key_u64(
                builder, "turnoff-delay", value.delays.turnoff);
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
            builder.add(
                "operation", "WriteProjectedDynamicSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_key_u64(builder, "source", value.source);
            add_dynamic_index_key(builder, value.selection);
            add_key_u64(builder, "delay", value.delay);
            add_key_u64(builder, "rejection", value.rejection);
            add_key_u64(
                builder,
                "mode",
                static_cast<std::uint8_t>(value.mode));
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
            builder.add(
                "operation",
                "WriteProjectedWaveformDynamicSlice");
            add_key_u64(builder, "signal", value.signal);
            add_key_u64(
                builder, "signal-width", signal_widths[value.signal]);
            add_dynamic_index_key(builder, value.selection);
            add_key_u64(
                builder, "element-count", value.elements.size());
            for (const auto& element : value.elements) {
              add_key_u64(builder, "source", element.source);
              add_key_u64(builder, "delay", element.delay);
            }
            add_key_u64(builder, "rejection", value.rejection);
            add_key_u64(
                builder,
                "mode",
                static_cast<std::uint8_t>(value.mode));
          } else if constexpr (std::is_same_v<OperationType, Assert>) {
            builder.add("operation", "Assert");
            add_key_u64(builder, "condition", value.condition);
            builder.add("message", value.message);
            add_key_u64(
                builder, "severity",
                static_cast<std::underlying_type_t<
                    runtime::simir::AssertionSeverity>>(
                    value.severity));
            builder.add("source-path", value.source.path);
            add_key_u64(builder, "source-line", value.source.line);
            add_key_u64(builder, "source-column", value.source.column);
          } else if constexpr (std::is_same_v<OperationType, DebugPoint>) {
            builder.add("operation", "DebugPoint");
            add_key_u64(
                builder, "kind",
                static_cast<std::underlying_type_t<
                    runtime::simir::DebugPointKind>>(value.kind));
            builder.add("source-path", value.source.path);
            add_key_u64(builder, "source-line", value.source.line);
            add_key_u64(builder, "source-column", value.source.column);
            builder.add("scope", value.scope);
          } else if constexpr (std::is_same_v<OperationType, Display>) {
            builder.add("operation", "Display");
            builder.add("text", value.text);
            add_key_u64(builder, "newline", value.newline ? 1U : 0U);
            add_key_u64(
                builder,
                "postponed",
                value.postponed ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, FormatDisplay>) {
            builder.add("operation", "FormatDisplay");
            add_key_u64(builder, "source", value.source);
            add_key_u64(
                builder,
                "format",
                static_cast<std::underlying_type_t<
                    runtime::simir::OutputFormat>>(value.format));
            builder.add("prefix", value.prefix);
            builder.add("suffix", value.suffix);
            add_key_u64(
                builder, "newline", value.newline ? 1U : 0U);
            add_key_u64(
                builder,
                "postponed",
                value.postponed ? 1U : 0U);
            add_key_u64(
                builder,
                "signed-decimal",
                value.signed_decimal ? 1U : 0U);
            add_key_u64(
                builder,
                "suppress-leading-zero",
                value.suppress_leading_zero ? 1U : 0U);
            add_key_u64(
                builder, "minimum-width", value.minimum_width);
            add_key_u64(
                builder,
                "left-justify",
                value.left_justify ? 1U : 0U);
            add_key_u64(
                builder,
                "zero-pad",
                value.zero_pad ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, TimeDisplay>) {
            builder.add("operation", "TimeDisplay");
            builder.add("prefix", value.prefix);
            builder.add("suffix", value.suffix);
            add_key_u64(
                builder, "newline", value.newline ? 1U : 0U);
            add_key_u64(
                builder,
                "postponed",
                value.postponed ? 1U : 0U);
            add_key_u64(
                builder, "minimum-width", value.minimum_width);
            add_key_u64(
                builder,
                "left-justify",
                value.left_justify ? 1U : 0U);
            add_key_u64(
                builder,
                "zero-pad",
                value.zero_pad ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, StringDisplay>) {
            builder.add("operation", "StringDisplay");
            add_key_u64(builder, "source", value.source);
            builder.add("prefix", value.prefix);
            builder.add("suffix", value.suffix);
            add_key_u64(
                builder, "newline", value.newline ? 1U : 0U);
            add_key_u64(
                builder,
                "postponed",
                value.postponed ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, StringReport>) {
            builder.add("operation", "StringReport");
            add_key_u64(builder, "message", value.message);
            add_key_u64(builder, "severity", value.severity);
            builder.add("source-path", value.source.path);
            add_key_u64(builder, "source-line", value.source.line);
            add_key_u64(builder, "source-column", value.source.column);
            add_key_u64(
                builder, "standalone", value.standalone ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, MonitorInstall>) {
            builder.add("operation", "MonitorInstall");
            add_key_u64(
                builder, "value-count", value.values.size());
            for (std::size_t index = 0;
                 index < value.values.size();
                 ++index) {
              const auto& item = value.values[index];
              const auto key =
                  "value-" + std::to_string(index) + "-";
              add_key_u64(
                  builder,
                  key + "kind",
                  static_cast<std::underlying_type_t<
                      runtime::simir::MonitorValueKind>>(item.kind));
              add_key_u64(
                  builder, key + "signal", item.signal);
              add_key_u64(
                  builder,
                  key + "format",
                  static_cast<std::underlying_type_t<
                      runtime::simir::OutputFormat>>(item.format));
              builder.add(key + "prefix", item.prefix);
              add_key_u64(
                  builder,
                  key + "signed-decimal",
                  item.signed_decimal ? 1U : 0U);
              add_key_u64(
                  builder,
                  key + "suppress-leading-zero",
                  item.suppress_leading_zero ? 1U : 0U);
              add_key_u64(
                  builder,
                  key + "minimum-width",
                  item.minimum_width);
              add_key_u64(
                  builder,
                  key + "left-justify",
                  item.left_justify ? 1U : 0U);
              add_key_u64(
                  builder,
                  key + "zero-pad",
                  item.zero_pad ? 1U : 0U);
            }
            builder.add("trailing-text", value.trailing_text);
            add_key_u64(
                builder, "newline", value.newline ? 1U : 0U);
            add_key_u64(
                builder, "one-shot", value.one_shot ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
            builder.add("operation", "MonitorControl");
            add_key_u64(
                builder, "enabled", value.enabled ? 1U : 0U);
          } else if constexpr (std::is_same_v<OperationType, RandomValue>) {
            builder.add("operation", "RandomValue");
            add_key_u64(
                builder, "destination", value.destination);
            add_key_u64(
                builder,
                "kind",
                static_cast<std::underlying_type_t<
                    runtime::simir::RandomKind>>(value.kind));
            add_key_u64(
                builder,
                "has-maximum",
                value.maximum.has_value() ? 1U : 0U);
            if (value.maximum) {
              add_key_u64(
                  builder, "maximum", *value.maximum);
            }
            add_key_u64(
                builder,
                "has-minimum",
                value.minimum.has_value() ? 1U : 0U);
            if (value.minimum) {
              add_key_u64(
                  builder, "minimum", *value.minimum);
            }
          } else if constexpr (std::is_same_v<OperationType, Report>) {
            builder.add("operation", "Report");
            builder.add("message", value.message);
            add_key_u64(
                builder,
                "severity",
                static_cast<std::underlying_type_t<
                    runtime::simir::AssertionSeverity>>(
                    value.severity));
            builder.add("source-path", value.source.path);
            add_key_u64(
                builder, "source-line", value.source.line);
            add_key_u64(
                builder, "source-column", value.source.column);
          } else if constexpr (std::is_same_v<OperationType, Jump>) {
            builder.add("operation", "Jump");
            add_key_u64(builder, "target", value.target);
          } else if constexpr (std::is_same_v<OperationType, Call>) {
            builder.add("operation", "Call");
            add_key_u64(builder, "target", value.target);
            add_key_u64(
                builder, "return-target", value.return_target);
            add_key_u64(
                builder, "stack-pointer", value.stack.pointer);
            add_key_u64(
                builder, "stack-entries", value.stack.entries);
            add_key_u64(
                builder, "stack-capacity", value.stack.capacity);
          } else if constexpr (std::is_same_v<OperationType, Return>) {
            builder.add("operation", "Return");
            add_key_u64(
                builder, "stack-pointer", value.stack.pointer);
            add_key_u64(
                builder, "stack-entries", value.stack.entries);
            add_key_u64(
                builder, "stack-capacity", value.stack.capacity);
          } else if constexpr (std::is_same_v<OperationType, Branch>) {
            builder.add("operation", "Branch");
            add_key_u64(builder, "condition", value.condition);
            add_key_u64(builder, "when-true", value.when_true);
            add_key_u64(builder, "when-false", value.when_false);
            add_key_u64(
                builder, "unknown-policy",
                static_cast<
                    std::underlying_type_t<UnknownBranchPolicy>>(
                    value.unknown_policy));
          } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
            builder.add("operation", "WaitFor");
            add_key_u64(builder, "delay", value.delay);
          } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
            builder.add("operation", "WaitOn");
            add_key_u64(
                builder, "wait-on-signal-count", value.signals.size());
            for (std::size_t index = 0;
                 index < value.signals.size(); ++index) {
              const auto signal = value.signals[index];
              const auto edge =
                  value.edges.empty()
                      ? EdgeKind::any
                      : value.edges[index];
              add_key_u64(builder, "wait-on-signal", signal);
              add_key_u64(
                  builder, "wait-on-signal-width",
                  signal_widths[signal]);
              add_key_u64(
                  builder, "wait-on-edge",
                  static_cast<std::underlying_type_t<EdgeKind>>(
                      edge));
            }
            add_key_u64(
                builder,
                "wait-on-has-timeout",
                value.timeout.has_value());
            if (value.timeout) {
              add_key_u64(
                  builder,
                  "wait-on-timeout",
                  *value.timeout);
            }
            add_key_u64(
                builder,
                "wait-on-has-timeout-result",
                value.timeout_result.has_value());
            if (value.timeout_result) {
              add_key_u64(
                  builder,
                  "wait-on-timeout-result",
                  *value.timeout_result);
            }
            add_key_u64(
                builder,
                "wait-on-has-timeout-origin",
                value.timeout_origin.has_value());
            if (value.timeout_origin) {
              add_key_u64(
                  builder,
                  "wait-on-timeout-origin",
                  *value.timeout_origin);
            }
          } else if constexpr (std::is_same_v<OperationType, ClassAllocate>) {
            builder.add("operation", "ClassAllocate");
            add_key_u64(builder, "class-destination", value.destination);
            builder.add(
                "class-specialization", value.specialization_identity);
            builder.add("class-declared-type", value.declared_type);
            add_key_u64(
                builder, "class-actual-count",
                value.constructor_actuals.size());
            for (const auto actual : value.constructor_actuals) {
              add_key_u64(builder, "class-actual", actual);
            }
            for (const auto& name : value.constructor_actual_names) {
              builder.add("class-actual-name", name);
            }
          } else if constexpr (
              std::is_same_v<OperationType, ClassPropertyRead>) {
            builder.add("operation", "ClassPropertyRead");
            add_key_u64(builder, "class-destination", value.destination);
            add_key_u64(builder, "class-receiver", value.receiver);
            builder.add("class-property", value.property_identity);
            add_key_u64(builder, "class-width", value.width);
          } else if constexpr (
              std::is_same_v<OperationType, ClassPropertyWrite>) {
            builder.add("operation", "ClassPropertyWrite");
            add_key_u64(builder, "class-receiver", value.receiver);
            add_key_u64(builder, "class-source", value.source);
            builder.add("class-property", value.property_identity);
          } else if constexpr (
              std::is_same_v<OperationType, ClassMethodCall>
              || std::is_same_v<OperationType, ClassStaticMethodCall>) {
            builder.add(
                "operation",
                std::is_same_v<OperationType, ClassMethodCall>
                    ? "ClassMethodCall" : "ClassStaticMethodCall");
            add_key_u64(builder, "class-destination", value.destination);
            if constexpr (std::is_same_v<OperationType, ClassMethodCall>) {
              add_key_u64(builder, "class-receiver", value.receiver);
              add_key_u64(
                  builder, "class-virtual", value.virtual_dispatch ? 1U : 0U);
            }
            builder.add("class-method", value.method_identity);
            add_key_u64(builder, "class-width", value.result_width);
            add_key_u64(
                builder, "class-actual-count", value.actuals.size());
            for (std::size_t actual = 0;
                 actual < value.actuals.size(); ++actual) {
              add_key_u64(builder, "class-actual", value.actuals[actual]);
              builder.add("class-actual-name", value.actual_names[actual]);
              add_key_u64(
                  builder, "class-actual-direction",
                  value.actual_directions[actual]);
            }
          } else if constexpr (
              std::is_same_v<OperationType, ClassStaticPropertyRead>) {
            builder.add("operation", "ClassStaticPropertyRead");
            add_key_u64(builder, "class-destination", value.destination);
            builder.add("class-property", value.property_identity);
            add_key_u64(builder, "class-width", value.width);
          } else if constexpr (
              std::is_same_v<OperationType, ClassStaticPropertyWrite>) {
            builder.add("operation", "ClassStaticPropertyWrite");
            add_key_u64(builder, "class-source", value.source);
            builder.add("class-property", value.property_identity);
          } else if constexpr (std::is_same_v<OperationType, WaitSensitivity>) {
            builder.add("operation", "WaitSensitivity");
            add_key_u64(
                builder, "wait-sensitivity-count",
                process.static_sensitivity.size());
            for (const auto sensitivity : process.static_sensitivity) {
              add_key_u64(
                  builder, "wait-sensitivity-signal",
                  sensitivity.signal);
              add_key_u64(
                  builder, "wait-sensitivity-signal-width",
                  signal_widths[sensitivity.signal]);
              add_key_u64(
                  builder, "wait-sensitivity-edge",
                  static_cast<std::underlying_type_t<EdgeKind>>(
                      sensitivity.edge));
            }
          } else if constexpr (std::is_same_v<OperationType, WaitForever>) {
            builder.add("operation", "WaitForever");
          } else if constexpr (std::is_same_v<OperationType, Yield>) {
            builder.add("operation", "Yield");
          } else if constexpr (std::is_same_v<OperationType, Fork>) {
            builder.add("operation", "Fork");
            add_key_u64(
                builder, "fork-join",
                static_cast<std::underlying_type_t<ForkJoinKind>>(
                    value.join));
            add_key_u64(
                builder, "fork-branch-count", value.branches.size());
            for (const auto branch : value.branches) {
              add_key_u64(builder, "fork-branch", branch);
            }
          } else if constexpr (std::is_same_v<OperationType, ForkEnd>) {
            builder.add("operation", "ForkEnd");
          } else if constexpr (std::is_same_v<OperationType, WaitFork>) {
            builder.add("operation", "WaitFork");
          } else if constexpr (std::is_same_v<OperationType, DisableFork>) {
            builder.add("operation", "DisableFork");
          } else if constexpr (std::is_same_v<OperationType, Pause>) {
            builder.add("operation", "Pause");
          } else if constexpr (std::is_same_v<OperationType, Stop>) {
            builder.add("operation", "Stop");
          } else if constexpr (std::is_same_v<OperationType, Halt>) {
            builder.add("operation", "Halt");
          } else {
            llvm_unreachable(
                "unsupported operations were rejected before cache keying");
          }
        },
        operation);
  }
  return builder.finish();
}

[[nodiscard]] std::string make_native_module_cache_key(
    const std::string_view module_identity,
    const std::span<const std::string> process_keys) {
  CacheKeyBuilder builder;
  builder.add("llvm-module-schema", kNativeObjectCacheSchema);
  builder.add("module-identity", module_identity);
  add_key_u64(builder, "process-count", process_keys.size());
  for (const auto& process_key : process_keys) {
    builder.add("process-key", process_key);
  }
  return builder.finish();
}

[[nodiscard]] std::uint64_t cache_key_word(
    const std::string_view key, const std::size_t offset) noexcept {
  std::uint64_t result = 0;
  for (const auto character : key.substr(offset, 16)) {
    const auto digit = static_cast<std::uint64_t>(
        character >= '0' && character <= '9'
            ? character - '0'
            : character - 'a' + 10);
    result = (result << 4U) | digit;
  }
  return result;
}

[[nodiscard]] JitProcessFrameLayout
make_frame_layout(const std::string_view cache_key,
                  const std::size_t register_count,
                  const std::size_t string_register_count,
                  const bool uses_logic9) {
  return {
      cache_key_word(cache_key, 0),
      cache_key_word(cache_key, 16),
      static_cast<std::uint32_t>(register_count),
      static_cast<std::uint32_t>(string_register_count),
      uses_logic9,
  };
}

}  // namespace fsim::compiler::llvm_detail

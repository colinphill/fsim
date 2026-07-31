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
using runtime::simir::DynamicIndex;
using runtime::simir::DynamicInsert;
using runtime::simir::EdgeKind;
using runtime::simir::Extract;
using runtime::simir::FileClose;
using runtime::simir::FileEndOfFile;
using runtime::simir::FileErrorStatus;
using runtime::simir::FileOpen;
using runtime::simir::FileReadLine;
using runtime::simir::FileWriteFormatted;
using runtime::simir::FileWriteLiteral;
using runtime::simir::FileWriteString;
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
using runtime::simir::ReadStringObject;
using runtime::simir::Reduction;
using runtime::simir::ReductionOperator;
using runtime::simir::RegisterId;
using runtime::simir::RandomValue;
using runtime::simir::Report;
using runtime::simir::Return;
using runtime::simir::Shift;
using runtime::simir::ShiftOperator;
using runtime::simir::SignalActive;
using runtime::simir::SignalEvent;
using runtime::simir::SignalLastEvent;
using runtime::simir::SignalLastValue;
using runtime::simir::Stop;
using runtime::simir::StringDisplay;
using runtime::simir::StringIndex;
using runtime::simir::StringLength;
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
using runtime::simir::WriteAfterSlice;
using runtime::simir::WriteBlocking;
using runtime::simir::WriteBlockingDynamicSlice;
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
using runtime::simir::WriteUpdateSlice;
using runtime::simir::WriteStringObject;
using runtime::simir::Yield;


constexpr std::string_view kNativeObjectCacheSchema =
    "fsim-llvm-native-object-v35";

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;


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
        builder, "container-has-maximum",
        type.maximum_elements ? 1U : 0U);
    add_key_u64(
        builder, "container-maximum",
        type.maximum_elements.value_or(0));
  }
  builder.add(
      "container-semantics",
      "bounded-static-associative-v11-reduction-transformations");
  add_key_u64(
      builder,
      "container-entry-limit",
      runtime::simir::maximum_container_elements);
  add_key_u64(
      builder,
      "read-memory-byte-limit",
      runtime::simir::maximum_memory_file_bytes);
  builder.add("mutable-string-semantics", "simir-string-layout-v1");
  builder.add("text-file-semantics", "simir-text-file-v1");
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
  std::vector<runtime::simir::SignalId> referenced_signals;
  for (const auto& operation : process.operations) {
    std::visit(
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
    std::visit(
        Overloaded{
            [&](const LoadConstant &value) {
              builder.add("operation", "LoadConstant");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "width", value.value.width());
              add_key_u64(builder, "aval",
                          value.value.aval_words().front());
              add_key_u64(builder, "bval",
                          value.value.bval_words().front());
            },
            [&](const ReadSignal &value) {
              builder.add("operation", "ReadSignal");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
            },
            [&](const SignalEvent& value) {
              builder.add("operation", "SignalEvent");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "signal", value.signal);
            },
            [&](const SignalLastValue& value) {
              builder.add("operation", "SignalLastValue");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
            },
            [&](const SignalLastEvent& value) {
              builder.add("operation", "SignalLastEvent");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "signal", value.signal);
            },
            [&](const SignalActive& value) {
              builder.add("operation", "SignalActive");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "signal", value.signal);
            },
            [&](const CopyRegister& value) {
              builder.add("operation", "CopyRegister");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const LoadStringConstant& value) {
              builder.add("operation", "LoadStringConstant");
              add_key_u64(builder, "destination", value.destination);
              builder.add("literal-bytes", value.value);
            },
            [&](const CopyStringRegister& value) {
              builder.add("operation", "CopyStringRegister");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const ReadStringObject& value) {
              builder.add("operation", "ReadStringObject");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "object", value.object);
            },
            [&](const WriteStringObject& value) {
              builder.add("operation", "WriteStringObject");
              add_key_u64(builder, "object", value.object);
              add_key_u64(builder, "source", value.source);
            },
            [&](const ConcatenateStrings& value) {
              builder.add("operation", "ConcatenateStrings");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(
                  builder, "operand-count", value.operands.size());
              for (const auto operand : value.operands) {
                add_key_u64(builder, "operand", operand);
              }
            },
            [&](const CompareStrings& value) {
              builder.add("operation", "CompareStrings");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "lhs", value.lhs);
              add_key_u64(builder, "rhs", value.rhs);
              add_key_u64(
                  builder, "not-equal", value.not_equal ? 1U : 0U);
            },
            [&](const StringLength& value) {
              builder.add("operation", "StringLength");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const StringIndex& value) {
              builder.add("operation", "StringIndex");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "index", value.index);
              add_key_u64(
                  builder,
                  "signed-index",
                  value.signed_index ? 1U : 0U);
            },
            [&](const StringReplaceByte& value) {
              builder.add("operation", "StringReplaceByte");
              add_key_u64(builder, "target", value.target);
              add_key_u64(builder, "index", value.index);
              add_key_u64(builder, "source", value.source);
              add_key_u64(
                  builder,
                  "signed-index",
                  value.signed_index ? 1U : 0U);
            },
            [&](const runtime::simir::ResizeContainer& value) {
              builder.add("operation", "ResizeContainer");
              add_key_u64(builder, "target", value.target);
              add_key_u64(builder, "size", value.size);
            },
            [&](const runtime::simir::CopyContainerRegister& value) {
              builder.add("operation", "CopyContainerRegister");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const runtime::simir::ReadContainerObject& value) {
              builder.add("operation", "ReadContainerObject");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "object", value.object);
            },
            [&](const runtime::simir::WriteContainerObject& value) {
              builder.add("operation", "WriteContainerObject");
              add_key_u64(builder, "object", value.object);
              add_key_u64(builder, "source", value.source);
            },
            [&](const runtime::simir::ContainerSize& value) {
              builder.add("operation", "ContainerSize");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const runtime::simir::ContainerReduction& value) {
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
            },
            [&](const runtime::simir::OrderContainer& value) {
              builder.add("operation", "OrderContainer");
              add_key_u64(
                  builder, "ordering",
                  static_cast<std::uint64_t>(value.operation));
              add_key_u64(builder, "target", value.target);
            },
            [&](const runtime::simir::LocateContainer& value) {
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
            },
            [&](const runtime::simir::ContainerRead& value) {
              builder.add("operation", "ContainerRead");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "index", value.index);
              add_key_u64(
                  builder, "signed-index",
                  value.signed_index ? 1U : 0U);
            },
            [&](const runtime::simir::ContainerWrite& value) {
              builder.add("operation", "ContainerWrite");
              add_key_u64(builder, "target", value.target);
              add_key_u64(builder, "index", value.index);
              add_key_u64(builder, "source", value.source);
              add_key_u64(
                  builder, "signed-index",
                  value.signed_index ? 1U : 0U);
            },
            [&](const runtime::simir::DeleteContainer& value) {
              builder.add("operation", "DeleteContainer");
              add_key_u64(builder, "target", value.target);
              add_key_u64(
                  builder, "has-index", value.index ? 1U : 0U);
              add_key_u64(
                  builder, "index", value.index.value_or(0));
            },
            [&](const runtime::simir::ContainerExists& value) {
              builder.add("operation", "ContainerExists");
              add_key_u64(
                  builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "index", value.index);
            },
            [&](const runtime::simir::TraverseContainer& value) {
              builder.add("operation", "TraverseContainer");
              add_key_u64(
                  builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "index", value.index);
              add_key_u64(
                  builder, "traversal",
                  static_cast<std::uint64_t>(value.traversal));
            },
            [&](const runtime::simir::LoadMemory& value) {
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
            },
            [&](const runtime::simir::PushContainer& value) {
              builder.add("operation", "PushContainer");
              add_key_u64(builder, "target", value.target);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "front", value.front ? 1U : 0U);
            },
            [&](const runtime::simir::PopContainer& value) {
              builder.add("operation", "PopContainer");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "target", value.target);
              add_key_u64(builder, "front", value.front ? 1U : 0U);
            },
            [&](const FileOpen& value) {
              builder.add("operation", "FileOpen");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "path", value.path);
              add_key_u64(builder, "mode", value.mode);
            },
            [&](const FileClose& value) {
              builder.add("operation", "FileClose");
              add_key_u64(builder, "handle", value.handle);
            },
            [&](const FileWriteLiteral& value) {
              builder.add("operation", "FileWriteLiteral");
              add_key_u64(builder, "handle", value.handle);
              builder.add("text", value.text);
              add_key_u64(
                  builder, "newline", value.newline ? 1U : 0U);
            },
            [&](const FileWriteFormatted& value) {
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
            },
            [&](const FileWriteString& value) {
              builder.add("operation", "FileWriteString");
              add_key_u64(builder, "handle", value.handle);
              add_key_u64(builder, "source", value.source);
              builder.add("prefix", value.prefix);
              builder.add("suffix", value.suffix);
              add_key_u64(
                  builder, "newline", value.newline ? 1U : 0U);
            },
            [&](const FileReadLine& value) {
              builder.add("operation", "FileReadLine");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "handle", value.handle);
              add_key_u64(builder, "target", value.target);
            },
            [&](const FileEndOfFile& value) {
              builder.add("operation", "FileEndOfFile");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "handle", value.handle);
            },
            [&](const FileErrorStatus& value) {
              builder.add("operation", "FileErrorStatus");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "handle", value.handle);
              add_key_u64(builder, "target", value.target);
            },
            [&](const UnaryNot &value) {
              builder.add("operation", "UnaryNot");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const LogicalNot& value) {
              builder.add("operation", "LogicalNot");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const LogicalBinary& value) {
              builder.add("operation", "LogicalBinary");
              add_key_u64(
                  builder, "logical-binary-operator",
                  static_cast<
                      std::underlying_type_t<LogicalBinaryOperator>>(
                      value.operation));
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "lhs", value.lhs);
              add_key_u64(builder, "rhs", value.rhs);
            },
            [&](const Reduction& value) {
              builder.add("operation", "Reduction");
              add_key_u64(
                  builder, "reduction-operator",
                  static_cast<
                      std::underlying_type_t<ReductionOperator>>(
                      value.operation));
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const CountOnes& value) {
              builder.add("operation", "CountOnes");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const CountBits& value) {
              builder.add("operation", "CountBits");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "state-mask", value.state_mask);
            },
            [&](const Shift& value) {
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
            },
            [&](const Extract& value) {
              builder.add("operation", "Extract");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "offset", value.offset);
              add_key_u64(builder, "width", value.width);
            },
            [&](const DynamicExtract& value) {
              builder.add("operation", "DynamicExtract");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
              add_dynamic_index_key(builder, value.selection);
            },
            [&](const Insert& value) {
              builder.add("operation", "Insert");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "target", value.target);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "offset", value.offset);
            },
            [&](const DynamicInsert& value) {
              builder.add("operation", "DynamicInsert");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "target", value.target);
              add_key_u64(builder, "source", value.source);
              add_dynamic_index_key(builder, value.selection);
            },
            [&](const Concatenate& value) {
              builder.add("operation", "Concatenate");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "width", value.width);
              add_key_u64(
                  builder, "operand-count", value.operands.size());
              for (const auto operand : value.operands) {
                add_key_u64(builder, "operand", operand);
              }
            },
            [&](const Binary &value) {
              builder.add("operation", "Binary");
              add_key_u64(
                  builder, "binary-operator",
                  static_cast<std::underlying_type_t<BinaryOperator>>(
                      value.operation));
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "lhs", value.lhs);
              add_key_u64(builder, "rhs", value.rhs);
            },
            [&](const IntegerUnary& value) {
              builder.add("operation", "IntegerUnary");
              add_key_u64(
                  builder, "integer-unary-operator",
                  static_cast<
                      std::underlying_type_t<IntegerUnaryOperator>>(
                      value.operation));
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const IntegerBinary& value) {
              builder.add("operation", "IntegerBinary");
              add_key_u64(
                  builder, "integer-binary-operator",
                  static_cast<
                      std::underlying_type_t<IntegerBinaryOperator>>(
                      value.operation));
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "lhs", value.lhs);
              add_key_u64(builder, "rhs", value.rhs);
            },
            [&](const IntegerCheck& value) {
              builder.add("operation", "IntegerCheck");
              add_key_u64(builder, "source", value.source);
              add_key_u64(
                  builder, "lower",
                  static_cast<std::uint32_t>(value.lower));
              add_key_u64(
                  builder, "upper",
                  static_cast<std::uint32_t>(value.upper));
            },
            [&](const ConditionalSelect& value) {
              builder.add("operation", "ConditionalSelect");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "condition", value.condition);
              add_key_u64(builder, "when-true", value.when_true);
              add_key_u64(builder, "when-false", value.when_false);
            },
            [&](const WriteBlocking &value) {
              builder.add("operation", "WriteBlocking");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
            },
            [&](const WriteUpdate &value) {
              builder.add("operation", "WriteUpdate");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
            },
            [&](const WriteAfter &value) {
              builder.add("operation", "WriteAfter");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "delay", value.delay);
            },
            [&](const WriteInertial& value) {
              builder.add("operation", "WriteInertial");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "rise-delay", value.delays.rise);
              add_key_u64(builder, "fall-delay", value.delays.fall);
              add_key_u64(
                  builder, "turnoff-delay", value.delays.turnoff);
            },
            [&](const WriteProjected& value) {
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
            },
            [&](const WriteProjectedWaveform& value) {
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
            },
            [&](const WriteBlockingSlice& value) {
              builder.add("operation", "WriteBlockingSlice");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "offset", value.offset);
            },
            [&](const WriteUpdateSlice& value) {
              builder.add("operation", "WriteUpdateSlice");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "offset", value.offset);
            },
            [&](const WriteAfterSlice& value) {
              builder.add("operation", "WriteAfterSlice");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "offset", value.offset);
              add_key_u64(builder, "delay", value.delay);
            },
            [&](const WriteInertialSlice& value) {
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
            },
            [&](const WriteProjectedSlice& value) {
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
            },
            [&](const WriteProjectedWaveformSlice& value) {
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
            },
            [&](const WriteBlockingDynamicSlice& value) {
              builder.add(
                  "operation", "WriteBlockingDynamicSlice");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
              add_dynamic_index_key(builder, value.selection);
            },
            [&](const WriteUpdateDynamicSlice& value) {
              builder.add(
                  "operation", "WriteUpdateDynamicSlice");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
              add_dynamic_index_key(builder, value.selection);
            },
            [&](const WriteAfterDynamicSlice& value) {
              builder.add(
                  "operation", "WriteAfterDynamicSlice");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
              add_dynamic_index_key(builder, value.selection);
              add_key_u64(builder, "delay", value.delay);
            },
            [&](const WriteInertialDynamicSlice& value) {
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
            },
            [&](const WriteProjectedDynamicSlice& value) {
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
            },
            [&](const WriteProjectedWaveformDynamicSlice& value) {
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
            },
            [&](const Assert &value) {
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
            },
            [&](const DebugPoint& value) {
              builder.add("operation", "DebugPoint");
              add_key_u64(
                  builder, "kind",
                  static_cast<std::underlying_type_t<
                      runtime::simir::DebugPointKind>>(value.kind));
              builder.add("source-path", value.source.path);
              add_key_u64(builder, "source-line", value.source.line);
              add_key_u64(builder, "source-column", value.source.column);
            },
            [&](const Display& value) {
              builder.add("operation", "Display");
              builder.add("text", value.text);
              add_key_u64(builder, "newline", value.newline ? 1U : 0U);
              add_key_u64(
                  builder,
                  "postponed",
                  value.postponed ? 1U : 0U);
            },
            [&](const FormatDisplay& value) {
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
            },
            [&](const TimeDisplay& value) {
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
            },
            [&](const StringDisplay& value) {
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
            },
            [&](const MonitorInstall& value) {
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
            },
            [&](const MonitorControl& value) {
              builder.add("operation", "MonitorControl");
              add_key_u64(
                  builder, "enabled", value.enabled ? 1U : 0U);
            },
            [&](const RandomValue& value) {
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
            },
            [&](const Report& value) {
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
            },
            [&](const Jump &value) {
              builder.add("operation", "Jump");
              add_key_u64(builder, "target", value.target);
            },
            [&](const Call& value) {
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
            },
            [&](const Return& value) {
              builder.add("operation", "Return");
              add_key_u64(
                  builder, "stack-pointer", value.stack.pointer);
              add_key_u64(
                  builder, "stack-entries", value.stack.entries);
              add_key_u64(
                  builder, "stack-capacity", value.stack.capacity);
            },
            [&](const Branch &value) {
              builder.add("operation", "Branch");
              add_key_u64(builder, "condition", value.condition);
              add_key_u64(builder, "when-true", value.when_true);
              add_key_u64(builder, "when-false", value.when_false);
              add_key_u64(
                  builder, "unknown-policy",
                  static_cast<
                      std::underlying_type_t<UnknownBranchPolicy>>(
                      value.unknown_policy));
            },
            [&](const WaitFor &value) {
              builder.add("operation", "WaitFor");
              add_key_u64(builder, "delay", value.delay);
            },
            [&](const WaitOn &value) {
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
            },
            [&](const WaitSensitivity &) {
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
            },
            [&](const WaitForever &) {
              builder.add("operation", "WaitForever");
            },
            [&](const Yield &) {
              builder.add("operation", "Yield");
            },
            [&](const Pause &) {
              builder.add("operation", "Pause");
            },
            [&](const Stop &) {
              builder.add("operation", "Stop");
            },
            [&](const Halt &) {
              builder.add("operation", "Halt");
            },
            [&](const auto &) {
              llvm_unreachable(
                  "unsupported operations were rejected before cache keying");
            }},
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

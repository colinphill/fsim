// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_cache_key_internal.hpp"

#include <llvm/Support/ErrorHandling.h>

#include <limits>
#include <type_traits>
#include <variant>

namespace fsim::compiler::llvm_detail {

using runtime::Logic9;
using runtime::simir::Assert;
using runtime::simir::Binary;
using runtime::simir::BinaryOperator;
using runtime::simir::Branch;
using runtime::simir::Call;
using runtime::simir::CallableFramePop;
using runtime::simir::CallableFramePush;
using runtime::simir::ClassAllocate;
using runtime::simir::ClassMethodCall;
using runtime::simir::ClassPropertyRead;
using runtime::simir::ClassPropertyWrite;
using runtime::simir::ClassStaticMethodCall;
using runtime::simir::ClassStaticPropertyRead;
using runtime::simir::ClassStaticPropertyWrite;
using runtime::simir::CodeCoverageHit;
using runtime::simir::CompareStrings;
using runtime::simir::Concatenate;
using runtime::simir::ConcatenateStrings;
using runtime::simir::ConditionalSelect;
using runtime::simir::ConvertToTwoState;
using runtime::simir::CopyRegister;
using runtime::simir::CopyStringRegister;
using runtime::simir::CountBits;
using runtime::simir::CountOnes;
using runtime::simir::CoverageAccess;
using runtime::simir::CoverageControl;
using runtime::simir::CoverageDatabaseControl;
using runtime::simir::CoverageDatabaseControlKind;
using runtime::simir::CoverageQuery;
using runtime::simir::CoverageSample;
using runtime::simir::CoverageSampleTrigger;
using runtime::simir::DebugPoint;
using runtime::simir::DisableBlock;
using runtime::simir::DisableFork;
using runtime::simir::Display;
using runtime::simir::DynamicExtract;
using runtime::simir::DynamicIndex;
using runtime::simir::DynamicInsert;
using runtime::simir::DynamicPartIndex;
using runtime::simir::DynamicPartInsert;
using runtime::simir::DynamicPartSelect;
using runtime::simir::EdgeKind;
using runtime::simir::EventAlias;
using runtime::simir::EventTriggered;
using runtime::simir::ExpressionSizingKind;
using runtime::simir::ExpressionValueDomain;
using runtime::simir::Extract;
using runtime::simir::FileBinaryRead;
using runtime::simir::FileClose;
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
using runtime::simir::Fork;
using runtime::simir::ForkEnd;
using runtime::simir::ForkJoinKind;
using runtime::simir::FormatDisplay;
using runtime::simir::Halt;
using runtime::simir::Insert;
using runtime::simir::InstructionIndex;
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
using runtime::simir::MailboxCreate;
using runtime::simir::MailboxGet;
using runtime::simir::MailboxNum;
using runtime::simir::MailboxPut;
using runtime::simir::MonitorControl;
using runtime::simir::MonitorInstall;
using runtime::simir::MonitorValueKind;
using runtime::simir::Operation;
using runtime::simir::Pause;
using runtime::simir::PlaEvaluate;
using runtime::simir::PlusArgSelect;
using runtime::simir::Process;
using runtime::simir::ProcessAwait;
using runtime::simir::ProcessCompleted;
using runtime::simir::ProcessGetRandState;
using runtime::simir::ProcessKill;
using runtime::simir::ProcessResume;
using runtime::simir::ProcessSelf;
using runtime::simir::ProcessSetRandState;
using runtime::simir::ProcessSrandom;
using runtime::simir::ProcessStatusQuery;
using runtime::simir::ProcessSuspend;
using runtime::simir::RandomDistribution;
using runtime::simir::RandomValue;
using runtime::simir::ReadSignal;
using runtime::simir::ReadSimulationTime;
using runtime::simir::ReadStringObject;
using runtime::simir::Reduction;
using runtime::simir::ReductionOperator;
using runtime::simir::RegisterId;
using runtime::simir::ReleaseSignalSlice;
using runtime::simir::Report;
using runtime::simir::Return;
using runtime::simir::ScopeRandomize;
using runtime::simir::SemaphoreCreate;
using runtime::simir::SemaphoreGet;
using runtime::simir::SemaphorePut;
using runtime::simir::Shift;
using runtime::simir::ShiftOperator;
using runtime::simir::SignalActive;
using runtime::simir::SignalDriving;
using runtime::simir::SignalDrivingValue;
using runtime::simir::SignalEvent;
using runtime::simir::SignalLastActive;
using runtime::simir::SignalLastEvent;
using runtime::simir::SignalLastValue;
using runtime::simir::StochasticQueueOperation;
using runtime::simir::Stop;
using runtime::simir::StrengthRank;
using runtime::simir::StringDisplay;
using runtime::simir::StringIndex;
using runtime::simir::StringLength;
using runtime::simir::StringMethod;
using runtime::simir::StringReplaceByte;
using runtime::simir::StringReport;
using runtime::simir::SystemCommand;
using runtime::simir::TimeDisplay;
using runtime::simir::TimeFormatControl;
using runtime::simir::UnaryNot;
using runtime::simir::UnknownBranchPolicy;
using runtime::simir::ValueKind;
using runtime::simir::VcdControl;
using runtime::simir::VcdControlKind;
using runtime::simir::VhdlAssertApi;
using runtime::simir::VhdlEnvironmentCallPath;
using runtime::simir::VhdlEnvironmentDirectory;
using runtime::simir::VhdlEnvironmentGetCallPath;
using runtime::simir::VhdlEnvironmentGetenv;
using runtime::simir::VhdlEnvironmentTime;
using runtime::simir::VhdlEnvironmentTimeToString;
using runtime::simir::VhdlPslApi;
using runtime::simir::VitalDelay;
using runtime::simir::VitalTimingCheck;
using runtime::simir::WaitFor;
using runtime::simir::WaitForever;
using runtime::simir::WaitFork;
using runtime::simir::WaitOn;
using runtime::simir::WaitOrder;
using runtime::simir::WaitPla;
using runtime::simir::WaitRegion;
using runtime::simir::WaitSensitivity;
using runtime::simir::WriteAfter;
using runtime::simir::WriteAfterDynamicPartSlice;
using runtime::simir::WriteAfterDynamicSlice;
using runtime::simir::WriteAfterSlice;
using runtime::simir::WriteBlocking;
using runtime::simir::WriteBlockingDynamicPartSlice;
using runtime::simir::WriteBlockingDynamicSlice;
using runtime::simir::WriteBlockingSlice;
using runtime::simir::WriteInertial;
using runtime::simir::WriteInertialDynamicPartSlice;
using runtime::simir::WriteInertialDynamicSlice;
using runtime::simir::WriteInertialSlice;
using runtime::simir::WriteProjected;
using runtime::simir::WriteProjectedDynamicSlice;
using runtime::simir::WriteProjectedSlice;
using runtime::simir::WriteProjectedWaveform;
using runtime::simir::WriteProjectedWaveformDynamicSlice;
using runtime::simir::WriteProjectedWaveformSlice;
using runtime::simir::WriteStringObject;
using runtime::simir::WriteUpdate;
using runtime::simir::WriteUpdateDynamicPartSlice;
using runtime::simir::WriteUpdateDynamicSlice;
using runtime::simir::WriteUpdateSlice;
using runtime::simir::Yield;

void add_primary_operation_cache_key(
    CacheKeyBuilder& builder,
    const runtime::simir::Operation& operation,
    const std::span<const std::uint32_t> signal_widths)
{
    fsim::runtime::simir::visit_operation(
        [&](const auto& value) {
            using OperationType = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<OperationType, LoadConstant>) {
                    builder.add("operation", "LoadConstant");
                    add_key_u64(builder, "destination", value.destination);
                    add_packed_value_key(builder, value.value);
                } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
                    builder.add("operation", "ReadSignal");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "signal", value.signal);
                    add_key_u64(
                        builder, "read-kind",
                        static_cast<std::uint8_t>(value.kind));
                    add_key_u64(builder, "history-ticks", value.ticks);
                    add_key_u64(
                        builder, "has-sample-clock", value.clock.has_value());
                    add_key_u64(
                        builder, "sample-clock", value.clock.value_or(0));
                    add_key_u64(
                        builder, "sample-clock-edge",
                        static_cast<std::uint8_t>(value.clock_edge));
                    add_key_u64(
                        builder, "has-sample-gate", value.gate.has_value());
                    add_key_u64(
                        builder, "sample-gate", value.gate.value_or(0));
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
                } else if constexpr (
                    std::is_same_v<OperationType, ConvertToTwoState>) {
                    builder.add("operation", "ConvertToTwoState");
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
                } else if constexpr (
                    std::is_same_v<OperationType, StringReplaceByte>) {
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
                    add_key_u64(builder, "scalar-kind",
                        static_cast<std::uint8_t>(value.scalar_kind));
                    add_key_u64(
                        builder,
                        "use-timeformat-width",
                        value.use_timeformat_width ? 1U : 0U);
                } else if constexpr (std::is_same_v<OperationType, PlusArgSelect>) {
                    builder.add("operation", "PlusArgSelect");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "query", value.query);
                    add_key_u64(
                        builder, "has-selected",
                        value.selected.has_value() ? 1U : 0U);
                    add_key_u64(
                        builder, "selected",
                        value.selected.value_or(0));
                } else if constexpr (std::is_same_v<OperationType, SystemCommand>) {
                    builder.add("operation", "SystemCommand");
                    add_key_u64(
                        builder, "has-command",
                        value.command.has_value() ? 1U : 0U);
                    add_key_u64(
                        builder, "command",
                        value.command.value_or(0));
                    add_key_u64(
                        builder, "has-destination",
                        value.destination.has_value() ? 1U : 0U);
                    add_key_u64(
                        builder, "destination",
                        value.destination.value_or(0));
                } else if constexpr (std::is_same_v<OperationType, VcdControl>) {
                    builder.add("operation", "VcdControl");
                    add_key_u64(
                        builder, "kind",
                        static_cast<std::underlying_type_t<VcdControlKind>>(
                            value.kind));
                    add_key_u64(
                        builder, "has-filename",
                        value.filename.has_value() ? 1U : 0U);
                    add_key_u64(
                        builder, "filename", value.filename.value_or(0));
                    add_key_u64(
                        builder, "has-value",
                        value.value.has_value() ? 1U : 0U);
                    add_key_u64(
                        builder, "value", value.value.value_or(0));
                    builder.add("scope", value.scope);
                    add_key_u64(
                        builder, "selection-count", value.selections.size());
                    for (const auto& selection : value.selections) {
                        builder.add("selection", selection);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType,
                        CoverageDatabaseControl>) {
                    builder.add("operation", "CoverageDatabaseControl");
                    add_key_u64(
                        builder, "kind",
                        static_cast<std::underlying_type_t<
                            CoverageDatabaseControlKind>>(value.kind));
                    add_key_u64(builder, "filename", value.filename);
                } else if constexpr (std::is_same_v<OperationType, runtime::simir::SystemVerilogScalarBinary>) {
                    builder.add("operation", "SystemVerilogScalarBinary");
                    add_key_u64(
                        builder, "scalar-operator",
                        static_cast<std::underlying_type_t<
                            runtime::SystemVerilogScalarBinaryOperator>>(
                            value.operation));
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "lhs", value.lhs);
                    add_key_u64(builder, "rhs", value.rhs);
                    add_key_u64(
                        builder, "lhs-kind",
                        static_cast<std::uint64_t>(value.lhs_kind));
                    add_key_u64(
                        builder, "rhs-kind",
                        static_cast<std::uint64_t>(value.rhs_kind));
                    add_key_u64(
                        builder, "result-kind",
                        static_cast<std::uint64_t>(value.result_kind));
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         runtime::simir::SystemVerilogMath>) {
                    builder.add("operation", "SystemVerilogMath");
                    add_key_u64(
                        builder, "function",
                        static_cast<std::underlying_type_t<
                            runtime::SystemVerilogMathFunction>>(
                            value.function));
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "first", value.first);
                    add_key_u64(builder, "second", value.second);
                    add_key_u64(builder, "first-width", value.first_width);
                    add_key_u64(builder, "second-width", value.second_width);
                    add_key_u64(
                        builder, "first-kind",
                        static_cast<std::uint64_t>(value.first_kind));
                    add_key_u64(
                        builder, "second-kind",
                        static_cast<std::uint64_t>(value.second_kind));
                    add_key_u64(
                        builder, "first-signed",
                        value.first_signed ? 1U : 0U);
                    add_key_u64(
                        builder, "second-signed",
                        value.second_signed ? 1U : 0U);
                    add_key_u64(
                        builder, "time-unit-femtoseconds",
                        value.time_unit_femtoseconds);
                    add_key_u64(
                        builder, "time-precision-femtoseconds",
                        value.time_precision_femtoseconds);
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
                    add_key_u64(
                        builder, "allow-queue",
                        value.allow_queue ? 1U : 0U);
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
                    add_key_u64(
                        builder,
                        "has-transaction-signal",
                        value.transaction_signal.has_value() ? 1U : 0U);
                    if (value.transaction_signal) {
                        add_key_u64(
                            builder,
                            "transaction-signal",
                            *value.transaction_signal);
                    }
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
                        add_packed_value_key(builder, node.constant);
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
                        add_packed_value_key(builder, node.constant);
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
                        add_packed_value_key(builder, node.constant);
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
                        add_packed_value_key(builder, node.constant);
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
                    add_key_u64(
                        builder, "string-index",
                        value.string_index ? 1U : 0U);
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
                    add_key_u64(
                        builder, "string-index",
                        value.string_index ? 1U : 0U);
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         runtime::simir::WriteContainerObjectElement>) {
                    builder.add("operation", "WriteContainerObjectElement");
                    add_key_u64(builder, "object", value.object);
                    add_key_u64(builder, "index", value.index);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(
                        builder, "linear-index",
                        value.linear_index ? 1U : 0U);
                    add_key_u64(
                        builder, "signed-index",
                        value.signed_index ? 1U : 0U);
                    add_key_u64(
                        builder, "nonblocking",
                        value.nonblocking ? 1U : 0U);
                    add_key_u64(
                        builder, "has-transaction-signal",
                        value.transaction_signal.has_value() ? 1U : 0U);
                    if (value.transaction_signal) {
                        add_key_u64(
                            builder, "transaction-signal",
                            *value.transaction_signal);
                    }
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         runtime::simir::ContainerStringRead>) {
                    builder.add("operation", "ContainerStringRead");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "index", value.index);
                    add_key_u64(
                        builder, "linear-index",
                        value.linear_index ? 1U : 0U);
                    add_key_u64(
                        builder, "signed-index",
                        value.signed_index ? 1U : 0U);
                    add_key_u64(
                        builder, "string-index",
                        value.string_index ? 1U : 0U);
                    add_key_u64(builder, "member-count", value.members.size());
                    for (const auto member : value.members) {
                        add_key_u64(builder, "member", member);
                    }
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         runtime::simir::ContainerStringWrite>) {
                    builder.add("operation", "ContainerStringWrite");
                    add_key_u64(builder, "target", value.target);
                    add_key_u64(builder, "index", value.index);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(
                        builder, "linear-index",
                        value.linear_index ? 1U : 0U);
                    add_key_u64(
                        builder, "signed-index",
                        value.signed_index ? 1U : 0U);
                    add_key_u64(
                        builder, "string-index",
                        value.string_index ? 1U : 0U);
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         runtime::simir::ContainerElementRead>) {
                    builder.add("operation", "ContainerElementRead");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "index", value.index);
                    add_key_u64(
                        builder, "signed-index",
                        value.signed_index ? 1U : 0U);
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         runtime::simir::ContainerElementWrite>) {
                    builder.add("operation", "ContainerElementWrite");
                    add_key_u64(builder, "target", value.target);
                    add_key_u64(builder, "index", value.index);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(
                        builder, "signed-index",
                        value.signed_index ? 1U : 0U);
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         runtime::simir::ContainerAggregateRead>) {
                    builder.add("operation", "ContainerAggregateRead");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "index", value.index);
                    add_key_u64(builder, "member-count", value.members.size());
                    for (const auto member : value.members) {
                        add_key_u64(builder, "member", member);
                    }
                    add_key_u64(
                        builder, "linear-index",
                        value.linear_index ? 1U : 0U);
                    add_key_u64(
                        builder, "signed-index",
                        value.signed_index ? 1U : 0U);
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         runtime::simir::ContainerAggregateWrite>) {
                    builder.add("operation", "ContainerAggregateWrite");
                    add_key_u64(builder, "target", value.target);
                    add_key_u64(builder, "index", value.index);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "member-count", value.members.size());
                    for (const auto member : value.members) {
                        add_key_u64(builder, "member", member);
                    }
                    add_key_u64(
                        builder, "linear-index",
                        value.linear_index ? 1U : 0U);
                    add_key_u64(
                        builder, "signed-index",
                        value.signed_index ? 1U : 0U);
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         runtime::simir::CopyContainerAggregateElement>) {
                    builder.add("operation", "CopyContainerAggregateElement");
                    add_key_u64(builder, "target", value.target);
                    add_key_u64(builder, "target-index", value.target_index);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "source-index", value.source_index);
                    add_key_u64(
                        builder, "target-signed-index",
                        value.target_signed_index ? 1U : 0U);
                    add_key_u64(
                        builder, "source-signed-index",
                        value.source_signed_index ? 1U : 0U);
                } else if constexpr (std::is_same_v<OperationType, runtime::simir::DeleteContainer>) {
                    builder.add("operation", "DeleteContainer");
                    add_key_u64(builder, "target", value.target);
                    add_key_u64(
                        builder, "has-index", value.index ? 1U : 0U);
                    add_key_u64(
                        builder, "index", value.index.value_or(0));
                    add_key_u64(
                        builder, "string-index",
                        value.string_index ? 1U : 0U);
                } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerExists>) {
                    builder.add("operation", "ContainerExists");
                    add_key_u64(
                        builder, "destination", value.destination);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "index", value.index);
                    add_key_u64(
                        builder, "string-index",
                        value.string_index ? 1U : 0U);
                } else if constexpr (std::is_same_v<OperationType, runtime::simir::TraverseContainer>) {
                    builder.add("operation", "TraverseContainer");
                    add_key_u64(
                        builder, "destination", value.destination);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "index", value.index);
                    add_key_u64(
                        builder, "string-index",
                        value.string_index ? 1U : 0U);
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
                    add_key_u64(builder, "scalar-kind",
                        static_cast<std::uint8_t>(value.scalar_kind));
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
                    add_key_u64(
                        builder, "target-kind",
                        static_cast<std::uint8_t>(value.target_kind));
                    add_key_u64(builder, "target-width", value.target_width);
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
                    add_key_u64(
                        builder, "target-kind",
                        static_cast<std::uint8_t>(value.target_kind));
                    add_key_u64(builder, "target-width", value.target_width);
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
                        add_key_u64(builder, "scan-target-scalar-kind",
                            static_cast<std::uint8_t>(
                                conversion.target.scalar_kind));
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
                    add_key_u64(builder, "scalar-kind",
                        static_cast<std::uint8_t>(value.scalar_kind));
                } else if constexpr (std::is_same_v<OperationType, FilePosition>) {
                    builder.add("operation", "FilePosition");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "handle", value.handle);
                    add_key_u64(builder, "offset", value.offset);
                    add_key_u64(builder, "origin", value.origin);
                    add_key_u64(builder, "kind",
                        static_cast<std::uint8_t>(value.kind));
                }
        },
        operation);
}

} // namespace fsim::compiler::llvm_detail

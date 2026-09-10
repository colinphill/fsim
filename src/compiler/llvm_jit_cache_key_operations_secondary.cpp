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

void add_secondary_operation_cache_key(
    CacheKeyBuilder& builder,
    const runtime::simir::Process& process,
    const runtime::simir::Operation& operation,
    const std::span<const std::uint32_t> signal_widths)
{
    fsim::runtime::simir::visit_operation(
        [&](const auto& value) {
            using OperationType = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<OperationType, FileFlush>) {
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
                        static_cast<std::uint64_t>(value.lower));
                    add_key_u64(
                        builder, "upper",
                        static_cast<std::uint64_t>(value.upper));
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
                    add_key_u64(builder, "dynamic", value.selection.has_value());
                    add_key_u64(builder, "driving-value", value.driving_value);
                    if (value.selection) {
                        add_dynamic_index_key(builder, *value.selection);
                    }
                } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
                    builder.add("operation", "ReleaseSignalSlice");
                    add_key_u64(builder, "signal", value.signal);
                    add_key_u64(
                        builder, "signal-width", signal_widths[value.signal]);
                    add_key_u64(builder, "offset", value.offset);
                    add_key_u64(builder, "width", value.width);
                    add_key_u64(builder, "dynamic", value.selection.has_value());
                    add_key_u64(builder, "driving-value", value.driving_value);
                    if (value.selection) {
                        add_dynamic_index_key(builder, *value.selection);
                    }
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
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         WriteInertialDynamicPartSlice>) {
                    builder.add(
                        "operation", "WriteInertialDynamicPartSlice");
                    add_key_u64(builder, "signal", value.signal);
                    add_key_u64(
                        builder, "signal-width", signal_widths[value.signal]);
                    add_key_u64(builder, "source", value.source);
                    add_dynamic_part_index_key(builder, value.selection);
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
                    add_key_u64(builder, "scalar-kind",
                        static_cast<std::uint8_t>(value.scalar_kind));
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
                    add_key_u64(
                        builder,
                        "use-timeformat-width",
                        value.use_timeformat_width ? 1U : 0U);
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
                        const auto key = "value-" + std::to_string(index) + "-";
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
                        add_key_u64(builder, key + "scalar-kind",
                            static_cast<std::uint8_t>(item.scalar_kind));
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
                        add_key_u64(
                            builder,
                            key + "use-timeformat-width",
                            item.use_timeformat_width ? 1U : 0U);
                    }
                    builder.add("trailing-text", value.trailing_text);
                    add_key_u64(
                        builder,
                        "file-handle-present",
                        value.file_handle ? 1U : 0U);
                    if (value.file_handle) {
                        add_key_u64(
                            builder, "file-handle", *value.file_handle);
                    }
                    add_key_u64(
                        builder, "newline", value.newline ? 1U : 0U);
                    add_key_u64(
                        builder, "one-shot", value.one_shot ? 1U : 0U);
                } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
                    builder.add("operation", "MonitorControl");
                    add_key_u64(
                        builder, "enabled", value.enabled ? 1U : 0U);
                } else if constexpr (
                    std::is_same_v<OperationType, TimeFormatControl>) {
                    builder.add("operation", "TimeFormatControl");
                    add_key_u64(builder, "units", value.units);
                    add_key_u64(builder, "precision", value.precision);
                    add_key_u64(builder, "suffix", value.suffix);
                    add_key_u64(
                        builder, "minimum-width", value.minimum_width);
                } else if constexpr (std::is_same_v<OperationType, CoverageSample>) {
                    builder.add("operation", "CoverageSample");
                    builder.add("coverage-instance", value.instance_identity);
                    add_key_u64(
                        builder, "coverage-actual-count", value.actuals.size());
                    add_key_u64(
                        builder, "coverage-trigger",
                        static_cast<std::underlying_type_t<CoverageSampleTrigger>>(
                            value.trigger));
                    for (std::size_t actual = 0;
                        actual < value.actuals.size(); ++actual) {
                        const auto key = "coverage-actual-"
                            + std::to_string(actual) + "-";
                        add_key_u64(
                            builder, key + "register", value.actuals[actual]);
                        add_key_u64(
                            builder, key + "width", value.actual_widths[actual]);
                        add_key_u64(
                            builder, key + "signed", value.signed_actuals[actual]);
                        add_key_u64(
                            builder, key + "scalar-kind",
                            static_cast<std::uint64_t>(value.scalar_kinds[actual]));
                    }
                } else if constexpr (std::is_same_v<OperationType, CoverageQuery>) {
                    builder.add("operation", "CoverageQuery");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(
                        builder, "kind",
                        static_cast<std::uint64_t>(value.kind));
                } else if constexpr (std::is_same_v<OperationType, VhdlPslApi>) {
                    builder.add("operation", "VhdlPslApi");
                    add_key_u64(builder, "kind",
                        static_cast<std::uint64_t>(value.kind));
                    add_key_u64(builder, "has-destination",
                        value.destination.has_value() ? 1U : 0U);
                    if (value.destination) {
                        add_key_u64(builder, "destination", *value.destination);
                    }
                    add_key_u64(builder, "has-enable",
                        value.enable.has_value() ? 1U : 0U);
                    if (value.enable) {
                        add_key_u64(builder, "enable", *value.enable);
                    }
                } else if constexpr (std::is_same_v<OperationType, VhdlAssertApi>) {
                    builder.add("operation", "VhdlAssertApi");
                    add_key_u64(builder, "kind",
                        static_cast<std::uint64_t>(value.kind));
                    const auto optional_register = [&](const std::string_view name,
                                                       const auto& operand) {
                        add_key_u64(builder, std::string { "has-" } + std::string { name },
                            operand.has_value() ? 1U : 0U);
                        if (operand) {
                            add_key_u64(builder, name, *operand);
                        }
                    };
                    optional_register("destination", value.destination);
                    optional_register("string-destination", value.string_destination);
                    optional_register("level", value.level);
                    optional_register("enable", value.enable);
                    optional_register("format", value.format);
                    optional_register("valid", value.valid);
                    builder.add("source-path", value.source.path);
                    add_key_u64(builder, "source-line", value.source.line);
                    add_key_u64(builder, "source-column", value.source.column);
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageControl>) {
                    builder.add("operation", "CoverageControl");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "command", value.command);
                    add_key_u64(
                        builder, "coverage-type", value.coverage_type);
                    add_key_u64(builder, "scope", value.scope);
                    add_key_u64(builder, "selector", value.selector);
                    builder.add("instance-context", value.instance_context);
                    add_key_u64(builder, "selector-is-instance",
                        value.selector_is_instance ? 1U : 0U);
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageAccess>) {
                    builder.add("operation", "CoverageAccess");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "kind",
                        static_cast<std::uint64_t>(value.kind));
                    add_key_u64(
                        builder, "coverage-type", value.coverage_type);
                    add_key_u64(builder, "has-scope",
                        value.scope.has_value() ? 1U : 0U);
                    if (value.scope) {
                        add_key_u64(builder, "scope", *value.scope);
                    }
                    add_key_u64(builder, "has-selector",
                        value.selector.has_value() ? 1U : 0U);
                    if (value.selector) {
                        add_key_u64(builder, "selector", *value.selector);
                    }
                    builder.add("instance-context", value.instance_context);
                    add_key_u64(builder, "selector-is-instance",
                        value.selector_is_instance ? 1U : 0U);
                    add_key_u64(builder, "has-filename",
                        value.filename.has_value() ? 1U : 0U);
                    if (value.filename) {
                        add_key_u64(builder, "filename", *value.filename);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, CodeCoverageHit>) {
                    builder.add("operation", "CodeCoverageHit");
                    add_key_u64(builder, "point-high", value.point.high);
                    add_key_u64(builder, "point-low", value.point.low);
                    add_key_u64(
                        builder, "metric",
                        static_cast<std::uint64_t>(value.metric));
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
                } else if constexpr (
                    std::is_same_v<OperationType, RandomDistribution>) {
                    builder.add("operation", "RandomDistribution");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "seed", value.seed);
                    add_key_u64(
                        builder,
                        "kind",
                        static_cast<std::underlying_type_t<
                            runtime::simir::RandomDistributionKind>>(
                            value.kind));
                    add_key_u64(builder, "first", value.first);
                    add_key_u64(
                        builder,
                        "has-second",
                        value.second.has_value() ? 1U : 0U);
                    if (value.second)
                        add_key_u64(builder, "second", *value.second);
                } else if constexpr (
                    std::is_same_v<OperationType, VhdlEnvironmentTime>) {
                    builder.add("operation", "VhdlEnvironmentTime");
                    add_key_u64(builder, "kind",
                        static_cast<std::uint8_t>(value.kind));
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "has-first", value.first ? 1U : 0U);
                    if (value.first)
                        add_key_u64(builder, "first", *value.first);
                    add_key_u64(builder, "has-second", value.second ? 1U : 0U);
                    if (value.second)
                        add_key_u64(builder, "second", *value.second);
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentTimeToString>) {
                    builder.add("operation", "VhdlEnvironmentTimeToString");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "record", value.record);
                    add_key_u64(builder, "fractional-digits",
                        value.fractional_digits);
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentDirectory>) {
                    builder.add("operation", "VhdlEnvironmentDirectory");
                    add_key_u64(builder, "kind",
                        static_cast<std::uint8_t>(value.kind));
                    const auto optional_id = [&](const std::string_view name,
                                                 const auto id) {
                        add_key_u64(builder, std::string { "has-" } + std::string { name },
                            id.has_value() ? 1U : 0U);
                        if (id) {
                            add_key_u64(builder, name, *id);
                        }
                    };
                    optional_id("result", value.result);
                    optional_id("string-result", value.string_result);
                    optional_id("directory", value.directory);
                    optional_id("path", value.path);
                    optional_id("option", value.option);
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentGetenv>) {
                    builder.add("operation", "VhdlEnvironmentGetenv");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "name", value.name);
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentCallPath>) {
                    builder.add("operation", "VhdlEnvironmentCallPath");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "separator", value.separator);
                    builder.add("source-value-present",
                        value.source_value ? "1" : "0");
                    if (value.source_value) {
                        add_key_u64(
                            builder, "source-value", *value.source_value);
                    }
                    builder.add("source-index-present",
                        value.source_index ? "1" : "0");
                    if (value.source_index) {
                        add_key_u64(
                            builder, "source-index", *value.source_index);
                    }
                    builder.add("source-file", value.source.path.str());
                    add_key_u64(builder, "source-line", value.source.line);
                    add_key_u64(builder, "source-column", value.source.column);
                    builder.add("scope", value.scope.str());
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentGetCallPath>) {
                    builder.add("operation", "VhdlEnvironmentGetCallPath");
                    add_key_u64(builder, "destination", value.destination);
                    builder.add("source-file", value.source.path.str());
                    add_key_u64(builder, "source-line", value.source.line);
                    add_key_u64(builder, "source-column", value.source.column);
                    builder.add("scope", value.scope.str());
                } else if constexpr (
                    std::is_same_v<OperationType, StochasticQueueOperation>) {
                    builder.add("operation", "StochasticQueueOperation");
                    add_key_u64(builder, "kind", static_cast<std::uint8_t>(value.kind));
                    add_key_u64(builder, "queue-id", value.queue_id);
                    const auto add_optional = [&](const std::string_view name,
                                                  const auto operand) {
                        add_key_u64(
                            builder,
                            std::string { "has-" } + std::string { name },
                            operand.has_value() ? 1U : 0U);
                        add_key_u64(builder, name, operand.value_or(0));
                    };
                    add_optional("queue-type", value.queue_type);
                    add_optional("maximum-length", value.maximum_length);
                    add_optional("job-id", value.job_id);
                    add_optional("information-id", value.information_id);
                    add_optional("statistic-code", value.statistic_code);
                    add_optional("statistic-value", value.statistic_value);
                    add_key_u64(builder, "status", value.status);
                    add_optional("result", value.result);
                } else if constexpr (std::is_same_v<OperationType, PlaEvaluate>) {
                    builder.add("operation", "PlaEvaluate");
                    add_key_u64(builder, "memory", value.memory);
                    add_key_u64(builder, "input", value.input);
                    add_key_u64(builder, "output", value.output);
                    add_key_u64(builder, "input-width", value.input_width);
                    add_key_u64(builder, "output-width", value.output_width);
                    add_key_u64(
                        builder, "logic", static_cast<std::uint8_t>(value.logic));
                    add_key_u64(builder, "plane", value.plane);
                } else if constexpr (std::is_same_v<OperationType, ScopeRandomize>) {
                    builder.add("operation", "ScopeRandomize");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(
                        builder, "maximum-domain-values",
                        value.maximum_domain_values);
                    add_key_u64(builder, "target-count", value.targets.size());
                    for (std::size_t index = 0; index < value.targets.size(); ++index) {
                        const auto& target = value.targets[index];
                        const auto key = "target-" + std::to_string(index) + "-";
                        add_key_u64(builder, key + "register", target.target);
                        builder.add(key + "identity", target.canonical_identity);
                        add_key_u64(builder, key + "width", target.width);
                        add_key_u64(
                            builder, key + "signed", target.signed_value ? 1U : 0U);
                        add_key_u64(
                            builder, key + "domain-kind",
                            static_cast<std::underlying_type_t<
                                runtime::simir::ScopeRandomizeDomainKind>>(
                                target.domain_kind));
                        builder.add(key + "nominal-type", target.nominal_type);
                        add_key_u64(builder, key + "domain-size", target.domain.size());
                        for (std::size_t value_index = 0;
                            value_index < target.domain.size(); ++value_index) {
                            builder.add(
                                key + "domain-" + std::to_string(value_index),
                                target.domain[value_index].to_msb_string());
                        }
                    }
                    add_key_u64(
                        builder,
                        "inline-constraint-count",
                        value.inline_constraints.size());
                    for (std::size_t index = 0;
                        index < value.inline_constraints.size(); ++index) {
                        add_constraint_template_key(
                            builder,
                            value.inline_constraints[index],
                            "inline-constraint-" + std::to_string(index) + "-");
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
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePush>) {
                    builder.add("operation", "CallableFramePush");
                    add_key_u64(builder, "identity", value.identity);
                    add_key_u64(builder, "packed-count", value.packed.size());
                    for (const auto id : value.packed) {
                        add_key_u64(builder, "packed", id);
                    }
                    add_key_u64(builder, "string-count", value.strings.size());
                    for (const auto id : value.strings) {
                        add_key_u64(builder, "string", id);
                    }
                    add_key_u64(
                        builder, "container-count", value.containers.size());
                    for (const auto id : value.containers) {
                        add_key_u64(builder, "container", id);
                    }
                    add_key_u64(
                        builder, "native-isolated",
                        value.native_isolated ? 1U : 0U);
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePop>) {
                    builder.add("operation", "CallableFramePop");
                    add_key_u64(builder, "identity", value.identity);
                    add_key_u64(
                        builder, "preserve-packed-count",
                        value.preserve_packed.size());
                    for (const auto id : value.preserve_packed) {
                        add_key_u64(builder, "preserve-packed", id);
                    }
                    add_key_u64(
                        builder, "preserve-string-count",
                        value.preserve_strings.size());
                    for (const auto id : value.preserve_strings) {
                        add_key_u64(builder, "preserve-string", id);
                    }
                    add_key_u64(
                        builder, "preserve-container-count",
                        value.preserve_containers.size());
                    for (const auto id : value.preserve_containers) {
                        add_key_u64(builder, "preserve-container", id);
                    }
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
                } else if constexpr (std::is_same_v<OperationType, WaitRegion>) {
                    builder.add("operation", "WaitRegion");
                    add_key_u64(
                        builder, "wait-region-phase",
                        static_cast<std::underlying_type_t<
                            runtime::SchedulerPhase>>(value.phase));
                } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
                    builder.add("operation", "WaitFor");
                    add_key_u64(builder, "delay", value.delay);
                    add_key_u64(
                        builder, "dynamic-source",
                        value.source.value_or(
                            std::numeric_limits<RegisterId>::max()));
                    add_key_u64(builder, "dynamic-source-width", value.source_width);
                    add_key_u64(
                        builder, "dynamic-source-kind",
                        static_cast<std::uint64_t>(value.source_kind));
                    add_key_u64(builder, "dynamic-source-signed", value.source_signed);
                    add_key_u64(
                        builder, "dynamic-rounding-quantum",
                        value.rounding_quantum);
                } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
                    builder.add("operation", "WaitOn");
                    add_key_u64(
                        builder, "wait-on-signal-count", value.signals.size());
                    for (std::size_t index = 0;
                        index < value.signals.size(); ++index) {
                        const auto signal = value.signals[index];
                        const auto edge = value.edges.empty()
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
                } else if constexpr (std::is_same_v<OperationType, WaitPla>) {
                    builder.add("operation", "WaitPla");
                    add_key_u64(builder, "memory", value.memory);
                    add_key_u64(builder, "signal-count", value.signals.size());
                    for (const auto signal : value.signals) {
                        add_key_u64(builder, "signal", signal);
                        add_key_u64(builder, "signal-width", signal_widths[signal]);
                    }
                } else if constexpr (std::is_same_v<OperationType, WaitOrder>) {
                    builder.add("operation", "WaitOrder");
                    add_key_u64(
                        builder, "wait-order-event-count", value.events.size());
                    for (const auto event : value.events) {
                        add_key_u64(builder, "wait-order-event", event);
                        add_key_u64(
                            builder,
                            "wait-order-event-width",
                            signal_widths[event]);
                    }
                    add_key_u64(builder, "wait-order-result", value.result);
                } else if constexpr (std::is_same_v<OperationType, EventTriggered>) {
                    builder.add("operation", "EventTriggered");
                    add_key_u64(builder, "event-triggered-event", value.event);
                    add_key_u64(
                        builder,
                        "event-triggered-event-width",
                        signal_widths[value.event]);
                    add_key_u64(
                        builder,
                        "event-triggered-destination",
                        value.destination);
                } else if constexpr (std::is_same_v<OperationType, EventAlias>) {
                    builder.add("operation", "EventAlias");
                    add_key_u64(builder, "event-alias-target", value.target);
                    add_key_u64(
                        builder, "event-alias-has-source",
                        value.has_source);
                    if (value.has_source) {
                        add_key_u64(
                            builder, "event-alias-source", value.source);
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
                    for (const auto kind : value.constructor_actual_kinds) {
                        add_key_u64(builder, "class-actual-kind", kind);
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
                            ? "ClassMethodCall"
                            : "ClassStaticMethodCall");
                    add_key_u64(builder, "class-destination", value.destination);
                    if constexpr (std::is_same_v<OperationType, ClassMethodCall>) {
                        add_key_u64(builder, "class-receiver", value.receiver);
                        add_key_u64(
                            builder, "class-virtual", value.virtual_dispatch ? 1U : 0U);
                        add_key_u64(
                            builder,
                            "inline-constraint-count",
                            value.inline_constraints.size());
                        for (std::size_t index = 0;
                            index < value.inline_constraints.size(); ++index) {
                            add_constraint_template_key(
                                builder,
                                value.inline_constraints[index],
                                "inline-constraint-" + std::to_string(index) + "-");
                        }
                    }
                    builder.add("class-method", value.method_identity);
                    add_key_u64(builder, "class-width", value.result_width);
                    add_key_u64(
                        builder, "class-actual-count", value.actuals.size());
                    for (std::size_t actual = 0;
                        actual < value.actuals.size(); ++actual) {
                        add_key_u64(builder, "class-actual", value.actuals[actual]);
                        add_key_u64(
                            builder, "class-actual-kind",
                            value.actual_kinds.empty()
                                ? 0U
                                : value.actual_kinds[actual]);
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
                    add_key_u64(
                        builder, "fork-site-present", value.site.has_value());
                    if (value.site) {
                        add_key_u64(builder, "fork-site", *value.site);
                    }
                } else if constexpr (std::is_same_v<OperationType, DisableBlock>) {
                    builder.add("operation", "DisableBlock");
                    add_key_u64(builder, "block-begin", value.begin);
                    add_key_u64(builder, "block-end", value.end);
                } else if constexpr (std::is_same_v<OperationType, ProcessSelf>) {
                    builder.add("operation", "ProcessSelf");
                    add_key_u64(
                        builder, "destination", value.destination);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessStatusQuery>) {
                    builder.add("operation", "ProcessStatusQuery");
                    add_key_u64(
                        builder, "destination", value.destination);
                    add_key_u64(builder, "source", value.source);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessCompleted>) {
                    builder.add("operation", "ProcessCompleted");
                    add_key_u64(
                        builder, "destination", value.destination);
                    add_key_u64(builder, "source", value.source);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessAwait>) {
                    builder.add("operation", "ProcessAwait");
                    add_key_u64(builder, "source", value.source);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessKill>) {
                    builder.add("operation", "ProcessKill");
                    add_key_u64(builder, "source", value.source);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessSuspend>) {
                    builder.add("operation", "ProcessSuspend");
                    add_key_u64(builder, "source", value.source);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessResume>) {
                    builder.add("operation", "ProcessResume");
                    add_key_u64(builder, "source", value.source);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessGetRandState>) {
                    builder.add("operation", "ProcessGetRandState");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "source", value.source);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessSetRandState>) {
                    builder.add("operation", "ProcessSetRandState");
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "state", value.state);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessSrandom>) {
                    builder.add("operation", "ProcessSrandom");
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "seed", value.seed);
                } else if constexpr (
                    std::is_same_v<OperationType, MailboxCreate>) {
                    builder.add("operation", "MailboxCreate");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "capacity", value.capacity);
                    add_key_u64(builder, "element-width", value.element_width);
                } else if constexpr (
                    std::is_same_v<OperationType, MailboxPut>) {
                    builder.add("operation", "MailboxPut");
                    add_key_u64(builder, "receiver", value.receiver);
                    add_key_u64(builder, "source", value.source);
                    add_key_u64(builder, "element-width", value.element_width);
                    add_key_u64(
                        builder, "result",
                        value.result.value_or(
                            std::numeric_limits<RegisterId>::max()));
                } else if constexpr (
                    std::is_same_v<OperationType, MailboxGet>) {
                    builder.add("operation", "MailboxGet");
                    add_key_u64(builder, "receiver", value.receiver);
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "element-width", value.element_width);
                    add_key_u64(
                        builder, "result",
                        value.result.value_or(
                            std::numeric_limits<RegisterId>::max()));
                    add_key_u64(builder, "peek", value.peek ? 1U : 0U);
                } else if constexpr (
                    std::is_same_v<OperationType, MailboxNum>) {
                    builder.add("operation", "MailboxNum");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "receiver", value.receiver);
                } else if constexpr (
                    std::is_same_v<OperationType, SemaphoreCreate>) {
                    builder.add("operation", "SemaphoreCreate");
                    add_key_u64(builder, "destination", value.destination);
                    add_key_u64(builder, "keys", value.keys);
                } else if constexpr (
                    std::is_same_v<OperationType, SemaphoreGet>) {
                    builder.add("operation", "SemaphoreGet");
                    add_key_u64(builder, "receiver", value.receiver);
                    add_key_u64(builder, "keys", value.keys);
                    add_key_u64(
                        builder, "result",
                        value.result.value_or(
                            std::numeric_limits<RegisterId>::max()));
                } else if constexpr (
                    std::is_same_v<OperationType, SemaphorePut>) {
                    builder.add("operation", "SemaphorePut");
                    add_key_u64(builder, "receiver", value.receiver);
                    add_key_u64(builder, "keys", value.keys);
                } else if constexpr (
                    std::is_same_v<
                        OperationType, runtime::simir::VhdlReflectionApi>) {
                    builder.add("operation", "VhdlReflectionApi");
                    add_key_u64(
                        builder, "kind", static_cast<std::uint8_t>(value.kind));
                    const auto add_optional_register =
                        [&](const std::string_view name, const auto& operand) {
                            builder.add(
                                std::string { name } + "-present",
                                operand.has_value() ? "1" : "0");
                            if (operand) {
                                add_key_u64(builder, name, *operand);
                            }
                        };
                    add_optional_register("destination", value.destination);
                    add_optional_register(
                        "string-destination", value.string_destination);
                    add_optional_register("receiver", value.receiver);
                    add_optional_register("source", value.source);
                    add_optional_register("access-heap", value.access_heap);
                    add_key_u64(builder, "argument-count", value.arguments.size());
                    for (const auto argument : value.arguments) {
                        add_key_u64(builder, "argument", argument);
                    }
                    add_optional_register("string-argument", value.string_argument);
                    add_key_u64(builder, "result-width", value.result_width);
                    const auto add_type = [&](const auto& self,
                                              const runtime::simir::VhdlReflectionType& type)
                        -> void {
                        add_key_u64(builder, "type-class",
                            static_cast<std::uint8_t>(type.type_class));
                        builder.add("type-name", type.simple_name);
                        add_key_u64(builder, "type-width", type.packed_width);
                        add_key_u64(builder, "type-signed", type.signed_value);
                        add_key_u64(builder, "type-offset", type.lsb_offset);
                        add_key_u64(builder, "range-count", type.ranges.size());
                        for (const auto& range : type.ranges) {
                            add_key_u64(builder, "range-left",
                                static_cast<std::uint64_t>(range.left));
                            add_key_u64(builder, "range-right",
                                static_cast<std::uint64_t>(range.right));
                            add_key_u64(builder, "range-ascending", range.ascending);
                        }
                        add_key_u64(builder, "name-count", type.names.size());
                        for (const auto& name : type.names) {
                            builder.add("type-member-name", name);
                        }
                        add_key_u64(builder, "scale-count", type.scales.size());
                        for (const auto scale : type.scales) {
                            add_key_u64(builder, "type-scale", scale);
                        }
                        add_key_u64(builder, "child-count", type.children.size());
                        for (const auto& child : type.children) {
                            self(self, child);
                        }
                    };
                    add_type(add_type, value.type);
                    builder.add("source-path", value.source_location.path);
                    add_key_u64(builder, "source-line", value.source_location.line);
                    add_key_u64(
                        builder, "source-column", value.source_location.column);
                } else if constexpr (std::is_same_v<OperationType, Pause>) {
                    builder.add("operation", "Pause");
                    add_key_u64(
                        builder, "status",
                        value.status.value_or(
                            std::numeric_limits<RegisterId>::max()));
                } else if constexpr (std::is_same_v<OperationType, Stop>) {
                    builder.add("operation", "Stop");
                    add_key_u64(
                        builder, "status",
                        value.status.value_or(
                            std::numeric_limits<RegisterId>::max()));
                } else if constexpr (std::is_same_v<OperationType, Halt>) {
                    builder.add("operation", "Halt");
                    add_key_u64(
                        builder, "program-exit", value.program_exit ? 1U : 0U);
                } else if constexpr (
                    std::is_same_v<OperationType, LoadConstant>
                    || std::is_same_v<OperationType, CopyRegister>
                    || std::is_same_v<OperationType, ConvertToTwoState>
                    || std::is_same_v<OperationType, ReadSignal>
                    || std::is_same_v<OperationType, SignalEvent>
                    || std::is_same_v<OperationType, SignalLastValue>
                    || std::is_same_v<OperationType, SignalLastEvent>
                    || std::is_same_v<OperationType, ReadSimulationTime>
                    || std::is_same_v<OperationType, VitalTimingCheck>
                    || std::is_same_v<OperationType, VitalDelay>
                    || std::is_same_v<OperationType, SignalActive>
                    || std::is_same_v<OperationType, SignalLastActive>
                    || std::is_same_v<OperationType, SignalDriving>
                    || std::is_same_v<OperationType, SignalDrivingValue>
                    || runtime::simir::operation_group_contains_v<
                        OperationType, runtime::simir::StringOperationGroup>
                    || runtime::simir::operation_group_contains_v<
                        OperationType, runtime::simir::ContainerOperationGroup>
                    || runtime::simir::operation_group_contains_v<
                        OperationType, runtime::simir::FileOperationGroup>) {
                    // These alternatives were handled by the first constexpr chain.
                } else {
                    llvm_unreachable(
                        "unsupported operations were rejected before cache keying");
                }
        },
        operation);
}

} // namespace fsim::compiler::llvm_detail

// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_cache_key_internal.hpp"

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

constexpr std::string_view kNativeObjectCacheSchema = "fsim-llvm-native-object-v168";

void add_key_u64(CacheKeyBuilder& builder, const std::string_view label,
    const std::uint64_t value)
{
    std::array<std::byte, sizeof(value)> encoded { };
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        encoded[index] = static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
    }
    builder.add_bytes(label, encoded);
}

void add_packed_value_key(
    CacheKeyBuilder& builder, const runtime::PackedLogic4& value)
{
    add_key_u64(builder, "packed-width", value.width());
    add_key_u64(builder, "packed-logic9", value.is_logic9() ? 1U : 0U);
    add_key_u64(builder, "packed-word-count", value.aval_words().size());
    for (const auto word : value.aval_words()) {
        add_key_u64(builder, "packed-aval", word);
    }
    for (const auto word : value.bval_words()) {
        add_key_u64(builder, "packed-bval", word);
    }
    if (value.is_logic9()) {
        builder.add("packed-logic9-state", value.to_msb_string());
    }
}

void add_constraint_template_key(
    CacheKeyBuilder& builder,
    const runtime::SystemVerilogConstraintTemplate& expression,
    const std::string& prefix)
{
    add_key_u64(
        builder,
        prefix + "kind",
        static_cast<std::underlying_type_t<
            runtime::SystemVerilogConstraintTemplateKind>>(
            expression.kind));
    builder.add(prefix + "text", expression.text);
    add_key_u64(builder, prefix + "profile-kind",
        static_cast<std::underlying_type_t<
            runtime::SystemVerilogConstraintDomainKind>>(
            expression.profile.kind));
    add_key_u64(builder, prefix + "profile-width", expression.profile.width);
    add_key_u64(
        builder, prefix + "profile-signed",
        expression.profile.signed_value ? 1U : 0U);
    add_key_u64(
        builder, prefix + "profile-four-state",
        expression.profile.four_state ? 1U : 0U);
    builder.add(prefix + "profile-nominal", expression.profile.nominal_type);
    add_packed_value_key(builder, expression.constant);
    add_key_u64(builder, prefix + "operand-count", expression.operands.size());
    for (std::size_t index = 0; index < expression.operands.size(); ++index) {
        add_constraint_template_key(
            builder,
            expression.operands[index],
            prefix + "operand-" + std::to_string(index) + "-");
    }
}

void add_dynamic_index_key(
    CacheKeyBuilder& builder,
    const DynamicIndex& selection)
{
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
    add_key_u64(
        builder, "index-strict", selection.strict ? 1U : 0U);
}

void add_dynamic_part_index_key(
    CacheKeyBuilder& builder,
    const DynamicPartIndex& selection)
{
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

void add_container_type_key(
    CacheKeyBuilder& builder,
    const runtime::simir::ContainerType& type)
{
    add_key_u64(
        builder, "container-element-kind",
        static_cast<std::underlying_type_t<
            runtime::simir::ContainerElementKind>>(type.element_kind));
    add_key_u64(
        builder, "container-scalar-kind",
        static_cast<std::uint64_t>(type.scalar_kind));
    add_key_u64(builder, "container-element-width", type.element_width);
    builder.add("container-element-nominal-type", type.element_nominal_type);
    add_key_u64(builder, "container-two-state", type.two_state ? 1U : 0U);
    add_key_u64(
        builder, "container-signed", type.signed_elements ? 1U : 0U);
    add_key_u64(
        builder, "container-union-aggregate",
        type.union_aggregate ? 1U : 0U);
    add_key_u64(
        builder, "container-aggregate-value",
        type.aggregate_value ? 1U : 0U);
    add_key_u64(builder, "container-queue", type.queue ? 1U : 0U);
    add_key_u64(
        builder, "container-associative", type.associative ? 1U : 0U);
    add_key_u64(builder, "container-fixed", type.fixed ? 1U : 0U);
    add_key_u64(builder, "container-index-width", type.index_width);
    add_key_u64(
        builder, "container-index-two-state",
        type.two_state_indices ? 1U : 0U);
    add_key_u64(
        builder, "container-index-signed", type.signed_indices ? 1U : 0U);
    add_key_u64(
        builder, "container-index-string", type.string_indices ? 1U : 0U);
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
        builder, "container-maximum", type.maximum_elements.value_or(0));
    add_key_u64(
        builder, "container-member-name-count", type.member_names.size());
    for (const auto& name : type.member_names) {
        builder.add("container-member-name", name);
    }
    add_key_u64(
        builder, "container-child-type-count", type.element_types.size());
    for (const auto& child : type.element_types) {
        add_container_type_key(builder, child);
    }
}

[[nodiscard]] std::string make_native_object_cache_key(
    const std::string_view symbol, const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const JitOptimizationLevel optimization,
    const bool debug_instrumentation,
    const bool require_direct_update_slots,
    const std::string_view code_coverage_identity,
    const llvm::Triple& target_triple, const llvm::DataLayout& data_layout,
    const std::string_view target_cpu,
    const std::span<const std::string> target_features)
{
    CacheKeyBuilder builder;
    builder.add("llvm-object-schema", kNativeObjectCacheSchema);
    builder.add("llvm-version", LLVM_VERSION_STRING);
    builder.add("optimization", to_string(optimization));
    add_key_u64(builder, "debug-instrumentation", debug_instrumentation);
    add_key_u64(
        builder, "require-direct-update-slots", require_direct_update_slots);
    builder.add("code-coverage-identity", code_coverage_identity);
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
    for (const auto& feature : target_features) {
        builder.add("target-feature", feature);
    }

    builder.add("symbol", symbol);
    add_key_u64(builder, "process-id", process.id);
    builder.add("process-name", process.name);
    builder.add("language-standard", process.language_standard);
    builder.add("compatibility-profile", process.compatibility_profile);
    add_key_u64(builder, "register-count", process.register_count);
    add_key_u64(
        builder, "string-register-count", process.string_register_count);
    add_key_u64(
        builder, "container-register-count",
        process.container_register_count);
    for (const auto& type : process.container_register_types) {
        add_container_type_key(builder, type);
    }
    builder.add(
        "container-semantics",
        "resource-budgeted-recursive-composite-scalar-file-io-v32");
    add_key_u64(
        builder,
        "container-storage-byte-budget",
        runtime::simir::maximum_container_storage_bytes);
    add_key_u64(
        builder,
        "read-memory-byte-limit",
        runtime::simir::maximum_memory_file_bytes);
    builder.add("mutable-string-semantics", "simir-string-layout-v1");
    builder.add("text-file-semantics", "simir-text-file-v4-scalar-io");
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
        builder, "switch-source-offset", process.switch_source_offset);
    add_key_u64(
        builder, "switch-target-offset", process.switch_target_offset);
    add_key_u64(builder, "switch-width", process.switch_width);
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
                } else if constexpr (
                    std::is_same_v<OperationType, WaitOrder>) {
                    referenced_signals.insert(
                        referenced_signals.end(),
                        value.events.begin(),
                        value.events.end());
                } else if constexpr (
                    std::is_same_v<OperationType, EventTriggered>) {
                    referenced_signals.push_back(value.event);
                } else if constexpr (
                    std::is_same_v<OperationType, EventAlias>) {
                    referenced_signals.push_back(value.target);
                    if (value.has_source) {
                        referenced_signals.push_back(value.source);
                    }
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
        const auto kind = signal_value_kinds.empty()
            ? ValueKind::logic4
            : signal_value_kinds[signal];
        add_key_u64(
            builder,
            "referenced-signal-value-kind",
            static_cast<std::underlying_type_t<ValueKind>>(kind));
    }
    add_key_u64(builder, "sensitivity-count",
        process.static_sensitivity.size());
    for (const auto& sensitivity : process.static_sensitivity) {
        add_key_u64(builder, "sensitivity-signal", sensitivity.signal);
        add_key_u64(
            builder, "sensitivity-edge",
            static_cast<std::underlying_type_t<runtime::simir::EdgeKind>>(
                sensitivity.edge));
    }
    add_key_u64(
        builder, "static-trigger-region-count",
        process.static_trigger_regions.size());
    for (const auto& region : process.static_trigger_regions) {
        add_key_u64(builder, "static-trigger-region-begin", region.begin);
        add_key_u64(builder, "static-trigger-region-end", region.end);
        add_key_u64(builder, "static-trigger-region-mask", region.mask);
    }
    add_operation_cache_keys(builder, process, signal_widths);
    return builder.finish();
}

[[nodiscard]] std::string make_immutable_design_object_cache_key(
    const std::string_view design_identity,
    const std::string_view module_identity,
    const std::string_view symbol,
    const JitOptimizationLevel optimization,
    const bool debug_instrumentation,
    const bool require_direct_update_slots,
    const std::string_view code_coverage_identity,
    const llvm::Triple& target_triple,
    const llvm::DataLayout& data_layout,
    const std::string_view target_cpu,
    const std::span<const std::string> target_features)
{
    CacheKeyBuilder builder;
    builder.add("llvm-object-schema", kNativeObjectCacheSchema);
    builder.add("llvm-version", LLVM_VERSION_STRING);
    builder.add("optimization", to_string(optimization));
    add_key_u64(builder, "debug-instrumentation", debug_instrumentation);
    add_key_u64(
        builder, "require-direct-update-slots", require_direct_update_slots);
    builder.add("code-coverage-identity", code_coverage_identity);
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
    for (const auto& feature : target_features) {
        builder.add("target-feature", feature);
    }
    builder.add("immutable-design-identity", design_identity);
    builder.add("module-identity", module_identity);
    builder.add("symbol", symbol);
    return builder.finish();
}

[[nodiscard]] std::string make_native_module_cache_key(
    const std::string_view module_identity,
    const std::span<const std::string> process_keys)
{
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
    const std::string_view key, const std::size_t offset) noexcept
{
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
    const std::span<const std::uint32_t> register_widths,
    const std::size_t string_register_count,
    const bool uses_logic9,
    const bool tracks_register_initialization,
    const std::span<const runtime::simir::SignalId> direct_read_signals,
    const std::span<const runtime::simir::SignalId> direct_update_signals)
{
    JitProcessFrameLayout result;
    result.layout_id_low = cache_key_word(cache_key, 0);
    result.layout_id_high = cache_key_word(cache_key, 16);
    result.register_count = static_cast<std::uint32_t>(register_widths.size());
    result.string_register_count = static_cast<std::uint32_t>(string_register_count);
    result.uses_logic9 = uses_logic9;
    result.tracks_register_initialization = tracks_register_initialization;
    result.register_widths.assign(register_widths.begin(), register_widths.end());
    result.direct_read_signals.assign(
        direct_read_signals.begin(), direct_read_signals.end());
    result.direct_update_signals.assign(
        direct_update_signals.begin(), direct_update_signals.end());
    result.register_word_offsets.reserve(register_widths.size());
    std::uint64_t offset { };
    for (const auto width : register_widths) {
        result.register_word_offsets.push_back(static_cast<std::uint32_t>(offset));
        offset += (static_cast<std::uint64_t>(width) + 63U) / 64U;
        if (offset > std::numeric_limits<std::uint32_t>::max()) {
            throw LlvmJitError { "JIT register-word frame exceeds host ABI limits" };
        }
    }
    result.register_word_count = static_cast<std::uint32_t>(offset);
    return result;
}

} // namespace fsim::compiler::llvm_detail

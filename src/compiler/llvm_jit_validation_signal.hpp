// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llvm_jit_internal.hpp"

#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace fsim::compiler::llvm_detail {

template <typename OperationType, typename ExactSignalWidth,
    typename ReferencedSignalWidth, typename SignalWidth,
    typename RecordUse, typename ConstrainWidth,
    typename RecordUnsupported, typename ValidateTarget,
    typename ValidateCallStack, typename RecordDefinition,
    typename UnifyRegisters, typename ValidateDynamicSelection,
    typename ValidateDynamicPartSelection>
[[nodiscard]] bool validate_output_signal_operation(
    const OperationType& operation,
    const runtime::simir::Process& process,
    const std::size_t index,
    ValidatedProcess& result,
    ExactSignalWidth&& exact_signal_width,
    ReferencedSignalWidth&& referenced_signal_width,
    SignalWidth&& signal_width,
    RecordUse&& record_use,
    ConstrainWidth&& constrain_width,
    RecordUnsupported&& record_unsupported,
    ValidateTarget&& validate_target,
    ValidateCallStack&& validate_call_stack,
    RecordDefinition&& record_definition,
    UnifyRegisters&& unify_registers,
    ValidateDynamicSelection&& validate_dynamic_selection,
    ValidateDynamicPartSelection&& validate_dynamic_part_selection)
{
    using namespace runtime::simir;
    if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
        const auto width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(operation.source, width, index);
        result.uses_exact_signal_operation = result.uses_exact_signal_operation || width > 64;
    } else if constexpr (std::is_same_v<OperationType, Assert>) {
        record_use(operation.condition, index);
        constrain_width(operation.condition, 1U, index);
        if (operation.severity
            != runtime::simir::AssertionSeverity::failure) {
            result.uses_report = true;
        }
    } else if constexpr (std::is_same_v<OperationType, DebugPoint>) {
        result.uses_debug_points = true;
    } else if constexpr (std::is_same_v<OperationType, Display>) {
        if (operation.postponed) {
            result.uses_postponed_output = true;
        } else {
            result.uses_output = true;
        }
    } else if constexpr (std::is_same_v<OperationType, FormatDisplay>) {
        record_use(operation.source, index);
        const auto width = formatted_value_width(operation.format, operation.scalar_kind);
        if (!width)
            reject(process, index, "FormatDisplay scalar metadata is inconsistent");
        if (*width != 0)
            constrain_width(operation.source, *width, index);
        result.uses_formatted_output = true;
    } else if constexpr (std::is_same_v<OperationType, TimeDisplay>) {
        result.uses_time_output = true;
    } else if constexpr (std::is_same_v<OperationType, MonitorInstall>) {
        for (const auto& value : operation.values) {
            if (value.kind == MonitorValueKind::signal) {
                const auto signal_value_width = signal_width(value.signal, index);
                const auto width = formatted_value_width(value.format, value.scalar_kind);
                if (!width || (*width != 0 && *width != signal_value_width))
                    reject(process, index, "MonitorInstall scalar metadata is inconsistent");
            } else if (value.kind != MonitorValueKind::time)
                reject(process, index, "MonitorInstall value kind is invalid");
        }
        result.uses_monitor_install = true;
    } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
        result.uses_monitor_control = true;
    } else if constexpr (std::is_same_v<OperationType, RandomValue>) {
        record_definition(operation.destination, index);
        constrain_width(operation.destination, 32U, index);
        if (operation.maximum)
            record_use(*operation.maximum, index);
        if (operation.minimum)
            record_use(*operation.minimum, index);
        if (operation.minimum && !operation.maximum) {
            reject(process, index, "random minimum requires a maximum");
        }
        result.uses_random_value = true;
    } else if constexpr (std::is_same_v<OperationType, ScopeRandomize>) {
        record_definition(operation.destination, index);
        constrain_width(operation.destination, 32U, index);
        if (operation.targets.empty())
            reject(process, index, "scope randomize requires a target");
        for (const auto& target : operation.targets) {
            record_use(target.target, index);
            record_definition(target.target, index);
            constrain_width(target.target, target.width, index);
            if (target.canonical_identity.empty() || target.width == 0 || target.nominal_type.empty())
                reject(process, index, "scope randomize target is incomplete");
        }
        record_unsupported(index, "scope randomize uses the interpreter solver service");
    } else if constexpr (std::is_same_v<OperationType, Report>) {
        result.uses_report = true;
    } else if constexpr (std::is_same_v<OperationType, Jump>) {
        validate_target(operation.target, index, "jump");
    } else if constexpr (std::is_same_v<OperationType, Call>) {
        validate_target(operation.target, index, "call");
        validate_target(
            operation.return_target, index, "call return");
        validate_call_stack(operation.stack, index);
    } else if constexpr (std::is_same_v<OperationType, Return>) {
        validate_call_stack(operation.stack, index);
    } else if constexpr (
        std::is_same_v<OperationType, CallableFramePush>) {
        if (operation.identity == 0) {
            reject(process, index,
                "automatic callable frame identity must be nonzero");
        }
        auto packed = operation.packed;
        std::ranges::sort(packed);
        if (std::ranges::adjacent_find(packed) != packed.end()) {
            reject(process, index,
                "automatic callable frame repeats a packed register");
        }
        for (const auto id : operation.packed) {
            if (id >= process.register_count) {
                reject(process, index,
                    "automatic callable frame packed register is outside register_count");
            }
        }
        auto strings = operation.strings;
        std::ranges::sort(strings);
        if (std::ranges::adjacent_find(strings) != strings.end()) {
            reject(process, index,
                "automatic callable frame repeats a string register");
        }
        for (const auto id : operation.strings) {
            if (id >= process.string_register_count) {
                reject(process, index,
                    "automatic callable frame string register is outside string_register_count");
            }
        }
        auto containers = operation.containers;
        std::ranges::sort(containers);
        if (std::ranges::adjacent_find(containers) != containers.end()) {
            reject(process, index,
                "automatic callable frame repeats a container register");
        }
        for (const auto id : operation.containers) {
            if (id >= process.container_register_types.size()) {
                reject(process, index,
                    "automatic callable frame container register is outside the type table");
            }
        }
    } else if constexpr (
        std::is_same_v<OperationType, CallableFramePop>) {
        if (operation.identity == 0) {
            reject(process, index,
                "automatic callable frame identity must be nonzero");
        }
        auto packed = operation.preserve_packed;
        std::ranges::sort(packed);
        if (std::ranges::adjacent_find(packed) != packed.end()) {
            reject(process, index,
                "automatic callable frame pop repeats a preserved packed register");
        }
        for (const auto id : operation.preserve_packed) {
            record_use(id, index);
            record_definition(id, index);
        }
        auto strings = operation.preserve_strings;
        std::ranges::sort(strings);
        if (std::ranges::adjacent_find(strings) != strings.end()) {
            reject(process, index,
                "automatic callable frame pop repeats a preserved string register");
        }
        for (const auto id : operation.preserve_strings) {
            if (id >= process.string_register_count) {
                reject(process, index,
                    "automatic callable frame pop preserved string register is outside string_register_count");
            }
        }
        auto containers = operation.preserve_containers;
        std::ranges::sort(containers);
        if (std::ranges::adjacent_find(containers) != containers.end()) {
            reject(process, index,
                "automatic callable frame pop repeats a preserved container register");
        }
        for (const auto id : operation.preserve_containers) {
            if (id >= process.container_register_types.size()) {
                reject(process, index,
                    "automatic callable frame pop preserved container register is outside the type table");
            }
        }
    } else if constexpr (std::is_same_v<OperationType, Branch>) {
        record_use(operation.condition, index);
        constrain_width(operation.condition, 1U, index);
        validate_target(operation.when_true, index, "branch true");
        validate_target(operation.when_false, index, "branch false");
        switch (operation.unknown_policy) {
        case UnknownBranchPolicy::error:
        case UnknownBranchPolicy::when_false:
            break;
        default:
            reject(process, index, "branch has an invalid unknown policy");
        }
    } else if constexpr (std::is_same_v<OperationType, Halt>) {
    } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
        const auto width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(operation.source, width, index);
        if (width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_update = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
        const auto width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(operation.source, width, index);
        if (width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_after = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
        const auto width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(operation.source, width, index);
        if (width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_inertial = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
        record_use(operation.source, index);
        constrain_width(
            operation.source,
            signal_width(operation.signal, index),
            index);
        switch (operation.mode) {
        case runtime::simir::ProjectedDelayMode::transport:
        case runtime::simir::ProjectedDelayMode::inertial:
            break;
        default:
            reject(
                process,
                index,
                "projected write has an invalid delay mode");
        }
        if (operation.mode
                == runtime::simir::ProjectedDelayMode::inertial
            && operation.rejection > operation.delay) {
            reject(
                process,
                index,
                "projected-write rejection exceeds its delay");
        }
        if (operation.mode
                == runtime::simir::ProjectedDelayMode::transport
            && operation.rejection != 0) {
            reject(
                process,
                index,
                "transport projected write has a rejection limit");
        }
        result.uses_write_projected = true;
    } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
        const auto target_width = signal_width(operation.signal, index);
        if (operation.elements.size() < 2) {
            reject(
                process,
                index,
                "projected waveform requires at least two elements");
        }
        if (operation.elements.size()
            > std::numeric_limits<std::uint32_t>::max()) {
            reject(
                process,
                index,
                "projected waveform has too many elements for the "
                "runtime ABI");
        }
        std::optional<runtime::SimulationTick> previous_delay;
        for (const auto& element : operation.elements) {
            record_use(element.source, index);
            constrain_width(
                element.source, target_width, index);
            if (previous_delay
                && element.delay <= *previous_delay) {
                reject(
                    process,
                    index,
                    "projected-waveform delays must be strictly "
                    "ascending");
            }
            previous_delay = element.delay;
        }
        switch (operation.mode) {
        case runtime::simir::ProjectedDelayMode::transport:
            if (operation.rejection != 0) {
                reject(
                    process,
                    index,
                    "transport projected waveform has a rejection limit");
            }
            break;
        case runtime::simir::ProjectedDelayMode::inertial:
            if (operation.rejection
                > operation.elements.front().delay) {
                reject(
                    process,
                    index,
                    "projected-waveform rejection exceeds its first "
                    "delay");
            }
            break;
        default:
            reject(
                process,
                index,
                "projected waveform has an invalid delay mode");
        }
        result.uses_write_projected_waveform = true;
    } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_blocking_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_update_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_after_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_inertial_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
        record_use(operation.source, index);
        (void)signal_width(operation.signal, index);
        switch (operation.mode) {
        case runtime::simir::ProjectedDelayMode::transport:
        case runtime::simir::ProjectedDelayMode::inertial:
            break;
        default:
            reject(
                process,
                index,
                "projected slice has an invalid delay mode");
        }
        if (operation.mode
                == runtime::simir::ProjectedDelayMode::inertial
            && operation.rejection > operation.delay) {
            reject(
                process,
                index,
                "projected slice rejection exceeds its delay");
        }
        if (operation.mode
                == runtime::simir::ProjectedDelayMode::transport
            && operation.rejection != 0) {
            reject(
                process,
                index,
                "transport projected slice has a rejection limit");
        }
        result.uses_write_projected_slice = true;
    } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
        (void)signal_width(operation.signal, index);
        if (operation.elements.size() < 2) {
            reject(
                process,
                index,
                "projected slice waveform requires at least two "
                "elements");
        }
        if (operation.elements.size()
            > std::numeric_limits<std::uint32_t>::max()) {
            reject(
                process,
                index,
                "projected slice waveform has too many elements for the "
                "runtime ABI");
        }
        std::optional<runtime::SimulationTick> previous_delay;
        std::optional<RegisterId> first_source;
        for (const auto& element : operation.elements) {
            record_use(element.source, index);
            if (first_source) {
                unify_registers(
                    *first_source, element.source, index);
            } else {
                first_source = element.source;
            }
            if (previous_delay
                && element.delay <= *previous_delay) {
                reject(
                    process,
                    index,
                    "projected slice waveform delays must be strictly "
                    "ascending");
            }
            previous_delay = element.delay;
        }
        switch (operation.mode) {
        case runtime::simir::ProjectedDelayMode::transport:
            if (operation.rejection != 0) {
                reject(
                    process,
                    index,
                    "transport projected slice waveform has a rejection "
                    "limit");
            }
            break;
        case runtime::simir::ProjectedDelayMode::inertial:
            if (operation.rejection
                > operation.elements.front().delay) {
                reject(
                    process,
                    index,
                    "projected slice waveform rejection exceeds its first "
                    "delay");
            }
            break;
        default:
            reject(
                process,
                index,
                "projected slice waveform has an invalid delay mode");
        }
        result.uses_write_projected_waveform_slice = true;
    } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(operation.source, 1U, index);
        validate_dynamic_selection(
            operation.selection, target_width, index);
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_blocking_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(operation.source, 1U, index);
        validate_dynamic_selection(
            operation.selection, target_width, index);
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_update_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(operation.source, 1U, index);
        validate_dynamic_selection(
            operation.selection, target_width, index);
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_after_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(
            operation.source, operation.selection.width, index);
        validate_dynamic_part_selection(
            operation.selection, target_width, index);
        if (target_width > 64 || operation.selection.width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_blocking_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
        const auto target_width = referenced_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(
            operation.source, operation.selection.width, index);
        validate_dynamic_part_selection(
            operation.selection, target_width, index);
        if (target_width > 64 || operation.selection.width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_update_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(
            operation.source, operation.selection.width, index);
        validate_dynamic_part_selection(
            operation.selection, target_width, index);
        if (target_width > 64 || operation.selection.width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_after_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
        record_use(operation.source, index);
        const auto target_width = exact_signal_width(operation.signal, index);
        if (operation.selection) {
            constrain_width(operation.source, 1U, index);
            validate_dynamic_selection(
                *operation.selection, target_width, index);
        }
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else if (operation.driving_value) {
            result.uses_force_driver_signal_slice = true;
        } else {
            result.uses_force_signal_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        if (operation.width == 0) {
            reject(
                process, index,
                "ReleaseSignalSlice width must be greater than zero");
        }
        if (operation.selection) {
            if (operation.width != 1) {
                reject(
                    process, index,
                    "dynamic ReleaseSignalSlice width must be one");
            }
            validate_dynamic_selection(
                *operation.selection, target_width, index);
        }
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else if (operation.driving_value) {
            result.uses_release_driver_signal_slice = true;
        } else {
            result.uses_release_signal_slice = true;
        }
    } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
        const auto target_width = exact_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(operation.source, 1U, index);
        validate_dynamic_selection(
            operation.selection, target_width, index);
        if (target_width > 64) {
            result.uses_exact_signal_operation = true;
        } else {
            result.uses_write_inertial_slice = true;
        }
    } else if constexpr (std::is_same_v<
                             OperationType,
                             WriteInertialDynamicPartSlice>) {
        const auto target_width = referenced_signal_width(operation.signal, index);
        record_use(operation.source, index);
        constrain_width(
            operation.source, operation.selection.width, index);
        validate_dynamic_part_selection(
            operation.selection, target_width, index);
        result.uses_exact_signal_operation = true;
    } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
        const auto target_width = signal_width(operation.signal, index);
        record_use(operation.source, index);
        validate_dynamic_selection(
            operation.selection, target_width, index);
        switch (operation.mode) {
        case runtime::simir::ProjectedDelayMode::transport:
            if (operation.rejection != 0) {
                reject(
                    process,
                    index,
                    "transport projected dynamic slice has a rejection "
                    "limit");
            }
            break;
        case runtime::simir::ProjectedDelayMode::inertial:
            if (operation.rejection > operation.delay) {
                reject(
                    process,
                    index,
                    "projected dynamic-slice rejection exceeds its "
                    "delay");
            }
            break;
        default:
            reject(
                process,
                index,
                "projected dynamic slice has an invalid delay mode");
        }
        result.uses_write_projected_slice = true;
    } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
        const auto target_width = signal_width(operation.signal, index);
        validate_dynamic_selection(
            operation.selection, target_width, index);
        if (operation.elements.size() < 2) {
            reject(
                process,
                index,
                "projected dynamic-slice waveform requires at least "
                "two elements");
        }
        if (operation.elements.size()
            > std::numeric_limits<std::uint32_t>::max()) {
            reject(
                process,
                index,
                "projected dynamic-slice waveform has too many "
                "elements for the runtime ABI");
        }
        std::optional<runtime::SimulationTick> previous_delay;
        std::optional<RegisterId> first_source;
        for (const auto& element : operation.elements) {
            record_use(element.source, index);
            if (first_source) {
                unify_registers(
                    *first_source, element.source, index);
            } else {
                first_source = element.source;
            }
            if (previous_delay
                && element.delay <= *previous_delay) {
                reject(
                    process,
                    index,
                    "projected dynamic-slice waveform delays must be "
                    "strictly ascending");
            }
            previous_delay = element.delay;
        }
        switch (operation.mode) {
        case runtime::simir::ProjectedDelayMode::transport:
            if (operation.rejection != 0) {
                reject(
                    process,
                    index,
                    "transport projected dynamic-slice waveform has a "
                    "rejection limit");
            }
            break;
        case runtime::simir::ProjectedDelayMode::inertial:
            if (operation.rejection
                > operation.elements.front().delay) {
                reject(
                    process,
                    index,
                    "projected dynamic-slice waveform rejection exceeds "
                    "its first delay");
            }
            break;
        default:
            reject(
                process,
                index,
                "projected dynamic-slice waveform has an invalid delay "
                "mode");
        }
        result.uses_write_projected_waveform_slice = true;

    } else {
        return false;
    }
    return true;
}

} // namespace fsim::compiler::llvm_detail

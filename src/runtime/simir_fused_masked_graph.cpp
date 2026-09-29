// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <ranges>
#include <type_traits>

namespace fsim::runtime::simir {
namespace {

bool eligible_singleton_owner(const Process& process,
    const SignalId output)
{
    const auto count = process.operations.size();
    if (count < 3U || process.static_sensitivity.empty()
        || process.final || process.observed || process.reactive
        || process.postponed || process.switch_source
        || process.switch_target || process.switch_control
        || process.switch_bidirectional || process.switch_resistive
        || process.drive_strength != DriveStrength { }
        || process.string_register_count != 0U
        || process.container_register_count != 0U
        || !process.debug_locals.empty()
        || !process.debug_string_locals.empty()
        || !process.debug_container_locals.empty()
        || !process.static_trigger_regions.empty()
        || process.driver_regions.size() != 1U
        || process.driver_regions.front().signal != output
        || !operation_holds<WaitSensitivity>(
            process.operations.expanded(count - 2U))) {
        return false;
    }
    const auto tail = process.operations.expanded(count - 1U);
    const auto* jump = operation_get_if<Jump>(&tail);
    if (jump == nullptr || jump->target != 0U) {
        return false;
    }
    if (std::ranges::any_of(process.static_sensitivity,
            [](const Sensitivity& entry) {
                return entry.edge != EdgeKind::any;
            })) {
        return false;
    }
    bool wrote { };
    for (std::size_t index = 0U; index + 2U < count; ++index) {
        const auto operation = process.operations.expanded(index);
        const auto* whole = operation_get_if<WriteUpdate>(&operation);
        const auto* slice = operation_get_if<WriteUpdateSlice>(&operation);
        if (whole || slice) {
            const auto signal = whole ? whole->signal : slice->signal;
            if (signal != output) {
                return false;
            }
            wrote = true;
        } else if (visit_operation([](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, WriteProjected>
                || std::is_same_v<Type, WriteAfter>
                || std::is_same_v<Type, WriteInertial>
                || std::is_same_v<Type, WriteProjectedWaveform>) {
                return true;
            } else {
                return false;
            }
        }, operation)) {
            return false;
        }
    }
    return wrote;
}

std::optional<SignalId> eligible_projected_singleton(
    const Process& process)
{
    const auto count = process.operations.size();
    if (count < 3U || process.static_sensitivity.empty()
        || process.final || process.observed || process.reactive
        || process.postponed || process.switch_source
        || process.switch_target || process.switch_control
        || process.switch_bidirectional || process.switch_resistive
        || process.drive_strength != DriveStrength { }
        || process.string_register_count != 0U
        || process.container_register_count != 0U
        || !process.debug_locals.empty()
        || !process.debug_string_locals.empty()
        || !process.debug_container_locals.empty()
        || !process.static_trigger_regions.empty()
        || process.driver_regions.size() != 1U
        || !process.driver_regions.front().whole
        || !operation_holds<WaitSensitivity>(
            process.operations.expanded(count - 2U))) {
        return std::nullopt;
    }
    const auto tail = process.operations.expanded(count - 1U);
    const auto* jump = operation_get_if<Jump>(&tail);
    if (jump == nullptr || jump->target != 0U
        || std::ranges::any_of(process.static_sensitivity,
            [](const Sensitivity& item) {
                return item.edge != EdgeKind::any;
            })) {
        return std::nullopt;
    }
    std::optional<SignalId> output;
    for (std::size_t index = 0U; index + 2U < count; ++index) {
        const auto operation = process.operations.expanded(index);
        if (const auto* write = operation_get_if<WriteProjected>(&operation)) {
            if (output || write->delay != 0U || write->rejection != 0U
                || write->mode != ProjectedDelayMode::inertial
                || write->signal != process.driver_regions.front().signal) {
                return std::nullopt;
            }
            output = write->signal;
        } else if (visit_operation([](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            return std::is_same_v<Type, WriteUpdate>
                || std::is_same_v<Type, WriteUpdateSlice>
                || std::is_same_v<Type, WriteAfter>
                || std::is_same_v<Type, WriteInertial>
                || std::is_same_v<Type, WriteProjectedWaveform>;
        }, operation)) {
            return std::nullopt;
        }
    }
    return output;
}

template <typename SignalKind>
bool eligible_terminal_body(const Process& process,
    SignalKind signal_is_logic4)
{
    // A downstream member must not make an otherwise compilable producer
    // region fail as a whole. Admit a conservative language-neutral subset
    // of the masked compiler's pure body operations at this graph edge.
    bool logic4_only = std::ranges::all_of(
        process.register_value_kinds,
        [](const ValueKind kind) { return kind == ValueKind::logic4; });
    bool has_reduction { };
    for (std::size_t index = 0U;
         index + 2U < process.operations.size(); ++index) {
        const auto operation = process.operations.expanded(index);
        const auto accepted = visit_operation([&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, DebugPoint>
                || std::is_same_v<Type, CopyRegister>
                || std::is_same_v<Type, Extract>
                || std::is_same_v<Type, Concatenate>
                || std::is_same_v<Type, ConditionalSelect>
                || std::is_same_v<Type, WriteProjected>
                || std::is_same_v<Type, WriteUpdate>
                || std::is_same_v<Type, WriteUpdateSlice>) {
                return true;
            } else if constexpr (std::is_same_v<Type, LoadConstant>) {
                logic4_only &= !value.value.is_logic9();
                return true;
            } else if constexpr (std::is_same_v<Type, ReadSignal>) {
                logic4_only &= signal_is_logic4(value.signal);
                return value.kind == SignalReadKind::current
                    && value.ticks == 1U && !value.clock && !value.gate;
            } else if constexpr (std::is_same_v<Type, Reduction>) {
                has_reduction = true;
                return value.operation == ReductionOperator::bit_and
                    || value.operation == ReductionOperator::bit_or
                    || value.operation == ReductionOperator::bit_xor;
            } else if constexpr (std::is_same_v<Type, Binary>) {
                return value.operation == BinaryOperator::bit_and
                    || value.operation == BinaryOperator::bit_or
                    || value.operation == BinaryOperator::bit_xor
                    || value.operation == BinaryOperator::equal
                    || value.operation == BinaryOperator::case_equal;
            } else {
                return false;
            }
        }, operation);
        if (!accepted) {
            return false;
        }
    }
    return !has_reduction || logic4_only;
}

bool eligible_normal_single_writer(const Process& process,
    const SignalId output, const std::size_t width)
{
    if (!eligible_singleton_owner(process, output)
        || process.driver_regions.size() != 1U
        || !process.driver_regions.front().whole
        || process.driver_regions.front().offset != 0U
        || (process.driver_regions.front().width != 0U
            && process.driver_regions.front().width != width)) {
        return false;
    }
    std::size_t writes { };
    for (std::size_t index = 0U;
         index + 2U < process.operations.size(); ++index) {
        const auto operation = process.operations.expanded(index);
        if (const auto* update = operation_get_if<WriteUpdate>(&operation)) {
            if (update->signal != output) {
                return false;
            }
            ++writes;
        } else if (operation_holds<WriteUpdateSlice>(operation)) {
            return false;
        } else if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
            if (read->signal == output) {
                return false;
            }
        }
    }
    return writes == 1U;
}

} // namespace

void Interpreter::Impl::build_fused_masked_region_plans()
{
    const auto none = std::numeric_limits<std::size_t>::max();
    fused_masked_regions.clear();
    fused_masked_regions_by_container_object.assign(
        container_objects.size(), { });
    fused_masked_region_by_process.assign(processes.size(), none);
    fused_masked_offset_by_process.assign(processes.size(), none);
    fused_masked_counts = { };
    if (process_profile_enabled || execution_point_hook
        || driver_change_hook || signal_change_hook
        || stored_signal_change_hook || scalar_signal_change_hook
        || container_object_change_hook
        || (native_signal_observation_any_hook
            && native_signal_observation_any_hook())) {
        return;
    }
    build_fused_masked_normalized_programs();
    const auto program_for = [&](const ProcessId id) -> const Process& {
        return id < fused_masked_normalized_programs.size()
                && fused_masked_normalized_programs[id]
            ? *fused_masked_normalized_programs[id]
            : processes[id].program();
    };

    // This first lowering cut groups independently triggered original tasks
    // by a graph-certified disjoint-owned output. The compiler performs the
    // body/CFG proof before a candidate can be bound. It is a structural
    // grouping over the complete elaborated owner inventory, not a source
    // name, width, or operation-count recognizer.
    std::map<SignalId, std::vector<ProcessId>> by_output;
    std::size_t owned_candidates { };
    std::size_t singleton_candidates { };
    std::size_t body_candidates { };
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        if (id >= owned_driver_spans.size()) {
            continue;
        }
        const auto& owner = owned_driver_spans[id];
        if (owner.signal >= signals.size()
            || !owned_driver_active(owner.signal)
            || signal_transaction_observed[owner.signal]) {
            continue;
        }
        ++owned_candidates;
        const auto cohort = id < static_sensitivity_cohort_by_process.size()
            ? static_sensitivity_cohort_by_process[id] : none;
        if (cohort != none
            && static_sensitivity_cohorts[cohort].members.size() != 1U) {
            continue;
        }
        ++singleton_candidates;
        if (!eligible_singleton_owner(
                program_for(id), owner.signal)) {
            continue;
        }
        ++body_candidates;
        by_output[owner.signal].push_back(id);
    }

    // A complete single-writer terminal output can share the already
    // atomic masked call of a disjoint-owner aggregate when both processes
    // are triggered by that aggregate's committed change. The compiler
    // still proves the complete pure body; this inventory is only a cold
    // graph/sink certificate, not a source-shape recognizer.
    std::map<SignalId, std::vector<ProcessId>> normal_by_input;
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        const auto& program = program_for(id);
        if (program.driver_regions.size() != 1U) {
            continue;
        }
        const auto output = program.driver_regions.front().signal;
        if (output >= signals.size()) {
            continue;
        }
        const auto& signal = get_signal(output);
        const auto* record = driver_values[output].find(id);
        const bool ordinary_single_driver
            = signal.resolution == ResolutionKind::sv_wire
            && driver_values[output].size() == 1U
            && record != nullptr
            && record->strength == DriveStrength { }
            && record->value.width() == signal.initial_value.width();
        if (signal.value_kind != ValueKind::logic4
            || (signal.resolution != ResolutionKind::none
                && !ordinary_single_driver)
            || signal.systemverilog_scalar
                != SystemVerilogScalarKind::None
            || signal.has_implicit_driver || signal.has_charge_strength
            || signal.event_variable || signal_transaction_observed[output]
            || signal_writer_counts[output] != 1U
            || stable_single_writer_processes[output] != id
            || native_signal_has_runtime_dependency(output)
            || has_dynamic_waits(output)
            || !signal_container_aliases[output].empty()
            || forced_values[output] || forced_driver_values[output]
            || external_driver_values[output] || monitor_watches(output)
            || (native_signal_observation_required_hook
                && native_signal_observation_required_hook(output))
            || !eligible_normal_single_writer(
                program, output, signal.initial_value.width())
            || !eligible_terminal_body(program,
                [&](const SignalId read) {
                    return read < signals.size()
                        && signals[read].value_kind == ValueKind::logic4;
                })) {
            continue;
        }
        const auto cohort = id < static_sensitivity_cohort_by_process.size()
            ? static_sensitivity_cohort_by_process[id] : none;
        if (cohort != none
            && static_sensitivity_cohorts[cohort].members.size() != 1U) {
            continue;
        }
        for (const auto& sensitivity : program.static_sensitivity) {
            if (sensitivity.signal != output
                && by_output.contains(sensitivity.signal)) {
                normal_by_input[sensitivity.signal].push_back(id);
            }
        }
    }
    auto assigned_normal = std::vector<std::uint8_t>(processes.size(), 0U);

    for (auto& [signal, members] : by_output) {
        if (members.size() < 2U) {
            continue;
        }
        std::optional<ProcessId> normal_owner;
        if (const auto found = normal_by_input.find(signal);
            found != normal_by_input.end()) {
            for (const auto id : found->second) {
                if (!assigned_normal[id]) {
                    normal_owner = id;
                    assigned_normal[id] = 1U;
                    members.push_back(id);
                    break;
                }
            }
        }
        std::ranges::sort(members);
        auto& plan = fused_masked_regions.emplace_back();
        plan.candidate.region_id = fused_masked_regions.size() - 1U;
        plan.candidate.members = members;
        plan.candidate.outputs.push_back(signal);
        if (normal_owner) {
            const auto output = program_for(*normal_owner)
                .driver_regions.front().signal;
            plan.candidate.outputs.push_back(output);
            plan.candidate.normal_single_writer_output = output;
            plan.candidate.terminal_members.push_back(*normal_owner);
            plan.normal_output_owner = *normal_owner;
            ++fused_masked_counts.terminal_candidates;
            if (fused_masked_normalized_programs[*normal_owner]) {
                ++fused_masked_counts.normalized_terminal_candidates;
            }
        }
        // Boundary publication is retained until a separate complete reader
        // closure proves private fanout elision. This also admits packed
        // signals with an unbounded container reader safely.
        plan.candidate.boundary_outputs.push_back(signal);
        if (normal_owner) {
            plan.candidate.boundary_outputs.push_back(
                *plan.candidate.normal_single_writer_output);
        }
        const auto width = signals[signal].initial_value.width();
        plan.selected_write_mask.resize((width + 63U) / 64U);
        plan.activation_words.resize((members.size() + 63U) / 64U);
        if (normal_owner) {
            plan.before_terminal_write_mask.resize(
                plan.selected_write_mask.size());
            plan.after_terminal_write_mask.resize(
                plan.selected_write_mask.size());
            plan.before_terminal_activation_words.resize(
                plan.activation_words.size());
            plan.after_terminal_activation_words.resize(
                plan.activation_words.size());
        }
        plan.members.reserve(members.size());
        for (std::size_t index = 0U; index < members.size(); ++index) {
            const auto id = members[index];
            const auto& program = program_for(id);
            plan.members.push_back(FusedMaskedRegionPlan::Member {
                id,
                static_cast<InstructionIndex>(
                    program.operations.size() - 1U),
                { }
            });
            if (fused_masked_normalized_programs[id]) {
                const auto& original = processes[id].program();
                for (std::size_t operation_index = 0U;
                     operation_index < original.operations.size();
                     ++operation_index) {
                    const auto operation
                        = original.operations.expanded(operation_index);
                    if (const auto* read
                            = operation_get_if<ReadContainerObject>(
                                &operation)) {
                        plan.protected_container_objects.push_back(
                            read->object);
                    }
                }
            }
            fused_masked_region_by_process[id]
                = plan.candidate.region_id;
            fused_masked_offset_by_process[id] = index;
        }
        std::ranges::sort(plan.protected_container_objects);
        plan.protected_container_objects.erase(
            std::ranges::unique(plan.protected_container_objects).begin(),
            plan.protected_container_objects.end());
        plan.certified = true;
        for (const auto object : plan.protected_container_objects) {
            fused_masked_regions_by_container_object[object].push_back(
                plan.candidate.region_id);
        }
        ++fused_masked_counts.candidates;
    }
    // A projected writer remains tied to its original process/driver. Group
    // singleton writers by a shared elaborated trigger, then let each native
    // activation bit select only the owners actually ready in that delta.
    std::map<SignalId, std::vector<ProcessId>> projected_by_trigger;
    auto projected_outputs
        = std::vector<std::optional<SignalId>>(processes.size());
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        if (fused_masked_region_by_process[id] != none) {
            continue;
        }
        const auto cohort = id < static_sensitivity_cohort_by_process.size()
            ? static_sensitivity_cohort_by_process[id] : none;
        if (cohort != none
            && static_sensitivity_cohorts[cohort].members.size() != 1U) {
            continue;
        }
        const auto& program = processes[id].program();
        const auto output = eligible_projected_singleton(program);
        if (!output || *output >= signals.size()) {
            continue;
        }
        const auto& signal = get_signal(*output);
        if (signal.event_variable
            || signal_transaction_observed[*output]
            || native_signal_has_runtime_dependency(*output)
            || has_dynamic_waits(*output)
            || !signal_container_aliases[*output].empty()
            || forced_values[*output] || forced_driver_values[*output]
            || external_driver_values[*output]
            || monitor_watches(*output)
            || (native_signal_observation_required_hook
                && native_signal_observation_required_hook(*output))) {
            continue;
        }
        const bool unresolved_logic4
            = signal.value_kind == ValueKind::logic4
            && signal.resolution == ResolutionKind::none;
        const bool resolved_logic9
            = signal.value_kind == ValueKind::logic9
            && signal.resolution == ResolutionKind::std_logic;
        const auto* record = driver_values[*output].find(id);
        if ((unresolved_logic4
                && (signal_writer_counts[*output] != 1U
                    || stable_single_writer_processes[*output] != id))
            || (resolved_logic9
                && (record == nullptr
                    || driver_values[*output].size() != 1U
                    || record->strength != DriveStrength { }
                    || record->value.width()
                        != signal.initial_value.width()))
            || (!unresolved_logic4 && !resolved_logic9)) {
            continue;
        }
        projected_outputs[id] = *output;
        for (const auto& sensitivity : program.static_sensitivity) {
            projected_by_trigger[sensitivity.signal].push_back(id);
        }
    }
    auto trigger_order = std::vector<SignalId> { };
    trigger_order.reserve(projected_by_trigger.size());
    for (auto& [trigger, members] : projected_by_trigger) {
        std::ranges::sort(members);
        members.erase(std::ranges::unique(members).begin(), members.end());
        trigger_order.push_back(trigger);
    }
    std::ranges::sort(trigger_order,
        [&](const SignalId left, const SignalId right) {
            const auto left_size = projected_by_trigger.at(left).size();
            const auto right_size = projected_by_trigger.at(right).size();
            return left_size != right_size ? left_size > right_size
                                           : left < right;
        });
    auto assigned = std::vector<std::uint8_t>(processes.size(), 0U);
    for (const auto trigger : trigger_order) {
        auto members = std::vector<ProcessId> { };
        for (const auto id : projected_by_trigger.at(trigger)) {
            if (!assigned[id]) {
                members.push_back(id);
            }
        }
        if (members.size() < 2U) {
            continue;
        }
        // The producer tasks keep their original ready bits and read the
        // committed snapshot. A downstream pure singleton can share the
        // native call when it is ready in that same delta; its other triggers
        // still route through this region independently.
        auto producer_outputs = std::vector<SignalId> { };
        for (const auto id : members) {
            producer_outputs.push_back(*projected_outputs[id]);
        }
        auto terminal_members = std::vector<ProcessId> { };
        for (const auto id : members) {
            const auto& sensitivity = processes[id].program().static_sensitivity;
            if (std::ranges::any_of(sensitivity,
                    [&](const auto& item) {
                        return item.signal != *projected_outputs[id]
                            && std::ranges::find(producer_outputs,
                                item.signal) != producer_outputs.end();
                    })) {
                terminal_members.push_back(id);
            }
        }
        auto downstream = std::vector<ProcessId> { };
        for (const auto output : producer_outputs) {
            if (const auto found = projected_by_trigger.find(output);
                found != projected_by_trigger.end()) {
                downstream.insert(downstream.end(),
                    found->second.begin(), found->second.end());
            }
        }
        std::ranges::sort(downstream);
        downstream.erase(std::ranges::unique(downstream).begin(),
            downstream.end());
        for (const auto id : downstream) {
            if (!assigned[id]
                && std::ranges::find(members, id) == members.end()
                && std::ranges::find(producer_outputs,
                    *projected_outputs[id]) == producer_outputs.end()
                && eligible_terminal_body(processes[id].program(),
                    [&](const SignalId read) {
                        return read < signals.size()
                            && signals[read].value_kind
                                == ValueKind::logic4;
                    })) {
                terminal_members.push_back(id);
                members.push_back(id);
                break;
            }
        }
        std::ranges::sort(members);
        auto& plan = fused_masked_regions.emplace_back();
        plan.candidate.region_id = fused_masked_regions.size() - 1U;
        plan.candidate.members = members;
        plan.candidate.projected = true;
        plan.candidate.terminal_members = std::move(terminal_members);
        plan.activation_words.resize((members.size() + 63U) / 64U);
        plan.members.reserve(members.size());
        for (std::size_t index = 0U; index < members.size(); ++index) {
            const auto id = members[index];
            const auto& program = processes[id].program();
            const auto output = *eligible_projected_singleton(program);
            plan.candidate.outputs.push_back(output);
            plan.candidate.boundary_outputs.push_back(output);
            plan.members.push_back(FusedMaskedRegionPlan::Member {
                id,
                static_cast<InstructionIndex>(program.operations.size() - 1U),
                { }
            });
            fused_masked_region_by_process[id] = plan.candidate.region_id;
            fused_masked_offset_by_process[id] = index;
            assigned[id] = 1U;
        }
        plan.certified = true;
        fused_masked_counts.terminal_candidates
            += !plan.candidate.terminal_members.empty();
        ++fused_masked_counts.candidates;
    }
    if (std::getenv("FSIM_PROFILE_FUSED_MASKED") != nullptr) {
        std::cerr << "fsim masked graph: owned=" << owned_candidates
                  << " singleton=" << singleton_candidates
                  << " body=" << body_candidates
                  << " outputs=" << by_output.size()
                  << " regions=" << fused_masked_regions.size()
                  << '\n';
    }
}

void Interpreter::Impl::invalidate_fused_masked_regions() noexcept
{
    for (auto& region : fused_masked_regions) {
        region.certified = false;
    }
}

void Interpreter::Impl::invalidate_fused_masked_regions_for_container_object(
    const ContainerObjectId object) noexcept
{
    if (object >= fused_masked_regions_by_container_object.size()) {
        return;
    }
    for (const auto region_id :
         fused_masked_regions_by_container_object[object]) {
        auto& region = fused_masked_regions[region_id];
        if (region.certified) {
            region.certified = false;
            if (fused_masked_counters_enabled) {
                ++fused_masked_counts.demotions;
            }
        }
    }
}

void Interpreter::Impl::invalidate_fused_masked_regions_for_fork(
    const ProcessId child)
{
    const auto& program = processes.at(child).program();
    auto writes = std::vector<std::uint8_t>(signals.size(), 0U);
    for (const auto& owner : program.driver_regions) {
        if (owner.signal < writes.size()) {
            writes[owner.signal] = 1U;
        }
    }
    for (std::size_t index = 0U; index < program.operations.size(); ++index) {
        const auto operation = program.operations.expanded(index);
        visit_operation([&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, WriteUpdate>
                || std::is_same_v<Type, WriteUpdateSlice>
                || std::is_same_v<Type, WriteProjected>) {
                if (value.signal < writes.size()) {
                    writes[value.signal] = 1U;
                }
            }
        }, operation);
    }
    for (auto& region : fused_masked_regions) {
        const auto child_cohort
            = child < static_sensitivity_cohort_by_process.size()
            ? static_sensitivity_cohort_by_process[child]
            : std::numeric_limits<std::size_t>::max();
        const auto joins_member_cohort = std::ranges::any_of(
            region.candidate.members,
            [&](const ProcessId id) {
                return child_cohort
                        != std::numeric_limits<std::size_t>::max()
                    && id < static_sensitivity_cohort_by_process.size()
                    && static_sensitivity_cohort_by_process[id]
                        == child_cohort;
            });
        if (region.certified && (joins_member_cohort
            || std::ranges::any_of(region.candidate.outputs,
                [&](const SignalId signal) {
                    return writes[signal] != 0U;
                }))) {
            region.certified = false;
            if (fused_masked_counters_enabled) {
                ++fused_masked_counts.demotions;
            }
        }
    }
}

} // namespace fsim::runtime::simir

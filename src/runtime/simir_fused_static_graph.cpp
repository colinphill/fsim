// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_execution_context.hpp"
#include "fsim/runtime/simir_fused_branch_safety.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace fsim::runtime::simir {
namespace {

bool candidate_body_operation(const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, DebugPoint>
            || std::is_same_v<Type, CopyRegister>
            || std::is_same_v<Type, Extract>
            || std::is_same_v<Type, Concatenate>
            || std::is_same_v<Type, WriteUpdate>
            || std::is_same_v<Type, WriteUpdateSlice>) {
            return true;
        } else if constexpr (std::is_same_v<Type, LoadConstant>) {
            return true;
        } else if constexpr (std::is_same_v<Type, ReadSignal>) {
            return value.kind == SignalReadKind::current
                && value.ticks == 1U
                && !value.clock && !value.gate;
        } else if constexpr (std::is_same_v<Type, Binary>) {
            return value.operation == BinaryOperator::bit_and
                || value.operation == BinaryOperator::bit_or
                || value.operation == BinaryOperator::bit_xor;
        } else if constexpr (std::is_same_v<Type, Reduction>) {
            return value.operation == ReductionOperator::bit_and
                || value.operation == ReductionOperator::bit_or
                || value.operation == ReductionOperator::bit_xor;
        } else if constexpr (std::is_same_v<Type, WriteProjected>) {
            return value.delay == 0U && value.rejection == 0U
                && value.mode == ProjectedDelayMode::inertial;
        } else {
            return false;
        }
    }, operation);
}

bool generic_scheduling_operations(const ProcessProgramView& process)
{
    if (process.scheduling_domain() != ProcessSchedulingDomain::generic) {
        return false;
    }
    const auto& operations = process.operations();
    for (std::size_t index = 0U; index < operations.size(); ++index) {
        const auto operation = operations.expanded(index);
        const bool generic_domain = visit_operation([](const auto& value) {
            if constexpr (requires { value.domain; }) {
                return value.domain == SignalUpdateDomain::generic;
            }
            return true;
        }, operation);
        if (!generic_domain) {
            return false;
        }
    }
    return true;
}

bool pure_static_body(const ProcessProgramView& process)
{
    const auto register_value_kinds
        = process_layout_detail::ProcessLayoutAccess::view(process.register_value_kinds());
    const auto count = process.operations().size();
    if (count < 3U || process.static_sensitivity().empty()
        || process.final() || process.observed() || process.reactive()
        || process.postponed() || process.switch_source()
        || process.switch_target() || process.switch_control()
        || process.switch_bidirectional() || process.switch_resistive()
        || process.drive_strength() != DriveStrength { }
        || process.string_register_count() != 0U
        || process.container_register_count() != 0U
        || !process.debug_locals().empty()
        || !process.debug_string_locals().empty()
        || !process.debug_container_locals().empty()
        || !process.static_trigger_regions().empty()
        || !operation_holds<WaitSensitivity>(
            process.operations().expanded(count - 2U))) {
        return false;
    }
    const auto tail = process.operations().expanded(count - 1U);
    const auto* jump = operation_get_if<Jump>(&tail);
    if (jump == nullptr || jump->target != 0U) {
        return false;
    }
    for (std::size_t index = 0U; index + 2U < count; ++index) {
        if (!candidate_body_operation(process.operations().expanded(index))) {
            return false;
        }
    }
    const bool uses_logic9 = std::ranges::any_of(
        register_value_kinds,
        [](const auto kind) { return kind == ValueKind::logic9; });
    if (uses_logic9) {
        for (std::size_t index = 0U; index + 2U < count; ++index) {
            if (operation_holds<Reduction>(process.operations().expanded(index))) {
                return false;
            }
        }
    }
    return true;
}

struct MaskedAllActiveOutput {
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    bool projected { };
};

} // namespace

void Interpreter::Impl::build_fused_static_cohort_plans()
{
    fused_static_cohorts.clear();
    fused_static_cohorts.resize(static_sensitivity_cohorts.size());
    fused_static_certified_plan_count = 0U;
    fused_static_counts = { };
    fused_static_trace_enabled
        = std::getenv("FSIM_PROFILE_FUSED_STATIC") != nullptr;
    const bool trace = fused_static_trace_enabled;
    if (!process_signal_access_inventory_complete
        || process_profile_enabled || execution_point_hook
        || driver_change_hook || signal_change_hook
        || stored_signal_change_hook || scalar_signal_change_hook
        || container_object_change_hook
        || (native_signal_observation_any_hook
            && native_signal_observation_any_hook())) {
        if (trace) {
            std::cerr << "fsim fused-static graph: globally blocked"
                      << " process_profile=" << process_profile_enabled
                      << " execution_hook=" << bool(execution_point_hook)
                      << " signal_hook=" << bool(signal_change_hook)
                      << " driver_hook=" << bool(driver_change_hook)
                      << " any_observation="
                      << (native_signal_observation_any_hook
                          && native_signal_observation_any_hook())
                      << '\n';
        }
        return;
    }
    std::array<std::uint64_t, 4U> admission { };

    // Share the complete elaborated read/observation inventory. Range and
    // partial projected routes retain their existing conservative exclusion.
    const auto& graph = region_graph.value();
    std::vector<std::vector<ProcessId>> readers(signals.size());
    std::vector<std::uint8_t> observed(signals.size(), 0U);
    for (SignalId signal = 0U; signal < signals.size(); ++signal) {
        const auto& node = graph.signals()[signal];
        observed[signal] = node.observations != RegionObservation::none
            || node.writers_unknown || node.partial_projected_transactions;
        auto& ids = readers[signal];
        for (const auto& reader : node.readers) {
            if (ids.empty() || ids.back() != reader.process) {
                ids.push_back(reader.process);
            }
        }
    }

    const auto masked_all_active_body = [&](const ProcessProgramView& process,
                                           const OwnedDriverSpan& owner)
        -> std::optional<MaskedAllActiveOutput> {
        const auto count = process.operations().size();
        if (count < 3U || process.static_sensitivity().empty()
            || process.final() || process.observed() || process.reactive()
            || process.postponed() || process.switch_source()
            || process.switch_target() || process.switch_control()
            || process.switch_bidirectional() || process.switch_resistive()
            || process.drive_strength() != DriveStrength { }
            || process.register_count() == 0U
            || process.string_register_count() != 0U
            || process.container_register_count() != 0U
            || !process.debug_locals().empty()
            || !process.debug_string_locals().empty()
            || !process.debug_container_locals().empty()
            || !process.static_trigger_regions().empty()
            || process.driver_regions().size() != 1U
            || !operation_holds<WaitSensitivity>(
                process.operations().expanded(count - 2U))) {
            return std::nullopt;
        }
        const auto tail = process.operations().expanded(count - 1U);
        const auto* jump = operation_get_if<Jump>(&tail);
        if (jump == nullptr || jump->target != 0U) {
            return std::nullopt;
        }
        if (!detail::masked_forward_control_flow_is_valid(
                process.operations())) {
            return std::nullopt;
        }
        if (!detail::masked_branch_conditions_are_proven_known(
                process.operations(), process.register_count())) {
            return std::nullopt;
        }
        auto output = std::optional<MaskedAllActiveOutput> { };
        for (std::size_t index = 0U; index + 2U < count; ++index) {
            const auto operation = process.operations().expanded(index);
            const auto accepted = visit_operation([&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, DebugPoint>) {
                    return true;
                } else if constexpr (std::is_same_v<Type, LoadConstant>) {
                    return !value.value.is_logic9();
                } else if constexpr (std::is_same_v<Type, ReadSignal>) {
                    return value.signal < signals.size()
                        && signals[value.signal].value_kind
                            == ValueKind::logic4
                        && !has_container_signal_alias(value.signal)
                        && value.kind == SignalReadKind::current
                        && value.ticks == 1U && !value.clock && !value.gate;
                } else if constexpr (std::is_same_v<Type, CopyRegister>
                    || std::is_same_v<Type, Extract>
                    || std::is_same_v<Type, Concatenate>
                    || std::is_same_v<Type, ConditionalSelect>
                    || std::is_same_v<Type, Jump>) {
                    return true;
                } else if constexpr (std::is_same_v<Type, Branch>) {
                    // The shared dataflow proof above rejects an error policy
                    // whenever this reaching condition may contain X/Z.
                    return value.unknown_policy == UnknownBranchPolicy::error
                        || value.unknown_policy
                            == UnknownBranchPolicy::when_false;
                } else if constexpr (std::is_same_v<Type, Binary>) {
                    return value.operation == BinaryOperator::bit_and
                        || value.operation == BinaryOperator::bit_or
                        || value.operation == BinaryOperator::bit_xor
                        || value.operation == BinaryOperator::equal
                        || value.operation == BinaryOperator::case_equal;
                } else if constexpr (std::is_same_v<Type, Reduction>) {
                    return value.operation == ReductionOperator::bit_and
                        || value.operation == ReductionOperator::bit_or
                        || value.operation == ReductionOperator::bit_xor;
                } else if constexpr (std::is_same_v<Type, WriteUpdate>) {
                    if (output || value.signal != owner.signal
                        || owner.offset != 0U
                        || value.signal >= signals.size()
                        || signals[value.signal].value_kind
                            != ValueKind::logic4) {
                        return false;
                    }
                    const auto& region = process.driver_regions().front();
                    const auto width
                        = signals[value.signal].initial_value.width();
                    if (region.signal != owner.signal || !region.whole
                        || region.offset != 0U
                        || (region.width != 0U && region.width != width)
                        || owner.width != width) {
                        return false;
                    }
                    output = MaskedAllActiveOutput {
                        value.signal, 0U, static_cast<std::uint32_t>(width),
                        false
                    };
                    return true;
                } else if constexpr (std::is_same_v<Type,
                                     WriteUpdateSlice>) {
                    if (output || owner.width == 0U
                        || value.signal != owner.signal
                        || value.offset != owner.offset
                        || value.signal >= signals.size()
                        || signals[value.signal].value_kind
                            != ValueKind::logic4) {
                        return false;
                    }
                    const auto& region = process.driver_regions().front();
                    if (region.signal != owner.signal || region.whole
                        || region.offset != owner.offset
                        || region.width != owner.width) {
                        return false;
                    }
                    output = MaskedAllActiveOutput {
                        owner.signal, owner.offset, owner.width, false
                    };
                    return true;
                } else if constexpr (std::is_same_v<Type, WriteProjected>) {
                    if (output || value.delay != 0U || value.rejection != 0U
                        || value.mode != ProjectedDelayMode::inertial
                        || value.signal >= signals.size()
                        || signals[value.signal].value_kind
                            != ValueKind::logic4) {
                        return false;
                    }
                    const auto& region = process.driver_regions().front();
                    const auto width
                        = signals[value.signal].initial_value.width();
                    if (region.signal != value.signal || !region.whole
                        || region.offset != 0U
                        || (region.width != 0U && region.width != width)) {
                        return false;
                    }
                    output = MaskedAllActiveOutput {
                        value.signal, 0U, static_cast<std::uint32_t>(width),
                        true
                    };
                    return true;
                }
                return false;
            }, operation);
            if (!accepted) {
                return std::nullopt;
            }
        }
        return output;
    };

    for (std::size_t cohort_id = 0U;
         cohort_id < static_sensitivity_cohorts.size(); ++cohort_id) {
        const auto& source = static_sensitivity_cohorts[cohort_id];
        if (source.members.size() < 2U) {
            continue;
        }
        ++admission[0];
        auto& plan = fused_static_cohorts[cohort_id];
        std::set<SignalId> outputs;
        std::vector<SignalId> output_order;
        std::map<SignalId, std::vector<std::uint64_t>> write_masks;
        std::map<SignalId,
            std::map<ProcessId, std::vector<std::uint64_t>>>
            member_write_masks;
        std::map<SignalId, ProcessId> projected_owners;
        std::uint64_t owned_stage_calls { };
        bool all_projected_members = true;
        std::optional<bool> masked_all_active_route;
        bool masked_all_active_eligible = true;
        bool valid = true;
        for (const auto id : source.members) {
            const auto& process = processes.program_view(id);
            if (id >= owned_driver_spans.size()
                || !generic_scheduling_operations(process)) {
                valid = false;
                break;
            }
            auto owner = owned_driver_spans[id];
            if ((owner.signal >= signals.size() || owner.width == 0U)
                && process.driver_regions().size() == 1U) {
                const auto& region = process.driver_regions().front();
                if (region.signal < signals.size()) {
                    const auto width
                        = signals[region.signal].initial_value.width();
                    if (region.whole) {
                        owner = { region.signal, 0U,
                            static_cast<std::uint32_t>(width) };
                    } else if (region.width != 0U
                        && region.offset <= width
                        && region.width <= width - region.offset) {
                        owner = { region.signal, region.offset, region.width };
                    }
                }
            }
            if (owner.signal >= signals.size() || owner.width == 0U) {
                valid = false;
                break;
            }
            const bool pure_static = pure_static_body(process);
            plan.resume_instructions.push_back(
                static_cast<InstructionIndex>(
                    process.operations().size() - 1U));
            const auto masked_output = masked_all_active_body(process, owner);
            masked_all_active_eligible &= masked_output.has_value();
            const bool masked_all_active
                = !pure_static && masked_output.has_value();
            if (!pure_static && !masked_output) {
                valid = false;
                break;
            }
            if (masked_all_active_route
                && *masked_all_active_route != masked_all_active) {
                valid = false;
                break;
            }
            masked_all_active_route = masked_all_active;
            if (masked_all_active) {
                const auto signal = masked_output->signal;
                const auto width = signals[signal].initial_value.width();
                if (masked_output->offset > width
                    || masked_output->width > width - masked_output->offset) {
                    valid = false;
                    break;
                }
                auto& mask = write_masks[signal];
                mask.resize((width + 63U) / 64U);
                auto& member_mask = member_write_masks[signal][id];
                member_mask.resize(mask.size());
                for (std::size_t bit = masked_output->offset;
                     bit < static_cast<std::size_t>(masked_output->offset)
                         + masked_output->width; ++bit) {
                    const auto bit_mask = UINT64_C(1) << (bit % 64U);
                    mask[bit / 64U] |= bit_mask;
                    member_mask[bit / 64U] |= bit_mask;
                }
                if (masked_output->projected
                    && !projected_owners.try_emplace(signal, id).second) {
                    valid = false;
                    break;
                }
                all_projected_members &= masked_output->projected;
                if (outputs.insert(signal).second) {
                    output_order.push_back(signal);
                }
                owned_stage_calls += !masked_output->projected;
                continue;
            }
            auto widths = std::vector<std::uint32_t>(
                process.register_count(), 0U);
            std::set<SignalId> written_signals;
            std::size_t projected_write_count { };
            std::size_t write_operation_count { };
            const auto known = [&](const RegisterId register_id) {
                return register_id < widths.size()
                    && widths[register_id] != 0U;
            };
            const auto mark_write = [&](const SignalId signal,
                                        const RegisterId source_register,
                                        const std::uint32_t offset,
                                        const bool projected = false) {
                if (!known(source_register) || signal >= signals.size()
                    || (!projected
                        && (owner.signal != signal
                            || offset < owner.offset
                            || offset - owner.offset > owner.width
                            || widths[source_register]
                                > owner.width - (offset - owner.offset)))) {
                    return false;
                }
                const auto width = signals[signal].initial_value.width();
                if (offset > width
                    || widths[source_register] > width - offset) {
                    return false;
                }
                auto& masks = write_masks[signal];
                masks.resize((width + 63U) / 64U);
                auto& member_mask = member_write_masks[signal][id];
                member_mask.resize(masks.size());
                for (std::size_t bit = offset;
                     bit < static_cast<std::size_t>(offset)
                         + widths[source_register]; ++bit) {
                    const auto bit_mask = UINT64_C(1) << (bit % 64U);
                    masks[bit / 64U] |= bit_mask;
                    member_mask[bit / 64U] |= bit_mask;
                }
                if (outputs.insert(signal).second) {
                    output_order.push_back(signal);
                }
                written_signals.insert(signal);
                return true;
            };
            for (std::size_t index = 0U;
                 index + 2U < process.operations().size(); ++index) {
                const auto operation = process.operations().expanded(index);
                if (const auto* load
                    = operation_get_if<LoadConstant>(&operation)) {
                    valid = load->destination < widths.size();
                    if (valid) {
                        widths[load->destination] = static_cast<std::uint32_t>(
                            load->value.width());
                    }
                } else if (const auto* read
                    = operation_get_if<ReadSignal>(&operation)) {
                    valid = read->destination < widths.size()
                        && read->signal < signals.size()
                        && !has_container_signal_alias(read->signal)
                        && (signals[read->signal].value_kind
                                == ValueKind::logic4
                            || signals[read->signal].value_kind
                                == ValueKind::logic9);
                    if (valid) {
                        widths[read->destination] = static_cast<std::uint32_t>(
                            signals[read->signal].initial_value.width());
                    }
                } else if (const auto* copy
                    = operation_get_if<CopyRegister>(&operation)) {
                    valid = copy->destination < widths.size()
                        && known(copy->source);
                    if (valid) {
                        widths[copy->destination] = widths[copy->source];
                    }
                } else if (const auto* extract
                    = operation_get_if<Extract>(&operation)) {
                    valid = extract->destination < widths.size()
                        && known(extract->source)
                        && extract->offset <= widths[extract->source]
                        && extract->width
                            <= widths[extract->source] - extract->offset;
                    if (valid) {
                        widths[extract->destination] = extract->width;
                    }
                } else if (const auto* concat
                    = operation_get_if<Concatenate>(&operation)) {
                    valid = concat->destination < widths.size()
                        && concat->width != 0U;
                    std::uint64_t total { };
                    for (const auto operand : concat->operands) {
                        valid &= known(operand);
                        if (valid) {
                            total += widths[operand];
                        }
                    }
                    valid &= total == concat->width;
                    if (valid) {
                        widths[concat->destination] = concat->width;
                    }
                } else if (const auto* binary
                    = operation_get_if<Binary>(&operation)) {
                    valid = binary->destination < widths.size()
                        && known(binary->lhs) && known(binary->rhs)
                        && widths[binary->lhs] == widths[binary->rhs];
                    if (valid) {
                        widths[binary->destination] = widths[binary->lhs];
                    }
                } else if (const auto* reduction
                    = operation_get_if<Reduction>(&operation)) {
                    valid = reduction->destination < widths.size()
                        && known(reduction->source);
                    if (valid) {
                        widths[reduction->destination] = 1U;
                    }
                } else if (const auto* write
                    = operation_get_if<WriteUpdate>(&operation)) {
                    valid = write->signal < signals.size()
                        && known(write->source)
                        && widths[write->source]
                            == signals[write->signal].initial_value.width()
                        && mark_write(write->signal, write->source, 0U);
                    write_operation_count += valid;
                } else if (const auto* slice
                    = operation_get_if<WriteUpdateSlice>(&operation)) {
                    valid = mark_write(
                        slice->signal, slice->source, slice->offset);
                    write_operation_count += valid;
                } else if (const auto* projected
                    = operation_get_if<WriteProjected>(&operation)) {
                    valid = projected->delay == 0U
                        && projected->rejection == 0U
                        && projected->mode == ProjectedDelayMode::inertial
                        && projected->signal < signals.size()
                        && known(projected->source)
                        && widths[projected->source]
                            == signals[projected->signal].initial_value.width()
                        && process.driver_regions().size() == 1U
                        && process.driver_regions().front().signal
                            == projected->signal
                        && process.driver_regions().front().whole
                        && mark_write(projected->signal,
                            projected->source, 0U, true);
                    if (valid) {
                        ++projected_write_count;
                        ++write_operation_count;
                        valid = projected_owners.try_emplace(
                            projected->signal, id).second;
                    }
                }
                if (!valid) {
                    break;
                }
            }
            if (!valid || written_signals.empty()
                || (projected_write_count != 0U
                    && (projected_write_count != 1U
                        || write_operation_count != 1U
                        || written_signals.size() != 1U))) {
                valid = false;
                break;
            }
            all_projected_members &= projected_write_count == 1U
                && write_operation_count == 1U;
            owned_stage_calls += written_signals.size();
        }
        if (!valid || outputs.empty()
            || (!projected_owners.empty()
                && (projected_owners.size() != outputs.size()
                    || !all_projected_members))) {
            ++admission[1];
            continue;
        }
        const bool projected_route = !projected_owners.empty();
        if ((masked_all_active_route.value_or(false)
                || masked_all_active_eligible)
            && projected_route
            && output_order.size() != source.members.size()) {
            if (masked_all_active_route.value_or(false)) {
                ++admission[2];
                continue;
            }
            masked_all_active_eligible = false;
        }
        plan.owner_stage_calls_total = projected_route
            ? 0U : owned_stage_calls;
        for (const auto signal : output_order) {
            if (signal >= signals.size()
                || get_signal(signal).event_variable
                || signal_transaction_observed[signal]
                || native_signal_has_runtime_dependency(signal)
                || has_dynamic_waits(signal)
                || !signal_container_aliases[signal].empty()
                || forced_values[signal] || forced_driver_values[signal]
                || external_driver_values[signal]
                || monitor_watches(signal)) {
                valid = false;
                break;
            }
            if (native_signal_observation_required_hook
                && native_signal_observation_required_hook(signal)) {
                valid = false;
                break;
            }
            const auto width = get_signal(signal).initial_value.width();
            if (projected_route) {
                const auto owner_id = projected_owners.at(signal);
                const auto& table = driver_values[signal];
                const auto* record = table.find(owner_id);
                const auto& state = get_signal(signal);
                const bool unresolved_logic4
                    = state.value_kind == ValueKind::logic4
                    && state.resolution == ResolutionKind::none;
                const bool resolved_logic9
                    = state.value_kind == ValueKind::logic9
                    && state.resolution == ResolutionKind::std_logic;
                if ((unresolved_logic4
                        && (signal_writer_counts[signal] != 1U
                            || stable_single_writer_processes[signal]
                                != owner_id))
                    || (resolved_logic9
                        && (record == nullptr || table.size() != 1U
                            || record->strength != DriveStrength { }
                            || record->value.width()
                                != state.initial_value.width()))
                    || (!unresolved_logic4 && !resolved_logic9)) {
                    valid = false;
                    break;
                }
            } else if (owned_driver_active(signal)) {
                // The static cohort executor publishes only through the
                // original-owner A4 route. Signals that still require the
                // legacy aggregate-owned composite remain on checked process
                // execution.
                valid = false;
                break;
            } else {
                RegionAuthoritativeComponentState* authoritative_state { };
                if (get_signal(signal).value_kind != ValueKind::logic4
                    || !can_try_wide_disjoint_signal_commit(
                        signal, &authoritative_state)
                    || authoritative_state == nullptr
                    || !authoritative_state->values().layout().contains(
                        signal)) {
                    valid = false;
                    break;
                }
                const auto& layout = authoritative_state->values().layout();
                const auto layout_owners = layout.owners(signal);
                const auto member_owners = member_write_masks.find(signal);
                if (layout_owners.size() < 2U
                    || member_owners == member_write_masks.end()
                    || member_owners->second.size() < 2U) {
                    valid = false;
                    break;
                }
                auto selected_union
                    = std::vector<std::uint64_t>(write_masks.at(signal).size(),
                        0U);
                auto disjoint_owners
                    = std::vector<FusedStaticCohortPlan::Output::DisjointOwner> { };
                disjoint_owners.reserve(member_owners->second.size());
                for (const auto& [process, member_mask]
                    : member_owners->second) {
                    const auto layout_owner = std::ranges::find(
                        layout_owners, process,
                        &SignalDriverOwnerLayout::process);
                    const auto layout_mask = layout.owner_mask_words(
                        signal, process);
                    if (layout_owner == layout_owners.end()
                        || member_mask.size() != selected_union.size()
                        || layout_mask.size() != member_mask.size()
                        || !std::ranges::equal(layout_mask, member_mask)
                        || !can_try_wide_disjoint_owner_commit(
                            process, signal)) {
                        valid = false;
                        break;
                    }
                    const auto* const record
                        = driver_values[signal].find(process);
                    if (record == nullptr
                        || record->strength != DriveStrength { }
                        || record->value.width() != width
                        || record->value.is_logic9()) {
                        valid = false;
                        break;
                    }
                    for (std::size_t word = 0U;
                         word < member_mask.size(); ++word) {
                        if ((selected_union[word] & member_mask[word]) != 0U) {
                            valid = false;
                            break;
                        }
                        selected_union[word] |= member_mask[word];
                    }
                    if (!valid) {
                        break;
                    }
                    auto& staged_owner = disjoint_owners.emplace_back();
                    staged_owner.process = process;
                    staged_owner.mask = member_mask;
                    staged_owner.staged_value = PackedLogic4 {
                        width, Logic4::z
                    };
                }
                if (!valid || disjoint_owners.size() < 2U
                    || selected_union != write_masks.at(signal)) {
                    valid = false;
                    break;
                }
                auto& output = plan.outputs.emplace_back();
                output.signal = signal;
                output.width = static_cast<std::uint32_t>(width);
                output.owner_masks = write_masks.at(signal);
                output.disjoint_owners = std::move(disjoint_owners);
                output.route = FusedStaticCohortPlan::Output::Route::
                    disjoint_owner_group_logic4;
            }
            if (!valid) {
                break;
            }
            const auto output_found = std::ranges::find(plan.outputs, signal,
                &FusedStaticCohortPlan::Output::signal);
            if (output_found == plan.outputs.end()) {
                auto& output = plan.outputs.emplace_back();
                output.signal = signal;
                output.width = static_cast<std::uint32_t>(width);
                output.owner_masks = write_masks.at(signal);
                if (projected_route) {
                    output.original_owner = projected_owners.at(signal);
                    output.route = get_signal(signal).value_kind
                            == ValueKind::logic9
                        ? FusedStaticCohortPlan::Output::Route::projected_logic9
                        : FusedStaticCohortPlan::Output::Route::projected_logic4;
                }
            }
            bool private_signal = !observed[signal]
                && !readers[signal].empty();
            for (const auto reader : readers[signal]) {
                if (!pure_static_body(processes.program_view(reader))) {
                    private_signal = false;
                }
            }
            if (private_signal) {
                plan.candidate.private_outputs.push_back(signal);
            } else {
                plan.candidate.boundary_outputs.push_back(signal);
            }
        }
        if (!valid) {
            ++admission[2];
            continue;
        }
        ++admission[3];
        plan.candidate.cohort_id = cohort_id;
        plan.candidate.members = source.members;
        plan.candidate.outputs = std::move(output_order);
        plan.candidate.projected = projected_route;
        plan.candidate.masked_all_active
            = masked_all_active_route.value_or(false);
        plan.candidate.masked_all_active_eligible
            = masked_all_active_eligible;
        assert(!plan.certified);
        plan.certified = true;
        ++fused_static_certified_plan_count;
        ++fused_static_counts.candidates;
    }
    if (trace) {
        std::cerr << "fsim fused-static admission: multi=" << admission[0]
                  << " body_or_owner=" << admission[1]
                  << " output=" << admission[2]
                  << " certified=" << admission[3] << '\n';
    }
}

void Interpreter::Impl::invalidate_fused_static_cohorts() noexcept
{
    for (auto& plan : fused_static_cohorts) {
        if (plan.certified) {
            assert(fused_static_certified_plan_count != 0U);
            --fused_static_certified_plan_count;
            plan.certified = false;
        }
    }
    assert(fused_static_certified_plan_count == 0U);
}

void Interpreter::Impl::invalidate_fused_static_cohorts_for_fork(
    const ProcessId child)
{
    const auto& program = processes.at(child).program();
    const auto log_fork = [&](const std::size_t same_cohort,
                              const std::size_t output_writer,
                              const std::size_t private_reader,
                              const std::size_t survivors) {
        if (!fused_static_trace_enabled) {
            return;
        }
        std::cerr << "fsim fused-static fork: child=" << child
                  << " time=" << scheduler.now()
                  << " delta=" << scheduler.delta()
                  << " same_cohort=" << same_cohort
                  << " output_writer=" << output_writer
                  << " private_reader=" << private_reader
                  << " surviving=" << survivors << '\n';
    };
    if (fused_static_certified_plan_count == 0U) {
        if (fused_static_counters_enabled) {
            ++fused_static_counts.fork_events;
            fused_static_counts.fork_plans_surviving_after_last = 0U;
        }
        log_fork(0U, 0U, 0U, 0U);
        return;
    }
    auto writes = std::vector<SignalId> { };
    auto reads = std::vector<SignalId> { };
    const auto mark_signal = [&](auto& touched, const SignalId signal) {
        if (signal < signals.size()) {
            touched.push_back(signal);
        }
    };
    for (const auto& region : program.driver_regions()) {
        mark_signal(writes, region.signal);
    }
    for (const auto& sensitivity : program.static_sensitivity()) {
        mark_signal(reads, sensitivity.signal);
    }
    for (std::size_t index = 0U; index < program.operations().size();
         ++index) {
        const auto operation = program.operations().expanded(index);
        if (const auto signal = output_signal(operation)) {
            mark_signal(writes, *signal);
        }
        visit_operation([&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, ReadSignal>) {
                mark_signal(reads, value.signal);
                if (value.clock) {
                    mark_signal(reads, *value.clock);
                }
                if (value.gate) {
                    mark_signal(reads, *value.gate);
                }
            } else if constexpr (std::is_same_v<Type, WaitOn>
                || std::is_same_v<Type, WaitPla>) {
                for (const auto signal : value.signals) {
                    mark_signal(reads, signal);
                }
            } else if constexpr (std::is_same_v<Type, WaitOrder>) {
                for (const auto signal : value.events) {
                    mark_signal(reads, signal);
                }
            } else if constexpr (std::is_same_v<Type, MonitorInstall>) {
                for (const auto& item : value.values) {
                    if (item.kind == MonitorValueKind::signal) {
                        mark_signal(reads, item.signal);
                    }
                }
            } else if constexpr (std::is_same_v<Type, EventTriggered>) {
                mark_signal(reads, value.event);
            } else if constexpr (std::is_same_v<Type, EventAlias>) {
                mark_signal(reads, value.target);
                if (value.has_source) {
                    mark_signal(reads, value.source);
                }
            } else if constexpr (requires { value.signal; }) {
                mark_signal(reads, value.signal);
            }
        }, operation);
    }
    std::ranges::sort(writes);
    writes.erase(std::ranges::unique(writes).begin(), writes.end());
    std::ranges::sort(reads);
    reads.erase(std::ranges::unique(reads).begin(), reads.end());

    const auto child_cohort = child < static_sensitivity_cohort_by_process.size()
        ? static_sensitivity_cohort_by_process[child]
        : std::numeric_limits<std::size_t>::max();
    std::size_t same_cohort { };
    std::size_t output_writer { };
    std::size_t private_reader { };
    std::size_t survivors { };
    for (std::size_t cohort = 0U; cohort < fused_static_cohorts.size();
         ++cohort) {
        auto& plan = fused_static_cohorts[cohort];
        if (!plan.certified) {
            continue;
        }
        const bool joins_cohort = child_cohort == cohort;
        const bool writes_output = std::ranges::any_of(
            plan.candidate.outputs,
            [&](const SignalId signal) {
                return std::ranges::binary_search(writes, signal);
            });
        const bool reads_private = std::ranges::any_of(
            plan.candidate.private_outputs,
            [&](const SignalId signal) {
                return std::ranges::binary_search(reads, signal);
            });
        if (joins_cohort || writes_output || reads_private) {
            assert(fused_static_certified_plan_count != 0U);
            --fused_static_certified_plan_count;
            plan.certified = false;
            same_cohort += joins_cohort;
            output_writer += writes_output;
            private_reader += reads_private;
            if (fused_static_counters_enabled) {
                ++fused_static_counts.fork_plans_invalidated;
            }
        } else {
            ++survivors;
        }
    }
    if (fused_static_counters_enabled) {
        ++fused_static_counts.fork_events;
        fused_static_counts.fork_plans_surviving_after_last = survivors;
    }
    assert(fused_static_certified_plan_count == survivors);
    log_fork(same_cohort, output_writer, private_reader, survivors);
}

std::optional<std::size_t>
Interpreter::Impl::try_execute_fused_static_cohort(
    const std::span<const std::uint64_t> task_payloads,
    std::size_t& offered_tasks)
{
    offered_tasks = 0U;
    if (!process_signal_access_inventory_complete
        || task_payloads.empty() || !static_phase_batches_enabled
        || process_profile_enabled || update_profile_enabled
        || execution_point_hook
        || (native_signal_observation_any_hook
            && native_signal_observation_any_hook())) {
        return std::nullopt;
    }
    const auto raw = task_payloads.front();
    if (raw >= fused_static_cohorts.size()) {
        return std::nullopt;
    }
    const auto cohort = static_cast<std::size_t>(raw);
    auto& plan = fused_static_cohorts[cohort];
    if (!plan.certified || !plan.executor) {
        return std::nullopt;
    }
    auto& source = static_sensitivity_cohorts[cohort];
    // Queueing retained the cohort's capacity after snapshot publication.
    // Reuse that storage instead of allocating/copying into a fresh vector
    // for every hot activation.
    auto& ready = source.ready;
    if (!cohort_snapshots.copy_pending(source.pending, ready)
        || ready != plan.candidate.members) {
        if (fused_static_counters_enabled) {
            ++fused_static_counts.fallbacks;
        }
        return std::nullopt;
    }
    for (std::size_t index = 0U; index < ready.size(); ++index) {
        const auto& state = processes[ready[index]];
        if (!state.queued || !state.waiting_on_static
            || state.status != ProcessStatus::waiting
            || state.suspended || state.halted
            || state.pc != plan.resume_instructions[index]
            || state.has_callable_frame_push) {
            if (fused_static_counters_enabled) {
                ++fused_static_counts.fallbacks;
            }
            return std::nullopt;
        }
    }
    for (const auto& output : plan.outputs) {
        const bool disjoint_owner_group = output.route
            == FusedStaticCohortPlan::Output::Route::
                disjoint_owner_group_logic4;
        const bool unresolved_logic4 = output.route
            == FusedStaticCohortPlan::Output::Route::projected_logic4;
        const auto* record = disjoint_owner_group || unresolved_logic4
            ? nullptr : driver_values[output.signal].find(output.original_owner);
        bool route_valid = true;
        if (disjoint_owner_group) {
            RegionAuthoritativeComponentState* authoritative_state { };
            route_valid = !owned_driver_active(output.signal)
                && get_signal(output.signal).value_kind == ValueKind::logic4
                && can_try_wide_disjoint_signal_commit(
                    output.signal, &authoritative_state)
                && authoritative_state != nullptr
                && output.disjoint_owners.size() >= 2U;
            if (route_valid) {
                const auto& layout = authoritative_state->values().layout();
                route_valid = layout.contains(output.signal);
                for (const auto& owner : output.disjoint_owners) {
                    if (!route_valid) {
                        break;
                    }
                    const auto* const owner_record
                        = driver_values[output.signal].find(owner.process);
                    const auto layout_mask = layout.owner_mask_words(
                        output.signal, owner.process);
                    route_valid = can_try_wide_disjoint_owner_commit(
                            owner.process, output.signal)
                        && owner.mask.size() == output.owner_masks.size()
                        && layout_mask.size() == owner.mask.size()
                        && std::ranges::equal(layout_mask, owner.mask)
                        && owner_record != nullptr
                        && owner_record->strength == DriveStrength { }
                        && owner_record->value.width() == output.width
                        && !owner_record->value.is_logic9()
                        && owner.staged_value.width() == output.width
                        && !owner.staged_value.is_logic9();
                }
            }
        } else if (unresolved_logic4) {
            route_valid = signal_writer_counts[output.signal] == 1U
                && stable_single_writer_processes[output.signal]
                    == output.original_owner;
        } else {
            route_valid = record != nullptr
                && driver_values[output.signal].size() == 1U;
        }
        if (!route_valid
            || (unresolved_logic4
                && (signal_writer_counts[output.signal] != 1U
                    || stable_single_writer_processes[output.signal]
                        != output.original_owner))
            || (!disjoint_owner_group
                && (forced_values[output.signal]
                    || forced_driver_values[output.signal]
                    || external_driver_values[output.signal]))
            || (!disjoint_owner_group && !unresolved_logic4
                && (record == nullptr
                    || driver_values[output.signal].size() != 1U))) {
            if (fused_static_counters_enabled) {
                ++fused_static_counts.fallbacks;
            }
            return std::nullopt;
        }
    }

    const auto owner_group_output_count = static_cast<std::size_t>(
        std::ranges::count_if(plan.outputs, [](const auto& output) {
            return output.route
                == FusedStaticCohortPlan::Output::Route::
                    disjoint_owner_group_logic4;
        }));
    if (owner_group_output_count != 0U) {
        try {
            if (driver_update_scratch.size() < signals.size()) {
                driver_update_scratch.resize(signals.size());
            }
            if (resolved_update_marked.size() < signals.size()) {
                resolved_update_marked.resize(signals.size());
            }
            driver_update_signals.reserve(
                driver_update_signals.size() + owner_group_output_count);
            resolved_update_signals.reserve(
                resolved_update_signals.size() + owner_group_output_count);
            for (auto& output : plan.outputs) {
                if (output.route
                    != FusedStaticCohortPlan::Output::Route::
                        disjoint_owner_group_logic4) {
                    continue;
                }
                auto& staged = driver_update_scratch[output.signal];
                if (staged.empty()) {
                    if (resolved_update_marked[output.signal]
                        || std::ranges::find(driver_update_signals,
                               output.signal)
                            != driver_update_signals.end()
                        || std::ranges::find(resolved_update_signals,
                               output.signal)
                            != resolved_update_signals.end()
                        || std::ranges::any_of(pending_updates,
                               [&](const auto& pending) {
                                   return pending.signal == output.signal;
                               })) {
                        if (fused_static_counters_enabled) {
                            ++fused_static_counts.fallbacks;
                        }
                        return std::nullopt;
                    }
                    staged.reserve(output.disjoint_owners.size());
                    // A prior checked fallback may have moved a copy of
                    // this value into the raw DriverRecord. Detach and
                    // reset the plan-owned scratch before entering the
                    // optional executor so the post-execution fill/set
                    // sequence cannot allocate. A retained retry group
                    // takes the other branch and keeps its exact values.
                    for (auto& owner : output.disjoint_owners) {
                        owner.staged_value.fill(Logic4::z);
                    }
                    continue;
                }
                bool retryable_group { !update_commit_scheduled
                    && staged.size() == output.disjoint_owners.size()
                    && resolved_update_marked[output.signal]
                    && std::ranges::find(driver_update_signals,
                           output.signal)
                        != driver_update_signals.end()
                    && std::ranges::find(resolved_update_signals,
                           output.signal)
                        != resolved_update_signals.end() };
                for (const auto& owner : output.disjoint_owners) {
                    const auto update = std::ranges::find(staged,
                        std::optional<ProcessId> { owner.process },
                        &PendingDriverCommit::driver);
                    retryable_group &= update != staged.end()
                        && update->fused_cohort == cohort
                        && !update->owned_composite
                        && update->value.width() == output.width
                        && !update->value.is_logic9();
                }
                if (!retryable_group) {
                    if (fused_static_counters_enabled) {
                        ++fused_static_counts.fallbacks;
                    }
                    return std::nullopt;
                }
            }
        } catch (const std::bad_alloc&) {
            if (fused_static_counters_enabled) {
                ++fused_static_counts.fallbacks;
            }
            return std::nullopt;
        }
    }

    ExecutionContext context { *this, ready.front() };
    const ProcessCohortNativeContext native_context {
        this,
        context.direct_signal_aval(),
        context.direct_signal_bval(),
        context.signal_writer_revision(),
        context.supports_direct_word_updates(),
        context.execution_points_enabled(),
        context.direct_wide_signal_aval(),
        context.direct_wide_signal_bval(),
        context.direct_wide_signal_offsets(),
        context.direct_signal_logic9_plane0(),
        context.direct_signal_logic9_plane1(),
        context.direct_signal_logic9_plane2(),
        context.direct_signal_logic9_plane3(),
        context.direct_wide_signal_logic9_plane2(),
        context.direct_wide_signal_logic9_plane3(),
    };
    if (!native_context.supports_direct_word_updates
        || native_context.execution_points_enabled) {
        if (fused_static_counters_enabled) {
            ++fused_static_counts.fallbacks;
        }
        return std::nullopt;
    }

    // A declined executor has made no mutations. Once it accepts, every
    // failure retires the original task and its queued members, as with the
    // existing native batch continuation contract.
    offered_tasks = 1U;
    bool completion_available { };
    try {
        std::optional<FusedStaticCohortResume> completion;
        if (plan.candidate.masked_all_active) {
            const auto member_count = plan.candidate.members.size();
            const auto expected_words = member_count / 64U
                + static_cast<std::size_t>(member_count % 64U != 0U);
            if (member_count == 0U
                || plan.candidate.masked_all_active_words.size()
                    != expected_words) {
                offered_tasks = 0U;
                if (fused_static_counters_enabled) {
                    ++fused_static_counts.fallbacks;
                }
                return std::nullopt;
            }
            completion = plan.executor->resume_masked_all_active(
                native_context,
                plan.candidate.masked_all_active_words);
        } else {
            completion = plan.executor->resume(native_context);
        }
        if (!completion) {
            offered_tasks = 0U;
            if (fused_static_counters_enabled) {
                ++fused_static_counts.fallbacks;
            }
            return std::nullopt;
        }
        completion_available = true;
        if (plan.candidate.projected) {
            if (!completion->aggregate_slots.empty()
                || completion->projected_writes.size()
                    != plan.outputs.size()) {
                throw std::logic_error {
                    "fused cohort returned an incomplete projected write set"
                };
            }
            for (std::size_t index = 0U;
                 index < completion->projected_writes.size(); ++index) {
                const auto& write = completion->projected_writes[index];
                const auto& output = plan.outputs[index];
                const bool logic9 = output.route
                    == FusedStaticCohortPlan::Output::Route::projected_logic9;
                if (write.signal != output.signal
                    || write.value.width() != output.width
                    || write.value.is_logic9() != logic9) {
                    throw std::logic_error {
                        "fused cohort returned an invalid projected write"
                    };
                }
            }
            for (std::size_t index = 0U;
                 index < completion->projected_writes.size(); ++index) {
                schedule_projected(plan.outputs[index].original_owner,
                    completion->projected_writes[index].signal,
                    completion->projected_writes[index].value,
                    std::nullopt, 0U, 0U,
                    ProjectedDelayMode::inertial);
            }
        } else {
            if (!completion->projected_writes.empty()
                || completion->aggregate_slots.size()
                    != plan.outputs.size()) {
                throw std::logic_error {
                    "fused cohort returned an incomplete aggregate slot set"
                };
            }
            for (std::size_t index = 0U;
                 index < completion->aggregate_slots.size(); ++index) {
                const auto& slot = completion->aggregate_slots[index];
                const auto& output = plan.outputs[index];
                bool valid_slot = slot.signal == output.signal;
                if (valid_slot
                    && output.route
                        == FusedStaticCohortPlan::Output::Route::
                            disjoint_owner_group_logic4) {
                    valid_slot = slot.width == output.width
                        && slot.word_count == output.owner_masks.size()
                        && slot.active != nullptr && slot.aval != nullptr
                        && slot.bval != nullptr && slot.mask != nullptr
                        && *slot.active != 0U
                        && !signal_transaction_observed[slot.signal];
                    for (std::size_t word = 0U;
                         valid_slot && word < slot.word_count; ++word) {
                        const auto begin = word * 64U;
                        const auto size = std::min<std::size_t>(
                            64U, slot.width - begin);
                        const auto valid = size == 64U
                            ? std::numeric_limits<std::uint64_t>::max()
                            : (UINT64_C(1) << size) - 1U;
                        valid_slot = (slot.mask[word] & valid)
                                == output.owner_masks[word]
                            && (slot.mask[word] & ~valid) == 0U;
                    }
                } else {
                    valid_slot = false;
                }
                if (!valid_slot) {
                    throw std::logic_error {
                        "fused cohort returned an invalid aggregate slot"
                    };
                }
            }
            if (driver_update_scratch.size() < signals.size()) {
                driver_update_scratch.resize(signals.size());
                resolved_update_marked.resize(signals.size());
            }
            bool owner_group_staged { };
            for (const auto& slot : completion->aggregate_slots) {
                auto& output = *std::ranges::find(plan.outputs,
                    slot.signal, &FusedStaticCohortPlan::Output::signal);
                if (output.route
                    == FusedStaticCohortPlan::Output::Route::
                        disjoint_owner_group_logic4) {
                    auto& staged = driver_update_scratch[slot.signal];
                    if (!staged.empty()) {
                        for (const auto& owner : output.disjoint_owners) {
                            const auto update = std::ranges::find(staged,
                                std::optional<ProcessId> { owner.process },
                                &PendingDriverCommit::driver);
                            if (update == staged.end()
                                || update->fused_cohort != cohort
                                || update->owned_composite) {
                                throw std::logic_error {
                                    "fused owner-group retry lost its staging rows"
                                };
                            }
                            for (std::size_t bit = 0U;
                                 bit < output.width; ++bit) {
                                const auto mask_word = bit / 64U;
                                const auto in_word
                                    = static_cast<unsigned>(bit % 64U);
                                const auto expected = [&]() {
                                    if ((owner.mask[mask_word]
                                            & (UINT64_C(1) << in_word)) == 0U) {
                                        return Logic4::z;
                                    }
                                    const auto aval
                                        = (slot.aval[mask_word] >> in_word)
                                        & 1U;
                                    const auto bval
                                        = (slot.bval[mask_word] >> in_word)
                                        & 1U;
                                    return bval != 0U
                                        ? (aval != 0U ? Logic4::x : Logic4::z)
                                        : (aval != 0U
                                                ? Logic4::one : Logic4::zero);
                                }();
                                if (update->value.get(bit) != expected) {
                                    throw std::logic_error {
                                        "fused owner-group retry changed its slot"
                                    };
                                }
                            }
                        }
                        owner_group_staged = true;
                        if (fused_static_counters_enabled) {
                            ++fused_static_counts.aggregate_signals_staged;
                        }
                        continue;
                    }

                    const auto decode_bit = [&](const std::size_t bit) {
                        const auto word = bit / 64U;
                        const auto in_word
                            = static_cast<unsigned>(bit % 64U);
                        const auto aval
                            = (slot.aval[word] >> in_word) & 1U;
                        const auto bval
                            = (slot.bval[word] >> in_word) & 1U;
                        return bval != 0U
                            ? (aval != 0U ? Logic4::x : Logic4::z)
                            : (aval != 0U
                                    ? Logic4::one : Logic4::zero);
                    };
                    for (auto& owner : output.disjoint_owners) {
                        auto& owner_value = owner.staged_value;
                        owner_value.fill(Logic4::z);
                        for (std::size_t bit = 0U;
                             bit < output.width; ++bit) {
                            if ((owner.mask[bit / 64U]
                                    & (UINT64_C(1) << (bit % 64U))) != 0U) {
                                owner_value.set(bit, decode_bit(bit));
                            }
                        }
                        staged.emplace_back(owner.process, owner_value,
                            false, cohort);
                    }
                    if (resolved_update_marked[slot.signal]) {
                        throw std::logic_error {
                            "fused owner-group resolved marker was already set"
                        };
                    }
                    driver_update_signals.push_back(slot.signal);
                    resolved_update_signals.push_back(slot.signal);
                    resolved_update_marked[slot.signal] = true;
                    owner_group_staged = true;
                    if (fused_static_counters_enabled) {
                        ++fused_static_counts.aggregate_signals_staged;
                    }
                    continue;
                }
                throw std::logic_error {
                    "fused cohort output has no A4 owner-group route"
                };
            }
            if (owner_group_staged) {
                schedule_update_commit();
            }
        }
        cohort_snapshots.release(source.pending);
        source.pending = { };
        for (std::size_t index = 0U; index < ready.size(); ++index) {
            auto& state = processes[ready[index]];
            state.region_kernel_completion_boundary_validated = false;
            state.queued = false;
            state.pc = plan.resume_instructions[index];
            clear_wait_timeout(state);
            state.status = ProcessStatus::waiting;
            state.waiting_on_static = true;
            state.static_trigger_mask = 0U;
        }
        if (fused_static_counters_enabled) {
            ++fused_static_counts.invocations;
            fused_static_counts.masked_all_active_invocations
                += plan.candidate.masked_all_active;
            fused_static_counts.represented_members += ready.size();
            fused_static_counts.owner_stage_calls_avoided
                += plan.owner_stage_calls_total;
        }
        return 1U;
    } catch (const std::bad_alloc&) {
        if (completion_available && !plan.candidate.projected) {
            // The native body has returned its complete value set, but one
            // or more output slots may not have been staged yet. Keep the
            // original cohort snapshot and queued states so the scheduler
            // can replay this pure fused activation. Staged owner prefixes
            // are idempotent and their pending markers survive the retry.
            offered_tasks = 0U;
            throw;
        }
        cohort_snapshots.release(source.pending);
        source.pending = { };
        for (const auto id : ready) {
            processes[id].queued = false;
        }
        throw;
    } catch (...) {
        cohort_snapshots.release(source.pending);
        source.pending = { };
        for (const auto id : ready) {
            processes[id].queued = false;
        }
        throw;
    }
}

} // namespace fsim::runtime::simir

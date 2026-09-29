// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_execution_context.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
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

bool pure_static_body(const Process& process)
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
        || !operation_holds<WaitSensitivity>(
            process.operations.expanded(count - 2U))) {
        return false;
    }
    const auto tail = process.operations.expanded(count - 1U);
    const auto* jump = operation_get_if<Jump>(&tail);
    if (jump == nullptr || jump->target != 0U) {
        return false;
    }
    for (std::size_t index = 0U; index + 2U < count; ++index) {
        if (!candidate_body_operation(process.operations.expanded(index))) {
            return false;
        }
    }
    const bool uses_logic9 = std::ranges::any_of(
        process.register_value_kinds,
        [](const auto kind) { return kind == ValueKind::logic9; });
    if (uses_logic9) {
        for (std::size_t index = 0U; index + 2U < count; ++index) {
            if (operation_holds<Reduction>(process.operations.expanded(index))) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

void Interpreter::Impl::build_fused_static_cohort_plans()
{
    fused_static_cohorts.clear();
    fused_static_cohorts.resize(static_sensitivity_cohorts.size());
    fused_static_counts = { };
    const bool trace = std::getenv("FSIM_PROFILE_FUSED_STATIC") != nullptr;
    if (process_profile_enabled || execution_point_hook
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

    // Static sensitivities are trigger metadata, not a complete reader set.
    // Inspect every expanded body operation and conservatively mark every
    // non-current, dynamic, or otherwise observable signal reference.
    std::vector<std::vector<ProcessId>> readers(signals.size());
    std::vector<std::uint8_t> observed(signals.size(), 0U);
    const auto observe = [&](const SignalId signal) {
        if (signal < observed.size()) {
            observed[signal] = 1U;
        }
    };
    for (std::size_t id = 0U; id < processes.size(); ++id) {
        const auto& process = processes[id].program();
        for (const auto& sensitivity : process.static_sensitivity) {
            if (sensitivity.edge != EdgeKind::any) {
                observe(sensitivity.signal);
            }
            if (sensitivity.signal < readers.size()) {
                readers[sensitivity.signal].push_back(
                    static_cast<ProcessId>(id));
            }
        }
        for (std::size_t index = 0U; index < process.operations.size();
             ++index) {
            const auto operation = process.operations.expanded(index);
            visit_operation([&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, ReadSignal>) {
                    if (value.signal < readers.size()) {
                        readers[value.signal].push_back(
                            static_cast<ProcessId>(id));
                    }
                    if (value.kind != SignalReadKind::current
                        || value.clock || value.gate) {
                        observe(value.signal);
                    }
                    if (value.clock) {
                        observe(*value.clock);
                    }
                    if (value.gate) {
                        observe(*value.gate);
                    }
                } else if constexpr (std::is_same_v<Type, WaitOn>
                    || std::is_same_v<Type, WaitPla>) {
                    for (const auto signal : value.signals) {
                        observe(signal);
                    }
                } else if constexpr (std::is_same_v<Type, WaitOrder>) {
                    for (const auto signal : value.events) {
                        observe(signal);
                    }
                } else if constexpr (std::is_same_v<Type, MonitorInstall>) {
                    for (const auto& item : value.values) {
                        if (item.kind == MonitorValueKind::signal) {
                            observe(item.signal);
                        }
                    }
                } else if constexpr (std::is_same_v<Type, WriteUpdate>
                    || std::is_same_v<Type, WriteUpdateSlice>
                    || std::is_same_v<Type, WriteProjected>) {
                    // Writers are checked against the owned-driver graph.
                } else if constexpr (requires { value.signal; }) {
                    observe(value.signal);
                }
            }, operation);
        }
    }
    for (auto& signal_readers : readers) {
        std::ranges::sort(signal_readers);
        signal_readers.erase(
            std::ranges::unique(signal_readers).begin(),
            signal_readers.end());
    }

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
        std::map<SignalId, ProcessId> projected_owners;
        std::uint64_t owned_stage_calls { };
        bool all_projected_members = true;
        bool valid = true;
        for (const auto id : source.members) {
            const auto& process = processes[id].program();
            if (!pure_static_body(process)
                || id >= owned_driver_spans.size()) {
                valid = false;
                break;
            }
            plan.resume_instructions.push_back(
                static_cast<InstructionIndex>(
                    process.operations.size() - 1U));
            const auto& owner = owned_driver_spans[id];
            auto widths = std::vector<std::uint32_t>(
                process.register_count, 0U);
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
                for (std::size_t bit = offset;
                     bit < static_cast<std::size_t>(offset)
                         + widths[source_register]; ++bit) {
                    masks[bit / 64U] |= UINT64_C(1) << (bit % 64U);
                }
                if (outputs.insert(signal).second) {
                    output_order.push_back(signal);
                }
                written_signals.insert(signal);
                return true;
            };
            for (std::size_t index = 0U;
                 index + 2U < process.operations.size(); ++index) {
                const auto operation = process.operations.expanded(index);
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
                        && process.driver_regions.size() == 1U
                        && process.driver_regions.front().signal
                            == projected->signal
                        && process.driver_regions.front().whole
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
            } else if (!owned_driver_active(signal)
                || get_signal(signal).value_kind != ValueKind::logic4) {
                valid = false;
                break;
            }
            const auto width = get_signal(signal).initial_value.width();
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
            bool private_signal = !observed[signal]
                && !readers[signal].empty();
            for (const auto reader : readers[signal]) {
                if (!pure_static_body(processes[reader].program())) {
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
        plan.certified = true;
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
        plan.certified = false;
    }
    invalidate_fused_masked_regions();
}

void Interpreter::Impl::invalidate_fused_static_cohorts_for_fork(
    const ProcessId child)
{
    const auto& program = processes.at(child).program();
    auto writes = std::vector<std::uint8_t>(signals.size(), 0U);
    auto reads = std::vector<std::uint8_t>(signals.size(), 0U);
    for (const auto& region : program.driver_regions) {
        if (region.signal < writes.size()) {
            writes[region.signal] = 1U;
        }
    }
    for (const auto& sensitivity : program.static_sensitivity) {
        if (sensitivity.signal < reads.size()) {
            reads[sensitivity.signal] = 1U;
        }
    }
    for (std::size_t index = 0U; index < program.operations.size();
         ++index) {
        const auto operation = program.operations.expanded(index);
        if (const auto signal = output_signal(operation)) {
            if (*signal < writes.size()) {
                writes[*signal] = 1U;
            }
        }
        visit_operation([&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, ReadSignal>) {
                if (value.signal < reads.size()) {
                    reads[value.signal] = 1U;
                }
                if (value.clock && *value.clock < reads.size()) {
                    reads[*value.clock] = 1U;
                }
                if (value.gate && *value.gate < reads.size()) {
                    reads[*value.gate] = 1U;
                }
            } else if constexpr (std::is_same_v<Type, WaitOn>
                || std::is_same_v<Type, WaitPla>) {
                for (const auto signal : value.signals) {
                    if (signal < reads.size()) {
                        reads[signal] = 1U;
                    }
                }
            } else if constexpr (std::is_same_v<Type, WaitOrder>) {
                for (const auto signal : value.events) {
                    if (signal < reads.size()) {
                        reads[signal] = 1U;
                    }
                }
            } else if constexpr (std::is_same_v<Type, MonitorInstall>) {
                for (const auto& item : value.values) {
                    if (item.kind == MonitorValueKind::signal
                        && item.signal < reads.size()) {
                        reads[item.signal] = 1U;
                    }
                }
            } else if constexpr (std::is_same_v<Type, EventTriggered>) {
                if (value.event < reads.size()) {
                    reads[value.event] = 1U;
                }
            } else if constexpr (std::is_same_v<Type, EventAlias>) {
                if (value.target < reads.size()) {
                    reads[value.target] = 1U;
                }
                if (value.has_source && value.source < reads.size()) {
                    reads[value.source] = 1U;
                }
            } else if constexpr (requires { value.signal; }) {
                if (value.signal < reads.size()) {
                    reads[value.signal] = 1U;
                }
            }
        }, operation);
    }

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
            [&](const SignalId signal) { return writes[signal] != 0U; });
        const bool reads_private = std::ranges::any_of(
            plan.candidate.private_outputs,
            [&](const SignalId signal) { return reads[signal] != 0U; });
        if (joins_cohort || writes_output || reads_private) {
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
    if (std::getenv("FSIM_PROFILE_FUSED_STATIC") != nullptr) {
        std::cerr << "fsim fused-static fork: child=" << child
                  << " time=" << scheduler.now()
                  << " delta=" << scheduler.delta()
                  << " same_cohort=" << same_cohort
                  << " output_writer=" << output_writer
                  << " private_reader=" << private_reader
                  << " surviving=" << survivors << '\n';
    }
}

std::optional<std::size_t>
Interpreter::Impl::try_execute_fused_static_cohort(
    const std::span<const std::uint64_t> task_payloads,
    std::size_t& offered_tasks)
{
    offered_tasks = 0U;
    if (task_payloads.empty() || !static_phase_batches_enabled
        || process_profile_enabled || update_profile_enabled
        || execution_point_hook
        || (native_signal_observation_any_hook
            && native_signal_observation_any_hook())) {
        return std::nullopt;
    }
    const auto raw = task_payloads.front();
    if ((raw & (pure_wave_singleton_payload
            | native_static_region_payload)) != 0U
        || raw >= fused_static_cohorts.size()) {
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
        const bool owned = output.route
            == FusedStaticCohortPlan::Output::Route::owned_logic4;
        const bool unresolved_logic4 = output.route
            == FusedStaticCohortPlan::Output::Route::projected_logic4;
        const auto* record = owned || unresolved_logic4 ? nullptr
            : driver_values[output.signal].find(output.original_owner);
        if ((owned && !owned_driver_active(output.signal))
            || (unresolved_logic4
                && (signal_writer_counts[output.signal] != 1U
                    || stable_single_writer_processes[output.signal]
                        != output.original_owner))
            || (!owned
                && (forced_values[output.signal]
                    || forced_driver_values[output.signal]
                    || external_driver_values[output.signal]))
            || (!owned
                && !unresolved_logic4
                && (record == nullptr
                    || driver_values[output.signal].size() != 1U))) {
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
    try {
        const auto completion = plan.executor->resume(native_context);
        if (!completion) {
            offered_tasks = 0U;
            if (fused_static_counters_enabled) {
                ++fused_static_counts.fallbacks;
            }
            return std::nullopt;
        }
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
                if (slot.signal != plan.outputs[index].signal
                    || !valid_fused_owned_slot(cohort, slot)) {
                    throw std::logic_error {
                        "fused cohort returned an invalid aggregate slot"
                    };
                }
            }
            if (driver_update_scratch.size() < signals.size()) {
                driver_update_scratch.resize(signals.size());
                resolved_update_marked.resize(signals.size());
            }
            bool changed = false;
            for (const auto& slot : completion->aggregate_slots) {
                const auto outcome = stage_fused_owned_slot(cohort, slot);
                if (outcome == OwnedDriverStage::unsupported) {
                    throw std::logic_error {
                        "fused cohort lost its aggregate owner certificate"
                    };
                }
                changed |= outcome == OwnedDriverStage::changed;
                if (fused_static_counters_enabled) {
                    ++fused_static_counts.aggregate_signals_staged;
                }
            }
            if (changed) {
                schedule_update_commit();
            }
        }
        cohort_snapshots.release(source.pending);
        source.pending = { };
        for (std::size_t index = 0U; index < ready.size(); ++index) {
            auto& state = processes[ready[index]];
            state.queued = false;
            state.pc = plan.resume_instructions[index];
            clear_wait_timeout(state);
            state.status = ProcessStatus::waiting;
            state.waiting_on_static = true;
            state.static_trigger_mask = 0U;
        }
        if (fused_static_counters_enabled) {
            ++fused_static_counts.invocations;
            fused_static_counts.represented_members += ready.size();
            fused_static_counts.owner_stage_calls_avoided
                += plan.owner_stage_calls_total;
        }
        return 1U;
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

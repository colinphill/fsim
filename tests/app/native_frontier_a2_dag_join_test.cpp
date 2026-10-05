// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "../../src/runtime/simir_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess {
    struct Key {
        std::uint64_t time { };
        std::uint64_t delta { };
        std::uint64_t systemverilog_round { };
        std::uint64_t stable_order { };
        std::uint64_t sequence { };
        std::uint32_t process_domain { };
        std::uint32_t phase { };

        friend bool operator==(const Key&, const Key&) = default;
    };

    struct Stamp {
        std::uint32_t process_domain { };
        std::uint32_t phase { };
        std::uint64_t systemverilog_round { };

        friend bool operator==(const Stamp&, const Stamp&) = default;
    };

    struct Snapshot {
        std::string current;
        std::string last;
        std::string stored;
        std::string owner;
        std::string raw_driver;
        std::vector<std::uint64_t> direct_aval;
        std::vector<std::uint64_t> direct_bval;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        Stamp stamp;
        std::uint64_t value_revision { };
        std::uint64_t authoritative_revision { };
        bool materialization_pending { };
    };

    struct EngineSemanticSnapshot {
        std::string current;
        std::string last;
        std::string stored;
        std::vector<std::pair<ProcessId, std::string>> raw_drivers;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        Stamp stamp;
        std::uint64_t value_revision { };

        friend bool operator==(const EngineSemanticSnapshot&,
            const EngineSemanticSnapshot&) = default;
    };

    struct Graph {
        std::size_t component { std::numeric_limits<std::size_t>::max() };
        std::uint64_t generation { };
        std::array<SignalId, 7U> signals { };
        std::array<ProcessId, 5U> processes { };
        std::array<std::size_t, 5U> members { };
        std::array<Snapshot, 5U> before;
        std::array<std::optional<Key>, 5U> original_keys;
        std::array<std::uint64_t, 5U> resume_counts_when_queued { };
        std::array<std::uint64_t, 5U> resume_counts_at_cut { };
        std::array<bool, 5U> resume_count_captured { };
        std::array<bool, 5U> duplicate_keys { };
        bool join_key_seen_after_parent_rows { };
        std::uint64_t forwarding_evaluations_before { };
        std::uint64_t forwarding_members_before { };
        std::uint64_t selected_prefixes_before { };
        std::uint64_t selected_members_before { };
        std::uint64_t local_dispatches_before { };
    };

    struct RoleRow {
        SignalId signal { std::numeric_limits<SignalId>::max() };
        ProcessId owner { std::numeric_limits<ProcessId>::max() };
        std::size_t member { std::numeric_limits<std::size_t>::max() };
        Key original_key;
        std::uint64_t callback_order { };
        std::string predicted;
        std::vector<std::uint64_t> predicted_aval;
        std::vector<std::uint64_t> predicted_bval;
        Snapshot before;
        Snapshot hidden;
    };

    struct Cut {
        std::size_t component { std::numeric_limits<std::size_t>::max() };
        std::uint64_t generation { };
        std::uint64_t a4_revision { };
        std::array<RoleRow, 4U> rows;
        Key tail_key;
        std::uint64_t forwarding_evaluations { };
        std::uint64_t forwarding_members { };
        std::uint64_t selected_prefixes { };
        std::uint64_t selected_members { };
        std::uint64_t local_dispatches { };
        bool entered { };
    };

    [[nodiscard]] static Key copy_key(const RegionFrontierKeyV1& key) noexcept
    {
        return { key.time, key.delta, key.systemverilog_round,
            key.stable_order, key.sequence, key.process_domain, key.phase };
    }

    [[nodiscard]] static bool key_less(const Key& left, const Key& right) noexcept
    {
        return std::tie(left.time, left.delta, left.systemverilog_round,
                   left.phase, left.stable_order, left.sequence)
            < std::tie(right.time, right.delta, right.systemverilog_round,
                right.phase, right.stable_order, right.sequence);
    }

    [[nodiscard]] static const RegionConeForwardingKernel& kernel(
        const fsim::app::Simulation& simulation, const std::size_t component)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel) {
            throw std::logic_error { "the DAG component has no forwarding certificate" };
        }
        return *state.region_activation_programs[component]->forwarding_kernel;
    }

    [[nodiscard]] static std::size_t component_for_signal(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            throw std::logic_error { "the internal signal has no A4 component" };
        }
        const auto component = state.region_authoritative_component_by_signal[signal];
        (void)kernel(simulation, component);
        return component;
    }

    [[nodiscard]] static Snapshot snapshot(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const SignalId signal,
        const ProcessId owner)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (signal >= state.signals.size()
            || signal >= state.driver_values.size()
            || signal >= state.direct_wide_signal_offsets.size()
            || signal >= state.signal_last_values.size()
            || signal >= state.signal_events.size()
            || signal >= state.signal_transactions.size()
            || signal >= state.signal_event_scheduling_stamps.size()
            || signal >= state.signal_value_revisions.size()
            || signal >= state.direct_signal_materialization_pending.size()
            || component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]
            || !state.region_authoritative_state_by_component[component]->valid()
            || !state.region_authoritative_state_by_component[component]
                    ->values().layout().contains(signal)) {
            throw std::logic_error { "the DAG signal snapshot is incomplete" };
        }

        const auto& values
            = state.region_authoritative_state_by_component[component]->values();
        Snapshot result;
        result.current = values.current(signal).to_msb_string();
        result.last = values.previous(signal).to_msb_string();
        result.stored = values.stored(signal).to_msb_string();
        result.owner = values.owner_value(signal, owner).to_msb_string();
        const auto* const raw = state.driver_values[signal].find(owner);
        if (raw == nullptr) {
            throw std::logic_error { "the DAG output has no raw owner driver" };
        }
        result.raw_driver = raw->value.to_msb_string();

        const auto width = state.signals[signal].initial_value.width();
        const auto words = static_cast<std::size_t>(width / 64U)
            + static_cast<std::size_t>(width % 64U != 0U);
        const auto offset = static_cast<std::size_t>(
            state.direct_wide_signal_offsets[signal]);
        if (offset > state.direct_wide_signal_aval.size()
            || words > state.direct_wide_signal_aval.size() - offset
            || offset > state.direct_wide_signal_bval.size()
            || words > state.direct_wide_signal_bval.size() - offset) {
            throw std::logic_error { "the DAG direct-wide mirrors are truncated" };
        }
        const auto aval = std::span<const std::uint64_t> {
            state.direct_wide_signal_aval
        }.subspan(offset, words);
        const auto bval = std::span<const std::uint64_t> {
            state.direct_wide_signal_bval
        }.subspan(offset, words);
        result.direct_aval.assign(aval.begin(), aval.end());
        result.direct_bval.assign(bval.begin(), bval.end());
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        const auto& stamp = state.signal_event_scheduling_stamps[signal];
        result.stamp = {
            static_cast<std::uint32_t>(stamp.origin.process_domain),
            static_cast<std::uint32_t>(stamp.origin.phase),
            stamp.systemverilog_round };
        result.value_revision = state.signal_value_revisions[signal];
        result.authoritative_revision = values.revision();
        result.materialization_pending
            = state.direct_signal_materialization_pending[signal] != 0U;
        return result;
    }

    [[nodiscard]] static EngineSemanticSnapshot semantic_snapshot(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (signal >= state.signals.size()
            || signal >= state.signal_last_values.size()
            || signal >= state.driven_values.size()
            || signal >= state.driver_values.size()
            || signal >= state.signal_events.size()
            || signal >= state.signal_transactions.size()
            || signal >= state.signal_event_scheduling_stamps.size()
            || signal >= state.signal_value_revisions.size()) {
            throw std::logic_error {
                "the engine-neutral DAG semantic snapshot is incomplete"
            };
        }

        EngineSemanticSnapshot result;
        result.current = state.signals[signal].initial_value.to_msb_string();
        result.last = state.signal_last_values[signal].to_msb_string();
        result.stored = state.driven_values[signal].to_msb_string();
        state.driver_values[signal].for_each_in_process_order(
            [&result](const DriverRecord& driver) {
                result.raw_drivers.emplace_back(
                    driver.process, driver.value.to_msb_string());
            });
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        const auto& stamp = state.signal_event_scheduling_stamps[signal];
        result.stamp = {
            static_cast<std::uint32_t>(stamp.origin.process_domain),
            static_cast<std::uint32_t>(stamp.origin.phase),
            stamp.systemverilog_round };
        result.value_revision = state.signal_value_revisions[signal];
        return result;
    }

    [[nodiscard]] static ProcessId output_owner(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        const auto& outputs
            = state.region_activation_programs[component]->activation_kernel.outputs;
        std::optional<ProcessId> result;
        for (const auto& output : outputs) {
            if (output.signal != signal) {
                continue;
            }
            if (output.width != 65U || output.offset != 0U
                || output.value_kind != ValueKind::logic4
                || output.domain != SignalUpdateDomain::systemverilog_active
                || output.update_kind != RegionUpdateKind::systemverilog_active) {
                throw std::logic_error { "the DAG output is not a whole Logic4 Active write" };
            }
            if (result && *result != output.owner) {
                throw std::logic_error { "the DAG output has multiple owners" };
            }
            result = output.owner;
        }
        if (!result) {
            throw std::logic_error { "the DAG output has no compiled writer" };
        }
        return *result;
    }

    [[nodiscard]] static std::size_t writer_member(
        const RegionConeForwardingKernel& forwarding, const ProcessId process)
    {
        std::optional<std::size_t> result;
        for (std::size_t index = 0U; index < forwarding.members.size(); ++index) {
            if (forwarding.members[index].process != process) {
                continue;
            }
            if (result) {
                throw std::logic_error { "the DAG process has duplicate members" };
            }
            result = index;
        }
        if (!result) {
            throw std::logic_error { "the DAG writer is absent from its certificate" };
        }
        return *result;
    }

    [[nodiscard]] static bool reads_signal(
        const RegionConeForwardingKernel& forwarding,
        const std::size_t member,
        const SignalId signal)
    {
        if (member >= forwarding.members.size()) {
            return false;
        }
        const auto& descriptor = forwarding.members[member];
        if (descriptor.read_begin > forwarding.internal_reads.size()
            || descriptor.read_count
                > forwarding.internal_reads.size() - descriptor.read_begin) {
            return false;
        }
        for (std::size_t index = descriptor.read_begin;
             index < descriptor.read_begin + descriptor.read_count; ++index) {
            if (forwarding.internal_reads[index].signal == signal) {
                return true;
            }
        }
        return false;
    }

    static void initialize_graph(
        const fsim::app::Simulation& simulation,
        Graph& graph)
    {
        graph.component = component_for_signal(simulation, graph.signals[1U]);
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (graph.component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[graph.component]
            || !state.region_local_wave_state_by_component[graph.component]
                    ->forwarding_results
            || graph.component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[graph.component]
            || !state.region_authoritative_state_by_component[graph.component]->valid()) {
            throw std::logic_error { "the DAG runtime/A4 component is not prepared" };
        }
        auto& local = *state.region_local_wave_state_by_component[graph.component];
        graph.generation = local.generation;
        const auto& forwarding = kernel(simulation, graph.component);
        std::array<std::size_t, 5U> unique_members { };
        for (std::size_t index = 0U; index < graph.processes.size(); ++index) {
            graph.processes[index] = output_owner(
                simulation, graph.component, graph.signals[index + 1U]);
            graph.members[index] = writer_member(forwarding, graph.processes[index]);
            unique_members[index] = graph.members[index];
            graph.before[index] = snapshot(
                simulation, graph.component, graph.signals[index + 1U],
                graph.processes[index]);
            const auto& before = graph.before[index];
            if (before.current != before.stored
                || before.stored != before.owner
                || before.owner != before.raw_driver
                || before.materialization_pending) {
                throw std::logic_error { "the DAG output roles are not coherent at warmup" };
            }
        }
        std::ranges::sort(unique_members);
        if (std::ranges::adjacent_find(unique_members) != unique_members.end()) {
            throw std::logic_error { "the DAG outputs do not have distinct writer members" };
        }
        if (!reads_signal(forwarding, graph.members[2U], graph.signals[1U])
            || !reads_signal(forwarding, graph.members[3U], graph.signals[2U])
            || !reads_signal(forwarding, graph.members[3U], graph.signals[3U])
            || !reads_signal(forwarding, graph.members[4U], graph.signals[4U])) {
            throw std::logic_error { "the parsed DAG lacks its intended fork/join/tail edges" };
        }
        const auto& root_member = forwarding.members[graph.members[0U]];
        const auto& left_member = forwarding.members[graph.members[1U]];
        const auto& right_member = forwarding.members[graph.members[2U]];
        const auto& join_member = forwarding.members[graph.members[3U]];
        const auto& tail_member = forwarding.members[graph.members[4U]];
        if (root_member.depth != 0U || root_member.dependency_count != 0U
            || left_member.depth != 1U || left_member.dependency_count != 1U
            || right_member.depth != 1U || right_member.dependency_count != 1U
            || join_member.depth != 2U || join_member.dependency_count != 2U
            || tail_member.depth != 3U || tail_member.dependency_count != 1U) {
            throw std::logic_error { "the parsed fixture is not the balanced DAG shape" };
        }
        std::array<bool, 2U> join_parents { };
        if (join_member.dependency_begin > forwarding.dependencies.size()
            || join_member.dependency_count
                > forwarding.dependencies.size() - join_member.dependency_begin) {
            throw std::logic_error { "the parsed join dependency span is invalid" };
        }
        for (std::size_t index = join_member.dependency_begin;
             index < join_member.dependency_begin + join_member.dependency_count;
             ++index) {
            const auto& dependency = forwarding.dependencies[index];
            if (dependency.edge != EdgeKind::any) {
                throw std::logic_error { "the join edge is not an Any dependency" };
            }
            if (dependency.writer_member_index == graph.members[1U]
                && dependency.signal == graph.signals[2U]) {
                join_parents[0U] = true;
            } else if (dependency.writer_member_index == graph.members[2U]
                && dependency.signal == graph.signals[3U]) {
                join_parents[1U] = true;
            } else {
                throw std::logic_error { "the join does not have both branch writers" };
            }
        }
        if (!std::ranges::all_of(join_parents,
                [](const bool parent) { return parent; })) {
            throw std::logic_error { "the balanced join is missing one branch" };
        }
        graph.forwarding_evaluations_before
            = state.systemverilog_wave_profile_region_forwarding_evaluations;
        graph.forwarding_members_before
            = state.systemverilog_wave_profile_region_forwarding_member_consumptions;
        graph.selected_prefixes_before
            = state.systemverilog_wave_profile_v2_selected_forwarding_prefixes;
        graph.selected_members_before
            = state.systemverilog_wave_profile_v2_selected_forwarding_members;
        graph.local_dispatches_before
            = state.systemverilog_wave_profile_a2_local_update_dispatches;
    }

    static void capture_queued_keys(
        const fsim::app::Simulation& simulation,
        const Scheduler& scheduler,
        const SchedulerPhase phase,
        Graph& graph)
    {
        if (scheduler.now() != 1U || phase != SchedulerPhase::active) {
            return;
        }
        const auto& state = *simulation.impl_->interpreter->impl_;
        const auto& bank = *state.region_local_wave_state_by_component[
            graph.component]->forwarding_results;
        for (std::size_t index = 0U; index < graph.processes.size(); ++index) {
            const auto process = graph.processes[index];
            if (process >= state.processes.size()
                || process >= state.region_readiness_queued_by_process.size()
                || process >= state.native_process_resume_counts.size()) {
                continue;
            }
            const auto& queued = state.region_readiness_queued_by_process[process];
            if (!state.processes[process].queued || !queued.key_valid
                || queued.component != graph.component
                || queued.generation != graph.generation
                || queued.member != graph.members[index]
                || queued.queued_key.time != 1U
                || queued.queued_key.process_domain
                    != static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog)
                || queued.queued_key.phase
                    != static_cast<std::uint32_t>(SchedulerPhase::active)
                || queued.queued_key.stable_order != process) {
                continue;
            }
            const auto key = copy_key(queued.queued_key);
            if (graph.original_keys[index]) {
                graph.duplicate_keys[index]
                    = graph.duplicate_keys[index]
                    || *graph.original_keys[index] != key;
            } else {
                graph.original_keys[index] = key;
                graph.resume_counts_when_queued[index]
                    = state.native_process_resume_counts[process];
                graph.resume_count_captured[index] = true;
            }
            if (index == 3U && bank.applied_role_metadata.size() == 3U) {
                std::array<bool, 3U> parent_rows { };
                for (const auto& metadata : bank.applied_role_metadata) {
                    for (std::size_t parent = 0U;
                         parent < parent_rows.size(); ++parent) {
                        parent_rows[parent] = parent_rows[parent]
                            || metadata.signal == graph.signals[parent + 1U];
                    }
                }
                graph.join_key_seen_after_parent_rows
                    = graph.join_key_seen_after_parent_rows
                    || std::ranges::all_of(parent_rows,
                        [](const bool seen) { return seen; });
            }
        }
    }

    [[nodiscard]] static std::size_t journal_rows(
        const fsim::app::Simulation& simulation, const std::size_t component)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results) {
            return 0U;
        }
        return state.region_local_wave_state_by_component[component]
            ->forwarding_results->applied_role_mutations.size();
    }

    [[nodiscard]] static bool try_capture_join_cut(
        const fsim::app::Simulation& simulation,
        const Scheduler& scheduler,
        const SchedulerPhase phase,
        Graph& graph,
        Cut& cut)
    {
        capture_queued_keys(simulation, scheduler, phase, graph);
        if (scheduler.now() != 1U
            || phase != SchedulerPhase::active
            || journal_rows(simulation, graph.component) != 4U) {
            return false;
        }
        const auto& state = *simulation.impl_->interpreter->impl_;
        auto& local = *state.region_local_wave_state_by_component[graph.component];
        auto& bank = *local.forwarding_results;
        auto* const authoritative
            = state.region_authoritative_state_by_component[graph.component].get();
        const auto& outputs
            = state.region_activation_programs[graph.component]
                  ->activation_kernel.outputs;
        if (!bank.role_journal_enabled
            || bank.runtime_generation != graph.generation
            || bank.applied_role_metadata.size() != 4U
            || bank.applied_role_mutations.size() != 4U
            || state.region_forwarding_role_journal_nonempty_components == 0U
            || authoritative == nullptr || !authoritative->valid()) {
            return false;
        }

        std::array<bool, 4U> found_rows { };
        std::array<std::uint64_t, 4U> callback_orders { };
        for (std::size_t row_index = 0U;
             row_index < bank.applied_role_metadata.size(); ++row_index) {
            const auto& metadata = bank.applied_role_metadata[row_index];
            const auto& mutation = bank.applied_role_mutations[row_index];
            std::size_t graph_output_index = graph.signals.size();
            for (std::size_t index = 1U; index <= 4U; ++index) {
                if (graph.signals[index] == metadata.signal) {
                    graph_output_index = index - 1U;
                    break;
                }
            }
            if (graph_output_index >= 4U || found_rows[graph_output_index]
                || metadata.output_index >= outputs.size()
                || metadata.output_index >= bank.output_values.size()
                || metadata.signal != graph.signals[graph_output_index + 1U]
                || metadata.owner != graph.processes[graph_output_index]
                || mutation.signal != metadata.signal || mutation.words.empty()
                || !mutation.any_current_changed || !mutation.any_state_changed
                || metadata.callback_time != 1U || metadata.callback_order == 0U
                || metadata.origin.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || metadata.origin.phase != SchedulerPhase::active
                || metadata.callback_systemverilog_round == 0U) {
                return false;
            }
            const auto& expected_output = outputs[metadata.output_index];
            if (expected_output.signal != metadata.signal
                || expected_output.owner != metadata.owner
                || expected_output.width != 65U || expected_output.offset != 0U
                || expected_output.value_kind != ValueKind::logic4
                || expected_output.domain != SignalUpdateDomain::systemverilog_active
                || expected_output.update_kind != RegionUpdateKind::systemverilog_active) {
                return false;
            }
            const auto& key = graph.original_keys[graph_output_index];
            if (!key || graph.duplicate_keys[graph_output_index]
                || key->time != 1U
                || key->process_domain
                    != static_cast<std::uint32_t>(ProcessSchedulingDomain::systemverilog)
                || key->phase != static_cast<std::uint32_t>(SchedulerPhase::active)
                || key->stable_order != metadata.owner) {
                return false;
            }
            const auto hidden = snapshot(
                simulation, graph.component, metadata.signal, metadata.owner);
            const auto& before = graph.before[graph_output_index];
            if (hidden.current != before.current || hidden.last != before.last
                || hidden.stored != before.stored || hidden.owner != before.owner
                || hidden.raw_driver != before.raw_driver
                || hidden.direct_aval != before.direct_aval
                || hidden.direct_bval != before.direct_bval
                || hidden.materialization_pending != before.materialization_pending
                || metadata.expected_signal_event != hidden.event
                || metadata.expected_transaction != hidden.transaction
                || metadata.expected_value_revision != hidden.value_revision
                || metadata.expected_event_stamp.origin.process_domain
                    != static_cast<ProcessSchedulingDomain>(hidden.stamp.process_domain)
                || metadata.expected_event_stamp.origin.phase
                    != static_cast<SchedulerPhase>(hidden.stamp.phase)
                || metadata.expected_event_stamp.systemverilog_round
                    != hidden.stamp.systemverilog_round
                || metadata.callback_systemverilog_round
                    != hidden.stamp.systemverilog_round
                || hidden.stamp.process_domain
                    != static_cast<std::uint32_t>(ProcessSchedulingDomain::systemverilog)
                || hidden.stamp.phase
                    != static_cast<std::uint32_t>(SchedulerPhase::active)
                || !hidden.event || !hidden.transaction
                || hidden.event->first != 1U || hidden.transaction->first != 1U
                || hidden.value_revision <= before.value_revision) {
                return false;
            }
            const auto& value = bank.output_values[metadata.output_index];
            if (value.width() != 65U || value.to_msb_string() == before.current) {
                return false;
            }
            RoleRow row;
            row.signal = metadata.signal;
            row.owner = metadata.owner;
            row.member = graph.members[graph_output_index];
            row.original_key = *key;
            row.callback_order = metadata.callback_order;
            row.predicted = value.to_msb_string();
            const auto aval = value.aval_words();
            const auto bval = value.bval_words();
            row.predicted_aval.assign(aval.begin(), aval.end());
            row.predicted_bval.assign(bval.begin(), bval.end());
            row.before = before;
            row.hidden = hidden;
            cut.rows[graph_output_index] = std::move(row);
            callback_orders[graph_output_index] = metadata.callback_order;
            found_rows[graph_output_index] = true;
        }
        if (std::ranges::any_of(found_rows, [](const bool found) { return !found; })) {
            return false;
        }
        if (!(callback_orders[0U] < callback_orders[1U]
                && callback_orders[0U] < callback_orders[2U]
                && callback_orders[1U] < callback_orders[3U]
                && callback_orders[2U] < callback_orders[3U])
            || !key_less(cut.rows[0U].original_key, cut.rows[1U].original_key)
            || !key_less(cut.rows[0U].original_key, cut.rows[2U].original_key)
            || !key_less(cut.rows[1U].original_key, cut.rows[3U].original_key)
            || !key_less(cut.rows[2U].original_key, cut.rows[3U].original_key)) {
            return false;
        }
        if (!graph.join_key_seen_after_parent_rows) {
            return false;
        }
        for (std::size_t left = 0U; left < cut.rows.size(); ++left) {
            for (std::size_t right = 0U; right < cut.rows.size(); ++right) {
                if (key_less(cut.rows[left].original_key,
                        cut.rows[right].original_key)
                    && callback_orders[left] >= callback_orders[right]) {
                    return false;
                }
            }
        }

        const auto tail_index = 4U;
        const auto tail_process = graph.processes[tail_index];
        if (tail_process >= state.processes.size()
            || tail_process >= state.region_readiness_queued_by_process.size()
            || tail_process >= state.native_process_resume_counts.size()) {
            return false;
        }
        const auto& tail_state = state.processes[tail_process];
        const auto& tail_receipt
            = state.region_readiness_queued_by_process[tail_process];
        if (!tail_state.queued || !tail_receipt.key_valid
            || tail_receipt.component != graph.component
            || tail_receipt.generation != graph.generation
            || tail_receipt.member != graph.members[tail_index]
            || tail_receipt.queued_key.time != 1U
            || tail_receipt.queued_key.process_domain
                != static_cast<std::uint32_t>(ProcessSchedulingDomain::systemverilog)
            || tail_receipt.queued_key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)
            || tail_receipt.queued_key.stable_order != tail_process
            || !graph.resume_count_captured[tail_index]
            || state.native_process_resume_counts[tail_process]
                != graph.resume_counts_when_queued[tail_index]) {
            return false;
        }
        cut.tail_key = copy_key(tail_receipt.queued_key);
        if (graph.duplicate_keys[tail_index]) {
            return false;
        }
        if (graph.original_keys[tail_index]
            && *graph.original_keys[tail_index] != cut.tail_key) {
            return false;
        }
        graph.original_keys[tail_index] = cut.tail_key;
        if (!key_less(cut.rows[3U].original_key, cut.tail_key)) {
            return false;
        }
        // Flattened A2 consumes these authenticated member receipts without
        // entering the ordinary per-process executor-resume counter path.
        // The four output rows and exact selected-member delta below prove
        // native consumption; the per-process counters must stay unchanged.
        for (std::size_t index = 0U; index < 4U; ++index) {
            if (graph.processes[index] >= state.processes.size()
                || graph.processes[index]
                    >= state.region_readiness_queued_by_process.size()
                || graph.processes[index] >= state.native_process_resume_counts.size()
                || !graph.original_keys[index]
                || !graph.resume_count_captured[index]
                || graph.duplicate_keys[index]
                || state.native_process_resume_counts[graph.processes[index]]
                    != graph.resume_counts_when_queued[index]
                || state.processes[graph.processes[index]].queued
                || state.region_readiness_queued_by_process[graph.processes[index]]
                    .key_valid) {
                return false;
            }
            graph.resume_counts_at_cut[index]
                = state.native_process_resume_counts[graph.processes[index]];
        }
        graph.resume_counts_at_cut[4U]
            = state.native_process_resume_counts[tail_process];
        cut.component = graph.component;
        cut.generation = graph.generation;
        cut.a4_revision = authoritative->values().revision();
        cut.forwarding_evaluations
            = state.systemverilog_wave_profile_region_forwarding_evaluations;
        cut.forwarding_members
            = state.systemverilog_wave_profile_region_forwarding_member_consumptions;
        cut.selected_prefixes
            = state.systemverilog_wave_profile_v2_selected_forwarding_prefixes;
        cut.selected_members
            = state.systemverilog_wave_profile_v2_selected_forwarding_members;
        cut.local_dispatches
            = state.systemverilog_wave_profile_a2_local_update_dispatches;
        if (graph.forwarding_evaluations_before
                == std::numeric_limits<std::uint64_t>::max()
            || cut.forwarding_evaluations
                != graph.forwarding_evaluations_before + 1U
            || cut.forwarding_members <= graph.forwarding_members_before
            || cut.selected_prefixes <= graph.selected_prefixes_before
            || graph.selected_prefixes_before
                > std::numeric_limits<std::uint64_t>::max() - 4U
            || cut.selected_prefixes > graph.selected_prefixes_before + 4U
            || graph.selected_members_before
                > std::numeric_limits<std::uint64_t>::max() - 4U
            || cut.selected_members != graph.selected_members_before + 4U
            || graph.local_dispatches_before
                > std::numeric_limits<std::uint64_t>::max() - 4U
            || cut.local_dispatches != graph.local_dispatches_before + 4U) {
            return false;
        }
        cut.entered = true;
        return true;
    }

    [[nodiscard]] static bool journal_retired(
        const fsim::app::Simulation& simulation, const std::size_t component)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results) {
            return false;
        }
        const auto& bank = *state.region_local_wave_state_by_component[component]
            ->forwarding_results;
        return bank.applied_role_mutations.empty()
            && bank.applied_role_metadata.empty()
            && !bank.role_journal_enabled && bank.private_epoch_retired
            && state.region_forwarding_role_journal_nonempty_components == 0U;
    }

    [[nodiscard]] static bool tail_receipt_unchanged(
        const fsim::app::Simulation& simulation, const Graph& graph,
        const Key& key)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        const auto process = graph.processes[4U];
        if (process >= state.processes.size()
            || process >= state.region_readiness_queued_by_process.size()) {
            return false;
        }
        const auto& receipt = state.region_readiness_queued_by_process[process];
        return state.processes[process].queued && receipt.key_valid
            && receipt.component == graph.component
            && receipt.generation == graph.generation
            && receipt.member == graph.members[4U]
            && copy_key(receipt.queued_key) == key;
    }

    [[nodiscard]] static std::uint64_t forwarding_evaluations(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_evaluations;
    }

    [[nodiscard]] static std::uint64_t resume_count(
        const fsim::app::Simulation& simulation, const ProcessId process)
    {
        const auto& counts = simulation.impl_->interpreter->impl_
            ->native_process_resume_counts;
        return process < counts.size() ? counts[process] : 0U;
    }

    static void set_trace_hook(fsim::app::Simulation& simulation,
        void* context, Scheduler::TraceHook hook) noexcept
    {
        if (simulation.impl_ && simulation.impl_->interpreter) {
            simulation.impl_->interpreter->scheduler().set_trace_hook(context, hook);
        }
    }

    [[nodiscard]] static bool trace_hook_installed(
        const fsim::app::Simulation& simulation) noexcept
    {
        return simulation.impl_ && simulation.impl_->interpreter
            && simulation.impl_->interpreter->scheduler().trace_hook_installed();
    }
};

} // namespace fsim::runtime::simir

namespace {

using fsim::app::Simulation;
using fsim::runtime::RunStatus;
using fsim::runtime::Scheduler;
using fsim::runtime::SchedulerPhase;
using fsim::runtime::SchedulerTraceKind;
using fsim::runtime::SchedulerTraceRecord;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using fsim::runtime::simir::ProcessId;
using fsim::runtime::simir::SignalId;
using Graph = NativeRegionAllocationTestAccess::Graph;
using Key = NativeRegionAllocationTestAccess::Key;
using Cut = NativeRegionAllocationTestAccess::Cut;
using Snapshot = NativeRegionAllocationTestAccess::Snapshot;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_ { name }
    {
        if (const auto* const previous = std::getenv(name); previous != nullptr) {
            had_previous_ = true;
            previous_ = previous;
        }
#if defined(_WIN32)
        valid_ = ::_putenv_s(name_.c_str(), value == nullptr ? "" : value) == 0;
#else
        valid_ = value == nullptr
            ? ::unsetenv(name_.c_str()) == 0
            : ::setenv(name_.c_str(), value, 1) == 0;
#endif
        if (!valid_) {
            throw std::runtime_error { "could not set DAG test environment" };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        if (!valid_) {
            return;
        }
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(name_.c_str(), had_previous_ ? previous_.c_str() : ""));
#else
        if (had_previous_) {
            static_cast<void>(::setenv(name_.c_str(), previous_.c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name_.c_str()));
        }
#endif
    }

private:
    std::string name_;
    std::string previous_;
    bool had_previous_ { };
    bool valid_ { };
};

struct TemporaryDirectory {
    explicit TemporaryDirectory(const std::string_view suffix)
    {
        const auto ticks = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path = std::filesystem::temp_directory_path()
            / ("fsim-a2-dag-join-" + std::to_string(ticks)
                + "-" + std::string { suffix });
        std::filesystem::create_directories(path);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

[[nodiscard]] fsim::project::Config make_balanced_dag_config(
    const std::filesystem::path& root,
    const fsim::project::Optimization optimization)
{
    const auto source = root / "native_frontier_a2_dag_join.sv";
    std::ofstream output { source, std::ios::binary };
    output << R"(
module native_frontier_a2_dag_join(output wire [64:0] sink);
  logic [64:0] source;
  wire [64:0] root_stage;
  wire [64:0] left_stage;
  wire [64:0] right_stage;
  wire [64:0] joined_stage;
  wire [64:0] tail_stage;
  assign root_stage = source;
  assign left_stage = root_stage ^ 65'h1;
  assign right_stage = root_stage & 65'h3;
  assign joined_stage = left_stage ^ right_stage;
  assign tail_stage = joined_stage;
  assign sink = tail_stage;
  initial begin
    source = 65'b0;
    #1 source = 65'h1ffffffffffffffff;
    #2 $finish;
  end
endmodule
)";
    require(static_cast<bool>(output), "could not write the parsed DAG source");

    fsim::project::Config config;
    config.project.name = "native-frontier-a2-dag-join";
    config.project.top = "sv:work.native_frontier_a2_dag_join";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] std::array<SignalId, 7U> find_signals(const Simulation& simulation)
{
    const auto source = simulation.find_signal(
        "native_frontier_a2_dag_join.source");
    const auto root = simulation.find_signal(
        "native_frontier_a2_dag_join.root_stage");
    const auto left = simulation.find_signal(
        "native_frontier_a2_dag_join.left_stage");
    const auto right = simulation.find_signal(
        "native_frontier_a2_dag_join.right_stage");
    const auto joined = simulation.find_signal(
        "native_frontier_a2_dag_join.joined_stage");
    const auto tail = simulation.find_signal(
        "native_frontier_a2_dag_join.tail_stage");
    const auto sink = simulation.find_signal(
        "native_frontier_a2_dag_join.sink");
    require(source && root && left && right && joined && tail && sink,
        "the DAG fixture must expose source, all nodes, and sink");
    return { *source, *root, *left, *right, *joined, *tail, *sink };
}

[[nodiscard]] std::array<std::string, 7U> read_signals(
    const Simulation& simulation, const std::array<SignalId, 7U>& signals)
{
    std::array<std::string, 7U> values;
    for (std::size_t index = 0U; index < signals.size(); ++index) {
        values[index] = simulation.read_signal(signals[index]).to_msb_string();
    }
    return values;
}

struct TraceProbe {
    Key tail_key;
    std::array<ProcessId, 4U> consumed_processes { };
    std::uint64_t task_begin { };
    std::uint64_t batch_begin { };
    std::uint64_t task_end { };
    std::uint64_t producer_replay { };
    bool one_member_batch { true };

    static void receive(void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& probe = *static_cast<TraceProbe*>(context);
        if ((record.kind == SchedulerTraceKind::task_begin
                || record.kind == SchedulerTraceKind::batch_begin)
            && record.systemverilog && record.phase
            && std::find(probe.consumed_processes.begin(),
                probe.consumed_processes.end(), record.order)
                != probe.consumed_processes.end()
            && record.time >= probe.tail_key.time
            && static_cast<std::uint32_t>(*record.phase)
                == static_cast<std::uint32_t>(SchedulerPhase::active)) {
            ++probe.producer_replay;
        }
        if (!record.systemverilog || !record.phase
            || record.time != probe.tail_key.time
            || record.delta != probe.tail_key.delta
            || record.systemverilog_round
                != probe.tail_key.systemverilog_round
            || record.order != probe.tail_key.stable_order
            || record.sequence != probe.tail_key.sequence
            || static_cast<std::uint32_t>(*record.phase) != probe.tail_key.phase) {
            return;
        }
        if (record.kind == SchedulerTraceKind::task_begin) {
            ++probe.task_begin;
        } else if (record.kind == SchedulerTraceKind::batch_begin) {
            ++probe.batch_begin;
            probe.one_member_batch = probe.one_member_batch && record.count == 1U;
        } else if (record.kind == SchedulerTraceKind::task_end) {
            ++probe.task_end;
        }
    }
};

struct DagResult {
    std::array<std::string, 7U> values;
    NativeRegionAllocationTestAccess::EngineSemanticSnapshot joined_semantics;
    NativeRegionAllocationTestAccess::EngineSemanticSnapshot tail_semantics;
    std::uint64_t forwarding_evaluations { };
    std::uint64_t selected_prefix_delta { };
    std::uint64_t selected_member_delta { };
};

[[nodiscard]] DagResult run_compiled_dag(
    const fsim::project::Optimization optimization,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_balanced_dag_config(temporary.path, optimization);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the balanced DAG must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() != 0U,
        "the balanced DAG witness requires compiled processes");
    const auto signals = find_signals(simulation);

    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "time-zero DAG activity must settle before the measured source change");

    Graph graph;
    graph.signals = signals;
    NativeRegionAllocationTestAccess::initialize_graph(simulation, graph);
    Cut cut;
    const auto hook = simulation.add_safe_point_hook(
        [&simulation, &graph, &cut](Scheduler& scheduler,
            const SchedulerPhase phase) {
            if (!cut.entered
                && NativeRegionAllocationTestAccess::try_capture_join_cut(
                    simulation, scheduler, phase, graph, cut)) {
                scheduler.request_stop();
            }
        });
    const auto stopped = simulation.run(1U);
    simulation.remove_safe_point_hook(hook);
    require(stopped.status == RunStatus::stopped && stopped.time == 1U
            && cut.entered,
        "the balanced DAG must stop after four private rows with its tail queued");
    require(!NativeRegionAllocationTestAccess::trace_hook_installed(simulation),
        "no trace hook may veto authentic DAG admission before the cut");
    require(cut.rows[0U].signal == signals[1U]
            && cut.rows[1U].signal == signals[2U]
            && cut.rows[2U].signal == signals[3U]
            && cut.rows[3U].signal == signals[4U],
        "the root, both branch writers, and changed join must be privately applied");

    const std::string all_ones(65U, '1');
    const std::string left_value = std::string(61U, '1') + "1110";
    const std::string right_value = std::string(63U, '0') + "11";
    const std::string joined_value = std::string(61U, '1') + "1101";
    require(cut.rows[0U].predicted == all_ones
            && cut.rows[1U].predicted == left_value
            && cut.rows[2U].predicted == right_value
            && cut.rows[3U].predicted == joined_value,
        "the parsed balanced join must produce four changed expected values");
    // The readiness sidecar is the scheduler-authored full process-activation
    // key. The private output publication callback has its own scheduler round;
    // the journal's callback round is checked against the event stamp below,
    // never against the earlier activation receipt. The receipt sequence is
    // copied verbatim and never reconstructed from member order.
    require(NativeRegionAllocationTestAccess::key_less(
                cut.rows[1U].original_key, cut.rows[3U].original_key)
            && NativeRegionAllocationTestAccess::key_less(
                cut.rows[2U].original_key, cut.rows[3U].original_key),
        "both original parent activation keys must precede the join key");

    const auto observed_join
        = simulation.read_signal(signals[4U]).to_msb_string();
    require(observed_join == joined_value,
        "public join observation must flush its private predicted value");
    for (const auto& row : cut.rows) {
        const auto after = NativeRegionAllocationTestAccess::snapshot(
            simulation, cut.component, row.signal, row.owner);
        require(after.current == row.predicted && after.stored == row.predicted
                && after.owner == row.predicted && after.raw_driver == row.predicted
                && after.last == row.before.current
                && after.direct_aval == row.predicted_aval
                && after.direct_bval == row.predicted_bval
                && after.event == row.hidden.event
                && after.transaction == row.hidden.transaction
                && after.stamp == row.hidden.stamp
                && after.value_revision == row.hidden.value_revision
                && after.authoritative_revision > cut.a4_revision
                && !after.materialization_pending,
            "observation must atomically materialize each role row without replaying metadata");
    }
    require(NativeRegionAllocationTestAccess::journal_retired(
                simulation, cut.component)
            && NativeRegionAllocationTestAccess::tail_receipt_unchanged(
                simulation, graph, cut.tail_key),
        "observation must retire the journal and preserve the tail's original key");

    TraceProbe trace;
    trace.tail_key = cut.tail_key;
    std::copy_n(graph.processes.begin(), trace.consumed_processes.size(),
        trace.consumed_processes.begin());
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, &trace, &TraceProbe::receive);
    simulation.clear_stop();
    const auto completed = simulation.run();
    NativeRegionAllocationTestAccess::set_trace_hook(simulation, nullptr, nullptr);
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the retained original tail callback must finish the simulation");
    require(trace.task_begin == 1U && trace.batch_begin == 1U
            && trace.task_end == 1U && trace.one_member_batch
            && trace.producer_replay == 0U,
        "the exact original tail key must run once without replaying its producers");
    for (std::size_t index = 0U; index < graph.processes.size(); ++index) {
        const auto count = NativeRegionAllocationTestAccess::resume_count(
            simulation, graph.processes[index]);
        if (index < 4U) {
            require(count == graph.resume_counts_at_cut[index],
                "observation and tail resume must not replay a consumed DAG callback");
        } else {
            require(graph.resume_counts_at_cut[index]
                        < std::numeric_limits<std::uint64_t>::max()
                    && count == graph.resume_counts_at_cut[index] + 1U,
                "the retained tail callback must execute exactly once");
        }
    }

    DagResult result;
    result.values = read_signals(simulation, signals);
    result.joined_semantics
        = NativeRegionAllocationTestAccess::semantic_snapshot(
            simulation, signals[4U]);
    result.tail_semantics
        = NativeRegionAllocationTestAccess::semantic_snapshot(
            simulation, signals[5U]);
    result.forwarding_evaluations
        = NativeRegionAllocationTestAccess::forwarding_evaluations(simulation);
    result.selected_prefix_delta
        = cut.selected_prefixes - graph.selected_prefixes_before;
    result.selected_member_delta
        = cut.selected_members - graph.selected_members_before;
    return result;
}

[[nodiscard]] DagResult run_interpreter_dag(const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_balanced_dag_config(
        temporary.path, fsim::project::Optimization::o0);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the interpreter DAG source must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.start();
    const auto completed = simulation.run();
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the interpreter DAG reference must finish");
    DagResult result;
    const auto signals = find_signals(simulation);
    result.values = read_signals(simulation, signals);
    result.joined_semantics
        = NativeRegionAllocationTestAccess::semantic_snapshot(
            simulation, signals[4U]);
    result.tail_semantics
        = NativeRegionAllocationTestAccess::semantic_snapshot(
            simulation, signals[5U]);
    return result;
}

void test_balanced_dag_join()
{
    const auto interpreter = run_interpreter_dag("interpreter");
    const auto o0 = run_compiled_dag(
        fsim::project::Optimization::o0, "o0");
    const auto o2 = run_compiled_dag(
        fsim::project::Optimization::o2, "o2");
    require(o0.values == interpreter.values && o2.values == interpreter.values,
        "O0, O2, and interpreter must agree on the parsed DAG's final values");
    require(o0.joined_semantics == interpreter.joined_semantics
            && o2.joined_semantics == interpreter.joined_semantics
            && o0.tail_semantics == interpreter.tail_semantics
            && o2.tail_semantics == interpreter.tail_semantics,
        "joined and tail values, drivers, and change metadata must match the interpreter");
    const auto& joined = o0.joined_semantics;
    const auto& tail = o0.tail_semantics;
    const auto previous_value = std::string(64U, '0') + "1";
    require(joined.current == o0.values[4U]
            && joined.current == joined.stored
            && joined.last == previous_value
            && !joined.raw_drivers.empty()
            && joined.event && joined.transaction
            && joined.stamp.process_domain
                == static_cast<std::uint32_t>(
                    fsim::runtime::simir::ProcessSchedulingDomain::systemverilog)
            && joined.stamp.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && joined.value_revision != 0U
            && tail.current == o0.values[5U]
            && tail.current == tail.stored
            && tail.last == previous_value
            && !tail.raw_drivers.empty()
            && tail.event && tail.transaction
            && tail.stamp.process_domain == joined.stamp.process_domain
            && tail.stamp.phase == joined.stamp.phase
            && tail.value_revision != 0U
            && o2.joined_semantics.current == o2.values[4U]
            && o2.tail_semantics.current == o2.values[5U],
        "joined and tail current/LAST/stored/driver metadata must describe their published transitions");
    require(o0.forwarding_evaluations != 0U && o2.forwarding_evaluations != 0U
            && o0.selected_prefix_delta > 0U
            && o0.selected_prefix_delta <= o0.selected_member_delta
            && o2.selected_prefix_delta > 0U
            && o2.selected_prefix_delta <= o2.selected_member_delta
            && o0.selected_member_delta == 4U
            && o2.selected_member_delta == 4U,
        "both optimized executors must select four original members through authentic prefixes");
}

} // namespace

int main()
{
    try {
        ScopedEnvironment region_kernel { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedEnvironment local_wave { "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
        ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
        ScopedEnvironment process_counts {
            "FSIM_PROFILE_NATIVE_PROCESS_COUNTS", "1" };
        ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };
        test_balanced_dag_join();
    } catch (const std::exception& error) {
        std::cerr << "A2 balanced DAG join witness failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}

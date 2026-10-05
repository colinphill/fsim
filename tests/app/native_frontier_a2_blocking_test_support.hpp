// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "../../src/runtime/simir_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess {
    struct Key {
        SimulationTick time { };
        std::uint64_t delta { };
        std::uint64_t round { };
        StableOrder order { };
        std::uint64_t sequence { };
        std::uint32_t domain { };
        std::uint32_t phase { };

        friend bool operator==(const Key&, const Key&) = default;
    };

    struct Receipt {
        ProcessId process { };
        Key key;
        std::size_t member { };
        std::uint64_t generation { };
        std::size_t component { };

        friend bool operator==(const Receipt&, const Receipt&) = default;
    };

    struct SignalSnapshot {
        std::string current;
        std::string previous;
        std::string stored;
        std::string owner;
        std::string raw;
        PackedLogic4 current_value;
        PackedLogic4 previous_value;
        PackedLogic4 stored_value;
        PackedLogic4 owner_value;
        PackedLogic4 raw_value;
        bool raw_record_present { };
        std::vector<std::uint64_t> direct_aval;
        std::vector<std::uint64_t> direct_bval;
        std::uint64_t direct_last_aval { };
        std::uint64_t direct_last_bval { };
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        std::uint32_t origin_domain { };
        std::uint32_t origin_phase { };
        std::uint64_t event_round { };
        std::uint64_t revision { };
        bool materialization_pending { };
    };

    struct SemanticSnapshot {
        std::string current;
        std::string previous;
        std::string stored;
        std::vector<std::pair<ProcessId, std::string>> drivers;
        bool raw_driver_records_absent { true };
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        std::uint32_t origin_domain { };
        std::uint32_t origin_phase { };
        std::uint64_t event_round { };
        std::uint64_t revision { };

        friend bool operator==(const SemanticSnapshot&,
            const SemanticSnapshot&) = default;
    };

    struct PrivateRow {
        SignalId signal { };
        ProcessId owner { };
        std::size_t output_index { };
        std::string predicted;
        SignalSnapshot before;
        SignalSnapshot hidden;
        Key callback_key;
        std::uint64_t callback_order { };
    };

    struct Cut {
        std::size_t component { };
        std::uint64_t generation { };
        ProcessId root { };
        ProcessId middle { };
        ProcessId sink { };
        Receipt root_receipt;
        Receipt middle_receipt;
        Receipt sink_receipt;
        std::array<PrivateRow, 2U> rows;
        std::size_t row_count { };
        std::uint64_t eval_before { };
        std::uint64_t eval_after { };
        std::uint64_t suppressions_before { };
        std::uint64_t suppressions_after { };
        std::uint64_t forwarding_attempts_at_cut { };
        std::uint64_t root_resume_count_at_cut { };
        std::uint64_t middle_resume_count_at_cut { };
        bool captured { };
    };

    struct BlockingFailureSample {
        bool observed { };
        bool exact_middle_frontier { };
        bool middle_receipt_unchanged { };
        bool root_only_private_row { };
        bool middle_unpublished { };
        bool middle_roles_unchanged { };
        bool middle_direct_planes_unchanged { };
        bool blocking_output_staged { };
        std::size_t journal_row_count { };
        std::uint64_t forwarding_attempts { };
        std::uint64_t blocking_declines { };
    };

    /// Scalar-only request state used by the allocation interposer. The
    /// predicate reads borrowed runtime state and captures the authentic root
    /// and middle keys before the selected allocation is failed.
    struct BlockingFailurePredicateContext {
        const fsim::app::Simulation* simulation { };
        SignalId root_signal { };
        SignalId middle_signal { };
        ProcessId root { };
        ProcessId middle { };
        std::size_t component { };
        const SignalSnapshot* root_before { };
        const SignalSnapshot* middle_before { };
        std::uint64_t eval_before { };
        Cut* cut { };
        bool matched { };
    };

    [[nodiscard]] static Key copy_key(const RegionFrontierKeyV1& key) noexcept
    {
        return { key.time, key.delta, key.systemverilog_round,
            static_cast<StableOrder>(key.stable_order), key.sequence,
            key.process_domain, key.phase };
    }

    [[nodiscard]] static bool precedes(const Key& left,
        const Key& right) noexcept
    {
        if (left.time != right.time) {
            return left.time < right.time;
        }
        if (left.delta != right.delta) {
            return left.delta < right.delta;
        }
        if (left.round != right.round) {
            return left.round < right.round;
        }
        if (left.order != right.order) {
            return left.order < right.order;
        }
        return left.sequence < right.sequence;
    }

    [[nodiscard]] static std::size_t component_for_signal(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            throw std::logic_error { "blocking signal has no component" };
        }
        const auto component
            = state.region_authoritative_component_by_signal[signal];
        if (component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel) {
            throw std::logic_error { "blocking signal has no forwarding program" };
        }
        return component;
    }

    /// Select the first allocation after the middle's staged blocking output
    /// reaches its pre-publication path. This runs from operator new, so it
    /// must not allocate, call public snapshot helpers, or retain a lease.
    [[nodiscard]] static bool select_precommit_blocking_allocation(
        void* const opaque) noexcept
    {
        auto& request = *static_cast<BlockingFailurePredicateContext*>(opaque);
        if (request.matched || request.simulation == nullptr
            || request.root_before == nullptr
            || request.middle_before == nullptr || request.cut == nullptr) {
            return false;
        }
        const auto& state = *request.simulation->impl_->interpreter->impl_;
        const auto component = request.component;
        if (component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]
            || component >= state.region_readiness_queued_by_process.size()
            || request.root >= state.processes.size()
            || request.middle >= state.processes.size()
            || request.root >= state.region_readiness_member_index_by_process.size()
            || request.middle >= state.region_readiness_member_index_by_process.size()
            || request.root_signal >= state.signals.size()
            || request.middle_signal >= state.signals.size()
            || request.root_signal >= state.driver_values.size()
            || request.middle_signal >= state.driver_values.size()
            || request.root_signal >= state.signal_events.size()
            || request.root_signal >= state.signal_transactions.size()
            || request.root_signal >= state.signal_event_scheduling_stamps.size()
            || request.root_signal >= state.signal_value_revisions.size()
            || request.middle_signal >= state.signal_events.size()
            || request.middle_signal >= state.signal_transactions.size()
            || request.middle_signal >= state.signal_event_scheduling_stamps.size()
            || request.middle_signal >= state.signal_value_revisions.size()) {
            return false;
        }

        const auto& local = *state.region_local_wave_state_by_component[component];
        const auto* const authoritative
            = state.region_authoritative_state_by_component[component].get();
        if (!local.forwarding_results || local.generation == 0U
            || local.generation != state.region_runtime_generation
            || !local.seeded || !authoritative->valid()
            || !authoritative->values().packed_slots_bound()
            || local.authoritative_revision != authoritative->values().revision()) {
            return false;
        }
        const auto& program = *state.region_activation_programs[component];
        if (!program.forwarding_kernel) {
            return false;
        }
        const auto& kernel = *program.forwarding_kernel;
        const auto& outputs = program.activation_kernel.outputs;
        auto& bank = *local.forwarding_results;
        if (!bank.active || !bank.role_journal_enabled
            || bank.runtime_generation != local.generation
            || state.systemverilog_wave_profile_region_forwarding_evaluations
                <= request.eval_before
            || bank.applied_role_metadata.size() != 1U
            || bank.applied_role_mutations.size() != 1U
            || bank.member_indices.size() != 1U
            || bank.output_indices.size() != 1U
            || bank.output_values.size() != outputs.size()) {
            return false;
        }

        const auto root_output = std::ranges::find(outputs,
            request.root_signal, &RegionConeOutputBinding::signal);
        const auto middle_output = std::ranges::find(outputs,
            request.middle_signal, &RegionConeOutputBinding::signal);
        if (root_output == outputs.end() || middle_output == outputs.end()) {
            return false;
        }
        const auto root_output_index = static_cast<std::size_t>(
            root_output - outputs.begin());
        const auto middle_output_index = static_cast<std::size_t>(
            middle_output - outputs.begin());
        const auto& metadata = bank.applied_role_metadata.front();
        const auto& mutation = bank.applied_role_mutations.front();
        if (root_output_index >= bank.output_values.size()
            || middle_output_index >= bank.output_values.size()
            || bank.member_indices.front() >= kernel.members.size()
            || kernel.members[bank.member_indices.front()].process != request.middle
            || bank.output_indices.front() != middle_output_index
            || metadata.output_index != root_output_index
            || metadata.signal != request.root_signal
            || metadata.owner != request.root
            || mutation.signal != request.root_signal
            || !mutation.whole_signal
            || !mutation.any_state_changed
            || root_output->owner != request.root
            || root_output->offset != 0U || root_output->width != 1U
            || root_output->value_kind != ValueKind::logic4
            || root_output->domain != SignalUpdateDomain::systemverilog_active
            || root_output->update_kind != RegionUpdateKind::systemverilog_active
            || root_output->publication_kind
                != RegionOutputPublicationKind::blocking_immediate
            || middle_output->owner != request.middle
            || middle_output->offset != 0U || middle_output->width != 1U
            || middle_output->value_kind != ValueKind::logic4
            || middle_output->domain != SignalUpdateDomain::systemverilog_active
            || middle_output->update_kind != RegionUpdateKind::systemverilog_active
            || middle_output->publication_kind
                != RegionOutputPublicationKind::blocking_immediate
            || bank.output_values[root_output_index].is_logic9()
            || bank.output_values[middle_output_index].is_logic9()
            || bank.output_values[root_output_index].width() != 1U
            || bank.output_values[middle_output_index].width() != 1U
            || bank.output_values[root_output_index].get(0U) != Logic4::zero
            || bank.output_values[middle_output_index]
                != bank.output_values[root_output_index]
            || bank.output_values[middle_output_index]
                == request.middle_before->current_value
            || state.static_fanout_for(request.middle_signal).empty()) {
            return false;
        }

        const auto root_member = std::ranges::find(kernel.members, request.root,
            &RegionConeForwardingMember::process);
        const auto middle_member = std::ranges::find(kernel.members, request.middle,
            &RegionConeForwardingMember::process);
        if (root_member == kernel.members.end()
            || middle_member == kernel.members.end()) {
            return false;
        }
        const auto root_member_index = static_cast<std::size_t>(
            root_member - kernel.members.begin());
        const auto middle_member_index = static_cast<std::size_t>(
            middle_member - kernel.members.begin());
        if (root_member_index >= bank.member_consumed.size()
            || root_member_index >= bank.member_active.size()
            || middle_member_index >= bank.member_consumed.size()
            || middle_member_index >= bank.member_active.size()
            || bank.member_consumed[root_member_index] == 0U
            || bank.member_active[root_member_index] == 0U
            || bank.member_consumed[middle_member_index] != 0U
            || bank.member_active[middle_member_index] == 0U) {
            return false;
        }

        const auto root_before_index = request.root_signal;
        const auto middle_before_index = request.middle_signal;
        const auto same_l4_planes = [](
            const PackedLogic4PlaneReadLease& actual,
            const PackedLogic4& expected) noexcept {
            return actual && !actual.is_logic9()
                && actual.width() == expected.width()
                && std::ranges::equal(actual.plane_words(0U),
                    expected.aval_words())
                && std::ranges::equal(actual.plane_words(1U),
                    expected.bval_words());
        };
        const auto& values = authoritative->values();
        const auto root_current = values.plane_read_lease(
            request.root_signal, PackedPlaneRole::current);
        const auto root_previous = values.plane_read_lease(
            request.root_signal, PackedPlaneRole::previous);
        const auto root_stored = values.plane_read_lease(
            request.root_signal, PackedPlaneRole::stored);
        const auto root_owner = values.plane_read_lease(
            request.root_signal, PackedPlaneRole::owner, request.root);
        const auto middle_current = values.plane_read_lease(
            request.middle_signal, PackedPlaneRole::current);
        const auto middle_previous = values.plane_read_lease(
            request.middle_signal, PackedPlaneRole::previous);
        const auto middle_stored = values.plane_read_lease(
            request.middle_signal, PackedPlaneRole::stored);
        const auto middle_owner = values.plane_read_lease(
            request.middle_signal, PackedPlaneRole::owner, request.middle);
        if (!same_l4_planes(root_current,
                request.root_before->current_value)
            || !same_l4_planes(root_stored,
                request.root_before->stored_value)
            || !same_l4_planes(root_owner,
                request.root_before->owner_value)
            || !same_l4_planes(root_previous,
                request.root_before->previous_value)
            || !same_l4_planes(middle_current,
                request.middle_before->current_value)
            || !same_l4_planes(middle_previous,
                request.middle_before->previous_value)
            || !same_l4_planes(middle_stored,
                request.middle_before->stored_value)
            || !same_l4_planes(middle_owner,
                request.middle_before->owner_value)) {
            return false;
        }

        const auto direct_planes_unchanged = [&](const SignalId signal,
                                                  const SignalSnapshot& before) noexcept {
            if (signal >= state.direct_wide_signal_offsets.size()
                || signal >= state.direct_signal_aval.size()
                || signal >= state.direct_signal_bval.size()
                || signal >= state.direct_signal_last_aval.size()
                || signal >= state.direct_signal_last_bval.size()
                || signal >= state.direct_signal_materialization_pending.size()
                || signal >= state.driven_values.size()
                || signal >= state.signals.size()
                || signal >= state.direct_wide_signal_aval.size()) {
                return false;
            }
            const auto width = state.signals[signal].initial_value.width();
            if (width == 0U) {
                return false;
            }
            const auto words = static_cast<std::size_t>(width / 64U)
                + static_cast<std::size_t>(width % 64U != 0U);
            const auto offset = static_cast<std::size_t>(
                state.direct_wide_signal_offsets[signal]);
            return words == before.direct_aval.size()
                && words == before.direct_bval.size()
                && offset <= state.direct_wide_signal_aval.size()
                && words <= state.direct_wide_signal_aval.size() - offset
                && offset <= state.direct_wide_signal_bval.size()
                && words <= state.direct_wide_signal_bval.size() - offset
                && std::ranges::equal(
                    std::span { state.direct_wide_signal_aval }
                        .subspan(offset, words), before.direct_aval)
                && std::ranges::equal(
                    std::span { state.direct_wide_signal_bval }
                        .subspan(offset, words), before.direct_bval)
                && state.direct_signal_aval[signal] == before.direct_aval.front()
                && state.direct_signal_bval[signal] == before.direct_bval.front()
                && state.direct_signal_last_aval[signal]
                    == before.direct_last_aval
                && state.direct_signal_last_bval[signal]
                    == before.direct_last_bval
                && state.direct_signal_materialization_pending[signal]
                    == static_cast<std::uint8_t>(before.materialization_pending)
                && state.driven_values[signal] == before.raw_value;
        };
        if (!direct_planes_unchanged(request.root_signal,
                *request.root_before)
            || !direct_planes_unchanged(request.middle_signal,
                *request.middle_before)
            || !state.driver_values[root_before_index].empty()
            || !state.driver_values[middle_before_index].empty()) {
            return false;
        }

        if (state.signal_events[request.root_signal]
                != metadata.expected_signal_event
            || state.signal_transactions[request.root_signal]
                != metadata.expected_transaction
            || metadata.origin.process_domain
                != ProcessSchedulingDomain::systemverilog
            || metadata.origin.phase != SchedulerPhase::active
            || metadata.expected_event_stamp.origin.process_domain
                != metadata.origin.process_domain
            || metadata.expected_event_stamp.origin.phase
                != metadata.origin.phase
            || state.signal_event_scheduling_stamps[request.root_signal]
                .systemverilog_round
                != metadata.expected_event_stamp.systemverilog_round
            || state.signal_event_scheduling_stamps[request.root_signal]
                .origin.process_domain
                != metadata.expected_event_stamp.origin.process_domain
            || state.signal_event_scheduling_stamps[request.root_signal]
                .origin.phase != metadata.expected_event_stamp.origin.phase
            || state.signal_value_revisions[request.root_signal]
                != metadata.expected_value_revision
            || !metadata.expected_signal_event
            || !metadata.expected_transaction
            || metadata.expected_value_revision
                <= request.root_before->revision
            || state.signal_events[request.middle_signal]
                != request.middle_before->event
            || state.signal_transactions[request.middle_signal]
                != request.middle_before->transaction
            || state.signal_value_revisions[request.middle_signal]
                != request.middle_before->revision
            || state.signal_event_scheduling_stamps[request.middle_signal]
                .systemverilog_round != request.middle_before->event_round
            || static_cast<std::uint32_t>(
                state.signal_event_scheduling_stamps[request.middle_signal]
                    .origin.process_domain)
                != request.middle_before->origin_domain
            || static_cast<std::uint32_t>(
                state.signal_event_scheduling_stamps[request.middle_signal]
                    .origin.phase)
                != request.middle_before->origin_phase) {
            return false;
        }

        const auto& prefix = bank.prefix;
        if (prefix.frontier_generation == 0U
            || prefix.frontier_cursor > prefix.frontier_end
            || prefix.frontier_end - prefix.frontier_cursor
                < prefix.tasks.size()
            || prefix.time != 1U || prefix.delta != metadata.callback_delta
            || prefix.phase != SchedulerPhase::active
            || prefix.process_domain
                != ProcessSchedulingDomain::systemverilog) {
            return false;
        }
        const RegionKernelSchedulerPrefixTask* root_task { };
        for (std::size_t index = 0U; index < prefix.tasks.size(); ++index) {
            const auto& task = prefix.tasks[index];
            if (task.task_ordinal != prefix.frontier_cursor + index) {
                return false;
            }
            if (task.member.process != request.root) {
                continue;
            }
            if (root_task != nullptr) {
                return false;
            }
            root_task = &task;
        }
        if (root_task == nullptr
            || root_task->task_ordinal < prefix.frontier_cursor
            || root_task->task_ordinal >= prefix.frontier_end) {
            return false;
        }
        const auto& root_origin = root_task->member.origin;
        if (root_origin.process_domain
                != ProcessSchedulingDomain::systemverilog
            || root_origin.phase != SchedulerPhase::active
            || root_origin.time != prefix.time
            || root_origin.delta != prefix.delta
            || root_origin.systemverilog_round
                != prefix.systemverilog_round
            || root_origin.stable_order != request.root
            || root_origin.time != metadata.callback_time
            || root_origin.delta != metadata.callback_delta
            || root_origin.systemverilog_round
                != metadata.callback_systemverilog_round
            || metadata.callback_order != 1U) {
            return false;
        }
        const Key root_key { root_origin.time, root_origin.delta,
            root_origin.systemverilog_round, root_origin.stable_order,
            root_origin.sequence,
            static_cast<std::uint32_t>(ProcessSchedulingDomain::systemverilog),
            static_cast<std::uint32_t>(SchedulerPhase::active) };

        const auto frontier = state.scheduler.current_batch_frontier();
        if (!frontier || frontier->generation == 0U
            || frontier->time != state.scheduler.now()
            || frontier->delta != state.scheduler.delta()
            || frontier->systemverilog_round
                != state.scheduler.systemverilog_round()
            || frontier->phase != SchedulerPhase::active
            || frontier->time != prefix.time
            || request.middle >= state.region_readiness_queued_by_process.size()) {
            return false;
        }
        constexpr std::uint64_t wave_payload = UINT64_C(1) << 61U;
        const SchedulerBatchFrontierEntry* middle_task { };
        for (const auto& task : frontier->tasks) {
            if (task.stable_order != request.middle
                || task.payload != (wave_payload | request.middle)) {
                continue;
            }
            if (middle_task != nullptr) {
                return false;
            }
            middle_task = &task;
        }
        const auto& queued
            = state.region_readiness_queued_by_process[request.middle];
        if (middle_task == nullptr || !state.processes[request.middle].queued
            || !queued.key_valid || queued.component != component
            || queued.generation != local.generation
            || queued.member != state.region_readiness_member_index_by_process[
                request.middle]
            || queued.queued_key.time != frontier->time
            || queued.queued_key.delta != frontier->delta
            || queued.queued_key.systemverilog_round
                != frontier->systemverilog_round
            || queued.queued_key.stable_order != middle_task->stable_order
            || queued.queued_key.sequence != middle_task->sequence
            || queued.queued_key.process_domain
                != static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            || queued.queued_key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)) {
            return false;
        }
        const Key middle_key { frontier->time, frontier->delta,
            frontier->systemverilog_round, middle_task->stable_order,
            middle_task->sequence,
            static_cast<std::uint32_t>(ProcessSchedulingDomain::systemverilog),
            static_cast<std::uint32_t>(SchedulerPhase::active) };
        if (!(copy_key(queued.queued_key) == middle_key)) {
            return false;
        }

        const auto& middle_process = state.processes[request.middle];
        const auto middle_activation_member = std::ranges::find(
            program.activation_kernel.members, request.middle,
            &RegionConeKernelMember::process);
        if (middle_activation_member == program.activation_kernel.members.end()
            || std::ranges::count(program.activation_kernel.members,
                request.middle, &RegionConeKernelMember::process) != 1) {
            return false;
        }
        if (middle_activation_member->final_debug_state
            && middle_process.cold().current_scope.capacity()
                < middle_activation_member->final_debug_state->scope.size()) {
            return false;
        }
        const auto expected_completion_identity_count
            = static_cast<std::size_t>(
                !middle_process.region_kernel_completion_boundary_validated);
        if (bank.completion_storage_identities.size()
                != expected_completion_identity_count) {
            return false;
        }
        if (bank.completion_storage_identities.capacity()
            < bank.member_indices.size()) {
            return false;
        }

        auto& cut = *request.cut;
        cut.component = component;
        cut.generation = local.generation;
        cut.root = request.root;
        cut.middle = request.middle;
        cut.root_receipt = { request.root, root_key,
            root_member_index, local.generation, component };
        cut.middle_receipt = { request.middle, middle_key,
            queued.member, local.generation, component };
        cut.eval_after
            = state.systemverilog_wave_profile_region_forwarding_evaluations;
        cut.suppressions_after
            = state.systemverilog_wave_profile_a2_local_fanout_suppressions;
        cut.forwarding_attempts_at_cut
            = state.systemverilog_wave_profile_region_forwarding_attempts;
        cut.root_resume_count_at_cut = request.root
                < state.native_process_resume_counts.size()
            ? state.native_process_resume_counts[request.root] : 0U;
        cut.middle_resume_count_at_cut = request.middle
                < state.native_process_resume_counts.size()
            ? state.native_process_resume_counts[request.middle] : 0U;
        cut.row_count = 1U;
        auto& row = cut.rows[0U];
        row.signal = request.root_signal;
        row.owner = request.root;
        row.output_index = root_output_index;
        row.callback_key = root_key;
        row.callback_order = metadata.callback_order;
        row.hidden.event = metadata.expected_signal_event;
        row.hidden.transaction = metadata.expected_transaction;
        row.hidden.origin_domain = static_cast<std::uint32_t>(
            metadata.expected_event_stamp.origin.process_domain);
        row.hidden.origin_phase = static_cast<std::uint32_t>(
            metadata.expected_event_stamp.origin.phase);
        row.hidden.event_round
            = metadata.expected_event_stamp.systemverilog_round;
        row.hidden.revision = metadata.expected_value_revision;
        cut.captured = true;
        request.matched = true;
        return true;
    }

    [[nodiscard]] static ProcessId output_owner(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const SignalId signal,
        const RegionOutputPublicationKind expected_kind)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        const auto& outputs
            = state.region_activation_programs[component]
                  ->activation_kernel.outputs;
        const auto binding = std::ranges::find(outputs, signal,
            &RegionConeOutputBinding::signal);
        if (binding == outputs.end() || binding->offset != 0U
            || binding->width == 0U
            || binding->value_kind != ValueKind::logic4
            || binding->domain != SignalUpdateDomain::systemverilog_active
            || binding->publication_kind != expected_kind) {
            throw std::logic_error {
                "blocking fixture output lost its whole-write publication kind"
            };
        }
        return binding->owner;
    }

    [[nodiscard]] static ProcessId unique_whole_writer_for_signal(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        std::optional<ProcessId> writer;
        for (const auto& process : state.processes) {
            const auto& regions = process.program().driver_regions();
            const auto matching = std::ranges::find(regions, signal,
                &Process::DriverRegion::signal);
            if (matching == regions.end()) {
                continue;
            }
            if (std::ranges::find(std::next(matching), regions.end(), signal,
                    &Process::DriverRegion::signal) != regions.end()
                || !matching->whole || matching->offset != 0U) {
                throw std::logic_error {
                    "blocking exclusion output is not one whole process write"
                };
            }
            if (writer) {
                throw std::logic_error {
                    "blocking exclusion output has multiple process writers"
                };
            }
            writer = process.id;
        }
        if (!writer) {
            throw std::logic_error {
                "blocking exclusion output has no elaborated process writer"
            };
        }
        return *writer;
    }

    [[nodiscard]] static SignalSnapshot snapshot(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const SignalId signal,
        const ProcessId owner)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]
            || !state.region_authoritative_state_by_component[component]->valid()
            || !state.region_authoritative_state_by_component[component]
                    ->values().layout().contains(signal)
            || signal >= state.driver_values.size()
            || signal >= state.direct_wide_signal_offsets.size()
            || signal >= state.signal_events.size()
            || signal >= state.signal_transactions.size()
            || signal >= state.signal_event_scheduling_stamps.size()
            || signal >= state.signal_value_revisions.size()
            || signal >= state.direct_signal_materialization_pending.size()
            || signal >= state.direct_signal_last_aval.size()
            || signal >= state.direct_signal_last_bval.size()) {
            throw std::logic_error { "blocking role snapshot is incomplete" };
        }
        const auto& values
            = state.region_authoritative_state_by_component[component]->values();
        SignalSnapshot result;
        result.current_value = values.current(signal);
        result.previous_value = values.previous(signal);
        result.stored_value = values.stored(signal);
        result.owner_value = values.owner_value(signal, owner);
        result.current = values.current(signal).to_msb_string();
        result.previous = values.previous(signal).to_msb_string();
        result.stored = values.stored(signal).to_msb_string();
        result.owner = values.owner_value(signal, owner).to_msb_string();
        const auto* const raw = state.driver_values[signal].find(owner);
        if (raw != nullptr) {
            result.raw_value = raw->value;
            result.raw = raw->value.to_msb_string();
            result.raw_record_present = true;
        } else {
            const auto owners = values.layout().owners(signal);
            if (!state.driver_values[signal].empty()
                || signal >= state.driven_values.size()
                || values.layout().signal(signal).storage_class
                    != SignalDriverStorageClass::single_owner
                || owners.size() != 1U || owners.front().process != owner
                || !owners.front().aliases_stored) {
                throw std::logic_error {
                    "blocking role has no raw driver or stored alias"
                };
            }
            result.raw_value = state.driven_values[signal];
            result.raw = state.driven_values[signal].to_msb_string();
        }
        const auto width = state.signals[signal].initial_value.width();
        const auto word_count = static_cast<std::size_t>(width / 64U)
            + static_cast<std::size_t>(width % 64U != 0U);
        const auto offset = static_cast<std::size_t>(
            state.direct_wide_signal_offsets[signal]);
        if (offset > state.direct_wide_signal_aval.size()
            || word_count > state.direct_wide_signal_aval.size() - offset
            || offset > state.direct_wide_signal_bval.size()
            || word_count > state.direct_wide_signal_bval.size() - offset) {
            throw std::logic_error { "blocking direct planes are truncated" };
        }
        result.direct_aval.assign(
            state.direct_wide_signal_aval.begin()
                + static_cast<std::ptrdiff_t>(offset),
            state.direct_wide_signal_aval.begin()
                + static_cast<std::ptrdiff_t>(offset + word_count));
        result.direct_bval.assign(
            state.direct_wide_signal_bval.begin()
                + static_cast<std::ptrdiff_t>(offset),
            state.direct_wide_signal_bval.begin()
                + static_cast<std::ptrdiff_t>(offset + word_count));
        result.direct_last_aval = state.direct_signal_last_aval[signal];
        result.direct_last_bval = state.direct_signal_last_bval[signal];
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        const auto& stamp = state.signal_event_scheduling_stamps[signal];
        result.origin_domain
            = static_cast<std::uint32_t>(stamp.origin.process_domain);
        result.origin_phase = static_cast<std::uint32_t>(stamp.origin.phase);
        result.event_round = stamp.systemverilog_round;
        result.revision = state.signal_value_revisions[signal];
        result.materialization_pending
            = state.direct_signal_materialization_pending[signal] != 0U;
        return result;
    }

    [[nodiscard]] static SemanticSnapshot semantic_snapshot(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        auto& application = *simulation.impl_;
        auto& interpreter = *application.interpreter;
        auto& state = *interpreter.impl_;
        SemanticSnapshot result;
        result.current = simulation.read_signal(signal).to_msb_string();
        result.previous = state.signal_last_values[signal].to_msb_string();
        result.stored = interpreter.stored_signal_value_snapshot(signal)
                            .to_msb_string();
        state.driver_values[signal].for_each_in_process_order(
            [&result](const DriverRecord& driver) {
                result.raw_driver_records_absent = false;
                result.drivers.emplace_back(driver.process,
                    driver.value.to_msb_string());
            });
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        const auto& stamp = state.signal_event_scheduling_stamps[signal];
        result.origin_domain
            = static_cast<std::uint32_t>(stamp.origin.process_domain);
        result.origin_phase = static_cast<std::uint32_t>(stamp.origin.phase);
        result.event_round = stamp.systemverilog_round;
        result.revision = state.signal_value_revisions[signal];
        return result;
    }

    [[nodiscard]] static std::optional<Receipt> readiness_receipt(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const ProcessId process)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (process >= state.processes.size()
            || process >= state.region_readiness_queued_by_process.size()
            || process >= state.region_readiness_member_index_by_process.size()
            || !state.processes[process].queued) {
            return std::nullopt;
        }
        const auto& queued = state.region_readiness_queued_by_process[process];
        if (!queued.key_valid || queued.component != component
            || queued.generation != state.region_runtime_generation
            || queued.member
                != state.region_readiness_member_index_by_process[process]) {
            return std::nullopt;
        }
        return Receipt { process, copy_key(queued.queued_key), queued.member,
            queued.generation, queued.component };
    }

    [[nodiscard]] static bool receipt_unchanged(
        const fsim::app::Simulation& simulation, const Receipt& expected)
    {
        const auto actual = readiness_receipt(simulation,
            expected.component, expected.process);
        return actual && *actual == expected;
    }

    [[nodiscard]] static bool try_capture_root_private_prefix(
        fsim::app::Simulation& simulation,
        Scheduler& scheduler,
        const SchedulerPhase phase,
        const SignalId root_signal,
        const SignalId middle_signal,
        const SignalId sink_signal,
        const SignalSnapshot& root_before,
        const SignalSnapshot& middle_before,
        const Receipt& root_receipt,
        const std::uint64_t eval_before,
        const std::uint64_t suppressions_before,
        Cut& cut,
        const SimulationTick expected_time = 1U)
    {
        if (scheduler.now() != expected_time
            || phase != SchedulerPhase::active) {
            return false;
        }
        const auto component = component_for_signal(simulation, root_signal);
        if (component_for_signal(simulation, middle_signal) != component
            || component_for_signal(simulation, sink_signal) != component) {
            return false;
        }
        const auto root = output_owner(simulation, component, root_signal,
            RegionOutputPublicationKind::blocking_immediate);
        const auto middle = output_owner(simulation, component, middle_signal,
            RegionOutputPublicationKind::blocking_immediate);
        const auto sink = output_owner(simulation, component, sink_signal,
            RegionOutputPublicationKind::blocking_immediate);
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results) {
            return false;
        }
        const auto& local = *state.region_local_wave_state_by_component[component];
        const auto& bank = *local.forwarding_results;
        if (!bank.active || !bank.role_journal_enabled
            || bank.applied_role_metadata.size() != 1U
            || bank.applied_role_mutations.size() != 1U
            || bank.applied_role_metadata.front().signal != root_signal
            || bank.applied_role_mutations.front().signal != root_signal) {
            return false;
        }
        const auto middle_receipt = readiness_receipt(
            simulation, component, middle);
        if (!middle_receipt
            || root_receipt.process != root
            || root_receipt.component != component
            || root_receipt.generation != local.generation
            || root_receipt.key.time != expected_time
            || root_receipt.key.domain != static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog)
            || root_receipt.key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)
            || middle_receipt->process != middle
            || middle_receipt->component != component
            || middle_receipt->generation != local.generation
            || middle_receipt->key.time != expected_time
            || middle_receipt->key.domain != static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog)
            || middle_receipt->key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)) {
            return false;
        }

        const auto& metadata = bank.applied_role_metadata.front();
        const auto& outputs
            = state.region_activation_programs[component]
                  ->activation_kernel.outputs;
        if (metadata.output_index >= outputs.size()
            || metadata.signal != root_signal || metadata.owner != root
            || metadata.output_index >= bank.output_values.size()
            || outputs[metadata.output_index].publication_kind
                != RegionOutputPublicationKind::blocking_immediate
            || outputs[metadata.output_index].signal != root_signal
            || outputs[metadata.output_index].owner != root
            || metadata.callback_time != root_receipt.key.time
            || metadata.callback_delta != root_receipt.key.delta
            || metadata.callback_systemverilog_round
                != root_receipt.key.round
            // This is the bank-local row ordinal; scheduler order and
            // sequence are authenticated separately from the receipt.
            || metadata.callback_order != 1U
            || metadata.origin.process_domain
                != ProcessSchedulingDomain::systemverilog
            || metadata.origin.phase != SchedulerPhase::active
            || metadata.expected_value_revision
                != state.signal_value_revisions[root_signal]) {
            return false;
        }

        Cut candidate;
        candidate.component = component;
        candidate.generation = local.generation;
        candidate.root = root;
        candidate.middle = middle;
        candidate.sink = sink;
        candidate.root_receipt = root_receipt;
        candidate.middle_receipt = *middle_receipt;
        candidate.eval_before = eval_before;
        candidate.eval_after
            = state.systemverilog_wave_profile_region_forwarding_evaluations;
        candidate.suppressions_before = suppressions_before;
        candidate.suppressions_after
            = state.systemverilog_wave_profile_a2_local_fanout_suppressions;
        candidate.forwarding_attempts_at_cut
            = state.systemverilog_wave_profile_region_forwarding_attempts;
        candidate.root_resume_count_at_cut = root
                < state.native_process_resume_counts.size()
            ? state.native_process_resume_counts[root] : 0U;
        candidate.middle_resume_count_at_cut = middle
                < state.native_process_resume_counts.size()
            ? state.native_process_resume_counts[middle] : 0U;
        auto& row = candidate.rows[0U];
        row.signal = root_signal;
        row.owner = root;
        row.output_index = metadata.output_index;
        row.predicted = bank.output_values[metadata.output_index].to_msb_string();
        row.before = root_before;
        row.hidden = snapshot(simulation, component, root_signal, root);
        row.callback_key = root_receipt.key;
        row.callback_order = metadata.callback_order;
        candidate.row_count = 1U;
        if (row.hidden.current != row.before.current
            || row.hidden.previous != row.before.previous
            || row.hidden.stored != row.before.stored
            || row.hidden.owner != row.before.owner
            || row.hidden.raw != row.before.raw
            || row.hidden.raw_record_present
                != row.before.raw_record_present
            || row.hidden.direct_aval != row.before.direct_aval
            || row.hidden.direct_bval != row.before.direct_bval
            || row.hidden.event == row.before.event
            || row.hidden.transaction == row.before.transaction
            || row.hidden.revision <= row.before.revision
            || candidate.suppressions_after
                < candidate.suppressions_before + 1U) {
            return false;
        }
        if (middle_before.revision != state.signal_value_revisions[middle_signal]
            || middle_before.event != state.signal_events[middle_signal]
            || middle_before.transaction
                != state.signal_transactions[middle_signal]) {
            return false;
        }
        candidate.captured = true;
        cut = std::move(candidate);
        return true;
    }

    /// The allocator callback is noexcept and allocation-free. It samples only
    /// fixed fields while the failed blocking publication is still in flight.
    static void sample_blocking_failure(
        const fsim::app::Simulation& simulation,
        const Cut& cut,
        const SignalId root_signal,
        const SignalId middle_signal,
        const SignalSnapshot& middle_before,
        BlockingFailureSample& sample) noexcept
    {
        sample.observed = true;
        const auto& state = *simulation.impl_->interpreter->impl_;
        sample.forwarding_attempts
            = state.systemverilog_wave_profile_region_forwarding_attempts;
        sample.blocking_declines
            = state.systemverilog_wave_profile_region_forwarding_declines;
        if (cut.component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[cut.component]
            || !state.region_local_wave_state_by_component[cut.component]
                    ->forwarding_results
            || cut.middle >= state.processes.size()
            || cut.middle >= state.region_readiness_queued_by_process.size()) {
            return;
        }
        const auto& local
            = *state.region_local_wave_state_by_component[cut.component];
        const auto& bank = *local.forwarding_results;
        sample.journal_row_count = bank.applied_role_metadata.size();
        sample.root_only_private_row = bank.active
            && bank.role_journal_enabled
            && bank.runtime_generation == cut.generation
            && bank.applied_role_metadata.size() == 1U
            && bank.applied_role_mutations.size() == 1U
            && bank.applied_role_metadata.front().signal == root_signal
            && bank.applied_role_metadata.front().owner == cut.root
            && bank.applied_role_mutations.front().signal == root_signal;

        const auto& process = state.processes[cut.middle];
        const auto& queued = state.region_readiness_queued_by_process[cut.middle];
        sample.middle_receipt_unchanged = process.queued && queued.key_valid
            && queued.component == cut.component
            && queued.generation == cut.generation
            && queued.member == cut.middle_receipt.member
            && copy_key(queued.queued_key) == cut.middle_receipt.key;
        sample.middle_unpublished
            = middle_signal < state.signal_events.size()
            && middle_signal < state.signal_transactions.size()
            && middle_signal < state.signal_event_scheduling_stamps.size()
            && middle_signal < state.signal_value_revisions.size()
            && state.signal_events[middle_signal] == middle_before.event
            && state.signal_transactions[middle_signal]
                == middle_before.transaction
            && state.signal_value_revisions[middle_signal]
                == middle_before.revision
            && static_cast<std::uint32_t>(state.signal_event_scheduling_stamps[
                   middle_signal].origin.process_domain)
                == middle_before.origin_domain
            && static_cast<std::uint32_t>(state.signal_event_scheduling_stamps[
                   middle_signal].origin.phase)
                == middle_before.origin_phase
            && state.signal_event_scheduling_stamps[middle_signal]
                   .systemverilog_round == middle_before.event_round;

        sample.middle_unpublished = sample.middle_unpublished
            && middle_signal < state.driver_values.size()
            && state.driver_values[middle_signal].empty();

        if (cut.component
                < state.region_authoritative_state_by_component.size()
            && state.region_authoritative_state_by_component[cut.component]
            && state.region_authoritative_state_by_component[cut.component]
                    ->valid()) {
            const auto& values
                = state.region_authoritative_state_by_component[
                    cut.component]->values();
            if (values.layout().contains(middle_signal)) {
                const auto current = values.plane_read_lease(
                    middle_signal, PackedPlaneRole::current);
                const auto previous = values.plane_read_lease(
                    middle_signal, PackedPlaneRole::previous);
                const auto stored = values.plane_read_lease(
                    middle_signal, PackedPlaneRole::stored);
                const auto owner = values.plane_read_lease(
                    middle_signal, PackedPlaneRole::owner, cut.middle);
                const auto same_l4_planes = [](
                    const PackedLogic4PlaneReadLease& actual,
                    const PackedLogic4& expected) noexcept {
                    return actual && !actual.is_logic9()
                        && actual.width() == expected.width()
                        && std::ranges::equal(actual.plane_words(0U),
                            expected.aval_words())
                        && std::ranges::equal(actual.plane_words(1U),
                            expected.bval_words());
                };
                sample.middle_roles_unchanged
                    = same_l4_planes(current, middle_before.current_value)
                    && same_l4_planes(previous,
                        middle_before.previous_value)
                    && same_l4_planes(stored, middle_before.stored_value)
                    && same_l4_planes(owner, middle_before.owner_value);
            }
        }

        if (middle_signal < state.direct_wide_signal_offsets.size()
            && middle_signal < state.signals.size()
            && state.signals[middle_signal].initial_value.width() == 1U
            && middle_signal < state.driven_values.size()
            && middle_signal < state.direct_signal_aval.size()
            && middle_signal < state.direct_signal_bval.size()
            && middle_signal < state.direct_signal_last_aval.size()
            && middle_signal < state.direct_signal_last_bval.size()
            && middle_signal
                < state.direct_signal_materialization_pending.size()) {
            const auto words = static_cast<std::size_t>(
                state.signals[middle_signal].initial_value.width() / 64U)
                + static_cast<std::size_t>(
                    state.signals[middle_signal].initial_value.width() % 64U
                    != 0U);
            const auto offset = static_cast<std::size_t>(
                state.direct_wide_signal_offsets[middle_signal]);
            if (offset <= state.direct_wide_signal_aval.size()
                && words <= state.direct_wide_signal_aval.size() - offset
                && offset <= state.direct_wide_signal_bval.size()
                && words <= state.direct_wide_signal_bval.size() - offset
                && words == middle_before.direct_aval.size()
                && words == middle_before.direct_bval.size()
                && words != 0U) {
                const auto direct_aval
                    = std::span { state.direct_wide_signal_aval }
                          .subspan(offset, words);
                const auto direct_bval
                    = std::span { state.direct_wide_signal_bval }
                          .subspan(offset, words);
                sample.middle_direct_planes_unchanged
                    = std::ranges::equal(direct_aval,
                            middle_before.direct_aval)
                    && std::ranges::equal(direct_bval,
                        middle_before.direct_bval)
                    && state.driven_values[middle_signal]
                        == middle_before.raw_value
                    && state.direct_signal_aval[middle_signal]
                        == middle_before.direct_aval.front()
                    && state.direct_signal_bval[middle_signal]
                        == middle_before.direct_bval.front()
                    && state.direct_signal_last_aval[middle_signal]
                        == middle_before.direct_last_aval
                    && state.direct_signal_last_bval[middle_signal]
                        == middle_before.direct_last_bval
                    && state.direct_signal_materialization_pending[
                        middle_signal]
                        == static_cast<std::uint8_t>(
                            middle_before.materialization_pending);
            }
        }

        if (cut.component < state.region_activation_programs.size()
            && state.region_activation_programs[cut.component]) {
            const auto& outputs = state.region_activation_programs[cut.component]
                                      ->activation_kernel.outputs;
            const auto middle_binding = std::ranges::find(outputs,
                middle_signal, &RegionConeOutputBinding::signal);
            if (middle_binding != outputs.end()) {
                const auto output_index = static_cast<std::size_t>(
                    middle_binding - outputs.begin());
                const auto staged = std::ranges::find(
                    bank.output_indices, output_index);
                sample.blocking_output_staged
                    = staged != bank.output_indices.end()
                    && output_index < bank.output_values.size()
                    && middle_binding->owner == cut.middle
                    && middle_binding->offset == 0U
                    && middle_binding->width == 1U
                    && middle_binding->value_kind == ValueKind::logic4
                    && middle_binding->domain
                        == SignalUpdateDomain::systemverilog_active
                    && middle_binding->update_kind
                        == RegionUpdateKind::systemverilog_active
                    && middle_binding->publication_kind
                        == RegionOutputPublicationKind::blocking_immediate
                    && bank.output_values[output_index].width() == 1U
                    && bank.output_values[output_index].get(0U) == Logic4::zero;
            }
        }

        const auto frontier = state.scheduler.current_batch_frontier();
        if (!frontier || frontier->generation == 0U
            || frontier->phase != SchedulerPhase::active
            || frontier->time != cut.middle_receipt.key.time
            || frontier->delta != cut.middle_receipt.key.delta
            || frontier->systemverilog_round
                != cut.middle_receipt.key.round) {
            return;
        }
        sample.exact_middle_frontier = std::ranges::any_of(frontier->tasks,
            [&](const SchedulerBatchFrontierEntry& task) {
                return task.stable_order == cut.middle_receipt.key.order
                    && task.sequence == cut.middle_receipt.key.sequence;
            });
    }

    [[nodiscard]] static bool try_capture_positive_cut(
        fsim::app::Simulation& simulation,
        Scheduler& scheduler,
        const SchedulerPhase phase,
        const SignalId root_signal,
        const SignalId middle_signal,
        const SignalId sink_signal,
        const SignalSnapshot& root_before,
        const SignalSnapshot& middle_before,
        const Receipt& root_receipt,
        const Receipt& middle_receipt,
        const std::uint64_t eval_before,
        const std::uint64_t suppressions_before,
        Cut& cut,
        const SimulationTick expected_time = 1U)
    {
        if (scheduler.now() != expected_time || phase != SchedulerPhase::active) {
            return false;
        }
        const auto component = component_for_signal(simulation, root_signal);
        if (component_for_signal(simulation, middle_signal) != component
            || component_for_signal(simulation, sink_signal) != component) {
            return false;
        }
        const auto root = output_owner(simulation, component, root_signal,
            RegionOutputPublicationKind::blocking_immediate);
        const auto middle = output_owner(simulation, component, middle_signal,
            RegionOutputPublicationKind::blocking_immediate);
        const auto sink = output_owner(simulation, component, sink_signal,
            RegionOutputPublicationKind::blocking_immediate);
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results
            || root >= state.native_process_resume_counts.size()
            || middle >= state.native_process_resume_counts.size()) {
            return false;
        }
        const auto& local = *state.region_local_wave_state_by_component[component];
        const auto& bank = *local.forwarding_results;
        if (!bank.role_journal_enabled
            || bank.applied_role_metadata.size() != 2U
            || bank.applied_role_mutations.size() != 2U) {
            return false;
        }
        const auto sink_receipt = readiness_receipt(
            simulation, component, sink);
        if (root_receipt.process != root
            || root_receipt.component != component
            || root_receipt.generation != local.generation
            || middle_receipt.process != middle
            || middle_receipt.component != component
            || middle_receipt.generation != local.generation
            || !sink_receipt || sink_receipt->key.time != expected_time
            || sink_receipt->key.domain != static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog)
            || sink_receipt->key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)
            || root_receipt.key.time != expected_time
            || root_receipt.key.domain != static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog)
            || root_receipt.key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)
            || middle_receipt.key.time != expected_time
            || middle_receipt.key.domain != static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog)
            || middle_receipt.key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)) {
            return false;
        }

        Cut candidate;
        candidate.component = component;
        candidate.generation = local.generation;
        candidate.root = root;
        candidate.middle = middle;
        candidate.sink = sink;
        candidate.root_receipt = root_receipt;
        candidate.middle_receipt = middle_receipt;
        candidate.sink_receipt = *sink_receipt;
        candidate.eval_before = eval_before;
        candidate.eval_after
            = state.systemverilog_wave_profile_region_forwarding_evaluations;
        candidate.suppressions_before = suppressions_before;
        candidate.suppressions_after
            = state.systemverilog_wave_profile_a2_local_fanout_suppressions;
        candidate.root_resume_count_at_cut
            = state.native_process_resume_counts[root];
        candidate.middle_resume_count_at_cut
            = state.native_process_resume_counts[middle];
        std::array<bool, 2U> seen { };
        for (const auto& metadata : bank.applied_role_metadata) {
            if (metadata.signal != root_signal
                && metadata.signal != middle_signal) {
                continue;
            }
            const auto row_index = metadata.signal == root_signal ? 0U : 1U;
            if (row_index >= candidate.rows.size() || seen[row_index]) {
                return false;
            }
            seen[row_index] = true;
            const auto& outputs
                = state.region_activation_programs[component]
                      ->activation_kernel.outputs;
            if (metadata.output_index >= outputs.size()
                || outputs[metadata.output_index].publication_kind
                    != RegionOutputPublicationKind::blocking_immediate
                || outputs[metadata.output_index].signal != metadata.signal
                || outputs[metadata.output_index].owner != metadata.owner) {
                return false;
            }
            const auto& callback_receipt = row_index == 0U
                ? root_receipt : middle_receipt;
            const auto& callback_key = callback_receipt.key;
            // Callback metadata omits stable order and sequence, so retain
            // those fields from the authentic queued scheduler receipt.
            if (metadata.callback_time != callback_key.time
                || metadata.callback_delta != callback_key.delta
                || metadata.callback_systemverilog_round
                    != callback_key.round
                || metadata.origin.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || metadata.origin.phase != SchedulerPhase::active
                || callback_key.order != metadata.owner
                || callback_receipt.process != metadata.owner
                || callback_key.time != expected_time
                || metadata.expected_value_revision
                    != state.signal_value_revisions[metadata.signal]) {
                return false;
            }
            PrivateRow row;
            row.signal = metadata.signal;
            row.owner = metadata.owner;
            row.output_index = metadata.output_index;
            row.predicted = bank.output_values[metadata.output_index]
                                .to_msb_string();
            row.before = metadata.signal == root_signal
                ? root_before : middle_before;
            row.hidden = snapshot(simulation, component,
                metadata.signal, metadata.owner);
            row.callback_key = callback_key;
            row.callback_order = metadata.callback_order;
            if (row.hidden.current != row.before.current
                || row.hidden.previous != row.before.previous
                || row.hidden.stored != row.before.stored
                || row.hidden.owner != row.before.owner
                || row.hidden.raw != row.before.raw
                || row.hidden.raw_record_present
                    != row.before.raw_record_present
                || row.hidden.direct_aval != row.before.direct_aval
                || row.hidden.direct_bval != row.before.direct_bval
                || row.hidden.event == row.before.event
                || row.hidden.transaction == row.before.transaction
                || row.hidden.revision <= row.before.revision) {
                return false;
            }
            candidate.rows[row_index] = std::move(row);
            ++candidate.row_count;
        }
        if (candidate.row_count != 2U
            || !seen[0U] || !seen[1U]
            || candidate.rows[0U].signal != root_signal
            || candidate.rows[1U].signal != middle_signal
            || candidate.eval_after <= candidate.eval_before
            || candidate.suppressions_after
                < candidate.suppressions_before + 2U) {
            return false;
        }
        cut = std::move(candidate);
        cut.captured = true;
        return true;
    }

    [[nodiscard]] static bool journal_empty(
        const fsim::app::Simulation& simulation, const Cut& cut)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (cut.component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[cut.component]
            || !state.region_local_wave_state_by_component[cut.component]
                    ->forwarding_results) {
            return false;
        }
        const auto& bank = *state.region_local_wave_state_by_component[
            cut.component]->forwarding_results;
        return bank.applied_role_mutations.empty()
            && bank.applied_role_metadata.empty()
            && state.region_forwarding_role_journal_nonempty_components == 0U;
    }

    [[nodiscard]] static std::uint64_t forwarding_evaluations(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_evaluations;
    }

    [[nodiscard]] static std::uint64_t private_fanout_suppressions(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_a2_local_fanout_suppressions;
    }

    [[nodiscard]] static std::uint64_t forwarding_declines(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_declines;
    }

    [[nodiscard]] static std::uint64_t native_resume_count(
        const fsim::app::Simulation& simulation, const ProcessId process)
    {
        const auto& counts = simulation.impl_->interpreter->impl_
                                 ->native_process_resume_counts;
        return process < counts.size() ? counts[process] : 0U;
    }

    [[nodiscard]] static bool has_blocking_private_rows(
        const fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            return false;
        }
        const auto component = state.region_authoritative_component_by_signal[signal];
        if (component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results) {
            return false;
        }
        const auto& rows = state.region_local_wave_state_by_component[component]
                               ->forwarding_results->applied_role_metadata;
        return std::ranges::any_of(rows,
            [signal](const auto& row) { return row.signal == signal; });
    }

    static void set_trace_hook(fsim::app::Simulation& simulation,
        void* const context, Scheduler::TraceHook hook)
    {
        simulation.impl_->interpreter->scheduler().set_trace_hook(context, hook);
    }
};

} // namespace fsim::runtime::simir

namespace fsim::tests::app::a2_blocking {

using fsim::app::Simulation;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using fsim::runtime::simir::ProcessId;
using fsim::runtime::simir::SignalId;
using NativeAccess = NativeRegionAllocationTestAccess;
using SemanticSnapshot = NativeAccess::SemanticSnapshot;

enum class FixtureKind {
    positive,
    repeated_write,
    partial_write,
    mixed_boundary,
    unsupported_effect
};

inline void require(const bool condition, const std::string_view message)
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
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error { "failed to set blocking fixture policy" };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        if (had_previous_) {
            static_cast<void>(set(previous_.c_str()));
        } else {
            static_cast<void>(set(nullptr));
        }
    }

private:
    [[nodiscard]] bool set(const char* const value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value == nullptr ? "" : value) == 0;
#else
        return value == nullptr
            ? ::unsetenv(name_.c_str()) == 0
            : ::setenv(name_.c_str(), value, 1) == 0;
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

struct TemporaryDirectory {
    explicit TemporaryDirectory(const std::string_view suffix)
    {
        const auto ticks = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path = std::filesystem::temp_directory_path()
            / ("fsim-a2-blocking-" + std::to_string(ticks)
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

[[nodiscard]] inline std::string fixture_name(const FixtureKind kind)
{
    switch (kind) {
    case FixtureKind::positive: return "positive";
    case FixtureKind::repeated_write: return "repeated_write";
    case FixtureKind::partial_write: return "partial_write";
    case FixtureKind::mixed_boundary: return "mixed_boundary";
    case FixtureKind::unsupported_effect: return "unsupported_effect";
    }
    throw std::logic_error { "unknown blocking fixture kind" };
}

[[nodiscard]] inline fsim::project::Config make_config(
    const std::filesystem::path& root,
    const fsim::project::Optimization optimization,
    const FixtureKind kind)
{
    const auto module = "native_frontier_a2_blocking_" + fixture_name(kind);
    const auto source = root / (module + ".sv");
    std::ofstream output { source, std::ios::binary };
    output << "module " << module << "(output logic sink);\n";
    if (kind == FixtureKind::partial_write
        || kind == FixtureKind::repeated_write
        || kind == FixtureKind::mixed_boundary) {
        output << "  logic source;\n"
               << "  logic [1:0] root;\n";
    } else {
        output << "  logic source;\n"
               << "  logic root;\n";
    }
    if (kind == FixtureKind::positive) {
        output << "  logic middle;\n"
               << "  always_comb begin root = ~source; end\n"
               << "  always_comb begin middle = root; end\n"
               << "  always_comb begin sink = ~middle; end\n";
    } else if (kind == FixtureKind::repeated_write) {
        output << "  always_comb begin root = {2{source}}; root = ~{2{source}}; end\n"
               << "  always_comb begin sink = root[0]; end\n";
    } else if (kind == FixtureKind::partial_write) {
        output << "  always_comb begin root[0] = source; end\n"
               << "  always_comb begin sink = root[0]; end\n";
    } else if (kind == FixtureKind::mixed_boundary) {
        output << "  always_comb begin root = {2{source}}; sink = root[0]; end\n";
    } else {
        output << "  always_comb begin root = ~source; $display(\"blocking effect\"); end\n"
               << "  always_comb begin sink = root; end\n";
    }
    output << "  initial begin source = 1'b0; #1 source = 1'b1; #2 $finish; end\n"
           << "endmodule\n";
    require(static_cast<bool>(output), "could not write parsed blocking fixture");

    fsim::project::Config config;
    config.project.name = module;
    config.project.top = "sv:work." + module;
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

struct Signals {
    SignalId source { };
    SignalId root { };
    std::optional<SignalId> middle;
    SignalId sink { };

    [[nodiscard]] std::vector<SignalId> all() const
    {
        std::vector<SignalId> result { source, root };
        if (middle) {
            result.push_back(*middle);
        }
        result.push_back(sink);
        return result;
    }
};

[[nodiscard]] inline Signals find_signals(
    const Simulation& simulation, const FixtureKind kind)
{
    const auto prefix
        = "native_frontier_a2_blocking_" + fixture_name(kind) + ".";
    const auto source = simulation.find_signal(prefix + "source");
    const auto root = simulation.find_signal(prefix + "root");
    const auto middle = simulation.find_signal(prefix + "middle");
    const auto sink = simulation.find_signal(prefix + "sink");
    require(source && root && sink,
        "parsed blocking fixture must expose source, root, and sink");
    require((kind == FixtureKind::positive) == middle.has_value(),
        "only the positive chain has a middle signal");
    return { *source, *root, middle, *sink };
}

[[nodiscard]] inline std::vector<SemanticSnapshot> snapshot_all(
    const Simulation& simulation, const Signals& signals)
{
    std::vector<SemanticSnapshot> result;
    for (const auto signal : signals.all()) {
        result.push_back(NativeAccess::semantic_snapshot(simulation, signal));
    }
    return result;
}

} // namespace fsim::tests::app::a2_blocking

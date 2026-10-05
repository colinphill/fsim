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
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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

    struct SignalSnapshot {
        std::string current;
        std::string last;
        std::string stored;
        std::string owner;
        std::string raw_driver;
        std::vector<std::pair<ProcessId, std::string>> drivers;
        std::optional<std::string> forced_value;
        std::optional<std::string> forced_mask;
        std::vector<std::uint64_t> direct_aval;
        std::vector<std::uint64_t> direct_bval;
        std::optional<std::uint64_t> direct_scalar_aval;
        std::optional<std::uint64_t> direct_scalar_bval;
        std::optional<std::uint64_t> direct_last_scalar_aval;
        std::optional<std::uint64_t> direct_last_scalar_bval;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        Stamp stamp;
        std::uint64_t value_revision { };
        std::uint64_t authoritative_revision { };
        bool materialization_pending { };
    };

    struct RoleCutRow {
        SignalId signal { std::numeric_limits<SignalId>::max() };
        ProcessId owner { std::numeric_limits<ProcessId>::max() };
        std::uint32_t output_index { std::numeric_limits<std::uint32_t>::max() };
        std::string predicted_value;
        std::vector<std::uint64_t> predicted_aval;
        std::vector<std::uint64_t> predicted_bval;
        SignalSnapshot before;
        SignalSnapshot hidden;
    };

    struct AppliedCut {
        std::size_t component { std::numeric_limits<std::size_t>::max() };
        std::array<RoleCutRow, 2U> rows;
        std::size_t row_count { };
        ProcessId producer0 { std::numeric_limits<ProcessId>::max() };
        ProcessId producer1 { std::numeric_limits<ProcessId>::max() };
        ProcessId child { std::numeric_limits<ProcessId>::max() };
        Key child_key;
        std::uint64_t callback_order { };
        std::uint64_t callback_delta { };
        std::uint64_t callback_round { };
        std::uint64_t native_frontier_dispatches_at_cut { };
        std::uint64_t a4_revision_at_cut { };
        std::uint64_t forwarding_evaluations_at_cut { };
        std::uint64_t forwarding_members_at_cut { };
        std::uint64_t v2_forwarding_prefixes_at_cut { };
        std::uint64_t v2_forwarding_members_at_cut { };
        std::uint64_t a2_local_update_dispatches_at_cut { };
        std::uint64_t a2_local_update_fallbacks_at_cut { };
        std::uint64_t a2_ordinary_internal_updates_at_cut { };
        std::uint64_t producer0_resumes_at_cut { };
        std::uint64_t producer_resumes_at_cut { };
        bool entered { };
    };

    struct UnequalJoinProof {
        Key original_key;
        SignalSnapshot glitch;
        Key retained_slow2_key;
        Key successor_key;
        SignalId slow2_signal_at_cut { std::numeric_limits<SignalId>::max() };
        std::size_t slow2_member_index {
            std::numeric_limits<std::size_t>::max() };
        std::uint64_t slow2_generation_at_cut { };
        std::uint64_t slow2_trigger_mask_at_cut { };
        std::uint64_t forwarding_members_at_cut { };
        SignalSnapshot slow2_roles_at_cut;
        std::array<SignalId, 3U> ancestor_signals { };
        std::array<ProcessId, 3U> ancestor_owners { };
        std::array<SignalSnapshot, 3U> ancestor_hidden;
        std::array<std::string, 3U> ancestor_predicted_values;
        std::array<std::vector<std::uint64_t>, 3U> ancestor_predicted_aval;
        std::array<std::vector<std::uint64_t>, 3U> ancestor_predicted_bval;
        std::uint64_t resume_count_at_enqueue { };
        std::uint64_t resume_count_at_glitch { };
        std::uint64_t slow2_resume_count_at_cut { };
        std::uint64_t forwarding_declines_at_cut { };
        std::uint64_t delta_at_glitch { };
        std::size_t ancestor_row_count { };
        bool original_key_captured { };
        bool ancestor_prefix_captured { };
        bool no_join_row_at_cut { };
        bool retained_slow2_receipt_captured { };
        bool slow2_original_key_consumed_after_flush { };
        bool forwarding_decline_after_cut_observed { };
        bool glitch_captured { };
        bool no_join_role_row { };
        bool original_task_consumed { };
        bool successor_key_captured { };
    };

    [[nodiscard]] static Key copy_key(const RegionFrontierKeyV1& key) noexcept
    {
        return { key.time, key.delta, key.systemverilog_round,
            key.stable_order, key.sequence, key.process_domain, key.phase };
    }

    [[nodiscard]] static bool scheduler_key_precedes(
        const Key& left, const Key& right) noexcept
    {
        if (left.time != right.time) {
            return left.time < right.time;
        }
        if (left.delta != right.delta) {
            return left.delta < right.delta;
        }
        if (left.systemverilog_round != right.systemverilog_round) {
            return left.systemverilog_round < right.systemverilog_round;
        }
        if (left.stable_order != right.stable_order) {
            return left.stable_order < right.stable_order;
        }
        return left.sequence < right.sequence;
    }

    [[nodiscard]] static bool activation_origin_matches_key(
        const RegionKernelActivationOrigin& origin, const Key& key) noexcept
    {
        return static_cast<std::uint32_t>(origin.process_domain)
                == key.process_domain
            && static_cast<std::uint32_t>(origin.phase) == key.phase
            && origin.time == key.time && origin.delta == key.delta
            && origin.systemverilog_round == key.systemverilog_round
            && origin.stable_order == key.stable_order
            && origin.sequence == key.sequence;
    }

    [[nodiscard]] static SignalSnapshot snapshot(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const SignalId signal,
        const ProcessId owner)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error { "the app has no live interpreter" };
        }
        const auto& state = *application.interpreter->impl_;
        if (signal >= state.signals.size()
            || signal >= state.driver_values.size()
            || signal >= state.direct_wide_signal_offsets.size()
            || component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]
            || !state.region_authoritative_state_by_component[component]->valid()
            || !state.region_authoritative_state_by_component[component]
                    ->values().layout().contains(signal)
            || signal >= state.direct_signal_materialization_pending.size()
            || signal >= state.signal_events.size()
            || signal >= state.signal_transactions.size()
            || signal >= state.signal_event_scheduling_stamps.size()
            || signal >= state.signal_value_revisions.size()) {
            throw std::logic_error { "the A2 signal snapshot is incomplete" };
        }

        const auto& authoritative
            = state.region_authoritative_state_by_component[component]->values();
        SignalSnapshot result;
        result.current = authoritative.current(signal).to_msb_string();
        result.last = authoritative.previous(signal).to_msb_string();
        result.stored = authoritative.stored(signal).to_msb_string();
        result.owner = authoritative.owner_value(signal, owner).to_msb_string();
        const auto* const raw = state.driver_values[signal].find(owner);
        if (raw == nullptr) {
            throw std::logic_error { "the A2 signal has no raw owner record" };
        }
        result.raw_driver = raw->value.to_msb_string();
        const auto width = state.signals[signal].initial_value.width();
        const auto word_count = static_cast<std::size_t>(width / 64U)
            + static_cast<std::size_t>(width % 64U != 0U);
        const auto offset = static_cast<std::size_t>(
            state.direct_wide_signal_offsets[signal]);
        if (offset > state.direct_wide_signal_aval.size()
            || word_count > state.direct_wide_signal_aval.size() - offset
            || offset > state.direct_wide_signal_bval.size()
            || word_count > state.direct_wide_signal_bval.size() - offset) {
            throw std::logic_error { "the A2 direct wide mirrors are truncated" };
        }
        const auto direct_aval = std::span<const std::uint64_t> {
            state.direct_wide_signal_aval
        }.subspan(offset, word_count);
        const auto direct_bval = std::span<const std::uint64_t> {
            state.direct_wide_signal_bval
        }.subspan(offset, word_count);
        result.direct_aval.assign(direct_aval.begin(), direct_aval.end());
        result.direct_bval.assign(direct_bval.begin(), direct_bval.end());
        if (width <= 64U) {
            if (signal >= state.direct_signal_aval.size()
                || signal >= state.direct_signal_bval.size()
                || signal >= state.direct_signal_last_aval.size()
                || signal >= state.direct_signal_last_bval.size()) {
                throw std::logic_error {
                    "the A2 direct scalar mirrors are truncated"
                };
            }
            result.direct_scalar_aval = state.direct_signal_aval[signal];
            result.direct_scalar_bval = state.direct_signal_bval[signal];
            result.direct_last_scalar_aval
                = state.direct_signal_last_aval[signal];
            result.direct_last_scalar_bval
                = state.direct_signal_last_bval[signal];
        }
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        const auto& stamp = state.signal_event_scheduling_stamps[signal];
        result.stamp = { static_cast<std::uint32_t>(stamp.origin.process_domain),
            static_cast<std::uint32_t>(stamp.origin.phase),
            stamp.systemverilog_round };
        result.value_revision = state.signal_value_revisions[signal];
        result.authoritative_revision = authoritative.revision();
        result.materialization_pending
            = state.direct_signal_materialization_pending[signal] != 0U;
        return result;
    }

    [[nodiscard]] static SignalSnapshot signal_semantics_snapshot(
        const fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error { "the app has no live interpreter" };
        }
        const auto& state = *application.interpreter->impl_;
        if (signal >= state.signals.size()
            || signal >= state.signal_last_values.size()
            || signal >= state.driven_values.size()
            || signal >= state.driver_values.size()
            || signal >= state.signal_events.size()
            || signal >= state.signal_transactions.size()
            || signal >= state.signal_event_scheduling_stamps.size()
            || signal >= state.signal_value_revisions.size()) {
            throw std::logic_error {
                "the unequal-depth signal metadata is incomplete"
            };
        }

        SignalSnapshot result;
        result.current = state.signals[signal].initial_value.to_msb_string();
        result.last = state.signal_last_values[signal].to_msb_string();
        result.stored = state.driven_values[signal].to_msb_string();
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        const auto& stamp = state.signal_event_scheduling_stamps[signal];
        result.stamp = { static_cast<std::uint32_t>(stamp.origin.process_domain),
            static_cast<std::uint32_t>(stamp.origin.phase),
            stamp.systemverilog_round };
        result.value_revision = state.signal_value_revisions[signal];
        if (signal < state.driver_values.size()) {
            state.driver_values[signal].for_each_in_process_order(
                [&result](const DriverRecord& record) {
                    result.drivers.emplace_back(
                        record.process, record.value.to_msb_string());
                });
        }
        if (signal < state.forced_values.size()
            && state.forced_values[signal]) {
            result.forced_value
                = state.forced_values[signal]->to_msb_string();
        }
        if (signal < state.forced_masks.size()
            && state.forced_masks[signal]) {
            result.forced_mask
                = state.forced_masks[signal]->to_msb_string();
        }
        return result;
    }

    static void capture_unequal_join_glitch(
        const fsim::app::Simulation& simulation,
        Scheduler& scheduler,
        const ProcessId process,
        const SignalId signal,
        const std::size_t component,
        const std::string_view transient_value,
        UnequalJoinProof& proof)
    {
        if (scheduler.now() != 1U) {
            return;
        }
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (process >= state.processes.size()
            || process >= state.region_readiness_queued_by_process.size()
            || process >= state.native_process_resume_counts.size()
            || signal >= state.signals.size()
            || signal >= state.signal_value_revisions.size()
            || component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results
            || component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel) {
            return;
        }

        const auto& local = *state.region_local_wave_state_by_component[component];
        const auto& kernel
            = state.region_activation_programs[component]->forwarding_kernel;
        if (!proof.original_key_captured) {
            const auto& receipt
                = state.region_readiness_queued_by_process[process];
            if (state.processes[process].queued && receipt.key_valid
                && receipt.component == component
                && receipt.generation == local.generation
                && receipt.member < kernel->members.size()
                && kernel->members[receipt.member].process == process
                && receipt.queued_key.time == 1U
                && receipt.queued_key.process_domain
                    == static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog)
                && receipt.queued_key.phase
                    == static_cast<std::uint32_t>(SchedulerPhase::active)
                && receipt.queued_key.stable_order == process) {
                proof.original_key = copy_key(receipt.queued_key);
                proof.resume_count_at_enqueue
                    = state.native_process_resume_counts[process];
                proof.original_key_captured = true;
            }
        }
        if (!proof.original_key_captured || proof.glitch_captured
            || state.signals[signal].initial_value.to_msb_string()
                != transient_value) {
            return;
        }

        proof.glitch = signal_semantics_snapshot(simulation, signal);
        proof.resume_count_at_glitch
            = state.native_process_resume_counts[process];
        proof.delta_at_glitch = scheduler.delta();
        const auto& receipt = state.region_readiness_queued_by_process[process];
        const bool successor_receipt_valid
            = state.processes[process].queued && receipt.key_valid
            && receipt.component == component
            && receipt.generation == local.generation
            && receipt.member < kernel->members.size()
            && kernel->members[receipt.member].process == process;
        if (successor_receipt_valid
            && proof.resume_count_at_enqueue
                < std::numeric_limits<std::uint64_t>::max()
            && proof.resume_count_at_glitch
                == proof.resume_count_at_enqueue + 1U) {
            const auto successor_key = copy_key(receipt.queued_key);
            if (scheduler_key_precedes(proof.original_key, successor_key)) {
                proof.successor_key = successor_key;
                proof.successor_key_captured = true;
                proof.original_task_consumed = true;
            }
        }
        const auto& bank = *local.forwarding_results;
        proof.no_join_role_row
            = bank.applied_role_mutations.size()
                    == bank.applied_role_metadata.size()
            && std::ranges::none_of(bank.applied_role_metadata,
                [signal](const auto& row) { return row.signal == signal; });
        proof.glitch_captured = true;
    }

    static void capture_unequal_join_ancestor_prefix(
        const fsim::app::Simulation& simulation,
        Scheduler& scheduler,
        const SchedulerPhase phase,
        const ProcessId join_process,
        const ProcessId slow2_process,
        const SignalId joined_signal,
        const SignalId slow2_signal,
        const std::size_t component,
        const std::array<SignalId, 3U>& ancestor_signals,
        const std::array<ProcessId, 3U>& ancestor_owners,
        const std::array<SignalSnapshot, 3U>& ancestor_before,
        const std::string_view old_value,
        const std::string_view predicted_value,
        UnequalJoinProof& proof)
    {
        if (proof.ancestor_prefix_captured || scheduler.now() != 1U
            || phase != SchedulerPhase::active
            || !proof.original_key_captured) {
            return;
        }
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (join_process >= state.processes.size()
            || join_process >= state.region_readiness_queued_by_process.size()
            || slow2_process >= state.processes.size()
            || slow2_process >= state.region_readiness_queued_by_process.size()
            || slow2_process >= state.native_process_resume_counts.size()
            || joined_signal >= state.signals.size()
            || slow2_signal >= state.signals.size()
            || component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results
            || component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel) {
            return;
        }

        const auto& local = *state.region_local_wave_state_by_component[component];
        const auto& forwarding
            = *state.region_activation_programs[component]->forwarding_kernel;
        const auto& activation
            = state.region_activation_programs[component]->activation_kernel;
        const auto& join_receipt
            = state.region_readiness_queued_by_process[join_process];
        if (!state.processes[join_process].queued || !join_receipt.key_valid
            || join_receipt.component != component
            || join_receipt.generation != local.generation
            || join_receipt.member >= forwarding.members.size()
            || forwarding.members[join_receipt.member].process != join_process
            || copy_key(join_receipt.queued_key) != proof.original_key
            || join_receipt.queued_key.time != scheduler.now()
            || join_receipt.queued_key.process_domain
                != static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            || join_receipt.queued_key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)
            || join_receipt.queued_key.stable_order != join_process) {
            return;
        }

        const auto& slow2_receipt
            = state.region_readiness_queued_by_process[slow2_process];
        if (!state.processes[slow2_process].queued
            || !slow2_receipt.key_valid
            || slow2_receipt.component != component
            || slow2_receipt.generation != local.generation
            || slow2_receipt.member >= forwarding.members.size()
            || forwarding.members[slow2_receipt.member].process != slow2_process
            || slow2_receipt.queued_key.time != scheduler.now()
            || slow2_receipt.queued_key.process_domain
                != static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            || slow2_receipt.queued_key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)
            || slow2_receipt.queued_key.stable_order != slow2_process
            || !scheduler_key_precedes(proof.original_key,
                copy_key(slow2_receipt.queued_key))
            || state.signals[slow2_signal].initial_value.to_msb_string()
                != old_value
            || state.signals[joined_signal].initial_value.to_msb_string()
                != old_value) {
            return;
        }

        auto& bank = *local.forwarding_results;
        if (!bank.role_journal_enabled
            || bank.applied_role_mutations.size() != ancestor_signals.size()
            || bank.applied_role_metadata.size() != ancestor_signals.size()) {
            return;
        }

        std::array<std::size_t, 3U> row_indices {
            std::numeric_limits<std::size_t>::max(),
            std::numeric_limits<std::size_t>::max(),
            std::numeric_limits<std::size_t>::max() };
        bool rows_valid = true;
        for (std::size_t row_index = 0U;
             row_index < bank.applied_role_metadata.size(); ++row_index) {
            const auto& metadata = bank.applied_role_metadata[row_index];
            if (metadata.signal == joined_signal
                || metadata.signal == slow2_signal
                || (row_index != 0U
                    && bank.applied_role_metadata[row_index - 1U]
                            .callback_order >= metadata.callback_order)) {
                rows_valid = false;
                break;
            }
            const auto ancestor = std::ranges::find(
                ancestor_signals, metadata.signal);
            if (ancestor == ancestor_signals.end()) {
                rows_valid = false;
                break;
            }
            const auto ancestor_index = static_cast<std::size_t>(
                ancestor - ancestor_signals.begin());
            if (row_indices[ancestor_index]
                    != std::numeric_limits<std::size_t>::max()) {
                rows_valid = false;
                break;
            }
            row_indices[ancestor_index] = row_index;
        }
        if (!rows_valid
            || std::ranges::any_of(row_indices, [](const std::size_t row) {
                   return row == std::numeric_limits<std::size_t>::max();
               })) {
            return;
        }

        std::array<SignalSnapshot, 3U> hidden_rows;
        std::array<std::string, 3U> predicted_values;
        std::array<std::vector<std::uint64_t>, 3U> predicted_aval;
        std::array<std::vector<std::uint64_t>, 3U> predicted_bval;
        for (std::size_t ancestor_index = 0U;
             ancestor_index < ancestor_signals.size(); ++ancestor_index) {
            const auto signal = ancestor_signals[ancestor_index];
            const auto& metadata
                = bank.applied_role_metadata[row_indices[ancestor_index]];
            const auto& mutation
                = bank.applied_role_mutations[row_indices[ancestor_index]];
            if (metadata.signal != signal
                || metadata.owner != ancestor_owners[ancestor_index]
                || metadata.callback_time != scheduler.now()
                || metadata.origin.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || metadata.origin.phase != SchedulerPhase::active
                || metadata.output_index >= activation.outputs.size()
                || metadata.output_index >= bank.output_values.size()
                || bank.prepared_role_mutation_ready.size()
                    <= metadata.output_index
                || bank.prepared_role_mutation_ready[metadata.output_index] != 0U
                || mutation.signal != signal || mutation.words.empty()
                || !mutation.any_current_changed
                || bank.output_values[metadata.output_index].to_msb_string()
                    != predicted_value) {
                return;
            }
            const auto& output = activation.outputs[metadata.output_index];
            const auto signal_width
                = state.signals[signal].initial_value.width();
            if (output.signal != signal
                || output.owner != ancestor_owners[ancestor_index]
                || output.width != signal_width || output.offset != 0U
                || output.value_kind != ValueKind::logic4) {
                return;
            }

            auto hidden = snapshot(simulation, component, signal,
                ancestor_owners[ancestor_index]);
            const auto& before = ancestor_before[ancestor_index];
            const auto& live_stamp = state.signal_event_scheduling_stamps[signal];
            if (hidden.current != before.current
                || hidden.last != before.last
                || hidden.stored != before.stored
                || hidden.owner != before.owner
                || hidden.raw_driver != before.raw_driver
                || hidden.direct_aval != before.direct_aval
                || hidden.direct_bval != before.direct_bval
                || hidden.direct_scalar_aval != before.direct_scalar_aval
                || hidden.direct_scalar_bval != before.direct_scalar_bval
                || hidden.direct_last_scalar_aval
                    != before.direct_last_scalar_aval
                || hidden.direct_last_scalar_bval
                    != before.direct_last_scalar_bval
                || hidden.materialization_pending
                    != before.materialization_pending
                || hidden.current != old_value || !hidden.event
                || !hidden.transaction || hidden.event->first != scheduler.now()
                || hidden.transaction->first != scheduler.now()
                || hidden.value_revision <= before.value_revision
                || metadata.expected_signal_event != hidden.event
                || metadata.expected_transaction != hidden.transaction
                || metadata.expected_value_revision != hidden.value_revision
                || metadata.expected_event_stamp.origin.process_domain
                    != live_stamp.origin.process_domain
                || metadata.expected_event_stamp.origin.phase
                    != live_stamp.origin.phase
                || metadata.expected_event_stamp.systemverilog_round
                    != live_stamp.systemverilog_round) {
                return;
            }
            hidden_rows[ancestor_index] = std::move(hidden);
            const auto& predicted
                = bank.output_values[metadata.output_index];
            predicted_values[ancestor_index] = predicted.to_msb_string();
            const auto aval = predicted.aval_words();
            const auto bval = predicted.bval_words();
            predicted_aval[ancestor_index].assign(aval.begin(), aval.end());
            predicted_bval[ancestor_index].assign(bval.begin(), bval.end());
        }

        const auto slow2_output = std::ranges::find(activation.outputs,
            slow2_signal, &RegionConeOutputBinding::signal);
        if (slow2_output == activation.outputs.end()
            || std::ranges::count(activation.outputs, slow2_signal,
                   &RegionConeOutputBinding::signal) != 1U
            || slow2_output->owner != slow2_process
            || slow2_output->width
                != state.signals[slow2_signal].initial_value.width()
            || slow2_output->width != 65U
            || slow2_output->offset != 0U
            || slow2_output->value_kind != ValueKind::logic4
            || slow2_output->domain
                != SignalUpdateDomain::systemverilog_active
            || slow2_output->update_kind
                != RegionUpdateKind::systemverilog_active) {
            return;
        }
        const auto slow2_output_index = static_cast<std::size_t>(
            slow2_output - activation.outputs.begin());
        if (slow2_output_index >= bank.output_values.size()
            || bank.output_values[slow2_output_index].width() != 65U
            || bank.output_values[slow2_output_index].is_logic9()) {
            return;
        }
        auto slow2_roles = snapshot(simulation, component, slow2_signal,
            slow2_process);
        if (slow2_roles.materialization_pending) {
            return;
        }
        proof.retained_slow2_key = copy_key(slow2_receipt.queued_key);
        proof.slow2_signal_at_cut = slow2_signal;
        proof.slow2_member_index = slow2_receipt.member;
        proof.slow2_generation_at_cut = slow2_receipt.generation;
        proof.slow2_trigger_mask_at_cut = slow2_receipt.static_trigger_mask;
        proof.slow2_roles_at_cut = std::move(slow2_roles);
        proof.slow2_resume_count_at_cut
            = state.native_process_resume_counts[slow2_process];
        proof.forwarding_members_at_cut
            = state.systemverilog_wave_profile_region_forwarding_member_consumptions;
        proof.forwarding_declines_at_cut
            = state.systemverilog_wave_profile_region_forwarding_declines;
        proof.retained_slow2_receipt_captured = true;
        proof.ancestor_signals = ancestor_signals;
        proof.ancestor_owners = ancestor_owners;
        proof.ancestor_hidden = std::move(hidden_rows);
        proof.ancestor_predicted_values = std::move(predicted_values);
        proof.ancestor_predicted_aval = std::move(predicted_aval);
        proof.ancestor_predicted_bval = std::move(predicted_bval);
        proof.ancestor_row_count = bank.applied_role_metadata.size();
        proof.no_join_row_at_cut = true;
        proof.ancestor_prefix_captured = true;
    }

    static void capture_slow2_receipt_after_join_flush(
        const fsim::app::Simulation& simulation,
        const ProcessId slow2_process,
        const std::size_t component,
        UnequalJoinProof& proof)
    {
        if (!proof.ancestor_prefix_captured
            || !proof.retained_slow2_receipt_captured
            || proof.slow2_original_key_consumed_after_flush) {
            return;
        }
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (slow2_process >= state.processes.size()
            || slow2_process >= state.region_readiness_queued_by_process.size()
            || slow2_process >= state.native_process_resume_counts.size()
            || component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results
            || component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel) {
            return;
        }

        const auto& local = *state.region_local_wave_state_by_component[component];
        const auto& bank = *local.forwarding_results;
        const auto& forwarding
            = *state.region_activation_programs[component]->forwarding_kernel;
        const auto& prefix = bank.prefix;
        const auto& process_state = state.processes[slow2_process];
        const auto& receipt
            = state.region_readiness_queued_by_process[slow2_process];
        const auto member_index = proof.slow2_member_index;
        if (!bank.applied_role_metadata.empty()
            || !bank.applied_role_mutations.empty()
            || !bank.active || bank.private_epoch_retired
            || bank.runtime_generation != proof.slow2_generation_at_cut
            || local.generation != proof.slow2_generation_at_cut
            || bank.remaining_members != 1U
            || prefix.frontier_generation == 0U
            || prefix.process_domain
                != ProcessSchedulingDomain::systemverilog
            || prefix.phase != SchedulerPhase::active
            || prefix.time != proof.retained_slow2_key.time
            || prefix.delta != proof.retained_slow2_key.delta
            || prefix.systemverilog_round
                != proof.retained_slow2_key.systemverilog_round
            || prefix.frontier_cursor > prefix.frontier_end
            || prefix.tasks.size() != 1U
            || member_index >= forwarding.members.size()
            || forwarding.members[member_index].process != slow2_process
            || bank.member_indices.size() != 1U
            || bank.member_indices.front() != member_index
            || member_index >= bank.member_consumed.size()
            || bank.member_consumed[member_index] != 1U
            || process_state.queued || !process_state.waiting_on_static
            || process_state.status != ProcessStatus::waiting
            || !process_state.region_kernel_completion_boundary_validated
            || receipt.key_valid
            || state.native_process_resume_counts[slow2_process]
                != proof.slow2_resume_count_at_cut
            || proof.forwarding_members_at_cut
                == std::numeric_limits<std::uint64_t>::max()
            || state.systemverilog_wave_profile_region_forwarding_member_consumptions
                != proof.forwarding_members_at_cut + 1U) {
            return;
        }

        const auto& task = prefix.tasks.front();
        if (task.task_ordinal < prefix.frontier_cursor
            || task.task_ordinal >= prefix.frontier_end
            || task.member.process != slow2_process
            || task.member.trigger_mask != proof.slow2_trigger_mask_at_cut
            || !activation_origin_matches_key(task.member.origin,
                proof.retained_slow2_key)) {
            return;
        }

        // The observation/decline barrier must materialize every previously
        // applied ancestor row before the slow2 native member is consumed.
        // Preserve the original row-by-row role, mirror, and metadata proof.
        for (std::size_t index = 0U;
             index < proof.ancestor_signals.size(); ++index) {
            const auto actual = snapshot(simulation, component,
                proof.ancestor_signals[index], proof.ancestor_owners[index]);
            const auto& hidden = proof.ancestor_hidden[index];
            if (actual.current != proof.ancestor_predicted_values[index]
                || actual.last != hidden.current
                || actual.stored != proof.ancestor_predicted_values[index]
                || actual.owner != proof.ancestor_predicted_values[index]
                || actual.raw_driver
                    != proof.ancestor_predicted_values[index]
                || actual.direct_aval
                    != proof.ancestor_predicted_aval[index]
                || actual.direct_bval
                    != proof.ancestor_predicted_bval[index]
                || actual.direct_scalar_aval != hidden.direct_scalar_aval
                || actual.direct_scalar_bval != hidden.direct_scalar_bval
                || actual.direct_last_scalar_aval
                    != hidden.direct_last_scalar_aval
                || actual.direct_last_scalar_bval
                    != hidden.direct_last_scalar_bval
                || actual.materialization_pending
                    != hidden.materialization_pending
                || actual.event != hidden.event
                || actual.transaction != hidden.transaction
                || actual.stamp != hidden.stamp
                || actual.value_revision != hidden.value_revision) {
                return;
            }
        }

        const auto actual = snapshot(simulation, component,
            proof.slow2_signal_at_cut, slow2_process);
        const auto& before = proof.slow2_roles_at_cut;
        if (actual.current != before.current || actual.last != before.last
            || actual.stored != before.stored || actual.owner != before.owner
            || actual.raw_driver != before.raw_driver
            || actual.direct_aval != before.direct_aval
            || actual.direct_bval != before.direct_bval
            || actual.direct_scalar_aval != before.direct_scalar_aval
            || actual.direct_scalar_bval != before.direct_scalar_bval
            || actual.direct_last_scalar_aval
                != before.direct_last_scalar_aval
            || actual.direct_last_scalar_bval
                != before.direct_last_scalar_bval
            || actual.materialization_pending != before.materialization_pending
            || actual.event != before.event
            || actual.transaction != before.transaction
            || actual.stamp != before.stamp
            || actual.value_revision != before.value_revision) {
            return;
        }
        proof.slow2_original_key_consumed_after_flush = true;
        proof.forwarding_decline_after_cut_observed
            = state.systemverilog_wave_profile_region_forwarding_declines
                > proof.forwarding_declines_at_cut;
    }

    [[nodiscard]] static bool slow2_receipt_consumed_once(
        const UnequalJoinProof& proof) noexcept
    {
        return proof.slow2_original_key_consumed_after_flush
            && proof.forwarding_decline_after_cut_observed
            && proof.retained_slow2_key.time == 1U
            && proof.retained_slow2_key.process_domain
                == static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            && proof.retained_slow2_key.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active);
    }

    // The receipt authenticates the checked process activation. Its ordinary
    // WriteUpdate publication runs through a separate scheduler callback, so
    // the event stamp is validated by domain/phase and semantic parity below,
    // not equated to the activation key's SV round.
    [[nodiscard]] static bool unequal_join_checked_glitch(
        const UnequalJoinProof& proof,
        const SignalSnapshot& initial,
        const ProcessId process,
        const std::string_view old_value,
        const std::string_view transient_value) noexcept
    {
        return proof.original_key_captured && proof.glitch_captured
            && proof.no_join_role_row && proof.original_task_consumed
            && proof.successor_key_captured
            && scheduler_key_precedes(
                proof.original_key, proof.successor_key)
            && proof.successor_key.time == proof.original_key.time
            && proof.successor_key.process_domain
                == proof.original_key.process_domain
            && proof.successor_key.phase == proof.original_key.phase
            && proof.successor_key.stable_order
                == proof.original_key.stable_order
            && proof.original_key.time == 1U
            && proof.original_key.delta == proof.delta_at_glitch
            && proof.original_key.process_domain
                == static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            && proof.original_key.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && proof.original_key.stable_order == process
            && proof.glitch.current == transient_value
            && proof.glitch.last == old_value
            && proof.glitch.event
            && proof.glitch.event->first == proof.original_key.time
            && proof.glitch.transaction
            && proof.glitch.transaction->first == proof.original_key.time
            && proof.glitch.stamp.process_domain
                == proof.original_key.process_domain
            && proof.glitch.stamp.phase == proof.original_key.phase
            && proof.glitch.value_revision > initial.value_revision
            && proof.resume_count_at_enqueue
                < std::numeric_limits<std::uint64_t>::max()
            && proof.resume_count_at_glitch
                == proof.resume_count_at_enqueue + 1U;
    }

    [[nodiscard]] static bool interpreter_child_queued_at_cut(
        const fsim::app::Simulation& simulation,
        const Scheduler& scheduler,
        const ProcessId child,
        const std::array<SignalId, 3U>& predecessor_signals,
        const SignalId child_output_signal,
        const SchedulerPhase phase)
    {
        if (scheduler.now() != 1U || phase != SchedulerPhase::active
            || simulation.impl_ == nullptr
            || simulation.impl_->interpreter == nullptr) {
            return false;
        }
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (child >= state.processes.size()
            || child_output_signal >= state.driver_values.size()
            || state.driver_values[child_output_signal].find(child) == nullptr) {
            return false;
        }
        const auto& process = state.processes[child];
        if (!process.queued || !process.waiting_on_static) {
            return false;
        }

        const auto expected = std::string(65U, '1');
        for (const auto signal : predecessor_signals) {
            if (signal >= state.signals.size()
                || state.signals[signal].initial_value.to_msb_string()
                    != expected) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static std::size_t component_for_signal(
        const fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error { "the app has no live interpreter" };
        }
        const auto& state = *application.interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            throw std::logic_error { "the A2 signal has no component mapping" };
        }
        const auto component
            = state.region_authoritative_component_by_signal[signal];
        if (component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel) {
            throw std::logic_error { "the A2 signal has no forwarding certificate" };
        }
        return component;
    }

    [[nodiscard]] static ProcessId forwarding_output_owner(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel) {
            throw std::logic_error { "the A2 component has no forwarding kernel" };
        }
        const auto& kernel
            = state.region_activation_programs[component]->activation_kernel;
        std::optional<ProcessId> owner;
        for (const auto& output : kernel.outputs) {
            if (output.signal != signal || output.offset != 0U
                || output.width != state.signals[signal].initial_value.width()
                || output.value_kind != ValueKind::logic4) {
                continue;
            }
            if (owner && *owner != output.owner) {
                throw std::logic_error { "the A2 output has multiple owners" };
            }
            owner = output.owner;
        }
        if (!owner) {
            throw std::logic_error { "the A2 output has no certified owner" };
        }
        return *owner;
    }

    [[nodiscard]] static std::uint64_t forwarding_evaluations(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_evaluations;
    }

    [[nodiscard]] static std::uint64_t forwarding_members(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_member_consumptions;
    }

    [[nodiscard]] static std::uint64_t native_frontier_dispatches(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_native_frontier_member_dispatches;
    }

    [[nodiscard]] static std::uint64_t v2_forwarding_prefixes(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_v2_selected_forwarding_prefixes;
    }

    [[nodiscard]] static std::uint64_t v2_forwarding_members(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_v2_selected_forwarding_members;
    }

    [[nodiscard]] static std::uint64_t a2_local_update_dispatches(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_a2_local_update_dispatches;
    }

    [[nodiscard]] static std::uint64_t a2_local_update_fallbacks(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_a2_local_update_fallbacks;
    }

    [[nodiscard]] static std::uint64_t a2_ordinary_internal_updates(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_a2_ordinary_internal_updates;
    }

    [[nodiscard]] static bool try_capture_applied_prefix(
        fsim::app::Simulation& simulation,
        Scheduler& scheduler,
        const std::array<SignalId, 2U>& signals,
        const std::array<ProcessId, 2U>& owners,
        const std::array<SignalSnapshot, 2U>& before,
        const std::uint64_t forwarding_evaluations_before,
        const std::uint64_t forwarding_members_before,
        const std::uint64_t v2_forwarding_prefixes_before,
        const std::uint64_t v2_forwarding_members_before,
        const std::uint64_t a2_local_dispatches_before,
        const SchedulerPhase phase,
        AppliedCut& cut)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter || scheduler.now() != 1U
            || phase != SchedulerPhase::active) {
            return false;
        }
        auto& state = *application.interpreter->impl_;
        if (signals[0U] >= state.region_authoritative_component_by_signal.size()
            || signals[1U] >= state.region_authoritative_component_by_signal.size()) {
            return false;
        }
        const auto component
            = state.region_authoritative_component_by_signal[signals[0U]];
        if (state.region_authoritative_component_by_signal[signals[1U]]
                != component
            || component >= state.region_authoritative_state_by_component.size()
            || component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel
            || component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results) {
            return false;
        }

        auto& local = *state.region_local_wave_state_by_component[component];
        auto& bank = *local.forwarding_results;
        const auto& forwarding
            = *state.region_activation_programs[component]->forwarding_kernel;
        const auto& activation
            = state.region_activation_programs[component]->activation_kernel;
        const auto& authoritative
            = state.region_authoritative_state_by_component[component];
        if (!authoritative || !authoritative->valid()
            || !bank.role_journal_enabled
            || bank.applied_role_mutations.size() != signals.size()
            || bank.applied_role_metadata.size() != signals.size()) {
            return false;
        }

        for (std::size_t row_index = 0U; row_index < signals.size(); ++row_index) {
            const auto& metadata = bank.applied_role_metadata[row_index];
            const auto& mutation = bank.applied_role_mutations[row_index];
            const auto signal_width
                = state.signals[signals[row_index]].initial_value.width();
            if (metadata.signal != signals[row_index]
                || metadata.owner != owners[row_index]
                || metadata.callback_time != 1U
                || metadata.origin.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || metadata.origin.phase != SchedulerPhase::active
                || metadata.output_index
                    >= activation.outputs.size()
                || (row_index != 0U
                    && bank.applied_role_metadata[row_index - 1U]
                            .callback_order >= metadata.callback_order)
                || mutation.signal != signals[row_index]
                || mutation.words.empty() || !mutation.any_current_changed
                || metadata.owner >= state.driver_values.size()
                || metadata.owner >= state.native_process_resume_counts.size()
                || metadata.output_index >= bank.output_values.size()
                || bank.output_values[metadata.output_index].width()
                    != signal_width
                || !authoritative->values().packed_signal_slots_bound(
                    signals[row_index])
                || !authoritative->values().packed_owner_slot_bound(
                    signals[row_index], owners[row_index])) {
                return false;
            }
            const auto& output
                = activation.outputs[metadata.output_index];
            if (output.signal != signals[row_index]
                || output.owner != owners[row_index]
                || output.width != signal_width || output.offset != 0U
                || output.value_kind != ValueKind::logic4) {
                return false;
            }
            if (row_index + 1U == signals.size()
                && (metadata.callback_delta != scheduler.delta()
                    || metadata.callback_systemverilog_round
                        != scheduler.systemverilog_round())) {
                return false;
            }

            const auto hidden = snapshot(
                simulation, component, signals[row_index], owners[row_index]);
            if (hidden.current != before[row_index].current
                || hidden.last != before[row_index].last
                || hidden.stored != before[row_index].stored
                || hidden.owner != before[row_index].owner
                || hidden.raw_driver != before[row_index].raw_driver
                || hidden.direct_aval != before[row_index].direct_aval
                || hidden.direct_bval != before[row_index].direct_bval
                || hidden.direct_scalar_aval
                    != before[row_index].direct_scalar_aval
                || hidden.direct_scalar_bval
                    != before[row_index].direct_scalar_bval
                || hidden.direct_last_scalar_aval
                    != before[row_index].direct_last_scalar_aval
                || hidden.direct_last_scalar_bval
                    != before[row_index].direct_last_scalar_bval
                || hidden.materialization_pending
                    != before[row_index].materialization_pending
                || !hidden.event || !hidden.transaction
                || hidden.event->first != 1U
                || hidden.transaction->first != 1U
                || hidden.value_revision <= before[row_index].value_revision
                || metadata.expected_signal_event != hidden.event
                || metadata.expected_transaction != hidden.transaction
                || metadata.expected_value_revision != hidden.value_revision
                || static_cast<std::uint32_t>(
                    metadata.expected_event_stamp.origin.process_domain)
                    != hidden.stamp.process_domain
                || static_cast<std::uint32_t>(
                    metadata.expected_event_stamp.origin.phase)
                    != hidden.stamp.phase
                || metadata.expected_event_stamp.systemverilog_round
                    != hidden.stamp.systemverilog_round) {
                return false;
            }

            auto& cut_row = cut.rows[row_index];
            cut_row.signal = signals[row_index];
            cut_row.owner = owners[row_index];
            cut_row.output_index
                = static_cast<std::uint32_t>(metadata.output_index);
            cut_row.predicted_value
                = bank.output_values[metadata.output_index].to_msb_string();
            const auto predicted_aval
                = bank.output_values[metadata.output_index].aval_words();
            const auto predicted_bval
                = bank.output_values[metadata.output_index].bval_words();
            cut_row.predicted_aval.assign(
                predicted_aval.begin(), predicted_aval.end());
            cut_row.predicted_bval.assign(
                predicted_bval.begin(), predicted_bval.end());
            cut_row.before = before[row_index];
            cut_row.hidden = hidden;
        }

        const auto& last_metadata = bank.applied_role_metadata.back();
        std::optional<std::size_t> writer_member;
        for (std::size_t member = 0U; member < forwarding.members.size(); ++member) {
            if (forwarding.members[member].process == owners.back()) {
                if (writer_member) {
                    return false;
                }
                writer_member = member;
            }
        }
        if (!writer_member) {
            return false;
        }

        std::optional<ProcessId> child_process;
        for (const auto& member : forwarding.members) {
            if (member.read_begin > forwarding.internal_reads.size()
                || member.read_count
                    > forwarding.internal_reads.size() - member.read_begin) {
                return false;
            }
            for (std::size_t read_index = member.read_begin;
                 read_index < member.read_begin + member.read_count; ++read_index) {
                const auto& read = forwarding.internal_reads[read_index];
                if (read.signal == signals.back()
                    && read.writer_member_index == *writer_member) {
                    if (child_process && *child_process != member.process) {
                        return false;
                    }
                    child_process = member.process;
                }
            }
        }
        if (!child_process
            || *child_process >= state.processes.size()
            || *child_process >= state.region_readiness_queued_by_process.size()
            || *child_process >= state.native_process_resume_counts.size()) {
            return false;
        }
        const auto& child_state = state.processes[*child_process];
        const auto& queued
            = state.region_readiness_queued_by_process[*child_process];
        if (!child_state.queued || !queued.key_valid
            || queued.component != component
            || queued.generation != local.generation
            || queued.member >= forwarding.members.size()
            || forwarding.members[queued.member].process != *child_process
            || queued.queued_key.time != scheduler.now()
            || queued.queued_key.process_domain
                != static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            || queued.queued_key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)
            || queued.queued_key.stable_order != *child_process) {
            return false;
        }

        if (state.systemverilog_wave_profile_region_forwarding_evaluations
                != forwarding_evaluations_before + 1U
            || state.systemverilog_wave_profile_region_forwarding_member_consumptions
                <= forwarding_members_before
            || state.systemverilog_wave_profile_v2_selected_forwarding_prefixes
                != v2_forwarding_prefixes_before + signals.size()
            || state.systemverilog_wave_profile_v2_selected_forwarding_members
                != v2_forwarding_members_before + signals.size()
            || state.systemverilog_wave_profile_a2_local_update_dispatches
                != a2_local_dispatches_before + signals.size()) {
            return false;
        }

        cut.component = component;
        cut.row_count = signals.size();
        cut.producer0 = owners[0U];
        cut.producer1 = owners[1U];
        cut.child = *child_process;
        cut.child_key = copy_key(queued.queued_key);
        cut.callback_order = last_metadata.callback_order;
        cut.callback_delta = last_metadata.callback_delta;
        cut.callback_round = last_metadata.callback_systemverilog_round;
        cut.native_frontier_dispatches_at_cut
            = state.systemverilog_wave_profile_native_frontier_member_dispatches;
        cut.a4_revision_at_cut = authoritative->values().revision();
        cut.forwarding_evaluations_at_cut
            = state.systemverilog_wave_profile_region_forwarding_evaluations;
        cut.forwarding_members_at_cut
            = state.systemverilog_wave_profile_region_forwarding_member_consumptions;
        cut.v2_forwarding_prefixes_at_cut
            = state.systemverilog_wave_profile_v2_selected_forwarding_prefixes;
        cut.v2_forwarding_members_at_cut
            = state.systemverilog_wave_profile_v2_selected_forwarding_members;
        cut.a2_local_update_dispatches_at_cut
            = state.systemverilog_wave_profile_a2_local_update_dispatches;
        cut.a2_local_update_fallbacks_at_cut
            = state.systemverilog_wave_profile_a2_local_update_fallbacks;
        cut.a2_ordinary_internal_updates_at_cut
            = state.systemverilog_wave_profile_a2_ordinary_internal_updates;
        cut.producer0_resumes_at_cut
            = state.native_process_resume_counts[owners[0U]];
        cut.producer_resumes_at_cut
            = state.native_process_resume_counts[owners.back()];
        cut.entered = true;
        return true;
    }

    [[nodiscard]] static bool packed_roles_bound(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const SignalId signal,
        const ProcessId owner)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return false;
        }
        const auto& state = *application.interpreter->impl_;
        return component < state.region_authoritative_state_by_component.size()
            && state.region_authoritative_state_by_component[component]
            && state.region_authoritative_state_by_component[component]->valid()
            && state.region_authoritative_state_by_component[component]
                   ->values().packed_slots_bound()
            && state.region_authoritative_state_by_component[component]
                   ->values().packed_signal_slots_bound(signal)
            && state.region_authoritative_state_by_component[component]
                   ->values().packed_owner_slot_bound(signal, owner);
    }

    [[nodiscard]] static bool journal_empty_and_retired(
        const fsim::app::Simulation& simulation,
        const AppliedCut& cut)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (cut.component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[cut.component]
            ->forwarding_results) {
            return false;
        }
        const auto& bank = *state.region_local_wave_state_by_component[
            cut.component]->forwarding_results;
        return bank.applied_role_mutations.empty()
            && bank.applied_role_metadata.empty()
            && !bank.role_journal_enabled && bank.private_epoch_retired
            && state.region_forwarding_role_journal_nonempty_components == 0U;
    }

    [[nodiscard]] static bool child_receipt_unchanged(
        const fsim::app::Simulation& simulation,
        const AppliedCut& cut)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (cut.child >= state.region_readiness_queued_by_process.size()
            || cut.child >= state.processes.size()) {
            return false;
        }
        const auto& receipt = state.region_readiness_queued_by_process[cut.child];
        return state.processes[cut.child].queued && receipt.key_valid
            && receipt.component == cut.component
            && copy_key(receipt.queued_key) == cut.child_key;
    }

    [[nodiscard]] static std::uint64_t process_resume_count(
        const fsim::app::Simulation& simulation,
        const ProcessId process)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        return process < state.native_process_resume_counts.size()
            ? state.native_process_resume_counts[process] : 0U;
    }

    static void set_trace_hook(fsim::app::Simulation& simulation,
        void* context, Scheduler::TraceHook hook) noexcept
    {
        if (simulation.impl_ && simulation.impl_->interpreter) {
            simulation.impl_->interpreter->scheduler().set_trace_hook(
                context, hook);
        }
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
using Key = NativeRegionAllocationTestAccess::Key;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error { "failed to configure A2 app test" };
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
            / ("fsim-native-frontier-a2-" + std::to_string(ticks)
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

[[nodiscard]] fsim::project::Config make_linear_config(
    const std::filesystem::path& root,
    const fsim::project::Optimization optimization,
    const std::uint32_t width)
{
    require(width != 0U, "the A2 linear fixture needs a positive width");
    const auto source = root / "native_frontier_a2_applied_prefix.sv";
    const auto packed_range
        = "[" + std::to_string(width - 1U) + ":0]";
    const auto all_ones = std::string(width, '1');
    std::ofstream output { source, std::ios::binary };
    output << "module native_frontier_a2_applied_prefix(output wire "
           << packed_range << " sink);\n"
           << "  logic " << packed_range << " source;\n"
           << "  wire " << packed_range << " stage0;\n"
           << "  wire " << packed_range << " stage1;\n"
           << "  wire " << packed_range << " stage2;\n"
           << "  assign stage0 = source;\n"
           << "  assign stage1 = stage0;\n"
           << "  assign stage2 = stage1;\n"
           << "  assign sink = stage2;\n\n"
           << "  initial begin\n"
           << "    source = " << width << "'b0;\n"
           << "    #1 source = " << width << "'b" << all_ones << ";\n"
           << "    #2 $finish;\n"
           << "  end\n"
           << "endmodule\n";
    require(static_cast<bool>(output), "could not write A2 linear fixture");

    fsim::project::Config config;
    config.project.name = "native-frontier-a2-applied-prefix";
    config.project.top = "sv:work.native_frontier_a2_applied_prefix";
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

[[nodiscard]] fsim::project::Config make_unequal_depth_config(
    const std::filesystem::path& root,
    const fsim::project::Optimization optimization)
{
    const auto source = root / "native_frontier_a2_unequal_depth.sv";
    std::ofstream output { source, std::ios::binary };
    output << R"(
module native_frontier_a2_unequal_depth(output wire [64:0] sink);
  logic [64:0] source;
  wire [64:0] root_stage;
  wire [64:0] fast;
  wire [64:0] slow1;
  wire [64:0] slow2;
  wire [64:0] joined;
  assign root_stage = source;
  assign fast = root_stage;
  assign slow1 = root_stage;
  assign joined = fast ^ slow2;
  assign slow2 = slow1;
  assign sink = joined;

  initial begin
    source = 65'b0;
    #1 source = 65'h1ffffffffffffffff;
    #2 $finish;
  end
endmodule
)";
    require(static_cast<bool>(output), "could not write A2 unequal-depth fixture");

    fsim::project::Config config;
    config.project.name = "native-frontier-a2-unequal-depth";
    config.project.top = "sv:work.native_frontier_a2_unequal_depth";
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

struct TraceProbe {
    Key child_key;
    std::array<ProcessId, 2U> producers { UINT32_MAX, UINT32_MAX };
    ProcessId interpreter_child { UINT32_MAX };
    bool capture_interpreter_child_key { };
    bool interpreter_child_key_captured { };
    std::uint64_t child_task_begin { };
    std::uint64_t child_batch_begin { };
    std::uint64_t child_end { };
    std::uint64_t producer_replay { };
    bool child_batch_begin_one_member { true };

    static void receive(void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& probe = *static_cast<TraceProbe*>(context);
        if (probe.capture_interpreter_child_key
            && !probe.interpreter_child_key_captured
            && record.kind == SchedulerTraceKind::task_begin
            && record.systemverilog && record.phase
            && record.time == 1U
            && record.order == probe.interpreter_child
            && *record.phase == SchedulerPhase::active) {
            // The interpreter has no compiled readiness receipt. Capture the
            // scheduler's actual key when its already-queued child begins.
            probe.child_key = { record.time, record.delta,
                record.systemverilog_round, record.order, record.sequence,
                static_cast<std::uint32_t>(
                    fsim::runtime::simir::ProcessSchedulingDomain::systemverilog),
                static_cast<std::uint32_t>(*record.phase) };
            probe.interpreter_child_key_captured = true;
        }
        if ((record.kind == SchedulerTraceKind::task_begin
                || record.kind == SchedulerTraceKind::batch_begin)
            && record.systemverilog && record.phase
            && (record.order == probe.producers[0U]
                || record.order == probe.producers[1U])
            && record.time >= probe.child_key.time
            && static_cast<std::uint32_t>(*record.phase)
                == static_cast<std::uint32_t>(SchedulerPhase::active)) {
            ++probe.producer_replay;
        }
        if (!record.systemverilog || !record.phase
            || record.time != probe.child_key.time
            || record.delta != probe.child_key.delta
            || record.systemverilog_round
                != probe.child_key.systemverilog_round
            || record.order != probe.child_key.stable_order
            || record.sequence != probe.child_key.sequence
            || static_cast<std::uint32_t>(*record.phase)
                != probe.child_key.phase) {
            return;
        }
        if (record.kind == SchedulerTraceKind::task_begin) {
            ++probe.child_task_begin;
        } else if (record.kind == SchedulerTraceKind::batch_begin) {
            ++probe.child_batch_begin;
            probe.child_batch_begin_one_member
                = probe.child_batch_begin_one_member && record.count == 1U;
        } else if (record.kind == SchedulerTraceKind::task_end) {
            ++probe.child_end;
        }
    }
};

struct LinearResult {
    std::array<std::string, 5U> final_values;
    std::uint64_t forwarding_evaluations { };
    std::uint64_t forwarding_members { };
    std::uint64_t v2_forwarding_prefixes { };
    std::uint64_t v2_forwarding_members { };
};

struct UnequalDepthResult {
    std::array<std::string, 7U> final_values;
    NativeRegionAllocationTestAccess::SignalSnapshot joined;
    NativeRegionAllocationTestAccess::SignalSnapshot slow2;
};

enum class PublicMutationKind {
    deposit,
    force
};

struct PublicMutationResult {
    std::array<std::string, 5U> final_values;
    std::array<NativeRegionAllocationTestAccess::SignalSnapshot, 5U>
        final_signals;
    Key original_child_key;
    ProcessId child { UINT32_MAX };
    bool forced { };
};

[[nodiscard]] bool same_key_order(
    const Key& left, const Key& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_public_mutation_signal(
    const NativeRegionAllocationTestAccess::SignalSnapshot& left,
    const NativeRegionAllocationTestAccess::SignalSnapshot& right) noexcept
{
    return left.current == right.current
        && left.last == right.last
        && left.drivers == right.drivers
        && left.forced_value == right.forced_value
        && left.forced_mask == right.forced_mask
        && left.event == right.event
        && left.transaction == right.transaction
        && left.stamp == right.stamp;
}

[[nodiscard]] bool same_unequal_join_semantics(
    const NativeRegionAllocationTestAccess::SignalSnapshot& left,
    const NativeRegionAllocationTestAccess::SignalSnapshot& right) noexcept
{
    return left.current == right.current
        && left.last == right.last
        && left.stored == right.stored
        && left.drivers == right.drivers
        && left.event == right.event
        && left.transaction == right.transaction
        && left.stamp == right.stamp
        && left.value_revision == right.value_revision;
}

[[nodiscard]] std::array<std::string, 5U> read_linear_values(
    const Simulation& simulation,
    const std::array<SignalId, 5U>& signals)
{
    std::array<std::string, 5U> values;
    for (std::size_t index = 0U; index < signals.size(); ++index) {
        values[index] = simulation.read_signal(signals[index]).to_msb_string();
    }
    return values;
}

[[nodiscard]] LinearResult run_compiled_linear(
    const fsim::project::Optimization optimization,
    const std::string_view suffix,
    const std::uint32_t width)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_linear_config(temporary.path, optimization, width);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the A2 linear source must elaborate");

    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() != 0U,
        "the A2 witness needs installed compiled processes");

    const auto source = simulation.find_signal(
        "native_frontier_a2_applied_prefix.source");
    const auto stage0 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage0");
    const auto stage1 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage1");
    const auto stage2 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage2");
    const auto sink = simulation.find_signal(
        "native_frontier_a2_applied_prefix.sink");
    require(source && stage0 && stage1 && stage2 && sink,
        "the A2 fixture must expose its source and complete register chain");

    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "time-zero activity must settle before the measured A2 edge");
    const std::array<SignalId, 5U> signal_ids {
        *source, *stage0, *stage1, *stage2, *sink };
    const std::array<SignalId, 2U> applied_signals { *stage0, *stage1 };
    const auto component
        = NativeRegionAllocationTestAccess::component_for_signal(
            simulation, *stage0);
    require(NativeRegionAllocationTestAccess::component_for_signal(
                simulation, *stage1) == component,
        "the first two linear outputs must share one certified component");
    const std::array<ProcessId, 2U> owners {
        NativeRegionAllocationTestAccess::forwarding_output_owner(
            simulation, component, *stage0),
        NativeRegionAllocationTestAccess::forwarding_output_owner(
            simulation, component, *stage1) };
    for (std::size_t index = 0U; index < applied_signals.size(); ++index) {
        require(NativeRegionAllocationTestAccess::packed_roles_bound(
                    simulation, component, applied_signals[index], owners[index]),
            "the A2 path must begin with bound A4 roles for each internal output");
    }
    const std::array<NativeRegionAllocationTestAccess::SignalSnapshot, 2U> before {
        NativeRegionAllocationTestAccess::snapshot(
            simulation, component, *stage0, owners[0U]),
        NativeRegionAllocationTestAccess::snapshot(
            simulation, component, *stage1, owners[1U]) };
    const auto evaluations_before
        = NativeRegionAllocationTestAccess::forwarding_evaluations(simulation);
    const auto members_before
        = NativeRegionAllocationTestAccess::forwarding_members(simulation);
    const auto v2_prefixes_before
        = NativeRegionAllocationTestAccess::v2_forwarding_prefixes(simulation);
    const auto v2_members_before
        = NativeRegionAllocationTestAccess::v2_forwarding_members(simulation);
    const auto a2_local_dispatches_before
        = NativeRegionAllocationTestAccess::a2_local_update_dispatches(
            simulation);

    NativeRegionAllocationTestAccess::AppliedCut cut;
    const auto hook = simulation.add_safe_point_hook(
        [&simulation, applied_signals, owners, before,
            evaluations_before, members_before, v2_prefixes_before,
            v2_members_before, a2_local_dispatches_before, &cut](
                Scheduler& scheduler, const SchedulerPhase phase) {
            if (cut.entered) {
                return;
            }
            if (NativeRegionAllocationTestAccess::try_capture_applied_prefix(
                    simulation, scheduler, applied_signals, owners, before,
                    evaluations_before, members_before,
                    v2_prefixes_before, v2_members_before,
                    a2_local_dispatches_before, phase, cut)) {
                scheduler.request_stop();
            }
        });
    const auto stopped = simulation.run(1U);
    simulation.remove_safe_point_hook(hook);
    require(stopped.status == RunStatus::stopped && cut.entered
            && cut.row_count == applied_signals.size() && stopped.time == 1U,
        "the witness must stop after two authentic private role callbacks at time one");
    require(cut.rows[0U].signal == *stage0 && cut.rows[1U].signal == *stage1
            && cut.callback_order > 0U
            && cut.child_key.time == 1U
            && cut.child_key.process_domain
                == static_cast<std::uint32_t>(
                    fsim::runtime::simir::ProcessSchedulingDomain::systemverilog)
            && cut.child_key.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && cut.child_key.stable_order == cut.child,
        "the applied rows and following member must retain their authentic order and key");
    require(cut.forwarding_evaluations_at_cut
                == evaluations_before + 1U
            && cut.forwarding_members_at_cut > members_before
            && cut.v2_forwarding_prefixes_at_cut
                == v2_prefixes_before + applied_signals.size()
            && cut.v2_forwarding_members_at_cut
                == v2_members_before + applied_signals.size()
            && cut.a2_local_update_dispatches_at_cut
                == a2_local_dispatches_before + applied_signals.size(),
        "two V2-selected flattened prefixes must grow the private applied journal by two rows");

    const auto value_at_read
        = simulation.read_signal(*stage0).to_msb_string();
    require(value_at_read == cut.rows[0U].predicted_value,
        "public read must materialize the first private output value");
    for (std::size_t index = 0U; index < applied_signals.size(); ++index) {
        const auto after_read = NativeRegionAllocationTestAccess::snapshot(
            simulation, component, applied_signals[index], owners[index]);
        const auto& row = cut.rows[index];
        require(after_read.current == row.predicted_value
                && after_read.stored == row.predicted_value
                && after_read.owner == row.predicted_value
                && after_read.raw_driver == row.predicted_value
                && after_read.last == row.before.current
                && after_read.direct_aval == row.predicted_aval
                && after_read.direct_bval == row.predicted_bval
                && after_read.event == row.hidden.event
                && after_read.transaction == row.hidden.transaction
                && after_read.stamp == row.hidden.stamp
                && after_read.value_revision == row.hidden.value_revision
                && !after_read.materialization_pending,
            "the observation flush must publish each exact role without replaying metadata");
        if (row.before.direct_scalar_aval) {
            require(after_read.direct_scalar_aval
                        == std::optional<std::uint64_t> {
                            row.predicted_aval.front() }
                    && after_read.direct_scalar_bval
                        == std::optional<std::uint64_t> {
                            row.predicted_bval.front() }
                    && after_read.direct_last_scalar_aval
                        == row.before.direct_scalar_aval
                    && after_read.direct_last_scalar_bval
                        == row.before.direct_scalar_bval,
                "narrow materialization must publish scalar CURRENT and old LAST mirrors");
        }
        require(!NativeRegionAllocationTestAccess::packed_roles_bound(
                    simulation, component, applied_signals[index], owners[index]),
            "the public read must demote the observed component after its flush");
    }
    const auto after_first_row = NativeRegionAllocationTestAccess::snapshot(
        simulation, component, applied_signals[0U], owners[0U]);
    // The group publication advances the A4 revision once; the public
    // reference barrier then advances it once more when it unbinds the roles.
    require(cut.a4_revision_at_cut
                <= std::numeric_limits<std::uint64_t>::max() - 2U
            && after_first_row.authoritative_revision
                == cut.a4_revision_at_cut + 2U
            && NativeRegionAllocationTestAccess::journal_empty_and_retired(
                simulation, cut)
            && NativeRegionAllocationTestAccess::child_receipt_unchanged(
                simulation, cut),
        "one atomic role-group flush must retire both rows and preserve the child ticket");

    TraceProbe trace;
    trace.child_key = cut.child_key;
    trace.producers = { cut.producer0, cut.producer1 };
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, &trace, &TraceProbe::receive);
    simulation.clear_stop();
    const auto completed = simulation.run();
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, nullptr, nullptr);
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the checked suffix must drain and finish the linear fixture");
    require(trace.child_task_begin == 1U
            && trace.child_batch_begin == 1U && trace.child_end == 1U
            && trace.producer_replay == 0U
            && trace.child_batch_begin_one_member
            && NativeRegionAllocationTestAccess::process_resume_count(
                   simulation, cut.producer0)
                == cut.producer0_resumes_at_cut
            && NativeRegionAllocationTestAccess::process_resume_count(
                   simulation, cut.producer1)
                == cut.producer_resumes_at_cut,
        "resume must consume the exact child key once without replaying either producer");
    require(NativeRegionAllocationTestAccess::native_frontier_dispatches(
                simulation) == cut.native_frontier_dispatches_at_cut
            && NativeRegionAllocationTestAccess::v2_forwarding_prefixes(
                   simulation) == cut.v2_forwarding_prefixes_at_cut
            && NativeRegionAllocationTestAccess::v2_forwarding_members(
                   simulation) == cut.v2_forwarding_members_at_cut
            && NativeRegionAllocationTestAccess::forwarding_evaluations(
                   simulation) == cut.forwarding_evaluations_at_cut
            && NativeRegionAllocationTestAccess::forwarding_members(
                   simulation) == cut.forwarding_members_at_cut
            && NativeRegionAllocationTestAccess::a2_local_update_dispatches(
                   simulation) == cut.a2_local_update_dispatches_at_cut,
        "an observed component must not reenter its generated or flattened route");
    require(NativeRegionAllocationTestAccess::a2_local_update_fallbacks(
                simulation) == cut.a2_local_update_fallbacks_at_cut
            && NativeRegionAllocationTestAccess::a2_ordinary_internal_updates(
                   simulation) == cut.a2_ordinary_internal_updates_at_cut + 2U,
        "the checked suffix must publish both remaining internal updates without reentering local forwarding");

    LinearResult result;
    result.final_values = read_linear_values(simulation, signal_ids);
    result.forwarding_evaluations
        = NativeRegionAllocationTestAccess::forwarding_evaluations(simulation);
    result.forwarding_members
        = NativeRegionAllocationTestAccess::forwarding_members(simulation);
    result.v2_forwarding_prefixes
        = NativeRegionAllocationTestAccess::v2_forwarding_prefixes(simulation);
    result.v2_forwarding_members
        = NativeRegionAllocationTestAccess::v2_forwarding_members(simulation);
    return result;
}

[[nodiscard]] std::array<std::string, 5U> run_interpreter_linear(
    const std::string_view suffix,
    const std::uint32_t width)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_linear_config(
        temporary.path, fsim::project::Optimization::o0, width);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the interpreter A2 source must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.start();
    const auto startup = simulation.run(0U);
    require(startup.status == RunStatus::time_limit,
        "interpreter time-zero activity must settle");
    const auto completed = simulation.run();
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the interpreter reference must reach the fixture finish");
    const auto source = simulation.find_signal(
        "native_frontier_a2_applied_prefix.source");
    const auto stage0 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage0");
    const auto stage1 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage1");
    const auto stage2 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage2");
    const auto sink = simulation.find_signal(
        "native_frontier_a2_applied_prefix.sink");
    require(source && stage0 && stage1 && stage2 && sink,
        "the interpreter reference must expose every linear signal");
    const std::array<SignalId, 5U> signal_ids {
        *source, *stage0, *stage1, *stage2, *sink };
    return read_linear_values(simulation, signal_ids);
}

void apply_public_source_mutation(
    Simulation& simulation,
    const SignalId source,
    const PublicMutationKind mutation)
{
    const auto zeroes
        = fsim::runtime::PackedLogic4::from_msb_string(std::string(65U, '0'));
    if (mutation == PublicMutationKind::force) {
        simulation.force_signal(source, zeroes);
    } else {
        simulation.deposit_signal(source, zeroes);
    }
}

[[nodiscard]] PublicMutationResult run_compiled_private_cut_mutation(
    const fsim::project::Optimization optimization,
    const PublicMutationKind mutation,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_linear_config(
        temporary.path, optimization, 65U);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the A2 public-mutation source must elaborate");

    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() != 0U,
        "the A2 public-mutation witness needs compiled processes");

    const auto source = simulation.find_signal(
        "native_frontier_a2_applied_prefix.source");
    const auto stage0 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage0");
    const auto stage1 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage1");
    const auto stage2 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage2");
    const auto sink = simulation.find_signal(
        "native_frontier_a2_applied_prefix.sink");
    require(source && stage0 && stage1 && stage2 && sink,
        "the A2 public-mutation fixture must expose the full linear chain");

    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "time-zero activity must settle before the A2 mutation cut");
    const std::array<SignalId, 5U> signal_ids {
        *source, *stage0, *stage1, *stage2, *sink };
    const std::array<SignalId, 2U> applied_signals { *stage0, *stage1 };
    const auto component
        = NativeRegionAllocationTestAccess::component_for_signal(
            simulation, *stage0);
    require(NativeRegionAllocationTestAccess::component_for_signal(
                simulation, *stage1) == component,
        "the two private outputs must share the A2 component");
    const std::array<ProcessId, 2U> owners {
        NativeRegionAllocationTestAccess::forwarding_output_owner(
            simulation, component, *stage0),
        NativeRegionAllocationTestAccess::forwarding_output_owner(
            simulation, component, *stage1) };
    const std::array<NativeRegionAllocationTestAccess::SignalSnapshot, 2U>
        before {
            NativeRegionAllocationTestAccess::snapshot(
                simulation, component, *stage0, owners[0U]),
            NativeRegionAllocationTestAccess::snapshot(
                simulation, component, *stage1, owners[1U]) };
    const auto evaluations_before
        = NativeRegionAllocationTestAccess::forwarding_evaluations(simulation);
    const auto members_before
        = NativeRegionAllocationTestAccess::forwarding_members(simulation);
    const auto v2_prefixes_before
        = NativeRegionAllocationTestAccess::v2_forwarding_prefixes(simulation);
    const auto v2_members_before
        = NativeRegionAllocationTestAccess::v2_forwarding_members(simulation);
    const auto a2_local_dispatches_before
        = NativeRegionAllocationTestAccess::a2_local_update_dispatches(
            simulation);

    NativeRegionAllocationTestAccess::AppliedCut cut;
    const auto hook = simulation.add_safe_point_hook(
        [&simulation, applied_signals, owners, before,
            evaluations_before, members_before, v2_prefixes_before,
            v2_members_before, a2_local_dispatches_before, &cut](
                Scheduler& scheduler, const SchedulerPhase phase) {
            if (cut.entered) {
                return;
            }
            if (NativeRegionAllocationTestAccess::try_capture_applied_prefix(
                    simulation, scheduler, applied_signals, owners, before,
                    evaluations_before, members_before,
                    v2_prefixes_before, v2_members_before,
                    a2_local_dispatches_before, phase, cut)) {
                scheduler.request_stop();
            }
        });
    const auto stopped = simulation.run(1U);
    simulation.remove_safe_point_hook(hook);
    require(stopped.status == RunStatus::stopped && stopped.time == 1U
            && cut.entered && cut.row_count == applied_signals.size(),
        "the mutation must begin at the authentic two-row A2 private cut");
    require(cut.v2_forwarding_prefixes_at_cut
                == v2_prefixes_before + applied_signals.size()
            && cut.v2_forwarding_members_at_cut
                == v2_members_before + applied_signals.size()
            && cut.forwarding_evaluations_at_cut
                == evaluations_before + 1U,
        "both rows must come from the selected flattened private route");
    const auto all_ones = std::string(65U, '1');
    const auto source_at_cut
        = NativeRegionAllocationTestAccess::signal_semantics_snapshot(
            simulation, *source);
    require(source_at_cut.current == all_ones
            && cut.rows[0U].predicted_value == all_ones
            && cut.rows[1U].predicted_value == all_ones,
        "the cut must hold the original all-one stimulus before mutation");

    TraceProbe trace;
    trace.child_key = cut.child_key;
    trace.producers = { cut.producer0, cut.producer1 };
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, &trace, &TraceProbe::receive);
    apply_public_source_mutation(simulation, *source, mutation);

    const auto all_zeroes = std::string(65U, '0');
    const auto source_after_mutation
        = NativeRegionAllocationTestAccess::signal_semantics_snapshot(
            simulation, *source);
    require(source_after_mutation.current == all_zeroes
            && simulation.signal_is_forced(*source)
                == (mutation == PublicMutationKind::force),
        "the requested source force/deposit must publish synchronously");

    for (std::size_t index = 0U; index < applied_signals.size(); ++index) {
        const auto after_mutation = NativeRegionAllocationTestAccess::snapshot(
            simulation, component, applied_signals[index], owners[index]);
        const auto& row = cut.rows[index];
        require(after_mutation.current == row.predicted_value
                && after_mutation.stored == row.predicted_value
                && after_mutation.owner == row.predicted_value
                && after_mutation.raw_driver == row.predicted_value
                && after_mutation.last == row.before.current
                && after_mutation.direct_aval == row.predicted_aval
                && after_mutation.direct_bval == row.predicted_bval
                && after_mutation.event == row.hidden.event
                && after_mutation.transaction == row.hidden.transaction
                && after_mutation.stamp == row.hidden.stamp
                && after_mutation.value_revision == row.hidden.value_revision
                && !after_mutation.materialization_pending,
            "the external mutation must first flush exact private values without replaying metadata");
    }
    require(NativeRegionAllocationTestAccess::journal_empty_and_retired(
                simulation, cut),
        "the source mutation barrier must retire the complete private journal");

    simulation.clear_stop();
    const auto completed = simulation.run();
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, nullptr, nullptr);
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the mutated compiled suffix must reach the original finish");
    require(trace.child_task_begin == 1U
            && trace.child_batch_begin == 1U && trace.child_end == 1U
            && trace.child_batch_begin_one_member,
        "the exact pre-mutation child key must be consumed once after the barrier");

    static_cast<void>(read_linear_values(simulation, signal_ids));
    PublicMutationResult result;
    result.original_child_key = cut.child_key;
    result.child = cut.child;
    result.forced = simulation.signal_is_forced(*source);
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        result.final_signals[index]
            = NativeRegionAllocationTestAccess::signal_semantics_snapshot(
                simulation, signal_ids[index]);
        result.final_values[index] = result.final_signals[index].current;
    }
    return result;
}

[[nodiscard]] PublicMutationResult run_interpreter_private_cut_mutation(
    const PublicMutationKind mutation,
    const ProcessId child,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_linear_config(
        temporary.path, fsim::project::Optimization::o0, 65U);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the A2 interpreter mutation source must elaborate");

    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    const auto source = simulation.find_signal(
        "native_frontier_a2_applied_prefix.source");
    const auto stage0 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage0");
    const auto stage1 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage1");
    const auto stage2 = simulation.find_signal(
        "native_frontier_a2_applied_prefix.stage2");
    const auto sink = simulation.find_signal(
        "native_frontier_a2_applied_prefix.sink");
    require(source && stage0 && stage1 && stage2 && sink,
        "the A2 interpreter mutation fixture must expose the full chain");

    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "interpreter time-zero activity must settle before mutation");
    const std::array<SignalId, 5U> signal_ids {
        *source, *stage0, *stage1, *stage2, *sink };
    const std::array<SignalId, 3U> predecessor_signals {
        *source, *stage0, *stage1 };
    const auto child_output_signal = *stage2;
    bool captured_child { };
    const auto hook = simulation.add_safe_point_hook(
        [&simulation, child, predecessor_signals, child_output_signal,
            &captured_child](
                Scheduler& scheduler, const SchedulerPhase phase) {
            if (captured_child) {
                return;
            }
            captured_child
                = NativeRegionAllocationTestAccess::interpreter_child_queued_at_cut(
                    simulation, scheduler, child, predecessor_signals,
                    child_output_signal, phase);
            if (captured_child) {
                scheduler.request_stop();
            }
        });
    const auto stopped = simulation.run(1U);
    simulation.remove_safe_point_hook(hook);
    require(stopped.status == RunStatus::stopped && stopped.time == 1U
            && captured_child,
        "the interpreter cut must stop with the child queued after all three predecessors publish");

    TraceProbe trace;
    trace.capture_interpreter_child_key = true;
    trace.interpreter_child = child;
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, &trace, &TraceProbe::receive);
    apply_public_source_mutation(simulation, *source, mutation);
    const auto source_after_mutation
        = NativeRegionAllocationTestAccess::signal_semantics_snapshot(
            simulation, *source);
    require(source_after_mutation.current == std::string(65U, '0')
            && simulation.signal_is_forced(*source)
                == (mutation == PublicMutationKind::force),
        "the interpreter must apply the same synchronous source mutation");

    simulation.clear_stop();
    const auto completed = simulation.run();
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, nullptr, nullptr);
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the interpreter mutated suffix must reach the same finish");
    require(trace.interpreter_child_key_captured
            && trace.child_task_begin == 1U && trace.child_end == 1U,
        "the interpreter must execute and complete its queued child at one exact scheduler key");

    static_cast<void>(read_linear_values(simulation, signal_ids));
    PublicMutationResult result;
    result.original_child_key = trace.child_key;
    result.child = child;
    result.forced = simulation.signal_is_forced(*source);
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        result.final_signals[index]
            = NativeRegionAllocationTestAccess::signal_semantics_snapshot(
                simulation, signal_ids[index]);
        result.final_values[index] = result.final_signals[index].current;
    }
    return result;
}

void test_a2_force_deposit_private_cut()
{
    for (const auto mutation : {
            PublicMutationKind::deposit, PublicMutationKind::force }) {
        const auto label = mutation == PublicMutationKind::force
            ? std::string_view { "force" }
            : std::string_view { "deposit" };
        const auto o0 = run_compiled_private_cut_mutation(
            fsim::project::Optimization::o0, mutation,
            std::string("private-") + std::string(label) + "-o0");
        const auto o2 = run_compiled_private_cut_mutation(
            fsim::project::Optimization::o2, mutation,
            std::string("private-") + std::string(label) + "-o2");
        const auto reference = run_interpreter_private_cut_mutation(
            mutation, o0.child,
            std::string("private-") + std::string(label) + "-interpreter");

        require(o0.child == o2.child && o0.child == reference.child
                && same_key_order(o0.original_child_key, o2.original_child_key)
                && same_key_order(o0.original_child_key,
                    reference.original_child_key),
            "all engines must preserve the same child ordering point (sequence values are engine-local)");
        require(o0.final_values == o2.final_values
                && o0.final_values == reference.final_values
                && std::ranges::all_of(o0.final_values,
                    [](const std::string& value) {
                        return value == std::string(65U, '0');
                    }),
            "force/deposit final values must match the interpreter at all five signals");
        for (std::size_t index = 0U; index < o0.final_signals.size(); ++index) {
            require(same_public_mutation_signal(
                        o0.final_signals[index], o2.final_signals[index])
                    && same_public_mutation_signal(
                        o0.final_signals[index], reference.final_signals[index]),
                "force/deposit CURRENT, LAST, raw drivers, events, and transactions must match");
        }
        const bool should_be_forced
            = mutation == PublicMutationKind::force;
        require(o0.forced == should_be_forced
                && o2.forced == should_be_forced
                && reference.forced == should_be_forced,
            "only force must retain the source force state");
    }
}

[[nodiscard]] UnequalDepthResult run_unequal_depth(
    const fsim::app::SimulationEngine engine,
    const fsim::project::Optimization optimization,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_unequal_depth_config(temporary.path, optimization);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the unequal-depth source must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    if (engine == fsim::app::SimulationEngine::compiled) {
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the unequal-depth witness needs compiled processes");
    }
    const auto source = simulation.find_signal(
        "native_frontier_a2_unequal_depth.source");
    const auto root = simulation.find_signal(
        "native_frontier_a2_unequal_depth.root_stage");
    const auto fast = simulation.find_signal(
        "native_frontier_a2_unequal_depth.fast");
    const auto slow1 = simulation.find_signal(
        "native_frontier_a2_unequal_depth.slow1");
    const auto slow2 = simulation.find_signal(
        "native_frontier_a2_unequal_depth.slow2");
    const auto joined = simulation.find_signal(
        "native_frontier_a2_unequal_depth.joined");
    const auto sink = simulation.find_signal(
        "native_frontier_a2_unequal_depth.sink");
    require(source && root && fast && slow1 && slow2 && joined && sink,
        "the unequal-depth fixture must expose every path node");

    simulation.start();
    const auto startup = simulation.run(0U);
    require(startup.status == RunStatus::time_limit,
        "unequal-depth time-zero activity must settle");
    const auto joined_before
        = NativeRegionAllocationTestAccess::signal_semantics_snapshot(
            simulation, *joined);
    const auto zero_value = std::string(65U, '0');
    const auto one_value = std::string(65U, '1');
    NativeRegionAllocationTestAccess::UnequalJoinProof join_proof;
    auto join_component = std::numeric_limits<std::size_t>::max();
    auto join_process = std::numeric_limits<ProcessId>::max();
    auto root_process = std::numeric_limits<ProcessId>::max();
    auto fast_process = std::numeric_limits<ProcessId>::max();
    auto slow1_process = std::numeric_limits<ProcessId>::max();
    auto slow2_process = std::numeric_limits<ProcessId>::max();
    std::array<SignalId, 3U> ancestor_signals { *root, *fast, *slow1 };
    std::array<ProcessId, 3U> ancestor_owners { };
    std::array<NativeRegionAllocationTestAccess::SignalSnapshot, 3U>
        ancestor_before;
    if (engine == fsim::app::SimulationEngine::compiled) {
        join_component
            = NativeRegionAllocationTestAccess::component_for_signal(
                simulation, *joined);
        join_process
            = NativeRegionAllocationTestAccess::forwarding_output_owner(
                simulation, join_component, *joined);
        root_process
            = NativeRegionAllocationTestAccess::forwarding_output_owner(
                simulation, join_component, *root);
        fast_process
            = NativeRegionAllocationTestAccess::forwarding_output_owner(
                simulation, join_component, *fast);
        slow1_process
            = NativeRegionAllocationTestAccess::forwarding_output_owner(
                simulation, join_component, *slow1);
        slow2_process
            = NativeRegionAllocationTestAccess::forwarding_output_owner(
                simulation, join_component, *slow2);
        ancestor_owners = { root_process, fast_process, slow1_process };
        for (std::size_t index = 0U; index < ancestor_signals.size(); ++index) {
            ancestor_before[index] = NativeRegionAllocationTestAccess::snapshot(
                simulation, join_component, ancestor_signals[index],
                ancestor_owners[index]);
        }
    }
    bool stop_at_ancestor_prefix { };
    const auto hook = simulation.add_safe_point_hook(
        [&simulation, engine, join_process, join_component,
            slow2_process, ancestor_signals, ancestor_owners, ancestor_before,
            joined_signal = *joined,
            slow2_signal = *slow2, &join_proof, &zero_value, &one_value,
            &stop_at_ancestor_prefix](
            Scheduler& scheduler, const SchedulerPhase phase) {
            if (engine == fsim::app::SimulationEngine::compiled) {
                NativeRegionAllocationTestAccess::capture_unequal_join_glitch(
                    simulation, scheduler, join_process, joined_signal,
                    join_component, one_value, join_proof);
                NativeRegionAllocationTestAccess::capture_unequal_join_ancestor_prefix(
                    simulation, scheduler, phase, join_process, slow2_process,
                    joined_signal, slow2_signal, join_component,
                    ancestor_signals, ancestor_owners, ancestor_before,
                    zero_value, one_value, join_proof);
                if (join_proof.ancestor_prefix_captured
                    && !stop_at_ancestor_prefix) {
                    stop_at_ancestor_prefix = true;
                    scheduler.request_stop();
                }
                NativeRegionAllocationTestAccess::capture_slow2_receipt_after_join_flush(
                    simulation, slow2_process, join_component, join_proof);
            }
        });
    const auto run = [&]() {
        if (engine == fsim::app::SimulationEngine::compiled) {
            const auto stopped = simulation.run(1U);
            require(stopped.status == RunStatus::stopped && stopped.time == 1U
                    && join_proof.ancestor_prefix_captured
                    && join_proof.ancestor_row_count == ancestor_signals.size()
                    && join_proof.no_join_row_at_cut
                    && join_proof.retained_slow2_receipt_captured
                    && join_proof.original_key_captured
                    && !join_proof.glitch_captured,
                "the early join cut must retain three applied ancestors and the exact slow suffix receipt");
            simulation.clear_stop();
            return simulation.run();
        }
        return simulation.run();
    }();
    simulation.remove_safe_point_hook(hook);
    require(run.status == RunStatus::stopped && run.time == 3U,
        "the unequal-depth fixture must reach its finish");
    const auto joined_after
        = NativeRegionAllocationTestAccess::signal_semantics_snapshot(
            simulation, *joined);
    const auto slow2_after
        = NativeRegionAllocationTestAccess::signal_semantics_snapshot(
            simulation, *slow2);
    if (engine == fsim::app::SimulationEngine::compiled) {
        const auto slow2_roles_after
            = NativeRegionAllocationTestAccess::snapshot(
                simulation, join_component, *slow2, slow2_process);
        const fsim::runtime::PackedLogic4 expected_slow2(
            65U, fsim::runtime::Logic4::one);
        const auto expected_aval_words = expected_slow2.aval_words();
        const auto expected_bval_words = expected_slow2.bval_words();
        require(slow2_roles_after.current == one_value
                && slow2_roles_after.last == zero_value
                && slow2_roles_after.stored == one_value
                && slow2_roles_after.owner == one_value
                && slow2_roles_after.raw_driver == one_value
                && slow2_roles_after.direct_aval
                    == std::vector<std::uint64_t>(expected_aval_words.begin(),
                        expected_aval_words.end())
                && slow2_roles_after.direct_bval
                    == std::vector<std::uint64_t>(expected_bval_words.begin(),
                        expected_bval_words.end())
                && !slow2_roles_after.materialization_pending
                && slow2_roles_after.event == slow2_after.event
                && slow2_roles_after.transaction == slow2_after.transaction
                && slow2_roles_after.stamp == slow2_after.stamp
                && slow2_roles_after.value_revision
                    == slow2_after.value_revision,
            "the later slow2 callback must publish matching A4 roles and direct mirrors");
    }
    require(slow2_after.current == one_value
            && slow2_after.last == zero_value
            && slow2_after.event && slow2_after.transaction
            && slow2_after.event->first == 1U
            && slow2_after.transaction->first == 1U
            && slow2_after.stamp.process_domain
                == static_cast<std::uint32_t>(
                    fsim::runtime::simir::ProcessSchedulingDomain::systemverilog)
            && slow2_after.stamp.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active),
        "the retained slow2 activation must publish its later Logic4 output after native consumption");
    require(joined_after.current == zero_value
            && joined_after.last == one_value
            && joined_after.event.has_value()
            && joined_after.transaction.has_value()
            && joined_after.stamp.process_domain
                == static_cast<std::uint32_t>(
                    fsim::runtime::simir::ProcessSchedulingDomain::systemverilog)
            && joined_after.stamp.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && joined_before.value_revision
                <= std::numeric_limits<std::uint64_t>::max() - 2U
            && joined_after.value_revision
                >= joined_before.value_revision + 2U,
        "the unequal-depth join must preserve its intermediate glitch");
    if (engine == fsim::app::SimulationEngine::compiled) {
        require(NativeRegionAllocationTestAccess::unequal_join_checked_glitch(
                    join_proof, joined_before, join_process, zero_value, one_value),
            "the unequal-depth join must publish its intermediate glitch at its original checked activation key");
        require(join_proof.ancestor_prefix_captured
                && join_proof.no_join_row_at_cut
                && join_proof.original_task_consumed,
            "the original join receipt must be consumed once without tracing away native admission");
        require(NativeRegionAllocationTestAccess::slow2_receipt_consumed_once(
                    join_proof)
                && join_proof.forwarding_decline_after_cut_observed
                && join_proof.retained_slow2_key.time == 1U
                && join_proof.retained_slow2_key.stable_order == slow2_process,
            "the early join must flush its ancestors, then consume the exact slow2 key through one accepted native member");
    }
    const std::array<SignalId, 7U> signal_ids {
        *source, *root, *fast, *slow1, *slow2, *joined, *sink };
    UnequalDepthResult result;
    result.joined = joined_after;
    result.slow2 = slow2_after;
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        result.final_values[index]
            = simulation.read_signal(signal_ids[index]).to_msb_string();
    }
    return result;
}

void test_o0_o2_a2_applied_prefix()
{
    constexpr std::array<std::uint32_t, 3U> widths { 1U, 64U, 65U };
    for (const auto width : widths) {
        const auto suffix = std::to_string(width);
        const auto interpreter = run_interpreter_linear(
            "linear-interpreter-" + suffix, width);
        const auto o0 = run_compiled_linear(
            fsim::project::Optimization::o0, "linear-o0-" + suffix, width);
        const auto o2 = run_compiled_linear(
            fsim::project::Optimization::o2, "linear-o2-" + suffix, width);
        require(o0.final_values == interpreter
                && o2.final_values == interpreter,
            "O0, O2, and the interpreter must agree across every linear signal");
        require(o0.v2_forwarding_prefixes == o2.v2_forwarding_prefixes
                && o0.v2_forwarding_members == o2.v2_forwarding_members
                && o0.forwarding_evaluations == o2.forwarding_evaluations,
            "the O0 and O2 witnesses must select the same two-prefix route at each width");
    }

    const auto unequal_interpreter = run_unequal_depth(
        fsim::app::SimulationEngine::interpreter,
        fsim::project::Optimization::o0, "unequal-interpreter");
    const auto unequal_compiled = run_unequal_depth(
        fsim::app::SimulationEngine::compiled,
        fsim::project::Optimization::o0, "unequal-compiled");
    require(unequal_compiled.final_values == unequal_interpreter.final_values
            && same_unequal_join_semantics(
                unequal_compiled.joined, unequal_interpreter.joined)
            && same_unequal_join_semantics(
                unequal_compiled.slow2, unequal_interpreter.slow2),
        "the unequal-depth join and slow2 publication values, LAST state, and change metadata must match the interpreter");
}

} // namespace

int main()
{
    try {
        ScopedEnvironment region_kernel {
            "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedEnvironment local_wave {
            "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
        ScopedEnvironment wave_profile {
            "FSIM_PROFILE_SV_WAVES", "1" };
        ScopedEnvironment native_process_counts {
            "FSIM_PROFILE_NATIVE_PROCESS_COUNTS", "1" };
        ScopedEnvironment jit_profile {
            "FSIM_PROFILE_JIT", nullptr };
        test_o0_o2_a2_applied_prefix();
        test_a2_force_deposit_private_cut();
    } catch (const std::exception& error) {
        std::cerr << "A2 applied-prefix witness failed: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "A2 applied-prefix witness passed\n";
}

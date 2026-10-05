// SPDX-License-Identifier: Apache-2.0

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
#include <iostream>
#include <limits>
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
        std::uint64_t time { };
        std::uint64_t delta { };
        std::uint64_t systemverilog_round { };
        std::uint64_t stable_order { };
        std::uint64_t sequence { };
        std::uint32_t process_domain { };
        std::uint32_t phase { };

        friend bool operator==(const Key&, const Key&) = default;
    };

    struct Receipt {
        ProcessId process { std::numeric_limits<ProcessId>::max() };
        std::size_t member { std::numeric_limits<std::size_t>::max() };
        Key key;

        friend bool operator==(const Receipt&, const Receipt&) = default;
    };

    struct Stamp {
        std::uint32_t process_domain { };
        std::uint32_t phase { };
        std::uint64_t systemverilog_round { };

        friend bool operator==(const Stamp&, const Stamp&) = default;
    };

    struct SignalSnapshot {
        std::string current;
        std::string previous;
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

        friend bool operator==(const SignalSnapshot&, const SignalSnapshot&) = default;
    };

    [[nodiscard]] static bool same_signal_scoped_snapshot(
        const SignalSnapshot& left, const SignalSnapshot& right)
    {
        // A sibling commit may advance the shared role-store revision.
        return left.current == right.current
            && left.previous == right.previous
            && left.stored == right.stored
            && left.owner == right.owner
            && left.raw_driver == right.raw_driver
            && left.direct_aval == right.direct_aval
            && left.direct_bval == right.direct_bval
            && left.event == right.event
            && left.transaction == right.transaction
            && left.stamp == right.stamp
            && left.value_revision == right.value_revision
            && left.materialization_pending == right.materialization_pending;
    }

    struct SemanticSnapshot {
        std::string current;
        std::string previous;
        std::string stored;
        std::vector<std::pair<ProcessId, std::string>> drivers;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        Stamp stamp;
        std::uint64_t value_revision { };

        friend bool operator==(const SemanticSnapshot&, const SemanticSnapshot&) = default;
    };

    struct JournalRow {
        SignalId signal { std::numeric_limits<SignalId>::max() };
        ProcessId owner { std::numeric_limits<ProcessId>::max() };
        std::uint32_t output_index { std::numeric_limits<std::uint32_t>::max() };
        std::uint64_t callback_order { };
        std::string predicted;
        std::vector<std::uint64_t> predicted_aval;
        std::vector<std::uint64_t> predicted_bval;
        SignalSnapshot before;
        SignalSnapshot hidden;
    };

    struct Cut {
        std::size_t component { std::numeric_limits<std::size_t>::max() };
        std::uint64_t generation { };
        std::uint64_t authoritative_revision { };
        std::uint64_t forwarding_evaluations_before { };
        std::uint64_t forwarding_evaluations_after { };
        std::uint64_t selected_prefixes_before { };
        std::uint64_t selected_prefixes_after { };
        std::uint64_t selected_members_before { };
        std::uint64_t selected_members_after { };
        std::array<ProcessId, 2U> roots { };
        std::array<std::size_t, 2U> root_members { };
        std::size_t join_member { std::numeric_limits<std::size_t>::max() };
        std::vector<Receipt> receipts;
        std::vector<Receipt> pending_receipts;
        std::vector<JournalRow> rows;
        std::vector<std::uint8_t> member_active;
        bool entered { };
    };

    [[nodiscard]] static Key key_from_origin(
        const RegionKernelActivationOrigin& origin) noexcept
    {
        return { origin.time, origin.delta, origin.systemverilog_round,
            static_cast<std::uint64_t>(origin.stable_order), origin.sequence,
            static_cast<std::uint32_t>(origin.process_domain),
            static_cast<std::uint32_t>(origin.phase) };
    }

    [[nodiscard]] static Key key_from_frontier(
        const RegionFrontierKeyV1& key) noexcept
    {
        return { key.time, key.delta, key.systemverilog_round,
            static_cast<std::uint64_t>(key.stable_order), key.sequence,
            key.process_domain, key.phase };
    }

    [[nodiscard]] static std::size_t component_for_signal(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            throw std::logic_error { "the forest signal has no component" };
        }
        const auto component = state.region_authoritative_component_by_signal[signal];
        if (component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel) {
            throw std::logic_error { "the forest signal has no forwarding certificate" };
        }
        return component;
    }

    [[nodiscard]] static ProcessId output_owner(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        const auto& outputs
            = state.region_activation_programs[component]->activation_kernel.outputs;
        std::optional<ProcessId> owner;
        for (const auto& output : outputs) {
            if (output.signal != signal) {
                continue;
            }
            if (output.width != 65U || output.offset != 0U
                || output.value_kind != ValueKind::logic4
                || output.domain != SignalUpdateDomain::systemverilog_active
                || output.update_kind != RegionUpdateKind::systemverilog_active) {
                throw std::logic_error { "the forest output is not a whole Logic4 Active write" };
            }
            if (owner && *owner != output.owner) {
                throw std::logic_error { "the forest output has multiple owners" };
            }
            owner = output.owner;
        }
        if (!owner) {
            throw std::logic_error { "the forest output has no writer" };
        }
        return *owner;
    }

    [[nodiscard]] static std::size_t member_for_process(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const ProcessId process)
    {
        const auto& members
            = simulation.impl_->interpreter->impl_
                ->region_activation_programs[component]->forwarding_kernel->members;
        for (std::size_t index = 0U; index < members.size(); ++index) {
            if (members[index].process == process) {
                return index;
            }
        }
        throw std::logic_error { "the forest output owner has no member" };
    }

    [[nodiscard]] static SignalSnapshot snapshot(
        const fsim::app::Simulation& simulation,
        const std::size_t component,
        const SignalId signal,
        const ProcessId owner)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (signal >= state.driver_values.size()
            || signal >= state.direct_wide_signal_offsets.size()
            || component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]
            || !state.region_authoritative_state_by_component[component]->valid()
            || !state.region_authoritative_state_by_component[component]
                    ->values().layout().contains(signal)
            || signal >= state.signal_events.size()
            || signal >= state.signal_transactions.size()
            || signal >= state.signal_event_scheduling_stamps.size()
            || signal >= state.signal_value_revisions.size()
            || signal >= state.direct_signal_materialization_pending.size()) {
            throw std::logic_error { "the forest role snapshot is incomplete" };
        }
        const auto& values
            = state.region_authoritative_state_by_component[component]->values();
        SignalSnapshot result;
        result.current = values.current(signal).to_msb_string();
        result.previous = values.previous(signal).to_msb_string();
        result.stored = values.stored(signal).to_msb_string();
        result.owner = values.owner_value(signal, owner).to_msb_string();
        const auto* const raw = state.driver_values[signal].find(owner);
        if (raw == nullptr) {
            throw std::logic_error { "the forest signal has no raw owner row" };
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
            throw std::logic_error { "the forest direct planes are truncated" };
        }
        result.direct_aval.assign(
            state.direct_wide_signal_aval.begin() + static_cast<std::ptrdiff_t>(offset),
            state.direct_wide_signal_aval.begin()
                + static_cast<std::ptrdiff_t>(offset + word_count));
        result.direct_bval.assign(
            state.direct_wide_signal_bval.begin() + static_cast<std::ptrdiff_t>(offset),
            state.direct_wide_signal_bval.begin()
                + static_cast<std::ptrdiff_t>(offset + word_count));
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        const auto& stamp = state.signal_event_scheduling_stamps[signal];
        result.stamp = { static_cast<std::uint32_t>(stamp.origin.process_domain),
            static_cast<std::uint32_t>(stamp.origin.phase),
            stamp.systemverilog_round };
        result.value_revision = state.signal_value_revisions[signal];
        result.authoritative_revision = values.revision();
        result.materialization_pending
            = state.direct_signal_materialization_pending[signal] != 0U;
        return result;
    }

    [[nodiscard]] static SemanticSnapshot semantic_snapshot(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error { "the forest simulation has no interpreter" };
        }
        auto& interpreter = *application.interpreter;
        const auto& state = *interpreter.impl_;
        if (signal >= state.signal_last_values.size()
            || signal >= state.signal_events.size()
            || signal >= state.signal_transactions.size()
            || signal >= state.signal_event_scheduling_stamps.size()
            || signal >= state.signal_value_revisions.size()) {
            throw std::logic_error { "the forest semantic snapshot is incomplete" };
        }
        SemanticSnapshot result;
        result.current = simulation.read_signal(signal).to_msb_string();
        result.previous = state.signal_last_values[signal].to_msb_string();
        result.stored = interpreter.stored_signal_value_snapshot(signal).to_msb_string();
        state.driver_values[signal].for_each_in_process_order(
            [&result](const DriverRecord& driver) {
                result.drivers.emplace_back(driver.process,
                    driver.value.to_msb_string());
            });
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        const auto& stamp = state.signal_event_scheduling_stamps[signal];
        result.stamp = { static_cast<std::uint32_t>(stamp.origin.process_domain),
            static_cast<std::uint32_t>(stamp.origin.phase),
            stamp.systemverilog_round };
        result.value_revision = state.signal_value_revisions[signal];
        return result;
    }

    static void record_real_receipts(
        const fsim::app::Simulation& simulation,
        const Scheduler& scheduler,
        const std::size_t component,
        Cut& cut)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        cut.pending_receipts.clear();
        if (component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results) {
            return;
        }
        const auto& local = *state.region_local_wave_state_by_component[component];
        const auto& bank = *local.forwarding_results;
        const auto add = [&cut, &simulation, component](
                             const ProcessId process, const Key& key,
                             const bool pending) {
            if (key.time != 1U
                || key.process_domain != static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
                || key.phase != static_cast<std::uint32_t>(SchedulerPhase::active)
                || key.stable_order != static_cast<std::uint64_t>(process)) {
                return;
            }
            const auto member
                = member_for_process(simulation, component, process);
            const auto duplicate = std::ranges::find_if(cut.receipts,
                [process, &key](const Receipt& receipt) {
                    return receipt.process == process && receipt.key == key;
                });
            if (duplicate == cut.receipts.end()) {
                cut.receipts.push_back({ process, member, key });
            }
            if (pending) {
                const auto pending_duplicate = std::ranges::find_if(
                    cut.pending_receipts, [process, &key](const Receipt& receipt) {
                        return receipt.process == process && receipt.key == key;
                    });
                if (pending_duplicate == cut.pending_receipts.end()) {
                    cut.pending_receipts.push_back(
                        { process, member, key });
                }
            }
        };

        if (bank.prefix.time == scheduler.now()
            && bank.prefix.phase == SchedulerPhase::active
            && bank.prefix.process_domain
                == ProcessSchedulingDomain::systemverilog) {
            for (const auto& task : bank.prefix.tasks) {
                add(task.member.process, key_from_origin(task.member.origin), false);
            }
        }
        for (std::size_t process = 0U;
             process < state.region_readiness_queued_by_process.size()
                && process < state.processes.size(); ++process) {
            const auto& readiness
                = state.region_readiness_queued_by_process[process];
            if (!readiness.key_valid || !state.processes[process].queued
                || readiness.component != component
                || readiness.generation != local.generation) {
                continue;
            }
            if (readiness.member
                != member_for_process(simulation, component,
                    static_cast<ProcessId>(process))) {
                throw std::logic_error {
                    "the queued forest receipt names the wrong member"
                };
            }
            add(static_cast<ProcessId>(process),
                key_from_frontier(readiness.queued_key), true);
        }
    }

    [[nodiscard]] static bool has_receipt(
        const Cut& cut, const ProcessId process) noexcept
    {
        return std::ranges::any_of(cut.receipts,
            [process](const Receipt& receipt) {
                return receipt.process == process && receipt.key.time == 1U
                    && receipt.key.process_domain == static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog)
                    && receipt.key.phase == static_cast<std::uint32_t>(
                        SchedulerPhase::active);
            });
    }

    [[nodiscard]] static bool try_capture_cut(
        fsim::app::Simulation& simulation,
        Scheduler& scheduler,
        const SchedulerPhase phase,
        const std::array<SignalId, 3U>& outputs,
        const std::array<ProcessId, 2U>& roots,
        const std::array<std::size_t, 3U>& before_indices,
        const std::array<SignalSnapshot, 3U>& before,
        const bool expect_both_roots,
        Cut& cut)
    {
        if (!simulation.impl_->interpreter || scheduler.now() != 1U
            || phase != SchedulerPhase::active) {
            return false;
        }
        const auto& state = *simulation.impl_->interpreter->impl_;
        const auto component = component_for_signal(simulation, outputs[0U]);
        if (component_for_signal(simulation, outputs[1U]) != component
            || component_for_signal(simulation, outputs[2U]) != component
            || component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results
            || component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]
            || !state.region_authoritative_state_by_component[component]->valid()) {
            return false;
        }
        const auto& local = *state.region_local_wave_state_by_component[component];
        const auto& bank = *local.forwarding_results;
        if (bank.applied_role_mutations.empty()
            || bank.applied_role_mutations.size()
                != bank.applied_role_metadata.size()) {
            return false;
        }

        std::array<std::size_t, 2U> root_members { };
        std::size_t join_member { std::numeric_limits<std::size_t>::max() };
        try {
            root_members = { member_for_process(simulation, component, roots[0U]),
                member_for_process(simulation, component, roots[1U]) };
            join_member = member_for_process(simulation, component,
                output_owner(simulation, component, outputs[2U]));
        } catch (...) {
            return false;
        }
        if (root_members[0U] >= bank.member_active.size()
            || root_members[1U] >= bank.member_active.size()
            || join_member >= bank.member_active.size()) {
            return false;
        }
        if (bank.member_active[root_members[0U]] == 0U
            || bank.member_active[join_member] == 0U
            || (expect_both_roots
                ? bank.member_active[root_members[1U]] == 0U
                : bank.member_active[root_members[1U]] != 0U)) {
            return false;
        }
        if (!has_receipt(cut, roots[0U])
            || (expect_both_roots && !has_receipt(cut, roots[1U]))) {
            return false;
        }

        std::array<bool, 3U> seen { };
        Cut candidate;
        candidate.component = component;
        candidate.generation = local.generation;
        candidate.authoritative_revision
            = state.region_authoritative_state_by_component[component]
                ->values().revision();
        candidate.forwarding_evaluations_before
            = cut.forwarding_evaluations_before;
        candidate.forwarding_evaluations_after
            = state.systemverilog_wave_profile_region_forwarding_evaluations;
        candidate.selected_prefixes_before = cut.selected_prefixes_before;
        candidate.selected_prefixes_after
            = state.systemverilog_wave_profile_v2_selected_forwarding_prefixes;
        candidate.selected_members_before = cut.selected_members_before;
        candidate.selected_members_after
            = state.systemverilog_wave_profile_v2_selected_forwarding_members;
        candidate.roots = roots;
        candidate.root_members = root_members;
        candidate.join_member = join_member;
        candidate.receipts = cut.receipts;
        candidate.pending_receipts = cut.pending_receipts;
        candidate.member_active = bank.member_active;
        for (std::size_t index = 0U;
             index < bank.applied_role_metadata.size(); ++index) {
            const auto& metadata = bank.applied_role_metadata[index];
            const auto& mutation = bank.applied_role_mutations[index];
            const auto found = std::find(outputs.begin(), outputs.end(), metadata.signal);
            if (found == outputs.end()) {
                return false;
            }
            const auto row_index = static_cast<std::size_t>(found - outputs.begin());
            if (seen[row_index]
                || metadata.output_index >= bank.output_values.size()
                || metadata.signal != mutation.signal
                || mutation.words.empty() || !mutation.any_current_changed
                || metadata.owner != output_owner(simulation, component, metadata.signal)
                || metadata.callback_time != 1U
                || metadata.origin.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || metadata.origin.phase != SchedulerPhase::active
                || (index != 0U
                    && bank.applied_role_metadata[index - 1U].callback_order
                        >= metadata.callback_order)) {
                return false;
            }
            const auto hidden = snapshot(
                simulation, component, metadata.signal, metadata.owner);
            const auto& old = before[before_indices[row_index]];
            if (hidden.current != old.current
                || hidden.previous != old.previous
                || hidden.stored != old.stored
                || hidden.owner != old.owner
                || hidden.raw_driver != old.raw_driver
                || hidden.direct_aval != old.direct_aval
                || hidden.direct_bval != old.direct_bval
                || hidden.materialization_pending != old.materialization_pending
                || hidden.event != metadata.expected_signal_event
                || hidden.transaction != metadata.expected_transaction
                || hidden.value_revision != metadata.expected_value_revision
                || static_cast<std::uint32_t>(metadata.expected_event_stamp.origin.process_domain)
                    != hidden.stamp.process_domain
                || static_cast<std::uint32_t>(metadata.expected_event_stamp.origin.phase)
                    != hidden.stamp.phase
                || metadata.expected_event_stamp.systemverilog_round
                    != hidden.stamp.systemverilog_round
                || !hidden.event || !hidden.transaction
                || hidden.value_revision <= old.value_revision) {
                return false;
            }
            const auto& predicted = bank.output_values[metadata.output_index];
            JournalRow row;
            row.signal = metadata.signal;
            row.owner = metadata.owner;
            row.output_index = static_cast<std::uint32_t>(metadata.output_index);
            row.callback_order = metadata.callback_order;
            row.predicted = predicted.to_msb_string();
            const auto aval = predicted.aval_words();
            const auto bval = predicted.bval_words();
            row.predicted_aval.assign(aval.begin(), aval.end());
            row.predicted_bval.assign(bval.begin(), bval.end());
            row.before = old;
            row.hidden = hidden;
            candidate.rows.push_back(std::move(row));
            seen[row_index] = true;
        }

        if (!seen[0U] || (expect_both_roots && !seen[1U])) {
            return false;
        }
        if (!expect_both_roots && seen[1U]) {
            return false;
        }
        if (expect_both_roots
            && (!seen[2U]
                || !has_receipt(cut, output_owner(simulation, component,
                    outputs[2U])))) {
            return false;
        }
        if (!expect_both_roots && !seen[2U]) {
            // The selective-root case can stop after its root's private row but
            // before the authentic overlapping join callback reaches Update.
            // Its original receipt must still be queued; an old offered key is
            // not evidence that this callback remains pending.
            const auto join_owner = output_owner(simulation, component, outputs[2U]);
            if (!std::ranges::any_of(cut.pending_receipts,
                    [join_owner](const Receipt& receipt) {
                        return receipt.process == join_owner
                            && receipt.key.time == 1U;
                    })) {
                return false;
            }
        }

        if (roots[1U] >= state.processes.size()
            || roots[1U] >= state.region_readiness_queued_by_process.size()) {
            return false;
        }
        const auto& inactive_state = state.processes[roots[1U]];
        const auto& inactive_receipt
            = state.region_readiness_queued_by_process[roots[1U]];
        if (!expect_both_roots) {
            const auto root_b = snapshot(
                simulation, component, outputs[1U], roots[1U]);
            if (!same_signal_scoped_snapshot(
                    root_b, before[before_indices[1U]])
                || inactive_state.queued
                || (inactive_receipt.key_valid
                    && inactive_receipt.queued_key.time == scheduler.now())) {
                return false;
            }
        }
        candidate.entered = true;
        cut = std::move(candidate);
        return true;
    }

    [[nodiscard]] static bool journal_retired(
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
            && !bank.role_journal_enabled && bank.private_epoch_retired
            && state.region_forwarding_role_journal_nonempty_components == 0U;
    }

    [[nodiscard]] static bool pending_receipts_unchanged(
        const fsim::app::Simulation& simulation, const Cut& cut)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        for (const auto& saved : cut.pending_receipts) {
            if (saved.process >= state.processes.size()
                || saved.process >= state.region_readiness_queued_by_process.size()) {
                return false;
            }
            const auto& process = state.processes[saved.process];
            const auto& readiness
                = state.region_readiness_queued_by_process[saved.process];
            const auto current = key_from_frontier(readiness.queued_key);
            if (!process.queued || !readiness.key_valid
                || readiness.component != cut.component
                || readiness.member != saved.member
                || readiness.generation != cut.generation
                || saved.member
                    != member_for_process(simulation, cut.component, saved.process)
                || current.stable_order
                    != static_cast<std::uint64_t>(saved.process)
                || !(current == saved.key)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static std::uint64_t forwarding_evaluations(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_evaluations;
    }

    [[nodiscard]] static std::uint64_t selected_prefixes(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_v2_selected_forwarding_prefixes;
    }

    [[nodiscard]] static std::uint64_t selected_members(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_v2_selected_forwarding_members;
    }

    static void set_trace_hook(fsim::app::Simulation& simulation,
        void* context, Scheduler::TraceHook hook) noexcept
    {
        if (simulation.impl_ && simulation.impl_->interpreter) {
            simulation.impl_->interpreter->scheduler().set_trace_hook(context, hook);
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
using Receipt = NativeRegionAllocationTestAccess::Receipt;
using Cut = NativeRegionAllocationTestAccess::Cut;
using SignalSnapshot = NativeRegionAllocationTestAccess::SignalSnapshot;
using SemanticSnapshot = NativeRegionAllocationTestAccess::SemanticSnapshot;

enum class ForestShape {
    same_boundary,
    independent_boundaries,
    selective_independent_boundaries
};

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
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error { "failed to configure the root-forest witness" };
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
            / ("fsim-a2-root-forest-" + std::to_string(ticks)
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

struct ForestSignals {
    SignalId source_a { std::numeric_limits<SignalId>::max() };
    std::optional<SignalId> source_b;
    SignalId root_a { std::numeric_limits<SignalId>::max() };
    SignalId root_b { std::numeric_limits<SignalId>::max() };
    SignalId joined { std::numeric_limits<SignalId>::max() };
    SignalId sink { std::numeric_limits<SignalId>::max() };

    [[nodiscard]] std::vector<SignalId> all() const
    {
        std::vector<SignalId> result { source_a };
        if (source_b) {
            result.push_back(*source_b);
        }
        result.insert(result.end(), { root_a, root_b, joined, sink });
        return result;
    }
};

struct RunResult {
    std::vector<SemanticSnapshot> final_semantics;
    std::uint64_t forwarding_evaluations { };
    std::uint64_t selected_prefixes { };
    std::uint64_t selected_members { };
    bool cut_entered { };
};

[[nodiscard]] std::string shape_name(const ForestShape shape)
{
    switch (shape) {
    case ForestShape::same_boundary:
        return "same_boundary";
    case ForestShape::independent_boundaries:
        return "independent_boundaries";
    case ForestShape::selective_independent_boundaries:
        return "selective_independent_boundaries";
    }
    throw std::logic_error { "unknown root-forest shape" };
}

[[nodiscard]] std::string module_name(const ForestShape shape)
{
    return "native_frontier_a2_root_forest_" + shape_name(shape);
}

[[nodiscard]] fsim::project::Config make_config(
    const std::filesystem::path& root,
    const fsim::project::Optimization optimization,
    const ForestShape shape)
{
    const auto module = module_name(shape);
    const auto source = root / (module + ".sv");
    std::ofstream output { source, std::ios::binary };
    if (shape == ForestShape::same_boundary) {
        output << R"(
module native_frontier_a2_root_forest_same_boundary(output wire [64:0] sink);
  logic [64:0] source_a;
  wire [64:0] root_a;
  wire [64:0] root_b;
  wire [64:0] joined;
  assign root_a = source_a;
  assign root_b = source_a & 65'h3;
  assign joined = root_a ^ root_b;
  assign sink = joined;
  initial begin
    source_a = 65'b0;
    #1 source_a = 65'h1ffffffffffffffff;
    #2 $finish;
  end
endmodule
)";
    } else if (shape == ForestShape::independent_boundaries) {
        output << R"(
module native_frontier_a2_root_forest_independent_boundaries(output wire [64:0] sink);
  logic [64:0] source_a;
  logic [64:0] source_b;
  wire [64:0] root_a;
  wire [64:0] root_b;
  wire [64:0] joined;
  assign root_a = source_a;
  assign root_b = source_b ^ 65'h1;
  assign joined = root_a ^ root_b;
  assign sink = joined;
  initial begin
    source_a = 65'b0;
    source_b = 65'b0;
    #1 source_a = 65'h1ffffffffffffffff;
       source_b = 65'h2;
    #2 $finish;
  end
endmodule
)";
    } else {
        output << R"(
module native_frontier_a2_root_forest_selective_independent_boundaries(output wire [64:0] sink);
  logic [64:0] source_a;
  logic [64:0] source_b;
  wire [64:0] root_a;
  wire [64:0] root_b;
  wire [64:0] joined;
  assign root_a = source_a;
  assign root_b = source_b ^ 65'h1;
  assign joined = root_a ^ root_b;
  assign sink = joined;
  initial begin
    source_a = 65'b0;
    source_b = 65'b0;
    #1 source_a = 65'h1ffffffffffffffff;
    #1 source_b = 65'h2;
    #1 $finish;
  end
endmodule
)";
    }
    require(static_cast<bool>(output), "could not write the parsed root-forest fixture");

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

[[nodiscard]] ForestSignals find_signals(
    const Simulation& simulation, const ForestShape shape)
{
    const auto prefix = module_name(shape) + ".";
    const auto source_a = simulation.find_signal(prefix + "source_a");
    const auto source_b = simulation.find_signal(prefix + "source_b");
    const auto root_a = simulation.find_signal(prefix + "root_a");
    const auto root_b = simulation.find_signal(prefix + "root_b");
    const auto joined = simulation.find_signal(prefix + "joined");
    const auto sink = simulation.find_signal(prefix + "sink");
    require(source_a && root_a && root_b && joined && sink,
        "the root-forest fixture must expose both roots, the join, and sink");
    ForestSignals result;
    result.source_a = *source_a;
    result.source_b = source_b;
    result.root_a = *root_a;
    result.root_b = *root_b;
    result.joined = *joined;
    result.sink = *sink;
    require((shape == ForestShape::same_boundary) == !result.source_b,
        "the fixture boundary count must match its selected topology");
    return result;
}

struct TraceProbe {
    std::array<Receipt, 8U> pending;
    std::array<std::uint64_t, 8U> task_end { };
    std::array<ProcessId, 2U> original_roots { };
    std::uint64_t root_replay { };
    std::size_t pending_count { };

    static bool same_key(const SchedulerTraceRecord& record,
        const Key& key) noexcept
    {
        return record.systemverilog && record.phase
            && record.time == key.time && record.delta == key.delta
            && record.systemverilog_round == key.systemverilog_round
            && record.order == key.stable_order
            && record.sequence == key.sequence
            && static_cast<std::uint32_t>(*record.phase) == key.phase;
    }

    static void receive(void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& probe = *static_cast<TraceProbe*>(context);
        if ((record.kind == SchedulerTraceKind::task_begin
                || record.kind == SchedulerTraceKind::batch_begin)
            && record.systemverilog && record.phase && record.time == 1U
            && (*record.phase == SchedulerPhase::active)
            && (record.order == probe.original_roots[0U]
                || record.order == probe.original_roots[1U])) {
            ++probe.root_replay;
        }
        if (record.kind != SchedulerTraceKind::task_end) {
            return;
        }
        for (std::size_t index = 0U; index < probe.pending_count; ++index) {
            if (probe.pending[index].process == record.order
                && same_key(record, probe.pending[index].key)) {
                ++probe.task_end[index];
            }
        }
    }
};

[[nodiscard]] RunResult run_compiled(
    const ForestShape shape,
    const fsim::project::Optimization optimization,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(temporary.path, optimization, shape);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the root-forest source must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() != 0U,
        "the root-forest witness needs installed compiled processes");
    const auto signals = find_signals(simulation, shape);
    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "the root-forest time-zero initialization must settle");
    const auto component
        = NativeRegionAllocationTestAccess::component_for_signal(simulation,
            signals.root_a);
    require(NativeRegionAllocationTestAccess::component_for_signal(
                simulation, signals.root_b) == component
            && NativeRegionAllocationTestAccess::component_for_signal(
                simulation, signals.joined) == component,
        "both roots and their join must share one certified component");
    const std::array<SignalId, 3U> output_signals {
        signals.root_a, signals.root_b, signals.joined };
    const std::array<ProcessId, 2U> roots {
        NativeRegionAllocationTestAccess::output_owner(
            simulation, component, signals.root_a),
        NativeRegionAllocationTestAccess::output_owner(
            simulation, component, signals.root_b) };
    const auto join_owner = NativeRegionAllocationTestAccess::output_owner(
        simulation, component, signals.joined);
    const std::array<std::size_t, 3U> before_indices { 0U, 1U, 2U };
    const std::array<SignalSnapshot, 3U> before {
        NativeRegionAllocationTestAccess::snapshot(
            simulation, component, signals.root_a, roots[0U]),
        NativeRegionAllocationTestAccess::snapshot(
            simulation, component, signals.root_b, roots[1U]),
        NativeRegionAllocationTestAccess::snapshot(
            simulation, component, signals.joined, join_owner) };
    for (const auto& role : before) {
        require(role.current == role.stored && role.stored == role.owner
                && role.owner == role.raw_driver,
            "the forest outputs must begin with coherent A4 and raw owner roles");
    }

    Cut cut;
    cut.forwarding_evaluations_before
        = NativeRegionAllocationTestAccess::forwarding_evaluations(simulation);
    cut.selected_prefixes_before
        = NativeRegionAllocationTestAccess::selected_prefixes(simulation);
    cut.selected_members_before
        = NativeRegionAllocationTestAccess::selected_members(simulation);
    const bool expect_both_roots = shape != ForestShape::selective_independent_boundaries;
    const auto hook = simulation.add_safe_point_hook(
        [&simulation, &cut, &output_signals, &roots, &before_indices, &before,
            expect_both_roots](Scheduler& scheduler, const SchedulerPhase phase) {
            if (scheduler.now() != 1U || phase != SchedulerPhase::active
                || cut.entered) {
                return;
            }
            const auto component
                = NativeRegionAllocationTestAccess::component_for_signal(
                    simulation, output_signals[0U]);
            NativeRegionAllocationTestAccess::record_real_receipts(
                simulation, scheduler, component, cut);
            if (NativeRegionAllocationTestAccess::try_capture_cut(
                    simulation, scheduler, phase, output_signals, roots,
                    before_indices, before, expect_both_roots, cut)) {
                scheduler.request_stop();
            }
        });
    const auto stopped = simulation.run(1U);
    simulation.remove_safe_point_hook(hook);
    require(stopped.status == RunStatus::stopped && stopped.time == 1U
            && cut.entered,
        "the root forest must stop at the authentic t1 private-role cut");
    require(cut.forwarding_evaluations_after > cut.forwarding_evaluations_before
            && cut.selected_prefixes_after > cut.selected_prefixes_before
            && cut.selected_members_after > cut.selected_members_before,
        "the cut must prove V2 selected the flattened forwarding prefix");
    require(std::ranges::any_of(cut.rows,
                [signal = signals.root_a](const auto& row) {
                    return row.signal == signal;
                })
            && NativeRegionAllocationTestAccess::has_receipt(cut, roots[0U]),
        "root A must have an applied row and its original scheduler receipt");
    if (expect_both_roots) {
        require(std::ranges::any_of(cut.rows,
                    [signal = signals.root_b](const auto& row) {
                        return row.signal == signal;
                    })
                && NativeRegionAllocationTestAccess::has_receipt(cut, roots[1U]),
            "both independently triggered roots must retain actual original receipts");
    } else {
        require(!std::ranges::any_of(cut.rows,
                    [signal = signals.root_b](const auto& row) {
                        return row.signal == signal;
                    })
                && cut.root_members[1U] < cut.member_active.size()
                && cut.member_active[cut.root_members[1U]] == 0U
                && !NativeRegionAllocationTestAccess::has_receipt(cut, roots[1U]),
            "an unqueued forest root must remain inactive and uncommitted");
        const auto untouched = NativeRegionAllocationTestAccess::snapshot(
            simulation, component, signals.root_b, roots[1U]);
        require(NativeRegionAllocationTestAccess::same_signal_scoped_snapshot(
                    untouched, before[1U]),
            "the unqueued root's complete roles and metadata must remain unchanged at the cut");
    }
    require(!cut.rows.empty()
            && std::ranges::all_of(cut.rows, [](const auto& row) {
                return row.hidden.current == row.before.current
                    && row.hidden.previous == row.before.previous
                    && row.hidden.stored == row.before.stored
                    && row.hidden.owner == row.before.owner
                    && row.hidden.raw_driver == row.before.raw_driver
                    && row.hidden.direct_aval == row.before.direct_aval
                    && row.hidden.direct_bval == row.before.direct_bval
                    && row.hidden.materialization_pending
                        == row.before.materialization_pending
                    && row.hidden.value_revision > row.before.value_revision;
            }),
        "private rows must advance metadata while keeping all public roles hidden");

    static_cast<void>(simulation.read_signal(signals.root_a));
    require(NativeRegionAllocationTestAccess::journal_retired(simulation, cut)
            && NativeRegionAllocationTestAccess::pending_receipts_unchanged(
                simulation, cut),
        "the first public observation must atomically retire the applied role prefix");
    for (const auto& row : cut.rows) {
        const auto after = NativeRegionAllocationTestAccess::snapshot(
            simulation, component, row.signal, row.owner);
        require(after.current == row.predicted
                && after.stored == row.predicted
                && after.owner == row.predicted
                && after.raw_driver == row.predicted
                && after.previous == row.before.current
                && after.direct_aval == row.predicted_aval
                && after.direct_bval == row.predicted_bval
                && after.event == row.hidden.event
                && after.transaction == row.hidden.transaction
                && after.stamp == row.hidden.stamp
                && after.value_revision == row.hidden.value_revision
                && after.authoritative_revision > cut.authoritative_revision
                && !after.materialization_pending,
            "role flush must publish predicted values without replaying event metadata");
    }

    TraceProbe trace;
    trace.original_roots = roots;
    for (const auto& receipt : cut.pending_receipts) {
        if (trace.pending_count >= trace.pending.size()) {
            throw std::logic_error { "the forest pending receipt table overflowed" };
        }
        trace.pending[trace.pending_count++] = receipt;
    }
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, &trace, &TraceProbe::receive);
    simulation.clear_stop();
    const auto completed = simulation.run();
    NativeRegionAllocationTestAccess::set_trace_hook(simulation, nullptr, nullptr);
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the original root-forest callback suffix must complete after observation");
    require(trace.root_replay == 0U,
        "the private role prefix must not replay either producer callback");
    for (std::size_t index = 0U; index < trace.pending_count; ++index) {
        require(trace.task_end[index] == 1U,
            "each queued original suffix receipt must retire exactly once");
    }

    RunResult result;
    for (const auto signal : signals.all()) {
        result.final_semantics.push_back(
            NativeRegionAllocationTestAccess::semantic_snapshot(simulation, signal));
    }
    result.forwarding_evaluations
        = NativeRegionAllocationTestAccess::forwarding_evaluations(simulation);
    result.selected_prefixes
        = NativeRegionAllocationTestAccess::selected_prefixes(simulation);
    result.selected_members
        = NativeRegionAllocationTestAccess::selected_members(simulation);
    result.cut_entered = cut.entered;
    return result;
}

[[nodiscard]] std::vector<SemanticSnapshot> run_interpreter(
    const ForestShape shape, const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(temporary.path,
        fsim::project::Optimization::o0, shape);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the interpreter root-forest source must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    const auto signals = find_signals(simulation, shape);
    simulation.start();
    const auto completed = simulation.run();
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the interpreter root-forest reference must finish at t3");
    std::vector<SemanticSnapshot> result;
    for (const auto signal : signals.all()) {
        result.push_back(
            NativeRegionAllocationTestAccess::semantic_snapshot(simulation, signal));
    }
    return result;
}

void test_root_forest_case(const ForestShape shape)
{
    const auto interpreter = run_interpreter(shape, shape_name(shape) + "-interp");
    const auto o0 = run_compiled(shape, fsim::project::Optimization::o0,
        shape_name(shape) + "-o0");
    const auto o2 = run_compiled(shape, fsim::project::Optimization::o2,
        shape_name(shape) + "-o2");
    require(o0.final_semantics == interpreter
            && o2.final_semantics == interpreter,
        "O0 and O2 must preserve interpreter current/LAST/stored/driver/event metadata");
    require(o0.cut_entered && o2.cut_entered
            && o0.forwarding_evaluations != 0U
            && o2.forwarding_evaluations != 0U
            && o0.selected_prefixes != 0U
            && o2.selected_prefixes != 0U
            && o0.selected_members != 0U
            && o2.selected_members != 0U,
        "both compiled modes must execute the parsed root-forest role journal");
    const auto ones = std::string(65U, '1');
    const auto three = std::string(63U, '0') + "11";
    const auto joined = std::string(63U, '1') + "00";
    const auto root_a_index
        = shape == ForestShape::same_boundary ? 1U : 2U;
    const auto root_b_index = root_a_index + 1U;
    const auto joined_index = root_b_index + 1U;
    const auto sink_index = joined_index + 1U;
    const auto expected_root_b = three;
    require(o0.final_semantics[root_a_index].current == ones
            && o2.final_semantics[root_a_index].current == ones
            && o0.final_semantics[root_b_index].current == expected_root_b
            && o2.final_semantics[root_b_index].current == expected_root_b
            && o0.final_semantics[joined_index].current == joined
            && o2.final_semantics[joined_index].current == joined
            && o0.final_semantics[sink_index].current == joined
            && o2.final_semantics[sink_index].current == joined,
        "the forest roots and join must match parsed final values");
}

} // namespace

int main()
{
    try {
        ScopedEnvironment region_kernel { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedEnvironment local_wave { "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
        ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
        ScopedEnvironment native_process_counts {
            "FSIM_PROFILE_NATIVE_PROCESS_COUNTS", "1" };
        ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };
        ScopedEnvironment wide_single_commit {
            "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", nullptr };
        ScopedEnvironment wide_disjoint_commit {
            "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", nullptr };
        test_root_forest_case(ForestShape::same_boundary);
        test_root_forest_case(ForestShape::independent_boundaries);
        test_root_forest_case(ForestShape::selective_independent_boundaries);
    } catch (const std::exception& error) {
        std::cerr << "A2 root-forest witness failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

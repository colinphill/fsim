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
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct JoinSignalSnapshot {
    std::string current;
    std::string last;
    std::string stored;
    std::size_t driver_count { };
    std::optional<ProcessId> raw_driver_owner;
    std::optional<std::string> raw_driver;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    std::uint32_t stamp_domain { };
    std::uint32_t stamp_phase { };
    std::uint64_t stamp_systemverilog_round { };
    std::uint64_t value_revision { };

    friend bool operator==(
        const JoinSignalSnapshot&, const JoinSignalSnapshot&) = default;
};

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
    };

    struct RoleRow {
        SignalId signal { std::numeric_limits<SignalId>::max() };
        ProcessId owner { std::numeric_limits<ProcessId>::max() };
        std::uint32_t output_index { std::numeric_limits<std::uint32_t>::max() };
        std::uint64_t callback_time { };
        std::uint64_t callback_delta { };
        std::uint64_t callback_round { };
        std::uint64_t callback_order { };
        SignalChangeOrigin origin;
        std::string predicted;
        std::vector<std::uint64_t> predicted_aval;
        std::vector<std::uint64_t> predicted_bval;
        SignalSnapshot before;
        SignalSnapshot hidden;
    };

    struct Receipt {
        ProcessId process { std::numeric_limits<ProcessId>::max() };
        Key key;

        friend bool operator==(const Receipt&, const Receipt&) = default;
    };

    struct FanoutCut {
        std::size_t component { std::numeric_limits<std::size_t>::max() };
        std::uint64_t generation { };
        std::uint64_t forwarding_evaluations { };
        std::uint64_t forwarding_members { };
        std::uint64_t selected_forwarding_prefixes { };
        std::uint64_t selected_forwarding_members { };
        std::uint64_t a4_revision { };
        std::vector<RoleRow> rows;
        std::array<Receipt, 2U> leaves;
        std::array<ProcessId, 3U> producers {
            std::numeric_limits<ProcessId>::max(),
            std::numeric_limits<ProcessId>::max(),
            std::numeric_limits<ProcessId>::max() };
        bool entered { };
    };

    [[nodiscard]] static Key copy_key(const RegionFrontierKeyV1& key) noexcept
    {
        return { key.time, key.delta, key.systemverilog_round,
            key.stable_order, key.sequence, key.process_domain, key.phase };
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
            || signal >= state.signal_events.size()
            || signal >= state.signal_transactions.size()
            || signal >= state.signal_event_scheduling_stamps.size()
            || signal >= state.signal_value_revisions.size()
            || signal >= state.direct_signal_materialization_pending.size()) {
            throw std::logic_error { "the fanout role snapshot is incomplete" };
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
            throw std::logic_error { "the fanout signal has no owner driver row" };
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
            throw std::logic_error { "the direct wide mirrors are truncated" };
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

    [[nodiscard]] static std::size_t component_for_signal(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            throw std::logic_error { "the signal has no A4 component mapping" };
        }
        const auto component = state.region_authoritative_component_by_signal[signal];
        if (component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]
            || !state.region_activation_programs[component]->forwarding_kernel) {
            throw std::logic_error { "the component has no forwarding certificate" };
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
                throw std::logic_error { "the fork output is not a whole Logic4 write" };
            }
            if (owner && *owner != output.owner) {
                throw std::logic_error { "the fork output has multiple owners" };
            }
            owner = output.owner;
        }
        if (!owner) {
            throw std::logic_error { "the fork output is not in the kernel" };
        }
        return *owner;
    }

    [[nodiscard]] static bool reads_signal(
        const RegionConeForwardingKernel& kernel,
        const std::size_t member_index,
        const SignalId signal)
    {
        if (member_index >= kernel.members.size()) {
            return false;
        }
        const auto& member = kernel.members[member_index];
        if (member.read_begin > kernel.internal_reads.size()
            || member.read_count > kernel.internal_reads.size() - member.read_begin) {
            return false;
        }
        for (std::size_t index = member.read_begin;
             index < member.read_begin + member.read_count; ++index) {
            if (kernel.internal_reads[index].signal == signal) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static bool try_capture_fanout_cut(
        fsim::app::Simulation& simulation,
        Scheduler& scheduler,
        const SchedulerPhase phase,
        const std::array<SignalId, 3U>& signals,
        const std::array<SignalSnapshot, 3U>& before,
        const std::array<SignalId, 2U>& leaf_inputs,
        const std::uint64_t forwarding_before,
        const std::uint64_t members_before,
        const std::uint64_t selected_prefixes_before,
        const std::uint64_t selected_members_before,
        FanoutCut& cut)
    {
        if (!simulation.impl_->interpreter || scheduler.now() != 1U
            || phase != SchedulerPhase::active) {
            return false;
        }
        cut = FanoutCut { };
        auto& state = *simulation.impl_->interpreter->impl_;
        const auto component = component_for_signal(simulation, signals[0U]);
        for (const auto signal : signals) {
            if (component_for_signal(simulation, signal) != component) {
                return false;
            }
        }
        if (component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results
            || component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]
            || !state.region_authoritative_state_by_component[component]->valid()) {
            return false;
        }

        auto& local = *state.region_local_wave_state_by_component[component];
        auto& bank = *local.forwarding_results;
        const auto& kernel
            = *state.region_activation_programs[component]->forwarding_kernel;
        const auto& activation
            = state.region_activation_programs[component]->activation_kernel;
        const auto& authoritative
            = state.region_authoritative_state_by_component[component];
        if (!bank.role_journal_enabled
            || state.region_forwarding_role_journal_nonempty_components == 0U
            || bank.applied_role_mutations.size() != signals.size()
            || bank.applied_role_metadata.size() != signals.size()) {
            return false;
        }

        cut.component = component;
        cut.generation = local.generation;
        cut.a4_revision = authoritative->values().revision();
        cut.rows.reserve(signals.size());
        std::array<std::uint8_t, 3U> seen { };
        std::array<ProcessId, 3U> owners { };
        for (std::size_t index = 0U; index < bank.applied_role_metadata.size(); ++index) {
            const auto& metadata = bank.applied_role_metadata[index];
            const auto& mutation = bank.applied_role_mutations[index];
            const auto signal_it = std::find(signals.begin(), signals.end(), metadata.signal);
            if (signal_it == signals.end()) {
                return false;
            }
            const auto row_index = static_cast<std::size_t>(signal_it - signals.begin());
            if (seen[row_index] != 0U || metadata.callback_time != 1U
                || metadata.origin.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || metadata.origin.phase != SchedulerPhase::active
                || metadata.callback_order == 0U
                || (index != 0U
                    && bank.applied_role_metadata[index - 1U].callback_order
                        >= metadata.callback_order)
                || metadata.output_index >= activation.outputs.size()
                || metadata.output_index >= bank.output_values.size()
                || mutation.signal != metadata.signal || mutation.words.empty()
                || !mutation.any_current_changed) {
                return false;
            }
            const auto& output = activation.outputs[metadata.output_index];
            if (output.signal != metadata.signal || output.owner != metadata.owner
                || output.width != 65U || output.offset != 0U
                || output.value_kind != ValueKind::logic4
                || output.domain != SignalUpdateDomain::systemverilog_active
                || output.update_kind != RegionUpdateKind::systemverilog_active
                || bank.output_values[metadata.output_index].width() != 65U
                || !authoritative->values().packed_signal_slots_bound(metadata.signal)
                || !authoritative->values().packed_owner_slot_bound(
                    metadata.signal, metadata.owner)) {
                return false;
            }
            const auto hidden = snapshot(
                simulation, component, metadata.signal, metadata.owner);
            if (hidden.current != before[row_index].current
                || hidden.previous != before[row_index].previous
                || hidden.stored != before[row_index].stored
                || hidden.owner != before[row_index].owner
                || hidden.raw_driver != before[row_index].raw_driver
                || hidden.direct_aval != before[row_index].direct_aval
                || hidden.direct_bval != before[row_index].direct_bval
                || hidden.materialization_pending != before[row_index].materialization_pending
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
                    != hidden.stamp.systemverilog_round
                || !hidden.event || !hidden.transaction
                || hidden.event->first != 1U || hidden.transaction->first != 1U
                || hidden.value_revision <= before[row_index].value_revision) {
                return false;
            }

            const auto& value = bank.output_values[metadata.output_index];
            RoleRow row;
            row.signal = metadata.signal;
            row.owner = metadata.owner;
            row.output_index = static_cast<std::uint32_t>(metadata.output_index);
            row.callback_time = metadata.callback_time;
            row.callback_delta = metadata.callback_delta;
            row.callback_round = metadata.callback_systemverilog_round;
            row.callback_order = metadata.callback_order;
            row.origin = metadata.origin;
            row.predicted = value.to_msb_string();
            const auto aval = value.aval_words();
            const auto bval = value.bval_words();
            row.predicted_aval.assign(aval.begin(), aval.end());
            row.predicted_bval.assign(bval.begin(), bval.end());
            row.before = before[row_index];
            row.hidden = hidden;
            cut.rows.push_back(std::move(row));
            owners[row_index] = metadata.owner;
            seen[row_index] = 1U;
        }
        if (std::ranges::any_of(seen, [](const auto value) { return value == 0U; })) {
            return false;
        }

        std::array<std::size_t, 3U> writer_members { };
        for (std::size_t index = 0U; index < owners.size(); ++index) {
            bool found { };
            for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
                if (kernel.members[member].process == owners[index]) {
                    if (found) {
                        return false;
                    }
                    writer_members[index] = member;
                    found = true;
                }
            }
            if (!found) {
                return false;
            }
        }
        if (writer_members[0U] == writer_members[1U]
            || writer_members[0U] == writer_members[2U]
            || writer_members[1U] == writer_members[2U]) {
            return false;
        }
        std::array<std::uint8_t, 2U> root_children { };
        for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
            if (!reads_signal(kernel, member, signals[0U])) {
                continue;
            }
            if (member == writer_members[1U]) {
                root_children[0U] = 1U;
            } else if (member == writer_members[2U]) {
                root_children[1U] = 1U;
            } else {
                return false;
            }
        }
        if (root_children[0U] == 0U || root_children[1U] == 0U) {
            return false;
        }

        // These are exact queued readiness receipts for the two terminal
        // consumers. The callback metadata above deliberately records only
        // callback order/origin; it is not used to synthesize scheduler keys.
        for (std::size_t leaf_index = 0U; leaf_index < leaf_inputs.size(); ++leaf_index) {
            std::optional<std::size_t> member_index;
            for (std::size_t index = 0U; index < kernel.members.size(); ++index) {
                if (!reads_signal(kernel, index, leaf_inputs[leaf_index])) {
                    continue;
                }
                if (member_index) {
                    return false;
                }
                member_index = index;
            }
            if (!member_index) {
                return false;
            }
            const auto process = kernel.members[*member_index].process;
            if (process >= state.processes.size()
                || process >= state.region_readiness_queued_by_process.size()) {
                return false;
            }
            const auto& process_state = state.processes[process];
            const auto& receipt = state.region_readiness_queued_by_process[process];
            if (!process_state.queued || !receipt.key_valid
                || receipt.component != component
                || receipt.generation != local.generation
                || receipt.member != *member_index
                || receipt.queued_key.time != scheduler.now()
                || receipt.queued_key.process_domain
                    != static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog)
                || receipt.queued_key.phase
                    != static_cast<std::uint32_t>(SchedulerPhase::active)
                || receipt.queued_key.stable_order != process) {
                return false;
            }
            cut.leaves[leaf_index] = { process, copy_key(receipt.queued_key) };
        }
        if (cut.leaves[0U].process == cut.leaves[1U].process) {
            return false;
        }
        cut.producers = owners;
        cut.forwarding_evaluations
            = state.systemverilog_wave_profile_region_forwarding_evaluations;
        cut.forwarding_members
            = state.systemverilog_wave_profile_region_forwarding_member_consumptions;
        cut.selected_forwarding_prefixes
            = state.systemverilog_wave_profile_v2_selected_forwarding_prefixes;
        cut.selected_forwarding_members
            = state.systemverilog_wave_profile_v2_selected_forwarding_members;
        if (cut.forwarding_evaluations <= forwarding_before
            || cut.forwarding_members <= members_before
            || cut.selected_forwarding_prefixes <= selected_prefixes_before
            || cut.selected_forwarding_members <= selected_members_before) {
            return false;
        }
        cut.entered = true;
        return true;
    }

    [[nodiscard]] static bool receipts_unchanged(
        const fsim::app::Simulation& simulation, const FanoutCut& cut)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        for (const auto& saved : cut.leaves) {
            if (saved.process >= state.processes.size()
                || saved.process >= state.region_readiness_queued_by_process.size()) {
                return false;
            }
            const auto& process = state.processes[saved.process];
            const auto& receipt = state.region_readiness_queued_by_process[saved.process];
            if (!process.queued || !receipt.key_valid
                || receipt.component != cut.component
                || receipt.generation != cut.generation
                || !(copy_key(receipt.queued_key) == saved.key)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static bool journal_retired(
        const fsim::app::Simulation& simulation, const FanoutCut& cut)
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

    static bool try_sample_join(
        fsim::app::Simulation& simulation,
        const Scheduler& scheduler,
        const SchedulerPhase phase,
        const SignalId root_a,
        const SignalId root_b,
        const SignalId join,
        bool& sampled,
        bool& private_roots_seen_without_join,
        bool& private_join_row_seen,
        std::size_t& independent_roots)
    {
        if (!simulation.impl_->interpreter || scheduler.now() != 1U
            || phase != SchedulerPhase::active) {
            return false;
        }
        auto& state = *simulation.impl_->interpreter->impl_;
        const auto component = component_for_signal(simulation, join);
        if (component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results) {
            return false;
        }
        const auto& bank = *state.region_local_wave_state_by_component[
            component]->forwarding_results;
        const auto& kernel
            = *state.region_activation_programs[component]->forwarding_kernel;
        independent_roots = 0U;
        for (const auto& member : kernel.members) {
            if (member.dependency_begin > kernel.dependencies.size()
                || member.dependency_count
                    > kernel.dependencies.size() - member.dependency_begin) {
                return false;
            }
            independent_roots += member.dependency_count == 0U ? 1U : 0U;
        }
        const auto has_applied_row = [&](const SignalId signal) {
            return std::ranges::any_of(
                bank.applied_role_metadata, [signal](const auto& metadata) {
                    return metadata.signal == signal;
                });
        };
        const bool root_a_applied = has_applied_row(root_a);
        const bool root_b_applied = has_applied_row(root_b);
        const bool join_applied = has_applied_row(join);
        sampled = true;
        private_roots_seen_without_join = private_roots_seen_without_join
            || (root_a_applied && root_b_applied && !join_applied);
        private_join_row_seen = private_join_row_seen || join_applied;
        return true;
    }

    [[nodiscard]] static std::uint64_t forwarding_evaluations(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_evaluations;
    }

    [[nodiscard]] static std::uint64_t forwarding_members(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_member_consumptions;
    }

    [[nodiscard]] static std::uint64_t selected_forwarding_prefixes(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_v2_selected_forwarding_prefixes;
    }

    [[nodiscard]] static std::uint64_t selected_forwarding_members(
        const fsim::app::Simulation& simulation)
    {
        return simulation.impl_->interpreter->impl_
            ->systemverilog_wave_profile_v2_selected_forwarding_members;
    }

    [[nodiscard]] static bool join_role_journal_empty(
        const fsim::app::Simulation& simulation, const SignalId join)
    {
        const auto& state = *simulation.impl_->interpreter->impl_;
        const auto component = component_for_signal(simulation, join);
        if (component >= state.region_local_wave_state_by_component.size()
            || !state.region_local_wave_state_by_component[component]
            || !state.region_local_wave_state_by_component[component]
                    ->forwarding_results) {
            return false;
        }
        const auto& bank = *state.region_local_wave_state_by_component[
            component]->forwarding_results;
        return !bank.role_journal_enabled
            && bank.applied_role_mutations.empty()
            && bank.applied_role_metadata.empty()
            && state.region_forwarding_role_journal_nonempty_components == 0U;
    }

    [[nodiscard]] static JoinSignalSnapshot join_signal_snapshot(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error { "the app has no live interpreter" };
        }
        const auto& state = *application.interpreter->impl_;
        if (signal >= state.signal_last_values.size()
            || signal >= state.driven_values.size()
            || signal >= state.driver_values.size()
            || signal >= state.signal_events.size()
            || signal >= state.signal_transactions.size()
            || signal >= state.signal_event_scheduling_stamps.size()
            || signal >= state.signal_value_revisions.size()) {
            throw std::logic_error { "the join semantic snapshot is incomplete" };
        }

        JoinSignalSnapshot result;
        result.current = simulation.read_signal_snapshot(signal).to_msb_string();
        result.last = state.signal_last_values[signal].to_msb_string();
        result.stored = application.interpreter
            ->stored_signal_value_snapshot(signal).to_msb_string();
        const auto& drivers = state.driver_values[signal];
        result.driver_count = drivers.size();
        if (const auto* const raw = drivers.sole()) {
            result.raw_driver_owner = raw->process;
            result.raw_driver = raw->value.to_msb_string();
        }
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        const auto& stamp = state.signal_event_scheduling_stamps[signal];
        result.stamp_domain
            = static_cast<std::uint32_t>(stamp.origin.process_domain);
        result.stamp_phase = static_cast<std::uint32_t>(stamp.origin.phase);
        result.stamp_systemverilog_round = stamp.systemverilog_round;
        result.value_revision = state.signal_value_revisions[signal];
        return result;
    }

    [[nodiscard]] static bool trace_hook_installed(
        const fsim::app::Simulation& simulation) noexcept
    {
        return simulation.impl_ && simulation.impl_->interpreter
            && simulation.impl_->interpreter->scheduler().trace_hook_installed();
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
using FanoutCut = NativeRegionAllocationTestAccess::FanoutCut;
using SignalSnapshot = NativeRegionAllocationTestAccess::SignalSnapshot;

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
            throw std::runtime_error { "could not set A2 test environment" };
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
        ::_putenv_s(name_.c_str(), had_previous_ ? previous_.c_str() : "");
#else
        if (had_previous_) {
            ::setenv(name_.c_str(), previous_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
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
            / ("fsim-a2-fanout-" + std::to_string(ticks)
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

[[nodiscard]] fsim::project::Config make_config(
    const std::filesystem::path& root,
    const fsim::project::Optimization optimization,
    const bool join)
{
    const auto module = join ? "native_frontier_a2_fanout_join"
                             : "native_frontier_a2_single_root_fanout";
    const auto source = root / (std::string { module } + ".sv");
    std::ofstream output { source, std::ios::binary };
    if (join) {
        output << R"(
module native_frontier_a2_fanout_join(output wire [64:0] sink);
  logic [64:0] source;
  wire [64:0] root_stage;
  wire [64:0] left_stage;
  wire [64:0] right_stage;
  wire [64:0] joined_stage;
  assign root_stage = source;
  assign left_stage = root_stage ^ 65'h1;
  assign right_stage = source & 65'h3;
  assign joined_stage = left_stage ^ right_stage;
  assign sink = joined_stage;
  initial begin
    source = 65'b0;
    #1 source = 65'h1ffffffffffffffff;
    #2 $finish;
  end
endmodule
)";
    } else {
        output << R"(
module native_frontier_a2_single_root_fanout(
    output wire [64:0] left_sink, output wire [64:0] right_sink);
  logic [64:0] source;
  wire [64:0] root_stage;
  wire [64:0] left_stage;
  wire [64:0] right_stage;
  assign root_stage = source;
  assign left_stage = root_stage ^ 65'h1;
  assign right_stage = root_stage ^ 65'h2;
  assign left_sink = left_stage;
  assign right_sink = right_stage;
  initial begin
    source = 65'b0;
    #1 source = 65'h1ffffffffffffffff;
    #2 $finish;
  end
endmodule
)";
    }
    require(static_cast<bool>(output), "could not write the A2 fanout fixture");

    fsim::project::Config config;
    config.project.name = module;
    config.project.top = std::string { "sv:work." } + module;
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

struct KeyTrace {
    std::array<NativeRegionAllocationTestAccess::Receipt, 2U> receipts;
    std::array<std::uint64_t, 2U> task_end_count { };
    std::array<ProcessId, 3U> producers { };
    std::uint64_t producer_replay_count { };

    [[nodiscard]] static bool matches(
        const SchedulerTraceRecord& record, const Key& key) noexcept
    {
        return record.systemverilog && record.phase
            && record.time == key.time && record.delta == key.delta
            && record.systemverilog_round == key.systemverilog_round
            && record.order == key.stable_order && record.sequence == key.sequence
            && static_cast<std::uint32_t>(*record.phase) == key.phase;
    }

    static void receive(void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& trace = *static_cast<KeyTrace*>(context);
        if ((record.kind == SchedulerTraceKind::task_begin
                || record.kind == SchedulerTraceKind::batch_begin)
            && record.systemverilog && record.phase
            && record.time == 1U
            && static_cast<std::uint32_t>(*record.phase)
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && std::find(trace.producers.begin(), trace.producers.end(), record.order)
                != trace.producers.end()) {
            ++trace.producer_replay_count;
        }
        if (record.kind != SchedulerTraceKind::task_end) {
            return;
        }
        for (std::size_t index = 0U; index < trace.receipts.size(); ++index) {
            if (matches(record, trace.receipts[index].key)) {
                ++trace.task_end_count[index];
            }
        }
    }
};

struct FanoutResult {
    std::array<std::string, 6U> values;
    std::uint64_t forwarding_evaluations { };
};

[[nodiscard]] std::array<SignalId, 6U> fanout_signals(
    const Simulation& simulation)
{
    const auto source = simulation.find_signal(
        "native_frontier_a2_single_root_fanout.source");
    const auto root = simulation.find_signal(
        "native_frontier_a2_single_root_fanout.root_stage");
    const auto left = simulation.find_signal(
        "native_frontier_a2_single_root_fanout.left_stage");
    const auto right = simulation.find_signal(
        "native_frontier_a2_single_root_fanout.right_stage");
    const auto left_sink = simulation.find_signal(
        "native_frontier_a2_single_root_fanout.left_sink");
    const auto right_sink = simulation.find_signal(
        "native_frontier_a2_single_root_fanout.right_sink");
    require(source && root && left && right && left_sink && right_sink,
        "the fanout fixture must expose both branches and their source");
    return { *source, *root, *left, *right, *left_sink, *right_sink };
}

[[nodiscard]] std::array<std::string, 6U> read_values(
    const Simulation& simulation, const std::array<SignalId, 6U>& signals)
{
    std::array<std::string, 6U> values;
    for (std::size_t index = 0U; index < signals.size(); ++index) {
        values[index] = simulation.read_signal(signals[index]).to_msb_string();
    }
    return values;
}

[[nodiscard]] FanoutResult run_compiled_fanout(
    const fsim::project::Optimization optimization,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(temporary.path, optimization, false);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the single-root fanout source must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() != 0U,
        "the fanout witness needs installed compiled processes");
    const auto signals = fanout_signals(simulation);
    const std::array<SignalId, 3U> internal { signals[1U], signals[2U], signals[3U] };
    std::array<ProcessId, 3U> owners { };
    std::array<SignalSnapshot, 3U> before;

    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "time-zero activity must settle before the fork edge");
    const auto component = NativeRegionAllocationTestAccess::component_for_signal(
        simulation, internal[0U]);
    for (std::size_t index = 0U; index < internal.size(); ++index) {
        require(NativeRegionAllocationTestAccess::component_for_signal(
                    simulation, internal[index]) == component,
            "all three internal fork outputs must share one certified component");
        owners[index] = NativeRegionAllocationTestAccess::output_owner(
            simulation, component, internal[index]);
        before[index] = NativeRegionAllocationTestAccess::snapshot(
            simulation, component, internal[index], owners[index]);
        require(before[index].current == before[index].stored
                && before[index].stored == before[index].owner
                && before[index].owner == before[index].raw_driver,
            "each fork output must start with coherent packed and raw owner roles");
    }
    const auto evaluations_before
        = NativeRegionAllocationTestAccess::forwarding_evaluations(simulation);

    // Warmup is complete. Record selection counters just before the measured
    // source transition without installing scheduler tracing, which vetoes
    // private-role admission.
    const auto members_before
        = NativeRegionAllocationTestAccess::forwarding_members(simulation);
    const auto prefixes_before
        = NativeRegionAllocationTestAccess::selected_forwarding_prefixes(simulation);
    const auto selected_members_before
        = NativeRegionAllocationTestAccess::selected_forwarding_members(simulation);

    FanoutCut cut;
    const auto hook = simulation.add_safe_point_hook(
        [&simulation, internal, before, &cut, evaluations_before,
            members_before, prefixes_before, selected_members_before](
                Scheduler& scheduler, const SchedulerPhase phase) {
            if (cut.entered) {
                return;
            }
            const std::array<SignalId, 2U> leaf_inputs { internal[1U], internal[2U] };
            if (NativeRegionAllocationTestAccess::try_capture_fanout_cut(
                    simulation, scheduler, phase, internal, before, leaf_inputs,
                    evaluations_before, members_before, prefixes_before,
                    selected_members_before, cut)) {
                scheduler.request_stop();
            }
        });
    const auto stopped = simulation.run(1U);
    simulation.remove_safe_point_hook(hook);
    require(stopped.status == RunStatus::stopped && stopped.time == 1U && cut.entered,
        "the fanout witness must stop after the authentic three-row private prefix");
    require(cut.rows.size() == 3U
            && cut.rows[0U].callback_order < cut.rows[1U].callback_order
            && cut.rows[1U].callback_order < cut.rows[2U].callback_order
            && cut.rows[0U].callback_time == 1U
            && cut.rows[1U].callback_time == 1U
            && cut.rows[2U].callback_time == 1U,
        "all three role rows must retain increasing authentic callback order and time");
    require(NativeRegionAllocationTestAccess::receipts_unchanged(simulation, cut)
            && cut.leaves[0U].process != cut.leaves[1U].process
            && cut.leaves[0U].key.time == 1U && cut.leaves[1U].key.time == 1U,
        "both terminal readers must retain their exact original pending keys at the cut");
    require(!NativeRegionAllocationTestAccess::trace_hook_installed(simulation),
        "the private admission witness must not install a scheduler trace hook");

    const auto observed = simulation.read_signal(internal[0U]).to_msb_string();
    const auto root_row = std::find_if(cut.rows.begin(), cut.rows.end(),
        [signal = internal[0U]](const auto& row) { return row.signal == signal; });
    require(root_row != cut.rows.end() && observed == root_row->predicted,
        "public observation must flush the root's private predicted value");
    for (const auto& row : cut.rows) {
        const auto after = NativeRegionAllocationTestAccess::snapshot(
            simulation, cut.component, row.signal, row.owner);
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
                && after.authoritative_revision > cut.a4_revision
                && !after.materialization_pending,
            "observation must materialize all branch roles without replaying metadata");
    }
    require(NativeRegionAllocationTestAccess::journal_retired(simulation, cut)
            && NativeRegionAllocationTestAccess::receipts_unchanged(simulation, cut),
        "the atomic observation flush must retire rows but preserve both original leaf tickets");

    KeyTrace trace;
    trace.receipts = cut.leaves;
    trace.producers = cut.producers;
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, &trace, &KeyTrace::receive);
    simulation.clear_stop();
    const auto completed = simulation.run();
    NativeRegionAllocationTestAccess::set_trace_hook(simulation, nullptr, nullptr);
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the checked leaf suffix must finish after observation");
    require(trace.task_end_count[0U] == 1U
            && trace.task_end_count[1U] == 1U
            && trace.producer_replay_count == 0U,
        "each exact original leaf key must retire once without replaying any producer");

    FanoutResult result;
    result.values = read_values(simulation, signals);
    result.forwarding_evaluations
        = NativeRegionAllocationTestAccess::forwarding_evaluations(simulation);
    return result;
}

[[nodiscard]] std::array<std::string, 6U> run_interpreter_fanout(
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(
        temporary.path, fsim::project::Optimization::o0, false);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the interpreter fanout source must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.start();
    const auto completed = simulation.run();
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the interpreter fanout reference must finish");
    return read_values(simulation, fanout_signals(simulation));
}

struct JoinProbe {
    SignalId join { std::numeric_limits<SignalId>::max() };
    bool sampled_active_cut { };
    bool private_roots_seen_without_join { };
    bool private_join_row_seen { };
    std::size_t independent_root_count { };
};

struct JoinControlResult {
    std::array<std::string, 6U> values;
    fsim::runtime::simir::JoinSignalSnapshot joined;
};

[[nodiscard]] JoinControlResult run_compiled_join_control(
    const fsim::project::Optimization optimization,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(temporary.path, optimization, true);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the join control source must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() != 0U,
        "the join control needs installed compiled processes");
    const auto source = simulation.find_signal("native_frontier_a2_fanout_join.source");
    const auto root = simulation.find_signal("native_frontier_a2_fanout_join.root_stage");
    const auto left = simulation.find_signal("native_frontier_a2_fanout_join.left_stage");
    const auto right = simulation.find_signal("native_frontier_a2_fanout_join.right_stage");
    const auto joined = simulation.find_signal("native_frontier_a2_fanout_join.joined_stage");
    const auto sink = simulation.find_signal("native_frontier_a2_fanout_join.sink");
    require(source && root && left && right && joined && sink,
        "the join control must expose both incoming branches and the join");
    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "join time-zero activity must settle");
    const auto forwarding_before
        = NativeRegionAllocationTestAccess::forwarding_evaluations(simulation);

    JoinProbe probe;
    probe.join = *joined;
    const auto hook = simulation.add_safe_point_hook(
        [&simulation, &probe, root = *root, right = *right](
            Scheduler& scheduler, const SchedulerPhase phase) {
            NativeRegionAllocationTestAccess::try_sample_join(
                simulation, scheduler, phase, root, right, probe.join,
                probe.sampled_active_cut,
                probe.private_roots_seen_without_join,
                probe.private_join_row_seen, probe.independent_root_count);
        });
    const auto completed = simulation.run();
    simulation.remove_safe_point_hook(hook);
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the join control must complete through ordinary legal scheduling");
    require(probe.sampled_active_cut && probe.independent_root_count == 2U
            && probe.private_roots_seen_without_join
            && !probe.private_join_row_seen,
        "the unequal-depth join must keep its two private roots while the join stays checked");
    require(NativeRegionAllocationTestAccess::forwarding_evaluations(simulation)
                > forwarding_before,
        "the accepted multi-root join must use flattened evaluation");

    const std::array<SignalId, 6U> signal_ids {
        *source, *root, *left, *right, *joined, *sink };
    JoinControlResult result;
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        const auto snapshot
            = NativeRegionAllocationTestAccess::join_signal_snapshot(
                simulation, signal_ids[index]);
        result.values[index] = snapshot.current;
        if (signal_ids[index] == *joined) {
            result.joined = snapshot;
        }
    }
    require(NativeRegionAllocationTestAccess::join_role_journal_empty(
                simulation, *joined),
        "public snapshots must flush the accepted multi-root role journal");
    return result;
}

[[nodiscard]] JoinControlResult run_interpreter_join_control(
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(
        temporary.path, fsim::project::Optimization::o0, true);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "the interpreter join source must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.start();
    const auto completed = simulation.run();
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the interpreter join reference must finish");
    const auto source = simulation.find_signal("native_frontier_a2_fanout_join.source");
    const auto root = simulation.find_signal("native_frontier_a2_fanout_join.root_stage");
    const auto left = simulation.find_signal("native_frontier_a2_fanout_join.left_stage");
    const auto right = simulation.find_signal("native_frontier_a2_fanout_join.right_stage");
    const auto joined = simulation.find_signal("native_frontier_a2_fanout_join.joined_stage");
    const auto sink = simulation.find_signal("native_frontier_a2_fanout_join.sink");
    require(source && root && left && right && joined && sink,
        "the interpreter join reference must expose every node");
    const std::array<SignalId, 6U> signal_ids {
        *source, *root, *left, *right, *joined, *sink };
    JoinControlResult result;
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        const auto snapshot
            = NativeRegionAllocationTestAccess::join_signal_snapshot(
                simulation, signal_ids[index]);
        result.values[index] = snapshot.current;
        if (signal_ids[index] == *joined) {
            result.joined = snapshot;
        }
    }
    return result;
}

void test_single_root_fanout_and_join()
{
    const auto interpreter = run_interpreter_fanout("fanout-interpreter");
    const auto o0 = run_compiled_fanout(
        fsim::project::Optimization::o0, "fanout-o0");
    const auto o2 = run_compiled_fanout(
        fsim::project::Optimization::o2, "fanout-o2");
    require(o0.values == interpreter && o2.values == interpreter,
        "O0, O2, and interpreter must agree across both fork branches");
    require(o0.forwarding_evaluations != 0U && o2.forwarding_evaluations != 0U,
        "both optimized executors must exercise flattened fork evaluation");

    const auto join_interpreter = run_interpreter_join_control("join-interpreter");
    const auto join_o0 = run_compiled_join_control(
        fsim::project::Optimization::o0, "join-o0");
    const auto join_o2 = run_compiled_join_control(
        fsim::project::Optimization::o2, "join-o2");
    require(join_o0.values == join_interpreter.values
            && join_o2.values == join_interpreter.values,
        "the private role journal must preserve interpreter values");
    require(join_o0.joined == join_interpreter.joined
            && join_o2.joined == join_interpreter.joined,
        "the joined role flush must preserve current, LAST, stored, driver, and event metadata");
    const auto changed_join = std::string(63U, '1') + "01";
    require(join_interpreter.joined.current == changed_join,
        "the independent-root join must publish the parsed changed value");
    require(join_interpreter.joined.event.has_value()
            && join_interpreter.joined.transaction.has_value()
            && join_interpreter.joined.stamp_domain
                == static_cast<std::uint32_t>(
                    fsim::runtime::simir::ProcessSchedulingDomain::systemverilog)
            && join_interpreter.joined.stamp_phase
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && join_interpreter.joined.value_revision != 0U,
        "the joined output snapshot must include its SystemVerilog commit and value event");
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
        test_single_root_fanout_and_join();
    } catch (const std::exception& error) {
        std::cerr << "A2 single-root fanout witness failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}

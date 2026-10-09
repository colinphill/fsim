// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace fsim::runtime::simir {

namespace {

template <typename ImplType, typename StatePointer>
[[nodiscard]] bool prepare_native_a4_single_owner_publication(
    ImplType& implementation,
    const SignalId signal_id,
    const ValueKind expected_kind,
    const ResolutionKind expected_resolution,
    StatePointer& a4_state,
    ProcessId& a4_owner)
{
    if (implementation.scheduler.trace_hook_installed()
        || implementation.native_signal_observation_any_hook
        || implementation.native_signal_observation_required_hook
        || implementation.signal_change_hook
        || implementation.stored_signal_change_hook
        || implementation.driver_change_hook
        || implementation.scalar_signal_change_hook
        || implementation.container_object_change_hook
        || implementation.container_element_change_hook
        || implementation.region_recertification_pending
        || implementation.region_recertification_requires_snapshot
        || implementation.region_authoritative_recertification_waiting
        || signal_id >= implementation.signals.size()
        || signal_id >= implementation.direct_single_driver_routes.size()
        || signal_id
            >= implementation.direct_signal_materialization_pending.size()
        || signal_id >= implementation.direct_wide_signal_offsets.size()
        || signal_id
            >= implementation.native_signal_publication_shape_certificate.size()
        || implementation.native_signal_publication_shape_certificate[
               signal_id]
            == 0U
        || implementation.direct_signal_materialization_pending[signal_id]
            != 0U
        || implementation.native_signal_has_runtime_dependency(signal_id)
        || implementation.has_dynamic_waits(signal_id)
        || implementation.monitor_watches(signal_id)
        || signal_id >= implementation.external_driver_values.size()
        || signal_id >= implementation.forced_values.size()
        || signal_id >= implementation.forced_masks.size()
        || signal_id >= implementation.forced_driver_values.size()
        || signal_id >= implementation.forced_driver_masks.size()
        || implementation.external_driver_values[signal_id]
        || implementation.forced_values[signal_id]
        || implementation.forced_masks[signal_id]
        || implementation.forced_driver_values[signal_id]
        || implementation.forced_driver_masks[signal_id]) {
        return false;
    }

    const auto wide_offset
        = implementation.direct_wide_signal_offsets[signal_id];
    if (wide_offset >= implementation.direct_wide_signal_aval.size()
        || wide_offset >= implementation.direct_wide_signal_bval.size()
        || signal_id >= implementation.signal_last_values.size()
        || signal_id >= implementation.driven_values.size()) {
        return false;
    }
    if (expected_kind == ValueKind::logic4) {
        if (signal_id >= implementation.direct_signal_aval.size()
            || signal_id >= implementation.direct_signal_bval.size()
            || signal_id >= implementation.direct_signal_last_aval.size()
            || signal_id >= implementation.direct_signal_last_bval.size()) {
            return false;
        }
    } else if (signal_id
                   >= implementation.direct_signal_logic9_plane0.size()
        || signal_id
            >= implementation.direct_signal_logic9_plane1.size()
        || signal_id
            >= implementation.direct_signal_logic9_plane2.size()
        || signal_id
            >= implementation.direct_signal_logic9_plane3.size()
        || signal_id
            >= implementation.direct_signal_last_logic9_plane0.size()
        || signal_id
            >= implementation.direct_signal_last_logic9_plane1.size()
        || signal_id
            >= implementation.direct_signal_last_logic9_plane2.size()
        || signal_id
            >= implementation.direct_signal_last_logic9_plane3.size()
        || wide_offset
            >= implementation.direct_wide_signal_logic9_plane2.size()
        || wide_offset
            >= implementation.direct_wide_signal_logic9_plane3.size()) {
        return false;
    }

    const auto& signal = implementation.signals[signal_id];
    const auto& route = implementation.direct_single_driver_routes[signal_id];
    const auto* const driver
        = implementation.direct_single_driver_record(signal_id);
    const auto width = signal.initial_value.width();
    if (!route.active || driver == nullptr
        || signal.resolution != expected_resolution
        || signal.value_kind != expected_kind
        || signal.systemverilog_scalar != SystemVerilogScalarKind::None
        || signal.event_variable || signal.has_implicit_driver
        || signal.has_charge_strength || width == 0U || width > 64U) {
        return false;
    }

    auto* const candidate_state
        = implementation.region_authoritative_state_for_signal(signal_id);
    if (candidate_state == nullptr || !candidate_state->valid()
        || candidate_state->generation()
            != implementation.region_runtime_generation
        || !candidate_state->values().requires_prewrite_unbind()
        || !candidate_state->values().packed_slots_bound()
        || !candidate_state->values().packed_signal_slots_bound(signal_id)
        || !candidate_state->values().packed_owner_slot_bound(
            signal_id, route.process)
        || candidate_state->wide_mutation_scratch().words.capacity() < 1U) {
        return false;
    }

    const auto& layout = candidate_state->values().layout();
    const auto owners = layout.owners(signal_id);
    const auto owner_mask
        = layout.owner_mask_words(signal_id, route.process);
    const auto valid_mask = width == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << width) - UINT64_C(1);
    if (owners.size() != 1U || owners.front().process != route.process
        || owners.front().aliases_stored || owner_mask.size() != 1U
        || owner_mask.front() != valid_mask
        || layout.signal(signal_id).storage_class
            != SignalDriverStorageClass::single_owner) {
        return false;
    }

    if (expected_kind == ValueKind::logic4) {
        const auto current = Logic4Word {
            width, implementation.direct_signal_aval[signal_id],
            implementation.direct_signal_bval[signal_id]
        };
        const auto previous = Logic4Word {
            width, implementation.direct_signal_last_aval[signal_id],
            implementation.direct_signal_last_bval[signal_id]
        };
        if (signal.initial_value.unchecked_low_word() != current
            || implementation.signal_last_values[signal_id]
                    .unchecked_low_word() != previous
            || candidate_state->values().current(signal_id)
                != signal.initial_value
            || candidate_state->values().previous(signal_id)
                != implementation.signal_last_values[signal_id]
            || candidate_state->values().stored(signal_id)
                != implementation.driven_values[signal_id]
            || candidate_state->values().owner_value(
                signal_id, route.process) != driver->value) {
            return false;
        }
    } else {
        const auto current = Logic9Word {
            width,
            { implementation.direct_signal_logic9_plane0[signal_id],
                implementation.direct_signal_logic9_plane1[signal_id],
                implementation.direct_signal_logic9_plane2[signal_id],
                implementation.direct_signal_logic9_plane3[signal_id] }
        };
        const auto previous = Logic9Word {
            width,
            { implementation.direct_signal_last_logic9_plane0[signal_id],
                implementation.direct_signal_last_logic9_plane1[signal_id],
                implementation.direct_signal_last_logic9_plane2[signal_id],
                implementation.direct_signal_last_logic9_plane3[signal_id] }
        };
        if (!current.has_canonical_codes()
            || !previous.has_canonical_codes()
            || !signal.initial_value.is_logic9()
            || !implementation.signal_last_values[signal_id].is_logic9()
            || !implementation.driven_values[signal_id].is_logic9()
            || !driver->value.is_logic9()) {
            return false;
        }
        const auto current_value = PackedLogic4::from_logic9_word(current);
        const auto previous_value = PackedLogic4::from_logic9_word(previous);
        if (signal.initial_value != current_value
            || implementation.signal_last_values[signal_id]
                != previous_value
            || candidate_state->values().current(signal_id)
                != signal.initial_value
            || candidate_state->values().previous(signal_id)
                != implementation.signal_last_values[signal_id]
            || candidate_state->values().stored(signal_id)
                != implementation.driven_values[signal_id]
            || candidate_state->values().owner_value(
                signal_id, route.process) != driver->value) {
            return false;
        }
    }

    a4_state = candidate_state;
    a4_owner = route.process;
    return true;
}

template <typename ImplType, typename StatePointer>
[[nodiscard]] bool publish_native_a4_single_owner_value(
    ImplType& implementation,
    const SignalId signal_id,
    StatePointer* const a4_state,
    const ProcessId a4_owner,
    const PackedLogic4& value)
{
    if (a4_state == nullptr
        || signal_id
            >= implementation.region_authoritative_component_by_signal.size()) {
        return false;
    }
    if (a4_state->values().packed_slots_bound()
        && implementation.systemverilog_wave_profile_enabled) {
        ++implementation.systemverilog_wave_profile_a4_authoritative_slot_writes;
    }
    const auto revision = a4_state->values().revision();
    a4_state->values().mirror_owner_into(
        a4_state->wide_mutation_scratch(), signal_id, a4_owner,
        value, value, value);
    return a4_state->valid()
        && a4_state->generation() == implementation.region_runtime_generation
        && implementation.region_authoritative_state_for_signal(signal_id)
            == a4_state
        && a4_state->values().requires_prewrite_unbind()
        && a4_state->values().packed_slots_bound()
        && a4_state->values().packed_signal_slots_bound(signal_id)
        && a4_state->values().packed_owner_slot_bound(signal_id, a4_owner)
        && a4_state->values().revision() == revision + 1U;
}

} // namespace

[[nodiscard]] std::size_t Interpreter::Impl::InertialDriverKeyHash::operator()(
    const InertialDriverKey& key) const noexcept
{
    auto result = static_cast<std::size_t>(key.process);
    result ^= static_cast<std::size_t>(key.signal)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.offset)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.width)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.process_domain)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.phase)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    return result;
}

[[nodiscard]] std::size_t Interpreter::Impl::ProjectedDriverKeyHash::operator()(
    const ProjectedDriverKey& key) const noexcept
{
    auto result = static_cast<std::size_t>(key.process);
    result ^= static_cast<std::size_t>(key.signal)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.offset)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    return result;
}

void Interpreter::Impl::retire_forwarding_epoch_after_scheduler_discard() noexcept
{
    bool pending_role_flush { };
    for (const auto& local : region_local_wave_state_by_component) {
        if (!local || !local->forwarding_results) {
            continue;
        }
        auto& bank = *local->forwarding_results;
        const bool applied_roles = !bank.applied_role_mutations.empty()
            || !bank.applied_role_metadata.empty();
        bank.discard();
        if (applied_roles) {
            bank.private_epoch_retired = true;
            pending_role_flush = true;
        }
    }
    region_forwarding_role_flush_pending_after_discard = pending_role_flush
        || region_forwarding_role_journal_nonempty_components != 0U;
}

void Interpreter::Impl::discard_scheduler_work() noexcept
{
    clear_systemverilog_update_pool();
    cohort_snapshots.discard();
    active_cohort_ready.clear();
    for (auto& cohort : static_sensitivity_cohorts) {
        cohort.pending = { };
        cohort.ready.clear();
    }
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        if (auto* compact = processes.compact_constant(id)) {
            compact->queued = false;
        } else {
            processes[id].queued = false;
        }
    }
    for (const auto& runtime : region_frontier_runtime_by_component) {
        if (!runtime || runtime->execution_mode
                != RegionFrontierExecutionModeV2::generic_deferred_update) {
            continue;
        }
        std::ranges::fill(runtime->generic_queued_ready_words, UINT64_C(0));
        std::ranges::fill(runtime->generic_queued_members,
            RegionFrontierComponentRuntime::GenericQueuedMember { });
        runtime->generic_prefix_processes.clear();
        runtime->generic_prefix_members.clear();
    }
    for (const auto& readiness : vhdl_projected_readiness_by_component) {
        if (!readiness) {
            continue;
        }
        for (auto& member : readiness->members) {
            member.receipt = { };
            member.static_trigger_mask = 0U;
        }
        std::ranges::fill(readiness->ticket_member_offsets,
            std::numeric_limits<std::size_t>::max());
    }
    std::ranges::fill(region_readiness_mask_words, 0U);
    for (auto& queued : region_readiness_queued_by_process) {
        queued = { };
    }
    // Scheduler entries are gone and the update pool has canceled only their
    // unapplied tokens. Applied role rows stay in the banks until a checked
    // read, write, or later slot start can materialize them.
    retire_forwarding_epoch_after_scheduler_discard();
}

[[nodiscard]] SignalHot& Interpreter::Impl::get_signal(SignalId id)
{
    if (id >= signals.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    materialize_direct_signal(id);
    return signals[id];
}

[[nodiscard]] const SignalHot& Interpreter::Impl::get_signal(
    SignalId id) const
{
    if (id >= signals.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    const_cast<Impl*>(this)->materialize_direct_signal(id);
    return signals[id];
}

[[nodiscard]] SignalCold& Interpreter::Impl::get_signal_cold(SignalId id)
{
    if (id >= signal_cold.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    return signal_cold[id];
}

[[nodiscard]] const SignalCold& Interpreter::Impl::get_signal_cold(
    SignalId id) const
{
    if (id >= signal_cold.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    return signal_cold[id];
}

DriverRecord* Interpreter::Impl::direct_single_driver_record(
    const SignalId signal_id) noexcept
{
    if (signal_id >= direct_single_driver_routes.size()
        || signal_id >= driver_values.size()
        || has_container_signal_alias(signal_id)) {
        return nullptr;
    }
    const auto& route = direct_single_driver_routes[signal_id];
    return route.active
        ? driver_values[signal_id].find(route.process)
        : nullptr;
}

const DriverRecord* Interpreter::Impl::direct_single_driver_record(
    const SignalId signal_id) const noexcept
{
    if (signal_id >= direct_single_driver_routes.size()
        || signal_id >= driver_values.size()
        || has_container_signal_alias(signal_id)) {
        return nullptr;
    }
    const auto& route = direct_single_driver_routes[signal_id];
    return route.active
        ? driver_values[signal_id].find(route.process)
        : nullptr;
}

bool Interpreter::Impl::is_aggregate_signal_proxy(
    const SignalId signal_id) const noexcept
{
    return signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id].has_value();
}

bool Interpreter::Impl::has_container_signal_alias(
    const SignalId signal_id) const noexcept
{
    if (is_aggregate_signal_proxy(signal_id)
        || (signal_id < signal_container_element_aliases.size()
            && signal_container_element_aliases[signal_id])) {
        return true;
    }
    return signal_id < signal_container_aliases.size()
        && !signal_container_aliases[signal_id].empty();
}

void Interpreter::Impl::revoke_stable_writer_shadow_for_container_alias(
    const SignalId signal_id)
{
    if (signal_id >= stable_single_writer_processes.size()
        || stable_single_writer_processes[signal_id]
            == std::numeric_limits<ProcessId>::max()) {
        return;
    }
    if (signal_writer_revision
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error {
            "SimIR signal-writer topology revision overflow"
        };
    }
    stable_single_writer_processes[signal_id]
        = std::numeric_limits<ProcessId>::max();
    ++signal_writer_revision;
}

void Interpreter::Impl::materialize_direct_signal(const SignalId id)
{
    if (id >= direct_signal_materialization_pending.size()
        || direct_signal_materialization_pending[id] == 0U) {
        return;
    }
    prepare_region_authoritative_write(id);
    const auto width = signals[id].initial_value.width();
    if (signals[id].value_kind == ValueKind::logic9) {
        const auto current = Logic9Word {
            width,
            { direct_signal_logic9_plane0[id],
                direct_signal_logic9_plane1[id],
                direct_signal_logic9_plane2[id],
                direct_signal_logic9_plane3[id] }
        };
        const auto previous = Logic9Word {
            width,
            { direct_signal_last_logic9_plane0[id],
                direct_signal_last_logic9_plane1[id],
                direct_signal_last_logic9_plane2[id],
                direct_signal_last_logic9_plane3[id] }
        };
        signals[id].initial_value.assign_logic9_word(current);
        signal_last_values[id].assign_logic9_word(previous);
        driven_values[id].assign_logic9_word(current);
        if (auto* record = direct_single_driver_record(id)) {
            record->value.assign_logic9_word(current);
        }
        direct_signal_materialization_pending[id] = 0U;
        return;
    }
    const auto current = Logic4Word {
        width, direct_signal_aval[id], direct_signal_bval[id]
    };
    const auto previous = Logic4Word {
        width, direct_signal_last_aval[id], direct_signal_last_bval[id]
    };
    signals[id].initial_value.assign_word(current);
    signal_last_values[id].assign_word(previous);
    driven_values[id].assign_word(current);
    if (auto* record = direct_single_driver_record(id)) {
        record->value.assign_word(current);
    }
    direct_signal_materialization_pending[id] = 0U;
}

[[nodiscard]] Interpreter::Impl::ProcessState&
Interpreter::Impl::get_process(ProcessId id)
{
    if (id >= processes.size()) {
        throw std::out_of_range("invalid SimIR process ID");
    }
    return processes.promote_state(id);
}

void Interpreter::Impl::ProcessTable::reserve_initial(
    const std::size_t capacity)
{
    if (!slots_.empty() || frozen_) {
        throw std::logic_error {
            "cannot reserve process table after registration"
        };
    }
    slots_.reserve(capacity);
    stable_full_.reserve_initial(capacity);
}

void Interpreter::Impl::ProcessTable::push_back(ProcessState&& process)
{
    if (process.id != slots_.size()) {
        throw std::invalid_argument {
            "SimIR process table IDs must remain dense and ordered"
        };
    }
    const auto& program_template
        = process.cold().program_storage().program_template;
    const auto scheduling_domain = program_template != nullptr
        ? program_template->scheduling_domain
        : ProcessSchedulingDomain::generic;
    const auto full_index = stable_full_.size();
    if (slots_.size() == slots_.capacity()) {
        const auto next_capacity = std::max<std::size_t>(
            4U, slots_.capacity() * 2U);
        slots_.reserve(next_capacity);
    }
    stable_full_.push_back(std::move(process));
    slots_.push_back(
        Slot { SlotKind::full, scheduling_domain, full_index });
}

void Interpreter::Impl::ProcessTable::push_compact(
    ConstantDriverStartupEntry&& process)
{
    if (frozen_ || process.id != slots_.size()) {
        throw std::logic_error {
            "compact constant processes are admitted before start"
        };
    }
    const auto scheduling_domain
        = process.program_storage->program_template->scheduling_domain;
    if (slots_.size() == slots_.capacity()) {
        const auto next_capacity = std::max<std::size_t>(
            4U, slots_.capacity() * 2U);
        slots_.reserve(next_capacity);
    }
    const auto compact_index = compact_constants_.size();
    compact_constants_.push_back(std::move(process));
    try {
        slots_.push_back(Slot {
            SlotKind::compact_constant, scheduling_domain, compact_index });
    } catch (...) {
        compact_constants_.pop_back();
        throw;
    }
    ++active_compact_constants_;
}

void Interpreter::Impl::ProcessTable::freeze_initial_storage()
{
    frozen_ = true;
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::full_state(const std::size_t index)
{
    return stable_full_[index];
}

const Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::full_state(const std::size_t index) const
{
    return stable_full_[index];
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::promote(const std::size_t id)
{
    if (id >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    auto& slot = slots_[id];
    if (slot.kind == SlotKind::full) {
        return full_state(slot.index);
    }

    const auto compact_index = slot.index;
    const auto scheduling_domain = slot.scheduling_domain;
    auto& compact = compact_constants_[compact_index];
    std::optional<OperationList> restored_startup_body;
    if (compact.startup_write_bank != nullptr) {
        // Reconstruct before appending or publishing full state. Allocation
        // failure leaves the compact descriptor and any borrowed views live.
        restored_startup_body.emplace(
            compact.startup_write_bank->operations());
    }
    ProcessState promoted;
    promoted.id = compact.id;
    promoted.generation = compact.generation;
    promoted.pc = compact.pc;
    promoted.execution_phase = compact.execution_phase;
    promoted.status = compact.status;
    promoted.static_trigger_mask = compact.static_trigger_mask;
    promoted.queued = compact.queued;
    promoted.waiting_on_static = compact.waiting_on_static;
    promoted.waiting_on_signal = compact.waiting_on_signal;
    promoted.halted = compact.halted;
    promoted.suspended = compact.suspended;
    promoted.suspended_wake = compact.suspended_wake;
    promoted.killed = compact.killed;
    promoted.suspended_status = compact.suspended_status;
    promoted.cold().design_process = compact.id;
    promoted.cold().random_state = compact.random_state;
    promoted.clear_frontier_debug_token();
    promoted.cold().current_source = compact.current_source;
    promoted.cold().current_scope = compact.current_scope;
    promoted.cold().track_interpreter_operations
        = compact.track_interpreter_operations;
    promoted.cold().interpreter_operations
        = compact.interpreter_operations;
    promoted.cold().profile_calls = compact.profile_calls;
    promoted.cold().profile_interpreter_operations
        = compact.profile_interpreter_operations;
    promoted.cold().profile_updates = compact.profile_updates;
    promoted.cold().profile_total_nanoseconds
        = compact.profile_total_nanoseconds;
    const auto full_index = stable_full_.size();
    stable_full_.push_back(std::move(promoted));
    if (restored_startup_body) {
        compact.program_storage->instance_program.operations
            = std::move(*restored_startup_body);
    }
    full_state(full_index).cold().retained_program_storage
        = std::move(compact.program_storage);

    slot = Slot { SlotKind::full, scheduling_domain, full_index };
    --active_compact_constants_;
    return full_state(full_index);
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::operator[](const std::size_t index)
{
    if (index >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    if (slots_[index].kind != SlotKind::full) {
        throw std::logic_error {
            "compact process access requires explicit promotion"
        };
    }
    return full_state(slots_[index].index);
}

const Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::operator[](const std::size_t index) const
{
    if (index >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    if (slots_[index].kind != SlotKind::full) {
        throw std::logic_error {
            "compact constant process has no full ProcessState"
        };
    }
    return full_state(slots_[index].index);
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::at(const std::size_t index)
{
    return (*this)[index];
}

const Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::at(const std::size_t index) const
{
    return (*this)[index];
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::promote_state(const std::size_t index)
{
    return promote(index);
}

Interpreter::Impl::ProcessState*
Interpreter::Impl::ProcessTable::full_state_if_present(
    const ProcessId id) noexcept
{
    if (id >= slots_.size() || slots_[id].kind != SlotKind::full) {
        return nullptr;
    }
    return std::addressof(full_state(slots_[id].index));
}

const Interpreter::Impl::ProcessState*
Interpreter::Impl::ProcessTable::full_state_if_present(
    const ProcessId id) const noexcept
{
    if (id >= slots_.size() || slots_[id].kind != SlotKind::full) {
        return nullptr;
    }
    return std::addressof(full_state(slots_[id].index));
}

ProcessProgramView Interpreter::Impl::ProcessTable::program_view(
    const ProcessId id) const
{
    if (id >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    const auto& slot = slots_[id];
    if (slot.kind == SlotKind::full) {
        return full_state(slot.index).program();
    }
    const auto& compact = compact_constants_[slot.index];
    const auto& storage = *compact.program_storage;
    return ProcessProgramView {
        *storage.program_template, storage.instance_program,
        compact.startup_write_bank.get()
    };
}

const Process& Interpreter::Impl::ProcessTable::public_program(
    const ProcessId id) const
{
    if (id >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    if (slots_[id].kind == SlotKind::full) {
        return full_state(slots_[id].index).cold().public_program();
    }
    auto& storage = *compact_constants_[slots_[id].index].program_storage;
    if (!storage.public_program_facade) {
        storage.public_program_facade
            = std::make_unique<Process>(program_view(id).materialize());
    }
    return *storage.public_program_facade;
}

std::size_t Interpreter::Impl::ProcessTable::operation_count(
    const ProcessId id) const
{
    if (id >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    if (const auto* const compact = compact_constant(id)) {
        if (compact->startup_write_bank != nullptr) {
            return compact->startup_write_bank->operation_count;
        }
        return compact->program_storage->instance_program.operations.size();
    }
    return program_view(id).operations().size();
}

Interpreter::Impl::ConstantDriverStartupEntry*
Interpreter::Impl::ProcessTable::compact_constant(
    const ProcessId id) noexcept
{
    if (id >= slots_.size()
        || slots_[id].kind != SlotKind::compact_constant) {
        return nullptr;
    }
    return std::addressof(compact_constants_[slots_[id].index]);
}

const Interpreter::Impl::ConstantDriverStartupEntry*
Interpreter::Impl::ProcessTable::compact_constant(
    const ProcessId id) const noexcept
{
    if (id >= slots_.size()
        || slots_[id].kind != SlotKind::compact_constant) {
        return nullptr;
    }
    return std::addressof(compact_constants_[slots_[id].index]);
}

bool Interpreter::Impl::ProcessTable::is_compact_constant(
    const ProcessId id) const noexcept
{
    return compact_constant(id) != nullptr;
}

std::size_t Interpreter::Impl::ProcessTable::compact_constant_count() const noexcept
{
    return active_compact_constants_;
}

bool Interpreter::Impl::ProcessTable::public_facade_materialized(
    const ProcessId id) const noexcept
{
    if (id >= slots_.size()) {
        return false;
    }
    const auto& slot = slots_[id];
    if (slot.kind == SlotKind::full) {
        return full_state(slot.index).cold()
            .program_storage().public_program_facade != nullptr;
    }
    return compact_constants_[slot.index].program_storage
        ->public_program_facade != nullptr;
}

void Interpreter::Impl::ProcessTable::add_profile_updates(
    const ProcessId id, const std::size_t count)
{
    if (auto* compact = compact_constant(id)) {
        compact->profile_updates += count;
        return;
    }
    full_state_if_present(id)->cold().profile_updates += count;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_updates(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->profile_updates;
    }
    return full_state_if_present(id)->cold().profile_updates;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_total_nanoseconds(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->profile_total_nanoseconds;
    }
    return full_state_if_present(id)->cold().profile_total_nanoseconds;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_calls(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->profile_calls;
    }
    return full_state_if_present(id)->cold().profile_calls;
}

std::uint64_t
Interpreter::Impl::ProcessTable::profile_interpreter_operations(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->profile_interpreter_operations;
    }
    return full_state_if_present(id)->cold().profile_interpreter_operations;
}

std::uint64_t Interpreter::Impl::ProcessTable::interpreter_operations(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->interpreter_operations;
    }
    return full_state_if_present(id)->cold().interpreter_operations;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_native_resumes(
    const ProcessId id) const
{
    return compact_constant(id) != nullptr
        ? 0U
        : full_state_if_present(id)->cold().profile_native_resumes;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_native_nanoseconds(
    const ProcessId id) const
{
    return compact_constant(id) != nullptr
        ? 0U
        : full_state_if_present(id)->cold().profile_native_nanoseconds;
}

bool Interpreter::Impl::ProcessTable::has_executor(
    const ProcessId id) const noexcept
{
    const auto* state = full_state_if_present(id);
    return state != nullptr && static_cast<bool>(state->executor);
}

[[nodiscard]] Interpreter::Impl::ProcessFrame&
Interpreter::Impl::ensure_process_frame(ProcessState& process)
{
    if (process.frame) {
        return *process.frame;
    }

    auto frame = std::make_shared<ProcessFrame>();
    frame->registers.assign(
        process.program().register_count(), PackedLogic4 { });
    frame->string_registers.assign(
        process.program().string_register_count(), { });
    frame->container_registers.reserve(
        process.program().container_register_count());
    const auto container_register_types
        = process_layout_detail::ProcessLayoutAccess::view(
            process.program().container_register_types());
    for (const auto& type : container_register_types) {
        frame->container_registers.push_back(
            default_container_register(type));
    }
    process.frame = std::move(frame);
    return *process.frame;
}

[[nodiscard]] bool Interpreter::Impl::can_install_deferred_executor(
    const ProcessState& process) noexcept
{
    return (!process.frame
               || (process.frame.use_count() == 1
                   && process.frame->vital_memories.empty()))
        && process.cold().dynamic_call_stack.empty()
        && process.cold().callable_frames.empty();
}

[[nodiscard]] PackedLogic4& Interpreter::Impl::get_register(ProcessState& process,
    RegisterId id)
{
    auto& frame = ensure_process_frame(process);
    if (id >= frame.registers.size()) {
        throw InterpreterError(process.id, process.pc,
            "invalid register ID");
    }
    return frame.registers[id];
}

[[nodiscard]] std::string& Interpreter::Impl::get_string_register(
    ProcessState& process,
    const StringRegisterId id)
{
    auto& frame = ensure_process_frame(process);
    if (id >= frame.string_registers.size()) {
        throw InterpreterError(
            process.id, process.pc, "invalid string register ID");
    }
    return frame.string_registers[id];
}

[[nodiscard]] StringObject& Interpreter::Impl::get_string_object(
    const StringObjectId id)
{
    if (id >= string_objects.size()) {
        throw std::out_of_range { "invalid SimIR string object ID" };
    }
    return string_objects[id];
}

[[nodiscard]] const StringObject& Interpreter::Impl::get_string_object(
    const StringObjectId id) const
{
    if (id >= string_objects.size()) {
        throw std::out_of_range { "invalid SimIR string object ID" };
    }
    return string_objects[id];
}

[[nodiscard]] ContainerValue&
Interpreter::Impl::get_container_register(
    ProcessState& process,
    const ContainerRegisterId id)
{
    if (!process.frame || id >= process.frame->container_registers.size()) {
        throw InterpreterError(
            process.id, process.pc,
            "invalid container register ID");
    }
    auto& storage = process.frame->container_registers[id];
    if (!storage) {
        throw InterpreterError(
            process.id, process.pc,
            "uninitialized container register storage");
    }
    if (storage.use_count() != 1) {
        storage = std::make_shared<ContainerValue>(*storage);
    }
    return *storage;
}

[[nodiscard]] const ContainerValue&
Interpreter::Impl::read_container_register(
    const ProcessState& process,
    const ContainerRegisterId id) const
{
    if (!process.frame || id >= process.frame->container_registers.size()
        || !process.frame->container_registers[id]) {
        throw InterpreterError(
            process.id, process.pc,
            "invalid container register ID");
    }
    return *process.frame->container_registers[id];
}

[[nodiscard]] Interpreter::Impl::SharedContainerValue
Interpreter::Impl::container_register_storage(
    const ProcessState& process,
    const ContainerRegisterId id) const
{
    (void)read_container_register(process, id);
    return process.frame->container_registers[id];
}

void Interpreter::Impl::set_container_register_storage(
    ProcessState& process,
    const ContainerRegisterId id,
    SharedContainerValue value)
{
    if (!value || !process.frame
        || id >= process.frame->container_registers.size()) {
        throw InterpreterError(
            process.id, process.pc,
            "invalid container register storage");
    }
    process.frame->container_registers[id] = std::move(value);
}

[[nodiscard]] Interpreter::Impl::SharedContainerValue
Interpreter::Impl::default_container_register(const ContainerType& type)
{
    const auto found = std::ranges::find_if(
        default_container_values,
        [&](const SharedContainerValue& value) {
            return value && value->type == type;
        });
    if (found != default_container_values.end()) {
        return *found;
    }
    auto value = std::make_shared<ContainerValue>(
        default_container_value(type));
    default_container_values.push_back(value);
    return value;
}

[[nodiscard]] ContainerObject& Interpreter::Impl::get_container_object(
    const ContainerObjectId id)
{
    if (id >= container_objects.size()) {
        throw std::out_of_range { "invalid SimIR container object ID" };
    }
    return container_objects[id];
}

[[nodiscard]] const ContainerObject&
Interpreter::Impl::get_container_object(
    const ContainerObjectId id) const
{
    if (id >= container_objects.size()) {
        throw std::out_of_range { "invalid SimIR container object ID" };
    }
    return container_objects[id];
}

[[nodiscard]] ValueKind Interpreter::Impl::register_value_kind(
    const ProcessState& process,
    const RegisterId id)
{
    if (process.program().register_value_kinds().empty()) {
        return ValueKind::logic4;
    }
    return process_layout_detail::ProcessLayoutAccess::copy_at(
        process.program().register_value_kinds(), id);
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::coerce_value_kind(
    PackedLogic4 value,
    const ValueKind kind)
{
    if (kind == ValueKind::logic9) {
        return value.is_logic9()
            ? value
            : value.promoted_to_logic9();
    }
    return value.is_logic9()
        ? collapse_to_logic4(value)
        : value;
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::normalize_signal_value(
    const SignalId signal,
    PackedLogic4 value) const
{
    auto normalized = coerce_value_kind(
        std::move(value),
        get_signal(signal).value_kind);
    const auto kind = get_signal(signal).systemverilog_scalar;
    if (kind != SystemVerilogScalarKind::None) {
        const auto decoded = decode_systemverilog_scalar_payload(
            normalized, kind);
        if (!decoded) {
            throw std::invalid_argument {
                "SimIR scalar signal has an invalid encoded payload"
            };
        }
        const auto classification = classify_systemverilog_scalar(decoded.value);
        if (kind != SystemVerilogScalarKind::Chandle
            && (!classification || !classification.finite)) {
            throw std::invalid_argument {
                "SimIR scalar signal requires a finite value"
            };
        }
    }
    return normalized;
}

void Interpreter::Impl::remove_dynamic_wait_nonempty(ProcessState& process)
{
    auto& cold = process.cold();
    auto* const dynamic_wait = cold.dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr) {
        process.waiting_on_signal = false;
        return;
    }
    const auto old_wait_generation = dynamic_wait->dynamic_wait_generation;
    if (dynamic_wait->dynamic_wait_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        fail(process, "dynamic wait generation overflow");
    }
    ++dynamic_wait->dynamic_wait_generation;
    ensure_dynamic_fanout_counts();
    for (std::size_t sensitivity_index = 0;
        sensitivity_index < dynamic_wait->dynamic_sensitivity.size();
        ++sensitivity_index) {
        const auto signal
            = dynamic_wait->dynamic_sensitivity[sensitivity_index].signal;
        if (signal >= dynamic_fanout.size()
            || sensitivity_index
                >= dynamic_wait->dynamic_fanout_positions.size()) {
            continue;
        }
        auto& fanout = dynamic_fanout[signal];
        const auto position
            = dynamic_wait->dynamic_fanout_positions[sensitivity_index];
        if (position >= fanout.size()) {
            continue;
        }
        auto& registration = fanout[position];
        if (!registration.active
            || registration.process != process.id
            || registration.process_generation != process.generation
            || registration.wait_generation != old_wait_generation
            || registration.sensitivity_index != sensitivity_index) {
            continue;
        }
        registration.active = false;
        auto& active = dynamic_fanout_active_counts[signal];
        if (active != 0U) {
            --active;
        }
        ++dynamic_fanout_tombstone_counts[signal];
        compact_dynamic_fanout(signal);
    }
    dynamic_wait->dynamic_sensitivity.clear();
    dynamic_wait->dynamic_triggered.clear();
    dynamic_wait->dynamic_fanout_positions.clear();
    dynamic_wait->dynamic_wait_all = false;
    dynamic_wait->wait_order_events.clear();
    dynamic_wait->wait_order_index = 0;
    dynamic_wait->wait_order_result.reset();
    if (dynamic_wait->waiting_on_container) {
        auto& fanout
            = container_dynamic_fanout[*dynamic_wait->waiting_on_container];
        std::erase(fanout, process.id);
        dynamic_wait->waiting_on_container.reset();
    }
    process.waiting_on_signal = false;
}

void Interpreter::Impl::ensure_dynamic_fanout_counts()
{
    if (dynamic_fanout_active_counts.size() < dynamic_fanout.size()) {
        dynamic_fanout_active_counts.resize(dynamic_fanout.size());
    }
    if (dynamic_fanout_tombstone_counts.size() < dynamic_fanout.size()) {
        dynamic_fanout_tombstone_counts.resize(dynamic_fanout.size());
    }
}

void Interpreter::Impl::register_dynamic_wait_fanout(ProcessState& process)
{
    auto& cold = process.cold();
    auto* const dynamic_wait = cold.dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr) {
        throw std::logic_error {
            "dynamic wait registration has no process-side wait state"
        };
    }
    dynamic_wait->dynamic_fanout_positions.clear();
    dynamic_wait->dynamic_fanout_positions.reserve(
        dynamic_wait->dynamic_sensitivity.size());
    for (const auto& sensitivity : dynamic_wait->dynamic_sensitivity) {
        if (sensitivity.signal >= dynamic_fanout.size()) {
            fail(process, "dynamic wait references an invalid signal");
        }
    }
    if (dynamic_wait->dynamic_wait_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        fail(process, "dynamic wait generation overflow");
    }
    ++dynamic_wait->dynamic_wait_generation;
    ensure_dynamic_fanout_counts();
    std::size_t registered = 0;
    try {
        for (std::size_t index = 0;
            index < dynamic_wait->dynamic_sensitivity.size(); ++index) {
            const auto& sensitivity = dynamic_wait->dynamic_sensitivity[index];
            compact_dynamic_fanout(sensitivity.signal);
            auto& active = dynamic_fanout_active_counts[sensitivity.signal];
            if (active == std::numeric_limits<std::size_t>::max()) {
                fail(process, "dynamic wait registration count overflow");
            }
            auto& fanout = dynamic_fanout[sensitivity.signal];
            const auto position = fanout.size();
            fanout.push_back({
                process.id,
                process.generation,
                dynamic_wait->dynamic_wait_generation,
                sensitivity.edge,
                index,
                true
            });
            dynamic_wait->dynamic_fanout_positions.push_back(position);
            ++active;
            ++registered;
        }
    } catch (...) {
        for (std::size_t index = 0; index < registered; ++index) {
            const auto signal
                = dynamic_wait->dynamic_sensitivity[index].signal;
            const auto position
                = dynamic_wait->dynamic_fanout_positions[index];
            auto& fanout = dynamic_fanout[signal];
            if (position < fanout.size() && fanout[position].active
                && fanout[position].process == process.id
                && fanout[position].wait_generation
                    == dynamic_wait->dynamic_wait_generation) {
                fanout[position].active = false;
                --dynamic_fanout_active_counts[signal];
                ++dynamic_fanout_tombstone_counts[signal];
            }
        }
        dynamic_wait->dynamic_fanout_positions.clear();
        throw;
    }
}

[[nodiscard]] bool Interpreter::Impl::dynamic_wait_registration_is_current(
    const ProcessState& process,
    const DynamicWaitRegistration& registration,
    const SignalId signal) const noexcept
{
    if (!registration.active
        || registration.process != process.id
        || registration.process_generation != process.generation
        || registration.wait_generation == 0U
        || process.halted || !process.waiting_on_signal) {
        return false;
    }
    const auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr
        || dynamic_wait->dynamic_wait_generation != registration.wait_generation
        || registration.sensitivity_index
            >= dynamic_wait->dynamic_sensitivity.size()) {
        return false;
    }
    const auto& sensitivity = dynamic_wait->dynamic_sensitivity[
        registration.sensitivity_index];
    return sensitivity.signal == signal
        && sensitivity.edge == registration.edge;
}

void Interpreter::Impl::compact_dynamic_fanout(const SignalId signal)
{
    if (signal >= dynamic_fanout.size()) {
        return;
    }
    ensure_dynamic_fanout_counts();
    auto& fanout = dynamic_fanout[signal];
    const auto active = dynamic_fanout_active_counts[signal];
    const auto tombstones = dynamic_fanout_tombstone_counts[signal];
    if (tombstones == 0U
        || (active != 0U
            && (tombstones < 64U || tombstones < active))) {
        return;
    }
    std::erase_if(fanout, [&](const DynamicWaitRegistration& registration) {
        return registration.process >= processes.size()
            || !dynamic_wait_registration_is_current(
                processes[registration.process], registration, signal);
    });
    for (std::size_t index = 0; index < fanout.size(); ++index) {
        const auto& registration = fanout[index];
        if (registration.process >= processes.size()) {
            continue;
        }
        auto& process = processes[registration.process];
        auto* const dynamic_wait
            = process.cold().dynamic_wait_state_if_present();
        if (dynamic_wait != nullptr
            && registration.sensitivity_index
                < dynamic_wait->dynamic_fanout_positions.size()
            && dynamic_wait_registration_is_current(
                process, registration, signal)) {
            dynamic_wait->dynamic_fanout_positions[
                registration.sensitivity_index] = index;
        }
    }
    dynamic_fanout_active_counts[signal] = fanout.size();
    dynamic_fanout_tombstone_counts[signal] = 0;
}

[[nodiscard]] bool Interpreter::Impl::has_dynamic_waits(
    const SignalId signal) const noexcept
{
    return signal < dynamic_fanout_active_counts.size()
        && dynamic_fanout_active_counts[signal] != 0U;
}

void Interpreter::Impl::write_process_register(
    Interpreter::Impl::ProcessState& process,
    const RegisterId destination,
    const PackedLogic4& value)
{
    const auto converted = coerce_value_kind(
        value, register_value_kind(process, destination));
    if (process.cold().suspended_callable_context) {
        auto& context = *process.cold().suspended_callable_context;
        const auto found = std::ranges::lower_bound(
            context.packed_ids, destination);
        if (found != context.packed_ids.end()
            && *found == destination) {
            const auto index = static_cast<std::size_t>(
                std::distance(context.packed_ids.begin(), found));
            context.packed[index] = converted;
            return;
        }
    }
    if (process.executor) {
        process.executor->write_register(
            destination, converted);
        return;
    }
    get_register(process, destination) = converted;
}

void Interpreter::Impl::clear_wait_timeout_nonempty(ProcessState& process)
{
    auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr) {
        process.wait_timeout_origin.reset();
        return;
    }
    if (dynamic_wait->wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        fail(process, "wait timeout generation overflow");
    }
    ++dynamic_wait->wait_timeout_generation;
    process.wait_timeout_origin.reset();
    dynamic_wait->wait_timeout_deadline.reset();
    dynamic_wait->wait_timeout_result.reset();
}

void Interpreter::Impl::set_wait_timeout_result(
    Interpreter::Impl::ProcessState& process,
    const bool timed_out)
{
    auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr || !dynamic_wait->wait_timeout_result) {
        return;
    }
    write_process_register(
        process,
        *dynamic_wait->wait_timeout_result,
        PackedLogic4::from_msb_string(
            timed_out ? "1" : "0"));
}

void Interpreter::Impl::begin_wait_timeout(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex origin,
    const SimulationTick delay,
    const std::optional<RegisterId> result)
{
    clear_wait_timeout(process);
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - scheduler.now()) {
        process.pc = origin;
        fail(process, "simulation time overflow in WaitOn timeout");
    }
    auto& dynamic_wait = process.cold().ensure_dynamic_wait_state();
    if (dynamic_wait.wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        process.pc = origin;
        fail(process, "wait timeout generation overflow");
    }
    const auto generation = ++dynamic_wait.wait_timeout_generation;
    const auto deadline = scheduler.now() + delay;
    process.wait_timeout_origin = origin;
    dynamic_wait.wait_timeout_deadline = deadline;
    dynamic_wait.wait_timeout_result = result;
    set_wait_timeout_result(process, false);

    struct WaitTimeoutTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
        InstructionIndex origin { };
        std::uint64_t generation { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            WaitTimeoutTask,
            +[](Scheduler&, const WaitTimeoutTask& scheduled) {
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                auto* const timeout
                    = state.cold().dynamic_wait_state_if_present();
                if (timeout == nullptr
                    || timeout->wait_timeout_generation
                        != scheduled.generation
                    || state.wait_timeout_origin
                        != std::optional { scheduled.origin }) {
                    return;
                }
                scheduled.owner->set_wait_timeout_result(state, true);
                state.wait_timeout_origin.reset();
                timeout->wait_timeout_deadline.reset();
                timeout->wait_timeout_result.reset();
                scheduled.owner->queue_active_current(
                    scheduled.process);
            }>(WaitTimeoutTask {
                this, process.id, origin, generation });
    const bool systemverilog_process
        = process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog;
    if (delay == 0) {
        if (systemverilog_process) {
            scheduler.schedule_internal_systemverilog(
                SchedulerPhase::inactive, process.id, task);
        } else {
            scheduler.schedule_internal(
                SchedulerPhase::inactive, process.id, task);
        }
    } else if (systemverilog_process) {
        scheduler.schedule_internal_systemverilog_at(
            deadline, SchedulerPhase::active, process.id, task);
    } else {
        scheduler.schedule_internal_at(
            deadline, SchedulerPhase::active, process.id, task);
    }
}

void Interpreter::Impl::rearm_wait_timeout(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex origin,
    const std::optional<RegisterId> result)
{
    const auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (process.wait_timeout_origin
            != std::optional { origin }
        || dynamic_wait == nullptr
        || !dynamic_wait->wait_timeout_deadline) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout rearm has no matching active deadline");
    }
    if (dynamic_wait->wait_timeout_result != result) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout rearm result register mismatch");
    }
    if (*dynamic_wait->wait_timeout_deadline < scheduler.now()) {
        process.pc = instruction;
        fail(process, "WaitOn timeout deadline was missed");
    }
}

void Interpreter::Impl::mark_dynamic_event_resume(
    Interpreter::Impl::ProcessState& process)
{
    const auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    const auto timed_out = dynamic_wait != nullptr
        && dynamic_wait->wait_timeout_deadline
        && *dynamic_wait->wait_timeout_deadline <= scheduler.now();
    set_wait_timeout_result(process, timed_out);
}

[[nodiscard]] bool Interpreter::Impl::dynamic_wait_satisfied(
    Interpreter::Impl::ProcessState& process,
    const SignalId signal,
    const DynamicWaitRegistration& registration)
{
    if (!dynamic_wait_registration_is_current(
            process, registration, signal)) {
        return false;
    }
    auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr) {
        return false;
    }
    if (dynamic_wait->wait_order_result) {
        if (dynamic_wait->wait_order_index
            >= dynamic_wait->wait_order_events.size()) {
            throw std::logic_error {
                "wait_order state has no expected event"
            };
        }
        if (signal
            == dynamic_wait->wait_order_events[
                dynamic_wait->wait_order_index]) {
            ++dynamic_wait->wait_order_index;
            if (dynamic_wait->wait_order_index
                != dynamic_wait->wait_order_events.size()) {
                return false;
            }
            write_process_register(
                process,
                *dynamic_wait->wait_order_result,
                PackedLogic4::from_aval_bval(1, 1, 0));
            return true;
        }
        write_process_register(
            process,
            *dynamic_wait->wait_order_result,
            PackedLogic4::from_aval_bval(1, 0, 0));
        return true;
    }
    if (!dynamic_wait->dynamic_wait_all) {
        return true;
    }
    if (registration.sensitivity_index
        >= dynamic_wait->dynamic_triggered.size()) {
        throw std::logic_error {
            "dynamic wait-all state has no matching sensitivity"
        };
    }
    dynamic_wait->dynamic_triggered[registration.sensitivity_index] = true;
    return std::all_of(
        dynamic_wait->dynamic_triggered.begin(),
        dynamic_wait->dynamic_triggered.end(),
        [](const bool triggered) { return triggered; });
}

void Interpreter::Impl::register_static_sensitivity_cohort(
    const ProcessId id)
{
    const auto process = processes.program_view(id);
    static_sensitivity_cohort_by_process.resize(
        processes.size(), std::numeric_limits<std::size_t>::max());
    if (process.static_sensitivity().empty()) {
        return;
    }

    auto sensitivity = process.static_sensitivity();
    normalize_sensitivities(sensitivity);

    std::string key;
    key += process.postponed() ? 'p'
        : process.reactive() ? 'r'
        : process.observed() ? 'o'
                           : 'a';
    key += process.scheduling_domain()
            == ProcessSchedulingDomain::systemverilog
        ? 's'
        : 'g';
    for (const auto& entry : sensitivity) {
        key += std::to_string(static_cast<std::size_t>(entry.signal));
        key += ':';
        key += std::to_string(static_cast<std::underlying_type_t<EdgeKind>>(
            entry.edge));
        key += ':';
        key += std::to_string(entry.offset);
        key += ':';
        key += std::to_string(entry.width);
        key += ';';
    }
    const auto [found, inserted]
        = static_sensitivity_cohort_by_key.try_emplace(
            std::move(key), static_sensitivity_cohorts.size());
    if (inserted) {
        static_sensitivity_cohorts.emplace_back();
    }
    const auto cohort = found->second;
    static_sensitivity_cohorts[cohort].members.push_back(id);
    static_sensitivity_cohort_by_process[id] = cohort;
}

void Interpreter::Impl::rebuild_static_fanout()
{
    if (!static_fanout_dirty) {
        return;
    }

    constexpr auto category_count = std::size_t { 4U };
    using CategoryCounts = std::array<std::size_t, category_count>;
    std::vector<std::size_t> counts(signals.size(), 0U);
    std::vector<CategoryCounts> category_counts(
        signals.size(), CategoryCounts { });
    // Inert static kernel members have no static sensitivity.
    const auto inert = [&](const std::size_t process) {
        return static_kernel && process < fusion_dormant_process.size()
            && fusion_dormant_process[process] == 1U;
    };
    for (std::size_t process_index = 0;
         process_index < processes.size(); ++process_index) {
        if (inert(process_index)) {
            continue;
        }
        for (const auto& sensitivity :
             processes.program_view(
                 static_cast<ProcessId>(process_index)).static_sensitivity()) {
            if (sensitivity.signal >= counts.size()) {
                throw std::logic_error {
                    "static sensitivity references a missing signal"
                };
            }
            const auto category = static_cast<std::size_t>(sensitivity.edge);
            if (category >= category_count) {
                throw std::logic_error {
                    "static sensitivity has an invalid edge kind"
                };
            }
            auto& count = counts[sensitivity.signal];
            if (count == std::numeric_limits<std::size_t>::max()) {
                throw std::length_error {
                    "static sensitivity fanout is too large"
                };
            }
            ++count;
            auto& category_count_for_signal
                = category_counts[sensitivity.signal][category];
            if (category_count_for_signal
                == std::numeric_limits<std::size_t>::max()) {
                throw std::length_error {
                    "static sensitivity fanout is too large"
                };
            }
            ++category_count_for_signal;
        }
    }

    std::vector<std::size_t> offsets(signals.size() + 1U, 0U);
    for (std::size_t signal = 0; signal < counts.size(); ++signal) {
        const auto previous = offsets[signal];
        if (counts[signal]
            > std::numeric_limits<std::size_t>::max() - previous) {
            throw std::length_error {
                "static sensitivity fanout is too large"
            };
        }
        offsets[signal + 1U] = previous + counts[signal];
    }

    std::vector<std::array<FanoutSpan, category_count>> category_spans(
        signals.size());
    std::size_t category_entry_count { };
    for (std::size_t signal = 0; signal < category_counts.size(); ++signal) {
        for (std::size_t category = 0; category < category_count;
             ++category) {
            auto& span = category_spans[signal][category];
            span.begin = category_entry_count;
            span.count = category_counts[signal][category];
            if (span.count
                > std::numeric_limits<std::size_t>::max()
                    - category_entry_count) {
                throw std::length_error {
                    "static sensitivity fanout is too large"
                };
            }
            category_entry_count += span.count;
        }
    }

    std::vector<Fanout> entries(offsets.back());
    std::vector<std::size_t> next(offsets.begin(), offsets.end() - 1);
    std::vector<std::size_t> category_entries(category_entry_count);
    std::vector<CategoryCounts> category_next(signals.size());
    for (std::size_t signal = 0; signal < category_spans.size(); ++signal) {
        for (std::size_t category = 0; category < category_count;
             ++category) {
            category_next[signal][category]
                = category_spans[signal][category].begin;
        }
    }
    for (std::size_t process_index = 0;
         process_index < processes.size(); ++process_index) {
        if (inert(process_index)) {
            continue;
        }
        const auto process = processes.program_view(
            static_cast<ProcessId>(process_index));
        const auto id = static_cast<ProcessId>(process_index);
        for (std::size_t sensitivity_index = 0;
             sensitivity_index < process.static_sensitivity().size();
             ++sensitivity_index) {
            const auto& sensitivity
                = process.static_sensitivity()[sensitivity_index];
            const auto trigger_mask
                = sensitivity_index < 63U
                    && !process.static_trigger_regions().empty()
                ? UINT64_C(1) << sensitivity_index
                : Process::full_static_trigger_mask;
            const auto category = static_cast<std::size_t>(sensitivity.edge);
            const auto entry_index = next[sensitivity.signal]++;
            entries[entry_index] = {
                id, sensitivity.edge, trigger_mask,
                sensitivity.offset, sensitivity.width
            };
            category_entries[
                category_next[sensitivity.signal][category]++] = entry_index;
        }
    }

    static_fanout_entries.swap(entries);
    static_fanout_offsets.swap(offsets);
    static_fanout_category_entries.swap(category_entries);
    static_fanout_category_spans.swap(category_spans);
    static_fanout_dirty = false;
}

std::span<const Interpreter::Impl::Fanout>
Interpreter::Impl::static_fanout_for(const SignalId signal) const noexcept
{
    const auto index = static_cast<std::size_t>(signal);
    if (static_fanout_offsets.empty()
        || index >= static_fanout_offsets.size() - 1U) {
        return { };
    }
    const auto begin = static_fanout_offsets[index];
    const auto count = static_fanout_offsets[index + 1U] - begin;
    return std::span<const Fanout> { static_fanout_entries }
        .subspan(begin, count);
}

std::span<const std::size_t>
Interpreter::Impl::static_fanout_indices_for(
    const SignalId signal,
    const EdgeKind edge) const noexcept
{
    const auto index = static_cast<std::size_t>(signal);
    const auto category = static_cast<std::size_t>(edge);
    if (index >= static_fanout_category_spans.size()
        || category >= static_fanout_category_spans[index].size()) {
        return { };
    }
    const auto span = static_fanout_category_spans[index][category];
    return std::span<const std::size_t> { static_fanout_category_entries }
        .subspan(span.begin, span.count);
}

RegionAuthoritativeComponentState*
Interpreter::Impl::region_authoritative_state_for_signal(
    const SignalId signal) noexcept
{
    if (!region_graph
        || signal >= region_authoritative_component_by_signal.size()) {
        return nullptr;
    }
    const auto component = region_authoritative_component_by_signal[signal];
    if (component >= region_authoritative_state_by_component.size()
        || component >= region_graph->certificate_inventory().components.size()
        || !region_graph->component_epochs_current(component)) {
        return nullptr;
    }
    auto* const state
        = region_authoritative_state_by_component[component].get();
    if (state == nullptr || !state->valid()
        || state->generation() != region_runtime_generation) {
        return nullptr;
    }
    return state;
}

void Interpreter::Impl::note_region_authoritative_mirror() noexcept
{
    if (region_authoritative_recertification_waiting) {
        // A previous quiet-point reseed found a checked value that the A4
        // planes cannot represent. Retry only after subsequent state traffic,
        // rather than rebuilding the whole snapshot at every quiet point.
        request_full_region_recertification();
    }
}

bool Interpreter::Impl::region_has_fanout_member(
    const SignalId signal, const ProcessId process) noexcept
{
    auto* const state = region_authoritative_state_for_signal(signal);
    if (state == nullptr || process >= region_authoritative_member_index_by_process.size()
        || process >= region_component_by_process.size()) {
        return false;
    }
    const auto component = region_authoritative_component_by_signal[signal];
    return region_component_by_process[process] == component
        && region_authoritative_member_index_by_process[process]
            < state->readiness().member_count();
}

bool Interpreter::Impl::region_take_ready(
    const SignalId signal,
    const ProcessId process,
    std::uint64_t& trigger_mask) noexcept
{
    trigger_mask = 0U;
    if (!region_has_fanout_member(signal, process)) {
        return false;
    }
    const auto component = region_authoritative_component_by_signal[signal];
    auto& state = *region_authoritative_state_by_component[component];
    const auto member = region_authoritative_member_index_by_process[process];
    if (!state.readiness().ready(member)) {
        return false;
    }
    trigger_mask = state.readiness().trigger_mask(member);
    state.readiness().clear(member);
    if (trigger_mask != 0U && systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_a4_ready_consumptions;
    }
    return trigger_mask != 0U;
}

void Interpreter::Impl::mark_region_value_change(
    const SignalId signal,
    const PackedLogic4& previous,
    const PackedLogic4& current) noexcept
{
    auto* const state = region_authoritative_state_for_signal(signal);
    if (state == nullptr) {
        return;
    }
    if (systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_a4_value_marks;
    }
    state->fanout().mark_transition(signal, previous, current,
        EdgeKind::any, state->readiness());
    if (previous.width() != 1U || current.width() != 1U) {
        return;
    }
    const auto low_logic4 = [](const PackedLogic4& value) {
        return value.is_logic9()
            ? to_logic4(value.get_logic9(0U)) : value.get(0U);
    };
    const auto transition = decode_static_transition(
        low_logic4(previous), low_logic4(current));
    if (transition.posedge) {
        state->fanout().mark_transition(signal, previous, current,
            EdgeKind::posedge, state->readiness());
    }
    if (transition.negedge) {
        state->fanout().mark_transition(signal, previous, current,
            EdgeKind::negedge, state->readiness());
    }
}

void Interpreter::Impl::mirror_region_stored(
    const SignalId signal) noexcept
{
    note_region_authoritative_mirror();
    if (auto* const state = region_authoritative_state_for_signal(signal)) {
        if (state->values().packed_slots_bound()
            && systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_authoritative_slot_writes;
        }
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_stored_mirrors;
        }
        const bool was_valid = state->valid();
        state->values().mirror_stored(signal, driven_values[signal]);
        if (!state->valid()) {
            request_full_region_recertification();
            demote_region_authoritative_slots(
                region_authoritative_component_by_signal[signal], true);
            if (was_valid && systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a4_invalidations;
            }
        }
    }
}

void Interpreter::Impl::mirror_region_visible(
    const SignalId signal) noexcept
{
    note_region_authoritative_mirror();
    if (auto* const state = region_authoritative_state_for_signal(signal)) {
        if (state->values().packed_slots_bound()
            && systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_authoritative_slot_writes;
        }
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_visible_mirrors;
        }
        const bool was_valid = state->valid();
        state->values().mirror_visible(signal,
            signal_last_values[signal], signals[signal].initial_value);
        if (!state->valid()) {
            request_full_region_recertification();
            demote_region_authoritative_slots(
                region_authoritative_component_by_signal[signal], true);
            if (was_valid && systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a4_invalidations;
            }
        }
    }
}

void Interpreter::Impl::mirror_region_owner(
    const SignalId signal,
    const ProcessId process,
    const PackedLogic4& value) noexcept
{
    note_region_authoritative_mirror();
    if (auto* const state = region_authoritative_state_for_signal(signal)) {
        if (state->values().packed_slots_bound()
            && systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_authoritative_slot_writes;
        }
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_owner_mirrors;
        }
        const bool was_valid = state->valid();
        const bool reusable_wide_storage
            = signals[signal].initial_value.width() > 64U
            && state->values().requires_prewrite_unbind()
            && state->values().packed_signal_slots_bound(signal)
            && state->values().packed_owner_slot_bound(signal, process);
        if (reusable_wide_storage) {
            state->values().mirror_owner_into(
                state->wide_mutation_scratch(), signal, process, value,
                signals[signal].initial_value, driven_values[signal]);
        } else {
            state->values().mirror_owner(signal, process, value);
        }
        if (!state->valid()) {
            request_full_region_recertification();
            demote_region_authoritative_slots(
                region_authoritative_component_by_signal[signal], true);
            if (was_valid && systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a4_invalidations;
            }
        }
    }
}

Interpreter::Impl::StaticTransitionMatches
Interpreter::Impl::decode_static_transition(
    const Logic4 old_value,
    const Logic4 new_value) noexcept
{
    return {
        edge_matches(EdgeKind::posedge, old_value, new_value),
        edge_matches(EdgeKind::negedge, old_value, new_value),
    };
}

void Interpreter::Impl::notify_static_value_change(
    const SignalId signal_id,
    const StaticTransitionMatches transition,
    const bool count_native_word_profile,
    const SignalChangeOrigin origin,
    const bool region_value_prepared)
{
    const bool group_static_fanout = fanout_cohort_grouping_enabled;
    const auto visit = next_static_fanout_visit();
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    auto* const region_state
        = region_authoritative_state_for_signal(signal_id);
    // Grouped fanout compares the packed previous/current values below. A
    // deferred direct publication can change a boundary signal without an
    // A4 component state to trigger its usual materialization.
    materialize_direct_signal(signal_id);
    const auto visit_category = [&](const EdgeKind edge) {
        if (region_state != nullptr && !region_value_prepared) {
            region_state->fanout().mark_transition(signal_id,
                signal_last_values[signal_id],
                signals[signal_id].initial_value, edge,
                region_state->readiness());
        }
        if (edge == EdgeKind::any
            && schedule_systemverilog_grouped_fanout(signal_id,
                signal_last_values[signal_id],
                signals[signal_id].initial_value, origin)) {
            return;
        }
        for (const auto entry_index :
             static_fanout_indices_for(signal_id, edge)) {
            const auto& sensitivity = static_fanout_entries[entry_index];
            if (sensitivity.width != 0U) {
                // Native word publication may leave packed storage deferred.
                // Materialize current and previous values before inspecting a
                // retained aggregate range; no stale packed copy is authoritative.
                materialize_direct_signal(signal_id);
                if (!sensitivity_range_changed(signal_last_values[signal_id],
                        signals[signal_id].initial_value,
                        sensitivity.offset, sensitivity.width)) {
                    continue;
                }
            }
            auto& triggered_process = get_process(sensitivity.process);
            std::uint64_t grouped_trigger_mask { };
            if (region_take_ready(signal_id, sensitivity.process,
                    grouped_trigger_mask)) {
                triggered_process.static_trigger_mask
                    |= grouped_trigger_mask;
            } else {
                triggered_process.static_trigger_mask
                    |= sensitivity.static_trigger_mask;
            }
            merge_generic_frontier_ready_mask(sensitivity.process,
                triggered_process.static_trigger_mask);

            if (native_process_count_profile_enabled) {
                if (count_native_word_profile) {
                    ++native_process_word_fanout_matches;
                }
                if (triggered_process.waiting_on_static) {
                    if (count_native_word_profile) {
                        ++native_process_word_fanout_ready;
                        if (sensitivity.process
                            >= native_process_word_fanout_ready_counts.size()) {
                            native_process_word_fanout_ready_counts.resize(
                                static_cast<std::size_t>(sensitivity.process)
                                + 1U);
                            native_process_static_trigger_counts.resize(
                                static_cast<std::size_t>(sensitivity.process)
                                + 1U);
                        }
                    } else if (sensitivity.process
                        >= native_process_static_trigger_counts.size()) {
                        native_process_static_trigger_counts.resize(
                            static_cast<std::size_t>(sensitivity.process)
                            + 1U);
                    }
                    ++native_process_static_trigger_counts[
                        sensitivity.process][signal_id];
                }
            }

            const auto cohort
                = sensitivity.process < static_sensitivity_cohort_by_process.size()
                ? static_sensitivity_cohort_by_process[sensitivity.process]
                : no_cohort;
            if (group_static_fanout && cohort != no_cohort
                && static_sensitivity_cohorts[cohort].members.size() >= 2U) {
                auto& grouped = static_sensitivity_cohorts[cohort];
                bool all_generic_update_members =
                    origin.process_domain == ProcessSchedulingDomain::generic
                    && triggered_process.program().scheduling_domain()
                        == ProcessSchedulingDomain::generic
                    && region_graph.has_value();
                if (all_generic_update_members) {
                    const auto& graph_processes = region_graph->processes();
                    for (const auto member : grouped.members) {
                        if (member >= graph_processes.size()
                            || graph_processes[member].scheduling_domain
                                != ProcessSchedulingDomain::generic
                            || graph_processes[member].update_kind
                                != RegionUpdateKind::generic) {
                            all_generic_update_members = false;
                            break;
                        }
                    }
                }
                if (all_generic_update_members) {
                    // Generic Update members must reach their per-member
                    // region admission before the legacy shared-cohort path.
                    // Queue each triggered member at its original key so its
                    // trigger mask and queued state remain member-specific.
                    if (triggered_process.waiting_on_static) {
                        queue_static_next_delta(sensitivity.process, origin);
                    }
                    continue;
                }
                if (grouped.fanout_visit != visit) {
                    grouped.fanout_visit = visit;
                    if (origin.process_domain
                            == ProcessSchedulingDomain::generic
                        && triggered_process.program().scheduling_domain()
                            == ProcessSchedulingDomain::generic) {
                        queue_static_cohort_next_delta(cohort);
                    } else {
                        for (const auto member : grouped.members) {
                            auto& candidate = get_process(member);
                            if (candidate.waiting_on_static) {
                                queue_static_next_delta(member, origin);
                            }
                        }
                    }
                }
                continue;
            }

            if (triggered_process.waiting_on_static) {
                queue_static_next_delta(sensitivity.process, origin);
            }
        }
    };

    visit_category(EdgeKind::any);
    if (transition.posedge) {
        visit_category(EdgeKind::posedge);
    }
    if (transition.negedge) {
        visit_category(EdgeKind::negedge);
    }
}

void Interpreter::Impl::queue_at(ProcessId id, SimulationTick time)
{
    if (auto* const compact = processes.compact_constant(id)) {
        if (compact->halted || compact->queued) {
            return;
        }
        compact->queued = true;
        const auto phase = compact->execution_phase;
        struct ProcessQueueTask {
            Interpreter::Impl* owner { };
            ProcessId process { };
        };
        const auto task =
            fsim::runtime::detail::make_scheduler_task_descriptor<
                ProcessQueueTask,
                +[](Scheduler&, const ProcessQueueTask& scheduled) {
                    if (auto* state = scheduled.owner->processes
                            .compact_constant(scheduled.process)) {
                        state->queued = false;
                        state->waiting_on_static = false;
                    } else {
                        auto& full_state = scheduled.owner->get_process(
                            scheduled.process);
                        full_state.queued = false;
                        full_state.waiting_on_static = false;
                        scheduled.owner->remove_dynamic_wait(full_state);
                    }
                    scheduled.owner->execute(scheduled.process);
                }>(ProcessQueueTask { this, id });
        if (processes.program_view(id).scheduling_domain()
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_internal_systemverilog_at(
                time, phase, id, task);
        } else {
            scheduler.schedule_internal_at(time, phase, id, task);
        }
        return;
    }
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
    const auto phase = process.execution_phase;
    struct ProcessQueueTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessQueueTask,
            +[](Scheduler&, const ProcessQueueTask& scheduled) {
                if (auto* compact = scheduled.owner->processes
                        .compact_constant(scheduled.process)) {
                    compact->queued = false;
                    compact->waiting_on_static = false;
                } else {
                    auto& state = scheduled.owner->get_process(
                        scheduled.process);
                    state.queued = false;
                    state.waiting_on_static = false;
                    scheduled.owner->remove_dynamic_wait(state);
                }
                scheduled.owner->execute(scheduled.process);
            }>(ProcessQueueTask { this, id });
    if (process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_internal_systemverilog_at(
            time, phase, id, task);
    } else {
        scheduler.schedule_internal_at(time, phase, id, task);
    }
}

void Interpreter::Impl::queue_next_delta(
    const ProcessId id,
    const SignalChangeOrigin origin)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
    const auto phase = process.execution_phase;
    struct ProcessQueueTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessQueueTask,
            +[](Scheduler&, const ProcessQueueTask& scheduled) {
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                state.queued = false;
                state.waiting_on_static = false;
                scheduled.owner->remove_dynamic_wait(state);
                scheduled.owner->execute(scheduled.process);
            }>(ProcessQueueTask { this, id });
    if (processes.scheduling_domain(id)
        == ProcessSchedulingDomain::systemverilog) {
        if (origin.process_domain
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_internal_systemverilog_next_delta(
                phase, id, task);
        } else {
            scheduler.schedule_next_delta(
                SchedulerPhase::active, id,
                [this, id, phase](Scheduler& runtime) {
                    auto& state = get_process(id);
                    bool queued_as_wave { };
                    if (phase == SchedulerPhase::active) {
                        // This callback is already the generic-origin
                        // next-delta boundary. Requeue only the final SV
                        // activation as a receipt-bearing wave entry so an
                        // eligible static member can use the native frontier.
                        state.queued = false;
                        try {
                            queued_as_wave = queue_systemverilog_wave(id);
                        } catch (...) {
                            // Restore local queue state before propagating a
                            // scheduler exception.
                            state.queued = true;
                            throw;
                        }
                    }
                    if (queued_as_wave) {
                        return;
                    }
                    state.queued = true;
                    runtime.schedule_systemverilog(
                        phase, id,
                        [this, id](Scheduler&) {
                            auto& state = get_process(id);
                            state.queued = false;
                            state.waiting_on_static = false;
                            remove_dynamic_wait(state);
                            execute(id);
                        });
                });
        }
        return;
    }
    scheduler.schedule_internal_next_delta(phase, id, task);
}

void Interpreter::Impl::queue_zero_delay_resume(
    ProcessState& process)
{
    if (process.halted || process.queued) {
        return;
    }
    if (process.program().postponed()
        || process.program().scheduling_domain()
            != ProcessSchedulingDomain::systemverilog) {
        queue_next_delta(process.id);
        return;
    }

    process.queued = true;
    struct ProcessResumeTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessResumeTask,
            +[](Scheduler&, const ProcessResumeTask& scheduled) {
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                state.queued = false;
                state.waiting_on_static = false;
                scheduled.owner->remove_dynamic_wait(state);
                scheduled.owner->execute(scheduled.process);
            }>(ProcessResumeTask { this, process.id });
    scheduler.schedule_internal_systemverilog(
        SchedulerPhase::inactive, process.id, task);
}

void Interpreter::Impl::queue_static_next_delta(
    const ProcessId id,
    const SignalChangeOrigin origin)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    const auto scheduling_domain
        = processes.scheduling_origin(id).process_domain;
    if (scheduling_domain == ProcessSchedulingDomain::systemverilog
        && origin.process_domain == ProcessSchedulingDomain::systemverilog
        && queue_systemverilog_wave(id)) {
        return;
    }
    if (scheduling_domain != ProcessSchedulingDomain::generic
        || origin.process_domain
            != ProcessSchedulingDomain::generic) {
        queue_next_delta(id, origin);
        return;
    }
    const bool generic_update = region_graph
        && id < region_graph->processes().size()
        && region_graph->processes()[id].update_kind
            == RegionUpdateKind::generic;
    if (generic_update && queue_generic_projected_region(id)) {
        return;
    }
    if (!generic_update && queue_generic_projected_region(id)) {
        return;
    }
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    const auto cohort = id < static_sensitivity_cohort_by_process.size()
        ? static_sensitivity_cohort_by_process[id]
        : no_cohort;
    if (cohort == no_cohort
        || static_sensitivity_cohorts[cohort].members.size() < 2U) {
        queue_next_delta(id, origin);
        return;
    }
    queue_static_cohort_next_delta(cohort);
}

void Interpreter::Impl::queue_static_cohort_next_delta(
    const std::size_t cohort_id)
{
    auto& cohort = static_sensitivity_cohorts.at(cohort_id);
    if (cohort.pending) {
        return;
    }

    cohort.ready.clear();
    cohort.ready.reserve(cohort.members.size());
    for (const auto member : cohort.members) {
        auto& candidate = get_process(member);
        if (candidate.halted || candidate.queued
            || !candidate.waiting_on_static) {
            continue;
        }
        cohort.ready.push_back(member);
    }
    if (cohort.ready.empty()) {
        return;
    }
    const auto& process = get_process(cohort.ready.front());
    const auto phase = process.execution_phase;
    const auto order = cohort.ready.front();
    const auto snapshot = cohort_snapshots.acquire(cohort.ready);
    const auto execute_one = [this, cohort_id](Scheduler&) {
        execute_queued_static_cohort(cohort_id);
    };
    try {
        if (static_phase_batches_enabled) {
            static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
            if (cohort_id >= generic_projected_region_payload) {
                throw std::overflow_error(
                    "static cohort identifier exceeds scheduler payload");
            }
            scheduler.schedule_next_delta_batchable(
                phase, order, *this,
                static_cast<std::uint64_t>(cohort_id), execute_one);
        } else {
            scheduler.schedule_next_delta(phase, order, execute_one);
        }
    } catch (...) {
        cohort_snapshots.release(snapshot);
        cohort.ready.clear();
        throw;
    }
    for (const auto member : cohort.ready) {
        get_process(member).queued = true;
    }
    cohort.pending = snapshot;
    cohort.ready.clear();
}

void Interpreter::Impl::execute_queued_static_cohort(
    const std::size_t cohort_id)
{
    auto& cohort = static_sensitivity_cohorts.at(cohort_id);
    const auto snapshot = std::exchange(
        cohort.pending, CohortSnapshotPool::Token { });
    if (!cohort_snapshots.consume(snapshot, [this](
            const std::span<const ProcessId> ready) {
            execute_static_cohort(ready);
        })) {
        throw std::logic_error {
            "scheduler batch references an empty static cohort"
        };
    }
}

std::uint64_t Interpreter::Impl::next_static_fanout_visit()
{
    ++static_fanout_visit_generation;
    if (static_fanout_visit_generation != 0U) {
        return static_fanout_visit_generation;
    }
    static_fanout_visit_generation = 1U;
    for (auto& cohort : static_sensitivity_cohorts) {
        cohort.fanout_visit = 0U;
    }
    return static_fanout_visit_generation;
}

void Interpreter::Impl::queue_current(ProcessId id)
{
    if (auto* const compact = processes.compact_constant(id)) {
        if (compact->halted || compact->queued) {
            return;
        }
        compact->queued = true;
        const auto phase
            = scheduler.current_phase().value_or(SchedulerPhase::active);
        struct ProcessQueueTask {
            Interpreter::Impl* owner { };
            ProcessId process { };
        };
        const auto task =
            fsim::runtime::detail::make_scheduler_task_descriptor<
                ProcessQueueTask,
                +[](Scheduler&, const ProcessQueueTask& scheduled) {
                    if (auto* state = scheduled.owner->processes
                            .compact_constant(scheduled.process)) {
                        state->queued = false;
                    } else {
                        scheduled.owner->get_process(
                            scheduled.process).queued = false;
                    }
                    scheduled.owner->execute(scheduled.process);
                }>(ProcessQueueTask { this, id });
        if (processes.program_view(id).scheduling_domain()
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_internal_systemverilog(phase, id, task);
        } else {
            scheduler.schedule_internal(phase, id, task);
        }
        return;
    }
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
    const auto phase = scheduler.current_phase().value_or(SchedulerPhase::active);
    struct ProcessQueueTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessQueueTask,
            +[](Scheduler&, const ProcessQueueTask& scheduled) {
                if (auto* compact = scheduled.owner->processes
                        .compact_constant(scheduled.process)) {
                    compact->queued = false;
                } else {
                    scheduled.owner->get_process(
                        scheduled.process).queued = false;
                }
                scheduled.owner->execute(scheduled.process);
            }>(ProcessQueueTask { this, id });
    if (process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_internal_systemverilog(phase, id, task);
    } else {
        scheduler.schedule_internal(phase, id, task);
    }
}

void Interpreter::Impl::queue_active_current(ProcessId id)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
    const auto phase = process.execution_phase;
    struct ProcessQueueTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessQueueTask,
            +[](Scheduler&, const ProcessQueueTask& scheduled) {
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                state.queued = false;
                state.waiting_on_static = false;
                scheduled.owner->remove_dynamic_wait(state);
                scheduled.owner->execute(scheduled.process);
            }>(ProcessQueueTask { this, id });
    if (process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_internal_systemverilog(phase, id, task);
    } else {
        scheduler.schedule_internal(phase, id, task);
    }
}

void Interpreter::Impl::queue_static_active_current(const ProcessId id)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    if (process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        if (!queue_systemverilog_wave(id)) {
            queue_active_current(id);
        }
        return;
    }
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    const auto cohort = id < static_sensitivity_cohort_by_process.size()
        ? static_sensitivity_cohort_by_process[id]
        : no_cohort;
    if (cohort == no_cohort
        || static_sensitivity_cohorts[cohort].members.size() < 2U) {
        queue_active_current(id);
        return;
    }

    active_cohort_ready.clear();
    active_cohort_ready.reserve(
        static_sensitivity_cohorts[cohort].members.size());
    for (const auto member : static_sensitivity_cohorts[cohort].members) {
        auto& candidate = get_process(member);
        if (candidate.halted || candidate.queued
            || !candidate.waiting_on_static) {
            continue;
        }
        active_cohort_ready.push_back(member);
    }
    if (active_cohort_ready.empty()) {
        return;
    }
    const auto phase = process.execution_phase;
    const auto order = active_cohort_ready.front();
    const auto snapshot = cohort_snapshots.acquire(active_cohort_ready);
    try {
        scheduler.schedule(phase, order, [this, snapshot](Scheduler&) {
            if (!cohort_snapshots.consume(snapshot, [this](
                    const std::span<const ProcessId> ready) {
                    execute_static_cohort(ready);
                })) {
                throw std::logic_error {
                    "scheduler references an empty static cohort"
                };
            }
        });
    } catch (...) {
        cohort_snapshots.release(snapshot);
        active_cohort_ready.clear();
        throw;
    }
    for (const auto member : active_cohort_ready) {
        get_process(member).queued = true;
    }
    active_cohort_ready.clear();
}

void Interpreter::Impl::set_event_identity(
    const SignalId signal,
    const std::optional<SignalId> identity)
{
    if (!get_signal(signal).event_variable) {
        throw std::logic_error {
            "named-event identity can only be assigned to event variables"
        };
    }
    if (identity && !get_signal(*identity).event_variable) {
        throw std::logic_error {
            "named-event identity must reference an event variable"
        };
    }
    auto& current_identity = event_identities.at(signal);
    const auto previous = current_identity;
    if (previous == identity) {
        return;
    }
    if (previous) {
        const auto& previous_members = event_identity_members.at(*previous);
        const auto member = std::lower_bound(
            previous_members.begin(), previous_members.end(), signal);
        if (member == previous_members.end() || *member != signal) {
            throw std::logic_error {
                "named-event identity index is missing its current member"
            };
        }
    }
    if (identity) {
        auto& next_members = event_identity_members.at(*identity);
        const auto member = std::lower_bound(
            next_members.begin(), next_members.end(), signal);
        if (member == next_members.end() || *member != signal) {
            next_members.insert(member, signal);
        }
    }
    if (previous) {
        auto& previous_members = event_identity_members.at(*previous);
        const auto member = std::lower_bound(
            previous_members.begin(), previous_members.end(), signal);
        previous_members.erase(member);
    }
    current_identity = identity;
}

void Interpreter::Impl::trigger_event(
    const SignalId event,
    const SignalChangeOrigin origin)
{
    const auto& source = get_signal(event);
    if (event_trigger_hook) {
        event_trigger_hook(event, scheduler.now());
    }
    const auto update_member = [&](const SignalId member) {
        record_signal_event(member, scheduler.delta(), origin);
        for (const auto& sensitivity : static_fanout_for(member)) {
            auto& process = get_process(sensitivity.process);
            process.static_trigger_mask |= sensitivity.static_trigger_mask;
            merge_systemverilog_readiness_mask(
                sensitivity.process, sensitivity.static_trigger_mask);
            merge_generic_frontier_ready_mask(sensitivity.process,
                process.static_trigger_mask);
            if (process.waiting_on_static) {
                if (process.program().scheduling_domain()
                    == origin.process_domain) {
                    queue_static_active_current(sensitivity.process);
                } else {
                    queue_static_next_delta(sensitivity.process, origin);
                }
            }
        }
    };
    if (source.event_variable) {
        for (const auto member : event_identity_members.at(event)) {
            update_member(member);
        }
    } else {
        update_member(event);
    }
    // Copy because queue_active_current removes dynamic registrations.
    const auto dynamic = dynamic_fanout[event];
    for (const auto& registration : dynamic) {
        if (registration.process >= processes.size()) {
            continue;
        }
        auto& process = get_process(registration.process);
        if (dynamic_wait_satisfied(
                process, event, registration)) {
            mark_dynamic_event_resume(process);
            if (process.program().scheduling_domain()
                == origin.process_domain) {
                queue_active_current(registration.process);
            } else {
                queue_next_delta(registration.process, origin);
            }
        }
    }
}

void Interpreter::Impl::stamp_signal_event(
    const SignalId signal,
    const std::uint64_t generic_delta,
    const SignalChangeOrigin origin)
{
    signal_events.at(signal)
        = std::pair { scheduler.now(), generic_delta };
    const auto systemverilog_round
        = origin.process_domain == ProcessSchedulingDomain::systemverilog
        ? scheduler.systemverilog_round()
        : std::uint64_t { };
    signal_event_scheduling_stamps.at(signal)
        = SignalEventSchedulingStamp { origin, systemverilog_round };
}

void Interpreter::Impl::record_signal_event(
    const SignalId signal,
    const std::uint64_t generic_delta,
    const SignalChangeOrigin origin)
{
    stamp_signal_event(signal, generic_delta, origin);
    capture_sampled_history_clock(signal, generic_delta, origin);
}

[[nodiscard]] std::uint64_t Interpreter::Impl::invalidate_event(
    const SignalId event)
{
    (void)get_signal(event);
    auto& state = event_states[event];
    if (state.generation
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error {
            "event notification generation overflow"
        };
    }
    state.kind = PendingEventKind::none;
    state.due = 0;
    state.origin = { };
    return ++state.generation;
}

void Interpreter::Impl::cancel_event(const SignalId event)
{
    const auto& signal = get_signal(event);
    const auto identity = signal.event_variable
        ? event_identities.at(event)
        : std::optional<SignalId> { event };
    if (identity) {
        (void)invalidate_event(*identity);
    }
}

void Interpreter::Impl::notify_event(
    const SignalId event,
    const SimulationTick delay,
    const EventNotificationKind kind,
    const StableOrder order,
    const SignalChangeOrigin origin)
{
    const auto& signal = get_signal(event);
    const auto identity = signal.event_variable
        ? event_identities.at(event)
        : std::optional<SignalId> { event };
    if (!identity) {
        return;
    }
    const auto notified_event = *identity;
    auto& state = event_states[notified_event];
    auto effective_kind = kind;

    if (kind == EventNotificationKind::delayed) {
        if (state.kind != PendingEventKind::none) {
            throw std::logic_error {
                "notify_delayed requires an event with no pending notification"
            };
        }
        effective_kind = delay == 0
            ? EventNotificationKind::delta
            : EventNotificationKind::timed;
    }

    if (effective_kind == EventNotificationKind::immediate) {
        if (delay != 0) {
            throw std::invalid_argument {
                "immediate event notification cannot have a delay"
            };
        }
        (void)invalidate_event(notified_event);
        trigger_event(notified_event, origin);
        return;
    }

    if (effective_kind == EventNotificationKind::delta) {
        if (delay != 0) {
            throw std::invalid_argument {
                "delta event notification cannot have a delay"
            };
        }
        if (state.kind == PendingEventKind::delta) {
            return;
        }
        const auto generation = invalidate_event(notified_event);
        state.kind = PendingEventKind::delta;
        state.due = scheduler.now();
        state.origin = origin;
        struct PendingEventTask {
            Interpreter::Impl* owner { };
            SignalId event { };
            std::uint64_t generation { };
        };
        const auto task =
            fsim::runtime::detail::make_scheduler_task_descriptor<
                PendingEventTask,
                +[](Scheduler&, const PendingEventTask& scheduled) {
                    auto& pending
                        = scheduled.owner->event_states[scheduled.event];
                    if (pending.generation != scheduled.generation
                        || pending.kind
                            != PendingEventKind::delta) {
                        return;
                    }
                    pending.kind = PendingEventKind::none;
                    pending.due = 0;
                    const auto origin = pending.origin;
                    pending.origin = { };
                    scheduled.owner->trigger_event(
                        scheduled.event, origin);
                }>(PendingEventTask {
                    this, notified_event, generation });
        if (origin.process_domain
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_internal_systemverilog_next_delta(
                origin.phase, order, task);
        } else {
            scheduler.schedule_internal_next_delta(
                SchedulerPhase::active, order, task);
        }
        return;
    }

    if (effective_kind != EventNotificationKind::timed || delay == 0) {
        throw std::invalid_argument {
            "timed event notification requires a non-zero delay"
        };
    }
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - scheduler.now()) {
        throw std::overflow_error {
            "simulation time overflow while scheduling event notification"
        };
    }
    const auto due = scheduler.now() + delay;
    if (state.kind == PendingEventKind::delta
        || (state.kind == PendingEventKind::timed
            && state.due <= due)) {
        return;
    }
    const auto generation = invalidate_event(notified_event);
    state.kind = PendingEventKind::timed;
    state.due = due;
    state.origin = origin;
    struct PendingEventTask {
        Interpreter::Impl* owner { };
        SignalId event { };
        std::uint64_t generation { };
        SimulationTick due { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            PendingEventTask,
            +[](Scheduler&, const PendingEventTask& scheduled) {
                auto& pending = scheduled.owner->event_states[
                    scheduled.event];
                if (pending.generation != scheduled.generation
                    || pending.kind != PendingEventKind::timed
                    || pending.due != scheduled.due) {
                    return;
                }
                pending.kind = PendingEventKind::none;
                pending.due = 0;
                const auto origin = pending.origin;
                pending.origin = { };
                scheduled.owner->trigger_event(
                    scheduled.event, origin);
            }>(PendingEventTask {
                this, notified_event, generation, due });
    if (origin.process_domain
        == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_internal_systemverilog_at(
            due, origin.phase, order, task);
    } else {
        scheduler.schedule_internal_at(
            due, SchedulerPhase::active, order, task);
    }
}

void Interpreter::Impl::notify_execution_point(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex instruction,
    const ExecutionPointKind kind,
    const SourceLocation& source,
    const std::string_view scope)
{
    notify_execution_point(
        process.id, process.cold().design_process, process.program(),
        instruction, kind, source,
        scope.empty() ? std::string_view { process.cold().current_scope }
                      : scope);
}

void Interpreter::Impl::notify_execution_point(
    const ProcessId process,
    const ProcessId design_process,
    const ProcessProgramView program,
    const InstructionIndex instruction,
    const ExecutionPointKind kind,
    const SourceLocation& source,
    const std::string_view scope)
{
    if (execution_point_hook) {
        execution_point_hook(
            scheduler,
            ExecutionPoint {
                process, design_process, instruction, kind, source,
                std::string { scope }, program.language_standard(),
                program.compatibility_profile() });
    }
}

[[nodiscard]] bool Interpreter::Impl::monitor_watches(
    const SignalId signal) const
{
    if (!monitor) {
        return false;
    }
    if (monitor_signal_watches_unknown
        || monitor_signal_watch_mask.size() != signals.size()
        || signal >= monitor_signal_watch_mask.size()) {
        return true;
    }
    return monitor_signal_watch_mask[signal] != 0U;
}

[[nodiscard]] std::string Interpreter::Impl::render_monitor(
    const MonitorInstall& registration) const
{
    std::string text;
    for (const auto& value : registration.values) {
        if (value.kind == MonitorValueKind::time) {
            text += make_time_output(
                value.prefix,
                { },
                scheduler.now(),
                time_format,
                value.use_timeformat_width,
                value.minimum_width,
                value.left_justify,
                value.zero_pad);
        } else if (value.kind == MonitorValueKind::simulation_time) {
            const auto now = systemverilog_time_function(
                value.time_function,
                scheduler.now(),
                { value.time_unit_femtoseconds,
                    value.time_precision_femtoseconds,
                    time_format.resolution_femtoseconds });
            const auto encoded = !now
                ? SystemVerilogPackedScalarResult {
                      PackedLogic4 { }, now.error }
                : value.time_function == SystemVerilogTimeFunction::Realtime
                ? encode_systemverilog_scalar_payload(now.value)
                : systemverilog_scalar_to_packed(now.value,
                      value.time_function == SystemVerilogTimeFunction::Stime
                          ? 32U
                          : 64U,
                      false);
            text += make_formatted_output(
                value.prefix,
                { },
                value.format,
                encoded ? encoded.value
                        : PackedLogic4 { 64U, Logic4::x },
                value.signed_decimal,
                value.suppress_leading_zero,
                value.minimum_width,
                value.left_justify,
                value.zero_pad,
                value.scalar_kind);
        } else {
            text += make_formatted_output(
                value.prefix,
                { },
                value.format,
                get_signal(value.signal).initial_value,
                value.signed_decimal,
                value.suppress_leading_zero,
                value.minimum_width,
                value.left_justify,
                value.zero_pad,
                value.scalar_kind);
        }
    }
    text += registration.trailing_text;
    return text;
}

void Interpreter::Impl::schedule_monitor_publication()
{
    if (!monitor || !monitor_enabled) {
        return;
    }
    const auto generation = monitor_generation;
    const auto process = monitor_process;
    if (processes.program_view(process).scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        const auto publication = scheduler.now();
        if (monitor_systemverilog_publication == publication) {
            return;
        }
        monitor_systemverilog_publication = publication;
        scheduler.schedule_end_of_time_slot(
            process,
            [this, generation, process](Scheduler& runtime) {
                if (generation != monitor_generation
                    || !monitor_enabled || !monitor) {
                    return;
                }
                monitor_systemverilog_publication.reset();
                if (monitor_file_handle) {
                    write_file(
                        process,
                        *monitor_file_handle,
                        render_monitor(*monitor),
                        monitor->newline);
                } else if (output_hook) {
                    output_hook(
                        process,
                        render_monitor(*monitor),
                        monitor->newline,
                        runtime.now(),
                        runtime.delta());
                }
            });
        return;
    }
    const auto publication = std::pair { scheduler.now(), scheduler.delta() };
    if (monitor_publication == publication) {
        return;
    }
    monitor_publication = publication;
    scheduler.schedule(
        SchedulerPhase::postponed,
        process,
        [this, generation, process](Scheduler& runtime) {
            if (generation != monitor_generation
                || !monitor_enabled || !monitor) {
                return;
            }
            monitor_publication.reset();
            if (monitor_file_handle) {
                write_file(
                    process,
                    *monitor_file_handle,
                    render_monitor(*monitor),
                    monitor->newline);
            } else if (output_hook) {
                output_hook(
                    process,
                    render_monitor(*monitor),
                    monitor->newline,
                    runtime.now(),
                    runtime.delta());
            }
        });
}

void Interpreter::Impl::install_monitor(
    const ProcessId process,
    const MonitorInstall& registration)
{
    const auto file_handle = registration.file_handle
        ? std::optional<FileHandle> { known_file_handle(
              get_process(process), *registration.file_handle) }
        : std::nullopt;
    if (registration.one_shot) {
        auto publish = [this, process, registration, file_handle](
                           Scheduler& runtime) {
                if (file_handle) {
                    write_file(
                        process,
                        *file_handle,
                        render_monitor(registration),
                        registration.newline);
                } else if (output_hook) {
                    output_hook(
                        process,
                        render_monitor(registration),
                        registration.newline,
                        runtime.now(),
                        runtime.delta());
                }
            };
        if (processes.program_view(process).scheduling_domain()
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_end_of_time_slot(process, std::move(publish));
        } else {
            scheduler.schedule(
                SchedulerPhase::postponed, process, std::move(publish));
        }
        return;
    }
    if (monitor_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "monitor generation overflow" };
    }
    ++monitor_generation;
    monitor = registration;
    monitor_signal_watches_unknown
        = monitor_signal_watch_mask.size() != signals.size();
    std::ranges::fill(monitor_signal_watch_mask, 0U);
    for (const auto& value : registration.values) {
        if (value.kind != MonitorValueKind::signal) {
            continue;
        }
        if (value.signal >= monitor_signal_watch_mask.size()) {
            monitor_signal_watches_unknown = true;
            continue;
        }
        monitor_signal_watch_mask[value.signal] = 1U;
    }
    monitor_process = process;
    monitor_file_handle = file_handle;
    monitor_enabled = true;
    monitor_publication.reset();
    monitor_systemverilog_publication.reset();
    schedule_monitor_publication();
}

void Interpreter::Impl::set_monitor_enabled(const bool enabled)
{
    if (monitor_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "monitor generation overflow" };
    }
    ++monitor_generation;
    monitor_enabled = enabled;
    monitor_publication.reset();
    monitor_systemverilog_publication.reset();
    if (enabled) {
        schedule_monitor_publication();
    }
}

void Interpreter::Impl::set_time_format(
    ProcessState& process,
    const TimeFormatControl& operation)
{
    const auto decode = [&](const RegisterId id, const char* name) {
        const auto& value = get_register(process, id);
        const auto word = value.low_word();
        if (value.width() != 32 || word.bval != 0) {
            fail(
                process,
                std::string { "$timeformat " } + name
                    + " must be a known 32-bit value");
        }
        return static_cast<std::uint32_t>(word.aval);
    };
    const auto units = static_cast<std::int32_t>(
        decode(operation.units, "units"));
    const auto precision = decode(operation.precision, "precision");
    const auto minimum_width = decode(
        operation.minimum_width, "minimum width");
    const auto& suffix = get_string_register(process, operation.suffix);
    if (units < -15 || units > 0
        || precision > maximum_string_bytes
        || minimum_width > maximum_string_bytes
        || suffix.size() > maximum_string_bytes) {
        fail(
            process,
            "$timeformat arguments exceed their supported IEEE profile");
    }
    time_format.units = units;
    time_format.precision = precision;
    time_format.suffix = suffix;
    time_format.minimum_width = minimum_width;
}

[[nodiscard]] std::uint64_t Interpreter::Impl::initial_random_state(
    const std::uint64_t seed,
    const ProcessId process) noexcept
{
    auto value = seed
        + UINT64_C(0x9e3779b97f4a7c15)
            * (static_cast<std::uint64_t>(process) + 1U);
    value = (value ^ (value >> 30U))
        * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U))
        * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

[[nodiscard]] std::uint32_t Interpreter::Impl::next_random(
    Interpreter::Impl::ProcessState& process) noexcept
{
    auto value = (process.cold().random_state += UINT64_C(0x9e3779b97f4a7c15));
    value = (value ^ (value >> 30U))
        * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U))
        * UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return static_cast<std::uint32_t>(value >> 32U);
}

[[nodiscard]] std::optional<std::uint32_t>
Interpreter::Impl::known_random_bound(const PackedLogic4& value)
{
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint32_t result { };
    const auto width = std::min<std::size_t>(32U, value.width());
    for (std::size_t bit = 0; bit < width; ++bit) {
        const auto state = value.get(bit);
        if (state == Logic4::x || state == Logic4::z) {
            return std::nullopt;
        }
        if (state == Logic4::one) {
            result |= UINT32_C(1) << bit;
        }
    }
    return result;
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::random_value(
    const ProcessId process_id,
    const RandomKind kind,
    const std::optional<PackedLogic4>& maximum,
    const std::optional<PackedLogic4>& minimum)
{
    auto& process = get_process(process_id);
    if (kind != RandomKind::urandom_range) {
        return PackedLogic4::from_aval_bval(
            32, next_random(process), 0);
    }
    if (!maximum) {
        throw std::logic_error {
            "$urandom_range operation has no maximum"
        };
    }
    const auto known_maximum = known_random_bound(*maximum);
    const auto known_minimum = minimum
        ? known_random_bound(*minimum)
        : std::optional<std::uint32_t> { 0U };
    if (!known_maximum || !known_minimum) {
        return PackedLogic4(32, Logic4::x);
    }
    auto low = *known_minimum;
    auto high = *known_maximum;
    if (high < low) {
        std::swap(low, high);
    }
    const auto span = static_cast<std::uint64_t>(high)
        - static_cast<std::uint64_t>(low) + 1U;
    std::uint32_t sample { };
    if (span == (UINT64_C(1) << 32U)) {
        sample = next_random(process);
    } else {
        const auto full_range = UINT64_C(1) << 32U;
        const auto accepted = full_range - full_range % span;
        do {
            sample = next_random(process);
        } while (static_cast<std::uint64_t>(sample) >= accepted);
        sample = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(low)
            + static_cast<std::uint64_t>(sample) % span);
    }
    return PackedLogic4::from_aval_bval(32, sample, 0);
}

void Interpreter::Impl::publish(
    SignalId signal_id, PackedLogic4 value,
    const bool notify_fanout)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    auto& signal = get_signal(signal_id);
    if (signal.initial_value.width() != value.width()) {
        throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    value = normalize_signal_value(signal_id, std::move(value));
    publish_normalized(signal_id, std::move(value), notify_fanout);
}

void Interpreter::Impl::publish_normalized(
    const SignalId signal_id,
    PackedLogic4 value,
    const bool notify_fanout,
    const SignalChangeOrigin origin)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    prepare_region_authoritative_write(signal_id);
    const std::array<SignalId, 1U> changed_signals { signal_id };
    auto reference_refresh
        = prepare_container_value_reference_refresh(changed_signals);
    ActiveContainerReferenceRefresh active_refresh;
    begin_container_value_reference_refresh(
        active_refresh, changed_signals, reference_refresh);
    const auto finish_refresh = [this](
                                    ActiveContainerReferenceRefresh* frame) noexcept {
        end_container_value_reference_refresh(*frame);
    };
    const std::unique_ptr<ActiveContainerReferenceRefresh,
        decltype(finish_refresh)> refresh_scope(
        &active_refresh, finish_refresh);
    note_signal_transaction(signal_id, notify_fanout, origin);
    auto& signal = signals[signal_id];
    if (signal.initial_value == value) {
        synchronize_container_value_references(changed_signals, reference_refresh);
        return;
    }
    signal_last_values[signal_id] = std::move(signal.initial_value);
    signal.initial_value = std::move(value);
    mirror_region_visible(signal_id);
    refresh_direct_signal_planes(signal_id);
    synchronize_container_value_references(changed_signals, reference_refresh);
    publish_value_change(signal_id, notify_fanout, origin);
}

void Interpreter::Impl::publish_normalized_word(
    const SignalId signal_id,
    const Logic4Word value,
    const bool notify_fanout,
    const SignalChangeOrigin origin)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    prepare_region_authoritative_write(signal_id);
    const std::array<SignalId, 1U> changed_signals { signal_id };
    auto reference_refresh
        = prepare_container_value_reference_refresh(changed_signals);
    ActiveContainerReferenceRefresh active_refresh;
    begin_container_value_reference_refresh(
        active_refresh, changed_signals, reference_refresh);
    const auto finish_refresh = [this](
                                    ActiveContainerReferenceRefresh* frame) noexcept {
        end_container_value_reference_refresh(*frame);
    };
    const std::unique_ptr<ActiveContainerReferenceRefresh,
        decltype(finish_refresh)> refresh_scope(
        &active_refresh, finish_refresh);
    note_signal_transaction(signal_id, notify_fanout, origin);
    auto& signal = signals[signal_id];
    const auto width = signal.initial_value.width();
    const auto current = signal.initial_value.unchecked_low_word();
    if (current == value) {
        synchronize_container_value_references(changed_signals, reference_refresh);
        return;
    }
    if (native_process_count_profile_enabled) {
        ++native_process_word_changes;
    }
    signal_last_values[signal_id].assign_word(current);
    signal.initial_value.assign_word(value);
    if (auto* const state
        = region_authoritative_state_for_signal(signal_id)) {
        if (state->values().packed_slots_bound()
            && systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_authoritative_slot_writes;
        }
        state->values().mirror_logic4_word(signal_id,
            Logic4Word { width, current.aval, current.bval },
            value,
            driven_values[signal_id].unchecked_low_word());
        if (!state->valid()) {
            request_full_region_recertification();
            demote_region_authoritative_slots(
                region_authoritative_component_by_signal[signal_id], true);
        }
    }
    const auto encoded = signal.initial_value.unchecked_low_word();
    direct_signal_aval[signal_id] = encoded.aval;
    direct_signal_bval[signal_id] = encoded.bval;
    synchronize_container_value_references(changed_signals, reference_refresh);
    publish_value_change(signal_id, notify_fanout, origin);
}

bool Interpreter::Impl::can_publish_native_word(
    const SignalId signal_id,
    const ProcessId process) noexcept
{
    if (native_signal_observation_required_hook) {
        if (native_signal_observation_required_hook(signal_id)) {
            if (native_phase_profile_enabled) {
                ++native_phase_profile_rejected_observer;
            }
            return false;
        }
    } else if (signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook
        || container_element_change_hook) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_observer;
        }
        return false;
    }
    return can_publish_native_word_prevalidated(signal_id, process);
}

bool Interpreter::Impl::native_word_publication_phase_eligible() noexcept
{
    if (native_signal_observation_any_hook) {
        return !native_signal_observation_any_hook();
    }
    if (native_signal_observation_required_hook) {
        return false;
    }
    return !signal_change_hook && !stored_signal_change_hook
        && !driver_change_hook && !scalar_signal_change_hook
        && !container_object_change_hook
        && !container_element_change_hook;
}

bool Interpreter::Impl::native_signal_has_runtime_dependency(
    const SignalId signal_id,
    const bool region_graph_has_exact_sensitivity_ranges) const noexcept
{
    if (signal_id >= signals.size()
        || signals[signal_id].public_value_reference_exposed
        || signal_id >= signal_container_element_aliases.size()
        || signal_id >= signal_container_aggregate_aliases.size()
        || native_signal_dependencies_unknown
        || native_signal_dependency_mask.size() != signals.size()) {
        return true;
    }
    // Element aliases publish logical-container changes and may feed dynamic
    // array readers through the ContainerObject interface. Aggregate proxies
    // are callback-backed views of those leaves. Native word and fused
    // publication routes do not prove either bridge, so keep both on the
    // ordinary checked publication path.
    if (signal_container_element_aliases[signal_id]
        || signal_container_aggregate_aliases[signal_id]) {
        return true;
    }
    return native_signal_has_non_alias_runtime_dependency(signal_id,
        region_graph_has_exact_sensitivity_ranges);
}

bool Interpreter::Impl::native_signal_has_non_alias_runtime_dependency(
    const SignalId signal_id,
    const bool region_graph_has_exact_sensitivity_ranges) const noexcept
{
    if (signal_id >= signals.size()
        || signals[signal_id].public_value_reference_exposed
        || native_signal_dependencies_unknown
        || native_signal_dependency_mask.size() != signals.size()
        || (region_graph_has_exact_sensitivity_ranges
            && native_signal_non_range_dependency_mask.size()
                != signals.size())) {
        return true;
    }
    if (requires_sampled_values) {
        if (sampled_value_dependencies_unknown
            || sampled_value_dependency_mask.size() != signals.size()) {
            return true;
        }
        if (sampled_value_dependency_mask[signal_id] != 0U) {
            return true;
        }
    }
    if (has_bidirectional_switches) {
        if (signal_id >= switch_endpoint_adjacency.size()
            || signal_id >= switch_control_adjacency.size()) {
            return true;
        }
        if (!switch_endpoint_adjacency[signal_id].empty()
            || !switch_control_adjacency[signal_id].empty()) {
            return true;
        }
    }
    if (monitor_watches(signal_id)) {
        return true;
    }
    return region_graph_has_exact_sensitivity_ranges
        ? native_signal_non_range_dependency_mask[signal_id] != 0U
        : native_signal_dependency_mask[signal_id] != 0U;
}

void Interpreter::Impl::build_native_signal_dependency_masks() noexcept
{
    native_signal_dependencies_unknown = false;
    native_signal_dependency_mask.clear();
    native_signal_non_range_dependency_mask.clear();
    module_path_destination_mask.clear();
    try {
        native_signal_dependency_mask.assign(signals.size(), 0U);
        native_signal_non_range_dependency_mask.assign(signals.size(), 0U);
        module_path_destination_mask.assign(signals.size(), 0U);
    } catch (...) {
        native_signal_dependency_mask.clear();
        native_signal_non_range_dependency_mask.clear();
        module_path_destination_mask.clear();
        native_signal_dependencies_unknown = true;
        return;
    }

    const auto mark_signal = [&](const SignalId signal,
                                 const bool non_range_dependency = true) {
        if (signal >= native_signal_dependency_mask.size()) {
            native_signal_dependencies_unknown = true;
            return;
        }
        native_signal_dependency_mask[signal] = 1U;
        if (non_range_dependency) {
            native_signal_non_range_dependency_mask[signal] = 1U;
        }
    };
    const auto mark_terminal = [&](const ModulePathTerminal& terminal) {
        mark_signal(terminal.signal);
    };
    const auto mark_expression = [&](const ModulePathExpression& expression) {
        if (expression.empty()) {
            return;
        }
        if (expression.root >= expression.nodes.size()) {
            native_signal_dependencies_unknown = true;
            return;
        }
        for (const auto& node : expression.nodes) {
            switch (node.operation) {
            case ModulePathExpressionOperator::constant:
            case ModulePathExpressionOperator::bit_not:
            case ModulePathExpressionOperator::logical_not:
            case ModulePathExpressionOperator::reduction:
            case ModulePathExpressionOperator::binary:
            case ModulePathExpressionOperator::logical_binary:
            case ModulePathExpressionOperator::shift:
            case ModulePathExpressionOperator::conditional:
            case ModulePathExpressionOperator::concatenate:
                break;
            case ModulePathExpressionOperator::terminal:
                mark_terminal(node.terminal);
                break;
            default:
                native_signal_dependencies_unknown = true;
                break;
            }
        }
    };
    const auto mark_event = [&](const ModuleTimingEvent& event) {
        mark_terminal(event.terminal);
        mark_expression(event.condition);
    };

    // Legacy native publication/cone routes lack range-aware readiness proofs.
    // Keep these signals on checked publication until RegionGraph certifies them.
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        // Inert static kernel members have no static sensitivity.
        if (static_kernel && id < fusion_dormant_process.size()
            && fusion_dormant_process[id] == 1U) {
            continue;
        }
        const auto program = processes.program_view(id);
        for (const auto& sensitivity : program.static_sensitivity()) {
            if (sensitivity.width != 0U)
                mark_signal(sensitivity.signal, false);
        }
    }

    for (const auto& path : module_paths) {
        for (const auto& source : path.sources) {
            mark_terminal(source);
        }
        for (const auto& destination : path.destinations) {
            mark_terminal(destination);
            if (destination.signal >= module_path_destination_mask.size()) {
                native_signal_dependencies_unknown = true;
            } else {
                module_path_destination_mask[destination.signal] = 1U;
            }
        }
        mark_expression(path.condition);
        mark_expression(path.data_source);
    }

    if (module_timing_check_states.size() != module_timing_checks.size()) {
        native_signal_dependencies_unknown = true;
    }
    for (const auto& check : module_timing_checks) {
        mark_event(check.reference);
        if (check.data) {
            mark_event(*check.data);
        }
        if (check.notifier) {
            mark_signal(*check.notifier);
        }
        if (check.delayed_reference) {
            mark_terminal(*check.delayed_reference);
        }
        if (check.delayed_data) {
            mark_terminal(*check.delayed_data);
        }
        mark_expression(check.timestamp_condition);
        mark_expression(check.timecheck_condition);
    }
}

void Interpreter::Impl::build_native_signal_publication_shape_certificate()
    noexcept
{
    native_signal_publication_shape_certificate.clear();
    if (native_signal_dependencies_unknown
        || native_signal_dependency_mask.size() != signals.size()
        || (requires_sampled_values
            && (sampled_value_dependencies_unknown
                || sampled_value_dependency_mask.size() != signals.size()))
        || signal_container_aliases.size() != signals.size()
        || signal_container_element_aliases.size() != signals.size()
        || signal_container_aggregate_aliases.size() != signals.size()
        || (has_bidirectional_switches
            && (switch_endpoint_adjacency.size() != signals.size()
                || switch_control_adjacency.size() != signals.size()))) {
        return;
    }

    try {
        std::vector<std::uint8_t> prepared(signals.size(), 0U);
        for (std::size_t index = 0U; index < signals.size(); ++index) {
            if (native_signal_dependency_mask[index] != 0U
                || (requires_sampled_values
                    && sampled_value_dependency_mask[index] != 0U)
                || !signal_container_aliases[index].empty()
                || signal_container_element_aliases[index]
                || signal_container_aggregate_aliases[index]) {
                continue;
            }
            if (has_bidirectional_switches
                && (!switch_endpoint_adjacency[index].empty()
                    || !switch_control_adjacency[index].empty())) {
                continue;
            }
            prepared[index] = 1U;
        }
        native_signal_publication_shape_certificate.swap(prepared);
    } catch (...) {
        // The certificate only permits an optimization. Allocation or
        // topology-shape failures leave every signal on checked publication.
        native_signal_publication_shape_certificate.clear();
    }
}

bool Interpreter::Impl::can_publish_native_word_prevalidated(
    const SignalId signal_id,
    const ProcessId process) noexcept
{
    const bool shape_certified
        = signal_id < signals.size()
        && signal_id < native_signal_publication_shape_certificate.size()
        && native_signal_publication_shape_certificate[signal_id] != 0U;
    if (native_phase_profile_enabled) {
        if (shape_certified) {
            ++native_phase_profile_shape_certificate_hits;
        } else {
            ++native_phase_profile_shape_certificate_misses;
        }
    }
    if (!shape_certified
        || signal_id >= direct_single_driver_routes.size()
        || signal_id >= dynamic_fanout.size()
        || signal_id >= signal_container_aliases.size()
        || signals[signal_id].public_value_reference_exposed
        || monitor_watches(signal_id)
        || has_dynamic_waits(signal_id)) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_structure;
        }
        return false;
    }
    const auto& signal = signals[signal_id];
    const auto& route = direct_single_driver_routes[signal_id];
    if (!route.active || route.process != process
        || direct_single_driver_record(signal_id) == nullptr) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_route;
        }
        return false;
    }
    if (signal.resolution != ResolutionKind::sv_wire
        && signal.value_kind == ValueKind::logic4
        && signal.systemverilog_scalar == SystemVerilogScalarKind::None) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_semantics;
        }
        return false;
    }
    if (signal.resolution != ResolutionKind::sv_wire
        || signal.value_kind != ValueKind::logic4
        || signal.systemverilog_scalar != SystemVerilogScalarKind::None
        || signal.event_variable || signal.has_implicit_driver
        || signal.has_charge_strength || external_driver_values[signal_id]
        || forced_values[signal_id]
        || forced_driver_values[signal_id]
        || signal.initial_value.width() == 0U
        || signal.initial_value.width() > 64U) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_runtime;
        }
        return false;
    }
    return true;
}

bool Interpreter::Impl::can_publish_native_logic9_word_prevalidated(
    const SignalId signal_id,
    const ProcessId process) noexcept
{
    if (signal_id >= signals.size()
        || signal_id >= direct_single_driver_routes.size()
        || signal_id >= dynamic_fanout.size()
        || signal_id >= signal_container_aliases.size()
        || native_signal_has_runtime_dependency(signal_id)
        || has_dynamic_waits(signal_id)
        || !signal_container_aliases[signal_id].empty()) {
        return false;
    }
    const auto& signal = signals[signal_id];
    const auto& route = direct_single_driver_routes[signal_id];
    return route.active && route.process == process
        && direct_single_driver_record(signal_id) != nullptr
        && signal.resolution == ResolutionKind::std_logic
        && signal.value_kind == ValueKind::logic9
        && signal.systemverilog_scalar == SystemVerilogScalarKind::None
        && !signal.event_variable && !signal.has_implicit_driver
        && !signal.has_charge_strength
        && !external_driver_values[signal_id]
        && !forced_values[signal_id]
        && !forced_driver_values[signal_id]
        && signal.initial_value.width() != 0U
        && signal.initial_value.width() <= 64U;
}

bool Interpreter::Impl::can_publish_blocking_word(
    const SignalId signal_id) noexcept
{
    if (signal_id >= signals.size()
        || signal_id >= dynamic_fanout.size()
        || signal_id >= signal_container_aliases.size()
        || native_signal_has_runtime_dependency(signal_id)
        || has_dynamic_waits(signal_id)
        || !signal_container_aliases[signal_id].empty()) {
        return false;
    }
    if (native_signal_observation_required_hook) {
        if (native_signal_observation_required_hook(signal_id)) {
            return false;
        }
    } else if (signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook
        || container_element_change_hook) {
        return false;
    }
    const auto& signal = signals[signal_id];
    return signal.resolution == ResolutionKind::none
        && signal.value_kind == ValueKind::logic4
        && signal.systemverilog_scalar == SystemVerilogScalarKind::None
        && !signal.event_variable && !signal.has_implicit_driver
        && !signal.has_charge_strength
        && !external_driver_values[signal_id]
        && !forced_values[signal_id]
        && !forced_driver_values[signal_id]
        && signal.initial_value.width() != 0U
        && signal.initial_value.width() <= 64U;
}

void Interpreter::Impl::publish_native_word(
    const SignalId signal_id,
    Logic4Word value,
    const SignalChangeOrigin origin)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);

    const auto wide_offset = direct_wide_signal_offsets[signal_id];
    RegionAuthoritativeComponentState* a4_state { };
    ProcessId a4_owner { };
    const bool use_a4_direct_publication
        = prepare_native_a4_single_owner_publication(*this, signal_id,
            ValueKind::logic4, ResolutionKind::sv_wire, a4_state, a4_owner);
    if (!use_a4_direct_publication) {
        prepare_region_authoritative_write(signal_id);
    }
    note_region_authoritative_mirror();

    const bool has_static_fanout = !static_fanout_for(signal_id).empty();
    note_signal_transaction(signal_id, has_static_fanout, origin,
        use_a4_direct_publication);
    const auto width = signals[signal_id].initial_value.width();
    const auto mask = width == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << width) - UINT64_C(1);
    value.aval &= mask;
    value.bval &= mask;
    const auto current = Logic4Word {
        width, direct_signal_aval[signal_id], direct_signal_bval[signal_id]
    };
    if (current == value) {
        return;
    }

    std::optional<PackedLogic4> packed_value;
    if (use_a4_direct_publication) {
        packed_value.emplace(PackedLogic4::from_aval_bval(
            width, value.aval, value.bval));
    }

    completed_callback_observation_generation = 0U;
    direct_signal_last_aval[signal_id] = current.aval;
    direct_signal_last_bval[signal_id] = current.bval;
    direct_signal_aval[signal_id] = value.aval;
    direct_signal_bval[signal_id] = value.bval;
    direct_wide_signal_aval[wide_offset] = value.aval;
    direct_wide_signal_bval[wide_offset] = value.bval;

    bool a4_direct_publication_completed { };
    if (use_a4_direct_publication) {
        a4_direct_publication_completed
            = publish_native_a4_single_owner_value(
                *this, signal_id, a4_state, a4_owner, *packed_value);
        if (!a4_direct_publication_completed) {
            request_full_region_recertification();
            demote_region_authoritative_slots(
                region_authoritative_component_by_signal[signal_id], true);
        }
    }

    if (a4_direct_publication_completed) {
        mark_region_value_change(signal_id,
            signal_last_values[signal_id], signals[signal_id].initial_value);
    } else {
        direct_signal_materialization_pending[signal_id] = 1U;
        if (use_a4_direct_publication) {
            materialize_direct_signal(signal_id);
        }
        if (auto* const state
            = region_authoritative_state_for_signal(signal_id)) {
            materialize_direct_signal(signal_id);
            if (state->values().packed_slots_bound()
                && systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a4_authoritative_slot_writes;
            }
            state->values().mirror_logic4_word(signal_id, current, value,
                driven_values[signal_id].unchecked_low_word());
            const auto& route = direct_single_driver_routes[signal_id];
            if (route.active) {
                if (const auto* const record
                    = direct_single_driver_record(signal_id)) {
                    state->values().mirror_owner(
                        signal_id, route.process, record->value);
                }
            }
            if (!state->valid()) {
                request_full_region_recertification();
                demote_region_authoritative_slots(
                    region_authoritative_component_by_signal[signal_id], true);
            }
            mark_region_value_change(signal_id,
                signal_last_values[signal_id],
                signals[signal_id].initial_value);
        }
    }

    ++signal_value_revisions[signal_id];
    note_aggregate_leaf_current_change(signal_id);
    if (signal_value_revisions[signal_id] == 0U) {
        signal_value_revisions[signal_id] = 1U;
        std::ranges::fill(
            container_materialized_revisions, std::nullopt);
    }
    record_signal_event(
        signal_id, scheduler.delta() + 1U, origin);
    scheduler.note_signal_change(signal_id);

    if (!has_static_fanout) {
        return;
    }
    const auto decode_low = [](const Logic4Word word) {
        const bool aval = (word.aval & UINT64_C(1)) != 0U;
        const bool bval = (word.bval & UINT64_C(1)) != 0U;
        return bval ? (aval ? Logic4::x : Logic4::z)
                    : (aval ? Logic4::one : Logic4::zero);
    };
    auto transition = StaticTransitionMatches { };
    const bool has_scalar_edge_fanout
        = !static_fanout_indices_for(
               signal_id, EdgeKind::posedge).empty()
        || !static_fanout_indices_for(
               signal_id, EdgeKind::negedge).empty();
    if (width == 1U && has_scalar_edge_fanout) {
        transition = decode_static_transition(
            decode_low(current), decode_low(value));
    }
    notify_static_value_change(
        signal_id, transition, true, origin, true);
}

void Interpreter::Impl::publish_native_logic9_word(
    const SignalId signal_id,
    Logic9Word value,
    const SignalChangeOrigin origin)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    const auto width = signals[signal_id].initial_value.width();
    const auto mask = width == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << width) - UINT64_C(1);
    value.width = width;
    for (auto& plane : value.planes) {
        plane &= mask;
    }

    RegionAuthoritativeComponentState* a4_state { };
    ProcessId a4_owner { };
    const bool use_a4_direct_publication
        = value.has_canonical_codes()
        && prepare_native_a4_single_owner_publication(*this, signal_id,
            ValueKind::logic9, ResolutionKind::std_logic, a4_state, a4_owner);
    if (!use_a4_direct_publication) {
        prepare_region_authoritative_write(signal_id);
    }
    note_region_authoritative_mirror();
    const bool has_static_fanout = !static_fanout_for(signal_id).empty();
    note_signal_transaction(signal_id, has_static_fanout, origin,
        use_a4_direct_publication);

    // Preserve the checked runtime value even if a raw native plane contains
    // a reserved code. The A4 mirror declines this component until a later
    // quiet-point snapshot can seed valid checked values again.
    const auto current = Logic9Word {
        width,
        { direct_signal_logic9_plane0[signal_id],
            direct_signal_logic9_plane1[signal_id],
            direct_signal_logic9_plane2[signal_id],
            direct_signal_logic9_plane3[signal_id] }
    };
    if ((((current.planes[0] ^ value.planes[0])
              | (current.planes[1] ^ value.planes[1])
              | (current.planes[2] ^ value.planes[2])
              | (current.planes[3] ^ value.planes[3]))
            & mask)
        == 0U) {
        return;
    }
    completed_callback_observation_generation = 0U;
    std::optional<PackedLogic4> packed_value;
    if (use_a4_direct_publication) {
        packed_value.emplace(PackedLogic4::from_logic9_word(value));
    }
    direct_signal_last_logic9_plane0[signal_id] = current.planes[0];
    direct_signal_last_logic9_plane1[signal_id] = current.planes[1];
    direct_signal_last_logic9_plane2[signal_id] = current.planes[2];
    direct_signal_last_logic9_plane3[signal_id] = current.planes[3];
    direct_signal_logic9_plane0[signal_id] = value.planes[0];
    direct_signal_logic9_plane1[signal_id] = value.planes[1];
    direct_signal_logic9_plane2[signal_id] = value.planes[2];
    direct_signal_logic9_plane3[signal_id] = value.planes[3];

    const auto wide_offset = direct_wide_signal_offsets[signal_id];
    bool a4_direct_publication_completed { };
    if (use_a4_direct_publication) {
        direct_wide_signal_aval[wide_offset] = value.planes[0U];
        direct_wide_signal_bval[wide_offset] = value.planes[1U];
        direct_wide_signal_logic9_plane2[wide_offset] = value.planes[2U];
        direct_wide_signal_logic9_plane3[wide_offset] = value.planes[3U];
        a4_direct_publication_completed
            = publish_native_a4_single_owner_value(
                *this, signal_id, a4_state, a4_owner, *packed_value);
        if (!a4_direct_publication_completed) {
            request_full_region_recertification();
            demote_region_authoritative_slots(
                region_authoritative_component_by_signal[signal_id], true);
        }
    }

    if (!a4_direct_publication_completed) {
        direct_signal_materialization_pending[signal_id] = 1U;
        if (use_a4_direct_publication) {
            materialize_direct_signal(signal_id);
        }
        if (auto* const state
            = region_authoritative_state_for_signal(signal_id)) {
            materialize_direct_signal(signal_id);
            if (state->values().packed_slots_bound()
                && systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a4_authoritative_slot_writes;
            }
            state->values().mirror_logic9_word(signal_id, current, value,
                driven_values[signal_id].logic9_low_word());
            const auto& route = direct_single_driver_routes[signal_id];
            if (route.active) {
                if (const auto* const record
                    = direct_single_driver_record(signal_id)) {
                    state->values().mirror_owner(
                        signal_id, route.process, record->value);
                }
            }
            if (!state->valid()) {
                request_full_region_recertification();
                demote_region_authoritative_slots(
                    region_authoritative_component_by_signal[signal_id],
                    true);
            }
            mark_region_value_change(signal_id,
                signal_last_values[signal_id], signals[signal_id].initial_value);
        }
    } else {
        mark_region_value_change(signal_id,
            signal_last_values[signal_id], signals[signal_id].initial_value);
    }

    ++signal_value_revisions[signal_id];
    note_aggregate_leaf_current_change(signal_id);
    if (signal_value_revisions[signal_id] == 0U) {
        signal_value_revisions[signal_id] = 1U;
        std::ranges::fill(
            container_materialized_revisions, std::nullopt);
    }
    record_signal_event(
        signal_id, scheduler.delta() + 1U, origin);
    scheduler.note_signal_change(signal_id);

    if (!has_static_fanout) {
        return;
    }
    const auto decode_low = [](const Logic9Word& word) {
        std::uint8_t encoded { };
        for (std::size_t plane = 0; plane < word.planes.size(); ++plane) {
            encoded |= static_cast<std::uint8_t>(
                (word.planes[plane] & UINT64_C(1)) << plane);
        }
        const auto logic9
            = encoded <= static_cast<std::uint8_t>(Logic9::dont_care)
            ? static_cast<Logic9>(encoded)
            : Logic9::x;
        return to_logic4(logic9);
    };
    auto transition = StaticTransitionMatches { };
    const bool has_scalar_edge_fanout
        = !static_fanout_indices_for(
               signal_id, EdgeKind::posedge).empty()
        || !static_fanout_indices_for(
               signal_id, EdgeKind::negedge).empty();
    if (width == 1U && has_scalar_edge_fanout) {
        transition = decode_static_transition(
            decode_low(current), decode_low(value));
    }
    notify_static_value_change(
        signal_id, transition, false, origin, true);
}

void Interpreter::Impl::note_signal_transaction(
    const SignalId signal_id,
    const bool notify_fanout,
    const SignalChangeOrigin origin,
    const bool authoritative_write_prepared)
{
    if (!authoritative_write_prepared) {
        prepare_region_authoritative_write(signal_id);
        mirror_region_stored(signal_id);
    }
    if (notify_fanout) {
        if (auto* const region_state
            = region_authoritative_state_for_signal(signal_id)) {
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a4_transaction_marks;
            }
            region_state->fanout().mark_transaction(
                signal_id, region_state->readiness());
        }
    }
    scheduler.note_signal_transaction(signal_id);
    const bool group_static_fanout = fanout_cohort_grouping_enabled;
    signal_transactions[signal_id] = std::pair { scheduler.now(), scheduler.delta() + 1 };
    if (signal_id < signal_container_element_aliases.size()
        && signal_container_element_aliases[signal_id]) {
        const auto object
            = signal_container_element_aliases[signal_id]->first;
        if (object < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[object]) {
            const auto proxy
                = container_aggregate_signal_aliases[object]->signal;
            auto& batch = aggregate_signal_batches[proxy];
            if (batch.depth != 0U) {
                batch.transaction_changed = true;
                batch.notify_fanout = batch.notify_fanout || notify_fanout;
                batch.origin = origin;
            } else {
                note_signal_transaction(proxy, notify_fanout, origin);
            }
        }
    }
    const auto transaction_fanout
        = static_fanout_indices_for(signal_id, EdgeKind::transaction);
    if (notify_fanout && !transaction_fanout.empty()) {
        const auto visit = next_static_fanout_visit();
        const auto no_cohort = std::numeric_limits<std::size_t>::max();
        for (const auto entry_index : transaction_fanout) {
            const auto& sensitivity = static_fanout_entries[entry_index];
            auto& triggered_process = get_process(sensitivity.process);
            std::uint64_t grouped_trigger_mask { };
            if (region_take_ready(signal_id, sensitivity.process,
                    grouped_trigger_mask)) {
                triggered_process.static_trigger_mask
                    |= grouped_trigger_mask;
            } else {
                triggered_process.static_trigger_mask
                    |= sensitivity.static_trigger_mask;
            }
            merge_generic_frontier_ready_mask(sensitivity.process,
                triggered_process.static_trigger_mask);
            const auto cohort
                = sensitivity.process
                        < static_sensitivity_cohort_by_process.size()
                ? static_sensitivity_cohort_by_process[
                      sensitivity.process]
                : no_cohort;
            if (group_static_fanout && cohort != no_cohort
                && static_sensitivity_cohorts[cohort].members.size()
                    >= 2U) {
                auto& grouped = static_sensitivity_cohorts[cohort];
                bool all_generic_update_members =
                    origin.process_domain == ProcessSchedulingDomain::generic
                    && triggered_process.program().scheduling_domain()
                        == ProcessSchedulingDomain::generic
                    && region_graph.has_value();
                if (all_generic_update_members) {
                    const auto& graph_processes = region_graph->processes();
                    for (const auto member : grouped.members) {
                        if (member >= graph_processes.size()
                            || graph_processes[member].scheduling_domain
                                != ProcessSchedulingDomain::generic
                            || graph_processes[member].update_kind
                                != RegionUpdateKind::generic) {
                            all_generic_update_members = false;
                            break;
                        }
                    }
                }
                if (all_generic_update_members) {
                    if (triggered_process.waiting_on_static) {
                        queue_static_next_delta(sensitivity.process, origin);
                    }
                    continue;
                }
                if (grouped.fanout_visit != visit) {
                    grouped.fanout_visit = visit;
                    if (origin.process_domain
                            == ProcessSchedulingDomain::generic
                        && triggered_process.program().scheduling_domain()
                            == ProcessSchedulingDomain::generic) {
                        queue_static_cohort_next_delta(cohort);
                    } else {
                        for (const auto member : grouped.members) {
                            auto& candidate = get_process(member);
                            if (candidate.waiting_on_static) {
                                queue_static_next_delta(member, origin);
                            }
                        }
                    }
                }
                continue;
            }
            auto& process = get_process(sensitivity.process);
            if (process.waiting_on_static) {
                queue_static_next_delta(sensitivity.process, origin);
            }
        }
    }
}

void Interpreter::Impl::publish_aggregate_leaf_value_change(
    const SignalId signal_id,
    const bool notify_fanout,
    const SignalChangeOrigin origin)
{
    if (signal_id >= signal_container_element_aliases.size()
        || !signal_container_element_aliases[signal_id]) {
        return;
    }
    const auto object
        = signal_container_element_aliases[signal_id]->first;
    if (object >= container_aggregate_signal_aliases.size()
        || !container_aggregate_signal_aliases[object]) {
        return;
    }
    const auto proxy = container_aggregate_signal_aliases[object]->signal;
    auto& batch = aggregate_signal_batches[proxy];
    if (batch.depth != 0U) {
        batch.changed = true;
        batch.notify_fanout = batch.notify_fanout || notify_fanout;
        batch.origin = origin;
        return;
    }
    publish_aggregate_signal_value_change(proxy, notify_fanout, origin);
}

void Interpreter::Impl::publish_aggregate_signal_value_change(
    const SignalId proxy,
    const bool notify_fanout,
    const SignalChangeOrigin origin,
    std::exception_ptr* observer_error,
    const bool event_prepared)
{
    prepare_region_authoritative_write(proxy);
    // Whole-family writes can preinstall the stamp and history before traces.
    if (!event_prepared) {
        record_signal_event(proxy, scheduler.delta() + 1U, origin);
    }
    scheduler.note_signal_change(proxy);

    if (observer_error == nullptr) {
        const auto& previous = aggregate_signal_last_value(proxy);
        const auto& current = aggregate_signal_current_value(proxy);
        evaluate_module_timing_checks(proxy, previous, current);
        if (signal_change_hook) {
            signal_change_hook(proxy, current, scheduler.now());
        }
        if (monitor_watches(proxy)) {
            schedule_monitor_publication();
        }
        if (!notify_fanout) {
            return;
        }
        const auto static_entries = static_fanout_for(proxy);
        const auto dynamic_entries = dynamic_fanout.at(proxy);
        if (static_entries.empty() && dynamic_entries.empty()) {
            return;
        }
        signals[proxy].initial_value = current;
        signal_last_values[proxy] = previous;
        refresh_direct_signal_planes(proxy);
        notify_static_value_change(proxy, { }, false, origin);
        for (const auto& registration : dynamic_entries) {
            if (registration.edge != EdgeKind::any) {
                continue;
            }
            if (registration.process >= processes.size()) {
                continue;
            }
            auto& process = get_process(registration.process);
            if (dynamic_wait_satisfied(process, proxy, registration)) {
                mark_dynamic_event_resume(process);
                queue_next_delta(registration.process, origin);
            }
        }
        return;
    }

    const auto& previous = aggregate_signal_last_value(proxy);
    const auto& current = aggregate_signal_current_value(proxy);
    std::optional<PackedLogic4> observer_value;
    if (signal_change_hook) {
        observer_value.emplace(current);
    }
    evaluate_module_timing_checks(proxy, previous, current);
    if (monitor_watches(proxy)) {
        schedule_monitor_publication();
    }
    if (notify_fanout) {
        const auto static_entries = static_fanout_for(proxy);
        const auto dynamic_entries = dynamic_fanout.at(proxy);
        if (!static_entries.empty() || !dynamic_entries.empty()) {
            signals[proxy].initial_value = current;
            signal_last_values[proxy] = previous;
            refresh_direct_signal_planes(proxy);
            notify_static_value_change(proxy, { }, false, origin);
            for (const auto& registration : dynamic_entries) {
                if (registration.edge != EdgeKind::any) {
                    continue;
                }
                if (registration.process >= processes.size()) {
                    continue;
                }
                auto& process = get_process(registration.process);
                if (dynamic_wait_satisfied(process, proxy, registration)) {
                    mark_dynamic_event_resume(process);
                    queue_next_delta(registration.process, origin);
                }
            }
        }
    }

    if (observer_value) {
        try {
            signal_change_hook(
                proxy, *observer_value, scheduler.now());
        } catch (...) {
            if (!*observer_error) {
                *observer_error = std::current_exception();
            }
        }
    }
}

void Interpreter::Impl::publish_value_change(
    const SignalId signal_id,
    const bool notify_fanout,
    const SignalChangeOrigin origin,
    const bool state_prepared)
{
    auto& signal = signals[signal_id];
    const auto& old_value = signal_last_values[signal_id];
    if (!state_prepared) {
        ++signal_value_revisions[signal_id];
        note_aggregate_leaf_current_change(signal_id);
        if (signal_value_revisions[signal_id] == 0U) {
            signal_value_revisions[signal_id] = 1U;
            std::ranges::fill(
                container_materialized_revisions, std::nullopt);
        }
        record_signal_event(
            signal_id, scheduler.delta() + 1U, origin);
    }
    if (notify_fanout) {
        mark_region_value_change(
            signal_id, old_value, signal.initial_value);
    }
    scheduler.note_signal_change(signal_id);
    evaluate_module_timing_checks(signal_id, old_value, signal.initial_value);
    auto* alias_observer = container_alias_leaf_observer(signal_id);
    if (signal_change_hook && alias_observer) {
        alias_observer->current_changed = true;
        const auto object
            = signal_container_element_aliases[signal_id]->first;
        container_alias_write_batches[object].frames.back().changed = true;
    } else if (signal_change_hook) {
        signal_change_hook(signal_id, signal.initial_value, scheduler.now());
    }
    if (scalar_signal_change_hook
        && signal.systemverilog_scalar != SystemVerilogScalarKind::None) {
        if (alias_observer) {
            alias_observer->scalar_changed = true;
        } else {
            const auto scalar = decode_systemverilog_scalar_payload(
                signal.initial_value, signal.systemverilog_scalar);
            if (!scalar) {
                throw std::logic_error {
                    "SimIR scalar signal published an invalid payload"
                };
            }
            scalar_signal_change_hook(
                signal_id, scalar.value, scheduler.now());
        }
    }
    if (monitor_watches(signal_id)) {
        schedule_monitor_publication();
    }

    publish_aggregate_leaf_value_change(signal_id, notify_fanout, origin);

    if (notify_fanout) {
        auto transition = StaticTransitionMatches { };
        bool transition_decoded { };
        const bool has_static_edge_fanout
            = !static_fanout_indices_for(
                   signal_id, EdgeKind::posedge).empty()
            || !static_fanout_indices_for(
                   signal_id, EdgeKind::negedge).empty();
        if (old_value.width() == 1U && signal.initial_value.width() == 1U
            && has_static_edge_fanout) {
            transition = decode_static_transition(
                old_value.get(0), signal.initial_value.get(0));
            transition_decoded = true;
        }
        notify_static_value_change(
            signal_id, transition, false, origin, true);

        // Copy because queue_next_delta removes a process from every dynamic list.
        const auto dynamic = dynamic_fanout[signal_id];
        for (const auto& registration : dynamic) {
            if (registration.edge != EdgeKind::any) {
                if (old_value.width() != 1U
                    || signal.initial_value.width() != 1U) {
                    continue;
                }
                if (!transition_decoded) {
                    transition = decode_static_transition(
                        old_value.get(0), signal.initial_value.get(0));
                    transition_decoded = true;
                }
                if (!transition.matches(registration.edge)) {
                    continue;
                }
            }
            if (registration.process >= processes.size()) {
                continue;
            }
            auto& process = get_process(registration.process);
            if (dynamic_wait_satisfied(
                    process, signal_id, registration)) {
                mark_dynamic_event_resume(process);
                queue_next_delta(registration.process, origin);
            }
        }
    }
}

void Interpreter::Impl::refresh_direct_signal_planes(
    const SignalId signal_id)
{
    const auto& value = signals[signal_id].initial_value;
    if (value.is_logic9()) {
        if (value.width() != 0U && value.width() <= 64U) {
            const auto word = value.logic9_low_word();
            direct_signal_logic9_plane0[signal_id] = word.planes[0];
            direct_signal_logic9_plane1[signal_id] = word.planes[1];
            direct_signal_logic9_plane2[signal_id] = word.planes[2];
            direct_signal_logic9_plane3[signal_id] = word.planes[3];
        }
        const auto offset = direct_wide_signal_offsets[signal_id];
        const auto plane0 = value.logic9_plane_words(0U);
        const auto plane1 = value.logic9_plane_words(1U);
        const auto plane2 = value.logic9_plane_words(2U);
        const auto plane3 = value.logic9_plane_words(3U);
        std::ranges::copy(
            plane0, direct_wide_signal_aval.begin() + offset);
        std::ranges::copy(
            plane1, direct_wide_signal_bval.begin() + offset);
        std::ranges::copy(
            plane2, direct_wide_signal_logic9_plane2.begin() + offset);
        std::ranges::copy(
            plane3, direct_wide_signal_logic9_plane3.begin() + offset);
        return;
    }
    if (value.width() <= 64U) {
        const auto word = value.unchecked_low_word();
        direct_signal_aval[signal_id] = word.aval;
        direct_signal_bval[signal_id] = word.bval;
    }
    const auto offset = direct_wide_signal_offsets[signal_id];
    const auto aval = value.aval_words();
    const auto bval = value.bval_words();
    std::ranges::copy(
        aval, direct_wide_signal_aval.begin() + offset);
    std::ranges::copy(
        bval, direct_wide_signal_bval.begin() + offset);
}

} // namespace fsim::runtime::simir

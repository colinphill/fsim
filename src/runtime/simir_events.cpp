// SPDX-License-Identifier: Apache-2.0
// Registered scheduler event kinds of the interpreter (#128), batch A: process
// wake and timeout events. The dispatch bodies are the bodies of the lambdas
// they replace.
#include "simir_events.hpp"
#include "simir_internal.hpp"

#include <algorithm>

namespace fsim::runtime::simir::events {

void WaitTimeout::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& state = scheduled.owner->get_process(scheduled.process);
    auto* const timeout = state.cold().dynamic_wait_state_if_present();
    if (timeout == nullptr
        || timeout->wait_timeout_generation != scheduled.generation
        || state.wait_timeout_origin != std::optional { scheduled.origin }) {
        return;
    }
    scheduled.owner->set_wait_timeout_result(state, true);
    state.wait_timeout_origin.reset();
    timeout->wait_timeout_deadline.reset();
    timeout->wait_timeout_result.reset();
    if (scheduled.after_updates) {
        scheduled.owner->queue_next_delta(scheduled.process);
    } else {
        scheduled.owner->queue_active_current(scheduled.process);
    }
}

void ProcessQueueCompact::dispatch(Scheduler&, const Payload& scheduled)
{
    if (auto* state = scheduled.owner->processes.compact_constant(
            scheduled.process)) {
        state->queued = false;
        state->waiting_on_static = false;
    } else {
        auto& full_state = scheduled.owner->get_process(scheduled.process);
        full_state.queued = false;
        full_state.waiting_on_static = false;
        scheduled.owner->remove_dynamic_wait(full_state);
    }
    scheduled.owner->execute(scheduled.process);
}

void ProcessQueueTimed::dispatch(Scheduler&, const Payload& scheduled)
{
    if (auto* compact = scheduled.owner->processes.compact_constant(
            scheduled.process)) {
        compact->queued = false;
        compact->waiting_on_static = false;
    } else {
        auto& state = scheduled.owner->get_process(scheduled.process);
        // A disable abandoned this timer (IEEE 1800-2017 9.6.2).
        if (state.timer_epoch != scheduled.epoch) {
            return;
        }
        state.queued = false;
        state.timer_wake = false;
        state.waiting_on_static = false;
        scheduled.owner->remove_dynamic_wait(state);
    }
    scheduled.owner->execute(scheduled.process);
}

void ProcessTimeoutUpdate::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& state = scheduled.owner->get_process(scheduled.process);
    if (state.timer_epoch != scheduled.epoch) {
        return;
    }
    state.queued = false;
    state.timer_wake = false;
    scheduled.owner->queue_next_delta(scheduled.process);
}

void ProcessWake::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& state = scheduled.owner->get_process(scheduled.process);
    state.queued = false;
    state.waiting_on_static = false;
    scheduled.owner->remove_dynamic_wait(state);
    scheduled.owner->execute(scheduled.process);
}

void ProcessRunCurrent::dispatch(Scheduler&, const Payload& scheduled)
{
    if (auto* state = scheduled.owner->processes.compact_constant(
            scheduled.process)) {
        state->queued = false;
    } else {
        scheduled.owner->get_process(scheduled.process).queued = false;
    }
    scheduled.owner->execute(scheduled.process);
}

void ProcessRegionResume::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& state = scheduled.owner->get_process(scheduled.process);
    state.queued = false;
    scheduled.owner->execute(scheduled.process);
}

void ProcessWakeDomainCross::dispatch(
    Scheduler& runtime, const Payload& scheduled)
{
    auto& state = scheduled.owner->get_process(scheduled.process);
    state.queued = true;
    runtime.schedule_internal_systemverilog(scheduled.phase,
        scheduled.process,
        make<ProcessWake>(ProcessWake::Payload {
            scheduled.owner, scheduled.process }));
}

void EventNotifyDelta::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& pending = scheduled.owner->event_states[scheduled.event];
    if (pending.generation != scheduled.generation
        || pending.kind != Impl::PendingEventKind::delta) {
        return;
    }
    pending.kind = Impl::PendingEventKind::none;
    pending.due = 0;
    const auto origin = pending.origin;
    pending.origin = { };
    scheduled.owner->trigger_event(scheduled.event, origin);
}

void EventNotifyTimed::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& pending = scheduled.owner->event_states[scheduled.event];
    if (pending.generation != scheduled.generation
        || pending.kind != Impl::PendingEventKind::timed
        || pending.due != scheduled.due) {
        return;
    }
    pending.kind = Impl::PendingEventKind::none;
    pending.due = 0;
    const auto origin = pending.origin;
    pending.origin = { };
    scheduled.owner->trigger_event(scheduled.event, origin);
}

void UpdateCommit::dispatch(Scheduler& runtime, const Payload& scheduled)
{
    // The callback is built on first use, so a restart that re-creates this
    // event before any update was staged finds it.
    scheduled.owner->ensure_update_commit_callback();
    scheduled.owner->update_commit_callback(runtime);
}

void VhdlNextCycleResolutions::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    auto targets = std::move(owner.vhdl_next_cycle_resolutions);
    owner.vhdl_next_cycle_resolutions.clear();
    for (const auto target : targets) {
        if (owner.pending_vhdl_resolution_set.insert(target).second) {
            owner.pending_vhdl_resolutions.push_back(target);
        }
    }
    owner.drain_vhdl_resolutions();
}

void DriverDisconnect::dispatch(Scheduler& runtime, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    // This null transaction is no longer pending.
    if (const auto pending = owner.vhdl_pending_disconnects.find(
            std::pair { scheduled.signal, scheduled.process });
        pending != owner.vhdl_pending_disconnects.end()) {
        std::erase(pending->second, runtime.now());
        if (pending->second.empty()) {
            owner.vhdl_pending_disconnects.erase(pending);
        }
    }
    if (!owner.vhdl_disconnected_drivers
             .insert({ scheduled.signal, scheduled.process }).second) {
        return;
    }
    auto value = owner.run_vhdl_resolver(scheduled.signal);
    owner.vhdl_resolved_values.insert_or_assign(scheduled.signal, value);
    owner.commit(scheduled.signal,
        owner.apply_force(scheduled.signal, std::move(value)));
}

namespace {

// $monitor writes its line to the monitor's file, or to the output hook.
void emit_monitor(Impl& owner, const ProcessId process, Scheduler& runtime)
{
    if (owner.monitor_file_handle) {
        owner.write_file(process, *owner.monitor_file_handle,
            owner.render_monitor(*owner.monitor), owner.monitor->newline);
    } else if (owner.output_hook) {
        owner.output_hook(process, owner.render_monitor(*owner.monitor),
            owner.monitor->newline, runtime.now(), runtime.delta());
    }
}

} // namespace

void MonitorPublishSv::dispatch(Scheduler& runtime, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    if (scheduled.generation != owner.monitor_generation
        || !owner.monitor_enabled || !owner.monitor) {
        return;
    }
    owner.monitor_systemverilog_publication.reset();
    emit_monitor(owner, scheduled.process, runtime);
}

void MonitorPublishVhdl::dispatch(Scheduler& runtime, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    if (scheduled.generation != owner.monitor_generation
        || !owner.monitor_enabled || !owner.monitor) {
        return;
    }
    owner.monitor_publication.reset();
    emit_monitor(owner, scheduled.process, runtime);
}

void VcdBegin::dispatch(Scheduler& runtime, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    if (!owner.vcd_control_hook) {
        return;
    }
    VcdControlEvent begin;
    begin.kind = scheduled.kind;
    begin.time = runtime.now();
    begin.delta = runtime.delta();
    owner.vcd_control_hook(begin);
}

void TimingDelayedCopy::dispatch(Scheduler&, const Payload& scheduled)
{
    scheduled.owner->stage_update_slice(scheduled.signal,
        PackedLogic4 { 1, scheduled.value }, scheduled.offset);
}

void TimingCheckDeadline::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    const auto index = static_cast<std::size_t>(scheduled.index);
    auto& deadline_state = owner.module_timing_check_states[index];
    if (!deadline_state.active) {
        return;
    }
    owner.report_module_timing_violation(index);
    if (!owner.module_timing_checks[index].remain_active) {
        deadline_state.active = false;
    }
}

void ForeignNetDelayMature::dispatch(Scheduler&, const Payload& scheduled)
{
    using Pending = Impl::ForeignNetDelayPending;
    auto& impl = InterpreterEventAccess::impl(*scheduled.owner);
    auto& pending = impl.foreign_net_delay_loads.at(scheduled.signal)
                        .at(static_cast<std::size_t>(scheduled.index))
                        .pending;
    const auto found
        = std::ranges::find(pending, scheduled.serial, &Pending::serial);
    if (found == pending.end()) {
        return;
    }
    auto matured = std::move(*found);
    pending.erase(found);
    scheduled.owner->foreign_net_delay_mature(scheduled.signal,
        static_cast<std::size_t>(scheduled.index), matured.kind,
        matured.bits);
}

std::uint64_t driver_key_digest(const ProcessId process,
    const SignalId signal, const std::uint32_t offset,
    const std::uint32_t width, const ProcessSchedulingDomain domain,
    const SchedulerPhase phase) noexcept
{
    // A fixed mix (not std::hash): the digest is saved in the run-state image.
    const auto mix = [](std::uint64_t hash, const std::uint64_t value) {
        hash ^= value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6U)
            + (hash >> 2U);
        hash *= UINT64_C(0xff51afd7ed558ccd);
        hash ^= hash >> 33U;
        return hash;
    };
    std::uint64_t hash { UINT64_C(0x6a09e667f3bcc908) };
    hash = mix(hash, (static_cast<std::uint64_t>(signal) << 32U) | process);
    hash = mix(hash, (static_cast<std::uint64_t>(width) << 32U) | offset);
    hash = mix(hash,
        (static_cast<std::uint64_t>(domain) << 8U)
            | static_cast<std::uint64_t>(phase));
    return hash;
}

void InertialMature::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    const Impl::InertialDriverKey key { scheduled.process, scheduled.signal,
        scheduled.offset, scheduled.width, scheduled.domain, scheduled.phase };
    const auto pending = owner.pending_inertial_writes.find(key);
    if (pending == owner.pending_inertial_writes.end()) {
        return;
    }
    // The pending record holds the value this write matures to.
    auto value = std::move(pending->second.source_value);
    owner.pending_inertial_writes.erase(pending);
    const auto process = scheduled.process;
    const auto signal = scheduled.signal;
    const SignalChangeOrigin origin { scheduled.domain, scheduled.phase };
    const auto offset = static_cast<std::size_t>(scheduled.offset);
    if (scheduled.domain == ProcessSchedulingDomain::systemverilog) {
        if (scheduled.has_offset) {
            owner.commit_driver_slice(
                process, signal, std::move(value), offset, origin, false);
        } else {
            owner.commit_driver(
                process, signal, std::move(value), origin, false);
        }
    } else if (scheduled.has_offset) {
        owner.stage_update_slice(process, signal, std::move(value), offset);
    } else {
        owner.stage_update(process, signal, std::move(value));
    }
}

void ModulePathMature::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    const Impl::InertialDriverKey key { scheduled.process, scheduled.signal,
        scheduled.offset, scheduled.width, scheduled.domain, scheduled.phase };
    const auto pending = owner.pending_module_path_writes.find(key);
    if (pending == owner.pending_module_path_writes.end()) {
        return;
    }
    auto scalar = std::move(pending->second.source_value);
    owner.pending_module_path_writes.erase(pending);
    const auto target_bit = static_cast<std::size_t>(scheduled.offset);
    if (scheduled.domain == ProcessSchedulingDomain::systemverilog) {
        owner.commit_driver_slice(scheduled.process, scheduled.signal,
            std::move(scalar), target_bit,
            SignalChangeOrigin { scheduled.domain, scheduled.phase }, false);
    } else {
        owner.stage_update_unrouted(scheduled.process, scheduled.signal,
            std::move(scalar), target_bit);
    }
}

void ModulePathPulseX::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    const auto target_bit = static_cast<std::size_t>(scheduled.offset);
    if (scheduled.domain == ProcessSchedulingDomain::systemverilog) {
        owner.commit_driver_slice(scheduled.process, scheduled.signal,
            PackedLogic4 { 1U, Logic4::x }, target_bit,
            SignalChangeOrigin { scheduled.domain, scheduled.phase }, false);
    } else {
        owner.stage_update_unrouted(scheduled.process, scheduled.signal,
            PackedLogic4 { 1U, Logic4::x }, target_bit);
    }
}

void NetDelayMature::dispatch(Scheduler&, const Payload& scheduled)
{
    const Impl::InertialDriverKey key { scheduled.process, scheduled.signal,
        scheduled.offset, scheduled.width, scheduled.domain, scheduled.phase };
    scheduled.owner->mature_net_delay(key, scheduled.process, scheduled.signal,
        SignalChangeOrigin { scheduled.domain, scheduled.phase });
}

void ProjectedTransactionMature::dispatch(
    Scheduler&, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    const auto process = scheduled.process;
    const auto signal = scheduled.signal;
    const auto offset = scheduled.offset;
    const Impl::ProjectedDriverKey key { process, signal, offset };
    const auto found_driver = owner.projected_drivers.find(key);
    if (found_driver == owner.projected_drivers.end()) {
        return;
    }
    auto& state = found_driver->second;
    const auto found_transaction = std::ranges::find(state.transactions,
        scheduled.id, &Impl::ProjectedTransaction::id);
    if (found_transaction == state.transactions.end()) {
        return;
    }
    auto committed_value = PackedLogic4 { 1U, Logic4::x };
    committed_value.fill(found_transaction->value);
    state.transactions.erase(found_transaction);
    owner.stage_update_slice(process, signal, committed_value, offset);
}

void ProjectedGroupMature::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    const auto process = scheduled.process;
    const auto signal = scheduled.signal;
    const auto first = static_cast<std::size_t>(scheduled.first);
    const auto width = static_cast<std::size_t>(scheduled.width);
    const auto id_of = [&](const std::size_t bit) {
        return scheduled.first_id + bit;
    };
    const auto key_of = [&](const std::size_t bit) {
        return Impl::ProjectedDriverKey { process, signal,
            static_cast<std::uint32_t>(first + bit) };
    };
    bool complete_group = true;
    for (std::size_t bit = 0; bit < width; ++bit) {
        const auto driver = owner.projected_drivers.find(key_of(bit));
        if (driver == owner.projected_drivers.end()
            || std::ranges::find(driver->second.transactions, id_of(bit),
                   &Impl::ProjectedTransaction::id)
                == driver->second.transactions.end()) {
            complete_group = false;
            break;
        }
    }
    if (complete_group) {
        // The group's value is what its bits' transactions hold.
        PackedLogic4 value { width, Logic4::x };
        for (std::size_t bit = 0; bit < width; ++bit) {
            auto& transactions
                = owner.projected_drivers.find(key_of(bit))->second.transactions;
            const auto transaction = std::ranges::find(
                transactions, id_of(bit), &Impl::ProjectedTransaction::id);
            value.set_logic9(bit, transaction->value);
            transactions.erase(transaction);
        }
        owner.stage_update_slice(process, signal, std::move(value), first);
        return;
    }
    for (std::size_t bit = 0; bit < width; ++bit) {
        const auto driver = owner.projected_drivers.find(key_of(bit));
        if (driver == owner.projected_drivers.end()) {
            continue;
        }
        auto& transactions = driver->second.transactions;
        const auto transaction = std::ranges::find(
            transactions, id_of(bit), &Impl::ProjectedTransaction::id);
        if (transaction == transactions.end()) {
            continue;
        }
        auto scalar = PackedLogic4 { 1U, Logic4::x };
        scalar.fill(transaction->value);
        transactions.erase(transaction);
        owner.stage_update_slice(process, signal, std::move(scalar),
            first + bit);
    }
}

void ChargeDecay::dispatch(Scheduler&, const Payload& scheduled)
{
    auto& owner = *scheduled.owner;
    const auto signal_id = scheduled.signal;
    const SignalChangeOrigin origin { scheduled.domain, scheduled.phase };
    owner.charge_decay_handles.at(signal_id).reset();
    auto& decaying_charge = *owner.charge_values.at(signal_id);
    const auto active = [&](const std::size_t bit) {
        bool active_driver { };
        owner.driver_values.at(signal_id).for_each_in_process_order(
            [&](const auto& record) {
                if (active_driver) {
                    return;
                }
                const auto logic = owner.driver_force_logic4_at(
                    signal_id, record.process, record.value, bit);
                const auto strength = record.strength;
                active_driver
                    = (logic == Logic4::zero
                          && strength.zero != StrengthRank::highz)
                    || (logic == Logic4::one
                          && strength.one != StrengthRank::highz)
                    || (logic == Logic4::x
                          && (strength.zero != StrengthRank::highz
                              || strength.one != StrengthRank::highz));
            });
        if (active_driver) {
            return true;
        }
        return owner.external_driver_values.at(signal_id)
            && owner.external_driver_values.at(signal_id)->get(bit)
            != Logic4::z;
    };
    for (std::size_t bit = 0; bit < decaying_charge.width(); ++bit) {
        if (!active(bit))
            decaying_charge.set(bit, Logic4::z);
    }
    owner.commit(signal_id, owner.resolved_driver_value(signal_id), origin);
}

void CohortWake::dispatch(Scheduler&, const Payload& scheduled)
{
    scheduled.owner->execute_queued_static_cohort(
        static_cast<std::size_t>(scheduled.cohort));
}

namespace {

template <typename Kind>
SchedulerEventKindRegistration add()
{
    return register_scheduler_event_kind<typename Kind::Payload,
        &Kind::dispatch, Kind>(Kind::name, SchedulerEventClass::data);
}

// The pooled update tokens: a slot index and its generation. The slot's value
// is host state saved with the pool, so the restored pool keeps the indices.
struct SystemVerilogUpdateCodec
    : Codec<Impl::SystemVerilogUpdateToken,
          &Impl::SystemVerilogUpdateToken::slot,
          &Impl::SystemVerilogUpdateToken::generation> { };
struct DelayedUpdateCodec
    : Codec<Impl::DelayedUpdateToken, &Impl::DelayedUpdateToken::slot,
          &Impl::DelayedUpdateToken::generation> { };

} // namespace

void register_kinds()
{
    static_cast<void>(add<WaitTimeout>());
    static_cast<void>(add<ProcessQueueCompact>());
    static_cast<void>(add<ProcessQueueTimed>());
    static_cast<void>(add<ProcessTimeoutUpdate>());
    static_cast<void>(add<ProcessWake>());
    static_cast<void>(add<ProcessRunCurrent>());
    static_cast<void>(add<ProcessRegionResume>());
    static_cast<void>(add<ProcessWakeDomainCross>());
    static_cast<void>(add<EventNotifyDelta>());
    static_cast<void>(add<EventNotifyTimed>());
    static_cast<void>(add<UpdateCommit>());
    static_cast<void>(add<VhdlNextCycleResolutions>());
    static_cast<void>(add<DriverDisconnect>());
    static_cast<void>(add<MonitorPublishSv>());
    static_cast<void>(add<MonitorPublishVhdl>());
    static_cast<void>(add<VcdBegin>());
    static_cast<void>(add<TimingDelayedCopy>());
    static_cast<void>(add<TimingCheckDeadline>());
    static_cast<void>(add<ForeignNetDelayMature>());
    static_cast<void>(add<InertialMature>());
    static_cast<void>(add<ModulePathMature>());
    static_cast<void>(add<ModulePathPulseX>());
    static_cast<void>(add<NetDelayMature>());
    static_cast<void>(add<ProjectedTransactionMature>());
    static_cast<void>(add<ProjectedGroupMature>());
    static_cast<void>(add<ChargeDecay>());
    static_cast<void>(add<CohortWake>());
    // Existing descriptors: the pooled SystemVerilog update (token = slot and
    // generation; the slot is saved with the pool) and the pooled delayed
    // generic update.
    static_cast<void>(register_scheduler_event_kind<
        Impl::SystemVerilogUpdateToken, &Impl::dispatch_systemverilog_update,
        SystemVerilogUpdateCodec>("interp.sv_update",
        SchedulerEventClass::data));
    static_cast<void>(register_scheduler_event_kind<
        Impl::DelayedUpdateToken, &Impl::dispatch_delayed_update,
        DelayedUpdateCodec>("interp.delayed_update",
        SchedulerEventClass::data));
}

namespace {

// Registered before main so a save finds the kinds of every event, whichever
// translation unit scheduled it.
[[maybe_unused]] const bool registered = (register_kinds(), true);

} // namespace

} // namespace fsim::runtime::simir::events

namespace fsim::runtime::simir {

Interpreter::Impl& InterpreterEventAccess::impl(
    Interpreter& interpreter) noexcept
{
    return *interpreter.impl_;
}

void Interpreter::Impl::schedule_delayed_update(const DelayedUpdateKind kind,
    const ProcessId driver, const SignalId signal, PackedLogic4 value,
    const Logic4Word word, const std::size_t offset, const SimulationTick delay)
{
    if (delay > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
        throw std::overflow_error(
            "simulation time overflow while scheduling event");
    }
    std::size_t index = delayed_update_free_head;
    if (index != no_systemverilog_update_slot) {
        delayed_update_free_head = delayed_update_slots[index].next_free;
    } else {
        index = delayed_update_slots.size();
        delayed_update_slots.emplace_back();
    }
    auto& slot = delayed_update_slots[index];
    slot.kind = kind;
    slot.driver = driver;
    slot.signal = signal;
    slot.value = std::move(value);
    slot.word = word;
    slot.offset = offset;
    slot.next_free = no_systemverilog_update_slot;
    slot.occupied = true;
    const DelayedUpdateToken token { this, index, slot.generation };
    try {
        scheduler.schedule_internal_at(scheduler.now() + delay,
            SchedulerPhase::update, driver,
            fsim::runtime::detail::make_scheduler_task_descriptor<
                DelayedUpdateToken, &Impl::dispatch_delayed_update>(token));
    } catch (...) {
        auto& failed = delayed_update_slots[index];
        failed.occupied = false;
        failed.value = PackedLogic4 { };
        ++failed.generation;
        failed.next_free = delayed_update_free_head;
        delayed_update_free_head = index;
        throw;
    }
}

void Interpreter::Impl::dispatch_delayed_update(
    Scheduler&, const DelayedUpdateToken& token)
{
    auto& owner = *token.owner;
    if (token.slot >= owner.delayed_update_slots.size()
        || !owner.delayed_update_slots[token.slot].occupied
        || owner.delayed_update_slots[token.slot].generation
            != token.generation) {
        throw std::logic_error { "delayed update token is stale" };
    }
    auto& slot = owner.delayed_update_slots[token.slot];
    // Free the slot before staging: the update may schedule another delayed
    // update and grow the pool.
    const auto kind = slot.kind;
    const auto driver = slot.driver;
    const auto signal = slot.signal;
    auto value = std::move(slot.value);
    const auto word = slot.word;
    const auto offset = slot.offset;
    slot.occupied = false;
    slot.value = PackedLogic4 { };
    ++slot.generation;
    slot.next_free = owner.delayed_update_free_head;
    owner.delayed_update_free_head = token.slot;
    switch (kind) {
    case DelayedUpdateKind::whole:
        owner.stage_update(driver, signal, std::move(value));
        break;
    case DelayedUpdateKind::slice:
        owner.stage_update_slice(driver, signal, std::move(value), offset);
        break;
    case DelayedUpdateKind::word: {
        const auto update = ProcessUpdateWord { signal, word, 0U, false };
        owner.stage_validated_update_words(driver, std::span { &update, 1U });
        break;
    }
    case DelayedUpdateKind::slice_word: {
        const auto update = ProcessUpdateWord { signal, word,
            static_cast<std::uint32_t>(offset), true };
        owner.stage_validated_update_words(driver, std::span { &update, 1U });
        break;
    }
    }
}

void Interpreter::Impl::clear_delayed_update_pool() noexcept
{
    delayed_update_slots.clear();
    delayed_update_free_head = no_systemverilog_update_slot;
}

bool Interpreter::Impl::bind_restored_event_handle(
    const std::string_view kind, const std::uint64_t key,
    const ScheduledTaskHandle& handle)
{
    if (kind == events::TimingCheckDeadline::name) {
        if (key >= module_timing_check_states.size()) {
            return false;
        }
        module_timing_check_states[static_cast<std::size_t>(key)].deadline
            = handle;
        return true;
    }
    // The pending records of the delayed writes are restored with the host
    // state before the events, so each event finds the record it belongs to
    // by the digest of the record's key; a record that already has a handle
    // belongs to another event.
    const auto digest_of = [](const InertialDriverKey& record) {
        return events::driver_key_digest(record.process, record.signal,
            record.offset, record.width, record.process_domain, record.phase);
    };
    if (kind == events::InertialMature::name) {
        for (auto& [record, pending] : pending_inertial_writes) {
            if (!pending.handle && digest_of(record) == key) {
                pending.handle = handle;
                return true;
            }
        }
        return false;
    }
    if (kind == events::ModulePathMature::name) {
        for (auto& [record, pending] : pending_module_path_writes) {
            if (!pending.handle && digest_of(record) == key) {
                pending.handle = handle;
                return true;
            }
        }
        return false;
    }
    if (kind == events::NetDelayMature::name) {
        // Events arrive in time order and a record's wakes are ordered by
        // time, so the next wake without a handle is this event's.
        for (auto& [record, pending] : pending_net_delay_writes) {
            if (digest_of(record) != key) {
                continue;
            }
            for (auto& [time, wake] : pending.wakes) {
                static_cast<void>(time);
                if (!wake.handle) {
                    wake.handle = handle;
                    return true;
                }
            }
        }
        return false;
    }
    if (kind == events::ProjectedTransactionMature::name) {
        for (auto& [record, state] : projected_drivers) {
            static_cast<void>(record);
            for (auto& transaction : state.transactions) {
                if (transaction.id == key) {
                    transaction.handle = handle;
                    return true;
                }
            }
        }
        return false;
    }
    if (kind == events::ChargeDecay::name) {
        if (key >= charge_decay_handles.size()) {
            return false;
        }
        charge_decay_handles[static_cast<std::size_t>(key)] = handle;
        return true;
    }
    return false;
}

} // namespace fsim::runtime::simir


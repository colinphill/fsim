// SPDX-License-Identifier: Apache-2.0
#pragma once

// Registered scheduler event kinds of the interpreter (#128). Each kind is one
// (payload, dispatch) pair that used to be a local struct and lambda at its
// scheduling site; naming it here lets a run-state save write the pending event
// without pointers (scheduler_events.hpp) and a restart re-create it.
//
// Batch A: process wake and timeout events. Their payloads are an interpreter
// pointer plus ids; the state they test (queued flags, timer epochs, wait
// generations) is process state saved with the processes (S4).

#include "fsim/runtime/scheduler_events.hpp"
#include "fsim/runtime/simir.hpp"

#include <string_view>

namespace fsim::runtime::simir {

/// Names `Interpreter::Impl` for the event kinds (Impl is private to
/// Interpreter).
struct InterpreterEventAccess {
    using Impl = Interpreter::Impl;
    /// The private implementation of a public interpreter (events scheduled
    /// through the public API carry the Interpreter, not the Impl).
    [[nodiscard]] static Impl& impl(Interpreter& interpreter) noexcept;
};

namespace events {

using Impl = InterpreterEventAccess::Impl;

struct InterpreterDomain {
    static constexpr std::string_view name { "interpreter" };
};
struct InterpreterApiDomain {
    static constexpr std::string_view name { "interpreter_api" };
};

template <typename Payload, auto... Fields>
using Codec = OwnedFieldsSchedulerEventCodec<Payload, Impl,
    InterpreterDomain, &Payload::owner, Fields...>;

template <typename Payload, auto... Fields>
using ApiCodec = OwnedFieldsSchedulerEventCodec<Payload, Interpreter,
    InterpreterApiDomain, &Payload::owner, Fields...>;

template <typename Kind>
[[nodiscard]] fsim::runtime::detail::SchedulerTaskDescriptor make(
    const typename Kind::Payload& payload)
{
    return fsim::runtime::detail::make_scheduler_task_descriptor<
        typename Kind::Payload,
        &Kind::dispatch>(payload);
}

/// Registers every interpreter kind. Called once from a static initializer.
void register_kinds();

// A WaitOn timeout expires (IEEE 1800-2023 9.4.3 wait with timeout; IEEE
// 1076-2019 10.2): sets the timeout result and wakes the process.
struct WaitTimeoutPayload {
    Impl* owner { };
    ProcessId process { };
    InstructionIndex origin { };
    std::uint64_t generation { };
    bool after_updates { };
};
struct WaitTimeout : Codec<WaitTimeoutPayload, &WaitTimeoutPayload::process,
                         &WaitTimeoutPayload::origin,
                         &WaitTimeoutPayload::generation,
                         &WaitTimeoutPayload::after_updates> {
    using Payload = WaitTimeoutPayload;
    static constexpr std::string_view name { "interp.wait_timeout" };
    static void dispatch(Scheduler&, const Payload&);
};

// queue_at, constant-driver process: clears the queued flag and runs it.
struct ProcessId1Payload {
    Impl* owner { };
    ProcessId process { };
};
struct ProcessQueueCompact : Codec<ProcessId1Payload,
                                 &ProcessId1Payload::process> {
    using Payload = ProcessId1Payload;
    static constexpr std::string_view name { "interp.process_queue_compact" };
    static void dispatch(Scheduler&, const Payload&);
};

// queue_at, ordinary process; a disable that abandoned the timer changed the
// process's timer epoch (IEEE 1800-2023 9.6.2), which makes this a no-op.
struct ProcessEpochPayload {
    Impl* owner { };
    ProcessId process { };
    std::uint32_t epoch { };
};
struct ProcessQueueTimed : Codec<ProcessEpochPayload,
                               &ProcessEpochPayload::process,
                               &ProcessEpochPayload::epoch> {
    using Payload = ProcessEpochPayload;
    static constexpr std::string_view name { "interp.process_queue_timed" };
    static void dispatch(Scheduler&, const Payload&);
};

// queue_timeout: a VHDL timeout resumes after the signal updates of its time
// (IEEE 1076-2019 14.7.5.3).
struct ProcessTimeoutUpdate : Codec<ProcessEpochPayload,
                                  &ProcessEpochPayload::process,
                                  &ProcessEpochPayload::epoch> {
    using Payload = ProcessEpochPayload;
    static constexpr std::string_view name { "interp.process_timeout_update" };
    static void dispatch(Scheduler&, const Payload&);
};

// A wake that clears the queued and static-wait flags, drops the dynamic wait
// and runs the process: queue_next_delta, queue_zero_delay_resume,
// queue_active_current_at and the VHDL-to-SystemVerilog hand-off.
struct ProcessWake : Codec<ProcessId1Payload, &ProcessId1Payload::process> {
    using Payload = ProcessId1Payload;
    static constexpr std::string_view name { "interp.process_wake" };
    static void dispatch(Scheduler&, const Payload&);
};

// queue_current: clears the queued flag and runs the process in the region it
// was queued from.
struct ProcessRunCurrent : Codec<ProcessId1Payload,
                                &ProcessId1Payload::process> {
    using Payload = ProcessId1Payload;
    static constexpr std::string_view name { "interp.process_run_current" };
    static void dispatch(Scheduler&, const Payload&);
};

// WaitRegion: a process resumes in a later scheduler region.
struct ProcessRegionResume : Codec<ProcessId1Payload,
                                 &ProcessId1Payload::process> {
    using Payload = ProcessId1Payload;
    static constexpr std::string_view name { "interp.process_region_resume" };
    static void dispatch(Scheduler&, const Payload&);
};

// A VHDL process woken by SystemVerilog work: first hop (generic next delta)
// that queues the process into the SystemVerilog region `phase`, where the
// ProcessWake kind runs it (no standard orders zero-delay events across
// languages; fsim follows Vivado, S16).
struct ProcessDomainCrossPayload {
    Impl* owner { };
    ProcessId process { };
    SchedulerPhase phase { SchedulerPhase::active };
};
struct ProcessWakeDomainCross : Codec<ProcessDomainCrossPayload,
                                    &ProcessDomainCrossPayload::process,
                                    &ProcessDomainCrossPayload::phase> {
    using Payload = ProcessDomainCrossPayload;
    static constexpr std::string_view name {
        "interp.process_wake_domain_cross" };
    static void dispatch(Scheduler&, const Payload&);
};

// Named-event notification (IEEE 1800-2023 15.5.1 ->> and event delays).
struct EventDeltaPayload {
    Impl* owner { };
    SignalId event { };
    std::uint64_t generation { };
};
struct EventNotifyDelta : Codec<EventDeltaPayload, &EventDeltaPayload::event,
                              &EventDeltaPayload::generation> {
    using Payload = EventDeltaPayload;
    static constexpr std::string_view name { "interp.event_notify_delta" };
    static void dispatch(Scheduler&, const Payload&);
};
struct EventTimedPayload {
    Impl* owner { };
    SignalId event { };
    std::uint64_t generation { };
    SimulationTick due { };
};
struct EventNotifyTimed : Codec<EventTimedPayload, &EventTimedPayload::event,
                              &EventTimedPayload::generation,
                              &EventTimedPayload::due> {
    using Payload = EventTimedPayload;
    static constexpr std::string_view name { "interp.event_notify_timed" };
    static void dispatch(Scheduler&, const Payload&);
};

// ---- Batch B: signal update and driver events ---------------------------

// A single owner pointer: the event stands for "run this owner's pending
// work", whose state is saved with the owner.
struct OwnerPayload {
    Impl* owner { };
};

// The Update-region commit of staged signal updates (IEEE 1800-2023 4.5, IEEE
// 1076-2019 14.7.5.3). The staged updates are scratch that a slot-end save
// finds empty.
struct UpdateCommit : Codec<OwnerPayload> {
    using Payload = OwnerPayload;
    static constexpr std::string_view name { "interp.update_commit" };
    static void dispatch(Scheduler&, const Payload&);
};

// A VHDL signal read again in the next cycle (S'STABLE, S'QUIET) re-resolves
// (IEEE 1076-2019 14.7.4).
struct VhdlNextCycleResolutions : Codec<OwnerPayload> {
    using Payload = OwnerPayload;
    static constexpr std::string_view name {
        "interp.vhdl_next_cycle_resolutions" };
    static void dispatch(Scheduler&, const Payload&);
};

// A null transaction disconnects a guarded driver (IEEE 1076-2019 10.5.2.2).
struct DriverDisconnectPayload {
    Impl* owner { };
    ProcessId process { };
    SignalId signal { };
};
struct DriverDisconnect : Codec<DriverDisconnectPayload,
                              &DriverDisconnectPayload::process,
                              &DriverDisconnectPayload::signal> {
    using Payload = DriverDisconnectPayload;
    static constexpr std::string_view name { "interp.driver_disconnect" };
    static void dispatch(Scheduler&, const Payload&);
};

// ---- Batch D: end-of-slot and report ------------------------------------

// $monitor publication at the end of the time slot (IEEE 1800-2023 21.2.3)
// and at the postponed region of a VHDL cycle.
struct MonitorPublishPayload {
    Impl* owner { };
    std::uint64_t generation { };
    ProcessId process { };
};
struct MonitorPublishSv : Codec<MonitorPublishPayload,
                              &MonitorPublishPayload::generation,
                              &MonitorPublishPayload::process> {
    using Payload = MonitorPublishPayload;
    static constexpr std::string_view name { "interp.monitor_publish_sv" };
    static void dispatch(Scheduler&, const Payload&);
};
struct MonitorPublishVhdl : Codec<MonitorPublishPayload,
                                &MonitorPublishPayload::generation,
                                &MonitorPublishPayload::process> {
    using Payload = MonitorPublishPayload;
    static constexpr std::string_view name { "interp.monitor_publish_vhdl" };
    static void dispatch(Scheduler&, const Payload&);
};

// $dumpvars / $dumpports start recording at the end of the time step (IEEE
// 1800-2023 21.7.1.3).
struct VcdBeginPayload {
    Impl* owner { };
    VcdControlKind kind { VcdControlKind::begin_variables };
};
struct VcdBegin : Codec<VcdBeginPayload, &VcdBeginPayload::kind> {
    using Payload = VcdBeginPayload;
    static constexpr std::string_view name { "interp.vcd_begin" };
    static void dispatch(Scheduler&, const Payload&);
};

// ---- Batch E: net timing and checks -------------------------------------

// A delayed copy of a timing-check signal (IEEE 1800-2023 clause 31, delayed
// reference and data signals of $setuphold and $recrem).
struct TimingDelayedCopyPayload {
    Impl* owner { };
    SignalId signal { };
    std::uint32_t offset { };
    Logic4 value { Logic4::x };
};
struct TimingDelayedCopy : Codec<TimingDelayedCopyPayload,
                               &TimingDelayedCopyPayload::signal,
                               &TimingDelayedCopyPayload::offset,
                               &TimingDelayedCopyPayload::value> {
    using Payload = TimingDelayedCopyPayload;
    static constexpr std::string_view name { "interp.timing_delayed_copy" };
    static void dispatch(Scheduler&, const Payload&);
};

// A timing-check window deadline (IEEE 1800-2023 clause 31).
// Cancelable: the check state holds the handle, rebound through
// bind_restored_event_handle with the check index as the key.
struct TimingCheckDeadlinePayload {
    Impl* owner { };
    std::uint64_t index { };
};
struct TimingCheckDeadline : Codec<TimingCheckDeadlinePayload,
                                 &TimingCheckDeadlinePayload::index> {
    using Payload = TimingCheckDeadlinePayload;
    static constexpr std::string_view name { "interp.timing_check_deadline" };
    static std::uint64_t cancel_key(const Payload& payload)
    {
        return payload.index;
    }
    static void dispatch(Scheduler&, const Payload&);
};

// A foreign write's delayed net-delay load matures: an SDF INTERCONNECT delay
// applied to a value written through VPI/VHPI (IEEE 1800-2023 Annex N).
struct ForeignNetDelayMaturePayload {
    Interpreter* owner { };
    SignalId signal { };
    std::uint64_t index { };
    std::uint64_t serial { };
};
struct ForeignNetDelayMature : ApiCodec<ForeignNetDelayMaturePayload,
                                   &ForeignNetDelayMaturePayload::signal,
                                   &ForeignNetDelayMaturePayload::index,
                                   &ForeignNetDelayMaturePayload::serial> {
    using Payload = ForeignNetDelayMaturePayload;
    static constexpr std::string_view name {
        "interp.foreign_net_delay_mature" };
    static void dispatch(Scheduler&, const Payload&);
};

// ---- Batch B/E, second part: delayed writes that wait in the future -----

// The key fields of an InertialDriverKey, with the owner. The digest below is
// the cancel key of the kinds that carry one, so a restart finds the pending
// record the event belongs to (bind_restored_event_handle).
struct DriverKeyPayload {
    Impl* owner { };
    ProcessId process { };
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    ProcessSchedulingDomain domain { ProcessSchedulingDomain::generic };
    SchedulerPhase phase { SchedulerPhase::active };
};

/// A 64-bit digest of an InertialDriverKey's fields. Two different keys can
/// collide only with the probability of any 64-bit hash; the binders pick the
/// first unbound record with a matching digest, so a collision binds a handle
/// to the wrong one of two pending records only if both are pending at once.
[[nodiscard]] std::uint64_t driver_key_digest(ProcessId process,
    SignalId signal, std::uint32_t offset, std::uint32_t width,
    ProcessSchedulingDomain domain, SchedulerPhase phase) noexcept;

[[nodiscard]] inline std::uint64_t driver_key_digest(
    const DriverKeyPayload& key) noexcept
{
    return driver_key_digest(key.process, key.signal, key.offset, key.width,
        key.domain, key.phase);
}

// An inertial delayed write matures (IEEE 1076-2019 10.5.2.2 inertial delay;
// IEEE 1800-2023 28.16 delays on primitives): the value is the pending
// record's source value, which the record keeps until the write matures.
struct InertialMaturePayload {
    Impl* owner { };
    ProcessId process { };
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    ProcessSchedulingDomain domain { ProcessSchedulingDomain::generic };
    SchedulerPhase phase { SchedulerPhase::active };
    bool has_offset { };
};
struct InertialMature : Codec<InertialMaturePayload,
                           &InertialMaturePayload::process,
                           &InertialMaturePayload::signal,
                           &InertialMaturePayload::offset,
                           &InertialMaturePayload::width,
                           &InertialMaturePayload::domain,
                           &InertialMaturePayload::phase,
                           &InertialMaturePayload::has_offset> {
    using Payload = InertialMaturePayload;
    static constexpr std::string_view name { "interp.inertial_mature" };
    static std::uint64_t cancel_key(const Payload& payload)
    {
        return driver_key_digest(payload.process, payload.signal,
            payload.offset, payload.width, payload.domain, payload.phase);
    }
    static void dispatch(Scheduler&, const Payload&);
};

// A module-path delayed bit matures (IEEE 1800-2023 30.4 path delays).
struct ModulePathMature : Codec<DriverKeyPayload, &DriverKeyPayload::process,
                            &DriverKeyPayload::signal,
                            &DriverKeyPayload::offset,
                            &DriverKeyPayload::width,
                            &DriverKeyPayload::domain,
                            &DriverKeyPayload::phase> {
    using Payload = DriverKeyPayload;
    static constexpr std::string_view name { "interp.module_path_mature" };
    static std::uint64_t cancel_key(const Payload& payload)
    {
        return driver_key_digest(payload);
    }
    static void dispatch(Scheduler&, const Payload&);
};

// A module-path pulse too short to pass but wider than the reject limit
// reaches the load as x (IEEE 1800-2023 30.7 pulse filtering).
struct ModulePathPulseX : Codec<DriverKeyPayload, &DriverKeyPayload::process,
                            &DriverKeyPayload::signal,
                            &DriverKeyPayload::offset,
                            &DriverKeyPayload::width,
                            &DriverKeyPayload::domain,
                            &DriverKeyPayload::phase> {
    using Payload = DriverKeyPayload;
    static constexpr std::string_view name { "interp.module_path_pulse_x" };
    static void dispatch(Scheduler&, const Payload&);
};

// The changes of an interconnect delay's driver that mature at this time
// (IEEE 1800-2023 30.5; schedule_net_delay). One event stands for one wake of
// the pending record; a record has one wake per distinct maturity time and
// the binder pairs wakes and events in time order.
struct NetDelayMature : Codec<DriverKeyPayload, &DriverKeyPayload::process,
                           &DriverKeyPayload::signal,
                           &DriverKeyPayload::offset,
                           &DriverKeyPayload::width,
                           &DriverKeyPayload::domain,
                           &DriverKeyPayload::phase> {
    using Payload = DriverKeyPayload;
    static constexpr std::string_view name { "interp.net_delay_mature" };
    static std::uint64_t cancel_key(const Payload& payload)
    {
        return driver_key_digest(payload);
    }
    static void dispatch(Scheduler&, const Payload&);
};

// One projected (VHDL transport or inertial) transaction of one scalar
// element matures (IEEE 1076-2019 10.5.2.2, 14.7.5.3). The id is unique.
struct ProjectedTransactionPayload {
    Impl* owner { };
    ProcessId process { };
    SignalId signal { };
    std::uint32_t offset { };
    std::uint64_t id { };
};
struct ProjectedTransactionMature
    : Codec<ProjectedTransactionPayload,
          &ProjectedTransactionPayload::process,
          &ProjectedTransactionPayload::signal,
          &ProjectedTransactionPayload::offset,
          &ProjectedTransactionPayload::id> {
    using Payload = ProjectedTransactionPayload;
    static constexpr std::string_view name {
        "interp.projected_transaction_mature" };
    static std::uint64_t cancel_key(const Payload& payload)
    {
        return payload.id;
    }
    static void dispatch(Scheduler&, const Payload&);
};

// A multi-bit projected assignment with one element matures: its bits carry
// the consecutive transaction ids `first_id` ... `first_id + width - 1`; the
// value is read back from those transactions.
struct ProjectedGroupPayload {
    Impl* owner { };
    ProcessId process { };
    SignalId signal { };
    std::uint32_t first { };
    std::uint32_t width { };
    std::uint64_t first_id { };
};
struct ProjectedGroupMature
    : Codec<ProjectedGroupPayload, &ProjectedGroupPayload::process,
          &ProjectedGroupPayload::signal, &ProjectedGroupPayload::first,
          &ProjectedGroupPayload::width, &ProjectedGroupPayload::first_id> {
    using Payload = ProjectedGroupPayload;
    static constexpr std::string_view name {
        "interp.projected_group_mature" };
    static void dispatch(Scheduler&, const Payload&);
};

// A trireg net's stored charge decays to z (IEEE 1800-2023 28.16.2, 6.6.4).
// One pending decay per net, so the signal is the cancel key.
struct ChargeDecayPayload {
    Impl* owner { };
    SignalId signal { };
    ProcessSchedulingDomain domain { ProcessSchedulingDomain::generic };
    SchedulerPhase phase { SchedulerPhase::active };
};
struct ChargeDecay : Codec<ChargeDecayPayload, &ChargeDecayPayload::signal,
                         &ChargeDecayPayload::domain,
                         &ChargeDecayPayload::phase> {
    using Payload = ChargeDecayPayload;
    static constexpr std::string_view name { "interp.charge_decay" };
    static std::uint64_t cancel_key(const Payload& payload)
    {
        return payload.signal;
    }
    static void dispatch(Scheduler&, const Payload&);
};

// ---- Next-delta wake of a static sensitivity cohort -----------------------

// The generic processes of a cohort that wait on one signal wake together in
// the next delta (IEEE 1076-2019 14.7.5.3). The members are the cohort's
// pending snapshot, saved with the host state.
struct CohortWakePayload {
    Impl* owner { };
    std::uint64_t cohort { };
};
struct CohortWake : Codec<CohortWakePayload, &CohortWakePayload::cohort> {
    using Payload = CohortWakePayload;
    static constexpr std::string_view name { "interp.cohort_wake" };
    static void dispatch(Scheduler&, const Payload&);
};

} // namespace events
} // namespace fsim::runtime::simir

// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 hosted tier (docs/engine-v4-semantics.md S25). A hosted process is
// a SystemVerilog process the kernel schedules but does not evaluate: it keeps
// its own ProcessState and its operations run on the reference evaluator
// (Impl::execute), exactly as they would on the host scheduler. What moves is
// the scheduling. The host scheduler's queue_* entry points hand every wake of
// a hosted process to enqueue_hosted, and the kernel's activation loop runs the
// queued processes where the reference scheduler would:
//
// - Active region: every round of the loop runs the processes woken so far in
//   stable ProcessId order (the order the host scheduler sorts an Active
//   round by); what they wake runs in the next round (IEEE 1800-2023 4.4,
//   4.7).
// - Inactive region (#0): a host task of that region re-enters the kernel once
//   the host's own Active work is done, and the kernel then runs the #0
//   resumptions before any NBA commit (4.4, 4.5).
// - Next delta: wakes from another scheduling domain, and the time-zero late
//   starters, run in a later delta than the one that woke them; the kernel
//   yields to the host's next delta to release them.
// - Timers: a heap keyed (time, stable order) carrying the process's
//   timer_epoch, armed as a host task at the earliest time; a disable that
//   redirects the process advances the epoch and abandons its timer wake.
//
// Signals a hosted process reads or writes stay host signals (the planner's
// host_touched), so every read and write takes the reference path and the
// kernel sees the writes through scan_inputs and publish, as it does for any
// host process.
#include "simir_static_kernel_state.hpp"

#include "fsim/runtime/scheduler.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

namespace {

/// The host scheduler's order within a round: by stable order, then id.
template <typename Ready>
void sort_hosted(std::vector<Ready>& ready)
{
    std::ranges::sort(ready, [](const Ready& left, const Ready& right) {
        return left.order != right.order ? left.order < right.order
                                         : left.process < right.process;
    });
}

} // namespace

void Interpreter::Impl::static_kernel_enqueue_hosted(const ProcessId id,
    const StaticKernelHostedWake wake, const SimulationTick time,
    const StableOrder order)
{
    static_kernel->enqueue_hosted(id, wake, time, order);
}

void Interpreter::Impl::static_kernel_adopt_fork_child(
    const ProcessId parent, const ProcessId child)
{
    if (!static_kernel_hosts(parent)) {
        return;
    }
    if (static_kernel_hosted_process.size() <= child) {
        static_kernel_hosted_process.resize(
            static_cast<std::size_t>(child) + 1U, 0U);
    }
    static_kernel_hosted_process[child] = 1U;
}

void Interpreter::Impl::StaticKernel::adopt_hosted(
    const std::vector<StaticKernelMemberSpec>& hosted)
{
    const auto count = impl_.processes.size();
    for (const auto& entry : hosted) {
        const auto id = entry.process;
        if (id >= count || id == host_process_) {
            continue;
        }
        // Only a full-state process is hosted: a compact constant driver
        // keeps its own scheduling. The planner chose the SystemVerilog
        // processes of every region (S25); the process's execution phase says
        // which region list its wakes join.
        const auto* state
            = std::as_const(impl_.processes).full_state_if_present(id);
        if (state == nullptr) {
            continue;
        }
        // A SystemVerilog kernel hosts SystemVerilog processes and a VHDL or
        // mixed kernel hosts VHDL (generic domain) processes, which run in
        // its delta rounds (S25a): Active, or postponed for a postponed
        // process.
        const bool systemverilog = impl_.processes.scheduling_domain(id)
            == ProcessSchedulingDomain::systemverilog;
        // The one SystemVerilog-domain process a VHDL kernel hosts is an
        // implicit-signal driver (no initial run; the planner chose it).
        const bool implicit_driver = systemverilog && vhdl_
            && !impl_.processes.program_view(id).initialize()
            && state->execution_phase == SchedulerPhase::active;
        // A postponed VHDL process (1076-2019 11.3, 14.7.5.3) waits in the
        // postponed region list, the end of the time step (S25a).
        if ((systemverilog == vhdl_ && !implicit_driver)
            || (!systemverilog
                && state->execution_phase != SchedulerPhase::active
                && state->execution_phase != SchedulerPhase::postponed)) {
            continue;
        }
        if (impl_.static_kernel_hosted_process.size() != count) {
            impl_.static_kernel_hosted_process.assign(count, 0U);
        }
        impl_.static_kernel_hosted_process[id] = 1U;
        has_hosted_ = true;
    }
}

void Interpreter::Impl::StaticKernel::enqueue_hosted(const ProcessId id,
    const StaticKernelHostedWake wake, const SimulationTick time,
    const StableOrder order)
{
    auto& process = impl_.get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
    HostedReady ready;
    ready.process = id;
    ready.order = id;
    ready.wake = true;
    // The host scheduler queues a wake in the process's own region
    // (queue_at, queue_next_delta, queue_active_current), except the current
    // region (queue_current) and the Inactive region (a #0 resumption).
    ready.phase = process.execution_phase;
    switch (wake) {
    case StaticKernelHostedWake::current:
        // queue_current does not clear the process's wait registrations.
        ready.wake = false;
        if (hosted_running_region_) {
            ready.phase = *hosted_running_region_;
            push_hosted_region(ready);
            return;
        }
        ready.phase = SchedulerPhase::active;
        (hosted_running_inactive_ ? hosted_inactive_ : hosted_active_)
            .push_back(ready);
        break;
    case StaticKernelHostedWake::active:
        process.timer_wake = false;
        if (ready.phase != SchedulerPhase::active) {
            push_hosted_region(ready);
            return;
        }
        hosted_active_.push_back(ready);
        break;
    case StaticKernelHostedWake::next_delta:
        process.timer_wake = false;
        if (ready.phase == SchedulerPhase::postponed
            && impl_.processes.scheduling_domain(id)
                != ProcessSchedulingDomain::systemverilog) {
            // A postponed VHDL process resumes in the last delta cycle of
            // the time step (1076-2019 14.7.5.3), not in the next delta: the
            // host scheduler puts its wake in the end-of-slot queue at once.
            push_hosted_region(ready);
            return;
        }
        hosted_next_delta_.push_back(
            { impl_.scheduler.now(), impl_.scheduler.delta(), ready });
        break;
    case StaticKernelHostedWake::inactive: {
        // A #0 resumption of a reactive-region process runs in the
        // Re-Inactive region (IEEE 1800-2023 4.4, 4.5; the scheduler maps
        // an Inactive entry made while a reactive region runs).
        const auto running = impl_.scheduler.current_phase();
        if (running && *running >= SchedulerPhase::reactive
            && *running <= SchedulerPhase::re_update) {
            ready.phase = SchedulerPhase::re_inactive;
            push_hosted_region(ready);
            return;
        }
        ready.phase = SchedulerPhase::inactive;
        hosted_inactive_.push_back(ready);
        break;
    }
    case StaticKernelHostedWake::at: {
        ready.order = order;
        ready.timer = true;
        ready.epoch = process.timer_epoch;
        const bool future = time > impl_.scheduler.now();
        process.timer_wake = future;
        if (!future) {
            if (ready.phase != SchedulerPhase::active) {
                push_hosted_region(ready);
                return;
            }
            hosted_active_.push_back(ready);
            break;
        }
        hosted_timers_.push({ time, hosted_sequence_++, ready });
        if (!in_activation_) {
            arm_hosted_timer();
        }
        return;
    }
    }
    // Inside an activation (including the first, which initializes the
    // kernel) the loop, or finish_hosted after it, picks the wake up. Before
    // the first activation the host process starts at time zero anyway. The
    // first activation can already have run when start() queues the
    // time-zero processes (the host stub's static wait is prearmed), so a
    // due wake after it needs the host like any other.
    if (in_activation_ || !initialized_) {
        return;
    }
    switch (wake) {
    case StaticKernelHostedWake::current:
    case StaticKernelHostedWake::active:
    case StaticKernelHostedWake::at:
        // A due timer (`at` returns above for a future one) is Active work.
        impl_.queue_active_current(host_process_);
        break;
    case StaticKernelHostedWake::next_delta:
        impl_.queue_next_delta(host_process_);
        break;
    case StaticKernelHostedWake::inactive:
        defer_hosted_inactive();
        break;
    }
}

// ---- Region model (S25, Phase 4) ------------------------------------------
// Hosted processes of the regions after the NBA region wait in per-region
// lists. Each non-empty list has a doorbell, a host task of that phase: the
// host scheduler orders the regions (it restarts at the first non-empty
// region after every region it runs, begin_systemverilog_batch), so the
// kernel's lists run exactly where the reference scheduler runs those wakes,
// after the kernel's own Active and NBA work and the host's earlier regions.

void Interpreter::Impl::StaticKernel::push_hosted_region(
    const HostedReady& ready)
{
    hosted_regions_[static_cast<std::size_t>(ready.phase)].push_back(ready);
    ring_hosted_region(ready.phase);
}

void Interpreter::Impl::StaticKernel::route_hosted(const HostedReady& ready)
{
    if (ready.phase == SchedulerPhase::active) {
        hosted_active_.push_back(ready);
    } else {
        push_hosted_region(ready);
    }
}

bool Interpreter::Impl::StaticKernel::enqueue_hosted_region(
    const ProcessId id, const SchedulerPhase phase)
{
    switch (phase) {
    case SchedulerPhase::observed:
    case SchedulerPhase::reactive:
    case SchedulerPhase::re_inactive:
    case SchedulerPhase::postponed:
        break;
    default:
        return false;
    }
    auto& process = impl_.get_process(id);
    if (process.halted || process.queued) {
        return true;
    }
    process.queued = true;
    HostedReady ready;
    ready.process = id;
    ready.order = id;
    // The scheduler task of a WaitRegion clears queued only.
    ready.wake = false;
    ready.phase = phase;
    push_hosted_region(ready);
    return true;
}

void Interpreter::Impl::StaticKernel::ring_hosted_region(
    const SchedulerPhase phase)
{
    const auto index = static_cast<std::size_t>(phase);
    if (hosted_region_bell_[index]) {
        return;
    }
    hosted_region_bell_[index] = true;
    struct HostedRegionTask {
        Interpreter::Impl* owner { };
        ProcessId host { };
        SchedulerPhase phase { };
    };
    const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
        HostedRegionTask,
        +[](Scheduler&, const HostedRegionTask& scheduled) {
            auto& kernel = *scheduled.owner->static_kernel;
            kernel.hosted_region_bell_[static_cast<std::size_t>(
                scheduled.phase)] = false;
            kernel.run_hosted_region(scheduled.phase);
        }>(HostedRegionTask { &impl_, host_process_, phase });
    impl_.scheduler.schedule_internal_systemverilog_at(
        impl_.scheduler.now(), phase, host_process_, task);
}

void Interpreter::Impl::StaticKernel::run_hosted_region(
    const SchedulerPhase phase)
{
    auto& list = hosted_regions_[static_cast<std::size_t>(phase)];
    if (list.empty()) {
        return;
    }
    auto ready = std::move(list);
    list.clear();
    sort_hosted(ready);
    struct Running {
        std::optional<SchedulerPhase>& slot;
        std::optional<SchedulerPhase> saved;
        Running(std::optional<SchedulerPhase>& target, const SchedulerPhase now)
            : slot(target), saved(target)
        {
            slot = now;
        }
        ~Running() { slot = saved; }
    } running { hosted_running_region_, phase };
    for (std::size_t position = 0U; position < ready.size(); ++position) {
        if (impl_.scheduler.stop_requested()) {
            // $stop or $finish: the rest is kept for a resumed run.
            auto& rest = hosted_regions_[static_cast<std::size_t>(phase)];
            rest.insert(rest.end(),
                ready.begin() + static_cast<std::ptrdiff_t>(position),
                ready.end());
            ring_hosted_region(phase);
            break;
        }
        run_hosted(ready[position]);
    }
}

void Interpreter::Impl::StaticKernel::execute_hosted_startup(
    const ProcessId id)
{
    impl_.execute(id);
}

void Interpreter::Impl::StaticKernel::discard_hosted()
{
    hosted_active_.clear();
    hosted_inactive_.clear();
    hosted_next_delta_.clear();
    for (auto& list : hosted_regions_) {
        list.clear();
    }
    hosted_region_bell_.fill(false);
    hosted_timers_ = decltype(hosted_timers_) { };
    armed_hosted_timer_.reset();
    hosted_inactive_deferred_ = false;
    nonblocking_deferred_ = false;
    nonblocking_region_ = false;
    in_host_nonblocking_region_ = false;
    // The discarded host tasks can no longer run: the kernel's host process
    // is not queued any more.
    impl_.get_process(host_process_).queued = false;
}

void Interpreter::Impl::StaticKernel::run_finals(const bool discarded)
{
    // Final blocks read the values the design holds when the simulation
    // ends (IEEE 1800-2023 9.2.3), including those a last activation
    // staged for a region that $finish or $stop discarded.
    publish(true, true);
    if (!has_hosted_) {
        return;
    }
    if (discarded) {
        discard_hosted();
    }
    for (ProcessId id = 0; id < impl_.processes.size(); ++id) {
        if (impl_.static_kernel_hosts(id)
            && impl_.processes.program_view(id).final()) {
            impl_.queue_at(id, impl_.scheduler.now());
        }
    }
}

void Interpreter::Impl::StaticKernel::collect_hosted()
{
    const auto now = impl_.scheduler.now();
    while (!hosted_timers_.empty() && hosted_timers_.top().time <= now) {
        const auto timer = hosted_timers_.top();
        hosted_timers_.pop();
        const auto* state = std::as_const(impl_.processes)
                                .full_state_if_present(timer.ready.process);
        // A disable that redirected the process abandoned this wake.
        if (state != nullptr && state->timer_epoch == timer.ready.epoch) {
            route_hosted(timer.ready);
        }
    }
    if (hosted_next_delta_.empty()) {
        return;
    }
    // A next-delta wake runs in a delta after the one that queued it.
    const auto delta = impl_.scheduler.delta();
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < hosted_next_delta_.size(); ++index) {
        const auto& entry = hosted_next_delta_[index];
        if (entry.time == now && entry.delta == delta) {
            if (kept != index) {
                hosted_next_delta_[kept] = entry;
            }
            ++kept;
        } else {
            route_hosted(entry.ready);
        }
    }
    hosted_next_delta_.resize(kept);
}


bool Interpreter::Impl::StaticKernel::run_hosted_active()
{
    // Hosted Active work waits for the host's Active region when the kernel
    // runs in a host Inactive or NBA task (scheduler.current_phase() is not
    // Active there); finish_hosted wakes the host for it.
    if (hosted_active_.empty() || in_host_nonblocking_region_
        || hosted_inactive_task_) {
        return false;
    }
    auto ready = std::move(hosted_active_);
    hosted_active_.clear();
    sort_hosted(ready);
    for (std::size_t position = 0U; position < ready.size(); ++position) {
        if (impl_.scheduler.stop_requested()) {
            // $stop or $finish: the rest is kept for a resumed run.
            hosted_active_.insert(hosted_active_.end(),
                ready.begin() + static_cast<std::ptrdiff_t>(position),
                ready.end());
            break;
        }
        run_hosted(ready[position]);
    }
    return true;
}

bool Interpreter::Impl::StaticKernel::run_hosted_inactive()
{
    if (hosted_inactive_.empty()) {
        return false;
    }
    auto ready = std::move(hosted_inactive_);
    hosted_inactive_.clear();
    sort_hosted(ready);
    struct Running {
        bool& flag;
        explicit Running(bool& value) : flag(value) { flag = true; }
        ~Running() { flag = false; }
    } running { hosted_running_inactive_ };
    for (std::size_t position = 0U; position < ready.size(); ++position) {
        if (impl_.scheduler.stop_requested()) {
            hosted_inactive_.insert(hosted_inactive_.end(),
                ready.begin() + static_cast<std::ptrdiff_t>(position),
                ready.end());
            break;
        }
        run_hosted(ready[position]);
    }
    return true;
}

void Interpreter::Impl::StaticKernel::run_hosted_initializers()
{
    // Setting the initial value of a static variable in its declaration
    // happens before any initial or always procedure starts (IEEE 1800-2023
    // 6.8, 10.5). The host scheduler starts the initializer processes in
    // process order ahead of the procedures; the kernel's first activation
    // evaluates its members from initialize(), so the hosted initializers
    // run first, whether or not start() has queued them yet (they finish
    // and halt, so a later wake is dropped).
    std::vector<ProcessId> initializers;
    for (ProcessId id = 0U; id < impl_.static_kernel_hosted_process.size();
         ++id) {
        if (impl_.static_kernel_hosted_process[id] == 0U
            || impl_.get_process(id).halted) {
            continue;
        }
        const auto program = impl_.processes.program_view(id);
        if (program.initialize()
            && program.name().find(".$declaration_initializer_")
                != std::string::npos) {
            initializers.push_back(id);
        }
    }
    if (initializers.empty()) {
        return;
    }
    std::erase_if(hosted_active_, [&](const HostedReady& ready) {
        return std::ranges::binary_search(initializers, ready.process);
    });
    for (const auto id : initializers) {
        HostedReady ready;
        ready.process = id;
        ready.order = id;
        ready.wake = true;
        run_hosted(ready);
    }
}

void Interpreter::Impl::StaticKernel::run_hosted(const HostedReady& ready)
{
    auto& state = impl_.get_process(ready.process);
    if (ready.timer && state.timer_epoch != ready.epoch) {
        return;
    }
    // What the host scheduler's task for this wake does before it executes the
    // process (queue_next_delta, queue_at).
    state.queued = false;
    if (ready.timer) {
        state.timer_wake = false;
    }
    if (ready.wake) {
        state.waiting_on_static = false;
        impl_.remove_dynamic_wait(state);
    }
    // Not re-entrant: execute runs the process to its next suspension, and
    // every wake it causes is queued above, never run from here.
    impl_.execute(ready.process);
}

void Interpreter::Impl::StaticKernel::defer_hosted_inactive()
{
    if (hosted_inactive_deferred_) {
        return;
    }
    hosted_inactive_deferred_ = true;
    struct HostedInactiveTask {
        Interpreter::Impl* owner { };
        ProcessId host { };
    };
    const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
        HostedInactiveTask,
        +[](Scheduler&, const HostedInactiveTask& scheduled) {
            auto& kernel = *scheduled.owner->static_kernel;
            kernel.hosted_inactive_deferred_ = false;
            kernel.hosted_inactive_task_ = true;
            kernel.hosted_inactive_region_ = true;
            // activate() clears both flags when it ends, also on a throw.
            const bool yielded = kernel.activate();
            if (yielded) {
                scheduled.owner->queue_next_delta(scheduled.host);
            }
        }>(HostedInactiveTask { &impl_, host_process_ });
    impl_.scheduler.schedule_internal_systemverilog_at(impl_.scheduler.now(),
        SchedulerPhase::inactive, host_process_, task);
}

void Interpreter::Impl::StaticKernel::arm_hosted_timer()
{
    while (!hosted_timers_.empty()) {
        const auto* state = std::as_const(impl_.processes)
                                .full_state_if_present(
                                    hosted_timers_.top().ready.process);
        if (state != nullptr
            && state->timer_epoch == hosted_timers_.top().ready.epoch) {
            break;
        }
        hosted_timers_.pop();
    }
    if (hosted_timers_.empty()) {
        return;
    }
    const auto time = hosted_timers_.top().time;
    const auto now = impl_.scheduler.now();
    if (time <= now) {
        // Due already (a stopped run resumed): activate once more.
        impl_.queue_active_current(host_process_);
        return;
    }
    // A task already pending at this time, or at an earlier one (which
    // re-arms when it runs), covers the timer.
    if (armed_hosted_timer_ && *armed_hosted_timer_ <= time
        && *armed_hosted_timer_ > now) {
        return;
    }
    armed_hosted_timer_ = time;
    struct HostedTimerTask {
        Interpreter::Impl* owner { };
        SimulationTick time { };
    };
    const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
        HostedTimerTask,
        +[](Scheduler&, const HostedTimerTask& scheduled) {
            if (scheduled.owner->static_kernel) {
                scheduled.owner->static_kernel->hosted_timer_due(
                    scheduled.time);
            }
        }>(HostedTimerTask { &impl_, time });
    impl_.scheduler.schedule_internal_systemverilog_at(
        time, SchedulerPhase::active, host_process_, task);
}

void Interpreter::Impl::StaticKernel::hosted_timer_due(
    const SimulationTick time)
{
    if (armed_hosted_timer_ && *armed_hosted_timer_ == time) {
        armed_hosted_timer_.reset();
    }
    // The kernel runs as its host process; due timers are collected when it
    // activates.
    impl_.queue_active_current(host_process_);
}

void Interpreter::Impl::StaticKernel::finish_hosted()
{
    // The kernel already yields for another reason (time-zero late starters,
    // threads): the next delta serves hosted Active work too, rather than an
    // activation in this delta that would start the late starters early.
    const bool yielding = yield_requested_;
    if (!hosted_next_delta_.empty()) {
        yield_requested_ = true;
    }
    if (!hosted_active_.empty() && !yielding) {
        // Woken by this activation's publication, left behind by $stop or
        // $finish, or held back in a host Inactive or NBA task: the host's
        // Active region runs it (before the NBA region, 4.4).
        impl_.queue_active_current(host_process_);
    }
    if (!hosted_inactive_.empty() && !hosted_inactive_deferred_) {
        defer_hosted_inactive();
    }
    arm_hosted_timer();
}

} // namespace fsim::runtime::simir

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
#include <functional>
#include <iostream>
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

bool Interpreter::Impl::static_kernel_host(const ProcessId id) const noexcept
{
    return static_kernel && static_kernel->is_host(id);
}

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

bool Interpreter::Impl::run_hosted_tier(ProcessState& process)
{
    // A compiled simulation runs every process on native code: the
    // evaluator's loop is left for the diagnostic modes that count its
    // operations.
    if (!static_kernel || process_profile_enabled
        || process.cold().track_interpreter_operations) {
        return false;
    }
    return static_kernel->run_hosted_tier(process);
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
        // The SystemVerilog-domain processes a VHDL kernel hosts are
        // Active-region ones (implicit-signal drivers and Verilog processes
        // the planner chose); they run in its delta rounds like VHDL ones.
        const bool active_systemverilog = systemverilog && vhdl_
            && state->execution_phase == SchedulerPhase::active;
        // A postponed VHDL process (1076-2019 11.3, 14.7.5.3) waits in the
        // postponed region list, the end of the time step (S25a).
        if ((systemverilog == vhdl_ && !active_systemverilog)
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
    build_hosted_tiers();
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
    // The native threads' pending resumptions are discarded with the host's
    // events: after $finish or $stop only the final blocks run (IEEE
    // 1800-2023 9.2.3), and a round left to resume behind host work has
    // no host work left to follow.
    for (auto* threads : { &ready_threads_, &batch_threads_,
             &inactive_threads_ }) {
        for (const auto thread : *threads) {
            (void)take_ready(thread);
        }
        threads->clear();
    }
    batch_hosted_.clear();
    host_batch_resume_.reset();
    batch_resume_queued_ = false;
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
    if (has_hosted_) {
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
    // A final member runs in its process's place among the final blocks,
    // as the host orders them (process order), so a $finish stops the ones
    // after it: among the hosted finals in the kernel's sorted Active list,
    // or as a host task of its own.
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        if (!members_[index].final_block) {
            continue;
        }
        const auto process = members_[index].process;
        final_members_.emplace_back(process, index);
        if (has_hosted_) {
            HostedReady ready;
            ready.process = process;
            ready.order = process;
            ready.phase = SchedulerPhase::active;
            hosted_active_.push_back(ready);
            impl_.queue_active_current(host_process_);
            continue;
        }
        struct KernelFinalTask {
            Interpreter::Impl* owner { };
            std::uint32_t member { };
        };
        const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
            KernelFinalTask,
            +[](Scheduler&, const KernelFinalTask& scheduled) {
                if (scheduled.owner->static_kernel) {
                    scheduled.owner->static_kernel->run_final_member(
                        scheduled.member);
                }
            }>(KernelFinalTask { &impl_, index });
        impl_.scheduler.schedule_internal_systemverilog_at(impl_.scheduler.now(),
            SchedulerPhase::active, StableOrder { process }, task);
    }
}

void Interpreter::Impl::StaticKernel::run_final_member(const std::uint32_t member)
{
    // A final procedure has no timing control and writes no signal (the
    // planner's final rule): it runs to its end or to a $finish.
    run_thread(spawn_thread(member, 0U, static_kernel_detail::no_slot,
        static_kernel_detail::no_slot));
}

void Interpreter::Impl::StaticKernel::run_startup_member(const ProcessId process)
{
    const auto entry = std::ranges::find(startup_members_, process,
        &StartupMember::process);
    if (entry == startup_members_.end()) {
        throw std::logic_error("static kernel startup member is not installed");
    }
    // A package static initializer neither waits nor forks (the planner's
    // startup rule): it runs to its end or to a $finish.
    const auto thread = spawn_thread(entry->member, 0U,
        static_kernel_detail::no_slot, static_kernel_detail::no_slot);
    run_thread(thread);
    if (threads_[thread].alive && threads_[thread].member == entry->member
        && !impl_.stopped_by_design) {
        throw std::logic_error("package static initializer did not finish synchronously");
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
        if (host_batch_ahead(ready[position].order)) {
            // run_merged_active resumes the rest after the host's tasks.
            batch_hosted_.assign(
                ready.begin() + static_cast<std::ptrdiff_t>(position),
                ready.end());
            host_batch_resume_ = ready[position].order;
            break;
        }
        run_hosted(ready[position]);
    }
    return true;
}

bool Interpreter::Impl::StaticKernel::hosted_systemverilog(
    const ProcessId process) const
{
    return impl_.processes.scheduling_domain(process)
        == ProcessSchedulingDomain::systemverilog;
}

bool Interpreter::Impl::StaticKernel::hosted_systemverilog_pending() const
{
    return mixed_ && !in_host_nonblocking_region_ && !hosted_inactive_task_
        && std::ranges::any_of(hosted_active_, [&](const HostedReady& ready) {
               return hosted_systemverilog(ready.process);
           });
}

bool Interpreter::Impl::StaticKernel::run_hosted_active_domain(
    const bool systemverilog)
{
    // A mixed kernel runs its hosted processes by domain: SystemVerilog ones
    // in the delta's SystemVerilog Active region, VHDL ones in its round.
    if (!mixed_) {
        return run_hosted_active();
    }
    if (hosted_active_.empty() || in_host_nonblocking_region_
        || hosted_inactive_task_) {
        return false;
    }
    std::vector<HostedReady> ready;
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < hosted_active_.size(); ++index) {
        if (hosted_systemverilog(hosted_active_[index].process) == systemverilog) {
            ready.push_back(hosted_active_[index]);
        } else {
            hosted_active_[kept++] = hosted_active_[index];
        }
    }
    hosted_active_.resize(kept);
    if (ready.empty()) {
        return false;
    }
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

bool Interpreter::Impl::StaticKernel::yield_to_host_active()
{
    // The reference runs an Active region as batches in stable order: what
    // its processes queue while one batch runs (the rest of that batch, a
    // zero-delay update maturing) runs before the processes the batch woke,
    // which wait for the next one (IEEE 1800-2023 4.5, 4.7). After an
    // iteration that ran hosted processes, host Active work left pending
    // runs first; the kernel resumes after it in the same region.
    if (in_host_nonblocking_region_ || hosted_inactive_task_
        || impl_.scheduler.current_phase() != SchedulerPhase::active
        || !impl_.scheduler.systemverilog_active_region_pending()) {
        return false;
    }
    host_active_yield_ = true;
    return true;
}

bool Interpreter::Impl::StaticKernel::host_batch_ahead(
    const StableOrder order) const
{
    // The host runs an Active batch in stable order, first-queued first
    // among equal orders (IEEE 1800-2023 4.7): its tasks left in the batch
    // ahead of the kernel's next process run before it.
    // The kernel's own activations (at its host process's order) are not
    // host work: an earlier one finds nothing left to do.
    if (mixed_ || vhdl_ || in_host_nonblocking_region_
        || hosted_inactive_task_) {
        return false;
    }
    return impl_.scheduler.systemverilog_running_active_ahead(
        order, host_process_);
}

void Interpreter::Impl::StaticKernel::queue_batch_resume(
    const StableOrder order)
{
    // One resumption at a time: an activation that finds the round's rest
    // still behind host work leaves it to the queued one.
    if (batch_resume_queued_) {
        return;
    }
    batch_resume_queued_ = true;
    struct BatchResumeTask {
        Interpreter::Impl* owner { };
    };
    const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
        BatchResumeTask,
        +[](Scheduler&, const BatchResumeTask& scheduled) {
            if (auto& kernel = scheduled.owner->static_kernel) {
                kernel->batch_resume_queued_ = false;
                if (kernel->activate()) {
                    scheduled.owner->queue_next_delta(kernel->host_process_);
                }
            }
        }>(BatchResumeTask { &impl_ });
    impl_.scheduler.schedule_internal_systemverilog_running_active(
        order, task);
}

StableOrder Interpreter::Impl::StaticKernel::host_resume_order() const
{
    if (mixed_ || vhdl_) {
        return host_process_;
    }
    std::optional<StableOrder> order;
    const auto consider = [&](const StableOrder candidate) {
        if (!order || candidate < *order) {
            order = candidate;
        }
    };
    for (const auto& ready : hosted_active_) {
        consider(ready.order);
    }
    for (const auto thread : ready_threads_) {
        consider(threads_[thread].order);
    }
    return order.value_or(host_process_);
}

void Interpreter::Impl::StaticKernel::requeue_host_active()
{
    // The kernel's Active work resumes in the host's batch at the stable
    // order of its first process, so the host tasks queued before it
    // (zero-delay updates maturing, processes woken earlier) keep their
    // place ahead of or behind it.
    impl_.queue_active_current_at(host_process_, host_resume_order());
}

void Interpreter::Impl::StaticKernel::run_merged_active()
{
    // The reference scheduler runs a round's Active processes in their
    // stable order (S18). A hosted process reads the host's copy of the
    // kernel's outputs, so the blocking writes made before it are published
    // first, and its own blocking writes are events for the native processes
    // after it (IEEE 1800-2023 4.7, 9.4.2). What the round wakes runs in the
    // next one.
    // A round stopped for host tasks ahead of it in the running batch
    // resumes first; what it woke waits for the next round.
    std::vector<HostedReady> hosted;
    std::vector<std::uint32_t> threads;
    if (!batch_hosted_.empty() || !batch_threads_.empty()) {
        hosted = std::move(batch_hosted_);
        batch_hosted_.clear();
        threads = std::move(batch_threads_);
        batch_threads_.clear();
    } else if (mixed_) {
        // A mixed kernel's hosted VHDL processes wait for its round.
        std::size_t kept = 0U;
        for (std::size_t index = 0U; index < hosted_active_.size(); ++index) {
            if (hosted_systemverilog(hosted_active_[index].process)) {
                hosted.push_back(hosted_active_[index]);
            } else {
                hosted_active_[kept++] = hosted_active_[index];
            }
        }
        hosted_active_.resize(kept);
        threads = std::move(ready_threads_);
        ready_threads_.clear();
    } else {
        hosted = std::move(hosted_active_);
        hosted_active_.clear();
        threads = std::move(ready_threads_);
        ready_threads_.clear();
    }
    sort_hosted(hosted);
    const auto thread_order = [&](const std::uint32_t thread) {
        return StableOrder { threads_[thread].order };
    };
    std::ranges::stable_sort(threads, std::less { }, thread_order);
    std::size_t next_hosted = 0U;
    std::size_t next_thread = 0U;
    // Time zero's once members (start_once_) run in the same order; those
    // not yet run stay listed across a stop or a yield to the host.
    const auto once_order = [&] {
        return StableOrder { members_[start_once_[start_once_next_]].process };
    };
    while (next_hosted < hosted.size() || next_thread < threads.size()
        || start_once_pending()) {
        if (impl_.scheduler.stop_requested() || stopped_) {
            // $stop or $finish: the rest is kept for a resumed run, ahead of
            // what this round woke.
            hosted_active_.insert(hosted_active_.end(),
                hosted.begin() + static_cast<std::ptrdiff_t>(next_hosted),
                hosted.end());
            ready_threads_.insert(ready_threads_.begin(),
                threads.begin() + static_cast<std::ptrdiff_t>(next_thread),
                threads.end());
            return;
        }
        const bool hosted_or_thread
            = next_hosted < hosted.size() || next_thread < threads.size();
        const bool take_hosted = hosted_or_thread
            && (next_thread == threads.size()
                || (next_hosted < hosted.size()
                    && hosted[next_hosted].order
                        <= thread_order(threads[next_thread])));
        auto order = !hosted_or_thread ? once_order()
            : take_hosted              ? hosted[next_hosted].order
                                       : thread_order(threads[next_thread]);
        const bool take_once = start_once_pending()
            && (!hosted_or_thread || once_order() < order);
        if (take_once) {
            order = once_order();
        }
        if (host_batch_ahead(order)) {
            batch_hosted_.assign(
                hosted.begin() + static_cast<std::ptrdiff_t>(next_hosted),
                hosted.end());
            batch_threads_.assign(
                threads.begin() + static_cast<std::ptrdiff_t>(next_thread),
                threads.end());
            host_batch_resume_ = order;
            return;
        }
        if (take_once) {
            run(start_once_[start_once_next_++]);
            if (!start_once_pending()) {
                start_once_.clear();
                start_once_next_ = 0U;
            }
            continue;
        }
        if (take_hosted) {
            if (!output_queue_.empty()) {
                publish(true);
            }
            run_hosted(hosted[next_hosted++]);
            (void)scan_inputs(false);
            continue;
        }
        const auto index = threads[next_thread++];
        if (take_ready(index)) {
            run_thread(index);
        }
    }
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
    if (const auto final = std::ranges::find(final_members_, ready.process,
            &std::pair<ProcessId, std::uint32_t>::first);
        final != final_members_.end()) {
        // A final member among the hosted final blocks (run_finals); its
        // process state is the member's (the host stub's, for the host
        // member) and has no wake of its own.
        run_final_member(final->second);
        return;
    }
    auto& state = impl_.get_process(ready.process);
    if (ready.timer && state.timer_epoch != ready.epoch) {
        return;
    }
    if (trace_) {
        std::cerr << "fsim-kernel-trace: t=" << impl_.scheduler.now() << "+" << impl_.scheduler.delta()
                  << " hosted " << impl_.processes.program_view(ready.process).name()
                  << '\n';
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
    activate_for_due_timer();
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
    if (!hosted_active_.empty() && !yielding && !vhdl_round_deferred_) {
        // Woken by this activation's publication, left behind by $stop or
        // $finish, or held back in a host Inactive or NBA task: the host's
        // Active region runs it (before the NBA region, 4.4).
        requeue_host_active();
    }
    if (!hosted_inactive_.empty() && !hosted_inactive_deferred_) {
        defer_hosted_inactive();
    }
    arm_hosted_timer();
}

} // namespace fsim::runtime::simir

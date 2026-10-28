// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 behavioral tier (docs/simulation-engine-v4-progress.md §4b):
// testbench processes run inside the static kernel as resumable threads. A
// thread runs until it suspends on a delay, a dynamic or static wait, a fork
// join, a halt or $finish; waits on kernel-owned slots and on host inputs, and
// delays on a kernel timer queue, make it ready again.
#include "fsim/runtime/file_scanning.hpp"
#include "simir_execution_shared.hpp"
#include "simir_static_kernel_state.hpp"

#include "fsim/runtime/output_format.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;

std::uint32_t Interpreter::Impl::StaticKernel::spawn_thread(
    const std::uint32_t member, const std::uint32_t pc,
    const std::uint32_t parent, const std::uint32_t group,
    const std::uint32_t fork_site)
{
    std::uint32_t index { };
    if (!free_threads_.empty()) {
        index = free_threads_.back();
        free_threads_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(threads_.size());
        threads_.emplace_back();
    }
    auto& thread = threads_[index];
    const auto epoch = thread.epoch + 1U;
    thread = Thread { };
    thread.member = member;
    thread.pc = pc;
    thread.parent = parent;
    thread.group = group;
    thread.fork_site = fork_site;
    thread.epoch = epoch;
    thread.alive = true;
    thread.order = members_[member].process;
    if (parent != no_slot) {
        // A fork child is a process of its own, numbered after the processes
        // that exist (Interpreter::spawn_fork): its updates and its place in
        // a round follow that number.
        const auto id = static_cast<ProcessId>(
            std::max(impl_.processes.size(), next_child_process_));
        next_child_process_ = static_cast<std::size_t>(id) + 1U;
        thread.order = id;
        thread.random_state = Impl::initial_random_state(impl_.root_seed, id);
        if (members_[member].thread_contexts) {
            // The activation the fork runs in escapes: the child shares it,
            // and those its parent shares, holding their current values
            // (Interpreter::spawn_fork).
            store_contexts(parent);
            auto& inherited = threads_[parent].contexts;
            if (!inherited.empty()) {
                inherited.back()->escaping = true;
            }
            for (const auto& context : inherited) {
                if (context->escaping) {
                    threads_[index].contexts.push_back(context);
                }
            }
        }
        if (debug_points_) {
            thread.debug_process = id;
            debug_children_[id] = member;
        }
    }
    return index;
}

void Interpreter::Impl::StaticKernel::ready_thread(const std::uint32_t index)
{
    auto& thread = threads_[index];
    if (!thread.alive || thread.queued) {
        return;
    }
    // A resumed thread abandons every other registration of its wait.
    ++thread.epoch;
    thread.queued = true;
    if (members_[thread.member].vhdl) {
        // A VHDL thread resumes in the next delta cycle, whatever woke it
        // (IEEE 1076-2019 14.7.5.3): it runs with the VHDL members of the
        // next round, never in a SystemVerilog Active drain.
        vhdl_ready_threads_.push_back(index);
        return;
    }
    if (committing_round_) {
        // Woken by a VHDL commit: it runs in the next delta.
        deferred_threads_.push_back(index);
        return;
    }
    ready_threads_.push_back(index);
}

void Interpreter::Impl::StaticKernel::prepare_behavioral()
{
    slot_waitable_.resize(slots_.size(), 0U);
    slot_wait_seen_.assign(slots_.size(), Logic4::x);
    slot_waiters_.resize(slots_.size());
    input_waiters_.resize(inputs_.size());
    for (std::uint32_t slot = 0U; slot < slots_.size(); ++slot) {
        if (slot_waitable_[slot] != 0U) {
            slot_wait_seen_[slot] = slot_edge_bit(slot);
        }
    }
    // A process that begins at its static wait is armed before any other
    // time-zero process runs, as the host scheduler arms its processes
    // (Interpreter::start): it then sees the changes the once members
    // (declaration initializers, initial blocks) make. This is the one
    // fixed order fsim uses for that time-zero race (IEEE 1800-2023 4.7,
    // 9.2; engine-v4-semantics S18). A VHDL process that begins at its wait
    // (a port conversion that is not evaluated at initialization) is armed
    // the same way, so an SV update of time zero reaches it (IEEE 1076-2019
    // 14.7.5.2: initialization runs every process until it suspends).
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        auto& member = members_[index];
        if (member.kind != StaticKernelMemberKind::behavioral
            || member.start_late
            || member.wait_sensitivity.empty()
            || member.operations.empty()
            || !operation_holds<WaitSensitivity>(member.operations.front())) {
            continue;
        }
        member.prearmed = true;
        const auto thread = spawn_thread(index, 0U, no_slot, no_slot);
        run_thread(thread);
    }
}

void Interpreter::Impl::StaticKernel::start_behavioral()
{
    // Every behavioral process starts at time zero in the Active region,
    // like its reference process; one that the scheduler starts a delta late
    // (systemverilog_late_starters) starts in the next delta. An always_comb
    // or always_latch starts after every other procedure has started (IEEE
    // 1800-2023 9.2.2.2.2), as the scheduler orders it (queue_at).
    for (const bool after_procedures : { false, true }) {
        for (std::uint32_t index = 0U; index < members_.size(); ++index) {
            const auto& member = members_[index];
            if (member.kind != StaticKernelMemberKind::behavioral
                || member.prearmed
                || (member.after_procedures && !member.vhdl)
                    != after_procedures) {
                continue;
            }
            if (member.start_late) {
                late_members_.push_back(index);
            } else {
                ready_thread(spawn_thread(index, 0U, no_slot, no_slot));
            }
        }
    }
    if (!late_members_.empty()) {
        late_start_armed_ = true;
        // A SystemVerilog kernel's late activation is queued by
        // Interpreter::start like the reference's late starters, in the
        // generic pass of the first delta (queue_late_start).
        yield_requested_ = yield_requested_ || mixed_ || vhdl_;
    }
}

void Interpreter::Impl::StaticKernel::start_late_members()
{
    late_start_armed_ = false;
    for (const auto index : std::exchange(late_members_,
             std::vector<std::uint32_t> { })) {
        ready_thread(spawn_thread(index, 0U, no_slot, no_slot));
    }
}

void Interpreter::Impl::StaticKernel::queue_late_start(const ProcessId order)
{
    // The reference queues each late starter for the next delta before the
    // run (Interpreter::start), which is the generic pass of the first delta.
    struct KernelLateStartTask {
        Interpreter::Impl* owner { };
        ProcessId host { };
    };
    const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
        KernelLateStartTask,
        +[](Scheduler&, const KernelLateStartTask& scheduled) {
            auto& kernel = *scheduled.owner->static_kernel;
            kernel.late_start_due_ = true;
            bool yielded { };
            try {
                yielded = kernel.activate();
            } catch (...) {
                kernel.late_start_due_ = false;
                throw;
            }
            kernel.late_start_due_ = false;
            if (yielded) {
                scheduled.owner->queue_next_delta(scheduled.host);
            }
        }>(KernelLateStartTask { &impl_, host_process_ });
    impl_.scheduler.schedule_internal_next_delta(
        SchedulerPhase::active, StableOrder { order }, task);
}

std::optional<ProcessId>
Interpreter::Impl::StaticKernel::first_late_process() const
{
    if (mixed_ || vhdl_) {
        return std::nullopt;
    }
    std::optional<ProcessId> first;
    for (const auto& member : members_) {
        if (member.kind == StaticKernelMemberKind::behavioral
            && member.start_late && (!first || member.process < *first)) {
            first = member.process;
        }
    }
    return first;
}

bool Interpreter::Impl::StaticKernel::run_ready_threads()
{
    if (ready_threads_.empty()) {
        return false;
    }
    auto ready = std::move(ready_threads_);
    ready_threads_.clear();
    for (std::size_t position = 0U; position < ready.size(); ++position) {
        const auto index = ready[position];
        if (stopped_) {
            // Threads not yet run stay ready for a resumed run ($stop).
            ready_threads_.insert(ready_threads_.end(),
                ready.begin() + static_cast<std::ptrdiff_t>(position),
                ready.end());
            break;
        }
        if (take_ready(index)) {
            run_thread(index);
        }
    }
    return true;
}

bool Interpreter::Impl::StaticKernel::take_ready(const std::uint32_t index)
{
    auto& thread = threads_[index];
    thread.queued = false;
    if (thread.reclaim) {
        // Killed while queued: this was the last list naming it.
        thread.reclaim = false;
        release_thread(index);
        return false;
    }
    return thread.alive;
}

bool Interpreter::Impl::StaticKernel::run_vhdl_threads()
{
    if (vhdl_ready_threads_.empty()) {
        return false;
    }
    // Threads woken while these run (none today: a VHDL signal changes only
    // in the update phase) belong to the next round.
    auto ready = std::move(vhdl_ready_threads_);
    vhdl_ready_threads_.clear();
    for (std::size_t position = 0U; position < ready.size(); ++position) {
        const auto index = ready[position];
        if (stopped_) {
            // Threads not yet run stay ready for a resumed run ($stop, the
            // debugger), ahead of what this round woke.
            vhdl_ready_threads_.insert(vhdl_ready_threads_.begin(),
                ready.begin() + static_cast<std::ptrdiff_t>(position),
                ready.end());
            break;
        }
        if (take_ready(index)) {
            run_thread(index);
        }
    }
    return true;
}

void Interpreter::Impl::StaticKernel::wait_on(const std::uint32_t index,
    const std::span<const Sensitivity> entries)
{
    const auto epoch = threads_[index].epoch;
    for (const auto& entry : entries) {
        const Waiter waiter { index, epoch, entry.edge, entry.offset,
            entry.edge == EdgeKind::any ? entry.width : 0U };
        if (const auto slot = slot_of_signal_[entry.signal]; slot != no_slot) {
            slot_waiters_[slot].push_back(waiter);
        } else {
            input_waiters_[input_of_signal_[entry.signal]].push_back(waiter);
        }
    }
}

void Interpreter::Impl::StaticKernel::wake_waiters(std::vector<Waiter>& waiters,
    const WaitChange& change)
{
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < waiters.size(); ++index) {
        const auto waiter = waiters[index];
        const auto& thread = threads_[waiter.thread];
        if (!thread.alive || thread.queued || thread.epoch != waiter.epoch) {
            continue;
        }
        // A ranged waiter wakes only when its selected bits changed.
        if (waiter.edge == EdgeKind::any
            ? waiter.width == 0U || waiter_range_changed(waiter, change)
            : edge_matches(waiter.edge, change.before, change.after)) {
            ready_thread(waiter.thread);
            continue;
        }
        waiters[kept++] = waiter;
    }
    waiters.resize(kept);
}

void Interpreter::Impl::StaticKernel::release_thread(const std::uint32_t index)
{
    auto& thread = threads_[index];
    thread.alive = false;
    thread.call_stack.clear();
    thread.frames.clear();
    thread.saved_arrays.clear();
    thread.saved_array_marks.clear();
    thread.stack.clear();
    thread.contexts.clear();
    free_threads_.push_back(index);
}

void Interpreter::Impl::StaticKernel::store_contexts(const std::uint32_t index)
{
    auto& member = members_[threads_[index].member];
    const auto& contexts = threads_[index].contexts;
    std::vector<std::uint32_t> seen;
    for (auto entry = contexts.rbegin(); entry != contexts.rend(); ++entry) {
        auto& context = **entry;
        if (std::ranges::find(seen, context.identity) != seen.end()) {
            // An outer activation of a recursive callable: its values are
            // in the inner activation's frame.
            continue;
        }
        seen.push_back(context.identity);
        context.values.clear();
        for (const auto id : context.ids) {
            context.values.push_back(member.registers.at(id));
        }
        context.strings.clear();
        for (const auto id : context.string_ids) {
            context.strings.push_back(member.strings.at(id));
        }
        context.stored = true;
    }
}

void Interpreter::Impl::StaticKernel::load_contexts(
    const std::uint32_t index, const bool escaping_only)
{
    auto& member = members_[threads_[index].member];
    const auto& contexts = threads_[index].contexts;
    std::vector<std::uint32_t> seen;
    for (auto entry = contexts.rbegin(); entry != contexts.rend(); ++entry) {
        const auto& context = **entry;
        if (std::ranges::find(seen, context.identity) != seen.end()) {
            continue;
        }
        seen.push_back(context.identity);
        if (!context.stored || (escaping_only && !context.escaping)) {
            continue;
        }
        for (std::size_t at = 0U; at < context.ids.size(); ++at) {
            member.registers.at(context.ids[at]) = context.values[at];
        }
        for (std::size_t at = 0U; at < context.string_ids.size(); ++at) {
            member.strings.at(context.string_ids[at]) = context.strings[at];
        }
    }
}

std::vector<std::uint32_t> Interpreter::Impl::StaticKernel::live_children(
    const std::uint32_t parent) const
{
    std::vector<std::uint32_t> children;
    for (std::uint32_t index = 0U; index < threads_.size(); ++index) {
        if (threads_[index].alive && threads_[index].parent == parent) {
            children.push_back(index);
        }
    }
    std::ranges::sort(children, std::less { },
        [&](const std::uint32_t index) { return threads_[index].order; });
    return children;
}

void Interpreter::Impl::StaticKernel::cancel_thread_descendants(
    const std::uint32_t parent)
{
    for (const auto child : live_children(parent)) {
        cancel_thread_descendants(child);
        kill_thread(child);
    }
}

void Interpreter::Impl::StaticKernel::kill_thread(const std::uint32_t index)
{
    auto& thread = threads_[index];
    if (!thread.alive) {
        return;
    }
    // Its waiter and timer entries lapse.
    ++thread.epoch;
    const auto parent = thread.parent;
    const auto group = thread.group;
    if (index == running_thread_) {
        // run_thread parks and releases it (Boundary::killed).
        thread.alive = false;
    } else if (thread.queued) {
        thread.alive = false;
        thread.reclaim = true;
    } else {
        std::erase(inactive_threads_, index);
        release_thread(index);
    }
    fork_child_done(parent, group);
}

Interpreter::Impl::StaticKernel::Boundary
Interpreter::Impl::StaticKernel::disable_block(const std::uint32_t index,
    const std::uint32_t pc, const DisableBlock& op)
{
    // A local named block (the planner keeps a disable naming another
    // process's scope on the host). The running thread's position is past
    // the disable, as the reference's process counter is.
    const auto member_index = threads_[index].member;
    const auto& operations = members_[member_index].operations;
    if (!op.scopes.empty() || op.begin >= op.end || op.end > operations.size()) {
        fail(member_index, pc, "DisableBlock has an invalid lexical interval");
    }
    const auto begin = op.begin;
    const auto end = op.end;
    const auto running_pc = pc + 1U;
    const auto pc_of = [&](const std::uint32_t thread) {
        return thread == index ? running_pc : threads_[thread].pc;
    };
    const auto forked_inside = [&](const std::uint32_t thread) {
        const auto& state = threads_[thread];
        return state.parent != no_slot && state.fork_site != no_slot
            && state.fork_site >= begin && state.fork_site < end;
    };
    // The thread that runs the block: a fork child spawned inside it belongs
    // to its forking ancestor.
    auto owner = index;
    while (forked_inside(owner)) {
        owner = threads_[owner].parent;
    }
    std::optional<std::uint32_t> redirect;
    disable_thread_interval(index, running_pc, owner, begin, end, redirect);
    // The block may run in a sibling fork branch (IEEE 1800-2023 9.6.2): an
    // ancestor owns that branch.
    for (auto ancestor = owner;
         threads_[index].alive && threads_[ancestor].parent != no_slot;) {
        ancestor = threads_[ancestor].parent;
        disable_thread_interval(index, running_pc, ancestor, begin, end, redirect);
        for (const auto sibling : live_children(ancestor)) {
            if (sibling == index || !threads_[sibling].alive
                || pc_of(sibling) < begin || pc_of(sibling) >= end) {
                continue;
            }
            // A block that is the branch's whole body ends the branch.
            auto next = static_cast<std::size_t>(end);
            while (next < operations.size()
                && operation_holds<DebugPoint>(operations[next])) {
                ++next;
            }
            if (next < operations.size()
                && operation_holds<ForkEnd>(operations[next])) {
                cancel_thread_descendants(sibling);
                kill_thread(sibling);
            } else {
                disable_thread_interval(index, running_pc, sibling, begin, end,
                    redirect);
            }
        }
    }
    // The block ends in every concurrent activation of the code that
    // declares it, such as each call of an automatic task running in
    // another fork branch.
    std::vector<std::uint32_t> candidates;
    for (std::uint32_t thread = 0U; thread < threads_.size(); ++thread) {
        if (thread != index && threads_[thread].alive
            && threads_[thread].member == member_index
            && pc_of(thread) >= begin && pc_of(thread) < end) {
            candidates.push_back(thread);
        }
    }
    std::ranges::sort(candidates, std::less { },
        [&](const std::uint32_t thread) { return threads_[thread].order; });
    std::vector<std::uint32_t> other_owners;
    for (const auto thread : candidates) {
        auto other = thread;
        while (forked_inside(other)) {
            other = threads_[other].parent;
        }
        bool related = other == owner;
        for (auto ancestor = owner; !related && threads_[ancestor].parent != no_slot;) {
            ancestor = threads_[ancestor].parent;
            related = ancestor == other;
        }
        if (!related && std::ranges::find(other_owners, other) == other_owners.end()) {
            other_owners.push_back(other);
        }
    }
    for (const auto other : other_owners) {
        if (threads_[other].alive) {
            disable_thread_interval(index, running_pc, other, begin, end, redirect);
        }
    }
    if (!threads_[index].alive) {
        return Boundary::killed;
    }
    if (redirect) {
        redirect_pc_ = *redirect;
        return Boundary::redirect;
    }
    return Boundary::advance;
}

void Interpreter::Impl::StaticKernel::disable_thread_interval(
    const std::uint32_t running, const std::uint32_t running_pc,
    const std::uint32_t owner, const std::uint32_t begin, const std::uint32_t end,
    std::optional<std::uint32_t>& running_redirect)
{
    // Fork children spawned inside the block end with their descendants.
    std::vector<std::uint32_t> scoped;
    for (const auto child : live_children(owner)) {
        const auto site = threads_[child].fork_site;
        if (site != no_slot && site >= begin && site < end) {
            scoped.push_back(child);
        }
    }
    for (const auto child : scoped) {
        cancel_thread_descendants(child);
        kill_thread(child);
    }
    auto& thread = threads_[owner];
    const auto at = owner == running ? running_pc : thread.pc;
    if (!thread.alive || at < begin || at >= end) {
        return;
    }
    if (owner == running) {
        running_redirect = end;
        return;
    }
    // A suspended owner resumes at the block's end now, not when its wait
    // ends; one already ready (or resuming in the Inactive region) resumes
    // there when it runs.
    thread.pc = end;
    if (!thread.queued
        && std::ranges::find(inactive_threads_, owner) == inactive_threads_.end()) {
        ready_thread(owner);
    }
}

void Interpreter::Impl::StaticKernel::finish_thread(const std::uint32_t index)
{
    const auto parent = threads_[index].parent;
    const auto group = threads_[index].group;
    release_thread(index);
    fork_child_done(parent, group);
}

void Interpreter::Impl::StaticKernel::fork_child_done(const std::uint32_t parent,
    const std::uint32_t group)
{
    if (parent == no_slot || group == no_slot) {
        return;
    }
    auto& state = fork_groups_[group];
    if (state.remaining != 0U) {
        --state.remaining;
    }
    const bool resume = state.join == ForkJoinKind::any
        ? !state.resumed
        : state.join == ForkJoinKind::all && state.remaining == 0U;
    if (resume && !state.resumed) {
        state.resumed = true;
        // Like the reference, the parent resumes in the current Active step.
        ready_thread(parent);
    }
    if (state.remaining == 0U) {
        free_fork_groups_.push_back(group);
    }
}

void Interpreter::Impl::StaticKernel::behavioral_output(
    const std::uint32_t member, const std::string_view text, const bool newline)
{
    if (auto* const lane = worker_lane_; lane != nullptr) {
        // A worker's output waits for the leader's replay, which emits it at
        // the member's place in the serial order.
        if (impl_.output_hook) {
            LaneOutput output;
            output.member = member;
            output.newline = newline;
            output.text = text;
            lane->outputs.push_back(std::move(output));
        }
        return;
    }
    if (impl_.output_hook) {
        // A warped time step is the first delta of its time slot.
        impl_.output_hook(members_[member].process, text, newline,
            kernel_now(), warp_time_ ? 0U : impl_.scheduler.delta());
    }
}

std::string Interpreter::Impl::StaticKernel::format_display_text(
    const FormatDisplay& op, const PackedLogic4& value) const
{
    // %t follows $timeformat; an integral argument is in its module's time
    // unit (IEEE 1800-2017 21.2.1.3).
    std::optional<SimulationTick> tick;
    if (op.format == OutputFormat::time) {
        if (op.scalar_kind == SystemVerilogScalarKind::None
            || (op.scalar_kind != SystemVerilogScalarKind::Chandle
                && op.time_unit_femtoseconds != 0U)) {
            tick = integral_time_ticks(value, op.signed_decimal,
                op.time_unit_femtoseconds, impl_.time_format, op.scalar_kind);
        } else if (const auto decoded
            = decode_systemverilog_scalar_payload(value, op.scalar_kind)) {
            tick = decoded.value.as_time();
        }
    }
    return tick
        ? make_time_output(op.prefix, op.suffix, *tick, impl_.time_format,
              op.minimum_width == 0 && !op.suppress_leading_zero,
              op.minimum_width, op.left_justify, op.zero_pad)
        : make_formatted_output(op.prefix, op.suffix,
              op.format == OutputFormat::time
                      && op.scalar_kind == SystemVerilogScalarKind::None
                  ? OutputFormat::decimal
                  : op.format,
              value, op.signed_decimal, op.suppress_leading_zero,
              op.minimum_width, op.left_justify, op.zero_pad, op.scalar_kind,
              op.precision);
}

std::string Interpreter::Impl::StaticKernel::time_display_text(
    const TimeDisplay& op) const
{
    return make_time_output(op.prefix, op.suffix, kernel_now(),
        impl_.time_format, op.use_timeformat_width, op.minimum_width,
        op.left_justify, op.zero_pad);
}

void Interpreter::Impl::StaticKernel::emit_lane_output(const LaneOutput& output)
{
    const auto& member = members_[output.member];
    switch (output.kind) {
    case LaneOutput::Kind::text:
        behavioral_output(output.member, output.text, output.newline);
        break;
    case LaneOutput::Kind::format:
        // Formatted here: a $timeformat earlier in the replay applies.
        behavioral_output(output.member,
            format_display_text(
                operation_get<FormatDisplay>(member.operations[output.pc]),
                output.value),
            output.newline);
        break;
    case LaneOutput::Kind::time:
        behavioral_output(output.member,
            time_display_text(
                operation_get<TimeDisplay>(member.operations[output.pc])),
            output.newline);
        break;
    case LaneOutput::Kind::report:
        if (impl_.report_hook) {
            const auto& op = operation_get<Report>(member.operations[output.pc]);
            impl_.report_hook(member.process, op.message.str(), op.severity,
                op.source, kernel_now(), warp_time_ ? 0U : impl_.scheduler.delta());
        }
        break;
    }
}

bool Interpreter::Impl::StaticKernel::behavioral_step(
    const std::uint32_t member_index, const std::uint32_t pc)
{
    ++interpreted_operations_;
    ++interpreted_by_cause_[static_cast<std::size_t>(interpreting_)];
    return visit_operation([&](const auto& op) {
        return behavioral_operation(member_index, pc, op);
    }, members_[member_index].operations[pc]);
}

template <typename Op>
bool Interpreter::Impl::StaticKernel::behavioral_operation_at(
    const std::uint32_t member_index, const std::uint32_t pc)
{
    return behavioral_operation(member_index, pc,
        operation_get<Op>(members_[member_index].operations[pc]));
}

const std::array<Interpreter::Impl::StaticKernel::BehavioralOperationStep,
    operation_kind_count>&
Interpreter::Impl::StaticKernel::behavioral_operation_steps()
{
    static constexpr auto steps = []<std::size_t... K>(
                                      std::index_sequence<K...>) {
        return std::array<BehavioralOperationStep, operation_kind_count> {
            &StaticKernel::behavioral_operation_at<OperationKindAt<K>>...
        };
    }(std::make_index_sequence<operation_kind_count>());
    return steps;
}

template <typename Op>
bool Interpreter::Impl::StaticKernel::behavioral_operation(
    const std::uint32_t member_index, const std::uint32_t pc, const Op& op)
{
    auto& member = members_[member_index];
    const auto string_register = [&](const StringRegisterId id) -> std::string& {
        if (id >= member.strings.size()) {
            fail(member_index, pc, "invalid string register ID");
        }
        return member.strings[id];
    };
    const auto packed_register = [&](const RegisterId id) -> PackedLogic4& {
        if (id >= member.registers.size()) {
            fail(member_index, pc, "invalid register ID");
        }
        return member.registers[id];
    };
    const auto container_register = [&](const ContainerRegisterId id)
        -> std::uint32_t& {
        if (id >= member.container_registers.size()) {
            fail(member_index, pc, "invalid container register ID");
        }
        return member.container_registers[id];
    };
    bool handled = true;
    [&] {
        using T = Op;
        if constexpr (std::is_same_v<T, Display>) {
            behavioral_output(member_index, op.text, op.newline);
        } else if constexpr (std::is_same_v<T, FormatDisplay>) {
            if (auto* const lane = worker_lane_;
                lane != nullptr && op.format == OutputFormat::time) {
                // $timeformat may change before the replay reaches it.
                if (impl_.output_hook) {
                    LaneOutput output;
                    output.kind = LaneOutput::Kind::format;
                    output.member = member_index;
                    output.pc = pc;
                    output.newline = op.newline;
                    output.value = packed_register(op.source);
                    lane->outputs.push_back(std::move(output));
                }
                return;
            }
            const auto text = format_display_text(op, packed_register(op.source));
            behavioral_output(member_index, text, op.newline);
        } else if constexpr (std::is_same_v<T, StringDisplay>) {
            const auto text = op.prefix + string_register(op.source) + op.suffix;
            behavioral_output(member_index, text, op.newline);
        } else if constexpr (std::is_same_v<T, SystemVerilogScalarBinary>) {
            // Real, shortreal and time arithmetic, comparisons and
            // conversions (IEEE 1800-2023 6.12, 11.3.1), as the reference
            // evaluates them.
            const auto result = systemverilog_scalar_binary_payload(op.operation,
                packed_register(op.lhs), op.lhs_kind, packed_register(op.rhs),
                op.rhs_kind, op.result_kind);
            if (!result) {
                fail(member_index, pc,
                    "SystemVerilog scalar binary operation failed (error "
                        + std::to_string(static_cast<unsigned>(result.error))
                        + ", lhs kind "
                        + std::to_string(static_cast<unsigned>(op.lhs_kind))
                        + ", rhs kind "
                        + std::to_string(static_cast<unsigned>(op.rhs_kind)) + ")");
            }
            packed_register(op.destination) = result.value;
        } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
            if (op.function < SystemVerilogMathFunction::Time) {
                // The math and conversion functions (IEEE 1800-2023 20.5,
                // 20.8), as the reference evaluates them.
                const auto* second
                    = op.second_width == 0U ? nullptr : &packed_register(op.second);
                const auto result = systemverilog_math_payload(op.function,
                    packed_register(op.first), op.first_kind, op.first_signed,
                    second, op.second_kind, op.second_signed);
                if (!result) {
                    fail(member_index, pc,
                        "SystemVerilog math function failed (error "
                            + std::to_string(static_cast<unsigned>(result.error))
                            + ")");
                }
                packed_register(op.destination) = result.value;
                return;
            }
            // $time, $stime and $realtime (IEEE 1800-2017 20.3).
            const auto function = op.function == SystemVerilogMathFunction::Time
                ? SystemVerilogTimeFunction::Time
                : op.function == SystemVerilogMathFunction::Stime
                ? SystemVerilogTimeFunction::Stime
                : SystemVerilogTimeFunction::Realtime;
            const auto value = systemverilog_time_function(function,
                kernel_now(),
                { op.time_unit_femtoseconds, op.time_precision_femtoseconds,
                    impl_.time_format.resolution_femtoseconds });
            if (!value) {
                fail(member_index, pc, "SystemVerilog time query failed");
            }
            const auto encoded
                = op.function == SystemVerilogMathFunction::Realtime
                ? encode_systemverilog_scalar_payload(value.value)
                : systemverilog_scalar_to_packed(value.value,
                      op.function == SystemVerilogMathFunction::Stime ? 32U
                                                                      : 64U,
                      false);
            if (!encoded) {
                fail(member_index, pc, "SystemVerilog time query failed");
            }
            packed_register(op.destination) = member.vhdl
                ? Impl::coerce_value_kind(encoded.value,
                      op.destination < member.register_kinds.size()
                          ? member.register_kinds[op.destination]
                          : ValueKind::logic4)
                : encoded.value;
        } else if constexpr (std::is_same_v<T, TimeDisplay>) {
            if (auto* const lane = worker_lane_; lane != nullptr) {
                if (impl_.output_hook) {
                    LaneOutput output;
                    output.kind = LaneOutput::Kind::time;
                    output.member = member_index;
                    output.pc = pc;
                    output.newline = op.newline;
                    lane->outputs.push_back(std::move(output));
                }
                return;
            }
            behavioral_output(member_index, time_display_text(op), op.newline);
        } else if constexpr (std::is_same_v<T, Report>) {
            // A VHDL report statement (IEEE 1076-2019 10.9, 10.8): the
            // planner admits it only outside the 2019 mode, where the
            // reference evaluator emits it through the same hook.
            if (impl_.report_hook) {
                impl_.report_hook(member.process, op.message.str(),
                    op.severity, op.source, kernel_now(),
                    warp_time_ ? 0U : impl_.scheduler.delta());
            }
            if (op.severity == AssertionSeverity::failure) {
                throw AssertionError(member.process,
                    pc < member.origin.size() ? member.origin[pc] : pc,
                    op.message.empty() ? std::string { "report failure" }
                                       : op.message.str(),
                    op.severity, op.source, true);
            }
        } else if constexpr (std::is_same_v<T, LoadStringConstant>) {
            if (op.value.size() > maximum_string_bytes) {
                fail(member_index, pc, "string literal exceeds the string length limit");
            }
            try {
                (void)systemverilog_string_length(op.value);
            } catch (const std::invalid_argument& error) {
                fail(member_index, pc, error.what());
            }
            string_register(op.destination) = op.value;
        } else if constexpr (std::is_same_v<T, CopyStringRegister>) {
            string_register(op.destination) = string_register(op.source);
        } else if constexpr (std::is_same_v<T, ConcatenateStrings>) {
            std::string result;
            for (const auto operand : op.operands) {
                const auto& value = string_register(operand);
                if (value.size() > maximum_string_bytes - result.size()) {
                    fail(member_index, pc,
                        "string concatenation exceeds the string length limit");
                }
                result += value;
            }
            string_register(op.destination) = std::move(result);
        } else if constexpr (std::is_same_v<T, StringMethod>) {
            // The reference evaluator's execute_string, on the thread's
            // registers.
            try {
                auto& source = string_register(op.source);
                const bool dynamic_string_field = op.operation
                    == StringMethodOperator::format_string_dynamic_field;
                const bool dynamic_packed_field = op.operation
                    == StringMethodOperator::format_packed_dynamic_field;
                const bool string_format
                    = op.operation == StringMethodOperator::format_string
                    || dynamic_string_field;
                const bool packed_format
                    = op.operation == StringMethodOperator::format_packed
                    || dynamic_packed_field;
                const bool format = packed_format || string_format
                    || op.operation == StringMethodOperator::format_time
                    || op.operation == StringMethodOperator::format_vhdl_time;
                if (format) {
                    auto normalized = op;
                    if (dynamic_string_field || dynamic_packed_field) {
                        const auto width_register = dynamic_string_field
                            ? op.first : op.second;
                        normalized.minimum_width
                            = checked_vhdl_textio_field_width(
                                packed_register(width_register));
                        normalized.operation = dynamic_string_field
                            ? StringMethodOperator::format_string
                            : StringMethodOperator::format_packed;
                    }
                    const auto packed = packed_format
                            || op.operation
                                == StringMethodOperator::format_vhdl_time
                            || (op.operation == StringMethodOperator::format_time
                                && op.time_unit_femtoseconds != 0U)
                        ? packed_register(op.first)
                        : PackedLogic4 { 1, Logic4::zero };
                    execute_string_format(normalized, source, packed,
                        string_format
                            ? std::string_view { string_register(op.argument) }
                            : std::string_view { },
                        kernel_now(), impl_.time_format);
                } else {
                    const auto signed32 = [&](const RegisterId id)
                        -> std::optional<std::int32_t> {
                        const auto word = packed_register(id).low_word();
                        return word.bval == 0
                            ? std::optional<std::int32_t> {
                                  static_cast<std::int32_t>(
                                      static_cast<std::uint32_t>(word.aval)) }
                            : std::nullopt;
                    };
                    const bool compare
                        = op.operation == StringMethodOperator::compare
                        || op.operation == StringMethodOperator::icompare
                        || op.operation
                            == StringMethodOperator::vhdl_enumeration_value
                        || op.operation
                            == StringMethodOperator::vhdl_physical_value;
                    const bool element = op.operation
                            == StringMethodOperator::vhdl_composite_element
                        || op.operation
                            == StringMethodOperator::vhdl_character_element
                        || op.operation == StringMethodOperator::vhdl_string_fill;
                    const bool has_first
                        = op.operation == StringMethodOperator::getc
                        || op.operation == StringMethodOperator::putc
                        || op.operation == StringMethodOperator::substr
                        || element
                        || (op.operation >= StringMethodOperator::itoa
                            && op.operation <= StringMethodOperator::realtoa);
                    const bool has_second
                        = op.operation == StringMethodOperator::putc
                        || op.operation == StringMethodOperator::substr
                        || element;
                    const auto first_word = has_first
                        ? std::optional { packed_register(op.first).low_word() }
                        : std::nullopt;
                    const auto result = execute_string_method(op.operation,
                        source,
                        compare ? std::string_view { string_register(op.argument) }
                                : std::string_view { },
                        has_first ? signed32(op.first) : std::nullopt,
                        has_second ? signed32(op.second) : std::nullopt,
                        op.operation == StringMethodOperator::realtoa
                                && first_word && first_word->bval == 0
                            ? std::optional<std::uint64_t> { first_word->aval }
                            : std::nullopt);
                    if (result.integer) {
                        packed_register(op.destination)
                            = PackedLogic4::from_aval_bval(
                                32, *result.integer, 0);
                    }
                    if (result.scalar_bits) {
                        packed_register(op.destination)
                            = PackedLogic4::from_aval_bval(
                                64, *result.scalar_bits, 0);
                    }
                    if (result.string) {
                        string_register(op.string_destination) = *result.string;
                    }
                }
            } catch (const std::exception& error) {
                fail(member_index, pc, error.what());
            }
        } else if constexpr (std::is_same_v<T, PlusArgSelect>) {
            const auto selected = select_plusarg(
                impl_.plusargs, string_register(op.query), op.format_query);
            if (op.selected) {
                string_register(*op.selected).assign(
                    selected.value_or(std::string_view { }));
            }
            packed_register(op.destination) = PackedLogic4::from_aval_bval(
                32, selected ? 1U : 0U, 0);
        } else if constexpr (std::is_same_v<T, RandomValue>) {
            // $urandom, $urandom_range and $random (IEEE 1800-2023 18.13,
            // 20.15) draw from the process's own stream, like the reference
            // evaluator; a fork child's is its thread's (18.14).
            const auto maximum = op.maximum
                ? std::optional<PackedLogic4> { packed_register(*op.maximum) }
                : std::nullopt;
            const auto minimum = op.minimum
                ? std::optional<PackedLogic4> { packed_register(*op.minimum) }
                : std::nullopt;
            const auto thread = running_thread_;
            packed_register(op.destination)
                = thread != no_slot && threads_[thread].member == member_index
                    && threads_[thread].parent != no_slot
                ? impl_.random_value(threads_[thread].random_state, op.kind,
                      maximum, minimum)
                : impl_.random_value(members_[member_index].process, op.kind,
                      maximum, minimum);
        } else if constexpr (std::is_same_v<T, CallableFramePush>) {
            if (member.private_frame(op.identity)) {
                member.saved_array_marks.push_back(member.saved_arrays.size());
                for (const auto id : op.containers) {
                    if (id >= member.container_registers.size()
                        || member.container_registers[id] == no_container) {
                        continue;
                    }
                    const auto container = member.container_registers[id];
                    const auto& storage = containers_[container];
                    if (storage.object != no_container_object) {
                        continue;
                    }
                    const auto* begin = arena_.data() + storage.element_offset;
                    member.saved_arrays.push_back({ id, container,
                        std::vector<std::uint64_t>(begin,
                            begin + 2U * storage.element_words * storage.count) });
                }
            }
            if (member.frames_elided) {
                return;
            }
            if (op.identity == 0U) {
                fail(member_index, pc, "automatic callable frame identity is zero");
            }
            Member::Frame frame;
            frame.identity = op.identity;
            frame.ids.assign(op.packed.begin(), op.packed.end());
            frame.values.reserve(frame.ids.size());
            for (const auto id : frame.ids) {
                const auto& value = packed_register(id);
                frame.bytes += sizeof(PackedLogic4)
                    + value.aval_words().size_bytes()
                    + value.bval_words().size_bytes();
                frame.values.push_back(value);
            }
            frame.string_ids.assign(op.strings.begin(), op.strings.end());
            for (const auto id : frame.string_ids) {
                frame.strings.push_back(string_register(id));
                frame.bytes += sizeof(std::string) + frame.strings.back().size();
            }
            frame.container_ids.assign(op.containers.begin(), op.containers.end());
            for (const auto id : frame.container_ids) {
                frame.containers.push_back(container_register(id));
            }
            if (frame.bytes > maximum_container_storage_bytes
                    - std::min(member.frame_bytes,
                        maximum_container_storage_bytes)) {
                fail(member_index, pc,
                    "automatic callable frames exceed their owning-storage budget");
            }
            member.frame_bytes += frame.bytes;
            member.frames.push_back(std::move(frame));
            if (const auto thread = running_thread_; member.thread_contexts
                && thread != no_slot && threads_[thread].member == member_index) {
                // The callee may shadow the caller's registers: escaping
                // activations keep the caller's writes since its last
                // suspension.
                store_contexts(thread);
                auto context = std::make_shared<CallableContext>();
                context->identity = op.identity;
                context->ids.assign(op.packed.begin(), op.packed.end());
                context->string_ids.assign(op.strings.begin(), op.strings.end());
                threads_[thread].contexts.push_back(std::move(context));
            }
        } else if constexpr (std::is_same_v<T, CallableFramePop>) {
            if (member.private_frame(op.identity)) {
                if (member.saved_array_marks.empty()) {
                    fail(member_index, pc, "automatic callable frame stack mismatch");
                }
                const auto mark = member.saved_array_marks.back();
                member.saved_array_marks.pop_back();
                for (auto entry = mark; entry < member.saved_arrays.size(); ++entry) {
                    const auto& saved = member.saved_arrays[entry];
                    if (std::ranges::find(op.preserve_containers, saved.id)
                        != op.preserve_containers.end()) {
                        continue;
                    }
                    std::ranges::copy(saved.words, arena_.begin()
                        + containers_[saved.container].element_offset);
                }
                member.saved_arrays.resize(mark);
            }
            if (member.frames_elided) {
                return;
            }
            if (member.frames.empty()
                || member.frames.back().identity != op.identity) {
                fail(member_index, pc, "automatic callable frame stack mismatch");
            }
            const auto context_thread = member.thread_contexts
                    && running_thread_ != no_slot
                    && threads_[running_thread_].member == member_index
                ? running_thread_
                : no_slot;
            if (context_thread != no_slot) {
                // The activation ends for this thread; fork children still
                // inside it keep its final values (6.21).
                auto& contexts = threads_[context_thread].contexts;
                if (contexts.empty() || contexts.back()->identity != op.identity) {
                    fail(member_index, pc, "automatic callable context stack mismatch");
                }
                if (contexts.back()->escaping) {
                    auto& context = *contexts.back();
                    context.values.clear();
                    for (const auto id : context.ids) {
                        context.values.push_back(packed_register(id));
                    }
                    context.strings.clear();
                    for (const auto id : context.string_ids) {
                        context.strings.push_back(string_register(id));
                    }
                    context.stored = true;
                }
                contexts.pop_back();
            }
            std::vector<PackedLogic4> packed;
            for (const auto id : op.preserve_packed) {
                packed.push_back(packed_register(id));
            }
            std::vector<std::string> strings;
            for (const auto id : op.preserve_strings) {
                strings.push_back(string_register(id));
            }
            std::vector<std::uint32_t> containers;
            for (const auto id : op.preserve_containers) {
                containers.push_back(container_register(id));
            }
            auto frame = std::move(member.frames.back());
            member.frames.pop_back();
            for (std::size_t entry = 0U; entry < frame.ids.size(); ++entry) {
                packed_register(frame.ids[entry]) = std::move(frame.values[entry]);
            }
            for (std::size_t entry = 0U; entry < frame.string_ids.size(); ++entry) {
                string_register(frame.string_ids[entry])
                    = std::move(frame.strings[entry]);
            }
            for (std::size_t entry = 0U; entry < frame.container_ids.size(); ++entry) {
                container_register(frame.container_ids[entry])
                    = frame.containers[entry];
            }
            member.frame_bytes -= frame.bytes;
            if (context_thread != no_slot) {
                // Fork children may have written the escaping activations
                // the frame restore shadowed.
                load_contexts(context_thread, true);
            }
            for (std::size_t entry = 0U; entry < op.preserve_packed.size(); ++entry) {
                packed_register(op.preserve_packed[entry]) = std::move(packed[entry]);
            }
            for (std::size_t entry = 0U; entry < op.preserve_strings.size(); ++entry) {
                string_register(op.preserve_strings[entry]) = std::move(strings[entry]);
            }
            for (std::size_t entry = 0U; entry < op.preserve_containers.size(); ++entry) {
                container_register(op.preserve_containers[entry]) = containers[entry];
            }
        } else if constexpr (std::is_same_v<T, LoadMemory>) {
            // $readmemb/$readmemh (IEEE 1800-2023 21.4) into a kernel array:
            // a private one, or the design memory a ReadContainerObject
            // binding aliases. The file loads into the current contents,
            // written back element by element as element writes would be.
            // An I/O error leaves the array as it was (the reference loads
            // a copy and never writes it back).
            const auto index = container_register(op.target);
            if (op.write || index == no_container) {
                fail(member_index, pc, "static kernel container register is not bound");
            }
            const auto bound = [&](const std::optional<RegisterId> source,
                                   const char* role) -> std::optional<std::int32_t> {
                if (!source) {
                    return std::nullopt;
                }
                const auto converted = packed_register(*source).known_signed_value();
                if (!converted
                    || *converted < std::numeric_limits<std::int32_t>::min()
                    || *converted > std::numeric_limits<std::int32_t>::max()) {
                    fail(member_index, pc,
                        std::string { role } + " must be a known 32-bit integral value");
                }
                return static_cast<std::int32_t>(*converted);
            };
            const auto start = bound(op.start, "read-memory start");
            const auto finish = bound(op.finish, "read-memory finish");
            auto& container = containers_[index];
            ContainerValue value;
            value.type = container.type;
            value.elements.reserve(container.count);
            for (std::uint32_t ordinal = 0U; ordinal < container.count; ++ordinal) {
                value.elements.push_back(container_element(container, ordinal));
            }
            impl_.read_memory_file(member.process, pc, string_register(op.path), value,
                op.hexadecimal, start, finish);
            for (std::uint32_t ordinal = 0U; ordinal < container.count; ++ordinal) {
                if (!(value.elements[ordinal] == container_element(container, ordinal))) {
                    write_container_element(container, ordinal, value.elements[ordinal],
                        member_index, pc);
                }
            }
        } else if constexpr (std::is_same_v<T, WriteContainerObject>) {
            // The write-back of a binding to its own design memory (the
            // $readmem lowering): the binding aliases the memory, so there
            // is nothing to copy.
            if (op.nonblocking || op.transaction_signal
                || op.object >= container_of_object_.size()
                || container_of_object_[op.object] == no_container
                || container_register(op.source) != container_of_object_[op.object]) {
                fail(member_index, pc,
                    "static kernel container write is not a write-back of its binding");
            }
        } else {
            handled = false;
        }
    }();
    return handled;
}

Interpreter::Impl::StaticKernel::Boundary
Interpreter::Impl::StaticKernel::behavioral_boundary(const std::uint32_t index,
    const std::uint32_t pc)
{
    return visit_operation([&](const auto& op) {
        return behavioral_boundary_op(index, pc, op);
    }, members_[threads_[index].member].operations[pc]);
}

template <typename Op>
Interpreter::Impl::StaticKernel::Boundary
Interpreter::Impl::StaticKernel::behavioral_boundary_at(const std::uint32_t index,
    const std::uint32_t pc)
{
    return behavioral_boundary_op(index, pc,
        operation_get<Op>(members_[threads_[index].member].operations[pc]));
}

const std::array<Interpreter::Impl::StaticKernel::BoundaryStep,
    operation_kind_count>&
Interpreter::Impl::StaticKernel::boundary_steps()
{
    static constexpr auto steps = []<std::size_t... K>(
                                      std::index_sequence<K...>) {
        return std::array<BoundaryStep, operation_kind_count> {
            &StaticKernel::behavioral_boundary_at<OperationKindAt<K>>...
        };
    }(std::make_index_sequence<operation_kind_count>());
    return steps;
}

template <typename Op>
Interpreter::Impl::StaticKernel::Boundary
Interpreter::Impl::StaticKernel::behavioral_boundary_op(const std::uint32_t index,
    const std::uint32_t pc, const Op& op)
{
    const auto member_index = threads_[index].member;
    auto& member = members_[member_index];
    const auto read = [&](const RegisterId reg) -> PackedLogic4 {
        if (member.compiled && !member.generic_mode) {
            const auto& body = *member.compiled;
            if (reg >= body.register_widths.size()) {
                fail(member_index, pc, "invalid register ID");
            }
            const auto width = body.register_widths[reg];
            if (width > 64U) {
                return body.wide_registers.at(reg);
            }
            return uword_value(body.registers[reg], 0U, width, ValueKind::logic4);
        }
        if (reg >= member.registers.size()) {
            fail(member_index, pc, "invalid register ID");
        }
        return member.registers[reg];
    };
    auto outcome = Boundary::none;
    // process_suspend is raised once the wait (or halt) is registered.
    bool suspend_point = false;
    [&] {
        using T = Op;
        if constexpr (std::is_same_v<T, DebugPoint>) {
            // Debugger plan (also a deoptimized member's reference path):
            // the execution point of a statement, call, wait, assertion or
            // process entry. A requested stop pauses the thread right after
            // it; it resumes at the next operation when the run continues.
            if (debug_point_executable(op.kind)) {
                outcome = debug_point(index, pc) ? Boundary::suspend
                                                 : Boundary::advance;
            }
        } else if constexpr (std::is_same_v<T, WaitFor>) {
            suspend_point = true;
            auto delay = op.delay;
            if (op.source) {
                try {
                    delay = normalized_dynamic_wait_delay(op, read(*op.source));
                } catch (const std::exception& error) {
                    fail(member_index, pc, error.what());
                }
            }
            if (member.vhdl) {
                // IEEE 1076-2019 10.2: the process resumes when the time
                // expires, in the delta after that time's signal updates;
                // `wait for 0 ns` resumes in the next delta cycle.
                threads_[index].deadline.reset();
                threads_[index].timeout_result.reset();
                if (delay == 0U) {
                    ready_thread(index);
                } else {
                    const auto now = kernel_now();
                    // A wait beyond TIME'HIGH never resumes (14.7.5.1).
                    if (delay <= std::numeric_limits<SimulationTick>::max() - now) {
                        vhdl_timers_.push({ now + delay, timer_sequence_++,
                            index, threads_[index].epoch });
                    }
                }
            } else if (delay == 0U) {
                // #0 resumes in the Inactive region of this time step.
                inactive_threads_.push_back(index);
            } else {
                const auto now = kernel_now();
                // A wait beyond the last representable time never resumes.
                if (delay <= std::numeric_limits<SimulationTick>::max() - now) {
                    timers_.push({ now + delay, timer_sequence_++, index,
                        threads_[index].epoch });
                }
            }
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, WaitOn>) {
            suspend_point = true;
            std::vector<Sensitivity> entries;
            entries.reserve(op.signals.size());
            for (std::size_t entry = 0U; entry < op.signals.size(); ++entry) {
                entries.push_back({ op.signals[entry],
                    op.edges.empty() ? EdgeKind::any : op.edges[entry] });
            }
            wait_on(index, entries);
            if (member.vhdl) {
                auto& thread = threads_[index];
                if (!op.timeout) {
                    thread.deadline.reset();
                    thread.timeout_result.reset();
                } else {
                    const auto now = kernel_now();
                    if (op.timeout_origin) {
                        // A re-wait of a wait-until condition keeps the
                        // deadline its first wait established (10.2).
                        if (!thread.deadline) {
                            fail(member_index, pc,
                                "WaitOn timeout rearm has no matching active deadline");
                        }
                    } else {
                        auto timeout = *op.timeout;
                        if (op.timeout_source) {
                            // A runtime TIME timeout (10.2); the lowering
                            // has already rejected a negative value.
                            WaitFor scale;
                            scale.delay = 1U;
                            scale.source = op.timeout_source;
                            scale.source_width = op.timeout_source_width;
                            scale.source_signed = true;
                            try {
                                timeout = normalized_dynamic_wait_delay(
                                    scale, read(*op.timeout_source));
                            } catch (const std::exception& error) {
                                fail(member_index, pc, error.what());
                            }
                        }
                        if (timeout
                            > std::numeric_limits<SimulationTick>::max() - now) {
                            fail(member_index, pc,
                                "simulation time overflow in WaitOn timeout");
                        }
                        thread.deadline = now + timeout;
                    }
                    thread.timeout_result = op.timeout_result;
                    if (*thread.deadline <= now) {
                        ready_thread(index);
                    } else {
                        vhdl_timers_.push({ *thread.deadline, timer_sequence_++,
                            index, thread.epoch });
                    }
                }
            }
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, WaitSensitivity>) {
            suspend_point = true;
            wait_on(index, member.wait_sensitivity);
            if (member.vhdl) {
                threads_[index].deadline.reset();
                threads_[index].timeout_result.reset();
            }
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, WaitForever>) {
            suspend_point = true;
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, Halt>
            || std::is_same_v<T, ForkEnd>) {
            suspend_point = true;
            outcome = Boundary::finish;
        } else if constexpr (std::is_same_v<T, Stop>) {
            if (op.status) {
                const auto value = read(*op.status).known_signed_value();
                if (!value) {
                    fail(member_index, pc,
                        "STD.ENV simulator status is not a known signed INTEGER");
                }
                impl_.simulator_status = *value;
            } else {
                impl_.simulator_status.reset();
            }
            impl_.stopped_by_design = true;
            if (warp_time_) {
                // The host reaches the warped time step first, then stops.
                schedule_stop_at(*warp_time_);
            } else {
                impl_.scheduler.request_stop();
            }
            stopped_ = true;
            outcome = Boundary::finish;
        } else if constexpr (std::is_same_v<T, Pause>) {
            // $stop (IEEE 1800-2023 20.2) suspends the simulation at a safe
            // boundary: the host run ends as stopped, nothing is finished,
            // and the process resumes after the $stop when the run
            // continues.
            if (op.status) {
                const auto value = read(*op.status).known_signed_value();
                if (!value) {
                    fail(member_index, pc,
                        "STD.ENV simulator status is not a known signed INTEGER");
                }
                impl_.simulator_status = *value;
            } else {
                impl_.simulator_status.reset();
            }
            if (warp_time_) {
                schedule_stop_at(*warp_time_);
            } else {
                impl_.scheduler.request_stop();
                // As debug_stop: the host is requeued in this region so the
                // continued run activates the kernel and the paused thread
                // resumes there.
                impl_.queue_current(host_process_);
            }
            stopped_ = true;
            paused_thread_ = index;
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, DisableBlock>) {
            outcome = disable_block(index, pc, op);
        } else if constexpr (std::is_same_v<T, Fork>) {
            std::uint32_t group = no_slot;
            if (op.join != ForkJoinKind::none) {
                if (!free_fork_groups_.empty()) {
                    group = free_fork_groups_.back();
                    free_fork_groups_.pop_back();
                } else {
                    group = static_cast<std::uint32_t>(fork_groups_.size());
                    fork_groups_.emplace_back();
                }
                fork_groups_[group] = ForkGroup { index, op.join,
                    static_cast<std::uint32_t>(op.branches.size()), false };
            }
            for (const auto branch : op.branches) {
                ready_thread(spawn_thread(member_index, branch, index, group, pc));
            }
            outcome = op.join == ForkJoinKind::none ? Boundary::advance
                                                    : Boundary::suspend;
            suspend_point = op.join != ForkJoinKind::none;
        }
    }();
    (void)op;
    if (suspend_point && debug_points_) {
        debug_suspend(index, pc);
    }
    return outcome;
}

bool Interpreter::Impl::StaticKernel::debug_point(
    const std::uint32_t thread_index, const std::uint32_t pc)
{
    auto& thread = threads_[thread_index];
    thread.last_point = pc;
    auto& member = members_[thread.member];
    member.debug_pc = pc;
    if (member.debug_hit.size() != member.operations.size()) {
        member.debug_hit.assign(member.operations.size(), 0U);
    }
    if (std::exchange(member.debug_hit[pc], std::uint8_t { 1 }) == 0U) {
        // A newly reached statement may have made more registers written.
        member.debug_defined_pc = no_slot;
    }
    if (!impl_.execution_point_hook) {
        return false;
    }
    const auto* point = operation_get_if<DebugPoint>(&member.operations[pc]);
    if (point == nullptr) {
        return false;
    }
    // The kinds map as the reference evaluator maps them
    // (simir_execution_boundaries.cpp).
    auto kind = ExecutionPointKind::statement;
    switch (point->kind) {
    case DebugPointKind::statement:
        kind = ExecutionPointKind::statement;
        break;
    case DebugPointKind::call:
        kind = ExecutionPointKind::call;
        break;
    case DebugPointKind::wait:
        kind = ExecutionPointKind::wait;
        break;
    case DebugPointKind::assertion:
        kind = ExecutionPointKind::assertion;
        break;
    case DebugPointKind::process_entry:
        kind = ExecutionPointKind::process_entry;
        break;
    case DebugPointKind::block_entry:
    case DebugPointKind::block_exit:
        return false;
    }
    const auto instruction = static_cast<InstructionIndex>(
        pc < member.origin.size() ? member.origin[pc] : pc);
    // A member is a design process: its runtime and design ids agree; a fork
    // child has an id of its own.
    impl_.notify_execution_point(
        thread.debug_process != no_slot ? thread.debug_process : member.process,
        member.process,
        program_of(member), instruction, kind,
        point->source, point->scope.str());
    if (!impl_.scheduler.stop_requested()) {
        return false;
    }
    debug_stop(thread_index);
    return true;
}

void Interpreter::Impl::StaticKernel::debug_suspend(
    const std::uint32_t thread_index, const std::uint32_t pc)
{
    if (!impl_.execution_point_hook) {
        return;
    }
    const auto& thread = threads_[thread_index];
    const auto& member = members_[thread.member];
    SourceLocation source;
    std::string scope;
    if (thread.last_point < member.operations.size()) {
        if (const auto* point = operation_get_if<DebugPoint>(
                &member.operations[thread.last_point])) {
            source = point->source;
            scope = point->scope.str();
        }
    }
    const auto instruction = static_cast<InstructionIndex>(
        pc < member.origin.size() ? member.origin[pc] : pc);
    impl_.notify_execution_point(
        thread.debug_process != no_slot ? thread.debug_process : member.process,
        member.process, program_of(member), instruction,
        ExecutionPointKind::process_suspend, source, scope);
    if (impl_.scheduler.stop_requested()) {
        // The thread is already registered as waiting: only the run stops.
        debug_stop(no_slot);
    }
}

void Interpreter::Impl::StaticKernel::debug_stop(const std::uint32_t paused)
{
    // The kernel stops like a design $stop (Pause): its drain loops end at
    // `stopped_`, ready threads stay ready, and `paused` resumes first. The
    // host process is requeued in this region so that the next run
    // activates the kernel again without waiting for an event.
    debug_stopped_ = true;
    stopped_ = true;
    if (paused != no_slot) {
        paused_thread_ = paused;
    }
    impl_.queue_current(host_process_);
}

bool Interpreter::Impl::StaticKernel::debug_member(
    const ProcessId process) const noexcept
{
    return std::ranges::any_of(members_, [&](const Member& member) {
        return member.process == process
            && member.kind == StaticKernelMemberKind::behavioral;
    });
}

PackedLogic4 Interpreter::Impl::StaticKernel::debug_register(
    const ProcessId process, const RegisterId reg,
    const std::size_t width) const
{
    const auto found = std::ranges::find_if(members_, [&](const Member& member) {
        return member.process == process;
    });
    if (found == members_.end()) {
        throw std::logic_error { "debug process is not a kernel member" };
    }
    const auto& member = *found;
    if (!debug_register_defined(member, reg)) {
        throw std::logic_error { "SimIR debug local has not been initialized" };
    }
    PackedLogic4 value;
    if (member.compiled && !member.generic_mode) {
        const auto& body = *member.compiled;
        if (reg >= body.register_widths.size()
            || body.register_widths[reg] == 0U) {
            throw std::logic_error { "SimIR debug local has not been initialized" };
        }
        const auto register_width = body.register_widths[reg];
        value = register_width > 64U && reg < body.wide_base.size()
                && body.wide_base[reg] != 0U
            ? wide_value(body, body.registers.data(), reg, ValueKind::logic4)
            : register_width > 64U
            ? body.wide_registers.at(reg)
            : uword_value(body.registers[reg], 0U, register_width,
                  ValueKind::logic4);
    } else {
        if (reg >= member.registers.size()) {
            throw std::out_of_range { "invalid SimIR register ID" };
        }
        value = member.registers[reg];
    }
    if (value.width() != width) {
        throw std::logic_error { "SimIR debug local has not been initialized" };
    }
    return value;
}

std::optional<ProcessId> Interpreter::Impl::StaticKernel::debug_owner(
    const ProcessId process) const noexcept
{
    if (const auto child = debug_children_.find(process);
        child != debug_children_.end()) {
        return members_[child->second].process;
    }
    if (debug_member(process)) {
        return process;
    }
    return std::nullopt;
}

ContainerValue Interpreter::Impl::StaticKernel::debug_container_register(
    const ProcessId process, const ContainerRegisterId reg) const
{
    const auto found = std::ranges::find_if(members_, [&](const Member& member) {
        return member.process == process;
    });
    if (found == members_.end() || reg >= found->container_registers.size()
        || found->container_registers[reg] == no_container) {
        throw std::logic_error { "SimIR debug container local is unbound" };
    }
    // Kernel containers are fixed packed or scalar arrays: element ordinal
    // order is the reference value's element order.
    const auto& container = containers_[found->container_registers[reg]];
    ContainerValue value;
    value.type = container.type;
    value.elements.reserve(container.count);
    for (std::uint32_t ordinal = 0U; ordinal < container.count; ++ordinal) {
        value.elements.push_back(container_element(container, ordinal));
    }
    return value;
}

void Interpreter::Impl::StaticKernel::debug_write_register(
    const ProcessId process, const RegisterId reg, const PackedLogic4& value)
{
    const auto found = std::ranges::find_if(members_, [&](Member& member) {
        return member.process == process;
    });
    if (found == members_.end()) {
        throw std::logic_error { "debug process is not a kernel member" };
    }
    auto& member = *found;
    if (member.compiled && !member.generic_mode) {
        auto& body = *member.compiled;
        if (reg >= body.register_widths.size()
            || body.register_widths[reg] != value.width()) {
            throw std::invalid_argument {
                "invalid SimIR scalar debug-local payload"
            };
        }
        const auto converted = Impl::coerce_value_kind(value, ValueKind::logic4);
        if (value.width() > 64U && reg < body.wide_base.size()
            && body.wide_base[reg] != 0U) {
            (void)set_wide_value(body, body.registers.data(), reg, converted);
        } else if (value.width() > 64U) {
            body.wide_registers.at(reg) = converted;
        } else {
            const auto aval = converted.aval_words();
            const auto bval = converted.bval_words();
            body.registers[reg] = { aval.empty() ? 0U : aval[0],
                bval.empty() ? 0U : bval[0] };
        }
        // Two-state code assumes known registers; recheck before using it.
        member.registers_known = false;
        return;
    }
    if (reg >= member.registers.size()) {
        throw std::out_of_range { "invalid SimIR register ID" };
    }
    member.registers[reg] = Impl::coerce_value_kind(value,
        reg < member.register_kinds.size() ? member.register_kinds[reg]
                                           : ValueKind::logic4);
}

std::string Interpreter::Impl::StaticKernel::debug_string_register(
    const ProcessId process, const StringRegisterId reg) const
{
    const auto found = std::ranges::find_if(members_, [&](const Member& member) {
        return member.process == process;
    });
    if (found == members_.end() || reg >= found->strings.size()) {
        throw std::logic_error { "SimIR debug string local is unavailable" };
    }
    return found->strings[reg];
}

void Interpreter::Impl::StaticKernel::run_thread(const std::uint32_t index)
{
    const auto member_index = threads_[index].member;
    auto& member = members_[member_index];
    if (trace_) {
        std::cerr << "fsim-kernel-trace: t=" << impl_.scheduler.now() << "+" << impl_.scheduler.delta();
        if (warp_time_) {
            std::cerr << " warp=" << *warp_time_;
        }
        std::cerr << " thread " << impl_.processes.program_view(member.process).name()
                  << '\n';
    }
    struct RunningThread {
        std::uint32_t& running;
        std::uint32_t saved;
        RunningThread(std::uint32_t& value, const std::uint32_t thread) noexcept
            : running(value)
            , saved(value)
        {
            running = thread;
        }
        ~RunningThread() { running = saved; }
        RunningThread(const RunningThread&) = delete;
        RunningThread& operator=(const RunningThread&) = delete;
    } running { running_thread_, index };
    // The member's call stack and automatic frames belong to the running
    // thread; registers and strings are shared by all of its threads.
    std::swap(member.call_stack, threads_[index].call_stack);
    std::swap(member.frames, threads_[index].frames);
    std::swap(member.frame_bytes, threads_[index].frame_bytes);
    std::swap(member.saved_arrays, threads_[index].saved_arrays);
    std::swap(member.saved_array_marks, threads_[index].saved_array_marks);
    if (member.thread_contexts) {
        load_contexts(index);
    }
    if (threads_[index].timeout_result) {
        // A VHDL wait with timeout resumed: the result register tells the
        // timeout from an event (an event at the deadline counts as the
        // timeout, as for the reference evaluator).
        auto& waiting = threads_[index];
        const auto reg = *waiting.timeout_result;
        const bool timed_out = waiting.deadline
            && *waiting.deadline <= kernel_now();
        if (reg >= member.registers.size()) {
            fail(member_index, waiting.pc, "invalid register ID");
        }
        member.registers[reg] = Impl::coerce_value_kind(
            PackedLogic4::from_msb_string(timed_out ? "1" : "0"),
            reg < member.register_kinds.size() ? member.register_kinds[reg]
                                               : ValueKind::logic4);
        waiting.timeout_result.reset();
    }
    auto pc = threads_[index].pc;
    const auto size = static_cast<std::uint32_t>(member.operations.size());
    const auto park = [&](const std::uint32_t resume) {
        if (member.thread_contexts) {
            store_contexts(index);
        }
        auto& thread = threads_[index];
        std::swap(member.call_stack, thread.call_stack);
        std::swap(member.frames, thread.frames);
        std::swap(member.frame_bytes, thread.frame_bytes);
        std::swap(member.saved_arrays, thread.saved_arrays);
        std::swap(member.saved_array_marks, thread.saved_array_marks);
        thread.pc = resume;
    };
    for (;;) {
        if (pc >= size) {
            park(pc);
            fail(member_index, pc, "behavioral thread left its operation stream");
        }
        auto outcome = Boundary::none;
        if (member.compiled && !member.generic_mode) {
            const auto stopped_at = run_behavioral_compiled(index, pc);
            if (!stopped_at) {
                // Deoptimized: the member continues on the reference
                // evaluator from the returned position.
                pc = deopt_resume_;
                continue;
            }
            pc = *stopped_at;
            // Native code named the suspension's boundary helper; the
            // kernel bytecode tier did not.
            if (auto* helper = std::exchange(behavioral_boundary_helper_, nullptr)) {
                outcome = static_cast<Boundary>(
                    reinterpret_cast<std::uint32_t (*)(void*, std::uint32_t,
                        std::uint32_t)>(helper)(this, index, pc));
            } else {
                outcome = behavioral_boundary(index, pc);
            }
        } else if (const auto stopped = [&] {
                       // Native operation-tier code from `pc` to the next
                       // suspension or finish.
                       const auto outer = std::exchange(operation_tier_thread_, index);
                       const auto result = run_operation_tier(member_index, pc, true);
                       operation_tier_thread_ = outer;
                       return result;
                   }()) {
            if (*stopped != operation_tier_stop) {
                pc = *stopped;
                continue;
            }
            pc = operation_tier_pc_;
            outcome = operation_tier_boundary_;
        } else {
            outcome = behavioral_boundary(index, pc);
            if (outcome == Boundary::none) {
                const InterpretingScope scope { *this, member.compiled
                        ? InterpretedCause::generic_mode
                        : InterpretedCause::thread };
                if (behavioral_step(member_index, pc)) {
                    ++pc;
                } else {
                    pc = step_generic(member_index, pc);
                }
                continue;
            }
        }
        switch (outcome) {
        case Boundary::none:
            fail(member_index, pc, "behavioral thread stopped at a non-boundary");
        case Boundary::advance:
            ++pc;
            continue;
        case Boundary::suspend:
            park(pc + 1U);
            return;
        case Boundary::finish:
            park(pc);
            finish_thread(index);
            return;
        case Boundary::redirect:
            // A disable ended the block the thread runs in.
            pc = redirect_pc_;
            continue;
        case Boundary::killed:
            // A disable ended the thread (its fork group already counts it).
            park(pc);
            release_thread(index);
            return;
        }
    }
}

std::optional<std::uint32_t> Interpreter::Impl::StaticKernel::run_behavioral_compiled(
    const std::uint32_t index, const std::uint32_t start)
{
    const auto member_index = threads_[index].member;
    auto& member = members_[member_index];
    auto& body = *member.compiled;
    auto& thread = threads_[index];
    // The synthetic call stack lives in the shared register file; it holds
    // the running thread's return targets.
    if (body.call_stack_base != 0U) {
        const auto base = body.call_stack_base;
        body.registers[base] = { thread.stack.empty() ? 0U : thread.stack.front().a, 0U };
        for (std::size_t entry = 1U; entry < thread.stack.size(); ++entry) {
            body.registers[base + entry] = thread.stack[entry];
        }
    }
    behavioral_entry_ = start;
    behavioral_suspended_.reset();
    behavioral_boundary_helper_ = nullptr;
    if (member.native.entry == nullptr && member.native.lazy_template != no_slot) {
        note_lazy_run(member.native.lazy_template);
    }
    try {
        if (member.native.entry != nullptr) {
            run_native(member.native, body, member_index);
        } else {
            executed_steps_ = 0U;
            try {
                execute_body(body, member_index);
            } catch (const KernelDeopt&) {
                if (member.native.lazy_template != no_slot) {
                    note_lazy_work(member.native.lazy_template, executed_steps_);
                }
                throw;
            }
            if (member.native.lazy_template != no_slot) {
                note_lazy_work(member.native.lazy_template, executed_steps_);
            }
        }
    } catch (const KernelDeopt& deopt) {
        scratch().pending_deopt = deopt;
    }
    if (scratch().pending_deopt) {
        const auto deopt = *scratch().pending_deopt;
        scratch().pending_deopt.reset();
        // Every thread of this member continues on the reference evaluator:
        // move the compiled registers and this thread's stack into the
        // member's reference state.
        sync_behavioral_registers(member_index, deopt.executed ? deopt.keep
                                                               : no_slot);
        if (body.call_stack_base != 0U) {
            const auto depth = body.registers[body.call_stack_base].a;
            member.call_stack.clear();
            for (std::uint64_t entry = 0U; entry < depth; ++entry) {
                member.call_stack.push_back(static_cast<InstructionIndex>(
                    body.registers[body.call_stack_base + 1U + entry].a));
            }
        }
        // Other threads of the member suspended in compiled code carry
        // their return targets in compiled form.
        for (std::uint32_t other = 0U; other < threads_.size(); ++other) {
            auto& state = threads_[other];
            if (other == index || !state.alive || state.member != member_index) {
                continue;
            }
            state.call_stack.clear();
            for (std::size_t entry = 1U; entry < state.stack.size(); ++entry) {
                state.call_stack.push_back(
                    static_cast<InstructionIndex>(state.stack[entry].a));
            }
            state.stack.clear();
        }
        member.generic_mode = true;
        deopt_resume_ = deopt.executed ? deopt.resume + 1U : deopt.resume;
        return std::nullopt;
    }
    if (!behavioral_suspended_) {
        fail(member_index, start, "behavioral thread ran past its operations");
    }
    if (body.call_stack_base != 0U) {
        const auto base = body.call_stack_base;
        const auto depth = body.registers[base].a;
        thread.stack.assign(1U + depth, { });
        for (std::size_t entry = 0U; entry <= depth; ++entry) {
            thread.stack[entry] = body.registers[base + entry];
        }
    }
    return behavioral_suspended_;
}

void Interpreter::Impl::StaticKernel::sync_behavioral_registers(
    const std::uint32_t member_index, const std::uint32_t keep)
{
    auto& member = members_[member_index];
    const auto& body = *member.compiled;
    for (std::size_t reg = 0U; reg < member.registers.size(); ++reg) {
        if (reg == keep || reg >= body.register_widths.size()) {
            continue;
        }
        const auto width = body.register_widths[reg];
        if (width == 0U) {
            continue;
        }
        if (width > 64U) {
            if (reg < body.wide_registers.size()) {
                member.registers[reg] = body.wide_registers[reg];
            }
            continue;
        }
        member.registers[reg] = uword_value(body.registers[reg], 0U, width,
            ValueKind::logic4);
    }
}

void Interpreter::Impl::StaticKernel::collect_timers()
{
    const auto now = kernel_now();
    while (!timers_.empty() && timers_.top().time <= now) {
        const auto timer = timers_.top();
        timers_.pop();
        const auto& thread = threads_[timer.thread];
        if (thread.alive && !thread.queued && thread.epoch == timer.epoch) {
            ready_thread(timer.thread);
        }
    }
    // VHDL timers expire in the update phase of their time (vhdl_timer_due)
    // and resume their processes in the following delta.
    if (vhdl_timers_released_) {
        vhdl_timers_released_ = false;
        while (!vhdl_timers_.empty() && vhdl_timers_.top().time <= now) {
            const auto timer = vhdl_timers_.top();
            vhdl_timers_.pop();
            const auto& thread = threads_[timer.thread];
            if (thread.alive && !thread.queued && thread.epoch == timer.epoch) {
                ready_thread(timer.thread);
            }
        }
    }
}

void Interpreter::Impl::StaticKernel::arm_vhdl_timer()
{
    while (!vhdl_timers_.empty()) {
        const auto& top = vhdl_timers_.top();
        const auto& thread = threads_[top.thread];
        if (thread.alive && !thread.queued && thread.epoch == top.epoch) {
            break;
        }
        vhdl_timers_.pop();
    }
    if (vhdl_timers_.empty() || stopped_) {
        return;
    }
    const auto time = vhdl_timers_.top().time;
    if (time <= impl_.scheduler.now()) {
        return;
    }
    // A task already pending at this time, or at an earlier one (which
    // re-arms when it runs), covers the timer.
    if (armed_vhdl_timer_ && *armed_vhdl_timer_ <= time
        && *armed_vhdl_timer_ > impl_.scheduler.now()) {
        return;
    }
    armed_vhdl_timer_ = time;
    struct KernelVhdlTimerTask {
        Interpreter::Impl* owner { };
        SimulationTick time { };
    };
    const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
        KernelVhdlTimerTask,
        +[](Scheduler&, const KernelVhdlTimerTask& scheduled) {
            if (scheduled.owner->static_kernel) {
                scheduled.owner->static_kernel->vhdl_timer_due(scheduled.time);
            }
        }>(KernelVhdlTimerTask { &impl_, time });
    // IEEE 1076-2019 14.7.5.3: the signals update first; the processes whose
    // timeout expired run in the next delta with those the updates wake.
    impl_.scheduler.schedule_internal_at(
        time, SchedulerPhase::update, host_process_, task);
}

void Interpreter::Impl::StaticKernel::vhdl_timer_due(const SimulationTick time)
{
    if (armed_vhdl_timer_ && *armed_vhdl_timer_ == time) {
        armed_vhdl_timer_.reset();
    }
    vhdl_timers_released_ = true;
    impl_.queue_next_delta(host_process_);
}

void Interpreter::Impl::StaticKernel::arm_timer()
{
    while (!timers_.empty()) {
        const auto& top = timers_.top();
        const auto& thread = threads_[top.thread];
        if (thread.alive && !thread.queued && thread.epoch == top.epoch) {
            break;
        }
        timers_.pop();
    }
    if (stopped_) {
        return;
    }
    if (timers_.empty()) {
        if (warp_time_ && *warp_time_ > impl_.scheduler.now()) {
            // The last warped step ended the run: time must still reach it,
            // so the run completes at the time of its last event.
            struct KernelWarpEndTask {
            };
            const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
                KernelWarpEndTask,
                +[](Scheduler&, const KernelWarpEndTask&) { }>(
                KernelWarpEndTask { });
            impl_.scheduler.schedule_internal_systemverilog_at(
                *warp_time_, SchedulerPhase::active, host_process_, task);
        }
        return;
    }
    const auto time = timers_.top().time;
    if (armed_timer_ && *armed_timer_ <= time
        && *armed_timer_ > impl_.scheduler.now()) {
        return;
    }
    if (warp_time_ && time <= impl_.scheduler.now()) {
        return;
    }
    armed_timer_ = time;
    struct KernelTimerTask {
        Interpreter::Impl* owner { };
        SimulationTick time { };
    };
    const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
        KernelTimerTask,
        +[](Scheduler&, const KernelTimerTask& scheduled) {
            if (scheduled.owner->static_kernel) {
                scheduled.owner->static_kernel->timer_due(scheduled.time);
            }
        }>(KernelTimerTask { &impl_, time });
    impl_.scheduler.schedule_internal_systemverilog_at(
        time, SchedulerPhase::active, host_process_, task);
}

std::optional<SimulationTick> Interpreter::Impl::StaticKernel::next_timer()
{
    while (!timers_.empty()) {
        const auto& top = timers_.top();
        const auto& thread = threads_[top.thread];
        if (thread.alive && !thread.queued && thread.epoch == top.epoch) {
            return top.time;
        }
        timers_.pop();
    }
    return std::nullopt;
}

void Interpreter::Impl::StaticKernel::schedule_stop_at(const SimulationTick time)
{
    struct KernelStopTask {
        Interpreter::Impl* owner { };
    };
    const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
        KernelStopTask,
        +[](Scheduler&, const KernelStopTask& scheduled) {
            scheduled.owner->scheduler.request_stop();
        }>(KernelStopTask { &impl_ });
    impl_.scheduler.schedule_internal_systemverilog_at(
        time, SchedulerPhase::active, host_process_, task);
}

void Interpreter::Impl::StaticKernel::timer_due(const SimulationTick time)
{
    if (armed_timer_ && *armed_timer_ == time) {
        armed_timer_.reset();
    }
    activate_for_due_timer();
}

void Interpreter::Impl::StaticKernel::activate_for_due_timer()
{
    // A process whose delay expired runs in the Active round its time step
    // starts with (IEEE 1800-2023 4.4.2.2, 9.4.1), as on the reference
    // scheduler: the timer task activates the kernel itself, which collects
    // the due timers, as the host Inactive and NBA tasks do. Queueing the
    // host process instead would run them a round later, behind every other
    // process of the step.
    if (mixed_ || vhdl_) {
        // A VHDL or mixed kernel's rounds are deltas interleaved with the
        // host's (S16): it runs as its host process.
        impl_.queue_active_current(host_process_);
        return;
    }
    if (activate()) {
        impl_.queue_next_delta(host_process_);
    }
}

} // namespace fsim::runtime::simir

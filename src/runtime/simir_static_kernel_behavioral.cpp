// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 behavioral tier (docs/simulation-engine-v4-progress.md §4b):
// testbench processes run inside the static kernel as resumable threads. A
// thread runs until it suspends on a delay, a dynamic or static wait, a fork
// join, a halt or $finish; waits on kernel-owned slots and on host inputs, and
// delays on a kernel timer queue, make it ready again.
#include "fsim/runtime/file_scanning.hpp"
#include "simir_container_ops.hpp"
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
    if (parent == no_slot) {
        members_[member].root_thread = index;
    }
    if (parent != no_slot) {
        ++threads_[parent].child_records;
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
    if (const auto phase = members_[thread.member].phase;
        phase != SchedulerPhase::active) {
        // A wake schedules the process in its own region (the reference's
        // execution phase; IEEE 1800-2023 4.4.2), at its stable order among
        // that region's processes.
        ready_thread_in(index, phase);
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
    if (hosted_running_region_) {
        // Woken from a later region's run (a thread there ended a fork,
        // say): the host runs the kernel's Active work again, as
        // enqueue_hosted does for a hosted process.
        impl_.queue_active_current(host_process_);
    }
}

void Interpreter::Impl::StaticKernel::ready_thread_in(const std::uint32_t index,
    const SchedulerPhase phase)
{
    auto& thread = threads_[index];
    if (!thread.alive || thread.queued) {
        return;
    }
    // A resumed thread abandons every other registration of its wait.
    ++thread.epoch;
    thread.queued = true;
    HostedReady ready;
    ready.order = thread.order;
    ready.process = members_[thread.member].process;
    ready.thread = index;
    ready.phase = phase;
    push_hosted_region(ready);
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
    // A process that is not initialized (an implicit signal's driver, IEEE
    // 1076-2019 16.2.4) is armed the same way without running: it waits on
    // its static sensitivity and runs from its first operation when that
    // wakes it (Interpreter::add_process, waiting_on_static).
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        auto& member = members_[index];
        if (member.kind != StaticKernelMemberKind::behavioral
            || member.start_late || member.final_block
            || member.package_startup || member.resolver
            || member.wait_sensitivity.empty()
            || member.operations.empty()) {
            continue;
        }
        if (member.start_waiting) {
            if (trace_) {
                std::cerr << "fsim-kernel-trace: arm "
                          << impl_.processes.program_view(member.process).name()
                          << '\n';
            }
            member.prearmed = true;
            wait_on(spawn_thread(index, 0U, no_slot, no_slot),
                member.wait_sensitivity);
            continue;
        }
        if (!operation_holds<WaitSensitivity>(member.operations.front())) {
            continue;
        }
        member.prearmed = true;
        const auto thread = spawn_thread(index, 0U, no_slot, no_slot);
        run_thread(thread);
    }
}

void Interpreter::Impl::StaticKernel::run_behavioral_initializers()
{
    // A declaration initializer that is a thread member (one that allocates
    // through a class constructor, say) sets its static variable before any
    // initial or always procedure starts (IEEE 1800-2023 6.8, 10.5), as
    // run_hosted_initializers does for hosted ones: it runs here, in process
    // order, ahead of the once members and the threads.
    std::vector<std::uint32_t> initializers;
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        const auto& member = members_[index];
        if (member.kind != StaticKernelMemberKind::behavioral || member.vhdl
            || member.prearmed || member.start_late || member.final_block
            || member.package_startup || member.resolver
            || member.operations.empty()) {
            continue;
        }
        // A member's host process is dormant, its initialize flag cleared
        // (ElaboratedDesign): the name says what it is.
        if (member_program(index).name().find(".$declaration_initializer_")
            != std::string::npos) {
            initializers.push_back(index);
        }
    }
    std::ranges::stable_sort(initializers, std::less { },
        [&](const std::uint32_t member) { return members_[member].process; });
    for (const auto index : initializers) {
        if (trace_) {
            std::cerr << "fsim-kernel-trace: initialize "
                      << member_program(index).name() << '\n';
        }
        // start_behavioral does not start it again.
        members_[index].prearmed = true;
        run_thread(spawn_thread(index, 0U, no_slot, no_slot));
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
                || member.prearmed || member.final_block
                || member.package_startup || member.resolver
                || (member.after_procedures && !member.vhdl)
                    != after_procedures) {
                continue;
            }
            if (trace_) {
                std::cerr << "fsim-kernel-trace: start "
                          << impl_.processes.program_view(member.process).name()
                          << (member.start_late ? " late" : "") << '\n';
            }
            if (member.start_late) {
                late_members_.push_back(index);
            } else if (member.vhdl && member.phase == SchedulerPhase::postponed) {
                // A postponed process initializes with the others (IEEE
                // 1076-2019 14.7.5.2), in the first round's VHDL work.
                const auto thread = spawn_thread(index, 0U, no_slot, no_slot);
                ++threads_[thread].epoch;
                threads_[thread].queued = true;
                vhdl_ready_threads_.push_back(thread);
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
        if (const auto family = family_of_signal_[entry.signal];
            family != no_slot) {
            // An alias proxy's change is a change of a leaf it overlaps
            // (IEEE 1800-2023 9.4.2); each overlapped leaf gets a waiter for
            // its part of the range. The first to wake makes the rest stale.
            const auto begin = entry.offset;
            const auto end = entry.width == 0U ? families_[family].width
                : entry.offset + entry.width;
            for (const auto& leaf : families_[family].leaves) {
                const auto low = std::max(begin, leaf.offset);
                const auto high = std::min(end, leaf.offset + leaf.width);
                if (low < high) {
                    slot_waiters_[leaf.slot].push_back({ index, epoch,
                        EdgeKind::any, low - leaf.offset,
                        low == leaf.offset && high == leaf.offset + leaf.width
                            ? 0U : high - low });
                }
            }
            continue;
        }
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
        // A ranged waiter wakes only when its selected bits changed; a
        // change is a transaction too.
        if (waiter.edge == EdgeKind::transaction) {
            ready_thread(waiter.thread);
            continue;
        }
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

bool Interpreter::Impl::StaticKernel::wake_switch_reader(const ProcessId process)
{
    const auto found = switch_members_.find(process);
    if (found == switch_members_.end()) {
        return false;
    }
    switch_wakes_.push_back(
        { found->second, impl_.scheduler.now(), impl_.scheduler.delta() });
    impl_.queue_next_delta(host_process_);
    return true;
}

void Interpreter::Impl::StaticKernel::resume_switch_readers()
{
    // The host runs a woken reader in the next delta (queue_next_delta),
    // whatever it waits on.
    const auto now = impl_.scheduler.now();
    const auto delta = impl_.scheduler.delta();
    std::size_t kept = 0U;
    for (const auto wake : switch_wakes_) {
        if (wake.time == now && wake.delta == delta) {
            switch_wakes_[kept++] = wake;
            continue;
        }
        const auto thread = members_[wake.member].root_thread;
        if (thread != no_slot && threads_[thread].member == wake.member) {
            ready_thread(thread);
        }
    }
    switch_wakes_.resize(kept);
}

void Interpreter::Impl::StaticKernel::wake_transaction_waiters()
{
    for (const auto input_index : transaction_inputs_) {
        auto& input = inputs_[input_index];
        const auto& stamp = impl_.signal_transactions[input.signal];
        if (stamp == input.transaction_seen) {
            continue;
        }
        input.transaction_seen = stamp;
        // A transaction without a change of value still resumes the
        // threads that wait on the signal's transactions (IEEE 1076-2019
        // 16.2.4: S'TRANSACTION toggles in each cycle S is active).
        auto& waiters = input_waiters_[input_index];
        std::size_t kept = 0U;
        for (std::size_t index = 0U; index < waiters.size(); ++index) {
            const auto waiter = waiters[index];
            const auto& thread = threads_[waiter.thread];
            if (!thread.alive || thread.queued || thread.epoch != waiter.epoch) {
                continue;
            }
            if (waiter.edge == EdgeKind::transaction) {
                ready_thread(waiter.thread);
                continue;
            }
            waiters[kept++] = waiter;
        }
        waiters.resize(kept);
    }
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
    if (thread.child_records != 0U) {
        // Its descendants still name it as their ancestor.
        thread.ended_record = true;
        return;
    }
    free_threads_.push_back(index);
    // Ended ancestors whose last descendant record this was go too.
    for (auto parent = thread.parent; parent != no_slot;) {
        auto& record = threads_[parent];
        --record.child_records;
        if (!record.ended_record || record.child_records != 0U) {
            break;
        }
        record.ended_record = false;
        free_threads_.push_back(parent);
        parent = record.parent;
    }
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
        store_context(member, context);
    }
}

void Interpreter::Impl::StaticKernel::store_context(
    const Member& member, CallableContext& context)
{
    context.values.clear();
    for (const auto id : context.ids) {
        context.values.push_back(member.registers.at(id));
    }
    context.strings.clear();
    for (const auto id : context.string_ids) {
        context.strings.push_back(member.strings.at(id));
    }
    context.arrays.resize(context.container_ids.size());
    context.dynamic.assign(context.container_ids.size(), nullptr);
    for (std::size_t at = 0U; at < context.container_ids.size(); ++at) {
        const auto id = context.container_ids[at];
        auto& words = context.arrays[at];
        words.clear();
        if (member.dynamic_container(id)) {
            // Shared: the next write copies (get_container_register).
            context.dynamic[at] = member.dynamic_containers[id];
            continue;
        }
        if (id >= member.container_registers.size()
            || member.container_registers[id] == no_container) {
            continue;
        }
        const auto& storage = containers_[member.container_registers[id]];
        if (storage.object != no_container_object) {
            continue;
        }
        const auto* begin = arena_.data() + storage.element_offset;
        words.assign(begin, begin + 2U * storage.element_words * storage.count);
    }
    context.stored = true;
}

void Interpreter::Impl::StaticKernel::load_context(
    Member& member, const CallableContext& context)
{
    for (std::size_t at = 0U; at < context.ids.size(); ++at) {
        member.registers.at(context.ids[at]) = context.values[at];
    }
    for (std::size_t at = 0U; at < context.string_ids.size(); ++at) {
        member.strings.at(context.string_ids[at]) = context.strings[at];
    }
    for (std::size_t at = 0U; at < context.container_ids.size(); ++at) {
        const auto id = context.container_ids[at];
        if (at < context.dynamic.size() && context.dynamic[at]
            && member.dynamic_container(id)) {
            member.dynamic_containers[id] = context.dynamic[at];
            continue;
        }
        const auto& words = context.arrays[at];
        if (words.empty() || id >= member.container_registers.size()
            || member.container_registers[id] == no_container) {
            continue;
        }
        const auto& storage = containers_[member.container_registers[id]];
        if (storage.object != no_container_object
            || words.size() != 2U * storage.element_words * storage.count) {
            continue;
        }
        std::ranges::copy(words, arena_.begin() + storage.element_offset);
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
        load_context(member, context);
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

std::uint32_t Interpreter::Impl::StaticKernel::next_member_random(
    const std::uint32_t member)
{
    const auto thread = running_thread_;
    if (thread != no_slot && threads_[thread].member == member
        && threads_[thread].parent != no_slot) {
        return Impl::next_random(threads_[thread].random_state);
    }
    return Impl::next_random(
        impl_.get_process(members_[member].process).cold().random_state);
}

ProcessId Interpreter::Impl::StaticKernel::running_process(
    const std::uint32_t member) const
{
    const auto thread = running_thread_;
    return thread != no_slot && threads_[thread].member == member
        ? static_cast<ProcessId>(threads_[thread].order)
        : members_[member].process;
}

bool Interpreter::Impl::StaticKernel::only_detached_children(
    const std::uint32_t parent) const
{
    return std::ranges::all_of(threads_, [&](const Thread& thread) {
        return !thread.alive || thread.parent != parent || thread.detached;
    });
}

std::vector<std::uint32_t> Interpreter::Impl::StaticKernel::child_records(
    const std::uint32_t parent) const
{
    std::vector<std::uint32_t> children;
    for (std::uint32_t index = 0U; index < threads_.size(); ++index) {
        const auto& thread = threads_[index];
        if ((thread.alive || thread.ended_record) && thread.parent == parent) {
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
    // Like the reference's cancel_fork_descendants: the descendants of an
    // ended child end too.
    for (const auto child : child_records(parent)) {
        cancel_thread_descendants(child);
        kill_thread(child);
    }
}

void Interpreter::Impl::StaticKernel::disable_fork_site(
    const std::uint32_t index, const std::uint32_t site)
{
    // The reference's DisableFork of a named fork (simir_fork.cpp): the
    // nearest thread, this one or an ancestor, with live children of the
    // site owns it.
    const auto has_site = [&](const std::uint32_t owner) {
        return std::ranges::any_of(live_children(owner),
            [&](const std::uint32_t child) {
                return threads_[child].fork_site == site;
            });
    };
    auto owner = index;
    while (!has_site(owner) && threads_[owner].parent != no_slot) {
        owner = threads_[owner].parent;
    }
    const auto cancel_site = [&](const std::uint32_t site_owner) {
        for (const auto child : live_children(site_owner)) {
            if (threads_[child].fork_site != site) {
                continue;
            }
            cancel_thread_descendants(child);
            kill_thread(child);
        }
    };
    cancel_site(owner);
    // Threads an ended branch of the fork left running (its own join_none
    // children) belong to the disabled fork too.
    std::vector<std::uint32_t> descendants;
    for (std::uint32_t thread = 0U; thread < threads_.size(); ++thread) {
        if (!threads_[thread].alive) {
            continue;
        }
        for (auto link = thread; threads_[link].parent != no_slot;
             link = threads_[link].parent) {
            if (threads_[link].parent == owner
                && threads_[link].fork_site == site) {
                descendants.push_back(thread);
                break;
            }
        }
    }
    std::ranges::sort(descendants, std::less { },
        [&](const std::uint32_t thread) { return threads_[thread].order; });
    for (const auto thread : descendants) {
        if (threads_[thread].alive) {
            cancel_thread_descendants(thread);
            kill_thread(thread);
        }
    }
    // The fork ends in every concurrent activation of the code that holds
    // it (9.6.2), such as each call of an automatic task in another branch.
    const auto member = threads_[index].member;
    std::vector<std::uint32_t> activations;
    for (std::uint32_t thread = 0U; thread < threads_.size(); ++thread) {
        if (thread != owner && threads_[thread].alive
            && threads_[thread].member == member && has_site(thread)) {
            activations.push_back(thread);
        }
    }
    std::ranges::sort(activations, std::less { },
        [&](const std::uint32_t thread) { return threads_[thread].order; });
    for (const auto thread : activations) {
        cancel_site(thread);
    }
}

void Interpreter::Impl::StaticKernel::kill_thread(const std::uint32_t index)
{
    auto& thread = threads_[index];
    if (!thread.alive) {
        return;
    }
    if (thread.parent == no_slot && members_[thread.member].program_owner) {
        // A killed program process no longer keeps its program running.
        members_[thread.member].program_ended = true;
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
    // A named block of the running thread's own code (one named by
    // hierarchical name is disable_named_scope's). The running thread's
    // position is past the disable, as the reference's process counter is.
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

Interpreter::Impl::StaticKernel::Boundary
Interpreter::Impl::StaticKernel::disable_named_scope(const std::uint32_t index,
    const std::uint32_t pc, const std::vector<std::string>& scopes)
{
    if (!named_scopes_) {
        // The named intervals of the members' programs, in process order,
        // as the reference indexes its processes' programs.
        auto& named = named_scopes_.emplace();
        std::vector<std::uint32_t> order;
        for (std::uint32_t member = 0U; member < members_.size(); ++member) {
            if (members_[member].kind == StaticKernelMemberKind::behavioral
                && !members_[member].vhdl) {
                order.push_back(member);
            }
        }
        std::ranges::sort(order, std::less { },
            [&](const std::uint32_t member) { return members_[member].process; });
        for (const auto member : order) {
            const auto process = members_[member].process;
            const auto* original = startup_program(process);
            const auto program = original != nullptr
                ? ProcessProgramView { *original }
                : impl_.processes.program_view(process);
            const auto& operations = program.operations();
            std::vector<std::pair<std::string, InstructionIndex>> open;
            for (std::size_t instruction = 0U; instruction < operations.size();
                 ++instruction) {
                const auto* point = operation_get_if<DebugPoint>(
                    &std::as_const(operations)[instruction]);
                if (point == nullptr
                    || (point->kind != DebugPointKind::block_entry
                        && point->kind != DebugPointKind::block_exit)) {
                    continue;
                }
                const auto& actual = operations.debug_point(
                    static_cast<InstructionIndex>(instruction), *point);
                const auto& scope = operations.debug_scope(actual.scope).str();
                if (point->kind == DebugPointKind::block_entry) {
                    open.emplace_back(scope,
                        static_cast<InstructionIndex>(instruction));
                    continue;
                }
                for (auto entry = open.rbegin(); entry != open.rend(); ++entry) {
                    if (entry->first == scope) {
                        named[scope].push_back({ member, entry->second,
                            static_cast<std::uint32_t>(instruction) });
                        open.erase(std::next(entry).base());
                        break;
                    }
                }
            }
        }
    }
    const auto running_pc = pc + 1U;
    std::optional<std::uint32_t> redirect;
    for (const auto& scope : scopes) {
        const auto found = named_scopes_->find(scope);
        if (found == named_scopes_->end()) {
            continue;
        }
        const auto intervals = found->second;
        // The threads alive when the disable runs, in process order.
        std::vector<std::uint32_t> live;
        for (std::uint32_t thread = 0U; thread < threads_.size(); ++thread) {
            if (threads_[thread].alive) {
                live.push_back(thread);
            }
        }
        std::ranges::sort(live, std::less { },
            [&](const std::uint32_t thread) { return threads_[thread].order; });
        for (const auto& interval : intervals) {
            for (const auto thread : live) {
                if (!threads_[thread].alive
                    || threads_[thread].member != interval.member) {
                    continue;
                }
                disable_thread_interval(index, running_pc, thread,
                    interval.begin, interval.end, redirect, thread != index);
            }
        }
        break;
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
    std::optional<std::uint32_t>& running_redirect, const bool include_end)
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
    if (!thread.alive || at < begin || at > end || (at == end && !include_end)) {
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
    const auto member = threads_[index].member;
    release_thread(index);
    fork_child_done(parent, group);
    if (parent == no_slot && members_[member].program_owner) {
        complete_program_member(member);
    }
}

void Interpreter::Impl::StaticKernel::host_object_written()
{
    if (initialized_) {
        (void)scan_inputs(false);
    }
}

void Interpreter::Impl::StaticKernel::complete_program_member(
    const std::uint32_t member)
{
    // A program ends when the last of its processes ends (the fork children
    // they leave running do not keep it), as the reference's
    // complete_program_process.
    members_[member].program_ended = true;
    const auto owner = *members_[member].program_owner;
    if (std::ranges::none_of(program_members_.at(owner),
            [&](const std::uint32_t other) {
                return !members_[other].program_ended;
            })) {
        exit_program(owner);
    }
}

void Interpreter::Impl::StaticKernel::exit_program(const std::uint32_t owner)
{
    // The reference's exit_program: each live process of the program ends as
    // killed, with its fork descendants; the last program to end stops the
    // simulation (IEEE 1800-2023 24.7).
    if (impl_.exited_programs.contains(owner)) {
        return;
    }
    auto owned = program_members_.at(owner);
    std::ranges::sort(owned, std::less { },
        [&](const std::uint32_t member) { return members_[member].process; });
    for (const auto member : owned) {
        if (members_[member].program_ended) {
            continue;
        }
        members_[member].program_ended = true;
        // A member that has not started yet never does.
        std::erase(late_members_, member);
        for (std::uint32_t thread = 0U; thread < threads_.size(); ++thread) {
            if (threads_[thread].alive && threads_[thread].member == member
                && threads_[thread].parent == no_slot) {
                cancel_thread_descendants(thread);
                kill_thread(thread);
            }
        }
    }
    impl_.exited_programs.insert(owner);
    if (!impl_.program_owners.empty()
        && impl_.exited_programs.size() == impl_.program_owners.size()) {
        impl_.stopped_by_design = true;
        if (warp_time_) {
            schedule_stop_at(*warp_time_);
        } else {
            impl_.scheduler.request_stop();
        }
        stopped_ = true;
    }
}

void Interpreter::Impl::StaticKernel::fork_child_done(const std::uint32_t parent,
    const std::uint32_t group)
{
    if (parent != no_slot && threads_[parent].alive
        && threads_[parent].waiting_children
        && only_detached_children(parent)) {
        // wait fork (IEEE 1800-2023 9.6.1) ends with the last child.
        threads_[parent].waiting_children = false;
        ready_thread(parent);
    }
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
        if (member.vhdl_2019) {
            const auto& op = operation_get<Report>(member.operations[output.pc]);
            report_vhdl(output.member, output.pc, op.message.str(), op.severity,
                op.source, true);
        } else if (impl_.report_hook) {
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

void Interpreter::Impl::StaticKernel::report_vhdl(const std::uint32_t member_index,
    const std::uint32_t pc, const std::string_view message,
    const AssertionSeverity severity, const SourceLocation& source,
    const bool standalone)
{
    const auto& member = members_[member_index];
    impl_.report_vhdl(member.process,
        impl_.processes.program_view(member.process).name(),
        pc < member.origin.size() ? member.origin[pc] : pc, message, severity,
        source, standalone, kernel_now(),
        warp_time_ ? 0U : impl_.scheduler.delta());
}

struct Interpreter::Impl::StaticKernel::MemberFileStorage final
    : FileOperationStorage {
    MemberFileStorage(StaticKernel& kernel_value,
        const std::uint32_t member_value, const std::uint32_t pc_value) noexcept
        : kernel { kernel_value }, member { member_value }, pc { pc_value }
    {
    }
    [[nodiscard]] ProcessId process() const noexcept override
    {
        return kernel.members_[member].process;
    }
    [[nodiscard]] PackedLogic4& packed(const RegisterId id) override
    {
        auto& registers = kernel.members_[member].registers;
        if (id >= registers.size()) {
            kernel.fail(member, pc, "invalid register ID");
        }
        return registers[id];
    }
    [[nodiscard]] std::string& text(const StringRegisterId id) override
    {
        auto& strings = kernel.members_[member].strings;
        if (id >= strings.size()) {
            kernel.fail(member, pc, "invalid string register ID");
        }
        return strings[id];
    }
    [[nodiscard]] PackedLogic4 handle(const RegisterId id) override
    {
        return packed(id);
    }
    // commit_driver's immediate write, as the member's blocking writes make
    // it (step_operation).
    void commit(const SignalId signal, PackedLogic4 value) override
    {
        if (!kernel.owned(signal)) {
            kernel.write_host(member, signal, std::move(value), std::nullopt,
                true, SignalUpdateDomain::systemverilog_active);
            return;
        }
        if (kernel.members_[member].vhdl) {
            const auto slot = kernel.slot_of_signal_[signal];
            if (slot == static_kernel_detail::no_slot) {
                kernel.fail(member, pc,
                    "static kernel VHDL write targets an aggregate proxy");
            }
            kernel.write_slot_immediate(slot, std::move(value), std::nullopt,
                member, pc);
            return;
        }
        kernel.write_signal_owned(signal, std::move(value), std::nullopt,
            member, pc);
    }
    // A reference-format register is the reference's value; an arena one
    // (private, or bound to a kernel memory) is written back element by
    // element, as element writes would be.
    [[nodiscard]] ContainerValue container(const ContainerRegisterId id) override
    {
        auto& state = kernel.members_[member];
        if (state.dynamic_container(id)) {
            return *state.dynamic_containers[id];
        }
        return arena_value(arena_register(id));
    }
    void write_container(const ContainerRegisterId id, ContainerValue value) override
    {
        auto& state = kernel.members_[member];
        if (state.dynamic_container(id)) {
            state.dynamic_containers[id]
                = std::make_shared<ContainerValue>(std::move(value));
            return;
        }
        write_arena(arena_register(id), value);
    }
    // A kernel memory, or a host object (the host keeps the signals its
    // elements alias; the kernel rescans its inputs after the write).
    [[nodiscard]] ContainerValue object(const ContainerObjectId id) override
    {
        if (const auto index = kernel_object(id); index != static_kernel_detail::no_container) {
            return arena_value(index);
        }
        return kernel.impl_.read_container_object_value(id);
    }
    void write_object(const ContainerObjectId id, const ContainerValue& value) override
    {
        if (const auto index = kernel_object(id); index != static_kernel_detail::no_container) {
            write_arena(index, value);
            return;
        }
        kernel.impl_.write_container_object_value(id, value,
            kernel.members_[member].process);
        kernel.host_object_written();
    }
    [[noreturn]] void fail(const std::string& message) override
    {
        kernel.fail(member, pc, message);
    }
    [[nodiscard]] std::uint32_t arena_register(const ContainerRegisterId id)
    {
        const auto& registers = kernel.members_[member].container_registers;
        if (id >= registers.size() || registers[id] == static_kernel_detail::no_container) {
            kernel.fail(member, pc, "static kernel container register is not bound");
        }
        return registers[id];
    }
    [[nodiscard]] std::uint32_t kernel_object(const ContainerObjectId id) const
    {
        return id < kernel.container_of_object_.size()
            ? kernel.container_of_object_[id] : static_kernel_detail::no_container;
    }
    [[nodiscard]] ContainerValue arena_value(const std::uint32_t index) const
    {
        const auto& source = kernel.containers_[index];
        ContainerValue value;
        value.type = source.type;
        value.elements.reserve(source.count);
        for (std::uint32_t ordinal = 0U; ordinal < source.count; ++ordinal) {
            value.elements.push_back(kernel.container_element(source, ordinal));
        }
        return value;
    }
    void write_arena(const std::uint32_t index, const ContainerValue& value)
    {
        auto& target = kernel.containers_[index];
        for (std::uint32_t ordinal = 0U;
             ordinal < target.count && ordinal < value.elements.size(); ++ordinal) {
            if (!(value.elements[ordinal] == kernel.container_element(target, ordinal))) {
                kernel.write_container_element(target, ordinal,
                    value.elements[ordinal], member, pc);
            }
        }
    }
    StaticKernel& kernel;
    std::uint32_t member;
    std::uint32_t pc;
};

// A member thread's registers for the reference's assertion operations
// (run_sva, run_sampled_read). A directive's monitor belongs to the thread
// as a reference process: its stable order.
struct Interpreter::Impl::StaticKernel::MemberAssertionStorage final
    : AssertionStorage {
    MemberAssertionStorage(StaticKernel& kernel_value,
        const std::uint32_t member_value, const std::uint32_t pc_value) noexcept
        : kernel { kernel_value }, member { member_value }, pc { pc_value }
    {
    }
    [[nodiscard]] ProcessId process() const noexcept override
    {
        const auto thread = kernel.running_thread_;
        return thread != static_kernel_detail::no_slot
                && kernel.threads_[thread].member == member
            ? kernel.threads_[thread].order
            : kernel.members_[member].process;
    }
    [[nodiscard]] ProcessId design_process() const noexcept override
    {
        return kernel.members_[member].process;
    }
    [[nodiscard]] ProcessSchedulingDomain domain() const noexcept override
    {
        return ProcessSchedulingDomain::systemverilog;
    }
    [[nodiscard]] PackedLogic4 read(const RegisterId id,
        const std::size_t) override
    {
        return packed(id);
    }
    void write(const RegisterId id, const PackedLogic4& value) override
    {
        packed(id) = value;
    }
    [[noreturn]] void fail(const std::string& message) override
    {
        kernel.fail(member, pc, message);
    }
    [[nodiscard]] PackedLogic4& packed(const RegisterId id)
    {
        auto& registers = kernel.members_[member].registers;
        if (id >= registers.size()) {
            kernel.fail(member, pc, "invalid register ID");
        }
        return registers[id];
    }
    StaticKernel& kernel;
    std::uint32_t member;
    std::uint32_t pc;
};

// The member's registers for the reference's container operations
// (simir_container_ops): packed and string registers as the reference's
// frame holds them, and the reference-format container registers
// (Member::dynamic_containers), copied before a write when shared as the
// reference's get_container_register does.
// The member's registers and reports for the reference's std.env
// operations (Impl::run_vhdl_assert_api, run_vhdl_environment_time).
struct Interpreter::Impl::StaticKernel::MemberVhdlApiStorage final
    : VhdlApiStorage {
    MemberVhdlApiStorage(StaticKernel& kernel_value,
        const std::uint32_t member_value, const std::uint32_t pc_value) noexcept
        : kernel { kernel_value }, member { member_value }, pc { pc_value }
    {
    }
    [[nodiscard]] PackedLogic4 packed(const RegisterId id, std::size_t) override
    {
        const auto& registers = kernel.members_[member].registers;
        if (id >= registers.size()) {
            kernel.fail(member, pc, "invalid register ID");
        }
        return registers[id];
    }
    void write_packed(const RegisterId id, const PackedLogic4& value) override
    {
        auto& state = kernel.members_[member];
        if (id >= state.registers.size()) {
            kernel.fail(member, pc, "invalid register ID");
        }
        state.registers[id] = Impl::coerce_value_kind(value,
            id < state.register_kinds.size() ? state.register_kinds[id]
                                             : ValueKind::logic4);
    }
    [[nodiscard]] std::string text(const StringRegisterId id) override
    {
        return string(id);
    }
    void write_text(const StringRegisterId id, const std::string_view value) override
    {
        string(id) = value;
    }
    // std.env's values are reference-format registers
    // (static_kernel_vhdl_api_container_operation).
    [[nodiscard]] ContainerValue container(const ContainerRegisterId id) override
    {
        const auto& state = kernel.members_[member];
        if (!state.dynamic_container(id)) {
            kernel.fail(member, pc, "invalid container register ID");
        }
        return *state.dynamic_containers[id];
    }
    void write_container(const ContainerRegisterId id, ContainerValue value) override
    {
        auto& state = kernel.members_[member];
        if (!state.dynamic_container(id)) {
            kernel.fail(member, pc, "invalid container register ID");
        }
        state.dynamic_containers[id]
            = std::make_shared<ContainerValue>(std::move(value));
    }
    [[nodiscard]] ProcessProgramView program() override
    {
        return kernel.member_program(member);
    }
    // The running thread's (run_thread swaps it into the member).
    [[nodiscard]] std::vector<InstructionIndex> call_stack() override
    {
        return kernel.members_[member].call_stack;
    }
    void report(const std::string_view message, const AssertionSeverity severity,
        const SourceLocation& source, const bool standalone) override
    {
        kernel.report_vhdl(member, pc, message, severity, source, standalone);
    }
    [[noreturn]] void fail(const std::string& message) override
    {
        kernel.fail(member, pc, message);
    }
    [[nodiscard]] std::string& string(const StringRegisterId id)
    {
        auto& strings = kernel.members_[member].strings;
        if (id >= strings.size()) {
            kernel.fail(member, pc, "invalid string register ID");
        }
        return strings[id];
    }
    StaticKernel& kernel;
    std::uint32_t member;
    std::uint32_t pc;
};

struct Interpreter::Impl::StaticKernel::MemberRandomizeStorage final
    : RandomizeStorage {
    MemberRandomizeStorage(StaticKernel& kernel_value,
        const std::uint32_t member_value, const std::uint32_t pc_value) noexcept
        : kernel { kernel_value }, member { member_value }, pc { pc_value }
    {
    }
    [[nodiscard]] PackedLogic4& slot(const RegisterId id)
    {
        auto& registers = kernel.members_[member].registers;
        if (id >= registers.size()) {
            kernel.fail(member, pc, "invalid register ID");
        }
        return registers[id];
    }
    [[nodiscard]] PackedLogic4 packed(const RegisterId id, std::size_t) override
    {
        return slot(id);
    }
    void write_packed(const RegisterId id, PackedLogic4 value) override
    {
        slot(id) = std::move(value);
    }
    [[nodiscard]] ContainerValue container(const ContainerRegisterId id) override
    {
        const auto& state = kernel.members_[member];
        if (!state.dynamic_container(id)) {
            kernel.fail(member, pc, "invalid container register ID");
        }
        return *state.dynamic_containers[id];
    }
    void write_container(const ContainerRegisterId id, ContainerValue value) override
    {
        auto& state = kernel.members_[member];
        if (!state.dynamic_container(id)) {
            kernel.fail(member, pc, "invalid container register ID");
        }
        state.dynamic_containers[id]
            = std::make_shared<ContainerValue>(std::move(value));
    }
    [[nodiscard]] std::uint32_t next_random() override
    {
        return kernel.next_member_random(member);
    }
    [[nodiscard]] std::string scope() override
    {
        return std::string { kernel.member_program(member).name() };
    }
    StaticKernel& kernel;
    std::uint32_t member;
    std::uint32_t pc;
};

struct Interpreter::Impl::StaticKernel::MemberContainerAccess final
    : ContainerRegisterAccess {
    MemberContainerAccess(StaticKernel& kernel_value,
        const std::uint32_t member_value, const std::uint32_t pc_value) noexcept
        : kernel { kernel_value }, member { member_value }, pc { pc_value }
    {
    }
    [[nodiscard]] PackedLogic4& packed(const RegisterId id)
    {
        auto& registers = kernel.members_[member].registers;
        if (id >= registers.size()) {
            kernel.fail(member, pc, "invalid register ID");
        }
        return registers[id];
    }
    [[nodiscard]] const PackedLogic4& read(const RegisterId id) override
    {
        return packed(id);
    }
    void write(const RegisterId id, PackedLogic4 value) override
    {
        packed(id) = std::move(value);
    }
    [[nodiscard]] std::string& string(const StringRegisterId id) override
    {
        auto& strings = kernel.members_[member].strings;
        if (id >= strings.size()) {
            kernel.fail(member, pc, "invalid string register ID");
        }
        return strings[id];
    }
    [[nodiscard]] const std::string& read_string(const StringRegisterId id) override
    {
        return string(id);
    }
    [[nodiscard]] ContainerValue& container(const ContainerRegisterId id) override
    {
        auto& state = kernel.members_[member];
        if (!state.dynamic_container(id)) {
            kernel.fail(member, pc, "invalid container register ID");
        }
        auto& storage = state.dynamic_containers[id];
        if (storage.use_count() != 1) {
            storage = std::make_shared<ContainerValue>(*storage);
        }
        return *storage;
    }
    [[nodiscard]] const ContainerValue& read_container(
        const ContainerRegisterId id) override
    {
        const auto& state = kernel.members_[member];
        if (!state.dynamic_container(id)) {
            kernel.fail(member, pc, "invalid container register ID");
        }
        return *state.dynamic_containers[id];
    }
    [[nodiscard]] std::uint32_t next_random() override
    {
        return kernel.next_member_random(member);
    }
    StaticKernel& kernel;
    std::uint32_t member;
    std::uint32_t pc;
};

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
        // Reference-format registers (Member::dynamic_containers) run the
        // reference's container operations.
        if constexpr (static_kernel_reference_container_operation<T>
            || std::is_same_v<T, ContainerRead>
            || std::is_same_v<T, ContainerWrite>
            || std::is_same_v<T, CopyContainerRegister>) {
            bool dynamic = static_kernel_reference_container_operation<T>;
            if constexpr (std::is_same_v<T, ContainerRead>) {
                dynamic = member.dynamic_container(op.source);
            } else if constexpr (std::is_same_v<T, ContainerWrite>) {
                dynamic = member.dynamic_container(op.target);
            } else if constexpr (std::is_same_v<T, CopyContainerRegister>) {
                dynamic = member.dynamic_container(op.destination);
            }
            if (dynamic) {
                MemberContainerAccess access { *this, member_index, pc };
                execute_container_operation(op, access, member.process,
                    pc < member.origin.size() ? member.origin[pc] : pc);
                return;
            }
        } else if constexpr (std::is_same_v<T, ReadContainerObject>) {
            if (member.dynamic_container(op.destination)) {
                // The object's value (IEEE 1800-2023 7.6): a kernel array's
                // elements in ordinal order, else the host's.
                auto& storage = member.dynamic_containers[op.destination];
                ContainerValue value;
                if (op.object < container_of_object_.size()
                    && container_of_object_[op.object] != no_container) {
                    const auto& container = containers_[container_of_object_[op.object]];
                    value.type = container.type;
                    value.elements.reserve(container.count);
                    for (std::uint32_t ordinal = 0U; ordinal < container.count;
                         ++ordinal) {
                        value.elements.push_back(container_element(container, ordinal));
                    }
                } else {
                    value = impl_.read_container_object_value(op.object);
                }
                if (!(value.type == storage->type)) {
                    fail(member_index, pc, "container register type mismatch");
                }
                if (storage.use_count() == 1) {
                    *storage = std::move(value);
                } else {
                    storage = std::make_shared<ContainerValue>(std::move(value));
                }
                return;
            }
        } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
            if (op.object >= container_of_object_.size()
                || container_of_object_[op.object] == no_container) {
                // An element write of a host object (the planner admits
                // those without a transaction signal), as the reference
                // makes it: blocking now, nonblocking from the NBA region.
                std::vector<PackedLogic4> level_indices;
                level_indices.reserve(op.levels.size());
                for (const auto& level : op.levels) {
                    level_indices.push_back(packed_register(level.index));
                }
                impl_.write_object_element(op, member.vhdl
                        ? ProcessSchedulingDomain::generic
                        : ProcessSchedulingDomain::systemverilog,
                    member.process,
                    pc < member.origin.size() ? member.origin[pc] : pc,
                    packed_register(op.index), packed_register(op.source),
                    op.dynamic_part
                        ? std::optional { packed_register(op.dynamic_part->base) }
                        : std::nullopt,
                    std::move(level_indices));
                if (!op.nonblocking) {
                    host_object_written();
                }
                return;
            }
        } else if constexpr (std::is_same_v<T, WriteContainerObject>) {
            if (member.dynamic_container(op.source)) {
                const auto& value = *member.dynamic_containers[op.source];
                if (op.object < container_of_object_.size()
                    && container_of_object_[op.object] != no_container) {
                    // A kernel array (a fixed array of the arena's shape):
                    // changed elements are written as element writes, which
                    // notify its readers (copy_container).
                    auto& to = containers_[container_of_object_[op.object]];
                    if (!(to.type == value.type)
                        || to.count != value.elements.size()) {
                        fail(member_index, pc,
                            "static kernel container copy needs arrays of one type");
                    }
                    for (std::uint32_t ordinal = 0U; ordinal < to.count; ++ordinal) {
                        const auto& element = value.elements[ordinal];
                        if (!(element == container_element(to, ordinal))) {
                            write_container_element(to, ordinal, element,
                                member_index, pc);
                        }
                    }
                    return;
                }
                // A write of a host object (the planner keeps the object on
                // the host), as the reference makes it: blocking now,
                // nonblocking from the NBA region.
                impl_.write_object(op, member.vhdl
                        ? ProcessSchedulingDomain::generic
                        : ProcessSchedulingDomain::systemverilog,
                    member.process,
                    pc < member.origin.size() ? member.origin[pc] : pc, value);
                if (!op.nonblocking) {
                    host_object_written();
                }
                return;
            }
        }
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
            // A VHDL report statement (IEEE 1076-2019 10.9, 10.8), as the
            // reference evaluator emits it.
            if (member.vhdl_2019) {
                report_vhdl(member_index, pc, op.message.str(), op.severity,
                    op.source, true);
                return;
            }
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
        } else if constexpr (std::is_same_v<T, FileOpen>
            || std::is_same_v<T, FileClose>
            || std::is_same_v<T, FileWriteLiteral>
            || std::is_same_v<T, FileWriteFormatted>
            || std::is_same_v<T, FileWriteString>
            || std::is_same_v<T, FileReadLine>
            || std::is_same_v<T, FileEndOfFile>
            || std::is_same_v<T, FileErrorStatus>
            || std::is_same_v<T, FileScan>
            || std::is_same_v<T, FilePosition>
            || std::is_same_v<T, FileFlush>
            || std::is_same_v<T, FileBinaryRead>) {
            // File input and output (IEEE 1800-2023 21.3, 1076-2019 16.4):
            // the reference's operations on the thread's registers. The file
            // table, the standard streams and string objects are the host's;
            // a member that uses them runs on the leader, in its serial
            // place (the parallel plan treats the step as opaque).
            MemberFileStorage storage { *this, member_index, pc };
            try {
                impl_.run_file(storage, op);
            } catch (const InterpreterError&) {
                throw;
            } catch (const std::exception& error) {
                fail(member_index, pc, error.what());
            }
        } else if constexpr (std::is_same_v<T, MonitorInstall>) {
            // $monitor, $strobe and their file forms (IEEE 1800-2023 21.2.2,
            // 21.2.3, 21.3.2): the host's monitor watches and prints its
            // signals, which the planner keeps host signals.
            std::optional<FileHandle> file_handle;
            if (op.file_handle) {
                MemberFileStorage storage { *this, member_index, pc };
                file_handle = impl_.known_file_handle(storage, *op.file_handle);
            }
            impl_.install_monitor(member.process, op, file_handle);
        } else if constexpr (std::is_same_v<T, MonitorControl>) {
            // $monitoron and $monitoroff (21.2.3).
            impl_.set_monitor_enabled(op.enabled);
        } else if constexpr (std::is_same_v<T, RandomDistribution>) {
            // $dist_* (IEEE 1800-2023 20.14.2): the reference's step on the
            // member's registers; the seed is the caller's inout variable.
            RandomDistributionStep step;
            try {
                step = random_distribution_step(op, packed_register(op.seed),
                    packed_register(op.first),
                    op.second ? &packed_register(*op.second) : nullptr);
            } catch (const std::invalid_argument& error) {
                fail(member_index, pc, error.what());
            }
            if (step.warning && impl_.report_hook) {
                impl_.report_hook(member.process, *step.warning,
                    AssertionSeverity::warning, op.source, kernel_now(),
                    warp_time_ ? 0U : impl_.scheduler.delta());
            }
            const auto store = [&](const RegisterId id, const std::uint32_t value) {
                packed_register(id) = Impl::coerce_value_kind(
                    PackedLogic4::from_aval_bval(32U, value, 0U),
                    id < member.register_kinds.size() ? member.register_kinds[id]
                                                      : ValueKind::logic4);
            };
            store(op.destination, step.value);
            if (step.seed) {
                store(op.seed, *step.seed);
            }
        } else if constexpr (std::is_same_v<T, TimeFormatControl>) {
            // $timeformat (IEEE 1800-2023 20.4.2) sets the host's format in
            // the member's serial place (the parallel plan treats the step
            // as opaque).
            try {
                impl_.apply_time_format(packed_register(op.units),
                    packed_register(op.precision),
                    packed_register(op.minimum_width),
                    string_register(op.suffix));
            } catch (const std::invalid_argument& error) {
                fail(member_index, pc, error.what());
            }
        } else if constexpr (std::is_same_v<T, WriteAfter>
            || std::is_same_v<T, WriteAfterSlice>) {
            // A delayed update of a host signal (IEEE 1800-2023 10.3.3,
            // static_kernel_host_signal): the host schedules and matures it
            // for the member's driver, as for the reference process.
            std::optional<std::uint32_t> offset;
            if constexpr (std::is_same_v<T, WriteAfterSlice>) {
                offset = op.offset;
            }
            impl_.schedule_after_update(member.process, op.signal,
                packed_register(op.source), offset, op.domain, op.delay);
        } else if constexpr (std::is_same_v<T, WriteAfterDynamicSlice>) {
            // A write outside the declared range has no effect (IEEE
            // 1800-2017 11.5.1).
            std::optional<std::uint32_t> offset;
            try {
                offset = dynamic_index_offset(
                    packed_register(op.selection.index), op.selection);
            } catch (const std::invalid_argument& error) {
                if (op.selection.strict) {
                    fail(member_index, pc, error.what());
                }
            }
            if (offset) {
                impl_.schedule_after_update(member.process, op.signal,
                    packed_register(op.source), *offset, op.domain, op.delay);
            }
        } else if constexpr (std::is_same_v<T, WriteAfterDynamicPartSlice>) {
            std::optional<DynamicPartWrite> write;
            try {
                write = dynamic_part_write_value(packed_register(op.source),
                    packed_register(op.selection.base), op.selection);
            } catch (const std::invalid_argument& error) {
                fail(member_index, pc, error.what());
            }
            if (write) {
                impl_.schedule_after_update(member.process, op.signal,
                    std::move(write->value),
                    static_cast<std::uint32_t>(write->offset), op.domain,
                    op.delay);
            }
        } else if constexpr (std::is_same_v<T, WriteDelayed>) {
            // A gate or net delay (28.16): the transition's delay from the
            // operation's rise/fall/turn-off registers.
            const auto delays = Impl::write_delayed_delays(op,
                [&](const RegisterId id) -> const PackedLogic4& {
                    return packed_register(id);
                });
            impl_.schedule_write_delayed(member.process, op,
                packed_register(op.source), delays);
        } else if constexpr (std::is_same_v<T, WriteInertial>
            || std::is_same_v<T, WriteInertialSlice>) {
            std::optional<std::uint32_t> offset;
            if constexpr (std::is_same_v<T, WriteInertialSlice>) {
                offset = op.offset;
            }
            impl_.schedule_inertial(member.process, op.signal,
                packed_register(op.source), offset, op.delays, op.domain);
        } else if constexpr (std::is_same_v<T, EventTriggered>) {
            // .triggered (IEEE 1800-2023 15.5.3) of an event, a host signal:
            // the host stamps each trigger, the kernel's own included.
            packed_register(op.destination) = PackedLogic4(1,
                impl_.event_triggered_now(op.event) ? Logic4::one : Logic4::zero);
        } else if constexpr (std::is_same_v<T, SignalStrengthText>) {
            // %v (IEEE 1800-2023 21.2.1.5) of a host signal: the host holds
            // its drivers and their strengths.
            string_register(op.destination) = impl_.signal_strength_text(
                op.signal, op.offset, op.width);
        } else if constexpr (std::is_same_v<T, ReadStringObject>) {
            // A string variable (IEEE 1800-2023 6.16) is host storage with
            // no events: the member reads and writes it in its serial place.
            string_register(op.destination) = impl_.string_object_storage(op.object);
        } else if constexpr (std::is_same_v<T, WriteStringObject>) {
            // Blocking only (the planner hosts `s <= v`).
            if (impl_.package_static_initialization_active) {
                impl_.initialize_package_static_string_object(
                    op.object, string_register(op.source), member.process);
            } else {
                impl_.string_object_storage(op.object) = string_register(op.source);
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
        } else if constexpr (std::is_same_v<T, CompareStrings>) {
            // The reference evaluator's string equality (IEEE 1800-2023
            // 6.16; VHDL string `=` and `/=` as lowered, 1076-2019 9.2.3).
            bool equal { };
            try {
                equal = systemverilog_string_compare(string_register(op.lhs),
                            string_register(op.rhs))
                    == 0;
            } catch (const std::invalid_argument& error) {
                fail(member_index, pc, error.what());
            }
            packed_register(op.destination) = PackedLogic4 { 1,
                equal != op.not_equal ? Logic4::one : Logic4::zero };
        } else if constexpr (std::is_same_v<T, StringLength>) {
            std::size_t size { };
            try {
                size = systemverilog_string_length(string_register(op.source));
            } catch (const std::invalid_argument& error) {
                fail(member_index, pc, error.what());
            }
            packed_register(op.destination) = PackedLogic4::from_aval_bval(
                32, static_cast<std::uint32_t>(size), 0);
        } else if constexpr (std::is_same_v<T, StringIndex>
            || std::is_same_v<T, StringReplaceByte>) {
            constexpr bool read = std::is_same_v<T, StringIndex>;
            auto& text = [&]() -> std::string& {
                if constexpr (read) {
                    return string_register(op.source);
                } else {
                    return string_register(op.target);
                }
            }();
            std::size_t size { };
            try {
                size = systemverilog_string_length(text);
            } catch (const std::invalid_argument& error) {
                fail(member_index, pc, error.what());
            }
            // An unknown, negative or out-of-range index selects no
            // character: a read gives 0, a write changes nothing (6.16).
            const auto& index_value = packed_register(op.index);
            if (index_value.width() == 0U || index_value.width() > 64U) {
                fail(member_index, pc,
                    "string index must be a nonempty value of at most 64 bits");
            }
            const auto word = index_value.low_word();
            std::optional<std::size_t> index;
            if (word.bval == 0U
                && !(op.signed_index && word.width != 0U
                    && ((word.aval >> (word.width - 1U)) & 1U) != 0U)
                && word.aval < size) {
                index = static_cast<std::size_t>(word.aval);
            }
            if constexpr (read) {
                std::uint32_t byte { };
                try {
                    byte = index ? systemverilog_string_at(text, *index) : 0U;
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
                packed_register(op.destination)
                    = PackedLogic4::from_aval_bval(32, byte, 0);
            } else if (index) {
                const auto byte = packed_register(op.source).low_word();
                if (byte.bval != 0U) {
                    fail(member_index, pc, "string replacement byte contains X or Z");
                }
                try {
                    systemverilog_string_replace(text, *index,
                        static_cast<std::uint32_t>(byte.aval), maximum_string_bytes);
                } catch (const std::exception& error) {
                    fail(member_index, pc, error.what());
                }
            }
        } else if constexpr (std::is_same_v<T, VcdControl>) {
            // $dumpfile, $dumpvars and the other dump controls (IEEE
            // 1800-2023 21.7): the host's VCD observer, which records every
            // signal (the planner keeps them host signals, observe_all).
            if (!impl_.vcd_control_hook) {
                fail(member_index, pc, "VCD control service is unavailable");
            }
            VcdControlEvent event;
            event.kind = op.kind;
            if (op.filename) {
                event.filename = string_register(*op.filename);
            }
            if (op.value) {
                const auto converted = packed_register(*op.value).known_unsigned_value();
                if (!converted) {
                    fail(member_index, pc,
                        "VCD control value must be a known unsigned 64-bit integer");
                }
                event.value = *converted;
            }
            impl_.control_vcd(member.process, op, std::move(event));
        } else if constexpr (std::is_same_v<T, ClassStaticMethodCall>) {
            // A guarded assignment's driver disconnection (IEEE 1076-2019
            // 11.6, 10.5.2.2): the null transaction on the member's host
            // driver, as the reference schedules it. Any other is a static
            // method or DPI function call (IEEE 1800-2023 8.10, 35.5) for
            // the running process.
            bool disconnected = false;
            try {
                disconnected = impl_.try_vhdl_disconnect_call(member.process,
                    op.method_identity);
            } catch (const std::exception& error) {
                fail(member_index, pc, error.what());
            }
            if (!disconnected) {
                if (member.vhdl) {
                    fail(member_index, pc,
                        "static kernel static call is not a driver disconnection");
                }
                MemberContainerAccess access { *this, member_index, pc };
                if (const auto* error = impl_.run_class_operation(access, op,
                        running_process(member_index))) {
                    fail(member_index, pc, error);
                }
            }
        } else if constexpr (std::is_same_v<T, ClassAllocate>) {
            // The application's class services (IEEE 1800-2023 8.7) on the
            // member's registers, for the running process: a fork child's
            // own (18.14), else the member's.
            MemberContainerAccess access { *this, member_index, pc };
            if (const auto* error = impl_.run_class_operation(access, op,
                    running_process(member_index),
                    impl_.get_process(member.process).cold().design_process,
                    member_program(member_index).name())) {
                fail(member_index, pc, error);
            }
        } else if constexpr (std::is_same_v<T, ScopeRandomize>) {
            // std::randomize (18.12) on the member's registers.
            MemberRandomizeStorage storage { *this, member_index, pc };
            impl_.run_scope_randomize(storage, pc, op);
        } else if constexpr (std::is_same_v<T, ClassMethodCall>) {
            // A method call (8.6) for the running process.
            MemberContainerAccess access { *this, member_index, pc };
            if (const auto* error = impl_.run_class_operation(access, op,
                    running_process(member_index))) {
                fail(member_index, pc, error);
            }
        } else if constexpr (std::is_same_v<T, ClassPropertyRead>
            || std::is_same_v<T, ClassPropertyWrite>
            || std::is_same_v<T, ClassStaticPropertyRead>
            || std::is_same_v<T, ClassStaticPropertyWrite>
            || std::is_same_v<T, ClassPropertyContainerRead>
            || std::is_same_v<T, ClassPropertyContainerWrite>) {
            // Class property reads and writes (8.5, 8.9).
            MemberContainerAccess access { *this, member_index, pc };
            if (const auto* error = impl_.run_class_operation(access, op)) {
                fail(member_index, pc, error);
            }
        } else if constexpr (std::is_same_v<T, VhdlAssertApi>) {
            // The std.env assertion controls and counts (IEEE 1076-2019
            // 16.5.6), as the reference runs them.
            MemberVhdlApiStorage storage { *this, member_index, pc };
            impl_.run_vhdl_assert_api(storage, op);
        } else if constexpr (std::is_same_v<T, VhdlEnvironmentTime>) {
            // The std.env date and time functions (16.5.5).
            MemberVhdlApiStorage storage { *this, member_index, pc };
            impl_.run_vhdl_environment_time(storage, op);
        } else if constexpr (std::is_same_v<T, VhdlEnvironmentTimeToString>) {
            MemberVhdlApiStorage storage { *this, member_index, pc };
            impl_.run_vhdl_environment_time_string(storage, op);
        } else if constexpr (std::is_same_v<T, VhdlEnvironmentGetenv>) {
            // GETENV (16.5.6).
            MemberVhdlApiStorage storage { *this, member_index, pc };
            impl_.run_vhdl_environment_getenv(storage, op);
        } else if constexpr (std::is_same_v<T, VhdlEnvironmentDirectory>) {
            // The directory functions (16.5.6).
            MemberVhdlApiStorage storage { *this, member_index, pc };
            impl_.run_vhdl_environment_directory(storage, op);
        } else if constexpr (std::is_same_v<T, VhdlEnvironmentGetCallPath>) {
            // GET_CALL_PATH and TO_STRING of a call path (16.5.6).
            MemberVhdlApiStorage storage { *this, member_index, pc };
            impl_.run_vhdl_environment_get_call_path(storage, op);
        } else if constexpr (std::is_same_v<T, VhdlEnvironmentCallPath>) {
            MemberVhdlApiStorage storage { *this, member_index, pc };
            impl_.run_vhdl_environment_call_path(storage, op);
        } else if constexpr (std::is_same_v<T, VhdlReflectionApi>) {
            // The reflection API's mirrors (16.6), the host's table.
            MemberVhdlApiStorage storage { *this, member_index, pc };
            impl_.run_vhdl_reflection_api(storage, op);
        } else if constexpr (std::is_same_v<T, StringReport>) {
            // A VHDL report or assertion with a computed message (IEEE
            // 1076-2019 10.8, 10.9), as the reference emits it.
            const auto& encoded = packed_register(op.severity);
            if (encoded.width() != 2U || encoded.get(0) == Logic4::x
                || encoded.get(0) == Logic4::z || encoded.get(1) == Logic4::x
                || encoded.get(1) == Logic4::z) {
                fail(member_index, pc,
                    "VHDL severity expression produced an invalid value");
            }
            const auto severity = static_cast<AssertionSeverity>(
                (encoded.get(0) == Logic4::one ? 1U : 0U)
                | (encoded.get(1) == Logic4::one ? 2U : 0U));
            const auto& message = string_register(op.message);
            if (member.vhdl_2019) {
                report_vhdl(member_index, pc, message, severity, op.source,
                    op.standalone);
                return;
            }
            const auto delta = warp_time_ ? 0U : impl_.scheduler.delta();
            if (severity == AssertionSeverity::failure) {
                if (op.standalone && impl_.report_hook) {
                    impl_.report_hook(member.process, message, severity, op.source,
                        kernel_now(), delta);
                }
                throw AssertionError(member.process,
                    pc < member.origin.size() ? member.origin[pc] : pc,
                    message.empty()
                        ? std::string { op.standalone ? "report failure"
                                                      : "assertion failed" }
                        : message,
                    severity, op.source, op.standalone);
            }
            if (impl_.report_hook) {
                impl_.report_hook(member.process, message, severity, op.source,
                    kernel_now(), delta);
            }
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
                if (member.dynamic_container(id)) {
                    const auto& value = member.dynamic_containers[id];
                    frame.bytes += sizeof(ContainerValue)
                        + container_value_storage_bytes(*value);
                    frame.dynamic.push_back(value);
                } else {
                    frame.dynamic.push_back(nullptr);
                }
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
                context->container_ids.assign(
                    op.containers.begin(), op.containers.end());
                threads_[thread].contexts.push_back(std::move(context));
            }
        } else if constexpr (std::is_same_v<T, CallableFramePop>) {
            const auto context_thread = member.thread_contexts
                    && running_thread_ != no_slot
                    && threads_[running_thread_].member == member_index
                ? running_thread_
                : no_slot;
            if (context_thread != no_slot) {
                // The activation ends for this thread; fork children still
                // inside it keep its final values (6.21), taken before the
                // frame restores the caller's.
                auto& contexts = threads_[context_thread].contexts;
                if (contexts.empty() || contexts.back()->identity != op.identity) {
                    fail(member_index, pc, "automatic callable context stack mismatch");
                }
                if (contexts.back()->escaping) {
                    store_context(member, *contexts.back());
                }
                contexts.pop_back();
            }
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
            std::vector<PackedLogic4> packed;
            for (const auto id : op.preserve_packed) {
                packed.push_back(packed_register(id));
            }
            std::vector<std::string> strings;
            for (const auto id : op.preserve_strings) {
                strings.push_back(string_register(id));
            }
            std::vector<std::uint32_t> containers;
            std::vector<SharedContainerValue> dynamic;
            for (const auto id : op.preserve_containers) {
                containers.push_back(container_register(id));
                dynamic.push_back(member.dynamic_container(id)
                        ? member.dynamic_containers[id] : nullptr);
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
                if (entry < frame.dynamic.size() && frame.dynamic[entry]) {
                    member.dynamic_containers[frame.container_ids[entry]]
                        = std::move(frame.dynamic[entry]);
                }
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
                if (dynamic[entry]) {
                    member.dynamic_containers[op.preserve_containers[entry]]
                        = std::move(dynamic[entry]);
                }
            }
        } else if constexpr (std::is_same_v<T, LoadMemory>) {
            // $readmemb/$readmemh (IEEE 1800-2023 21.4) into a kernel array:
            // a private one, or the design memory a ReadContainerObject
            // binding aliases. The file loads into the current contents,
            // written back element by element as element writes would be.
            // An I/O error leaves the array as it was (the reference loads
            // a copy and never writes it back). $writememb/$writememh (21.5)
            // write the array's contents. A reference-format register
            // (an associative array, say) is the reference's own value.
            const bool dynamic = member.dynamic_container(op.target);
            const auto index = dynamic ? no_container : container_register(op.target);
            if (!dynamic && index == no_container) {
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
            const auto start = bound(op.start,
                op.write ? "write-memory start" : "read-memory start");
            const auto finish = bound(op.finish,
                op.write ? "write-memory finish" : "read-memory finish");
            const auto write_file = [&](const ContainerValue& source) {
                try {
                    impl_.write_memory_file(member.process, string_register(op.path),
                        source, op.hexadecimal, start, finish);
                } catch (const InterpreterError&) {
                    throw;
                } catch (const std::exception& error) {
                    fail(member_index, pc, error.what());
                }
            };
            if (dynamic) {
                MemberContainerAccess access { *this, member_index, pc };
                if (op.write) {
                    write_file(access.read_container(op.target));
                } else {
                    impl_.read_memory_file(member.process, pc, string_register(op.path),
                        access.container(op.target), op.hexadecimal, start, finish);
                }
                return;
            }
            auto& container = containers_[index];
            ContainerValue value;
            value.type = container.type;
            value.elements.reserve(container.count);
            for (std::uint32_t ordinal = 0U; ordinal < container.count; ++ordinal) {
                value.elements.push_back(container_element(container, ordinal));
            }
            if (op.write) {
                write_file(value);
                return;
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
            // is nothing to copy. A private array of the memory's type (a
            // task's output array, IEEE 1800-2023 13.5) is written element by
            // element, as element writes would be.
            if (op.nonblocking || op.transaction_signal
                || op.object >= container_of_object_.size()
                || container_of_object_[op.object] == no_container) {
                fail(member_index, pc,
                    "static kernel container write is not a write-back of its binding");
            }
            const auto source = container_register(op.source);
            auto& target = containers_[container_of_object_[op.object]];
            if (source != container_of_object_[op.object]) {
                if (source == no_container
                    || containers_[source].object != no_container_object
                    || !(containers_[source].type == target.type)
                    || containers_[source].count != target.count) {
                    fail(member_index, pc,
                        "static kernel container write needs a private array of its type");
                }
                const auto& from = containers_[source];
                for (std::uint32_t ordinal = 0U; ordinal < target.count; ++ordinal) {
                    auto value = container_element(from, ordinal);
                    if (!(value == container_element(target, ordinal))) {
                        write_container_element(target, ordinal, std::move(value),
                            member_index, pc);
                    }
                }
            }
        } else if constexpr (std::is_same_v<T, ReadSignal>) {
            // A sampled value (IEEE 1800-2023 16.5.1, 16.9.3) from the
            // host's sampled state; its signals are host signals.
            if (op.kind == SignalReadKind::current && op.ticks == 1U
                && !op.clock && !op.gate) {
                handled = false;
                return;
            }
            MemberAssertionStorage storage { *this, member_index, pc };
            impl_.run_sampled_read(storage, op);
        } else if constexpr (std::is_same_v<T, SvaOperation>) {
            // A concurrent assertion's monitor (16.14), the reference's.
            MemberAssertionStorage storage { *this, member_index, pc };
            impl_.run_sva(storage, op);
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
                        vhdl_timers_.push({ now + delay, threads_[index].order,
                            timer_sequence_++, index, threads_[index].epoch });
                    }
                }
            } else if (delay == 0U) {
                // #0 resumes in the Inactive region of this time step.
                inactive_threads_.push_back(index);
            } else {
                const auto now = kernel_now();
                // A wait beyond the last representable time never resumes.
                if (delay <= std::numeric_limits<SimulationTick>::max() - now) {
                    timers_.push({ now + delay, threads_[index].order,
                        timer_sequence_++, index, threads_[index].epoch });
                }
            }
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, WaitRegion>) {
            // A later region of this time step (IEEE 1800-2023 4.4.2, 16.4),
            // or the region the thread runs in, where it continues after
            // the processes already queued there.
            suspend_point = true;
            const auto current = impl_.scheduler.current_phase();
            if (!current || op.phase < *current) {
                fail(member_index, pc,
                    "WaitRegion must target a later scheduler region");
            }
            ready_thread_in(index, op.phase);
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
                        vhdl_timers_.push({ *thread.deadline, thread.order,
                            timer_sequence_++, index, thread.epoch });
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
        } else if constexpr (std::is_same_v<T, WaitFork>) {
            // wait fork (IEEE 1800-2023 9.6.1): until the thread's live
            // children end; without any, the reference requeues the process
            // in the current region (queue_current), which the next round is.
            suspend_point = true;
            if (only_detached_children(index)) {
                ready_thread(index);
            } else {
                threads_[index].waiting_children = true;
            }
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, DisableFork>) {
            // disable fork (9.6.3), as the reference's DisableFork: every
            // descendant, the process then requeued in the current region;
            // or the children of one named fork, the process continuing.
            if (!op.site) {
                suspend_point = true;
                // A pending clocking drive is not a subprocess: with one
                // live, only the other children end, with their descendants.
                const auto children = live_children(index);
                if (std::ranges::none_of(children, [&](const std::uint32_t child) {
                        return threads_[child].detached;
                    })) {
                    cancel_thread_descendants(index);
                } else {
                    for (const auto child : children) {
                        if (!threads_[child].detached && threads_[child].alive) {
                            cancel_thread_descendants(child);
                            kill_thread(child);
                        }
                    }
                }
                ready_thread(index);
                outcome = Boundary::suspend;
            } else {
                disable_fork_site(index, static_cast<std::uint32_t>(*op.site));
                outcome = threads_[index].alive ? Boundary::advance
                                                : Boundary::killed;
            }
        } else if constexpr (std::is_same_v<T, Halt>) {
            suspend_point = true;
            outcome = Boundary::finish;
            if (op.program_exit) {
                // $exit (IEEE 1800-2023 24.7) ends the program's threads,
                // this one among them.
                const auto& owner = members_[member_index].program_owner;
                if (!owner) {
                    fail(member_index, pc,
                        "$exit requires a SystemVerilog program owner");
                }
                exit_program(*owner);
                if (!threads_[index].alive) {
                    outcome = Boundary::killed;
                }
            }
        } else if constexpr (std::is_same_v<T, ForkEnd>) {
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
            // The process halts here, as the reference's does at $finish.
            suspend_point = true;
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
            // A $stop in a final procedure leaves the simulation paused
            // although it has finished (the reference's paused_in_final).
            impl_.paused_in_final = impl_.paused_in_final
                || members_[member_index].final_block;
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, DisableBlock>) {
            outcome = op.scopes.empty() ? disable_block(index, pc, op)
                                        : disable_named_scope(index, pc, op.scopes);
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
                const auto child
                    = spawn_thread(member_index, branch, index, group, pc);
                threads_[child].detached = op.detached;
                ready_thread(child);
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
    if (impl_.scheduler.stop_requested() && !impl_.stopped_by_design) {
        // The thread is already registered as waiting: only the run stops.
        // ($finish stops the run itself.)
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
    if (width == 0U) {
        // A null array (IEEE 1076-2019 5.3.2.2) has no elements to leave
        // uninitialized; the reference shows it empty.
        return PackedLogic4 { };
    }
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
    if (found != members_.end() && found->dynamic_container(reg)) {
        return *found->dynamic_containers[reg];
    }
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
    // A timer whose thread was killed or moved on (disable, disable fork)
    // still holds its time step, as the reference's scheduled resumption of
    // the process does: simulation time reaches it (collect_timers drops it).
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
    // Lapsed timers included (arm_timer): the time steps they hold are the
    // reference's too.
    if (timers_.empty()) {
        return std::nullopt;
    }
    return timers_.top().time;
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

// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 behavioral tier (docs/simulation-engine-v4-progress.md §4b):
// testbench processes run inside the static kernel as resumable threads. A
// thread runs until it suspends on a delay, a dynamic or static wait, a fork
// join, a halt or $finish; waits on kernel-owned slots and on host inputs, and
// delays on a kernel timer queue, make it ready again.
#include "simir_execution_shared.hpp"
#include "simir_static_kernel_state.hpp"

#include "fsim/runtime/output_format.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;

std::uint32_t Interpreter::Impl::StaticKernel::spawn_thread(
    const std::uint32_t member, const std::uint32_t pc,
    const std::uint32_t parent, const std::uint32_t group)
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
    thread.epoch = epoch;
    thread.alive = true;
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
    if (committing_round_) {
        // Woken by a VHDL commit: it runs in the next delta.
        deferred_threads_.push_back(index);
        return;
    }
    ready_threads_.push_back(index);
}

void Interpreter::Impl::StaticKernel::start_behavioral()
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
    // Every behavioral process starts at time zero in the Active region,
    // like its reference process.
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        if (members_[index].kind == StaticKernelMemberKind::behavioral) {
            ready_thread(spawn_thread(index, 0U, no_slot, no_slot));
        }
    }
}

bool Interpreter::Impl::StaticKernel::run_ready_threads()
{
    if (ready_threads_.empty()) {
        return false;
    }
    auto ready = std::move(ready_threads_);
    ready_threads_.clear();
    for (const auto index : ready) {
        threads_[index].queued = false;
        if (stopped_) {
            break;
        }
        if (threads_[index].alive) {
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
        const Waiter waiter { index, epoch, entry.edge };
        if (const auto slot = slot_of_signal_[entry.signal]; slot != no_slot) {
            slot_waiters_[slot].push_back(waiter);
        } else {
            input_waiters_[input_of_signal_[entry.signal]].push_back(waiter);
        }
    }
}

void Interpreter::Impl::StaticKernel::wake_waiters(std::vector<Waiter>& waiters,
    const Logic4 before, const Logic4 after)
{
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < waiters.size(); ++index) {
        const auto waiter = waiters[index];
        const auto& thread = threads_[waiter.thread];
        if (!thread.alive || thread.queued || thread.epoch != waiter.epoch) {
            continue;
        }
        if (waiter.edge == EdgeKind::any
            || edge_matches(waiter.edge, before, after)) {
            ready_thread(waiter.thread);
            continue;
        }
        waiters[kept++] = waiter;
    }
    waiters.resize(kept);
}

void Interpreter::Impl::StaticKernel::finish_thread(const std::uint32_t index)
{
    auto& thread = threads_[index];
    thread.alive = false;
    thread.call_stack.clear();
    thread.frames.clear();
    thread.saved_arrays.clear();
    thread.saved_array_marks.clear();
    const auto parent = thread.parent;
    const auto group = thread.group;
    free_threads_.push_back(index);
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
    if (impl_.output_hook) {
        // A warped time step is the first delta of its time slot.
        impl_.output_hook(members_[member].process, text, newline,
            kernel_now(), warp_time_ ? 0U : impl_.scheduler.delta());
    }
}

bool Interpreter::Impl::StaticKernel::behavioral_step(
    const std::uint32_t member_index, const std::uint32_t pc)
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
    visit_operation([&](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, Display>) {
            behavioral_output(member_index, op.text, op.newline);
        } else if constexpr (std::is_same_v<T, FormatDisplay>) {
            const auto text = make_formatted_output(op.prefix, op.suffix,
                op.format, packed_register(op.source), op.signed_decimal,
                op.suppress_leading_zero, op.minimum_width, op.left_justify,
                op.zero_pad, op.scalar_kind);
            behavioral_output(member_index, text, op.newline);
        } else if constexpr (std::is_same_v<T, StringDisplay>) {
            const auto text = op.prefix + string_register(op.source) + op.suffix;
            behavioral_output(member_index, text, op.newline);
        } else if constexpr (std::is_same_v<T, TimeDisplay>) {
            const auto text = make_time_output(op.prefix, op.suffix,
                kernel_now(), impl_.time_format,
                op.use_timeformat_width, op.minimum_width, op.left_justify,
                op.zero_pad);
            behavioral_output(member_index, text, op.newline);
        } else if constexpr (std::is_same_v<T, LoadStringConstant>) {
            if (op.value.size() > maximum_string_bytes) {
                fail(member_index, pc, "string literal exceeds 4096-byte limit");
            }
            try {
                (void)systemverilog_string_length(op.value);
            } catch (const std::invalid_argument& error) {
                fail(member_index, pc, error.what());
            }
            string_register(op.destination) = op.value;
        } else if constexpr (std::is_same_v<T, CopyStringRegister>) {
            string_register(op.destination) = string_register(op.source);
        } else if constexpr (std::is_same_v<T, PlusArgSelect>) {
            const auto query = string_register(op.query);
            const std::string* selected = nullptr;
            for (const auto& argument : impl_.plusargs) {
                auto candidate = std::string_view { argument };
                if (candidate.starts_with('+')) {
                    candidate.remove_prefix(1);
                }
                if (candidate.starts_with(query)) {
                    selected = &argument;
                    break;
                }
            }
            if (op.selected) {
                auto& destination = string_register(*op.selected);
                destination.clear();
                if (selected != nullptr) {
                    auto candidate = std::string_view { *selected };
                    if (candidate.starts_with('+')) {
                        candidate.remove_prefix(1);
                    }
                    destination.assign(candidate);
                }
            }
            packed_register(op.destination) = PackedLogic4::from_aval_bval(
                32, selected != nullptr ? 1U : 0U, 0);
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
            for (std::size_t entry = 0U; entry < op.preserve_packed.size(); ++entry) {
                packed_register(op.preserve_packed[entry]) = std::move(packed[entry]);
            }
            for (std::size_t entry = 0U; entry < op.preserve_strings.size(); ++entry) {
                string_register(op.preserve_strings[entry]) = std::move(strings[entry]);
            }
            for (std::size_t entry = 0U; entry < op.preserve_containers.size(); ++entry) {
                container_register(op.preserve_containers[entry]) = containers[entry];
            }
        } else {
            handled = false;
        }
    }, member.operations[pc]);
    return handled;
}

Interpreter::Impl::StaticKernel::Boundary
Interpreter::Impl::StaticKernel::behavioral_boundary(const std::uint32_t index,
    const std::uint32_t pc)
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
    visit_operation([&](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, WaitFor>) {
            auto delay = op.delay;
            if (op.source) {
                try {
                    delay = normalized_dynamic_wait_delay(op, read(*op.source));
                } catch (const std::exception& error) {
                    fail(member_index, pc, error.what());
                }
            }
            if (delay == 0U) {
                // #0 resumes in the Inactive region of this time step.
                inactive_threads_.push_back(index);
            } else {
                const auto now = kernel_now();
                if (delay > std::numeric_limits<SimulationTick>::max() - now) {
                    fail(member_index, pc, "simulation time overflow in WaitFor");
                }
                timers_.push({ now + delay, timer_sequence_++, index,
                    threads_[index].epoch });
            }
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, WaitOn>) {
            std::vector<Sensitivity> entries;
            entries.reserve(op.signals.size());
            for (std::size_t entry = 0U; entry < op.signals.size(); ++entry) {
                entries.push_back({ op.signals[entry],
                    op.edges.empty() ? EdgeKind::any : op.edges[entry] });
            }
            wait_on(index, entries);
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, WaitSensitivity>) {
            wait_on(index, member.wait_sensitivity);
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, WaitForever>) {
            outcome = Boundary::suspend;
        } else if constexpr (std::is_same_v<T, Halt>
            || std::is_same_v<T, ForkEnd>) {
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
                ready_thread(spawn_thread(member_index, branch, index, group));
            }
            outcome = op.join == ForkJoinKind::none ? Boundary::advance
                                                    : Boundary::suspend;
        }
    }, member.operations[pc]);
    return outcome;
}

void Interpreter::Impl::StaticKernel::run_thread(const std::uint32_t index)
{
    const auto member_index = threads_[index].member;
    auto& member = members_[member_index];
    // The member's call stack and automatic frames belong to the running
    // thread; registers and strings are shared by all of its threads.
    std::swap(member.call_stack, threads_[index].call_stack);
    std::swap(member.frames, threads_[index].frames);
    std::swap(member.frame_bytes, threads_[index].frame_bytes);
    std::swap(member.saved_arrays, threads_[index].saved_arrays);
    std::swap(member.saved_array_marks, threads_[index].saved_array_marks);
    auto pc = threads_[index].pc;
    const auto size = static_cast<std::uint32_t>(member.operations.size());
    const auto park = [&](const std::uint32_t resume) {
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
            outcome = behavioral_boundary(index, pc);
        } else {
            outcome = behavioral_boundary(index, pc);
            if (outcome == Boundary::none) {
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
        pending_deopt_ = deopt;
    }
    if (pending_deopt_) {
        const auto deopt = *pending_deopt_;
        pending_deopt_.reset();
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
    if (timers_.empty() || stopped_) {
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
    // The kernel runs as its host process; due timers are collected when it
    // activates.
    impl_.queue_active_current(host_process_);
}

} // namespace fsim::runtime::simir

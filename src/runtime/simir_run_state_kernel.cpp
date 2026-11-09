// SPDX-License-Identifier: Apache-2.0
//
// Run-state save and restart of the static kernel (#128; IEEE 1800-2023 D.9,
// IEEE 1076-2019 20.7-20.9; S5). Two sections:
//
//   kernel.values  the slot arena and the published outputs (the existing
//                  value section, restored in place because compiled code
//                  holds the arrays' addresses)
//   kernel.state   per member: registers, call stack, frames, deopt state;
//                  thread slots with their callable contexts and fork
//                  bookkeeping; waiters; delay timers, VHDL timers and hosted
//                  timers; pending delayed net writes; process records; the
//                  dissolved partitions; input and slot dynamic fields
//
// What the kernel derives is not saved: the structure of members, slots,
// inputs, partitions and the compiled code come from the design (the same
// artifact), and the doorbell events that wake the kernel (kernel.timer,
// kernel.vhdl_timer, kernel.hosted_timer, kernel.vhdl_round, kernel.late_start,
// kernel.hosted_region) are re-created by rearm_run_state() from the saved
// timers and flags. A partition a foreign write dissolved is dissolved again
// through dissolve_member before its saved state is put back, because the
// dissolution gates and recompiles the member's code.
//
// The save has no kernel-side cost: nothing here is called from a kernel step.
#include "simir_run_state.hpp"
#include "simir_static_kernel_state.hpp"

#include <algorithm>
#include <queue>

namespace fsim::runtime::simir {

namespace {

using Kernel = InterpreterEventAccess::Impl::StaticKernel;
using run_state::SharedTable;

/// A priority queue as its elements in pop order, so equal states give equal
/// bytes whatever the heap layout.
template <typename Queue>
void queue_io(RunStateArchive& ar, Queue& queue)
{
    using Element = typename Queue::value_type;
    std::vector<Element> items;
    if (ar.saving()) {
        Queue copy = queue;
        while (!copy.empty()) {
            items.push_back(copy.top());
            copy.pop();
        }
    }
    ar.io(items);
    if (ar.loading() && ar.ok()) {
        queue = Queue { };
        for (auto& item : items) {
            queue.push(std::move(item));
        }
    }
}

}  // namespace

void Kernel::audit_run_state(std::vector<std::string>& messages) const
{
    const auto busy = [&](const bool condition, const char* what) {
        if (condition) {
            messages.push_back(std::string { "FSIM-SAVE-0011: the kernel's " }
                + what + " is not empty at the save point");
        }
    };
    if (time_warp_) {
        messages.push_back("FSIM-SAVE-0013: the static kernel warps time "
                           "(FSIM_STATIC_KERNEL_TIME_WARP) and cannot be saved");
    }
    FSIM_RS_IDLE("kernel.impl", time_warp_);
    FSIM_RS_IDLE("kernel.impl", nba_);
    busy(!nba_.empty(), "nonblocking update queue");
    FSIM_RS_IDLE("kernel.impl", pending_generic_);
    busy(!pending_generic_.empty(), "generic update queue");
    FSIM_RS_IDLE("kernel.impl", output_queue_);
    busy(!output_queue_.empty(), "output queue");
    FSIM_RS_IDLE("kernel.impl", trigger_queue_);
    busy(!trigger_queue_.empty(), "trigger queue");
    FSIM_RS_IDLE("kernel.impl", triggered_);
    busy(!triggered_.empty(), "triggered list");
    // written_inputs_ is the plan's scan list (every input some process
    // writes), built at construction and extended by note_external_write; it
    // is never empty in a design with a written input, so it is not pending
    // work. The same holds for the capacity buffers.
    FSIM_RS_IDLE("kernel.impl", conversion_queue_);
    busy(!conversion_queue_.empty(), "conversion queue");
    // The FSIM_KERNEL_SVA_CHECK reference monitors live within one generated
    // tick.
    FSIM_RS_IDLE("kernel.impl", sva_check_refs_);
    busy(!sva_check_refs_.empty(), "assertion check reference");
    FSIM_RS_IDLE("kernel.impl", pending_dissolves_);
    busy(!pending_dissolves_.empty(), "pending dissolve list");
    FSIM_RS_IDLE("kernel.impl", yield_wakes_);
    busy(!yield_wakes_.empty(), "yield list");
    FSIM_RS_IDLE("kernel.impl", hosted_active_);
    busy(!hosted_active_.empty(), "hosted Active queue");
    FSIM_RS_IDLE("kernel.impl", hosted_inactive_);
    busy(!hosted_inactive_.empty(), "hosted Inactive queue");
    FSIM_RS_IDLE("kernel.impl", round_);
    FSIM_RS_IDLE("kernel.impl", next_round_);
    busy(!round_.empty() || !next_round_.empty(), "round list");
    FSIM_RS_IDLE("kernel.impl", deferred_targets_);
    busy(!deferred_targets_.empty(), "deferred schedule list");
    FSIM_RS_IDLE("kernel.impl", hosted_regions_);
    for (const auto& region : hosted_regions_) {
        busy(!region.empty(), "hosted region queue");
    }
    // host_active_staged_ is a hint cleared at the start of every activation
    // and host_nba_staged_ is read only with host_nba_time_ == now; a save
    // point is a slot end with no pending update (the save waits for the
    // queues above), so a stale flag is dead after a restore. The pending
    // updates themselves are the queues checked above and in the scheduler.
    FSIM_RS_IDLE("kernel.impl", in_activation_);
    FSIM_RS_IDLE("kernel.impl", activating_);
    FSIM_RS_IDLE("kernel.impl", running_conversions_);
    FSIM_RS_IDLE("kernel.impl", committing_round_);
    FSIM_RS_IDLE("kernel.impl", sv_draining_);
    busy(in_activation_ || activating_ || running_conversions_
            || committing_round_ || sv_draining_,
        "activation in progress");

    // The step scratch (#185): each member below is emptied or cleared when
    // the step that fills it ends, so a save at a slot or cycle end finds it
    // at rest. A member that is not names pending work a restart would drop.
    const auto marked = [](const auto& marks) {
        return std::ranges::any_of(marks, [](const auto mark) { return mark != 0; });
    };
    FSIM_RS_IDLE("kernel.impl", conversion_queued_);
    busy(marked(conversion_queued_), "conversion marks");
    FSIM_RS_IDLE("kernel.impl", partition_queued_);
    busy(marked(partition_queued_), "partition queue marks");
    FSIM_RS_IDLE("kernel.impl", round_bits_);
    busy(marked(round_bits_), "round sort bitmap");
    FSIM_RS_IDLE("kernel.impl", member_schedule_);
    busy(std::ranges::any_of(member_schedule_,
             [](const MemberSchedule& state) {
                 return state.queued || state.triggered || state.round
                     || state.deferred || state.collapse_queued;
             }),
        "member schedule flags");
    FSIM_RS_IDLE("kernel.impl", sv_woken_round_);
    FSIM_RS_IDLE("kernel.impl", sv_woken_threads_);
    FSIM_RS_IDLE("kernel.impl", sv_woken_writes_);
    busy(!sv_woken_round_.empty() || !sv_woken_threads_.empty()
            || sv_woken_writes_,
        "SystemVerilog-woken VHDL work");
    FSIM_RS_IDLE("kernel.impl", collapsing_);
    FSIM_RS_IDLE("kernel.impl", collapse_buckets_);
    FSIM_RS_IDLE("kernel.impl", collapse_batch_);
    FSIM_RS_IDLE("kernel.impl", collapse_pending_);
    busy(collapsing_ || collapse_pending_ != 0U || !collapse_batch_.empty()
            || std::ranges::any_of(collapse_buckets_,
                [](const auto& bucket) { return !bucket.empty(); }),
        "delta-collapse queues");
    FSIM_RS_IDLE("kernel.impl", deferred_threads_);
    busy(!deferred_threads_.empty(), "deferred thread list");
    FSIM_RS_IDLE("kernel.impl", held_round_);
    FSIM_RS_IDLE("kernel.impl", held_threads_);
    busy(!held_round_.empty() || !held_threads_.empty(), "held wake lists");
    FSIM_RS_IDLE("kernel.impl", parked_writes_);
    FSIM_RS_IDLE("kernel.impl", writes_);
    FSIM_RS_IDLE("kernel.impl", parked_pending_);
    busy(writes_.count != 0U || parked_writes_.count != 0U
            || !parked_pending_.empty(),
        "parked write queues");
    FSIM_RS_IDLE("kernel.impl", worker_lane_);
    busy(worker_lane_ != nullptr, "worker lane");
    FSIM_RS_IDLE("kernel.impl", pending_watches_);
    FSIM_RS_IDLE("kernel.impl", pending_revivals_);
    FSIM_RS_IDLE("kernel.impl", boundary_requested_);
    busy(!pending_watches_.empty() || !pending_revivals_.empty()
            || boundary_requested_,
        "foreign watch requests");
    FSIM_RS_IDLE("kernel.impl", debug_stopped_);
    busy(debug_stopped_, "debugger stop");
    FSIM_RS_IDLE("kernel.impl", running_thread_);
    busy(running_thread_ != static_kernel_detail::no_slot, "running thread");
    FSIM_RS_IDLE("kernel.impl", touched_);
    FSIM_RS_IDLE("kernel.impl", round_before_);
    busy(!touched_.empty() || !round_before_.empty(), "round commit lists");
    FSIM_RS_IDLE("kernel.impl", expanded_groups_);
    busy(!expanded_groups_.empty(), "expanded dispatch groups");
    FSIM_RS_IDLE("kernel.impl", buckets_);
    FSIM_RS_IDLE("kernel.impl", queued_count_);
    busy(queued_count_ != 0U
            || std::ranges::any_of(buckets_,
                [](const LevelQueue& bucket) { return bucket.count != 0U; }),
        "level queues");
    FSIM_RS_IDLE("kernel.impl", private_writes_);
    FSIM_RS_IDLE("kernel.impl", private_domains_);
    FSIM_RS_IDLE("kernel.impl", private_more_);
    busy(private_writes_ != 0U || private_domains_ != 0U || private_more_,
        "private domain writes");
    FSIM_RS_IDLE("kernel.impl", quiet_commit_);
    FSIM_RS_IDLE("kernel.impl", fused_running_);
    FSIM_RS_IDLE("kernel.impl", replaying_);
    FSIM_RS_IDLE("kernel.impl", replay_external_);
    FSIM_RS_IDLE("kernel.impl", reconstructing_);
    busy(quiet_commit_ || fused_running_ || replaying_ || replay_external_
            || reconstructing_,
        "commit or replay in progress");
    // The host-region handshake: each flag is the other side of a scheduler
    // event that must have run (the kernel.nonblocking and kernel.hosted_*
    // events are derived, so a save with one pending would lose it).
    FSIM_RS_IDLE("kernel.impl", host_nba_wakes_);
    FSIM_RS_IDLE("kernel.impl", nonblocking_after_host_);
    FSIM_RS_IDLE("kernel.impl", nonblocking_deferred_);
    FSIM_RS_IDLE("kernel.impl", nonblocking_region_);
    FSIM_RS_IDLE("kernel.impl", in_host_nonblocking_region_);
    busy(host_nba_wakes_ || nonblocking_after_host_ || nonblocking_deferred_
            || nonblocking_region_ || in_host_nonblocking_region_,
        "host nonblocking region");
    FSIM_RS_IDLE("kernel.impl", hosted_inactive_task_);
    FSIM_RS_IDLE("kernel.impl", hosted_inactive_region_);
    FSIM_RS_IDLE("kernel.impl", hosted_inactive_deferred_);
    FSIM_RS_IDLE("kernel.impl", hosted_running_inactive_);
    FSIM_RS_IDLE("kernel.impl", hosted_running_region_);
    busy(hosted_inactive_task_ || hosted_inactive_region_
            || hosted_inactive_deferred_ || hosted_running_inactive_
            || hosted_running_region_.has_value(),
        "hosted Inactive region");
    FSIM_RS_IDLE("kernel.impl", host_active_yield_);
    FSIM_RS_IDLE("kernel.impl", host_batch_resume_);
    FSIM_RS_IDLE("kernel.impl", batch_resume_queued_);
    busy(host_active_yield_ || host_batch_resume_.has_value()
            || batch_resume_queued_,
        "hosted batch resume");
    for (const auto& slot : slots_) {
        FSIM_RS_IDLE("kernel.slot", trigger_pending);
        FSIM_RS_IDLE("kernel.slot", output_pending);
        busy(slot.trigger_pending || slot.output_pending, "slot output marks");
    }
}

void Kernel::run_state_io(RunStateArchive& ar)
{
    using run_state::kernel_version;
    // The callable contexts that threads share (a fork's escaping activation,
    // IEEE 1800-2023 6.21, 9.3.2): numbered once, named by index.
    SharedTable<CallableContext> contexts;
    if (ar.saving()) {
        for (auto& thread : threads_) {
            for (const auto& context : thread.contexts) {
                static_cast<void>(contexts.index_of(context));
            }
            static_cast<void>(contexts.index_of(thread.isolated));
        }
    }
    const auto context_count = ar.count(contexts.objects().size());
    if (ar.loading()) {
        for (std::uint64_t index = 0U; index < context_count && ar.ok(); ++index) {
            contexts.objects().push_back(std::make_shared<CallableContext>());
        }
    }
    for (auto& context : contexts.objects()) {
        ar.io(*context);
    }

    // Dissolved partitions are read first so that they can be dissolved again
    // before anything else of the kernel is put back.
    FSIM_RS_CUSTOM("kernel.impl", dissolved_);
    std::vector<std::unique_ptr<DissolvedPartition>> dissolved;
    if (ar.saving()) {
        dissolved.resize(dissolved_.size());
        for (std::size_t index = 0U; index < dissolved_.size(); ++index) {
            if (dissolved_[index]) {
                dissolved[index] = std::make_unique<DissolvedPartition>(*dissolved_[index]);
            }
        }
    }
    const auto dissolved_count = ar.count(dissolved.size());
    if (ar.loading()) {
        dissolved.resize(static_cast<std::size_t>(dissolved_count));
    }
    for (auto& entry : dissolved) {
        std::uint8_t present = entry ? 1U : 0U;
        ar.io(present);
        if (ar.loading() && present != 0U) {
            entry = std::make_unique<DissolvedPartition>();
        }
        if (entry && ar.ok()) {
            FSIM_RS_IO("kernel.dissolved", begin, ar, *entry);
            FSIM_RS_IO("kernel.dissolved", readers, ar, *entry);
            FSIM_RS_IO("kernel.dissolved", seen, ar, *entry);
            FSIM_RS_IO("kernel.dissolved", due, ar, *entry);
            FSIM_RS_IO("kernel.dissolved", gated, ar, *entry);
        }
    }

    if (ar.saving()) {
        save_thread_registers();
    }
    FSIM_RS_TABLE("kernel.impl", members_, ar, *this, [&](Member& member) {
        FSIM_RS_IO("kernel.member", root_thread, ar, member);
        FSIM_RS_IO("kernel.member", program_ended, ar, member);
        FSIM_RS_IO("kernel.member", lazy_stamp, ar, member);
        FSIM_RS_IO("kernel.member", registers, ar, member);
        FSIM_RS_IO("kernel.member", dynamic_containers, ar, member);
        FSIM_RS_IO("kernel.member", call_stack, ar, member);
        FSIM_RS_IO("kernel.member", frames, ar, member);
        FSIM_RS_IO("kernel.member", frame_bytes, ar, member);
        FSIM_RS_IO("kernel.member", saved_arrays, ar, member);
        FSIM_RS_IO("kernel.member", saved_array_marks, ar, member);
        FSIM_RS_IO("kernel.member", private_frame_identities, ar, member);
        FSIM_RS_IO("kernel.member", registers_known, ar, member);
        FSIM_RS_IO("kernel.member", two_state_deopts, ar, member);
        FSIM_RS_IO("kernel.member", two_state_off, ar, member);
        FSIM_RS_IO("kernel.member", off_runs, ar, member);
        FSIM_RS_IO("kernel.member", off_retry, ar, member);
        FSIM_RS_IO("kernel.member", fresh, ar, member);
    });
    FSIM_RS_TABLE("kernel.impl", slots_, ar, *this, [&](Slot& slot) {
        FSIM_RS_IO("kernel.slot", edge_seen, ar, slot);
        FSIM_RS_IO("kernel.slot", vhdl_written, ar, slot);
        FSIM_RS_IO("kernel.slot", published, ar, slot);
        FSIM_RS_IO("kernel.slot", deferred_write, ar, slot);
        FSIM_RS_IO("kernel.slot", last_writer, ar, slot);
    });
    FSIM_RS_TABLE("kernel.impl", inputs_, ar, *this, [&](Input& input) {
        FSIM_RS_IO("kernel.input", cached, ar, input);
        FSIM_RS_IO("kernel.input", revision, ar, input);
        FSIM_RS_IO("kernel.input", event_seen, ar, input);
        FSIM_RS_IO("kernel.input", deposited, ar, input);
        FSIM_RS_IO("kernel.input", transaction_seen, ar, input);
        FSIM_RS_IO("kernel.input", unwritten, ar, input);
    });
    // The inputs whose host revision changed since the kernel last read them
    // (Impl::note_signal_revision): the kernel's next scan reads only these,
    // so a change it has not yet seen at the save point stays pending.
    FSIM_RS_IO("interp.impl", static_kernel_dirty_inputs, ar, impl_);
    // The containers are fixed by the design; their values are slot words.
    FSIM_RS_TABLE("kernel.impl", containers_, ar, *this,
        [](Container&) { });
    // The slot values are the section kernel.values (restore_value_section).
    FSIM_RS_CUSTOM("kernel.impl", arena_);
    FSIM_RS_CUSTOM("kernel.impl", published_);

    FSIM_RS_IO("kernel.impl", slot_override_, ar, *this);
    FSIM_RS_IO("kernel.impl", overrides_active_, ar, *this);
    FSIM_RS_IO("kernel.impl", last_writer_, ar, *this);

    // Threads. A thread's callable contexts are written as indices into the
    // table above.
    FSIM_RS_CUSTOM("kernel.impl", threads_);
    const auto threads = ar.count(threads_.size());
    if (ar.loading()) {
        threads_.clear();
        threads_.resize(static_cast<std::size_t>(threads));
    }
    for (auto& thread : threads_) {
        if (!ar.ok()) {
            return;
        }
        FSIM_RS_IO("kernel.thread", member, ar, thread);
        FSIM_RS_IO("kernel.thread", pc, ar, thread);
        FSIM_RS_IO("kernel.thread", call_stack, ar, thread);
        FSIM_RS_IO("kernel.thread", frames, ar, thread);
        FSIM_RS_IO("kernel.thread", frame_bytes, ar, thread);
        FSIM_RS_IO("kernel.thread", saved_arrays, ar, thread);
        FSIM_RS_IO("kernel.thread", saved_array_marks, ar, thread);
        FSIM_RS_IO("kernel.thread", parent, ar, thread);
        FSIM_RS_IO("kernel.thread", group, ar, thread);
        FSIM_RS_IO("kernel.thread", fork_site, ar, thread);
        FSIM_RS_IO("kernel.thread", fork_depth, ar, thread);
        FSIM_RS_CUSTOM("kernel.thread", contexts);
        FSIM_RS_CUSTOM("kernel.thread", isolated);
        std::vector<std::uint64_t> used;
        std::uint64_t isolated = 0U;
        if (ar.saving()) {
            for (const auto& context : thread.contexts) {
                used.push_back(contexts.index_of(context));
            }
            isolated = contexts.index_of(thread.isolated);
        }
        ar.io(used);
        ar.io(isolated);
        if (ar.loading() && ar.ok()) {
            thread.contexts.clear();
            for (const auto index : used) {
                thread.contexts.push_back(contexts.at(index));
            }
            thread.isolated = contexts.at(isolated);
        }
        FSIM_RS_IO("kernel.thread", children, ar, thread);
        FSIM_RS_IO("kernel.thread", child_records, ar, thread);
        FSIM_RS_IO("kernel.thread", ended_record, ar, thread);
        FSIM_RS_IO("kernel.thread", waiting_children, ar, thread);
        FSIM_RS_IO("kernel.thread", detached, ar, thread);
        FSIM_RS_IO("kernel.thread", generation, ar, thread);
        FSIM_RS_IO("kernel.thread", handle, ar, thread);
        FSIM_RS_IO("kernel.thread", suspended, ar, thread);
        FSIM_RS_IO("kernel.thread", suspended_wake, ar, thread);
        FSIM_RS_IO("kernel.thread", suspended_running, ar, thread);
        FSIM_RS_IO("kernel.thread", entered, ar, thread);
        FSIM_RS_IO("kernel.thread", random_state, ar, thread);
        FSIM_RS_IO("kernel.thread", epoch, ar, thread);
        FSIM_RS_IO("kernel.thread", alive, ar, thread);
        FSIM_RS_IO("kernel.thread", queued, ar, thread);
        FSIM_RS_IO("kernel.thread", reclaim, ar, thread);
        FSIM_RS_IO("kernel.thread", deadline, ar, thread);
        FSIM_RS_IO("kernel.thread", timeout_result, ar, thread);
        FSIM_RS_IO("kernel.thread", delivered, ar, thread);
        FSIM_RS_IO("kernel.thread", stack, ar, thread);
        FSIM_RS_IO("kernel.thread", last_point, ar, thread);
        FSIM_RS_IO("kernel.thread", debug_process, ar, thread);
        FSIM_RS_IO("kernel.thread", order, ar, thread);
    }
    FSIM_RS_IO("kernel.impl", free_threads_, ar, *this);
    FSIM_RS_IO("kernel.impl", child_threads_, ar, *this);
    FSIM_RS_CUSTOM("kernel.impl", fork_groups_);
    const auto groups = ar.count(fork_groups_.size());
    if (ar.loading()) {
        fork_groups_.clear();
        fork_groups_.resize(static_cast<std::size_t>(groups));
    }
    for (auto& group : fork_groups_) {
        FSIM_RS_IO("kernel.fork_group", parent, ar, group);
        FSIM_RS_IO("kernel.fork_group", join, ar, group);
        FSIM_RS_IO("kernel.fork_group", remaining, ar, group);
        FSIM_RS_IO("kernel.fork_group", resumed, ar, group);
    }
    FSIM_RS_IO("kernel.impl", free_fork_groups_, ar, *this);
    FSIM_RS_IO("kernel.impl", ready_threads_, ar, *this);
    FSIM_RS_IO("kernel.impl", inactive_threads_, ar, *this);
    FSIM_RS_IO("kernel.impl", vhdl_ready_threads_, ar, *this);

    // Waiters: one list per slot and per input.
    FSIM_RS_IO("kernel.impl", slot_waiters_, ar, *this);
    FSIM_RS_IO("kernel.impl", input_waiters_, ar, *this);
    FSIM_RS_CUSTOM("kernel.waiter", thread);
    FSIM_RS_CUSTOM("kernel.waiter", epoch);
    FSIM_RS_CUSTOM("kernel.waiter", edge);
    FSIM_RS_CUSTOM("kernel.waiter", offset);
    FSIM_RS_CUSTOM("kernel.waiter", width);
    FSIM_RS_IO("kernel.impl", wait_entries_, ar, *this);
    FSIM_RS_IO("kernel.impl", switch_wakes_, ar, *this);

    // Timers, in pop order.
    FSIM_RS_CUSTOM("kernel.impl", timers_);
    queue_io(ar, timers_);
    FSIM_RS_IO("kernel.impl", timer_sequence_, ar, *this);
    FSIM_RS_CUSTOM("kernel.impl", vhdl_timers_);
    queue_io(ar, vhdl_timers_);
    FSIM_RS_CUSTOM("kernel.timer", time);
    FSIM_RS_CUSTOM("kernel.timer", order);
    FSIM_RS_CUSTOM("kernel.timer", sequence);
    FSIM_RS_CUSTOM("kernel.timer", thread);
    FSIM_RS_CUSTOM("kernel.timer", epoch);
    FSIM_RS_CUSTOM("kernel.timer", net_delay);
    FSIM_RS_CUSTOM("kernel.timer", projected);
    FSIM_RS_IO("kernel.impl", vhdl_timers_released_, ar, *this);
    FSIM_RS_CUSTOM("kernel.impl", hosted_timers_);
    queue_io(ar, hosted_timers_);
    FSIM_RS_IO("kernel.impl", hosted_sequence_, ar, *this);
    FSIM_RS_IO("kernel.impl", hosted_next_delta_, ar, *this);
    FSIM_RS_CUSTOM("kernel.hosted_timer", time);
    FSIM_RS_CUSTOM("kernel.hosted_timer", sequence);
    FSIM_RS_CUSTOM("kernel.hosted_timer", ready);
    FSIM_RS_CUSTOM("kernel.hosted_ready", order);
    FSIM_RS_CUSTOM("kernel.hosted_ready", process);
    FSIM_RS_CUSTOM("kernel.hosted_ready", epoch);
    FSIM_RS_CUSTOM("kernel.hosted_ready", timer);
    FSIM_RS_CUSTOM("kernel.hosted_ready", wake);
    FSIM_RS_CUSTOM("kernel.hosted_ready", phase);
    FSIM_RS_CUSTOM("kernel.hosted_ready", thread);
    FSIM_RS_CUSTOM("kernel.hosted_delayed", time);
    FSIM_RS_CUSTOM("kernel.hosted_delayed", delta);
    FSIM_RS_CUSTOM("kernel.hosted_delayed", ready);

    // Delayed net writes (IEEE 1364-2005 16.2.4): the sites are made at the
    // operation's first execution, so a run holds them as state.
    FSIM_RS_IO("kernel.impl", net_delay_sites_, ar, *this);
    FSIM_RS_CUSTOM("kernel.net_delay_site", member);
    FSIM_RS_CUSTOM("kernel.net_delay_site", pc);
    FSIM_RS_CUSTOM("kernel.net_delay_site", part);
    FSIM_RS_CUSTOM("kernel.net_delay_site", slot);
    FSIM_RS_CUSTOM("kernel.net_delay_site", offset);
    FSIM_RS_CUSTOM("kernel.net_delay_site", width);
    FSIM_RS_CUSTOM("kernel.net_delay_site", pending);

    // Projected output waveforms of the members' drivers of kernel signals
    // (IEEE 1076-2019 10.5.2.2, 14.7.2): the transactions not yet matured.
    FSIM_RS_IO("kernel.impl", projected_sites_, ar, *this);

    FSIM_RS_IO("kernel.impl", stopped_, ar, *this);
    FSIM_RS_IO("kernel.impl", paused_thread_, ar, *this);
    FSIM_RS_IO("kernel.impl", lazy_epoch_, ar, *this);
    FSIM_RS_IO("kernel.impl", closed_, ar, *this);
    FSIM_RS_IO("kernel.impl", guard_groups_, ar, *this);
    FSIM_RS_IO("kernel.impl", guard_time_, ar, *this);
    FSIM_RS_IO("kernel.impl", guard_epoch_, ar, *this);
    FSIM_RS_IO("kernel.impl", guard_commit_, ar, *this);
    FSIM_RS_IO("kernel.impl", slot_wait_seen_, ar, *this);
    if (ar.loading() && behavioral_) {
        // A restore start builds the tables but not prepare_behavioral, which
        // sizes slot_waitable_ (a behavioral kernel indexes it by every slot
        // whose slot_wait_seen_ entry exists; the constructor sizes it only
        // when some thread waits on a kernel signal).
        slot_waitable_.resize(slots_.size(), 0U);
    }
    FSIM_RS_IO("kernel.impl", behavioral_suspended_, ar, *this);
    FSIM_RS_IO("kernel.impl", deopt_resume_, ar, *this);
    FSIM_RS_IO("kernel.impl", process_records_, ar, *this);
    FSIM_RS_CUSTOM("kernel.process_record", generation);
    FSIM_RS_CUSTOM("kernel.process_record", thread);
    FSIM_RS_CUSTOM("kernel.process_record", ended);
    FSIM_RS_CUSTOM("kernel.process_record", status);
    FSIM_RS_CUSTOM("kernel.process_record", random_state);
    FSIM_RS_CUSTOM("kernel.process_record", awaiting);
    FSIM_RS_IO("kernel.impl", synchronization_waits_, ar, *this);
    FSIM_RS_CUSTOM("kernel.sync_wait", thread);
    FSIM_RS_CUSTOM("kernel.sync_wait", epoch);
    FSIM_RS_CUSTOM("kernel.sync_wait", dynamic_event);
    FSIM_RS_IO("kernel.impl", lazy_pending_, ar, *this);
    FSIM_RS_IO("kernel.impl", shuffle_state_, ar, *this);
    FSIM_RS_IO("kernel.impl", debug_children_, ar, *this);
    FSIM_RS_IO("kernel.impl", next_child_process_, ar, *this);
    FSIM_RS_IO("kernel.impl", initialized_, ar, *this);
    FSIM_RS_IO("kernel.impl", vhdl_round_deferred_, ar, *this);
    FSIM_RS_IO("kernel.impl", vhdl_round_task_, ar, *this);
    FSIM_RS_IO("kernel.impl", start_once_, ar, *this);
    FSIM_RS_IO("kernel.impl", start_once_next_, ar, *this);
    FSIM_RS_IO("kernel.impl", late_members_, ar, *this);
    FSIM_RS_IO("kernel.impl", late_start_armed_, ar, *this);
    FSIM_RS_IO("kernel.impl", late_start_due_, ar, *this);
    static_cast<void>(kernel_version);

    if (ar.loading() && ar.ok()) {
        restore_thread_registers();
        // The lookup from a net-delay operation to its site.
        net_delay_site_of_.clear();
        for (std::size_t index = 0U; index < net_delay_sites_.size(); ++index) {
            const auto& site = net_delay_sites_[index];
            net_delay_site_of_.emplace(
                net_delay_site_key(site.member, site.pc, site.part),
                static_cast<std::uint32_t>(index));
        }
        projected_site_of_.clear();
        for (std::size_t index = 0U; index < projected_sites_.size(); ++index) {
            const auto& site = projected_sites_[index];
            projected_site_of_.emplace(
                (std::uint64_t { site.member } << 32U) | site.slot,
                static_cast<std::uint32_t>(index));
        }
        // Dissolve again, then put the saved gating state back.
        for (std::size_t partition = 0U; partition < dissolved.size(); ++partition) {
            if (!dissolved[partition]) {
                continue;
            }
            if (partition >= partitions_.size()) {
                ar.fail("a dissolved partition is not in the design");
                return;
            }
            const auto& gated = dissolved[partition]->gated;
            for (std::size_t position = 0U; position < gated.size(); ++position) {
                if (gated[position] == 0U) {
                    continue;
                }
                if (position >= partitions_[partition].members.size()) {
                    ar.fail("a dissolved partition member is not in the design");
                    return;
                }
                dissolve_member(partitions_[partition].members[position]);
            }
            if (dissolved_.size() <= partition || !dissolved_[partition]) {
                ar.fail("a dissolved partition could not be rebuilt");
                return;
            }
            *dissolved_[partition] = std::move(*dissolved[partition]);
        }
    }
}

bool Kernel::save_run_state(RunStateImage& image, std::vector<std::string>& errors)
{
    std::string values;
    record_value_section(values);
    image.put("kernel.values", run_state::kernel_version, std::move(values));
    return save_section(image, "kernel.state", run_state::kernel_version,
        [&](RunStateArchive& ar) { run_state_io(ar); }, errors);
}

bool Kernel::restore_run_state(
    const RunStateImage& image, std::vector<std::string>& errors)
{
    const auto* const values = image.find("kernel.values");
    if (values == nullptr) {
        errors.push_back("FSIM-SAVE-0008: the save file has no section 'kernel.values'");
        return false;
    }
    if (values->version != run_state::kernel_version) {
        errors.push_back("FSIM-SAVE-0009: section 'kernel.values' has version "
            + std::to_string(values->version) + " but this build reads version "
            + std::to_string(run_state::kernel_version));
        return false;
    }
    if (!load_section(image, "kernel.state", run_state::kernel_version,
            [&](RunStateArchive& ar) { run_state_io(ar); }, errors)) {
        return false;
    }
    // The marks as initialize() assigns them (a restored kernel is
    // initialized from the file and never runs it), with exactly the saved
    // inputs listed (one the restored run noted before the load is listed
    // again only if the file lists it).
    impl_.static_kernel_input_marks.assign(
        impl_.signal_value_revisions.size(), 0U);
    for (const auto& input : inputs_) {
        impl_.static_kernel_input_marks[input.signal] = 1U;
    }
    for (const auto signal : impl_.static_kernel_dirty_inputs) {
        if (signal >= input_of_signal_.size()
            || input_of_signal_[signal] == static_kernel_detail::no_slot) {
            errors.push_back("FSIM-SAVE-0010: section 'kernel.state' lists a "
                             "changed input this kernel does not read");
            return false;
        }
        impl_.static_kernel_input_marks[signal] = 2U;
    }
    if (!restore_value_section(values->bytes)) {
        errors.push_back("FSIM-SAVE-0010: section 'kernel.values' does not fit "
                         "this kernel's slots");
        return false;
    }
    return true;
}

void Kernel::rearm_run_state()
{
    // The doorbells are not in the file. Forget the ones the freshly started
    // kernel armed (the scheduler discarded them), then arm them again for the
    // saved timers and flags.
    armed_timer_.reset();
    armed_vhdl_timer_.reset();
    armed_hosted_timer_.reset();
    hosted_region_bell_.fill(false);
    const bool vhdl_round = std::exchange(vhdl_round_deferred_, false);
    arm_timer();
    arm_vhdl_timer();
    arm_hosted_timer();
    if (vhdl_round) {
        defer_vhdl_round();
    }
    if (late_start_armed_) {
        if (const auto first = first_late_process()) {
            queue_late_start(*first);
        }
    }
    for (std::size_t phase = 0U; phase < hosted_regions_.size(); ++phase) {
        if (!hosted_regions_[phase].empty()) {
            ring_hosted_region(static_cast<SchedulerPhase>(phase));
        }
    }
}

}  // namespace fsim::runtime::simir

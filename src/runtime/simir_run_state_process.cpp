// SPDX-License-Identifier: Apache-2.0
//
// Run-state save and restart of the reference processes (#128; IEEE 1800-2023
// D.9, IEEE 1076-2019 20.7-20.9; S4): program counter, flags, suspension and
// wait state, register frames, automatic callable frames, fork bookkeeping
// and VITAL state of every process the interpreter runs itself (a compiled run
// keeps its processes in the static kernel, which has its own sections).
//
// Sections: "host.processes.shared" (the count, the structure journal of
// processes created after start, and the lexical frames and callable contexts
// that several processes share) and "host.processes.<n>", one per 4096
// processes, so an incremental save rewrites only the chunks that changed.
//
// A process created at run time is rebuilt on restore from its journal entry:
// a fork child from its parent and fork site (the program is the parent's, IEEE
// 1800-2023 9.3.2), a foreign driver owner from its model process. The
// program of a static process comes from the design. A process that an
// alternate executor runs (SystemC, a foreign model) keeps its state inside
// the executor and refuses the save (FSIM-SAVE-0007).
#include "simir_run_state.hpp"

#include <algorithm>
#include <cstdio>

namespace fsim::runtime::simir::run_state {

namespace {

using ProcessState = Impl::ProcessState;
using ProcessFrame = Impl::ProcessFrame;
using CallableFrame = Impl::ProcessCallableFrameState;

constexpr std::uint8_t kind_compact = 0U;
constexpr std::uint8_t kind_static = 1U;
constexpr std::uint8_t kind_fork_child = 2U;
constexpr std::uint8_t kind_driver_owner = 3U;

constexpr std::string_view driver_owner_tag { ".$driver_owner[" };

struct Tables {
    SharedTable<ProcessFrame> frames;
    SharedTable<CallableFrame> contexts;
    /// Load: the frame table entry took over an existing process frame.
    std::vector<bool> frame_adopted;
};

[[nodiscard]] std::string chunk_name(const std::size_t chunk)
{
    char digits[16];
    std::snprintf(digits, sizeof digits, "%06zu", chunk);
    return std::string { "host.processes." } + digits;
}

[[nodiscard]] std::size_t chunk_count(const std::size_t processes) noexcept
{
    return (processes + process_chunk - 1U) / process_chunk;
}

/// A context and the contexts it keeps alive, numbered once each.
void discover_context(Tables& tables, const std::shared_ptr<CallableFrame>& context)
{
    if (!context) {
        return;
    }
    const auto before = tables.contexts.objects().size();
    const auto index = tables.contexts.index_of(context);
    if (index > before) {
        discover_context(tables, context->escaping_context);
    }
}

void discover(Impl& impl, Tables& tables)
{
    for (ProcessId id = 0U; id < impl.processes.size(); ++id) {
        auto* const state = impl.processes.full_state_if_present(id);
        if (state == nullptr) {
            continue;
        }
        static_cast<void>(tables.frames.index_of(state->frame));
        auto& cold = state->cold();
        for (auto& frame : cold.callable_frames) {
            discover_context(tables, frame.escaping_context);
        }
        for (const auto& context : cold.escaping_callable_contexts) {
            discover_context(tables, context);
        }
        if (cold.suspended_callable_context) {
            discover_context(tables, cold.suspended_callable_context->escaping_context);
        }
    }
}

/// A callable frame (IEEE 1800-2023 6.21 automatic variables, 9.6.2): its
/// saved register values and the context shared with fork children.
void callable_io(RunStateArchive& ar, CallableFrame& frame, Tables& tables)
{
    FSIM_RS_IO("process.callable_frame", identity, ar, frame);
    FSIM_RS_IO("process.callable_frame", call_depth, ar, frame);
    FSIM_RS_IO("process.callable_frame", packed_ids, ar, frame);
    FSIM_RS_IO("process.callable_frame", string_ids, ar, frame);
    FSIM_RS_IO("process.callable_frame", container_ids, ar, frame);
    FSIM_RS_IO("process.callable_frame", packed, ar, frame);
    FSIM_RS_IO("process.callable_frame", strings, ar, frame);
    FSIM_RS_IO("process.callable_frame", containers, ar, frame);
    // The storage accounting is a function of the contents above.
    FSIM_RS_IO("process.callable_frame", storage_bytes, ar, frame);
    FSIM_RS_CUSTOM("process.callable_frame", escaping_context);
    std::uint64_t escaping = ar.saving() ? tables.contexts.index_of(frame.escaping_context) : 0U;
    ar.io(escaping);
    if (ar.loading()) {
        frame.escaping_context = tables.contexts.at(escaping);
    }
}

void shared_io(RunStateArchive& ar, Impl& impl, Tables& tables,
    std::uint64_t& process_count)
{
    ar.io(process_count);
    const auto frames = ar.count(tables.frames.objects().size());
    if (ar.loading()) {
        for (std::uint64_t index = 0U; index < frames && ar.ok(); ++index) {
            tables.frames.objects().push_back(std::make_shared<ProcessFrame>());
        }
        tables.frame_adopted.assign(static_cast<std::size_t>(frames), false);
    }
    for (auto& frame : tables.frames.objects()) {
        if (!ar.ok()) {
            return;
        }
        ar.io(*frame);
    }
    const auto contexts = ar.count(tables.contexts.objects().size());
    if (ar.loading()) {
        for (std::uint64_t index = 0U; index < contexts && ar.ok(); ++index) {
            tables.contexts.objects().push_back(std::make_shared<CallableFrame>());
        }
    }
    for (auto& context : tables.contexts.objects()) {
        if (!ar.ok()) {
            return;
        }
        callable_io(ar, *context, tables);
    }
    static_cast<void>(impl);
}

void dynamic_wait_io(RunStateArchive& ar, Impl::ProcessDynamicWaitState& wait)
{
    FSIM_RS_IO("process.dynamic_wait", waiting_process, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", process_waiters, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", dynamic_sensitivity, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", dynamic_triggered, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", waiting_on_container, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", dynamic_wait_all, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", wait_order_events, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", wait_order_index, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", wait_order_result, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", wait_timeout_deadline, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", wait_timeout_result, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", wait_timeout_generation, ar, wait);
    FSIM_RS_IO("process.dynamic_wait", dynamic_wait_generation, ar, wait);
}

void fork_state_io(RunStateArchive& ar, Impl::ProcessForkState& fork)
{
    FSIM_RS_IO("process.fork", live_children, ar, fork);
    FSIM_RS_IO("process.fork", active_sites, ar, fork);
}

void vital_io(RunStateArchive& ar, Impl::ProcessVitalState& vital)
{
    FSIM_RS_IO("process.vital", timing, ar, vital);
    FSIM_RS_IO("process.vital", delay, ar, vital);
}

template <typename T, typename Visit>
void owned_io(RunStateArchive& ar, std::unique_ptr<T>& pointer, Visit&& visit)
{
    std::uint8_t present = pointer != nullptr ? 1U : 0U;
    ar.io(present);
    if (!ar.ok()) {
        return;
    }
    if (present > 1U) {
        ar.fail("bad pointer flag");
        return;
    }
    if (ar.loading()) {
        if (present == 0U) {
            pointer.reset();
            return;
        }
        pointer = std::make_unique<T>();
    }
    if (present != 0U) {
        visit(*pointer);
    }
}

void cold_io(RunStateArchive& ar, Impl::ProcessColdState& cold, Tables& tables)
{
    FSIM_RS_IO("process.cold", random_state, ar, cold);
    FSIM_RS_CUSTOM("process.cold", vital_state);
    owned_io(ar, cold.vital_state,
        [&](Impl::ProcessVitalState& vital) { vital_io(ar, vital); });
    FSIM_RS_CUSTOM("process.cold", dynamic_wait_state);
    owned_io(ar, cold.dynamic_wait_state,
        [&](Impl::ProcessDynamicWaitState& wait) { dynamic_wait_io(ar, wait); });
    FSIM_RS_CUSTOM("process.cold", callable_frames);
    const auto frames = ar.count(cold.callable_frames.size());
    if (ar.loading()) {
        cold.callable_frames.clear();
        cold.callable_frames.resize(static_cast<std::size_t>(frames));
    }
    for (auto& frame : cold.callable_frames) {
        if (!ar.ok()) {
            return;
        }
        callable_io(ar, frame, tables);
    }
    // The accounting of the frames above and of the suspended snapshot.
    FSIM_RS_IO("process.cold", callable_frame_storage_bytes, ar, cold);
    FSIM_RS_IO("process.cold", callable_context_storage_bytes, ar, cold);
    FSIM_RS_CUSTOM("process.cold", escaping_callable_contexts);
    std::vector<std::uint64_t> shared;
    if (ar.saving()) {
        for (const auto& context : cold.escaping_callable_contexts) {
            shared.push_back(tables.contexts.index_of(context));
        }
    }
    ar.io(shared);
    if (ar.loading() && ar.ok()) {
        cold.escaping_callable_contexts.clear();
        for (const auto index : shared) {
            cold.escaping_callable_contexts.push_back(tables.contexts.at(index));
        }
    }
    FSIM_RS_CUSTOM("process.cold", suspended_callable_context);
    owned_io(ar, cold.suspended_callable_context,
        [&](CallableFrame& frame) { callable_io(ar, frame, tables); });
    FSIM_RS_IO("process.cold", callable_registers_suspended, ar, cold);
    FSIM_RS_IO("process.cold", pending_mailbox_result, ar, cold);
    FSIM_RS_IO("process.cold", pending_dynamic_event, ar, cold);
    FSIM_RS_IO("process.cold", pending_mailbox_string_result, ar, cold);
    FSIM_RS_IO("process.cold", fork_parent, ar, cold);
    FSIM_RS_IO("process.cold", thread_announced, ar, cold);
    FSIM_RS_IO("process.cold", fork_site, ar, cold);
    FSIM_RS_IO("process.cold", fork_depth, ar, cold);
    FSIM_RS_IO("process.cold", fork_detached, ar, cold);
    FSIM_RS_IO("process.cold", fork_group, ar, cold);
    FSIM_RS_CUSTOM("process.cold", fork_state);
    owned_io(ar, cold.fork_state,
        [&](Impl::ProcessForkState& fork) { fork_state_io(ar, fork); });
    FSIM_RS_IO("process.cold", waiting_for_children, ar, cold);
    FSIM_RS_IO("process.cold", waiting_fork_group, ar, cold);
    FSIM_RS_IO("process.cold", dynamic_call_stack, ar, cold);
}

void state_io(RunStateArchive& ar, ProcessState& state, Tables& tables)
{
    FSIM_RS_IO("process.state", id, ar, state);
    FSIM_RS_IO("process.state", pc, ar, state);
    FSIM_RS_IO("process.state", wait_timeout_origin, ar, state);
    FSIM_RS_IO("process.state", static_trigger_mask, ar, state);
    FSIM_RS_IO("process.state", execution_phase, ar, state);
    FSIM_RS_IO("process.state", queued, ar, state);
    FSIM_RS_IO("process.state", timer_wake, ar, state);
    FSIM_RS_IO("process.state", waiting_on_static, ar, state);
    FSIM_RS_IO("process.state", waiting_on_signal, ar, state);
    FSIM_RS_IO("process.state", generation, ar, state);
    FSIM_RS_IO("process.state", timer_epoch, ar, state);
    FSIM_RS_IO("process.state", status, ar, state);
    FSIM_RS_IO("process.state", suspended_status, ar, state);
    FSIM_RS_IO("process.state", suspended, ar, state);
    FSIM_RS_IO("process.state", suspended_wake, ar, state);
    FSIM_RS_IO("process.state", halted, ar, state);
    FSIM_RS_IO("process.state", killed, ar, state);
    FSIM_RS_IO("process.state", has_callable_frame_push, ar, state);
    // The lexical frame is shared with fork children (IEEE 1800-2023 9.3.2),
    // so a file holds it once and each process names it.
    FSIM_RS_CUSTOM("process.state", frame);
    std::uint64_t frame = ar.saving() ? tables.frames.index_of(state.frame) : 0U;
    ar.io(frame);
    if (ar.loading() && ar.ok()) {
        if (frame == 0U) {
            state.frame.reset();
        } else if (frame > tables.frames.objects().size()) {
            ar.fail("process frame index out of range");
        } else {
            auto& shared = tables.frames.objects()[static_cast<std::size_t>(frame - 1U)];
            const auto position = static_cast<std::size_t>(frame - 1U);
            if (!tables.frame_adopted[position]) {
                tables.frame_adopted[position] = true;
                if (state.frame) {
                    // Keep the frame object the process already has: holders of
                    // its address stay valid.
                    *state.frame = std::move(*shared);
                    shared = state.frame;
                }
            }
            state.frame = shared;
        }
    }
    FSIM_RS_CUSTOM("process.state", cold_state);
    cold_io(ar, state.cold(), tables);
}

[[nodiscard]] std::optional<ProcessId> owner_model(Impl& impl, const ProcessState& state)
{
    const auto& name = state.program().name();
    const auto at = name.rfind(driver_owner_tag);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    const auto model_name = name.substr(0U, at);
    for (ProcessId id = 0U; id < impl.processes.size(); ++id) {
        if (id != state.id && impl.processes.program_view(id).name() == model_name) {
            return id;
        }
    }
    return std::nullopt;
}

/// Whether the running table already holds the dynamic process the image
/// names at `id`: same kind, parent or model, fork site and program name.
[[nodiscard]] bool dynamic_matches(Impl& impl, const ProcessId id,
    const std::uint8_t kind, const ProcessId link, const InstructionIndex site,
    const std::string& name)
{
    const auto* const state = impl.processes.full_state_if_present(id);
    if (state == nullptr || state->program().name() != name) {
        return false;
    }
    if (kind == kind_fork_child) {
        return state->cold().fork_parent == link
            && state->cold().fork_site.value_or(0U) == site;
    }
    return !state->cold().fork_parent && owner_model(impl, *state) == link;
}

/// Creates a process that the saved run made after start.
bool create_dynamic(Impl& impl, const std::uint8_t kind, const ProcessId id,
    const ProcessId parent_or_model, const InstructionIndex site,
    const std::string& name, RunStateArchive& ar)
{
    // A new process takes the next id; a process of the running table that
    // the image names differently (an in-process restart or a reset after
    // forks at other sites) is replaced in place, so its record keeps the
    // address the cohorts point at.
    const bool replace = static_cast<std::size_t>(id) < impl.processes.size();
    if (static_cast<std::size_t>(id) != impl.processes.size() && !replace) {
        ar.fail("process table differs from the design");
        return false;
    }
    if (parent_or_model >= impl.processes.size()) {
        ar.fail("process created after start names a missing parent");
        return false;
    }
    ProcessState child;
    child.id = id;
    auto& source = impl.processes.promote_state(parent_or_model);
    if (kind == kind_fork_child) {
        child.cold().inherit_fork_program(source.cold(), id, site, 0U);
        child.cold().design_process = source.cold().design_process;
        child.cold().program_storage().instance_program.name = name;
        if (replace) {
            impl.processes.promote_state(id) = std::move(child);
        } else {
            impl.processes.push_back(std::move(child));
            impl.static_kernel_adopt_fork_child(parent_or_model, id);
        }
        const bool sensitive
            = !impl.processes.program_view(id).static_sensitivity().empty();
        impl.static_fanout_dirty = impl.static_fanout_dirty || sensitive;
        impl.register_static_sensitivity_cohort(id);
        return true;
    }
    child.cold().inherit_driver_owner_program(source.cold(), id);
    child.cold().design_process = source.cold().design_process;
    child.cold().program_storage().instance_program.name = name;
    if (replace) {
        impl.processes.promote_state(id) = std::move(child);
    } else {
        impl.processes.push_back(std::move(child));
        impl.static_kernel_adopt_fork_child(parent_or_model, id);
    }
    return true;
}

void chunk_io(RunStateArchive& ar, Impl& impl, Tables& tables,
    const std::size_t first, const std::size_t last, bool& forked)
{
    // The process table: one record per process, in id order.
    FSIM_RS_CUSTOM("interp.impl", processes);
    for (std::size_t index = first; index < last && ar.ok(); ++index) {
        const auto id = static_cast<ProcessId>(index);
        std::uint8_t kind { };
        ProcessId link { };
        InstructionIndex site { };
        std::string name;
        if (ar.saving()) {
            auto* const state = impl.processes.full_state_if_present(id);
            kind = kind_static;
            if (state == nullptr) {
                kind = kind_compact;
            } else if (state->cold().fork_parent) {
                kind = kind_fork_child;
                link = *state->cold().fork_parent;
                site = state->cold().fork_site.value_or(0U);
                name = state->program().name();
            } else if (const auto model = owner_model(impl, *state)) {
                kind = kind_driver_owner;
                link = *model;
                name = state->program().name();
            }
        }
        ar.io(kind);
        if (kind == kind_fork_child || kind == kind_driver_owner) {
            ar.io(link);
            ar.io(site);
            ar.io(name);
        }
        if (!ar.ok() || kind == kind_compact) {
            if (kind > kind_driver_owner) {
                ar.fail("bad process kind");
            }
            continue;
        }
        if (kind > kind_driver_owner) {
            ar.fail("bad process kind");
            return;
        }
        if (ar.loading()) {
            if ((kind == kind_fork_child || kind == kind_driver_owner)
                && (index >= impl.processes.size()
                    || !dynamic_matches(impl, id, kind, link, site, name))) {
                if (!create_dynamic(impl, kind, id, link, site, name, ar)) {
                    return;
                }
                forked = forked || kind == kind_fork_child;
            } else if (index >= impl.processes.size()) {
                ar.fail("process table differs from the design");
                return;
            }
            auto* state = impl.processes.full_state_if_present(id);
            if (state == nullptr) {
                state = &impl.processes.promote_state(id);
            }
            state_io(ar, *state, tables);
        } else {
            state_io(ar, *impl.processes.full_state_if_present(id), tables);
        }
    }
}

/// Ends every process at or above `keep`, which the saved run does not have.
void retire_extra_processes(Impl& impl, const std::size_t keep)
{
    for (std::size_t index = keep; index < impl.processes.size(); ++index) {
        const auto id = static_cast<ProcessId>(index);
        auto* state = impl.processes.full_state_if_present(id);
        if (state == nullptr) {
            state = &impl.processes.promote_state(id);
        }
        state->executor.reset();
        ++state->executor_lifecycle_generation;
        state->queued = false;
        state->timer_wake = false;
        state->waiting_on_static = false;
        state->waiting_on_signal = false;
        state->suspended = false;
        state->suspended_wake = false;
        state->halted = true;
        state->killed = true;
        state->status = ProcessStatus::killed;
        ++state->generation;
        ++state->timer_epoch;
        state->wait_timeout_origin.reset();
        if (auto* const wait = state->cold().dynamic_wait_state_if_present()) {
            wait->dynamic_sensitivity.clear();
            wait->waiting_on_container.reset();
            wait->wait_timeout_deadline.reset();
        }
    }
}

/// Re-registers the waits the processes hold, in the tables derived from them.
void rebuild_waits(Impl& impl)
{
    for (auto& fanout : impl.dynamic_fanout) {
        fanout.clear();
    }
    std::ranges::fill(impl.dynamic_fanout_active_counts, 0U);
    std::ranges::fill(impl.dynamic_fanout_tombstone_counts, 0U);
    for (auto& waiters : impl.container_dynamic_fanout) {
        waiters.clear();
    }
    for (ProcessId id = 0U; id < impl.processes.size(); ++id) {
        auto* const state = impl.processes.full_state_if_present(id);
        if (state == nullptr) {
            continue;
        }
        auto* const wait = state->cold().dynamic_wait_state_if_present();
        if (wait == nullptr) {
            continue;
        }
        if (wait->waiting_on_container
            && *wait->waiting_on_container < impl.container_dynamic_fanout.size()) {
            impl.container_dynamic_fanout[*wait->waiting_on_container].push_back(id);
        }
        if (!wait->dynamic_sensitivity.empty() && state->waiting_on_signal) {
            // The registration advances the wait generation by one; the saved
            // one is kept.
            const auto generation = wait->dynamic_wait_generation;
            wait->dynamic_wait_generation = generation == 0U ? 0U : generation - 1U;
            wait->dynamic_fanout_positions.clear();
            impl.register_dynamic_wait_fanout(*state);
            wait->dynamic_wait_generation = generation;
        }
    }
}

}  // namespace

void audit_processes(const Impl& impl, const Findings& findings)
{
    for (ProcessId id = 0U; id < impl.processes.size(); ++id) {
        const auto* const state = impl.processes.full_state_if_present(id);
        if (state == nullptr || !state->executor || state->halted
            || state->killed) {
            continue;
        }
        // IEEE 1800-2023 D.9: $save keeps the whole state of the simulation
        // and IEEE 1076-2019 20.7 the state of the VHDL model. An executor
        // that is not state-free holds registers and a program counter in
        // its own (compiled or foreign) storage, which the image does not
        // describe, so the save is refused instead of restarting the process
        // from another point. A fork child's executor is a clone of its
        // parent's (IEEE 1800-2023 9.3.2) and carries the same limit.
        const bool child = state->cold().fork_parent.has_value();
        if (!state->executor->run_state_free()) {
            findings.add("FSIM-SAVE-0007: " + std::string { child ? "fork child " : "process " }
                + std::to_string(id) + " runs under an alternate executor that keeps its "
                "registers and program counter in its own storage, which the "
                "run-state image does not describe (IEEE 1800-2023 D.9, IEEE "
                "1076-2019 20.7: the whole state must be saved)");
            return;
        }
        if (child) {
            findings.add("FSIM-SAVE-0007: fork child " + std::to_string(id)
                + " carries a cloned executor, which a restart cannot recreate "
                  "from its parent (IEEE 1800-2023 9.3.2, D.9)");
            return;
        }
    }
}

bool save_processes(Impl& impl, RunStateImage& image, std::vector<std::string>& errors)
{
    Tables tables;
    discover(impl, tables);
    std::uint64_t count = impl.processes.size();
    bool ok = save_section(image, "host.processes.shared", process_version,
        [&](RunStateArchive& ar) { shared_io(ar, impl, tables, count); }, errors);
    bool forked = false;
    for (std::size_t chunk = 0U; chunk < chunk_count(impl.processes.size()); ++chunk) {
        const auto first = chunk * process_chunk;
        const auto last = std::min(first + process_chunk, impl.processes.size());
        ok = save_section(image, chunk_name(chunk), process_version,
                 [&](RunStateArchive& ar) {
                     chunk_io(ar, impl, tables, first, last, forked);
                 }, errors) && ok;
    }
    return ok;
}

bool restore_processes(
    Impl& impl, const RunStateImage& image, std::vector<std::string>& errors)
{
    Tables tables;
    std::uint64_t count { };
    if (!load_section(image, "host.processes.shared", process_version,
            [&](RunStateArchive& ar) { shared_io(ar, impl, tables, count); }, errors)) {
        return false;
    }
    const auto design_processes = static_cast<std::size_t>(count);
    bool forked = false;
    for (std::size_t chunk = 0U; chunk < chunk_count(static_cast<std::size_t>(count));
         ++chunk) {
        const auto first = chunk * process_chunk;
        const auto last = std::min(first + process_chunk, static_cast<std::size_t>(count));
        if (!load_section(image, chunk_name(chunk), process_version,
                [&](RunStateArchive& ar) {
                    chunk_io(ar, impl, tables, first, last, forked);
                }, errors)) {
            return false;
        }
    }
    // A run that went on after the image was taken (an in-process $restart or
    // a reset) may have created more processes than the image holds. They
    // cannot be removed from the table (ids are positions), so each ends
    // dead: killed, unqueued and out of every wait table, as a fork child
    // that already finished (IEEE 1800-2023 9.6.3).
    retire_extra_processes(impl, design_processes);
    if (forked) {
        impl.rebuild_static_fanout();
    }
    rebuild_waits(impl);
    return true;
}

}  // namespace fsim::runtime::simir::run_state

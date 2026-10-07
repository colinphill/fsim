// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

namespace {

    constexpr std::string_view random_state_prefix { "fsim-randstate-v1:" };

    [[nodiscard]] std::string encode_random_state(const std::uint64_t state)
    {
        constexpr std::string_view digits { "0123456789abcdef" };
        std::string result { random_state_prefix };
        result.resize(result.size() + 16U);
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto shift = 4U * (15U - index);
            result[random_state_prefix.size() + index]
                = digits[(state >> shift) & 0xfU];
        }
        return result;
    }

    [[nodiscard]] std::optional<std::uint64_t> decode_random_state(
        const std::string_view text)
    {
        if (!text.starts_with(random_state_prefix)
            || text.size() != random_state_prefix.size() + 16U) {
            return std::nullopt;
        }
        std::uint64_t result { };
        for (const auto character : text.substr(random_state_prefix.size())) {
            const auto digit = character >= '0' && character <= '9'
                ? static_cast<unsigned>(character - '0')
                : character >= 'a' && character <= 'f'
                ? static_cast<unsigned>(character - 'a') + 10U
                : character >= 'A' && character <= 'F'
                ? static_cast<unsigned>(character - 'A') + 10U
                : 16U;
            if (digit >= 16U)
                return std::nullopt;
            result = (result << 4U) | digit;
        }
        return result;
    }

} // namespace

[[nodiscard]] bool Interpreter::Impl::handle_fork_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const Operation& operation)
{
    if (const auto* fork = fsim::runtime::simir::operation_get_if<Fork>(&operation)) {
        clear_wait_timeout(process);
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.cold().current_source);
        spawn_fork(process, instruction, *fork);
        return true;
    }
    if (fsim::runtime::simir::operation_holds<ForkEnd>(operation)) {
        if (!process.cold().fork_parent) {
            process.pc = instruction;
            fail(process, "ForkEnd requires a dynamically spawned fork child");
        }
        clear_wait_timeout(process);
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.cold().current_source);
        snapshot_callable_context(process);
        complete_fork_child(process);
        return true;
    }
    if (fsim::runtime::simir::operation_holds<WaitFork>(operation)) {
        clear_wait_timeout(process);
        if (!process.cold().fork_state
            || process.cold().fork_state->live_children.empty()) {
            process.status = ProcessStatus::running;
            queue_current(process.id);
        } else {
            process.cold().waiting_for_children = true;
            process.status = ProcessStatus::waiting;
        }
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.cold().current_source);
        return true;
    }
    if (const auto* disable_block = fsim::runtime::simir::operation_get_if<DisableBlock>(
            &operation)) {
        if (disable_block->begin >= disable_block->end
            || disable_block->end > process.program().operations().size()) {
            process.pc = instruction;
            fail(process, "DisableBlock has an invalid lexical interval");
        }
        auto* owner = &process;
        while (owner->cold().fork_parent && owner->cold().fork_site
            && *owner->cold().fork_site >= disable_block->begin
            && *owner->cold().fork_site < disable_block->end) {
            owner = &get_process(*owner->cold().fork_parent);
        }
        std::vector<ProcessId> scoped_children;
        if (const auto* const fork_state = owner->cold().fork_state.get()) {
            for (const auto child_id : fork_state->live_children) {
                const auto& child = get_process(child_id);
                if (child.cold().fork_site
                    && *child.cold().fork_site >= disable_block->begin
                    && *child.cold().fork_site < disable_block->end) {
                    scoped_children.push_back(child_id);
                }
            }
        }
        for (const auto child_id : scoped_children) {
            auto& child = get_process(child_id);
            cancel_fork_descendants(child);
            if (!child.halted) {
                complete_fork_child(child, ProcessStatus::killed);
            }
        }
        const bool owner_inside = owner->pc >= disable_block->begin
            && owner->pc < disable_block->end;
        if (owner_inside) {
            clear_wait_timeout(*owner);
            owner->cold().waiting_for_children = false;
            owner->pc = disable_block->end;
            if (owner->executor) {
                owner->region_kernel_completion_boundary_validated = false;
                advance_process_executor_generation(*owner);
                owner->executor->redirect(disable_block->end);
            }
            owner->status = ProcessStatus::running;
            if (owner != &process) {
                queue_active_current(owner->id);
            }
        }
        return true;
    }
    if (!fsim::runtime::simir::operation_holds<DisableFork>(operation)) {
        return false;
    }
    const auto& disable = *fsim::runtime::simir::operation_get_if<DisableFork>(&operation);
    clear_wait_timeout(process);
    if (!disable.site) {
        cancel_fork_descendants(process);
    } else {
        ProcessState* owner = &process;
        while ((!owner->cold().fork_state
                   || !owner->cold().fork_state->active_sites.contains(
                       *disable.site))
            && owner->cold().fork_parent) {
            owner = &get_process(*owner->cold().fork_parent);
        }
        if (const auto* const fork_state = owner->cold().fork_state.get()) {
            const auto found = fork_state->active_sites.find(*disable.site);
            if (found != fork_state->active_sites.end()) {
                const std::vector<ProcessId> children {
                    found->second.begin(), found->second.end()
                };
                for (const auto child_id : children) {
                    auto& child = get_process(child_id);
                    cancel_fork_descendants(child);
                    if (!child.halted) {
                        complete_fork_child(child, ProcessStatus::killed);
                    }
                }
            }
        }
    }
    if (!process.halted) {
        process.status = ProcessStatus::running;
        if (!disable.site) {
            queue_current(process.id);
        }
    }
    if (!disable.site) {
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.cold().current_source);
    }
    return true;
}

std::uint64_t Interpreter::Impl::process_handle(
    const ProcessState& process) const
{
    const auto encoded_id = static_cast<std::uint64_t>(process.id) + 1U;
    if (encoded_id > std::numeric_limits<std::uint32_t>::max()
        || process.generation == 0) {
        throw std::overflow_error { "SimIR process handle identity overflow" };
    }
    return (static_cast<std::uint64_t>(process.generation) << 32U)
        | encoded_id;
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::process_from_handle(
    ProcessState& caller,
    const RegisterId source)
{
    const auto payload = caller.executor
        ? caller.executor->read_register(source, 64)
        : get_register(caller, source);
    if (payload.width() != 64) {
        fail(caller, "process handle register must be 64 bits");
    }
    const auto word = payload.low_word();
    if (word.bval != 0 || word.aval == 0) {
        fail(caller, "process handle is null or unknown");
    }
    const auto encoded_id = static_cast<std::uint32_t>(word.aval);
    const auto generation = static_cast<std::uint32_t>(word.aval >> 32U);
    if (encoded_id == 0
        || static_cast<std::size_t>(encoded_id - 1U)
            >= processes.size()) {
        fail(caller, "process handle is stale");
    }
    auto& target = get_process(
        static_cast<ProcessId>(encoded_id - 1U));
    if (generation == 0 || target.generation != generation) {
        fail(caller, "process handle generation is stale");
    }
    return target;
}

void Interpreter::Impl::complete_process(
    ProcessState& process,
    const ProcessStatus terminal_status)
{
    if (terminal_status != ProcessStatus::finished
        && terminal_status != ProcessStatus::killed) {
        fail(process, "process completion requires a terminal status");
    }
    process.halted = true;
    process.killed = terminal_status == ProcessStatus::killed;
    process.status = terminal_status;
    process.suspended = false;
    process.suspended_wake = false;
    process.waiting_on_static = false;
    process.cold().waiting_for_children = false;
    process.cold().waiting_fork_group.reset();
    auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (dynamic_wait != nullptr && dynamic_wait->waiting_process) {
        auto& target = get_process(*dynamic_wait->waiting_process);
        if (auto* const target_wait
            = target.cold().dynamic_wait_state_if_present()) {
            target_wait->process_waiters.erase(process.id);
        }
        dynamic_wait->waiting_process.reset();
    }
    remove_dynamic_wait(process);
    clear_wait_timeout(process);

    std::set<ProcessId> waiters;
    if (dynamic_wait != nullptr) {
        waiters.swap(dynamic_wait->process_waiters);
    }
    for (const auto waiter_id : waiters) {
        auto& waiter = get_process(waiter_id);
        auto* const waiter_wait
            = waiter.cold().dynamic_wait_state_if_present();
        if (waiter.halted
            || waiter_wait == nullptr
            || waiter_wait->waiting_process != process.id) {
            continue;
        }
        waiter_wait->waiting_process.reset();
        waiter.status = ProcessStatus::running;
        queue_active_current(waiter_id);
    }
    process.cold().escaping_callable_contexts.clear();
}

[[nodiscard]] bool Interpreter::Impl::handle_process_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const Operation& operation)
{
    if (const auto* self = fsim::runtime::simir::operation_get_if<ProcessSelf>(
            &operation)) {
        write_process_register(
            process,
            self->destination,
            PackedLogic4::from_aval_bval(
                64, process_handle(process), 0));
        process.status = ProcessStatus::running;
        return true;
    }
    if (const auto* query = fsim::runtime::simir::operation_get_if<ProcessStatusQuery>(
            &operation)) {
        const auto status = process_from_handle(
            process, query->source)
                                .status;
        write_process_register(
            process,
            query->destination,
            PackedLogic4::from_aval_bval(
                32, static_cast<std::uint32_t>(status), 0));
        process.status = ProcessStatus::running;
        return true;
    }
    if (const auto* query = fsim::runtime::simir::operation_get_if<ProcessCompleted>(
            &operation)) {
        const auto status = process_from_handle(
            process, query->source)
                                .status;
        const auto completed = status == ProcessStatus::finished
            || status == ProcessStatus::killed;
        write_process_register(
            process,
            query->destination,
            PackedLogic4::from_aval_bval(1, completed ? 1U : 0U, 0));
        process.status = ProcessStatus::running;
        return true;
    }
    if (const auto* alias = fsim::runtime::simir::operation_get_if<EventAlias>(
            &operation)) {
        const auto& target = get_signal(alias->target);
        const auto* source = alias->has_source
            ? &get_signal(alias->source)
            : nullptr;
        if (!target.event_variable
            || (source != nullptr && !source->event_variable)) {
            fail(process, "EventAlias requires named-event variables");
        }
        prepare_region_authoritative_write(alias->target);
        set_event_identity(
            alias->target,
            source != nullptr
                ? event_identities.at(alias->source)
                : std::nullopt);
        if (source != nullptr) {
            driven_values.at(alias->target) = driven_values.at(alias->source);
            signals.at(alias->target).initial_value = source->initial_value;
            signal_last_values.at(alias->target)
                = signal_last_values.at(alias->source);
            signal_events.at(alias->target) = signal_events.at(alias->source);
            signal_event_scheduling_stamps.at(alias->target)
                = signal_event_scheduling_stamps.at(alias->source);
            signal_transactions.at(alias->target)
                = signal_transactions.at(alias->source);
        }
        process.status = ProcessStatus::running;
        return true;
    }
    if (const auto* query = fsim::runtime::simir::operation_get_if<EventTriggered>(
            &operation)) {
        (void)get_signal(query->event);
        const auto& signal = get_signal(query->event);
        const auto identity = signal.event_variable
            ? event_identities.at(query->event)
            : query->event;
        const auto& event = identity
            ? signal_events[*identity]
            : std::optional<std::pair<SimulationTick, std::uint64_t>> { };
        const bool triggered = event
            && event->first == scheduler.now();
        write_process_register(
            process,
            query->destination,
            PackedLogic4::from_aval_bval(
                1, triggered ? 1U : 0U, 0));
        process.status = ProcessStatus::running;
        return true;
    }
    if (const auto* await = fsim::runtime::simir::operation_get_if<ProcessAwait>(
            &operation)) {
        auto& target = process_from_handle(process, await->source);
        if (target.id == process.id) {
            process.pc = instruction;
            fail(process, "a process cannot await itself");
        }
        if (target.status == ProcessStatus::finished
            || target.status == ProcessStatus::killed) {
            clear_wait_timeout(process);
            process.status = ProcessStatus::running;
            queue_current(process.id);
        } else {
            ProcessDynamicWaitState* target_wait { };
            ProcessDynamicWaitState* process_wait { };
            const auto resume_pc = process.pc;
            process.pc = instruction;
            target_wait
                = &target.cold().ensure_dynamic_wait_state();
            process_wait
                = &process.cold().ensure_dynamic_wait_state();
            process.pc = resume_pc;
            clear_wait_timeout(process);
            target_wait->process_waiters.insert(process.id);
            process_wait->waiting_process = target.id;
            process.status = ProcessStatus::waiting;
        }
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.cold().current_source);
        return true;
    }
    if (const auto* get = fsim::runtime::simir::operation_get_if<ProcessGetRandState>(
            &operation)) {
        auto& target = process_from_handle(process, get->source);
        const auto state = encode_random_state(target.cold().random_state);
        if (process.executor) {
            process.executor->write_string_register(get->destination, state);
        } else {
            get_string_register(process, get->destination) = state;
        }
        process.status = ProcessStatus::running;
        return true;
    }
    if (const auto* set = fsim::runtime::simir::operation_get_if<ProcessSetRandState>(
            &operation)) {
        auto& target = process_from_handle(process, set->source);
        const auto& text = process.executor
            ? process.executor->read_string_register(set->state)
            : get_string_register(process, set->state);
        const auto state = decode_random_state(text);
        if (!state) {
            process.pc = instruction;
            fail(process, "process random-state token is malformed");
        }
        target.cold().random_state = *state;
        process.status = ProcessStatus::running;
        return true;
    }
    if (const auto* seed = fsim::runtime::simir::operation_get_if<ProcessSrandom>(
            &operation)) {
        auto& target = process_from_handle(process, seed->source);
        const auto payload = process.executor
            ? process.executor->read_register(seed->seed, 32U)
            : get_register(process, seed->seed);
        const auto word = payload.low_word();
        if (payload.width() != 32U || word.bval != 0) {
            process.pc = instruction;
            fail(process, "process srandom seed must be a known 32-bit value");
        }
        target.cold().random_state = static_cast<std::uint32_t>(word.aval);
        process.status = ProcessStatus::running;
        return true;
    }
    const auto* kill = fsim::runtime::simir::operation_get_if<ProcessKill>(&operation);
    const auto* suspend = fsim::runtime::simir::operation_get_if<ProcessSuspend>(&operation);
    const auto* resume = fsim::runtime::simir::operation_get_if<ProcessResume>(&operation);
    if (kill == nullptr && suspend == nullptr && resume == nullptr)
        return false;
    const auto source = kill != nullptr ? kill->source
        : suspend != nullptr            ? suspend->source
                                        : resume->source;
    auto& target = process_from_handle(process, source);
    clear_wait_timeout(process);
    if (kill != nullptr && !target.halted) {
        cancel_fork_descendants(target);
        if (target.id == process.id) {
            snapshot_callable_context(target);
        }
        if (target.cold().fork_parent) {
            complete_fork_child(target, ProcessStatus::killed);
        } else {
            complete_process(target, ProcessStatus::killed);
        }
    } else if (suspend != nullptr && !target.halted && !target.suspended) {
        target.suspended_status = target.status;
        target.suspended = true;
        target.status = ProcessStatus::suspended;
    } else if (resume != nullptr && target.suspended) {
        target.suspended = false;
        const bool make_runnable = target.suspended_wake
            || target.suspended_status == ProcessStatus::running;
        target.suspended_wake = false;
        target.status = make_runnable
            ? ProcessStatus::running
            : target.suspended_status;
        if (make_runnable && !target.queued) {
            queue_active_current(target.id);
        }
    }
    if (!process.halted && !process.suspended) {
        process.status = ProcessStatus::running;
        queue_current(process.id);
    }
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.cold().current_source);
    return true;
}

void Interpreter::Impl::spawn_fork(
    ProcessState& parent,
    const InstructionIndex instruction,
    const Fork& operation)
{
    const auto parent_id = parent.id;
    if (!parent.frame && !parent.executor) {
        fail(parent, "fork parent has no lexical frame");
    }
    if (instruction + 1 >= parent.program().operations().size()) {
        fail(parent, "fork parent continuation is outside the operation stream");
    }
    for (const auto branch : operation.branches) {
        if (branch >= parent.program().operations().size()) {
            fail(parent, "fork branch entry is outside the operation stream");
        }
        if (branch <= instruction + 1) {
            fail(parent, "fork branch must follow its parent continuation");
        }
    }
    switch (operation.join) {
    case ForkJoinKind::all:
    case ForkJoinKind::any:
    case ForkJoinKind::none:
        break;
    default:
        fail(parent, "fork has an invalid join kind");
    }
    if (std::set<InstructionIndex> {
            operation.branches.begin(), operation.branches.end() }
            .size()
        != operation.branches.size()) {
        fail(parent, "fork branch entry is duplicated");
    }
    const auto selected_fork_spawn_filter = fork_spawn_filter;
    if (selected_fork_spawn_filter) {
        prepare_callback_observation();
        if (!(*selected_fork_spawn_filter)(parent.cold().design_process)) {
            parent.status = ProcessStatus::running;
            queue_current(parent_id);
            return;
        }
    }
    if (next_fork_group == 0) {
        throw std::overflow_error { "SimIR fork-group identity overflow" };
    }
    if (!operation.branches.empty() && !parent.cold().fork_state) {
        parent.cold().fork_state = std::make_unique<ProcessForkState>();
    }
    const auto group_id = next_fork_group++;
    const auto shared_frame = parent.frame;
    const auto design_process = parent.cold().design_process;
    auto* const executor = parent.executor.get();
    auto inherited_contexts = parent.cold().escaping_callable_contexts;
    if (operation.join == ForkJoinKind::none
        && !parent.cold().callable_frames.empty()) {
        auto& callable = parent.cold().callable_frames.back();
        if (!callable.escaping_context) {
            auto context
                = std::make_shared<ProcessState::CallableFrameState>();
            context->identity = callable.identity;
            context->packed_ids = callable.packed_ids;
            context->string_ids = callable.string_ids;
            context->container_ids = callable.container_ids;
            capture_callable_values(parent, *context);
            callable.escaping_context = std::move(context);
        }
        if (std::ranges::find(
                inherited_contexts, callable.escaping_context)
            == inherited_contexts.end()) {
            inherited_contexts.push_back(callable.escaping_context);
        }
    }

    std::set<ProcessId> children;
    for (const auto branch : operation.branches) {
        const auto child_id = static_cast<ProcessId>(processes.size());
        if (static_cast<std::size_t>(child_id) != processes.size()) {
            throw std::length_error { "too many dynamic SimIR processes" };
        }
        ProcessState child;
        if (next_process_generation == 0) {
            throw std::overflow_error { "SimIR process generation overflow" };
        }
        child.generation = next_process_generation++;
        const auto child_index = children.size();
        child.cold().inherit_fork_program(
            parent.cold(), child_id, instruction, child_index);
        child.cold().design_process = design_process;
        child.id = child_id;
        child.has_callable_frame_push = parent.has_callable_frame_push;
        child.execution_phase = process_execution_phase(
            child.program().observed(),
            child.program().reactive(),
            child.program().postponed());
        child.pc = branch;
        child.frame = shared_frame;
        child.cold().escaping_callable_contexts = inherited_contexts;
        if (executor != nullptr) {
            const bool parent_access_complete_before_clone
                = process_signal_access_is_complete(parent_id);
            if (!parent_access_complete_before_clone) {
                process_signal_access_inventory_complete = false;
                // Do not invoke an unverified nested clone before publishing
                // the exact hidden signal state it could observe or mutate.
                prepare_callback_observation();
            }
            advance_process_executor_generation(parent);
            advance_process_executor_generation(child);
            child.executor = executor->fork_clone(branch);
            if (!child.executor) {
                throw InterpreterError {
                    parent_id, instruction, "fork executor clone is null"
                };
            }
            const auto* const parent_access_binding
                = executor->program_access_binding();
            const auto* const access_binding
                = child.executor->program_access_binding();
            const bool parent_access_complete_after_clone
                = parent_access_complete_before_clone
                && process_signal_access_is_complete(parent_id);
            if (!parent_access_complete_after_clone
                || parent_access_binding == nullptr
                || access_binding == nullptr
                || !parent_access_binding->same_execution_binding(
                    *access_binding)
                || !child.program().matches_forked_binding(
                    child_id, *parent_access_binding)) {
                process_signal_access_inventory_complete = false;
                // A fork clone can introduce arbitrary signal effects even
                // when its parent executor had a valid binding. Materialize
                // hidden state before the child becomes runnable.
                prepare_callback_observation();
            } else {
                child.cold().fork_state
                    = std::make_unique<ProcessForkState>();
                child.cold().fork_state->access_binding_attestation.emplace(
                    InterpreterProgramAccess::fork_access_attestation(
                        *access_binding));
            }
        }
        child.cold().random_state = initial_random_state(root_seed, child_id);
        child.cold().fork_parent = parent_id;
        child.cold().fork_site = instruction;
        if (operation.join != ForkJoinKind::none) {
            child.cold().fork_group = group_id;
        }
        try {
            // A child shares its parent's program; without static
            // sensitivity it adds no fanout entries (the rebuild visits
            // every process, so avoid it per spawn).
            const bool sensitive
                = !child.program().static_sensitivity().empty();
            processes.push_back(std::move(child));
            static_fanout_dirty = static_fanout_dirty || sensitive;
            register_static_sensitivity_cohort(child_id);
            invalidate_fused_static_cohorts_for_fork(child_id);
        } catch (...) {
            // Insertion may have changed the graph before failing. Retire
            // every certificate rather than retaining a partial topology.
            invalidate_fused_static_cohorts();
            throw;
        }
        children.insert(child_id);
    }

    if (!children.empty()) {
        rebuild_static_fanout();
    }

    auto& current_parent = get_process(parent_id);
    if (!children.empty()) {
        auto& fork_state = *current_parent.cold().fork_state;
        fork_state.live_children.insert(children.begin(), children.end());
        auto& site_children = fork_state.active_sites[instruction];
        site_children.insert(children.begin(), children.end());
    }
    if (operation.join != ForkJoinKind::none && !children.empty()) {
        fork_groups.emplace(
            group_id,
            ForkGroup {
                parent_id, instruction, operation.join, children, false });
        current_parent.cold().waiting_fork_group = group_id;
        current_parent.status = ProcessStatus::waiting;
    }

    for (const auto child : children) {
        queue_active_current(child);
    }
    if (operation.join == ForkJoinKind::none || children.empty()) {
        current_parent.status = ProcessStatus::running;
        queue_current(parent_id);
    }
}

void Interpreter::Impl::complete_fork_child(
    ProcessState& child,
    const ProcessStatus status)
{
    const auto child_id = child.id;
    const auto parent_id = child.cold().fork_parent;
    const auto group_id = child.cold().fork_group;
    complete_process(child, status);
    if (!parent_id) {
        return;
    }

    auto& parent = get_process(*parent_id);
    if (auto* const fork_state = parent.cold().fork_state.get()) {
        fork_state->live_children.erase(child_id);
        for (auto site = fork_state->active_sites.begin();
             site != fork_state->active_sites.end();) {
            site->second.erase(child_id);
            if (site->second.empty()) {
                site = fork_state->active_sites.erase(site);
            } else {
                ++site;
            }
        }
    }
    if (parent.cold().waiting_for_children
        && (!parent.cold().fork_state
            || parent.cold().fork_state->live_children.empty())) {
        parent.cold().waiting_for_children = false;
        parent.status = ProcessStatus::running;
        queue_active_current(*parent_id);
    }
    if (!group_id) {
        return;
    }
    const auto found = fork_groups.find(*group_id);
    if (found == fork_groups.end()) {
        return;
    }
    auto& group = found->second;
    group.children.erase(child_id);
    const bool resume_parent = group.join == ForkJoinKind::any
        ? !group.parent_resumed
        : group.children.empty();
    if (!resume_parent) {
        return;
    }
    group.parent_resumed = true;
    parent.cold().waiting_fork_group.reset();
    parent.status = ProcessStatus::running;
    queue_active_current(group.parent);
    if (group.join == ForkJoinKind::any) {
        for (const auto remaining : group.children) {
            get_process(remaining).cold().fork_group.reset();
        }
    }
    fork_groups.erase(found);
}

void Interpreter::Impl::cancel_fork_descendants(ProcessState& parent)
{
    std::vector<ProcessId> children;
    children.reserve(processes.size());
    // Compact constant processes have no fork operations and therefore never
    // own fork descendants.
    for (const auto& candidate : processes) {
        if (candidate.cold().fork_parent == parent.id) {
            children.push_back(candidate.id);
        }
    }
    for (const auto child_id : children) {
        auto& child = get_process(child_id);
        cancel_fork_descendants(child);
        if (!child.halted) {
            complete_fork_child(child, ProcessStatus::killed);
        }
    }
    if (auto* const fork_state = parent.cold().fork_state.get()) {
        fork_state->live_children.clear();
        fork_state->active_sites.clear();
    }
    parent.cold().waiting_for_children = false;
    parent.cold().waiting_fork_group.reset();
}

void Interpreter::Impl::kill_dynamic_processes(
    const std::span<const ProcessId> design_processes)
{
    std::vector<ProcessId> roots;
    roots.reserve(processes.size());
    // Compact constants are initialization-only records with no dynamic
    // fork parent; this state scan intentionally visits full records.
    for (const auto& candidate : processes) {
        if (candidate.halted || !candidate.cold().fork_parent
            || std::ranges::find(
                   design_processes, candidate.cold().design_process)
                == design_processes.end()) {
            continue;
        }
        const auto& parent = get_process(*candidate.cold().fork_parent);
        if (!parent.cold().fork_parent || parent.halted) {
            roots.push_back(candidate.id);
        }
    }
    for (const auto root : roots) {
        auto& process = get_process(root);
        if (process.halted) {
            continue;
        }
        cancel_fork_descendants(process);
        complete_fork_child(process, ProcessStatus::killed);
    }
}

void Interpreter::Impl::exit_program(ProcessState& process)
{
    if (!process.program().program_owner()) {
        fail(process, "$exit requires a SystemVerilog program owner");
    }
    const auto owner = *process.program().program_owner();
    if (exited_programs.contains(owner)) {
        return;
    }

    std::vector<ProcessId> roots;
    // Compact constants have no program owner and cannot be program exits.
    for (const auto& candidate : processes) {
        if (!candidate.halted && !candidate.cold().fork_parent
            && candidate.program().program_owner() == owner
            && !candidate.program().final()) {
            roots.push_back(candidate.id);
        }
    }
    for (const auto root : roots) {
        auto& owned = get_process(root);
        cancel_fork_descendants(owned);
        if (!owned.halted) {
            complete_process(owned, ProcessStatus::killed);
        }
    }
    exited_programs.insert(owner);
    if (!program_owners.empty()
        && exited_programs.size() == program_owners.size()) {
        stopped_by_design = true;
        scheduler.request_stop();
    }
}

void Interpreter::Impl::complete_program_process(ProcessState& process)
{
    if (!process.program().program_owner() || process.program().final()
        || process.cold().fork_parent) {
        return;
    }
    const auto owner = *process.program().program_owner();
    const bool live_initial = std::ranges::any_of(
        processes,
        [&](const ProcessState& candidate) {
            return !candidate.halted && !candidate.cold().fork_parent
                && !candidate.program().final()
                && candidate.program().program_owner() == owner;
        });
    if (!live_initial) {
        exit_program(process);
    }
}

} // namespace fsim::runtime::simir

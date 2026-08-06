// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

[[nodiscard]] bool Interpreter::Impl::handle_fork_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const Operation& operation) {
  if (const auto* fork = fsim::runtime::simir::operation_get_if<Fork>(&operation)) {
    clear_wait_timeout(process);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    spawn_fork(process, instruction, *fork);
    return true;
  }
  if (fsim::runtime::simir::operation_holds<ForkEnd>(operation)) {
    if (!process.fork_parent) {
      process.pc = instruction;
      fail(process, "ForkEnd requires a dynamically spawned fork child");
    }
    clear_wait_timeout(process);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    complete_fork_child(process);
    return true;
  }
  if (fsim::runtime::simir::operation_holds<WaitFork>(operation)) {
    clear_wait_timeout(process);
    if (process.live_children.empty()) {
      process.status = ProcessStatus::running;
      queue_current(process.program.id);
    } else {
      process.waiting_for_children = true;
      process.status = ProcessStatus::waiting;
    }
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return true;
  }
  if (!fsim::runtime::simir::operation_holds<DisableFork>(operation)) {
    return false;
  }
  clear_wait_timeout(process);
  cancel_fork_descendants(process);
  process.status = ProcessStatus::running;
  queue_current(process.program.id);
  notify_execution_point(
      process, instruction, ExecutionPointKind::process_suspend,
      process.current_source);
  return true;
}

std::uint64_t Interpreter::Impl::process_handle(
    const ProcessState& process) const {
  const auto encoded_id =
      static_cast<std::uint64_t>(process.program.id) + 1U;
  if (encoded_id > std::numeric_limits<std::uint32_t>::max()
      || process.generation == 0) {
    throw std::overflow_error{"SimIR process handle identity overflow"};
  }
  return (static_cast<std::uint64_t>(process.generation) << 32U)
      | encoded_id;
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::process_from_handle(
    ProcessState& caller,
    const RegisterId source) {
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
  const auto encoded_id =
      static_cast<std::uint32_t>(word.aval);
  const auto generation =
      static_cast<std::uint32_t>(word.aval >> 32U);
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
    const ProcessStatus terminal_status) {
  if (terminal_status != ProcessStatus::finished
      && terminal_status != ProcessStatus::killed) {
    fail(process, "process completion requires a terminal status");
  }
  process.halted = true;
  process.killed = terminal_status == ProcessStatus::killed;
  process.status = terminal_status;
  process.waiting_on_static = false;
  process.waiting_on_signal = false;
  process.waiting_for_children = false;
  process.waiting_fork_group.reset();
  if (process.waiting_process) {
    get_process(*process.waiting_process)
        .process_waiters.erase(process.program.id);
    process.waiting_process.reset();
  }
  remove_dynamic_wait(process);
  clear_wait_timeout(process);

  const auto waiters = std::move(process.process_waiters);
  process.process_waiters.clear();
  for (const auto waiter_id : waiters) {
    auto& waiter = get_process(waiter_id);
    if (waiter.halted
        || waiter.waiting_process != process.program.id) {
      continue;
    }
    waiter.waiting_process.reset();
    waiter.status = ProcessStatus::running;
    queue_active_current(waiter_id);
  }
}

[[nodiscard]] bool Interpreter::Impl::handle_process_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const Operation& operation) {
  if (const auto* self =
          fsim::runtime::simir::operation_get_if<ProcessSelf>(
              &operation)) {
    write_process_register(
        process,
        self->destination,
        PackedLogic4::from_aval_bval(
            64, process_handle(process), 0));
    process.status = ProcessStatus::running;
    return true;
  }
  if (const auto* query =
          fsim::runtime::simir::operation_get_if<ProcessStatusQuery>(
              &operation)) {
    const auto status = process_from_handle(
        process, query->source).status;
    write_process_register(
        process,
        query->destination,
        PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(status), 0));
    process.status = ProcessStatus::running;
    return true;
  }
  if (const auto* query =
          fsim::runtime::simir::operation_get_if<ProcessCompleted>(
              &operation)) {
    const auto status = process_from_handle(
        process, query->source).status;
    const auto completed =
        status == ProcessStatus::finished
        || status == ProcessStatus::killed;
    write_process_register(
        process,
        query->destination,
        PackedLogic4::from_aval_bval(1, completed ? 1U : 0U, 0));
    process.status = ProcessStatus::running;
    return true;
  }
  if (const auto* await =
          fsim::runtime::simir::operation_get_if<ProcessAwait>(
              &operation)) {
    auto& target = process_from_handle(process, await->source);
    if (target.program.id == process.program.id) {
      process.pc = instruction;
      fail(process, "a process cannot await itself");
    }
    clear_wait_timeout(process);
    if (target.status == ProcessStatus::finished
        || target.status == ProcessStatus::killed) {
      process.status = ProcessStatus::running;
      queue_current(process.program.id);
    } else {
      target.process_waiters.insert(process.program.id);
      process.waiting_process = target.program.id;
      process.status = ProcessStatus::waiting;
    }
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return true;
  }
  const auto* kill =
      fsim::runtime::simir::operation_get_if<ProcessKill>(
          &operation);
  if (kill == nullptr) {
    return false;
  }
  auto& target = process_from_handle(process, kill->source);
  clear_wait_timeout(process);
  if (!target.halted) {
    cancel_fork_descendants(target);
    if (target.fork_parent) {
      complete_fork_child(target, ProcessStatus::killed);
    } else {
      complete_process(target, ProcessStatus::killed);
    }
  }
  if (!process.halted) {
    process.status = ProcessStatus::running;
    queue_current(process.program.id);
  }
  notify_execution_point(
      process, instruction, ExecutionPointKind::process_suspend,
      process.current_source);
  return true;
}

void Interpreter::Impl::spawn_fork(
    ProcessState& parent,
    const InstructionIndex instruction,
    const Fork& operation) {
  const auto parent_id = parent.program.id;
  if (!parent.frame) {
    fail(parent, "fork parent has no lexical frame");
  }
  if (instruction + 1 >= parent.program.operations.size()) {
    fail(parent, "fork parent continuation is outside the operation stream");
  }
  for (const auto branch : operation.branches) {
    if (branch >= parent.program.operations.size()) {
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
  if (std::set<InstructionIndex>{
          operation.branches.begin(), operation.branches.end()}
          .size()
      != operation.branches.size()) {
    fail(parent, "fork branch entry is duplicated");
  }
  if (next_fork_group == 0) {
    throw std::overflow_error{"SimIR fork-group identity overflow"};
  }
  const auto group_id = next_fork_group++;
  const auto shared_frame = parent.frame;
  const auto program = parent.program;
  const auto design_process = parent.design_process;
  auto* const executor = parent.executor.get();

  std::set<ProcessId> children;
  for (const auto branch : operation.branches) {
    const auto child_id = static_cast<ProcessId>(processes.size());
    if (static_cast<std::size_t>(child_id) != processes.size()) {
      throw std::length_error{"too many dynamic SimIR processes"};
    }
    ProcessState child;
    if (next_process_generation == 0) {
      throw std::overflow_error{"SimIR process generation overflow"};
    }
    child.generation = next_process_generation++;
    child.program = program;
    child.design_process = design_process;
    child.program.id = child_id;
    child.program.name +=
        ".$fork[" + std::to_string(instruction) + "].child["
        + std::to_string(children.size()) + "]";
    child.program.initialize = false;
    child.program.final = false;
    child.pc = branch;
    child.frame = shared_frame;
    if (executor != nullptr) {
      child.executor = executor->fork_clone(branch);
      if (!child.executor) {
        throw InterpreterError{
            parent_id, instruction, "fork executor clone is null"};
      }
    }
    child.random_state = initial_random_state(root_seed, child_id);
    child.fork_parent = parent_id;
    if (operation.join != ForkJoinKind::none) {
      child.fork_group = group_id;
    }
    processes.push_back(std::move(child));
    children.insert(child_id);
  }

  auto& current_parent = get_process(parent_id);
  current_parent.live_children.insert(children.begin(), children.end());
  if (!children.empty()) {
    auto& site_children =
        current_parent.active_fork_sites[instruction];
    site_children.insert(children.begin(), children.end());
  }
  if (operation.join != ForkJoinKind::none && !children.empty()) {
    fork_groups.emplace(
        group_id,
        ForkGroup{
            parent_id, instruction, operation.join, children, false});
    current_parent.waiting_fork_group = group_id;
    current_parent.status = ProcessStatus::waiting;
  }

  for (const auto child : children) {
    for (const auto sensitivity :
         get_process(child).program.static_sensitivity) {
      static_fanout[sensitivity.signal].push_back(
          {child, sensitivity.edge});
    }
    queue_active_current(child);
  }
  if (operation.join == ForkJoinKind::none || children.empty()) {
    current_parent.status = ProcessStatus::running;
    queue_current(parent_id);
  }
}

void Interpreter::Impl::complete_fork_child(
    ProcessState& child,
    const ProcessStatus status) {
  const auto child_id = child.program.id;
  const auto parent_id = child.fork_parent;
  const auto group_id = child.fork_group;
  complete_process(child, status);
  if (!parent_id) {
    return;
  }

  auto& parent = get_process(*parent_id);
  parent.live_children.erase(child_id);
  for (auto site = parent.active_fork_sites.begin();
       site != parent.active_fork_sites.end();) {
    site->second.erase(child_id);
    if (site->second.empty()) {
      site = parent.active_fork_sites.erase(site);
    } else {
      ++site;
    }
  }
  if (parent.waiting_for_children && parent.live_children.empty()) {
    parent.waiting_for_children = false;
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
  const bool resume_parent =
      group.join == ForkJoinKind::any
      ? !group.parent_resumed
      : group.children.empty();
  if (!resume_parent) {
    return;
  }
  group.parent_resumed = true;
  parent.waiting_fork_group.reset();
  parent.status = ProcessStatus::running;
  queue_active_current(group.parent);
  if (group.join == ForkJoinKind::any) {
    for (const auto remaining : group.children) {
      get_process(remaining).fork_group.reset();
    }
  }
  fork_groups.erase(found);
}

void Interpreter::Impl::cancel_fork_descendants(ProcessState& parent) {
  std::vector<ProcessId> children;
  children.reserve(processes.size());
  for (const auto& candidate : processes) {
    if (candidate.fork_parent == parent.program.id) {
      children.push_back(candidate.program.id);
    }
  }
  for (const auto child_id : children) {
    auto& child = get_process(child_id);
    cancel_fork_descendants(child);
    if (!child.halted) {
      complete_fork_child(child, ProcessStatus::killed);
    }
  }
  parent.live_children.clear();
  parent.active_fork_sites.clear();
  parent.waiting_for_children = false;
  parent.waiting_fork_group.reset();
}

}  // namespace fsim::runtime::simir

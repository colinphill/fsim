// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

[[nodiscard]] bool Interpreter::Impl::handle_fork_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const Operation& operation) {
  if (const auto* fork = std::get_if<Fork>(&operation)) {
    clear_wait_timeout(process);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    spawn_fork(process, instruction, *fork);
    return true;
  }
  if (std::holds_alternative<ForkEnd>(operation)) {
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
  if (std::holds_alternative<WaitFork>(operation)) {
    clear_wait_timeout(process);
    if (process.live_children.empty()) {
      queue_current(process.program.id);
    } else {
      process.waiting_for_children = true;
    }
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return true;
  }
  if (!std::holds_alternative<DisableFork>(operation)) {
    return false;
  }
  clear_wait_timeout(process);
  cancel_fork_descendants(process);
  queue_current(process.program.id);
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
  if (parent.active_fork_sites.contains(instruction)) {
    fail(
        parent,
        "a bounded fork site cannot be re-entered while one of its "
        "children is still active");
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
  auto* const executor = parent.executor.get();

  std::set<ProcessId> children;
  for (const auto branch : operation.branches) {
    const auto child_id = static_cast<ProcessId>(processes.size());
    if (static_cast<std::size_t>(child_id) != processes.size()) {
      throw std::length_error{"too many dynamic SimIR processes"};
    }
    ProcessState child;
    child.program = program;
    child.design_process = parent.design_process;
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
    current_parent.active_fork_sites.emplace(instruction, children);
  }
  if (operation.join != ForkJoinKind::none && !children.empty()) {
    fork_groups.emplace(
        group_id,
        ForkGroup{
            parent_id, instruction, operation.join, children, false});
    current_parent.waiting_fork_group = group_id;
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
    queue_current(parent_id);
  }
}

void Interpreter::Impl::complete_fork_child(ProcessState& child) {
  const auto child_id = child.program.id;
  const auto parent_id = child.fork_parent;
  const auto group_id = child.fork_group;
  child.halted = true;
  child.waiting_on_static = false;
  child.waiting_on_signal = false;
  remove_dynamic_wait(child);
  clear_wait_timeout(child);
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
      complete_fork_child(child);
    }
  }
  parent.live_children.clear();
  parent.active_fork_sites.clear();
  parent.waiting_for_children = false;
  parent.waiting_fork_group.reset();
}

}  // namespace fsim::runtime::simir

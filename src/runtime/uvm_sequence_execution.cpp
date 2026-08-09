// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_sequence.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <ranges>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidLifecycle{"FSIM-UVM-SEQ-005"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-SEQ-006"};
constexpr std::string_view kCallbackFailure{"FSIM-UVM-SEQ-007"};
constexpr std::string_view kInvalidResponsePolicy{"FSIM-UVM-SEQ-009"};

[[noreturn]] void fail(const std::string_view code,
                       const std::string_view message) {
  throw SystemVerilogUvmSequenceError{std::string{code}, std::string{message}};
}

[[nodiscard]] std::string current_exception_message() {
  try {
    throw;
  } catch (const std::exception &error) {
    return error.what();
  } catch (...) {
    return "unknown exception from UVM sequence callback";
  }
}

} // namespace

void SystemVerilogUvmSequenceService::set_execution_state(
    Sequence &value, const SystemVerilogUvmSequenceState state,
    const SystemVerilogUvmSequenceCallbackKind callback,
    SystemVerilogUvmSequenceExecutionResult &result) {
  if (execution_events_.size() >= limits_.maximum_execution_events ||
      value.reserved_events == 0 ||
      next_execution_order_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM sequence execution-event ceiling exceeded");
  }
  value.value.state = state;
  --value.reserved_events;
  --reserved_execution_events_;
  SystemVerilogUvmSequenceExecutionEvent event;
  event.sequence = value.value.handle;
  event.state = state;
  event.callback = callback;
  event.execution = value.value.execution_count;
  event.order = next_execution_order_;
  result.events.push_back(event);
  execution_events_.push_back(event);
  ++next_execution_order_;
}

void SystemVerilogUvmSequenceService::record_execution_failure(
    Sequence &value, const SystemVerilogUvmSequenceCallbackKind callback,
    std::string message, SystemVerilogUvmSequenceExecutionResult &result,
    const bool stop) {
  if (execution_failures_.size() >= limits_.maximum_execution_failures) {
    value.stop_requested = value.stop_requested || stop;
    return;
  }
  SystemVerilogUvmSequenceExecutionFailure failure;
  failure.diagnostic_code = std::string{kCallbackFailure};
  failure.sequence = value.value.handle;
  failure.callback = callback;
  failure.message = std::move(message);
  result.failures.push_back(failure);
  execution_failures_.push_back(std::move(failure));
  value.stop_requested = value.stop_requested || stop;
}

bool SystemVerilogUvmSequenceService::invoke_sequence_hook(
    Sequence &value, const SystemVerilogUvmSequenceState state,
    const SystemVerilogUvmSequenceCallbackKind callback,
    const SystemVerilogUvmSequenceHooks::Hook &hook,
    SystemVerilogUvmSequenceExecutionResult &result) {
  set_execution_state(value, state, callback, result);
  if (hook) {
    try {
      hook(value.value.handle);
    } catch (...) {
      record_execution_failure(value, callback, current_exception_message(),
                               result);
    }
  }
  return !value.stop_requested && !value.kill_requested;
}

SystemVerilogUvmSequenceExecutionResult SystemVerilogUvmSequenceService::start(
    const SystemVerilogUvmSequenceHandle handle,
    SystemVerilogUvmSequenceStartOptions options) {
  auto &selected = sequence(handle);
  if (selected.active ||
      (selected.value.state != SystemVerilogUvmSequenceState::Created &&
       selected.value.state != SystemVerilogUvmSequenceState::Stopped &&
       selected.value.state != SystemVerilogUvmSequenceState::Finished)) {
    fail(kInvalidLifecycle, "UVM sequence cannot start in its current state");
  }
  if (active_executions_ >= limits_.maximum_active_executions ||
      execution_events_.size() > limits_.maximum_execution_events ||
      reserved_execution_events_ > limits_.maximum_execution_events ||
      limits_.maximum_execution_events - execution_events_.size() <
          reserved_execution_events_ ||
      limits_.maximum_execution_events - execution_events_.size() -
              reserved_execution_events_ <
          7U ||
      execution_failures_.size() >= limits_.maximum_execution_failures ||
      next_execution_order_ > std::numeric_limits<std::uint64_t>::max() - 7U ||
      selected.value.execution_count ==
          std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM sequence execution ceiling exceeded");
  }

  Sequence *parent{};
  if (selected.value.parent) {
    parent = &sequence(selected.value.parent);
    if (!parent->active ||
        parent->value.state != SystemVerilogUvmSequenceState::Body) {
      fail(kInvalidLifecycle,
           "UVM child sequence must start from its active parent body");
    }
    if (!options.parent_process && parent->value.process) {
      options.parent_process = parent->value.process;
    }
    if (!options.phase && parent->value.phase) {
      options.phase = parent->value.phase;
    }
  }

  std::optional<SystemVerilogUvmPhaseProcessSnapshot> parent_process;
  if (options.parent_process) {
    if (!phases_) {
      fail(kInvalidLifecycle, "UVM sequence phase service is unavailable");
    }
    try {
      parent_process = phases_->process_snapshot(options.parent_process);
    } catch (...) {
      fail(kInvalidLifecycle, "UVM sequence parent phase process is invalid");
    }
    if (!options.phase)
      options.phase = parent_process->phase;
    if (options.phase != parent_process->phase ||
        parent_process->state != SystemVerilogUvmPhaseProcessState::Running ||
        (selected.value.root != 0 &&
         selected.value.root != parent_process->root)) {
      fail(kInvalidLifecycle,
           "UVM sequence phase-process ownership is inconsistent");
    }
  }

  if (options.phase) {
    if (!phases_) {
      fail(kInvalidLifecycle, "UVM sequence phase service is unavailable");
    }
    SystemVerilogUvmPhaseSnapshot phase;
    try {
      phase = phases_->snapshot(options.phase);
    } catch (...) {
      fail(kInvalidLifecycle, "UVM sequence phase handle is invalid");
    }
    if (phase.execution != SystemVerilogUvmPhaseExecutionKind::Task ||
        phase.state != SystemVerilogUvmPhaseState::Executing ||
        (selected.value.root != 0 &&
         std::ranges::find(phase.roots, selected.value.root) ==
             phase.roots.end())) {
      fail(kInvalidLifecycle, "UVM sequence phase is not executable here");
    }
  }
  if (options.automatic_phase_objection &&
      (!objections_ || !options.phase || selected.value.root == 0)) {
    fail(kInvalidLifecycle,
         "automatic UVM sequence objection lacks phase/root ownership");
  }

  SystemVerilogUvmPhaseProcessHandle owned_process;
  if (parent_process) {
    try {
      owned_process = phases_->begin_child_process(options.parent_process);
    } catch (...) {
      fail(kInvalidLifecycle, "UVM sequence phase process cannot begin");
    }
  }

  std::optional<SystemVerilogUvmObjectionSourceHandle> objection_source;
  std::vector<std::string> objection_failures;
  bool objection_raised{};
  if (options.automatic_phase_objection) {
    try {
      objection_source =
          objections_->bind_source(selected.value.object, selected.value.root);
      const auto raised = objections_->raise(options.phase, *objection_source,
                                             "automatic sequence objection: " +
                                                 selected.value.full_name);
      objection_raised = true;
      for (const auto &failure : raised.failures) {
        objection_failures.push_back(failure.message);
      }
    } catch (...) {
      if (owned_process && phases_->contains(owned_process)) {
        phases_->cancel_task_process(owned_process);
      }
      fail(kInvalidLifecycle, "automatic UVM sequence objection cannot raise");
    }
  }

  selected.active = true;
  selected.stop_requested = false;
  selected.kill_requested = false;
  selected.value.phase = options.phase;
  selected.value.process = owned_process;
  selected.reserved_events = 7;
  reserved_execution_events_ += 7U;
  ++selected.value.execution_count;
  ++active_executions_;
  publish_activity(SystemVerilogUvmActivityAction::Started,
                   selected.value.debug_name, selected.value.root,
                   selected.value.execution_count, "execution");

  SystemVerilogUvmSequenceExecutionResult result;
  result.sequence = handle;
  for (auto &message : objection_failures) {
    record_execution_failure(
        selected, SystemVerilogUvmSequenceCallbackKind::StateTransition,
        std::move(message), result, false);
  }
  auto continue_execution =
      invoke_sequence_hook(selected, SystemVerilogUvmSequenceState::PreStart,
                           SystemVerilogUvmSequenceCallbackKind::PreStart,
                           selected.hooks.pre_start, result);
  if (continue_execution && options.call_pre_post) {
    continue_execution =
        invoke_sequence_hook(selected, SystemVerilogUvmSequenceState::PreBody,
                             SystemVerilogUvmSequenceCallbackKind::PreBody,
                             selected.hooks.pre_body, result);
  }
  if (continue_execution) {
    continue_execution =
        invoke_sequence_hook(selected, SystemVerilogUvmSequenceState::Body,
                             SystemVerilogUvmSequenceCallbackKind::Body,
                             selected.hooks.body, result);
  }
  if (continue_execution && options.call_pre_post) {
    continue_execution =
        invoke_sequence_hook(selected, SystemVerilogUvmSequenceState::PostBody,
                             SystemVerilogUvmSequenceCallbackKind::PostBody,
                             selected.hooks.post_body, result);
  }
  if (continue_execution) {
    continue_execution =
        invoke_sequence_hook(selected, SystemVerilogUvmSequenceState::PostStart,
                             SystemVerilogUvmSequenceCallbackKind::PostStart,
                             selected.hooks.post_start, result);
  }

  if (continue_execution) {
    set_execution_state(selected, SystemVerilogUvmSequenceState::Ended,
                        SystemVerilogUvmSequenceCallbackKind::StateTransition,
                        result);
    set_execution_state(selected, SystemVerilogUvmSequenceState::Finished,
                        SystemVerilogUvmSequenceCallbackKind::StateTransition,
                        result);
  } else {
    set_execution_state(selected, SystemVerilogUvmSequenceState::Stopped,
                        SystemVerilogUvmSequenceCallbackKind::StateTransition,
                        result);
  }

  if (objection_raised) {
    try {
      const auto dropped = objections_->drop(options.phase, *objection_source,
                                             "automatic sequence objection: " +
                                                 selected.value.full_name);
      for (const auto &failure : dropped.failures) {
        record_execution_failure(
            selected, SystemVerilogUvmSequenceCallbackKind::StateTransition,
            failure.message, result, false);
      }
    } catch (...) {
      record_execution_failure(
          selected, SystemVerilogUvmSequenceCallbackKind::StateTransition,
          current_exception_message(), result, false);
    }
  }

  if (owned_process && phases_->contains(owned_process)) {
    const auto process = phases_->process_snapshot(owned_process);
    if (process.state == SystemVerilogUvmPhaseProcessState::Running) {
      try {
        if (selected.kill_requested)
          phases_->cancel_task_process(owned_process);
        else
          phases_->complete_task_process(owned_process);
      } catch (...) {
        if (phases_->process_snapshot(owned_process).state ==
            SystemVerilogUvmPhaseProcessState::Running) {
          phases_->cancel_task_process(owned_process);
        }
        record_execution_failure(
            selected, SystemVerilogUvmSequenceCallbackKind::StateTransition,
            current_exception_message(), result, false);
      }
    }
  }

  result.final_state = selected.value.state;
  result.stopped = selected.stop_requested;
  result.killed = selected.kill_requested;
  reserved_execution_events_ -= selected.reserved_events;
  selected.reserved_events = 0;
  selected.active = false;
  --active_executions_;
  publish_activity(result.success() ? SystemVerilogUvmActivityAction::Completed
                                    : SystemVerilogUvmActivityAction::Failed,
                   selected.value.debug_name, selected.value.root,
                   selected.value.execution_count,
                   std::to_string(static_cast<unsigned>(result.final_state)));
  return result;
}

void SystemVerilogUvmSequenceService::request_stop_tree(Sequence &value,
                                                        const bool killed) {
  const auto virtual_children = value.active_virtual_children;
  for (const auto &child_handle : virtual_children) {
    if (!contains(child_handle))
      continue;
    request_stop_tree(sequence(child_handle), killed);
  }
  for (const auto &child_handle : value.value.children) {
    if (!contains(child_handle))
      continue;
    auto &child = sequence(child_handle);
    request_stop_tree(child, killed);
  }
  cancel_sequence_accesses(value);
  cancel_sequence_transactions(
      value, killed
                 ? SystemVerilogUvmSequenceCancellationReason::SequenceKilled
                 : SystemVerilogUvmSequenceCancellationReason::SequenceStopped);
  if (!value.active)
    return;
  value.stop_requested = true;
  value.kill_requested = value.kill_requested || killed;
  if (killed && value.value.process && phases_ &&
      phases_->contains(value.value.process)) {
    const auto process = phases_->process_snapshot(value.value.process);
    if (process.state == SystemVerilogUvmPhaseProcessState::Running) {
      phases_->cancel_task_process(value.value.process);
    }
  }
}

void SystemVerilogUvmSequenceService::request_stop(
    const SystemVerilogUvmSequenceHandle handle) {
  auto &selected = sequence(handle);
  if (!selected.active) {
    fail(kInvalidLifecycle, "only an active UVM sequence can be stopped");
  }
  request_stop_tree(selected, false);
}

void SystemVerilogUvmSequenceService::kill(
    const SystemVerilogUvmSequenceHandle handle) {
  auto &selected = sequence(handle);
  if (!selected.active) {
    fail(kInvalidLifecycle, "only an active UVM sequence can be killed");
  }
  request_stop_tree(selected, true);
}

void SystemVerilogUvmSequenceService::configure_response_queue(
    const SystemVerilogUvmSequenceHandle handle, const std::size_t depth,
    const SystemVerilogUvmResponseOverflowPolicy policy,
    const bool error_on_overflow) {
  switch (policy) {
  case SystemVerilogUvmResponseOverflowPolicy::Error:
  case SystemVerilogUvmResponseOverflowPolicy::DropOldest:
  case SystemVerilogUvmResponseOverflowPolicy::DropNewest:
    break;
  default:
    fail(kInvalidResponsePolicy, "unknown UVM response overflow policy");
  }
  auto &selected = sequence(handle);
  if (depth == 0 || depth > limits_.maximum_responses_per_sequence) {
    fail(kInvalidResponsePolicy, "UVM response queue depth is invalid");
  }
  if (selected.value.responses.size() > depth) {
    fail(kInvalidResponsePolicy,
         "UVM response queue cannot shrink below retained responses");
  }
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM response policy mutation ceiling exceeded");
  }
  selected.value.response_queue_depth = depth;
  selected.value.response_overflow_policy = policy;
  selected.value.response_overflow_error = error_on_overflow;
  ++mutations_;
}

SystemVerilogUvmResponseRouteResult
SystemVerilogUvmSequenceService::route_response(
    const SystemVerilogUvmSequenceItemHandle handle) {
  auto &response = item(handle);
  if (response.value.role != SystemVerilogUvmSequenceItemRole::Response ||
      !response.value.owner_sequence) {
    fail(kInvalidLifecycle, "UVM response has no owning sequence");
  }
  auto &owner = sequence(response.value.owner_sequence);
  if (std::ranges::find(owner.value.responses, handle) !=
      owner.value.responses.end()) {
    fail(kInvalidLifecycle, "UVM response is already routed");
  }
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM sequence response queue ceiling exceeded");
  }
  if (owner.value.responses.size() >= owner.value.response_queue_depth) {
    if (owner.value.response_overflow_policy ==
            SystemVerilogUvmResponseOverflowPolicy::Error &&
        owner.value.response_overflow_error) {
      fail(kInvalidResponsePolicy, "UVM sequence response queue overflowed");
    }
    if (owner.value.response_overflow_policy !=
        SystemVerilogUvmResponseOverflowPolicy::DropOldest) {
      ++mutations_;
      return {SystemVerilogUvmResponseRouteStatus::DroppedNewest,
              std::optional<SystemVerilogUvmSequenceItemHandle>{handle}};
    }
    auto updated = owner.value.responses;
    const auto dropped = updated.front();
    updated.erase(updated.begin());
    updated.push_back(handle);
    auto &dropped_item = item(dropped);
    owner.value.responses.swap(updated);
    dropped_item.value.state = SystemVerilogUvmSequenceItemState::Owned;
    response.value.state = SystemVerilogUvmSequenceItemState::Routed;
    ++mutations_;
    return {SystemVerilogUvmResponseRouteStatus::DroppedOldest,
            std::optional<SystemVerilogUvmSequenceItemHandle>{dropped}};
  }
  auto updated = owner.value.responses;
  updated.push_back(handle);
  owner.value.responses.swap(updated);
  response.value.state = SystemVerilogUvmSequenceItemState::Routed;
  ++mutations_;
  return {SystemVerilogUvmResponseRouteStatus::Queued, std::nullopt};
}

std::optional<SystemVerilogUvmSequenceItemHandle>
SystemVerilogUvmSequenceService::pop_response(
    const SystemVerilogUvmSequenceHandle handle) {
  auto &owner = sequence(handle);
  if (owner.value.responses.empty())
    return std::nullopt;
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM sequence response mutation ceiling exceeded");
  }
  auto updated = owner.value.responses;
  const auto response = updated.front();
  updated.erase(updated.begin());
  auto &selected = item(response);
  owner.value.responses.swap(updated);
  selected.value.state = SystemVerilogUvmSequenceItemState::Owned;
  ++mutations_;
  return response;
}

std::vector<SystemVerilogUvmSequenceItemHandle>
SystemVerilogUvmSequenceService::responses(
    const SystemVerilogUvmSequenceHandle handle) const {
  return sequence(handle).value.responses;
}

} // namespace fsim::runtime

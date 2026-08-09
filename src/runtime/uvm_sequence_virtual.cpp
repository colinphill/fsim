// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_sequence.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidVirtual{"FSIM-UVM-SEQ-013"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-SEQ-006"};

[[noreturn]] void fail(const std::string_view code,
                       const std::string_view message) {
  throw SystemVerilogUvmSequenceError{std::string{code}, std::string{message}};
}

[[nodiscard]] bool valid_domain_name(const std::string_view name) {
  if (name.empty())
    return false;
  return std::ranges::none_of(name, [](const unsigned char character) {
    return character == '.' || character == '/' || character <= ' ';
  });
}

} // namespace

SystemVerilogUvmSequencerHandle
SystemVerilogUvmSequenceService::register_virtual_sequencer(
    SystemVerilogUvmVirtualSequencerDescriptor descriptor) {
  if (descriptor.domains.empty()) {
    fail(kInvalidVirtual, "virtual UVM sequencer has no child domains");
  }
  if (descriptor.domains.size() >
      limits_.maximum_virtual_domains_per_sequencer) {
    fail(kResourceLimit, "virtual UVM sequencer domain ceiling exceeded");
  }
  if (descriptor.component == 0 ||
      !components_->contains(descriptor.component)) {
    fail(kInvalidVirtual, "virtual UVM sequencer component is empty or stale");
  }
  const auto component = components_->snapshot(descriptor.component);
  std::set<std::string> domain_names;
  std::set<std::uint64_t> child_slots;
  for (const auto &domain : descriptor.domains) {
    if (!valid_domain_name(domain.name)) {
      fail(kInvalidVirtual, "virtual UVM sequencer domain name is invalid");
    }
    if (domain.name.size() > limits_.maximum_name_bytes) {
      fail(kResourceLimit,
           "virtual UVM sequencer domain name exceeds its byte ceiling");
    }
    auto &child = sequencer(domain.sequencer);
    if (child.value.is_virtual || child.value.parent_virtual) {
      fail(kInvalidVirtual, "virtual UVM child sequencer is already owned");
    }
    if (child.value.root != component.root) {
      fail(kInvalidVirtual, "virtual UVM child sequencer crosses roots");
    }
    if (domain.profile != child.value.profile) {
      fail(kInvalidVirtual, "virtual UVM child-domain profile does not match");
    }
    if (!domain_names.insert(domain.name).second ||
        !child_slots.insert(domain.sequencer.slot_).second) {
      fail(kInvalidVirtual, "virtual UVM child domain is duplicated");
    }
  }

  SystemVerilogUvmSequencerDescriptor base;
  base.component = descriptor.component;
  base.nominal_type = std::move(descriptor.nominal_type);
  base.profile = std::move(descriptor.profile);
  const auto handle = register_sequencer(std::move(base));
  auto &created = sequencer(handle);
  created.value.is_virtual = true;
  created.value.virtual_domains = std::move(descriptor.domains);
  for (const auto &domain : created.value.virtual_domains) {
    sequencer(domain.sequencer).value.parent_virtual = handle;
  }
  return handle;
}

SystemVerilogUvmSequenceHandle
SystemVerilogUvmSequenceService::register_virtual_sequence(
    SystemVerilogUvmSequenceDescriptor descriptor) {
  auto virtual_sequencer = descriptor.sequencer;
  if (!virtual_sequencer && descriptor.parent) {
    virtual_sequencer = sequence(descriptor.parent).value.sequencer;
  }
  if (!virtual_sequencer || !sequencer(virtual_sequencer).value.is_virtual) {
    fail(kInvalidVirtual, "virtual UVM sequence requires a virtual sequencer");
  }
  if (descriptor.parent && !sequence(descriptor.parent).value.is_virtual) {
    fail(kInvalidVirtual, "virtual UVM sequence parent is not virtual");
  }
  descriptor.sequencer = virtual_sequencer;
  const auto handle = register_sequence(std::move(descriptor));
  sequence(handle).value.is_virtual = true;
  return handle;
}

void SystemVerilogUvmSequenceService::append_virtual_event(
    Sequence &selected, SystemVerilogUvmVirtualSequenceResult &result,
    const SystemVerilogUvmVirtualSequenceEventKind kind,
    const std::uint64_t epoch, const SystemVerilogUvmVirtualSequenceStep *step,
    const SystemVerilogUvmSequenceRequestHandle request_handle,
    const SystemVerilogUvmSequenceAccessHandle access_handle) {
  if (selected.reserved_virtual_events == 0 ||
      virtual_events_.size() >= limits_.maximum_virtual_events ||
      next_virtual_event_order_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "virtual UVM sequence event ceiling exceeded");
  }
  SystemVerilogUvmVirtualSequenceEvent event;
  event.virtual_sequence = selected.value.handle;
  event.kind = kind;
  event.epoch = epoch;
  event.order = next_virtual_event_order_;
  event.request = request_handle;
  event.access = access_handle;
  if (step) {
    event.child_sequence = step->sequence;
    event.child_sequencer = sequence(step->sequence).value.sequencer;
    event.domain = step->domain;
  }
  --selected.reserved_virtual_events;
  --reserved_virtual_events_;
  ++next_virtual_event_order_;
  result.events.push_back(event);
  virtual_events_.push_back(std::move(event));
}

void SystemVerilogUvmSequenceService::cancel_virtual_requests(
    const SystemVerilogUvmSequenceHandle sequence_handle) {
  std::vector<SystemVerilogUvmSequenceRequestHandle> selected;
  for (const auto &[slot, request] : requests_) {
    if (request.live && request.value.sequence == sequence_handle) {
      selected.push_back(SystemVerilogUvmSequenceRequestHandle{
          owner_, slot, request.generation});
    }
  }
  for (const auto &handle : selected) {
    auto &request = requests_.at(handle.slot_);
    auto &owner = sequencer(request.value.sequencer);
    std::erase(owner.value.requests, handle);
    request.live = false;
    if (request.generation != std::numeric_limits<std::uint64_t>::max()) {
      ++request.generation;
    }
    --live_requests_;
    if (mutations_ < limits_.maximum_mutations)
      ++mutations_;
  }
}

void SystemVerilogUvmSequenceService::restore_virtual_responses(
    const std::vector<
        std::pair<SystemVerilogUvmSequenceHandle,
                  std::vector<SystemVerilogUvmSequenceItemHandle>>> &baseline) {
  for (const auto &[handle, original] : baseline) {
    auto &selected = sequence(handle);
    for (const auto &response : selected.value.responses) {
      if (std::ranges::find(original, response) == original.end() &&
          contains(response)) {
        item(response).value.state = SystemVerilogUvmSequenceItemState::Owned;
      }
    }
    if (selected.value.responses != original) {
      selected.value.responses = original;
      if (mutations_ < limits_.maximum_mutations)
        ++mutations_;
    }
  }
}

void SystemVerilogUvmSequenceService::request_virtual_reset(
    const SystemVerilogUvmSequenceHandle handle, const bool restart) {
  auto &selected = sequence(handle);
  if (!selected.value.is_virtual || !selected.active ||
      selected.value.state != SystemVerilogUvmSequenceState::Body ||
      selected.reserved_virtual_events == 0) {
    fail(kInvalidVirtual,
         "virtual UVM reset requires active coordinated body execution");
  }
  selected.virtual_reset_requested = true;
  selected.virtual_restart_requested = restart;
  for (const auto &child_handle : selected.active_virtual_children) {
    auto &child = sequence(child_handle);
    cancel_sequence_transactions(
        child, SystemVerilogUvmSequenceCancellationReason::VirtualReset);
    request_stop_tree(child, true);
  }
}

SystemVerilogUvmVirtualSequenceResult
SystemVerilogUvmSequenceService::coordinate_virtual(
    const SystemVerilogUvmSequenceHandle handle,
    const std::span<const SystemVerilogUvmVirtualSequenceStep> steps,
    const SystemVerilogUvmVirtualSequenceOptions options) {
  auto &selected = sequence(handle);
  if (!selected.value.is_virtual || !selected.active ||
      selected.value.state != SystemVerilogUvmSequenceState::Body) {
    fail(kInvalidVirtual,
         "virtual UVM coordination requires an active virtual-sequence body");
  }
  if (!selected.value.phase || !selected.value.process || !phases_ ||
      !phases_->contains(selected.value.process)) {
    fail(kInvalidVirtual,
         "virtual UVM coordination requires a live phase process");
  }
  if (steps.empty()) {
    fail(kInvalidVirtual, "virtual UVM coordination has no child steps");
  }
  if (steps.size() > limits_.maximum_virtual_steps ||
      selected.value.virtual_run_count ==
          std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "virtual UVM coordination ceiling exceeded");
  }
  auto &virtual_sequencer = sequencer(selected.value.sequencer);
  if (!virtual_sequencer.value.is_virtual) {
    fail(kInvalidVirtual, "virtual UVM sequence lost its virtual sequencer");
  }

  std::set<std::string> used_domains;
  std::set<std::uint64_t> used_sequences;
  std::vector<std::pair<SystemVerilogUvmSequenceHandle,
                        std::vector<SystemVerilogUvmSequenceItemHandle>>>
      response_baseline;
  response_baseline.reserve(steps.size());
  for (const auto &step : steps) {
    if (step.priority == 0 || step.priority > limits_.maximum_priority) {
      fail(kInvalidVirtual, "virtual UVM child priority is invalid");
    }
    const auto domain = std::ranges::find_if(
        virtual_sequencer.value.virtual_domains,
        [&](const auto &candidate) { return candidate.name == step.domain; });
    if (domain == virtual_sequencer.value.virtual_domains.end()) {
      fail(kInvalidVirtual, "virtual UVM child domain is not bound");
    }
    auto &child = sequence(step.sequence);
    if (child.value.is_virtual || child.active ||
        child.value.sequencer != domain->sequencer ||
        child.value.profile != domain->profile ||
        child.value.root != selected.value.root) {
      fail(kInvalidVirtual,
           "virtual UVM child sequence does not match its typed domain");
    }
    if (child.value.state != SystemVerilogUvmSequenceState::Created &&
        child.value.state != SystemVerilogUvmSequenceState::Stopped &&
        child.value.state != SystemVerilogUvmSequenceState::Finished) {
      fail(kInvalidVirtual, "virtual UVM child cannot restart in this state");
    }
    if (!used_domains.insert(step.domain).second ||
        !used_sequences.insert(step.sequence.slot_).second) {
      fail(kInvalidVirtual, "virtual UVM coordination step is duplicated");
    }
    if (step.access != SystemVerilogUvmVirtualSequenceAccess::None) {
      const auto &child_sequencer = sequencer(domain->sequencer);
      if (!child_sequencer.value.access_queue.empty() ||
          !child_sequencer.value.access_stack.empty()) {
        fail(kInvalidVirtual,
             "virtual UVM child domain is blocked by sequencer access");
      }
    }
    response_baseline.emplace_back(step.sequence, child.value.responses);
  }

  SystemVerilogUvmVirtualSequenceResult result;
  result.sequence = handle;
  ++selected.value.virtual_run_count;
  std::uint64_t epoch{};
  const auto release_reservation = [&] {
    reserved_virtual_events_ -= selected.reserved_virtual_events;
    selected.reserved_virtual_events = 0;
  };
  const auto reserve_epoch = [&] {
    const auto required = steps.size() * 8U + 4U;
    if (required > limits_.maximum_virtual_events ||
        virtual_events_.size() > limits_.maximum_virtual_events ||
        reserved_virtual_events_ > limits_.maximum_virtual_events ||
        limits_.maximum_virtual_events - virtual_events_.size() <
            reserved_virtual_events_ ||
        limits_.maximum_virtual_events - virtual_events_.size() -
                reserved_virtual_events_ <
            required ||
        next_virtual_event_order_ >
            std::numeric_limits<std::uint64_t>::max() - required) {
      fail(kResourceLimit, "virtual UVM sequence event ceiling exceeded");
    }
    selected.reserved_virtual_events = required;
    reserved_virtual_events_ += required;
  };
  const auto cleanup_epoch = [&](const bool rollback_responses) {
    for (const auto &step : steps) {
      cancel_virtual_requests(step.sequence);
      auto &child = sequence(step.sequence);
      try {
        cancel_sequence_accesses(child);
      } catch (...) {
      }
      try {
        cancel_sequence_transactions(
            child, SystemVerilogUvmSequenceCancellationReason::VirtualReset);
      } catch (...) {
      }
    }
    if (rollback_responses)
      restore_virtual_responses(response_baseline);
    selected.active_virtual_children.clear();
  };

  try {
    for (;;) {
      reserve_epoch();
      ++epoch;
      result.epochs = epoch;
      append_virtual_event(
          selected, result,
          epoch == 1
              ? SystemVerilogUvmVirtualSequenceEventKind::CoordinatedStart
              : SystemVerilogUvmVirtualSequenceEventKind::Restarted,
          epoch);
      selected.virtual_reset_requested = false;
      selected.virtual_restart_requested = false;

      for (const auto &step : steps) {
        SystemVerilogUvmSequenceAccessHandle access_handle;
        if (step.access == SystemVerilogUvmVirtualSequenceAccess::Lock) {
          access_handle = request_lock(step.sequence);
        } else if (step.access == SystemVerilogUvmVirtualSequenceAccess::Grab) {
          access_handle = request_grab(step.sequence);
        }
        if (access_handle) {
          if (access_snapshot(access_handle).state !=
              SystemVerilogUvmSequenceAccessState::Granted) {
            cancel_access(access_handle);
            fail(kInvalidVirtual, "virtual UVM child access was not granted");
          }
          append_virtual_event(
              selected, result,
              SystemVerilogUvmVirtualSequenceEventKind::AccessAcquired, epoch,
              &step, {}, access_handle);
        }

        const auto request_handle = macro_send(step.sequence, step.priority);
        append_virtual_event(
            selected, result,
            SystemVerilogUvmVirtualSequenceEventKind::RequestQueued, epoch,
            &step, request_handle, access_handle);
        selected.active_virtual_children.push_back(step.sequence);
        append_virtual_event(
            selected, result,
            SystemVerilogUvmVirtualSequenceEventKind::ChildStarted, epoch,
            &step, request_handle, access_handle);

        SystemVerilogUvmSequenceStartOptions child_options;
        child_options.phase = selected.value.phase;
        child_options.parent_process = selected.value.process;
        child_options.call_pre_post = step.call_pre_post;
        child_options.automatic_phase_objection =
            step.automatic_phase_objection;
        auto execution = start(step.sequence, child_options);
        std::erase(selected.active_virtual_children, step.sequence);

        SystemVerilogUvmVirtualSequenceChildResult child_result;
        child_result.domain = step.domain;
        child_result.sequence = step.sequence;
        child_result.sequencer = sequence(step.sequence).value.sequencer;
        child_result.request = request_handle;
        child_result.access = access_handle;
        child_result.priority = step.priority;
        child_result.epoch = epoch;
        child_result.execution = std::move(execution);
        const auto child_success = child_result.execution.success();
        result.children.push_back(std::move(child_result));
        append_virtual_event(
            selected, result,
            SystemVerilogUvmVirtualSequenceEventKind::ChildCompleted, epoch,
            &step, request_handle, access_handle);

        const auto request_slot = requests_.find(request_handle.slot_);
        if (request_slot != requests_.end() && request_slot->second.live) {
          cancel_request(request_handle);
          append_virtual_event(
              selected, result,
              SystemVerilogUvmVirtualSequenceEventKind::RequestCancelled, epoch,
              &step, request_handle, access_handle);
        }
        const auto access_slot = accesses_.find(access_handle.slot_);
        if (access_handle && access_slot != accesses_.end() &&
            access_slot->second.live) {
          if (step.access == SystemVerilogUvmVirtualSequenceAccess::Lock) {
            unlock(step.sequence);
          } else {
            ungrab(step.sequence);
          }
          append_virtual_event(
              selected, result,
              SystemVerilogUvmVirtualSequenceEventKind::AccessReleased, epoch,
              &step, request_handle, access_handle);
        }

        if (selected.virtual_reset_requested || selected.stop_requested ||
            selected.kill_requested ||
            (options.stop_on_child_failure && !child_success)) {
          if (!selected.virtual_reset_requested && !selected.stop_requested &&
              !selected.kill_requested) {
            selected.stop_requested = true;
          }
          break;
        }
        const auto process = phases_->process_snapshot(selected.value.process);
        if (process.state != SystemVerilogUvmPhaseProcessState::Running) {
          selected.stop_requested = true;
          selected.kill_requested = true;
          break;
        }
      }

      if (selected.virtual_reset_requested) {
        append_virtual_event(
            selected, result,
            SystemVerilogUvmVirtualSequenceEventKind::ResetRequested, epoch);
        cleanup_epoch(true);
        result.reset = true;
        const auto restart =
            options.restart_on_reset && selected.virtual_restart_requested;
        if (restart) {
          if (result.restarts >= limits_.maximum_virtual_restarts ||
              selected.value.virtual_restart_count ==
                  std::numeric_limits<std::uint64_t>::max()) {
            fail(kResourceLimit, "virtual UVM restart ceiling exceeded");
          }
          ++result.restarts;
          ++selected.value.virtual_restart_count;
          result.reset = false;
          release_reservation();
          continue;
        }
        append_virtual_event(selected, result,
                             SystemVerilogUvmVirtualSequenceEventKind::Stopped,
                             epoch);
        selected.stop_requested = true;
        result.stopped = true;
        release_reservation();
        break;
      }
      if (selected.stop_requested || selected.kill_requested) {
        cleanup_epoch(true);
        append_virtual_event(
            selected, result,
            selected.kill_requested
                ? SystemVerilogUvmVirtualSequenceEventKind::Killed
                : SystemVerilogUvmVirtualSequenceEventKind::Stopped,
            epoch);
        result.stopped = selected.stop_requested;
        result.killed = selected.kill_requested;
        release_reservation();
        break;
      }

      append_virtual_event(selected, result,
                           SystemVerilogUvmVirtualSequenceEventKind::Completed,
                           epoch);
      result.completed = true;
      release_reservation();
      break;
    }
  } catch (...) {
    cleanup_epoch(true);
    release_reservation();
    throw;
  }
  return result;
}

} // namespace fsim::runtime

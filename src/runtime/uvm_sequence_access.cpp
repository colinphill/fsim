// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_sequence.hpp"

#include <algorithm>
#include <limits>
#include <ranges>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-SEQ-001"};
constexpr std::string_view kForeignHandle{"FSIM-UVM-SEQ-002"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-SEQ-006"};
constexpr std::string_view kInvalidAccess{"FSIM-UVM-SEQ-010"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmSequenceError{
      std::string{code}, std::string{message}};
}

}  // namespace

SystemVerilogUvmSequenceAccessHandle
SystemVerilogUvmSequenceService::request_lock(
    const SystemVerilogUvmSequenceHandle sequence) {
  return request_access(sequence, SystemVerilogUvmSequenceAccessKind::Lock);
}

SystemVerilogUvmSequenceAccessHandle
SystemVerilogUvmSequenceService::request_grab(
    const SystemVerilogUvmSequenceHandle sequence) {
  return request_access(sequence, SystemVerilogUvmSequenceAccessKind::Grab);
}

SystemVerilogUvmSequenceAccessHandle
SystemVerilogUvmSequenceService::request_access(
    const SystemVerilogUvmSequenceHandle sequence_handle,
    const SystemVerilogUvmSequenceAccessKind kind) {
  auto& selected_sequence = sequence(sequence_handle);
  if (!selected_sequence.value.sequencer) {
    fail(kInvalidAccess, "unbound UVM sequence cannot own sequencer access");
  }
  auto& selected_sequencer = sequencer(selected_sequence.value.sequencer);
  for (const auto& queued : selected_sequencer.value.access_queue) {
    if (access(queued).value.sequence == sequence_handle) {
      fail(kInvalidAccess, "UVM sequence already waits for sequencer access");
    }
  }
  if (live_accesses_ >= limits_.maximum_access_requests
      || mutations_ >= limits_.maximum_mutations
      || next_access_slot_ == std::numeric_limits<std::uint64_t>::max()
      || next_access_order_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM sequencer access-request ceiling exceeded");
  }
  const auto nested = !selected_sequencer.value.access_stack.empty()
      && access_related(
          access(selected_sequencer.value.access_stack.front()).value.sequence,
          sequence_handle);
  if (nested && selected_sequencer.value.access_stack.size()
          >= limits_.maximum_access_depth) {
    fail(kResourceLimit, "UVM sequencer access nesting depth exceeded");
  }

  const auto slot = next_access_slot_;
  const auto handle = SystemVerilogUvmSequenceAccessHandle{owner_, slot, 1};
  SystemVerilogUvmSequenceAccessSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.sequence = sequence_handle;
  snapshot.sequencer = selected_sequence.value.sequencer;
  snapshot.kind = kind;
  snapshot.state = selected_sequencer.value.access_stack.empty() || nested
      ? SystemVerilogUvmSequenceAccessState::Granted
      : SystemVerilogUvmSequenceAccessState::Pending;
  snapshot.queue_order = next_access_order_;
  auto queue = selected_sequencer.value.access_queue;
  auto stack = selected_sequencer.value.access_stack;
  if (snapshot.state == SystemVerilogUvmSequenceAccessState::Granted) {
    stack.push_back(handle);
  } else if (kind == SystemVerilogUvmSequenceAccessKind::Grab) {
    const auto first_lock = std::ranges::find_if(queue, [&](const auto queued) {
      return access(queued).value.kind
          == SystemVerilogUvmSequenceAccessKind::Lock;
    });
    queue.insert(first_lock, handle);
  } else {
    queue.push_back(handle);
  }
  const auto [inserted, did_insert] = accesses_.emplace(
      slot, Access{std::move(snapshot), 1, true});
  if (!did_insert) fail(kInvalidAccess, "duplicate UVM access-request slot");
  (void)inserted;
  selected_sequencer.value.access_queue.swap(queue);
  selected_sequencer.value.access_stack.swap(stack);
  ++next_access_slot_;
  ++next_access_order_;
  ++live_accesses_;
  ++mutations_;
  return handle;
}

bool SystemVerilogUvmSequenceService::access_related(
    const SystemVerilogUvmSequenceHandle owner,
    SystemVerilogUvmSequenceHandle candidate) const {
  while (candidate) {
    if (candidate == owner) return true;
    candidate = sequence(candidate).value.parent;
  }
  return false;
}

bool SystemVerilogUvmSequenceService::has_lock(
    const SystemVerilogUvmSequenceHandle sequence_handle) const {
  const auto& selected_sequence = sequence(sequence_handle);
  if (!selected_sequence.value.sequencer) return false;
  const auto& selected_sequencer = sequencer(
      selected_sequence.value.sequencer);
  if (selected_sequencer.value.access_stack.empty()) return false;
  return access_related(
      access(selected_sequencer.value.access_stack.front()).value.sequence,
      sequence_handle);
}

void SystemVerilogUvmSequenceService::unlock(
    const SystemVerilogUvmSequenceHandle sequence_handle) {
  auto& selected = sequence(sequence_handle);
  if (!selected.value.sequencer) {
    fail(kInvalidAccess, "unbound UVM sequence cannot unlock");
  }
  release_access(
      sequencer(selected.value.sequencer), sequence_handle,
      SystemVerilogUvmSequenceAccessKind::Lock);
}

void SystemVerilogUvmSequenceService::ungrab(
    const SystemVerilogUvmSequenceHandle sequence_handle) {
  auto& selected = sequence(sequence_handle);
  if (!selected.value.sequencer) {
    fail(kInvalidAccess, "unbound UVM sequence cannot ungrab");
  }
  release_access(
      sequencer(selected.value.sequencer), sequence_handle,
      SystemVerilogUvmSequenceAccessKind::Grab);
}

void SystemVerilogUvmSequenceService::release_access(
    Sequencer& selected_sequencer,
    const SystemVerilogUvmSequenceHandle sequence_handle,
    const SystemVerilogUvmSequenceAccessKind kind) {
  if (selected_sequencer.value.access_stack.empty()) {
    fail(kInvalidAccess, "UVM sequencer access is not held");
  }
  auto& held = access(selected_sequencer.value.access_stack.back());
  if (held.value.sequence != sequence_handle || held.value.kind != kind) {
    fail(kInvalidAccess, "UVM unlock/ungrab does not match nested ownership");
  }
  if (mutations_ >= limits_.maximum_mutations
      || held.generation == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM access release ceiling exceeded");
  }
  selected_sequencer.value.access_stack.pop_back();
  held.live = false;
  ++held.generation;
  --live_accesses_;
  ++mutations_;
  if (selected_sequencer.value.access_stack.empty()) {
    grant_next_access(selected_sequencer);
  }
}

void SystemVerilogUvmSequenceService::grant_next_access(
    Sequencer& selected_sequencer) {
  if (selected_sequencer.value.access_queue.empty()) return;
  auto selected = selected_sequencer.value.access_queue.begin();
  if (selected_sequencer.value.consecutive_grabs
      >= limits_.maximum_consecutive_grabs) {
    const auto lock = std::ranges::find_if(
        selected_sequencer.value.access_queue, [&](const auto handle) {
          return access(handle).value.kind
              == SystemVerilogUvmSequenceAccessKind::Lock;
        });
    if (lock != selected_sequencer.value.access_queue.end()) selected = lock;
  }
  const auto handle = *selected;
  auto& granted = access(handle);
  selected_sequencer.value.access_queue.erase(selected);
  selected_sequencer.value.access_stack.push_back(handle);
  granted.value.state = SystemVerilogUvmSequenceAccessState::Granted;
  if (granted.value.kind == SystemVerilogUvmSequenceAccessKind::Grab) {
    ++selected_sequencer.value.consecutive_grabs;
  } else {
    selected_sequencer.value.consecutive_grabs = 0;
  }
}

void SystemVerilogUvmSequenceService::cancel_access(
    const SystemVerilogUvmSequenceAccessHandle handle) {
  auto& selected = access(handle);
  auto& owner = sequencer(selected.value.sequencer);
  if (selected.value.state == SystemVerilogUvmSequenceAccessState::Granted) {
    if (owner.value.access_stack.empty()
        || owner.value.access_stack.back() != handle) {
      fail(kInvalidAccess, "only the nested top access can be cancelled");
    }
    release_access(owner, selected.value.sequence, selected.value.kind);
    return;
  }
  if (mutations_ >= limits_.maximum_mutations
      || selected.generation == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM access cancellation ceiling exceeded");
  }
  std::erase(owner.value.access_queue, handle);
  selected.live = false;
  ++selected.generation;
  --live_accesses_;
  ++mutations_;
}

void SystemVerilogUvmSequenceService::cancel_sequence_accesses(
    Sequence& selected_sequence) {
  if (!selected_sequence.value.sequencer) return;
  auto& selected_sequencer = sequencer(selected_sequence.value.sequencer);

  const auto queued = selected_sequencer.value.access_queue;
  for (const auto& handle : queued) {
    if (access(handle).value.sequence == selected_sequence.value.handle) {
      cancel_access(handle);
    }
  }
  while (!selected_sequencer.value.access_stack.empty()) {
    const auto handle = selected_sequencer.value.access_stack.back();
    const auto held = access(handle).value;
    if (held.sequence != selected_sequence.value.handle) break;
    cancel_access(handle);
  }
}

SystemVerilogUvmSequenceAccessSnapshot
SystemVerilogUvmSequenceService::access_snapshot(
    const SystemVerilogUvmSequenceAccessHandle handle) const {
  return access(handle).value;
}

std::vector<SystemVerilogUvmSequenceAccessHandle>
SystemVerilogUvmSequenceService::access_requests(
    const SystemVerilogUvmSequencerHandle handle) const {
  const auto& selected = sequencer(handle).value;
  auto result = selected.access_stack;
  result.insert(
      result.end(), selected.access_queue.begin(), selected.access_queue.end());
  return result;
}

SystemVerilogUvmSequenceService::Access&
SystemVerilogUvmSequenceService::access(
    const SystemVerilogUvmSequenceAccessHandle handle) {
  return const_cast<Access&>(std::as_const(*this).access(handle));
}

const SystemVerilogUvmSequenceService::Access&
SystemVerilogUvmSequenceService::access(
    const SystemVerilogUvmSequenceAccessHandle handle) const {
  if (!handle.valid()) fail(kInvalidHandle, "UVM access handle is empty");
  if (handle.owner_ != owner_) {
    fail(kForeignHandle, "UVM access handle belongs to another simulation");
  }
  const auto found = accesses_.find(handle.slot_);
  if (found == accesses_.end() || !found->second.live
      || found->second.generation != handle.generation_) {
    fail(kInvalidHandle, "UVM access handle is stale");
  }
  if (!contains(found->second.value.sequence)
      || !contains(found->second.value.sequencer)) {
    fail(kInvalidHandle, "UVM access owner is stale");
  }
  return found->second;
}

}  // namespace fsim::runtime

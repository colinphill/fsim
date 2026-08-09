// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_sequence.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-SEQ-001"};
constexpr std::string_view kForeignHandle{"FSIM-UVM-SEQ-002"};
constexpr std::string_view kInvalidLifecycle{"FSIM-UVM-SEQ-005"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-SEQ-006"};
constexpr std::string_view kInvalidHandshake{"FSIM-UVM-SEQ-011"};

[[noreturn]] void fail(const std::string_view code,
                       const std::string_view message) {
  throw SystemVerilogUvmSequenceError{std::string{code}, std::string{message}};
}

[[nodiscard]] bool
terminal(const SystemVerilogUvmSequenceTransactionState state) noexcept {
  return state == SystemVerilogUvmSequenceTransactionState::Completed ||
         state == SystemVerilogUvmSequenceTransactionState::Cancelled;
}

[[nodiscard]] bool
inactive_phase(const SystemVerilogUvmPhaseState state) noexcept {
  return state == SystemVerilogUvmPhaseState::Dormant ||
         state == SystemVerilogUvmPhaseState::Ended ||
         state == SystemVerilogUvmPhaseState::Cleanup ||
         state == SystemVerilogUvmPhaseState::Done;
}

[[nodiscard]] bool valid_cancellation(
    const SystemVerilogUvmSequenceCancellationReason reason) noexcept {
  switch (reason) {
  case SystemVerilogUvmSequenceCancellationReason::Explicit:
  case SystemVerilogUvmSequenceCancellationReason::Timeout:
  case SystemVerilogUvmSequenceCancellationReason::PhaseEnded:
  case SystemVerilogUvmSequenceCancellationReason::PhaseJumped:
  case SystemVerilogUvmSequenceCancellationReason::ProcessCancelled:
  case SystemVerilogUvmSequenceCancellationReason::SequenceStopped:
  case SystemVerilogUvmSequenceCancellationReason::SequenceKilled:
  case SystemVerilogUvmSequenceCancellationReason::VirtualReset:
    return true;
  case SystemVerilogUvmSequenceCancellationReason::None:
    return false;
  }
  return false;
}

} // namespace

std::size_t SystemVerilogUvmSequenceService::active_transaction_count(
    const Sequencer &selected) const noexcept {
  return selected.value.active_transactions.size();
}

void SystemVerilogUvmSequenceService::configure_handshake(
    const SystemVerilogUvmSequencerHandle sequencer_handle,
    const std::size_t maximum_in_flight, const std::size_t push_capacity) {
  auto &selected = sequencer(sequencer_handle);
  if (maximum_in_flight == 0 ||
      maximum_in_flight > limits_.maximum_in_flight_per_sequencer ||
      push_capacity == 0 || push_capacity > limits_.maximum_push_capacity) {
    fail(kInvalidHandshake, "UVM driver handshake configuration is invalid");
  }
  if (active_transaction_count(selected) > maximum_in_flight ||
      selected.value.push_queue.size() > push_capacity) {
    fail(kInvalidHandshake,
         "UVM driver handshake configuration would strand active work");
  }
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM driver handshake mutation ceiling exceeded");
  }
  selected.value.maximum_in_flight = maximum_in_flight;
  selected.value.push_capacity = push_capacity;
  ++mutations_;
}

void SystemVerilogUvmSequenceService::validate_handshake_context(
    const Sequencer &selected,
    const SystemVerilogUvmSequenceHandshakeContext &context) const {
  if (context.timeout_ticks > limits_.maximum_transaction_timeout ||
      context.timeout_ticks >
          std::numeric_limits<SimulationTick>::max() - handshake_time_) {
    fail(kResourceLimit, "UVM driver transaction timeout ceiling exceeded");
  }
  if (!context.phase && !context.process)
    return;
  if (!phases_ || !context.phase || !phases_->contains(context.phase)) {
    fail(kInvalidHandshake, "UVM driver transaction phase is invalid");
  }
  const auto phase = phases_->snapshot(context.phase);
  if (phase.state == SystemVerilogUvmPhaseState::Jumping ||
      inactive_phase(phase.state) ||
      std::ranges::find(phase.roots, selected.value.root) ==
          phase.roots.end()) {
    fail(kInvalidHandshake, "UVM driver transaction phase is not active");
  }
  if (!context.process)
    return;
  if (!phases_->contains(context.process)) {
    fail(kInvalidHandshake, "UVM driver transaction process is stale");
  }
  const auto process = phases_->process_snapshot(context.process);
  if (process.phase != context.phase || process.root != selected.value.root ||
      process.state != SystemVerilogUvmPhaseProcessState::Running) {
    fail(kInvalidHandshake, "UVM driver transaction process does not match");
  }
}

SystemVerilogUvmSequenceAcquireResult
SystemVerilogUvmSequenceService::get_next_item(
    const SystemVerilogUvmSequencerHandle sequencer_handle,
    SystemVerilogUvmSequenceHandshakeContext context) {
  return acquire_transaction(sequencer_handle,
                             SystemVerilogUvmSequenceHandshakeKind::GetNextItem,
                             context);
}

SystemVerilogUvmSequenceAcquireResult
SystemVerilogUvmSequenceService::try_next_item(
    const SystemVerilogUvmSequencerHandle sequencer_handle,
    SystemVerilogUvmSequenceHandshakeContext context) {
  return acquire_transaction(sequencer_handle,
                             SystemVerilogUvmSequenceHandshakeKind::TryNextItem,
                             context);
}

SystemVerilogUvmSequenceAcquireResult SystemVerilogUvmSequenceService::get(
    const SystemVerilogUvmSequencerHandle sequencer_handle,
    SystemVerilogUvmSequenceHandshakeContext context) {
  return acquire_transaction(
      sequencer_handle, SystemVerilogUvmSequenceHandshakeKind::Get, context);
}

SystemVerilogUvmSequenceAcquireResult SystemVerilogUvmSequenceService::peek(
    const SystemVerilogUvmSequencerHandle sequencer_handle,
    SystemVerilogUvmSequenceHandshakeContext context) {
  return acquire_transaction(
      sequencer_handle, SystemVerilogUvmSequenceHandshakeKind::Peek, context);
}

SystemVerilogUvmSequenceAcquireResult
SystemVerilogUvmSequenceService::push_next_item(
    const SystemVerilogUvmSequencerHandle sequencer_handle,
    SystemVerilogUvmSequenceHandshakeContext context) {
  return acquire_transaction(
      sequencer_handle, SystemVerilogUvmSequenceHandshakeKind::Push, context);
}

SystemVerilogUvmSequenceAcquireResult
SystemVerilogUvmSequenceService::acquire_transaction(
    const SystemVerilogUvmSequencerHandle sequencer_handle,
    const SystemVerilogUvmSequenceHandshakeKind kind,
    const SystemVerilogUvmSequenceHandshakeContext context) {
  auto &selected = sequencer(sequencer_handle);
  validate_handshake_context(selected, context);

  if (kind == SystemVerilogUvmSequenceHandshakeKind::Peek ||
      kind == SystemVerilogUvmSequenceHandshakeKind::Get ||
      kind == SystemVerilogUvmSequenceHandshakeKind::GetNextItem ||
      kind == SystemVerilogUvmSequenceHandshakeKind::TryNextItem) {
    for (const auto &handle : selected.value.active_transactions) {
      auto &existing = transaction(handle);
      if (existing.value.state !=
          SystemVerilogUvmSequenceTransactionState::Peeked) {
        continue;
      }
      if (mutations_ >= limits_.maximum_mutations) {
        fail(kResourceLimit, "UVM driver peek mutation ceiling exceeded");
      }
      if (kind == SystemVerilogUvmSequenceHandshakeKind::Peek) {
        ++existing.value.peek_count;
      } else if (kind == SystemVerilogUvmSequenceHandshakeKind::Get) {
        if (next_transaction_completion_order_ ==
            std::numeric_limits<std::uint64_t>::max()) {
          fail(kResourceLimit, "UVM transaction completion order exhausted");
        }
        existing.value.kind = kind;
        existing.value.state =
            SystemVerilogUvmSequenceTransactionState::Completed;
        existing.value.completion_order = next_transaction_completion_order_++;
        std::erase(selected.value.active_transactions, handle);
        if (existing.value.request_item) {
          item(existing.value.request_item).value.state =
              SystemVerilogUvmSequenceItemState::Completed;
        }
      } else {
        existing.value.kind = kind;
        existing.value.state =
            SystemVerilogUvmSequenceTransactionState::Reserved;
      }
      ++mutations_;
      return {SystemVerilogUvmSequenceAcquireStatus::Acquired,
              std::optional<SystemVerilogUvmSequenceTransactionSnapshot>{
                  existing.value}};
    }
  }

  if (active_transaction_count(selected) >= selected.value.maximum_in_flight ||
      (kind == SystemVerilogUvmSequenceHandshakeKind::Push &&
       selected.value.push_queue.size() >= selected.value.push_capacity)) {
    return {SystemVerilogUvmSequenceAcquireStatus::Backpressured, std::nullopt};
  }
  if (selected.value.requests.empty()) {
    return {kind == SystemVerilogUvmSequenceHandshakeKind::TryNextItem
                ? SystemVerilogUvmSequenceAcquireStatus::Empty
                : SystemVerilogUvmSequenceAcquireStatus::WaitingForRequest,
            std::nullopt};
  }
  if (live_transactions_ >= limits_.maximum_transactions ||
      selected.value.transactions.size() >=
          limits_.maximum_transactions_per_sequencer ||
      next_transaction_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      2U > limits_.maximum_mutations - mutations_ ||
      (kind == SystemVerilogUvmSequenceHandshakeKind::Get &&
       next_transaction_completion_order_ ==
           std::numeric_limits<std::uint64_t>::max())) {
    fail(kResourceLimit, "UVM driver transaction ceiling exceeded");
  }

  const auto chosen = select_request(sequencer_handle);
  if (chosen.status == SystemVerilogUvmSequenceSelectionStatus::Empty) {
    return {kind == SystemVerilogUvmSequenceHandshakeKind::TryNextItem
                ? SystemVerilogUvmSequenceAcquireStatus::Empty
                : SystemVerilogUvmSequenceAcquireStatus::WaitingForRequest,
            std::nullopt};
  }
  if (chosen.status ==
      SystemVerilogUvmSequenceSelectionStatus::WaitingForRelevant) {
    return {SystemVerilogUvmSequenceAcquireStatus::WaitingForRelevant,
            std::nullopt};
  }
  if (!chosen.request) {
    fail(kInvalidHandshake, "UVM arbitration selected no request snapshot");
  }

  const auto slot = next_transaction_slot_;
  const auto handle =
      SystemVerilogUvmSequenceTransactionHandle{owner_, slot, 1};
  SystemVerilogUvmSequenceTransactionSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.request = *chosen.request;
  snapshot.sequence = chosen.request->sequence;
  snapshot.sequencer = sequencer_handle;
  snapshot.request_item = chosen.request->item;
  snapshot.kind = kind;
  snapshot.phase = context.phase;
  snapshot.process = context.process;
  snapshot.started_at = handshake_time_;
  if (context.timeout_ticks != 0) {
    snapshot.deadline = handshake_time_ + context.timeout_ticks;
  }
  if (snapshot.request_item) {
    auto &selected_item = item(snapshot.request_item);
    snapshot.request_object = selected_item.value.object;
    selected_item.value.state = SystemVerilogUvmSequenceItemState::InFlight;
  } else {
    snapshot.request_object = sequence(snapshot.sequence).value.object;
  }
  if (kind == SystemVerilogUvmSequenceHandshakeKind::Get) {
    if (next_transaction_completion_order_ ==
        std::numeric_limits<std::uint64_t>::max()) {
      fail(kResourceLimit, "UVM transaction completion order exhausted");
    }
    snapshot.state = SystemVerilogUvmSequenceTransactionState::Completed;
    snapshot.completion_order = next_transaction_completion_order_++;
    if (snapshot.request_item) {
      item(snapshot.request_item).value.state =
          SystemVerilogUvmSequenceItemState::Completed;
    }
  } else if (kind == SystemVerilogUvmSequenceHandshakeKind::Peek) {
    snapshot.state = SystemVerilogUvmSequenceTransactionState::Peeked;
    snapshot.peek_count = 1;
  } else if (kind == SystemVerilogUvmSequenceHandshakeKind::Push) {
    snapshot.state = SystemVerilogUvmSequenceTransactionState::Pushed;
  } else {
    snapshot.state = SystemVerilogUvmSequenceTransactionState::Reserved;
  }

  auto retained = selected.value.transactions;
  retained.push_back(handle);
  auto active = selected.value.active_transactions;
  auto pushed = selected.value.push_queue;
  if (!terminal(snapshot.state))
    active.push_back(handle);
  if (snapshot.state == SystemVerilogUvmSequenceTransactionState::Pushed) {
    pushed.push_back(handle);
  }
  const auto [inserted, did_insert] =
      transactions_.emplace(slot, Transaction{std::move(snapshot), 1, true});
  if (!did_insert)
    fail(kInvalidHandshake, "duplicate UVM transaction slot");
  selected.value.transactions.swap(retained);
  selected.value.active_transactions.swap(active);
  selected.value.push_queue.swap(pushed);
  ++next_transaction_slot_;
  ++live_transactions_;
  ++mutations_;
  return {SystemVerilogUvmSequenceAcquireStatus::Acquired,
          std::optional<SystemVerilogUvmSequenceTransactionSnapshot>{
              inserted->second.value}};
}

SystemVerilogUvmSequenceAcquireResult
SystemVerilogUvmSequenceService::try_pop_pushed_item(
    const SystemVerilogUvmSequencerHandle sequencer_handle) {
  auto &selected = sequencer(sequencer_handle);
  if (selected.value.push_queue.empty()) {
    return {SystemVerilogUvmSequenceAcquireStatus::Empty, std::nullopt};
  }
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM push-pop mutation ceiling exceeded");
  }
  const auto handle = selected.value.push_queue.front();
  auto &existing = transaction(handle);
  if (existing.value.state !=
      SystemVerilogUvmSequenceTransactionState::Pushed) {
    fail(kInvalidHandshake, "UVM push queue contains a non-pushed transaction");
  }
  selected.value.push_queue.erase(selected.value.push_queue.begin());
  existing.value.state = SystemVerilogUvmSequenceTransactionState::Reserved;
  ++mutations_;
  return {SystemVerilogUvmSequenceAcquireStatus::Acquired,
          std::optional<SystemVerilogUvmSequenceTransactionSnapshot>{
              existing.value}};
}

void SystemVerilogUvmSequenceService::complete_transaction(
    Transaction &selected, const SystemVerilogUvmSequenceItemHandle response) {
  if (selected.value.state !=
      SystemVerilogUvmSequenceTransactionState::Reserved) {
    fail(kInvalidHandshake, "UVM item_done requires a reserved transaction");
  }
  const auto required_mutations = response ? 2U : 1U;
  if (required_mutations > limits_.maximum_mutations - mutations_ ||
      next_transaction_completion_order_ ==
          std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM item_done mutation ceiling exceeded");
  }
  if (response) {
    const auto &response_value = item(response).value;
    if (response_value.role != SystemVerilogUvmSequenceItemRole::Response ||
        response_value.owner_sequence != selected.value.sequence ||
        response_value.sequencer != selected.value.sequencer) {
      fail(kInvalidLifecycle, "UVM item_done response ownership is invalid");
    }
    (void)route_response(response);
  }
  auto &owner = sequencer(selected.value.sequencer);
  std::erase(owner.value.active_transactions, selected.value.handle);
  selected.value.response_item = response;
  selected.value.state = SystemVerilogUvmSequenceTransactionState::Completed;
  selected.value.completion_order = next_transaction_completion_order_++;
  if (selected.value.request_item) {
    item(selected.value.request_item).value.state =
        SystemVerilogUvmSequenceItemState::Completed;
  }
  ++mutations_;
}

void SystemVerilogUvmSequenceService::item_done(
    const SystemVerilogUvmSequenceTransactionHandle handle,
    const SystemVerilogUvmSequenceItemHandle response) {
  complete_transaction(transaction(handle), response);
}

void SystemVerilogUvmSequenceService::put_response(
    const SystemVerilogUvmSequenceTransactionHandle handle,
    const SystemVerilogUvmSequenceItemHandle response) {
  auto &selected = transaction(handle);
  if (selected.value.state !=
          SystemVerilogUvmSequenceTransactionState::Completed ||
      selected.value.response_item || !response) {
    fail(
        kInvalidHandshake,
        "UVM put_response requires a completed transaction without a response");
  }
  if (2U > limits_.maximum_mutations - mutations_) {
    fail(kResourceLimit, "UVM put_response mutation ceiling exceeded");
  }
  const auto &response_value = item(response).value;
  if (response_value.role != SystemVerilogUvmSequenceItemRole::Response ||
      response_value.owner_sequence != selected.value.sequence ||
      response_value.sequencer != selected.value.sequencer) {
    fail(kInvalidLifecycle, "UVM put_response ownership is invalid");
  }
  (void)route_response(response);
  selected.value.response_item = response;
  ++mutations_;
}

void SystemVerilogUvmSequenceService::cancel_transaction_impl(
    Transaction &selected,
    const SystemVerilogUvmSequenceCancellationReason reason) {
  if (terminal(selected.value.state) || !valid_cancellation(reason)) {
    fail(kInvalidHandshake, "UVM transaction cancellation is invalid");
  }
  if (mutations_ >= limits_.maximum_mutations ||
      next_transaction_completion_order_ ==
          std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM transaction cancellation ceiling exceeded");
  }
  auto &owner = sequencer(selected.value.sequencer);
  std::erase(owner.value.active_transactions, selected.value.handle);
  std::erase(owner.value.push_queue, selected.value.handle);
  selected.value.state = SystemVerilogUvmSequenceTransactionState::Cancelled;
  selected.value.cancellation = reason;
  selected.value.completion_order = next_transaction_completion_order_++;
  if (selected.value.request_item) {
    item(selected.value.request_item).value.state =
        SystemVerilogUvmSequenceItemState::Cancelled;
  }
  ++mutations_;
}

void SystemVerilogUvmSequenceService::cancel_transaction(
    const SystemVerilogUvmSequenceTransactionHandle handle,
    const SystemVerilogUvmSequenceCancellationReason reason) {
  cancel_transaction_impl(transaction(handle), reason);
}

void SystemVerilogUvmSequenceService::cancel_phase_transactions(
    const SystemVerilogUvmPhaseHandle phase,
    const SystemVerilogUvmSequenceCancellationReason reason) {
  if (!phase || !valid_cancellation(reason) || !phases_ ||
      !phases_->contains(phase)) {
    fail(kInvalidHandshake, "UVM phase transaction cancellation is invalid");
  }
  std::vector<SystemVerilogUvmSequenceTransactionHandle> cancelled;
  for (const auto &[slot, existing] : transactions_) {
    (void)slot;
    if (existing.live && !terminal(existing.value.state) &&
        existing.value.phase == phase) {
      cancelled.push_back(existing.value.handle);
    }
  }
  if (cancelled.size() > limits_.maximum_mutations - mutations_) {
    fail(kResourceLimit, "UVM phase cancellation mutation ceiling exceeded");
  }
  for (const auto &handle : cancelled) {
    cancel_transaction_impl(transaction(handle), reason);
  }
}

SystemVerilogUvmPhaseJumpResult SystemVerilogUvmSequenceService::jump_phase(
    const SystemVerilogUvmPhaseHandle from,
    const SystemVerilogUvmPhaseHandle target) {
  if (!phases_ || !phases_->contains(from) || !phases_->contains(target)) {
    fail(kInvalidHandshake, "UVM transaction phase jump is invalid");
  }
  std::size_t active{};
  for (const auto &[slot, existing] : transactions_) {
    (void)slot;
    if (existing.live && !terminal(existing.value.state) &&
        existing.value.phase == from) {
      ++active;
    }
  }
  if (active > limits_.maximum_mutations - mutations_) {
    fail(kResourceLimit, "UVM phase-jump cancellation ceiling exceeded");
  }
  auto result = phases_->jump(from, target);
  cancel_phase_transactions(
      from, SystemVerilogUvmSequenceCancellationReason::PhaseJumped);
  return result;
}

void SystemVerilogUvmSequenceService::synchronize_phase_transactions() {
  std::vector<std::pair<SystemVerilogUvmSequenceTransactionHandle,
                        SystemVerilogUvmSequenceCancellationReason>>
      cancelled;
  for (const auto &[slot, existing] : transactions_) {
    (void)slot;
    if (!existing.live || terminal(existing.value.state) ||
        !existing.value.phase) {
      continue;
    }
    auto reason = SystemVerilogUvmSequenceCancellationReason::None;
    if (!phases_ || !phases_->contains(existing.value.phase)) {
      reason = SystemVerilogUvmSequenceCancellationReason::PhaseEnded;
    } else {
      const auto phase = phases_->snapshot(existing.value.phase);
      if (phase.state == SystemVerilogUvmPhaseState::Jumping) {
        reason = SystemVerilogUvmSequenceCancellationReason::PhaseJumped;
      } else if (inactive_phase(phase.state)) {
        reason = SystemVerilogUvmSequenceCancellationReason::PhaseEnded;
      }
    }
    if (reason == SystemVerilogUvmSequenceCancellationReason::None &&
        existing.value.process &&
        (!phases_->contains(existing.value.process) ||
         phases_->process_snapshot(existing.value.process).state !=
             SystemVerilogUvmPhaseProcessState::Running)) {
      reason = SystemVerilogUvmSequenceCancellationReason::ProcessCancelled;
    }
    if (reason != SystemVerilogUvmSequenceCancellationReason::None) {
      cancelled.emplace_back(existing.value.handle, reason);
    }
  }
  if (cancelled.size() > limits_.maximum_mutations - mutations_) {
    fail(kResourceLimit, "UVM phase synchronization mutation ceiling exceeded");
  }
  for (const auto &[handle, reason] : cancelled) {
    cancel_transaction_impl(transaction(handle), reason);
  }
}

void SystemVerilogUvmSequenceService::advance_handshake_time(
    const SimulationTick time) {
  if (time < handshake_time_) {
    fail(kInvalidHandshake, "UVM handshake time cannot move backward");
  }
  std::vector<SystemVerilogUvmSequenceTransactionHandle> expired;
  for (const auto &[slot, existing] : transactions_) {
    (void)slot;
    if (existing.live && !terminal(existing.value.state) &&
        existing.value.deadline && *existing.value.deadline <= time) {
      expired.push_back(existing.value.handle);
    }
  }
  if (expired.size() > limits_.maximum_mutations - mutations_) {
    fail(kResourceLimit, "UVM timeout cancellation mutation ceiling exceeded");
  }
  handshake_time_ = time;
  for (const auto &handle : expired) {
    cancel_transaction_impl(
        transaction(handle),
        SystemVerilogUvmSequenceCancellationReason::Timeout);
  }
}

void SystemVerilogUvmSequenceService::cancel_sequence_transactions(
    Sequence &selected,
    const SystemVerilogUvmSequenceCancellationReason reason) {
  std::vector<SystemVerilogUvmSequenceTransactionHandle> cancelled;
  for (const auto &[slot, existing] : transactions_) {
    (void)slot;
    if (existing.live && !terminal(existing.value.state) &&
        existing.value.sequence == selected.value.handle) {
      cancelled.push_back(existing.value.handle);
    }
  }
  if (cancelled.size() > limits_.maximum_mutations - mutations_) {
    fail(kResourceLimit, "UVM sequence cancellation mutation ceiling exceeded");
  }
  for (const auto &handle : cancelled) {
    cancel_transaction_impl(transaction(handle), reason);
  }
}

SystemVerilogUvmSequenceTransactionSnapshot
SystemVerilogUvmSequenceService::transaction_snapshot(
    const SystemVerilogUvmSequenceTransactionHandle handle) const {
  return transaction(handle).value;
}

std::vector<SystemVerilogUvmSequenceTransactionHandle>
SystemVerilogUvmSequenceService::transactions(
    const SystemVerilogUvmSequencerHandle sequencer_handle) const {
  return sequencer(sequencer_handle).value.transactions;
}

void SystemVerilogUvmSequenceService::release_transaction(
    const SystemVerilogUvmSequenceTransactionHandle handle) {
  auto &selected = transaction(handle);
  if (!terminal(selected.value.state)) {
    fail(kInvalidHandshake, "active UVM transaction cannot be released");
  }
  if (mutations_ >= limits_.maximum_mutations ||
      selected.generation == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM transaction release ceiling exceeded");
  }
  auto &owner = sequencer(selected.value.sequencer);
  std::erase(owner.value.transactions, handle);
  selected.live = false;
  ++selected.generation;
  --live_transactions_;
  ++mutations_;
}

SystemVerilogUvmSequenceService::Transaction &
SystemVerilogUvmSequenceService::transaction(
    const SystemVerilogUvmSequenceTransactionHandle handle) {
  return const_cast<Transaction &>(std::as_const(*this).transaction(handle));
}

const SystemVerilogUvmSequenceService::Transaction &
SystemVerilogUvmSequenceService::transaction(
    const SystemVerilogUvmSequenceTransactionHandle handle) const {
  if (!handle.valid())
    fail(kInvalidHandle, "UVM transaction handle is empty");
  if (handle.owner_ != owner_) {
    fail(kForeignHandle, "UVM transaction belongs to another simulation");
  }
  const auto found = transactions_.find(handle.slot_);
  if (found == transactions_.end() || !found->second.live ||
      found->second.generation != handle.generation_) {
    fail(kInvalidHandle, "UVM transaction handle is stale");
  }
  if (!contains(found->second.value.sequence) ||
      !contains(found->second.value.sequencer)) {
    fail(kInvalidHandle, "UVM transaction owner is stale");
  }
  return found->second;
}

} // namespace fsim::runtime

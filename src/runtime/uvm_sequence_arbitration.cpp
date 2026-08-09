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
constexpr std::string_view kInvalidArbitration{"FSIM-UVM-SEQ-008"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmSequenceError{
      std::string{code}, std::string{message}};
}

[[nodiscard]] std::uint64_t next_random(std::uint64_t& state) noexcept {
  state += 0x9e37'79b9'7f4a'7c15ULL;
  auto value = state;
  value = (value ^ (value >> 30U)) * 0xbf58'476d'1ce4'e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d0'49bb'1331'11ebULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] std::uint64_t bounded_random(
    std::uint64_t& state,
    std::uint64_t& draws,
    const std::uint64_t bound,
    std::size_t& work,
    const std::size_t maximum_work) {
  const auto threshold = (std::uint64_t{0} - bound) % bound;
  while (true) {
    if (work >= maximum_work
        || draws == std::numeric_limits<std::uint64_t>::max()) {
      fail(kResourceLimit, "UVM arbitration random-work ceiling exceeded");
    }
    ++work;
    ++draws;
    const auto value = next_random(state);
    if (value >= threshold) return value % bound;
  }
}

}  // namespace

void SystemVerilogUvmSequenceService::configure_arbitration(
    const SystemVerilogUvmSequencerHandle handle,
    const SystemVerilogUvmSequenceArbitrationMode mode,
    const std::uint64_t seed) {
  if (arbitration_callback_active_) {
    fail(kInvalidArbitration, "UVM arbitration callback cannot mutate policy");
  }
  switch (mode) {
    case SystemVerilogUvmSequenceArbitrationMode::Fifo:
    case SystemVerilogUvmSequenceArbitrationMode::Weighted:
    case SystemVerilogUvmSequenceArbitrationMode::Random:
    case SystemVerilogUvmSequenceArbitrationMode::StrictFifo:
    case SystemVerilogUvmSequenceArbitrationMode::StrictRandom:
    case SystemVerilogUvmSequenceArbitrationMode::User:
      break;
    default:
      fail(kInvalidArbitration, "unknown UVM arbitration mode");
  }
  auto& selected = sequencer(handle);
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM arbitration mutation ceiling exceeded");
  }
  selected.value.arbitration = mode;
  selected.value.random_seed = seed;
  selected.value.random_draws = 0;
  selected.value.relevance_waits = 0;
  selected.random_state = seed;
  ++mutations_;
}

void SystemVerilogUvmSequenceService::set_user_arbitration(
    const SystemVerilogUvmSequencerHandle handle,
    UserArbitration callback) {
  if (arbitration_callback_active_) {
    fail(kInvalidArbitration, "UVM arbitration callback cannot replace itself");
  }
  if (!callback) {
    fail(kInvalidArbitration, "UVM user arbitration callback is empty");
  }
  auto& selected = sequencer(handle);
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM arbitration mutation ceiling exceeded");
  }
  selected.user_arbitration = std::move(callback);
  ++mutations_;
}

void SystemVerilogUvmSequenceService::reseed_arbitration(
    const SystemVerilogUvmSequencerHandle handle,
    const std::uint64_t seed) {
  if (arbitration_callback_active_) {
    fail(kInvalidArbitration, "UVM arbitration callback cannot reseed");
  }
  auto& selected = sequencer(handle);
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM arbitration mutation ceiling exceeded");
  }
  selected.value.random_seed = seed;
  selected.value.random_draws = 0;
  selected.random_state = seed;
  ++mutations_;
}

SystemVerilogUvmSequenceRequestHandle
SystemVerilogUvmSequenceService::enqueue_request(
    SystemVerilogUvmSequenceRequestDescriptor descriptor) {
  if (arbitration_callback_active_) {
    fail(kInvalidArbitration, "UVM arbitration callback cannot enqueue");
  }
  auto& selected_sequence = sequence(descriptor.sequence);
  if (!selected_sequence.value.sequencer) {
    fail(kInvalidArbitration, "unbound UVM sequence cannot request arbitration");
  }
  auto& selected_sequencer = sequencer(selected_sequence.value.sequencer);
  if (descriptor.item) {
    const auto& selected_item = item(descriptor.item).value;
    if (selected_item.role != SystemVerilogUvmSequenceItemRole::Request
        || selected_item.owner_sequence != descriptor.sequence
        || selected_item.sequencer != selected_sequence.value.sequencer) {
      fail(
          kInvalidLifecycle,
          "UVM macro request item does not match its sequence and sequencer");
    }
  }
  if (selected_sequence.active) {
    fail(kInvalidLifecycle, "active UVM sequence cannot enqueue another request");
  }
  for (const auto& [slot, pending] : requests_) {
    (void)slot;
    if (pending.live && pending.value.sequence == descriptor.sequence) {
      fail(kInvalidLifecycle, "UVM sequence already has a pending request");
    }
  }
  if (descriptor.priority == 0
      || descriptor.priority > limits_.maximum_priority) {
    fail(kInvalidArbitration, "UVM sequence request priority is invalid");
  }
  if (live_requests_ >= limits_.maximum_pending_requests
      || selected_sequencer.value.requests.size()
          >= limits_.maximum_requests_per_sequencer
      || mutations_ >= limits_.maximum_mutations
      || next_request_slot_ == std::numeric_limits<std::uint64_t>::max()
      || next_request_order_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM sequence request queue ceiling exceeded");
  }

  const auto slot = next_request_slot_;
  const auto handle =
      SystemVerilogUvmSequenceRequestHandle{owner_, slot, 1};
  SystemVerilogUvmSequenceRequestSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.sequence = descriptor.sequence;
  snapshot.sequencer = selected_sequence.value.sequencer;
  snapshot.priority = descriptor.priority;
  snapshot.relevant = descriptor.relevant;
  snapshot.queue_order = next_request_order_;
  snapshot.item = descriptor.item;
  auto queue = selected_sequencer.value.requests;
  queue.push_back(handle);
  const auto [inserted, did_insert] = requests_.emplace(
      slot,
      Request{
          std::move(snapshot), std::move(descriptor.is_relevant), 1, true});
  if (!did_insert) {
    fail(kInvalidLifecycle, "duplicate UVM sequence request slot");
  }
  (void)inserted;
  selected_sequencer.value.requests.swap(queue);
  ++next_request_slot_;
  ++next_request_order_;
  ++live_requests_;
  ++mutations_;
  return handle;
}

void SystemVerilogUvmSequenceService::rollback_latest_request(
    const SystemVerilogUvmSequenceRequestHandle handle) noexcept {
  if (handle.owner_ != owner_) return;
  const auto found = requests_.find(handle.slot_);
  if (found == requests_.end() || !found->second.live
      || found->second.generation != handle.generation_) {
    return;
  }
  const auto sequencer_slot = found->second.value.sequencer.slot_;
  const auto selected_sequencer = sequencers_.find(sequencer_slot);
  if (selected_sequencer != sequencers_.end()) {
    std::erase(selected_sequencer->second.value.requests, handle);
  }
  const auto queue_order = found->second.value.queue_order;
  requests_.erase(found);
  if (live_requests_ != 0) --live_requests_;
  if (mutations_ != 0) --mutations_;
  if (next_request_slot_ == handle.slot_ + 1U) --next_request_slot_;
  if (next_request_order_ == queue_order + 1U) --next_request_order_;
}

void SystemVerilogUvmSequenceService::set_request_relevant(
    const SystemVerilogUvmSequenceRequestHandle handle,
    const bool relevant) {
  if (arbitration_callback_active_) {
    fail(kInvalidArbitration, "UVM relevance callback cannot mutate requests");
  }
  auto& selected = request(handle);
  auto& owner = sequencer(selected.value.sequencer);
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM relevance mutation ceiling exceeded");
  }
  selected.value.relevant = relevant;
  if (relevant) owner.value.relevance_waits = 0;
  ++mutations_;
}

void SystemVerilogUvmSequenceService::cancel_request(
    const SystemVerilogUvmSequenceRequestHandle handle) {
  if (arbitration_callback_active_) {
    fail(kInvalidArbitration, "UVM arbitration callback cannot cancel requests");
  }
  auto& selected = request(handle);
  auto& owner = sequencer(selected.value.sequencer);
  if (mutations_ >= limits_.maximum_mutations
      || selected.generation == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM request cancellation ceiling exceeded");
  }
  auto queue = owner.value.requests;
  std::erase(queue, handle);
  owner.value.requests.swap(queue);
  selected.live = false;
  ++selected.generation;
  --live_requests_;
  ++mutations_;
}

SystemVerilogUvmSequenceSelectionResult
SystemVerilogUvmSequenceService::select_request(
    const SystemVerilogUvmSequencerHandle handle) {
  if (arbitration_callback_active_) {
    fail(kInvalidArbitration, "nested UVM arbitration is not permitted");
  }
  auto& selected_sequencer = sequencer(handle);
  if (selected_sequencer.value.requests.empty()) {
    return {SystemVerilogUvmSequenceSelectionStatus::Empty, std::nullopt};
  }
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM arbitration mutation ceiling exceeded");
  }

  std::size_t work{};
  std::vector<SystemVerilogUvmSequenceRequestSnapshot> candidates;
  candidates.reserve(selected_sequencer.value.requests.size());
  for (const auto& request_handle : selected_sequencer.value.requests) {
    if (work >= limits_.maximum_arbitration_work) {
      fail(kResourceLimit, "UVM arbitration selection-work ceiling exceeded");
    }
    ++work;
    auto& pending = request(request_handle);
    bool relevant = pending.value.relevant;
    if (relevant && pending.is_relevant) {
      arbitration_callback_active_ = true;
      try {
        relevant = pending.is_relevant(pending.value.sequence);
      } catch (...) {
        arbitration_callback_active_ = false;
        fail(kInvalidArbitration, "UVM relevance callback threw");
      }
      arbitration_callback_active_ = false;
    }
    if (relevant) candidates.push_back(pending.value);
  }

  if (candidates.empty()) {
    if (selected_sequencer.value.relevance_waits
        >= limits_.maximum_relevance_waits) {
      fail(kResourceLimit, "UVM wait-for-relevant ceiling exceeded");
    }
    ++selected_sequencer.value.relevance_waits;
    ++mutations_;
    return {
        SystemVerilogUvmSequenceSelectionStatus::WaitingForRelevant,
        std::nullopt};
  }

  auto random_state = selected_sequencer.random_state;
  auto random_draws = selected_sequencer.value.random_draws;
  std::size_t selected_index{};
  switch (selected_sequencer.value.arbitration) {
    case SystemVerilogUvmSequenceArbitrationMode::Fifo:
      break;
    case SystemVerilogUvmSequenceArbitrationMode::Random:
      selected_index = static_cast<std::size_t>(bounded_random(
          random_state, random_draws, candidates.size(), work,
          limits_.maximum_arbitration_work));
      break;
    case SystemVerilogUvmSequenceArbitrationMode::StrictFifo: {
      const auto maximum = std::ranges::max(
          candidates, {}, &SystemVerilogUvmSequenceRequestSnapshot::priority)
                               .priority;
      selected_index = static_cast<std::size_t>(std::ranges::find(
          candidates, maximum,
          &SystemVerilogUvmSequenceRequestSnapshot::priority)
          - candidates.begin());
      break;
    }
    case SystemVerilogUvmSequenceArbitrationMode::StrictRandom: {
      const auto maximum = std::ranges::max(
          candidates, {}, &SystemVerilogUvmSequenceRequestSnapshot::priority)
                               .priority;
      std::vector<std::size_t> ties;
      for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (work >= limits_.maximum_arbitration_work) {
          fail(kResourceLimit, "UVM strict-random selection-work exceeded");
        }
        ++work;
        if (candidates[index].priority == maximum) ties.push_back(index);
      }
      selected_index = ties[static_cast<std::size_t>(bounded_random(
          random_state, random_draws, ties.size(), work,
          limits_.maximum_arbitration_work))];
      break;
    }
    case SystemVerilogUvmSequenceArbitrationMode::Weighted: {
      std::uint64_t total{};
      for (const auto& candidate : candidates) {
        if (work >= limits_.maximum_arbitration_work
            || total > std::numeric_limits<std::uint64_t>::max()
                - candidate.priority) {
          fail(kResourceLimit, "UVM weighted arbitration work overflow");
        }
        ++work;
        total += candidate.priority;
      }
      auto ticket = bounded_random(
          random_state, random_draws, total, work,
          limits_.maximum_arbitration_work);
      for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (ticket < candidates[index].priority) {
          selected_index = index;
          break;
        }
        ticket -= candidates[index].priority;
      }
      break;
    }
    case SystemVerilogUvmSequenceArbitrationMode::User: {
      if (!selected_sequencer.user_arbitration) {
        fail(kInvalidArbitration, "UVM user arbitration callback is unset");
      }
      SystemVerilogUvmSequenceRequestHandle chosen;
      arbitration_callback_active_ = true;
      try {
        chosen = selected_sequencer.user_arbitration(candidates);
      } catch (...) {
        arbitration_callback_active_ = false;
        fail(kInvalidArbitration, "UVM user arbitration callback threw");
      }
      arbitration_callback_active_ = false;
      const auto found = std::ranges::find(
          candidates, chosen,
          &SystemVerilogUvmSequenceRequestSnapshot::handle);
      if (found == candidates.end()) {
        fail(kInvalidArbitration, "UVM user arbitration chose no candidate");
      }
      selected_index = static_cast<std::size_t>(found - candidates.begin());
      break;
    }
    default:
      fail(kInvalidArbitration, "unknown UVM arbitration mode");
  }

  const auto chosen = candidates[selected_index];
  auto queue = selected_sequencer.value.requests;
  std::erase(queue, chosen.handle);
  auto& selected_request = request(chosen.handle);
  if (selected_request.generation
      == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM request generation ceiling exceeded");
  }
  selected_sequencer.value.requests.swap(queue);
  selected_sequencer.value.relevance_waits = 0;
  selected_sequencer.random_state = random_state;
  selected_sequencer.value.random_draws = random_draws;
  selected_request.live = false;
  ++selected_request.generation;
  --live_requests_;
  ++mutations_;
  return {
      SystemVerilogUvmSequenceSelectionStatus::Selected,
      std::optional<SystemVerilogUvmSequenceRequestSnapshot>{chosen}};
}

SystemVerilogUvmSequenceRequestSnapshot
SystemVerilogUvmSequenceService::request_snapshot(
    const SystemVerilogUvmSequenceRequestHandle handle) const {
  return request(handle).value;
}

std::vector<SystemVerilogUvmSequenceRequestHandle>
SystemVerilogUvmSequenceService::requests(
    const SystemVerilogUvmSequencerHandle handle) const {
  return sequencer(handle).value.requests;
}

SystemVerilogUvmSequenceService::Request&
SystemVerilogUvmSequenceService::request(
    const SystemVerilogUvmSequenceRequestHandle handle) {
  return const_cast<Request&>(std::as_const(*this).request(handle));
}

const SystemVerilogUvmSequenceService::Request&
SystemVerilogUvmSequenceService::request(
    const SystemVerilogUvmSequenceRequestHandle handle) const {
  if (!handle.valid()) fail(kInvalidHandle, "UVM sequence request is empty");
  if (handle.owner_ != owner_) {
    fail(kForeignHandle, "UVM sequence request belongs to another simulation");
  }
  const auto found = requests_.find(handle.slot_);
  if (found == requests_.end() || !found->second.live
      || found->second.generation != handle.generation_) {
    fail(kInvalidHandle, "UVM sequence request is stale");
  }
  if (!contains(found->second.value.sequence)
      || !contains(found->second.value.sequencer)) {
    fail(kInvalidHandle, "UVM sequence request owner is stale");
  }
  return found->second;
}

}  // namespace fsim::runtime

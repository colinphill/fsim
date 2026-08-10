// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_synchronization.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <ranges>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidPolicyObject{"FSIM-UVM-SYNC-001"};
constexpr std::string_view kPolicyResource{"FSIM-UVM-SYNC-002"};

[[noreturn]] void fail(
    const std::string_view code, const std::string_view message) {
  throw SystemVerilogUvmPolicyError{std::string{code}, std::string{message}};
}

[[nodiscard]] std::size_t edit_distance(
    const std::string_view left, const std::string_view right) {
  std::vector<std::size_t> previous(right.size() + 1U);
  std::vector<std::size_t> current(right.size() + 1U);
  std::iota(previous.begin(), previous.end(), 0U);
  for (std::size_t row = 1; row <= left.size(); ++row) {
    current[0] = row;
    for (std::size_t column = 1; column <= right.size(); ++column) {
      const auto substitution = previous[column - 1U]
          + (left[row - 1U] == right[column - 1U] ? 0U : 1U);
      current[column] = std::min(
          {previous[column] + 1U, current[column - 1U] + 1U, substitution});
    }
    previous.swap(current);
  }
  return previous.back();
}

[[nodiscard]] std::size_t item_text_bytes(
    const SystemVerilogUvmPackItem& item) {
  auto total = item.name.size() + item.type_name.size() + item.string_value.size();
  for (const auto& element : item.elements) {
    const auto nested = item_text_bytes(element);
    if (nested > std::numeric_limits<std::size_t>::max() - total) {
      fail(kPolicyResource, "UVM policy value text accounting overflowed");
    }
    total += nested;
  }
  return total;
}

}  // namespace

SystemVerilogUvmPolicyError::SystemVerilogUvmPolicyError(
    std::string code, std::string message)
    : std::runtime_error{std::move(message)}, diagnostic_code_{std::move(code)} {}

SystemVerilogUvmSynchronizationService::SystemVerilogUvmSynchronizationService(
    SystemVerilogUvmObjectService& objects,
    SystemVerilogUvmPolicyLimits limits)
    : objects_{&objects}, limits_{std::move(limits)} {
  if (limits_.maximum_events == 0 || limits_.maximum_barriers == 0
      || limits_.maximum_waiters == 0 || limits_.maximum_callbacks == 0
      || limits_.maximum_pools == 0 || limits_.maximum_pool_entries == 0
      || limits_.maximum_queues == 0 || limits_.maximum_queue_entries == 0
      || limits_.maximum_heartbeats == 0
      || limits_.maximum_heartbeat_participants == 0
      || limits_.maximum_callback_failures == 0
      || limits_.maximum_spell_candidates == 0
      || limits_.maximum_text_bytes == 0 || limits_.maximum_mutations == 0) {
    fail(kInvalidPolicyObject, "UVM policy resource limits must be nonzero");
  }
}

void SystemVerilogUvmSynchronizationService::set_scheduler(
    Scheduler& scheduler) noexcept {
  scheduler_ = &scheduler;
}

void SystemVerilogUvmSynchronizationService::validate_text(
    const std::string_view text) const {
  if (text.empty() || text.size() > limits_.maximum_text_bytes) {
    fail(kPolicyResource, "UVM policy text is empty or exceeds its ceiling");
  }
}

void SystemVerilogUvmSynchronizationService::mutate() {
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kPolicyResource, "UVM policy mutation ceiling was exceeded");
  }
  ++mutations_;
}

SystemVerilogUvmSynchronizationService::EventEntry&
SystemVerilogUvmSynchronizationService::event_entry(
    const SystemVerilogUvmEventHandle event) {
  const auto found = events_.find(event);
  if (event == 0 || found == events_.end()) {
    fail(kInvalidPolicyObject, "UVM event handle is empty or stale");
  }
  return found->second;
}

const SystemVerilogUvmSynchronizationService::EventEntry&
SystemVerilogUvmSynchronizationService::event_entry(
    const SystemVerilogUvmEventHandle event) const {
  const auto found = events_.find(event);
  if (event == 0 || found == events_.end()) {
    fail(kInvalidPolicyObject, "UVM event handle is empty or stale");
  }
  return found->second;
}

SystemVerilogUvmSynchronizationService::BarrierEntry&
SystemVerilogUvmSynchronizationService::barrier_entry(
    const SystemVerilogUvmBarrierHandle barrier) {
  const auto found = barriers_.find(barrier);
  if (barrier == 0 || found == barriers_.end()) {
    fail(kInvalidPolicyObject, "UVM barrier handle is empty or stale");
  }
  return found->second;
}

const SystemVerilogUvmSynchronizationService::BarrierEntry&
SystemVerilogUvmSynchronizationService::barrier_entry(
    const SystemVerilogUvmBarrierHandle barrier) const {
  const auto found = barriers_.find(barrier);
  if (barrier == 0 || found == barriers_.end()) {
    fail(kInvalidPolicyObject, "UVM barrier handle is empty or stale");
  }
  return found->second;
}

SystemVerilogUvmSynchronizationService::PoolEntry&
SystemVerilogUvmSynchronizationService::pool_entry(
    const SystemVerilogUvmPolicyPoolHandle pool) {
  const auto found = pools_.find(pool);
  if (pool == 0 || found == pools_.end()) {
    fail(kInvalidPolicyObject, "UVM pool handle is empty or stale");
  }
  return found->second;
}

const SystemVerilogUvmSynchronizationService::PoolEntry&
SystemVerilogUvmSynchronizationService::pool_entry(
    const SystemVerilogUvmPolicyPoolHandle pool) const {
  const auto found = pools_.find(pool);
  if (pool == 0 || found == pools_.end()) {
    fail(kInvalidPolicyObject, "UVM pool handle is empty or stale");
  }
  return found->second;
}

SystemVerilogUvmSynchronizationService::QueueEntry&
SystemVerilogUvmSynchronizationService::queue_entry(
    const SystemVerilogUvmPolicyQueueHandle queue) {
  const auto found = queues_.find(queue);
  if (queue == 0 || found == queues_.end()) {
    fail(kInvalidPolicyObject, "UVM queue handle is empty or stale");
  }
  return found->second;
}

const SystemVerilogUvmSynchronizationService::QueueEntry&
SystemVerilogUvmSynchronizationService::queue_entry(
    const SystemVerilogUvmPolicyQueueHandle queue) const {
  const auto found = queues_.find(queue);
  if (queue == 0 || found == queues_.end()) {
    fail(kInvalidPolicyObject, "UVM queue handle is empty or stale");
  }
  return found->second;
}

SystemVerilogUvmSynchronizationService::HeartbeatEntry&
SystemVerilogUvmSynchronizationService::heartbeat_entry(
    const SystemVerilogUvmHeartbeatHandle heartbeat) {
  const auto found = heartbeats_.find(heartbeat);
  if (heartbeat == 0 || found == heartbeats_.end()) {
    fail(kInvalidPolicyObject, "UVM heartbeat handle is empty or stale");
  }
  return found->second;
}

const SystemVerilogUvmSynchronizationService::HeartbeatEntry&
SystemVerilogUvmSynchronizationService::heartbeat_entry(
    const SystemVerilogUvmHeartbeatHandle heartbeat) const {
  const auto found = heartbeats_.find(heartbeat);
  if (heartbeat == 0 || found == heartbeats_.end()) {
    fail(kInvalidPolicyObject, "UVM heartbeat handle is empty or stale");
  }
  return found->second;
}

SystemVerilogUvmEventHandle SystemVerilogUvmSynchronizationService::event(
    std::string name) {
  validate_text(name);
  if (const auto found = event_names_.find(name); found != event_names_.end()) {
    return found->second;
  }
  if (events_.size() >= limits_.maximum_events) {
    fail(kPolicyResource, "UVM event-pool ceiling was exceeded");
  }
  mutate();
  const auto identity = next_identity_++;
  EventEntry stored;
  stored.snapshot.identity = identity;
  stored.snapshot.name = std::move(name);
  event_names_.emplace(stored.snapshot.name, identity);
  events_.emplace(identity, std::move(stored));
  return identity;
}

std::optional<SystemVerilogUvmEventHandle>
SystemVerilogUvmSynchronizationService::find_event(
    const std::string_view name) const noexcept {
  const auto found = event_names_.find(name);
  return found == event_names_.end()
      ? std::nullopt
      : std::optional<SystemVerilogUvmEventHandle>{found->second};
}

SystemVerilogUvmEventSnapshot
SystemVerilogUvmSynchronizationService::event_snapshot(
    const SystemVerilogUvmEventHandle event) const {
  return event_entry(event).snapshot;
}

void SystemVerilogUvmSynchronizationService::complete_waiters(
    std::vector<Waiter> waiters,
    const SystemVerilogUvmWaitOutcome outcome,
    const SystemVerilogClassHandle data) {
  for (auto& waiter : waiters) {
    if (!waiter.completion) continue;
    try {
      waiter.completion(outcome, data);
    } catch (const std::exception& error) {
      if (callback_failures_.size() == limits_.maximum_callback_failures) {
        callback_failures_.erase(callback_failures_.begin());
      }
      callback_failures_.push_back(error.what());
    } catch (...) {
      if (callback_failures_.size() == limits_.maximum_callback_failures) {
        callback_failures_.erase(callback_failures_.begin());
      }
      callback_failures_.push_back("unknown UVM policy callback failure");
    }
  }
}

SystemVerilogUvmPolicyWaitHandle
SystemVerilogUvmSynchronizationService::wait_event(
    const SystemVerilogUvmEventHandle event,
    const std::uint64_t process,
    const SystemVerilogUvmEventWaitKind kind,
    SystemVerilogUvmWaitCompletion completion) {
  auto& stored = event_entry(event);
  if (process == 0 || static_cast<std::uint8_t>(kind)
          > static_cast<std::uint8_t>(SystemVerilogUvmEventWaitKind::Off)) {
    fail(kInvalidPolicyObject, "UVM event waiter process or kind is invalid");
  }
  const auto immediate =
      ((kind == SystemVerilogUvmEventWaitKind::On
        || kind == SystemVerilogUvmEventWaitKind::PersistentTrigger)
       && stored.snapshot.on)
      || (kind == SystemVerilogUvmEventWaitKind::Off && !stored.snapshot.on);
  if (immediate) {
    mutate();
    std::vector<Waiter> waiters{{0, process, kind, std::move(completion)}};
    complete_waiters(
        std::move(waiters), SystemVerilogUvmWaitOutcome::Triggered,
        stored.snapshot.trigger_data);
    return 0;
  }
  if (waiter_count_ >= limits_.maximum_waiters
      || std::ranges::any_of(stored.waiters, [&](const auto& entry) {
           return entry.second.process == process;
         })) {
    fail(waiter_count_ >= limits_.maximum_waiters ? kPolicyResource
                                                   : kInvalidPolicyObject,
         "UVM event waiter ceiling or process ownership was violated");
  }
  mutate();
  const auto handle = next_waiter_++;
  stored.waiters.emplace(handle, Waiter{handle, process, kind,
                                        std::move(completion)});
  ++waiter_count_;
  stored.snapshot.waiter_count = stored.waiters.size();
  return handle;
}

bool SystemVerilogUvmSynchronizationService::cancel_wait(
    const SystemVerilogUvmPolicyWaitHandle waiter) {
  if (waiter == 0) return false;
  for (auto& [identity, event] : events_) {
    (void)identity;
    const auto found = event.waiters.find(waiter);
    if (found == event.waiters.end()) continue;
    auto completion = std::move(found->second);
    event.waiters.erase(found);
    --waiter_count_;
    event.snapshot.waiter_count = event.waiters.size();
    mutate();
    complete_waiters(
        {std::move(completion)}, SystemVerilogUvmWaitOutcome::Cancelled, 0);
    return true;
  }
  for (auto& [identity, barrier] : barriers_) {
    (void)identity;
    const auto found = barrier.waiters.find(waiter);
    if (found == barrier.waiters.end()) continue;
    auto completion = std::move(found->second);
    barrier.waiters.erase(found);
    --waiter_count_;
    barrier.snapshot.waiter_count = barrier.waiters.size();
    mutate();
    complete_waiters(
        {std::move(completion)}, SystemVerilogUvmWaitOutcome::Cancelled, 0);
    return true;
  }
  return false;
}

SystemVerilogUvmEventCallbackToken
SystemVerilogUvmSynchronizationService::add_event_callback(
    const SystemVerilogUvmEventHandle event,
    SystemVerilogUvmEventCallback callback) {
  auto& stored = event_entry(event);
  if (!callback) {
    fail(kInvalidPolicyObject, "UVM event callback is empty");
  }
  if (callback_count_ >= limits_.maximum_callbacks) {
    fail(kPolicyResource, "UVM event callback ceiling was exceeded");
  }
  mutate();
  const auto token = next_callback_++;
  stored.callbacks.emplace(token, std::move(callback));
  ++callback_count_;
  stored.snapshot.callback_count = stored.callbacks.size();
  return token;
}

bool SystemVerilogUvmSynchronizationService::remove_event_callback(
    const SystemVerilogUvmEventHandle event,
    const SystemVerilogUvmEventCallbackToken token) noexcept {
  const auto selected = events_.find(event);
  if (selected == events_.end()) return false;
  const auto erased = selected->second.callbacks.erase(token);
  if (erased != 0) {
    --callback_count_;
    selected->second.snapshot.callback_count =
        selected->second.callbacks.size();
    if (mutations_ < limits_.maximum_mutations) ++mutations_;
  }
  return erased != 0;
}

void SystemVerilogUvmSynchronizationService::trigger_event(
    const SystemVerilogUvmEventHandle event,
    const SystemVerilogClassHandle data) {
  auto& stored = event_entry(event);
  if (data != 0 && !objects_->contains(data)) {
    fail(kInvalidPolicyObject, "UVM event trigger data object is stale");
  }
  mutate();
  stored.snapshot.on = true;
  stored.snapshot.trigger_data = data;
  stored.snapshot.trigger_time = scheduler_ ? scheduler_->now() : 0;
  stored.snapshot.trigger_delta = scheduler_ ? scheduler_->delta() : 0;
  ++stored.snapshot.trigger_count;
  std::vector<Waiter> ready;
  for (auto iterator = stored.waiters.begin(); iterator != stored.waiters.end();) {
    if (iterator->second.kind == SystemVerilogUvmEventWaitKind::Off) {
      ++iterator;
      continue;
    }
    ready.push_back(std::move(iterator->second));
    iterator = stored.waiters.erase(iterator);
    --waiter_count_;
  }
  stored.snapshot.waiter_count = stored.waiters.size();
  std::vector<SystemVerilogUvmEventCallback> callbacks;
  callbacks.reserve(stored.callbacks.size());
  for (const auto& [token, callback] : stored.callbacks) {
    (void)token;
    callbacks.push_back(callback);
  }
  for (const auto& callback : callbacks) {
    try {
      callback(event, data);
    } catch (const std::exception& error) {
      if (callback_failures_.size() == limits_.maximum_callback_failures) {
        callback_failures_.erase(callback_failures_.begin());
      }
      callback_failures_.push_back(error.what());
    } catch (...) {
      if (callback_failures_.size() == limits_.maximum_callback_failures) {
        callback_failures_.erase(callback_failures_.begin());
      }
      callback_failures_.push_back("unknown UVM event callback failure");
    }
  }
  complete_waiters(
      std::move(ready), SystemVerilogUvmWaitOutcome::Triggered, data);
  for (auto& [heartbeat, value] : heartbeats_) {
    (void)heartbeat;
    if (value.snapshot.active && value.snapshot.event == event) {
      (void)heartbeat_check(value.snapshot.identity);
    }
  }
}

void SystemVerilogUvmSynchronizationService::reset_event(
    const SystemVerilogUvmEventHandle event, const bool wakeup) {
  auto& stored = event_entry(event);
  mutate();
  stored.snapshot.on = false;
  stored.snapshot.trigger_data = 0;
  std::vector<Waiter> ready;
  for (auto iterator = stored.waiters.begin(); iterator != stored.waiters.end();) {
    if (!wakeup && iterator->second.kind != SystemVerilogUvmEventWaitKind::Off) {
      ++iterator;
      continue;
    }
    ready.push_back(std::move(iterator->second));
    iterator = stored.waiters.erase(iterator);
    --waiter_count_;
  }
  stored.snapshot.waiter_count = stored.waiters.size();
  complete_waiters(
      std::move(ready), wakeup ? SystemVerilogUvmWaitOutcome::Reset
                               : SystemVerilogUvmWaitOutcome::Triggered,
      0);
}

bool SystemVerilogUvmSynchronizationService::erase_event(
    const SystemVerilogUvmEventHandle event) {
  auto& stored = event_entry(event);
  if (!stored.waiters.empty() || !stored.callbacks.empty()
      || std::ranges::any_of(heartbeats_, [&](const auto& entry) {
           return entry.second.snapshot.event == event;
         })) {
    fail(kInvalidPolicyObject, "referenced UVM event cannot be erased");
  }
  mutate();
  event_names_.erase(stored.snapshot.name);
  return events_.erase(event) != 0;
}

SystemVerilogUvmBarrierHandle
SystemVerilogUvmSynchronizationService::create_barrier(
    std::string name, const std::size_t threshold, const bool auto_reset) {
  validate_text(name);
  if (threshold == 0) {
    fail(kInvalidPolicyObject, "UVM barrier threshold must be nonzero");
  }
  if (barriers_.size() >= limits_.maximum_barriers) {
    fail(kPolicyResource, "UVM barrier ceiling was exceeded");
  }
  mutate();
  const auto identity = next_identity_++;
  BarrierEntry stored;
  stored.snapshot.identity = identity;
  stored.snapshot.name = std::move(name);
  stored.snapshot.threshold = threshold;
  stored.snapshot.auto_reset = auto_reset;
  barriers_.emplace(identity, std::move(stored));
  return identity;
}

SystemVerilogUvmBarrierSnapshot
SystemVerilogUvmSynchronizationService::barrier_snapshot(
    const SystemVerilogUvmBarrierHandle barrier) const {
  return barrier_entry(barrier).snapshot;
}

void SystemVerilogUvmSynchronizationService::release_barrier(
    BarrierEntry& barrier) {
  std::vector<Waiter> ready;
  ready.reserve(barrier.waiters.size());
  for (auto& [handle, waiter] : barrier.waiters) {
    (void)handle;
    ready.push_back(std::move(waiter));
  }
  waiter_count_ -= barrier.waiters.size();
  barrier.waiters.clear();
  barrier.snapshot.waiter_count = 0;
  ++barrier.snapshot.release_count;
  barrier.snapshot.open = !barrier.snapshot.auto_reset;
  complete_waiters(
      std::move(ready), SystemVerilogUvmWaitOutcome::BarrierReleased, 0);
}

SystemVerilogUvmPolicyWaitHandle
SystemVerilogUvmSynchronizationService::wait_barrier(
    const SystemVerilogUvmBarrierHandle barrier,
    const std::uint64_t process,
    SystemVerilogUvmWaitCompletion completion) {
  auto& stored = barrier_entry(barrier);
  if (process == 0) {
    fail(kInvalidPolicyObject, "UVM barrier waiter process is empty");
  }
  if (stored.snapshot.open) {
    mutate();
    complete_waiters(
        {{0, process, SystemVerilogUvmEventWaitKind::Trigger,
          std::move(completion)}},
        SystemVerilogUvmWaitOutcome::BarrierReleased, 0);
    return 0;
  }
  if (waiter_count_ >= limits_.maximum_waiters
      || std::ranges::any_of(stored.waiters, [&](const auto& entry) {
           return entry.second.process == process;
         })) {
    fail(waiter_count_ >= limits_.maximum_waiters ? kPolicyResource
                                                   : kInvalidPolicyObject,
         "UVM barrier waiter ceiling or process ownership was violated");
  }
  mutate();
  const auto handle = next_waiter_++;
  stored.waiters.emplace(
      handle,
      Waiter{handle, process, SystemVerilogUvmEventWaitKind::Trigger,
             std::move(completion)});
  ++waiter_count_;
  stored.snapshot.waiter_count = stored.waiters.size();
  if (stored.waiters.size() >= stored.snapshot.threshold) {
    release_barrier(stored);
  }
  return handle;
}

void SystemVerilogUvmSynchronizationService::set_barrier_threshold(
    const SystemVerilogUvmBarrierHandle barrier, const std::size_t threshold) {
  auto& stored = barrier_entry(barrier);
  if (threshold == 0) {
    fail(kInvalidPolicyObject, "UVM barrier threshold must be nonzero");
  }
  mutate();
  stored.snapshot.threshold = threshold;
  if (!stored.snapshot.open && stored.waiters.size() >= threshold) {
    release_barrier(stored);
  }
}

void SystemVerilogUvmSynchronizationService::reset_barrier(
    const SystemVerilogUvmBarrierHandle barrier, const bool wakeup) {
  auto& stored = barrier_entry(barrier);
  mutate();
  stored.snapshot.open = false;
  std::vector<Waiter> removed;
  removed.reserve(stored.waiters.size());
  for (auto& [handle, waiter] : stored.waiters) {
    (void)handle;
    removed.push_back(std::move(waiter));
  }
  waiter_count_ -= stored.waiters.size();
  stored.waiters.clear();
  stored.snapshot.waiter_count = 0;
  if (wakeup) {
    complete_waiters(
        std::move(removed), SystemVerilogUvmWaitOutcome::Reset, 0);
  }
}

bool SystemVerilogUvmSynchronizationService::erase_barrier(
    const SystemVerilogUvmBarrierHandle barrier) {
  if (!barrier_entry(barrier).waiters.empty()) {
    fail(kInvalidPolicyObject, "active UVM barrier cannot be erased");
  }
  mutate();
  return barriers_.erase(barrier) != 0;
}

SystemVerilogUvmPolicyPoolHandle
SystemVerilogUvmSynchronizationService::create_pool(std::string name) {
  validate_text(name);
  if (pools_.size() >= limits_.maximum_pools) {
    fail(kPolicyResource, "UVM pool ceiling was exceeded");
  }
  mutate();
  const auto identity = next_identity_++;
  pools_.emplace(identity, PoolEntry{std::move(name), {}});
  return identity;
}

void SystemVerilogUvmSynchronizationService::pool_put(
    const SystemVerilogUvmPolicyPoolHandle pool,
    std::string key,
    SystemVerilogUvmPackItem value,
    const bool replace) {
  auto& stored = pool_entry(pool);
  validate_text(key);
  if (item_text_bytes(value) > limits_.maximum_text_bytes) {
    fail(kPolicyResource, "UVM pool value text exceeds its ceiling");
  }
  const auto found = stored.entries.find(key);
  if (found != stored.entries.end()) {
    if (!replace) {
      fail(kInvalidPolicyObject, "UVM pool key already exists");
    }
    mutate();
    found->second = std::move(value);
    return;
  }
  if (pool_entry_count_ >= limits_.maximum_pool_entries) {
    fail(kPolicyResource, "UVM pool-entry ceiling was exceeded");
  }
  mutate();
  stored.entries.emplace(std::move(key), std::move(value));
  ++pool_entry_count_;
}

std::optional<SystemVerilogUvmPackItem>
SystemVerilogUvmSynchronizationService::pool_get(
    const SystemVerilogUvmPolicyPoolHandle pool,
    const std::string_view key) const {
  const auto& stored = pool_entry(pool);
  const auto found = stored.entries.find(key);
  return found == stored.entries.end()
      ? std::nullopt
      : std::optional<SystemVerilogUvmPackItem>{found->second};
}

bool SystemVerilogUvmSynchronizationService::pool_erase(
    const SystemVerilogUvmPolicyPoolHandle pool,
    const std::string_view key) {
  auto& stored = pool_entry(pool);
  const auto found = stored.entries.find(key);
  if (found == stored.entries.end()) return false;
  mutate();
  stored.entries.erase(found);
  --pool_entry_count_;
  return true;
}

std::vector<std::string> SystemVerilogUvmSynchronizationService::pool_keys(
    const SystemVerilogUvmPolicyPoolHandle pool) const {
  std::vector<std::string> keys;
  const auto& stored = pool_entry(pool);
  keys.reserve(stored.entries.size());
  for (const auto& [key, value] : stored.entries) {
    (void)value;
    keys.push_back(key);
  }
  return keys;
}

SystemVerilogUvmPolicyQueueHandle
SystemVerilogUvmSynchronizationService::create_queue(std::string name) {
  validate_text(name);
  if (queues_.size() >= limits_.maximum_queues) {
    fail(kPolicyResource, "UVM queue ceiling was exceeded");
  }
  mutate();
  const auto identity = next_identity_++;
  queues_.emplace(identity, QueueEntry{std::move(name), {}});
  return identity;
}

void SystemVerilogUvmSynchronizationService::queue_push_back(
    const SystemVerilogUvmPolicyQueueHandle queue,
    SystemVerilogUvmPackItem value) {
  auto& stored = queue_entry(queue);
  if (queue_entry_count_ >= limits_.maximum_queue_entries
      || item_text_bytes(value) > limits_.maximum_text_bytes) {
    fail(kPolicyResource, "UVM queue entry or text ceiling was exceeded");
  }
  mutate();
  stored.entries.push_back(std::move(value));
  ++queue_entry_count_;
}

void SystemVerilogUvmSynchronizationService::queue_push_front(
    const SystemVerilogUvmPolicyQueueHandle queue,
    SystemVerilogUvmPackItem value) {
  queue_insert(queue, 0, std::move(value));
}

void SystemVerilogUvmSynchronizationService::queue_insert(
    const SystemVerilogUvmPolicyQueueHandle queue,
    const std::size_t index,
    SystemVerilogUvmPackItem value) {
  auto& stored = queue_entry(queue);
  if (index > stored.entries.size()) {
    fail(kInvalidPolicyObject, "UVM queue insertion index is out of range");
  }
  if (queue_entry_count_ >= limits_.maximum_queue_entries
      || item_text_bytes(value) > limits_.maximum_text_bytes) {
    fail(kPolicyResource, "UVM queue entry or text ceiling was exceeded");
  }
  mutate();
  stored.entries.insert(
      stored.entries.begin() + static_cast<std::ptrdiff_t>(index),
      std::move(value));
  ++queue_entry_count_;
}

std::optional<SystemVerilogUvmPackItem>
SystemVerilogUvmSynchronizationService::queue_pop_back(
    const SystemVerilogUvmPolicyQueueHandle queue) {
  auto& stored = queue_entry(queue);
  if (stored.entries.empty()) return std::nullopt;
  mutate();
  auto value = std::move(stored.entries.back());
  stored.entries.pop_back();
  --queue_entry_count_;
  return value;
}

std::optional<SystemVerilogUvmPackItem>
SystemVerilogUvmSynchronizationService::queue_pop_front(
    const SystemVerilogUvmPolicyQueueHandle queue) {
  auto& stored = queue_entry(queue);
  if (stored.entries.empty()) return std::nullopt;
  mutate();
  auto value = std::move(stored.entries.front());
  stored.entries.erase(stored.entries.begin());
  --queue_entry_count_;
  return value;
}

std::optional<SystemVerilogUvmPackItem>
SystemVerilogUvmSynchronizationService::queue_get(
    const SystemVerilogUvmPolicyQueueHandle queue,
    const std::size_t index) const {
  const auto& stored = queue_entry(queue);
  return index >= stored.entries.size()
      ? std::nullopt
      : std::optional<SystemVerilogUvmPackItem>{stored.entries[index]};
}

bool SystemVerilogUvmSynchronizationService::queue_erase(
    const SystemVerilogUvmPolicyQueueHandle queue, const std::size_t index) {
  auto& stored = queue_entry(queue);
  if (index >= stored.entries.size()) return false;
  mutate();
  stored.entries.erase(
      stored.entries.begin() + static_cast<std::ptrdiff_t>(index));
  --queue_entry_count_;
  return true;
}

std::size_t SystemVerilogUvmSynchronizationService::queue_size(
    const SystemVerilogUvmPolicyQueueHandle queue) const {
  return queue_entry(queue).entries.size();
}

SystemVerilogUvmHeartbeatHandle
SystemVerilogUvmSynchronizationService::create_heartbeat(
    std::string name,
    const SystemVerilogUvmEventHandle event,
    const SystemVerilogUvmHeartbeatMode mode) {
  validate_text(name);
  (void)event_entry(event);
  if (static_cast<std::uint8_t>(mode)
      > static_cast<std::uint8_t>(SystemVerilogUvmHeartbeatMode::One)) {
    fail(kInvalidPolicyObject, "UVM heartbeat mode is invalid");
  }
  if (heartbeats_.size() >= limits_.maximum_heartbeats) {
    fail(kPolicyResource, "UVM heartbeat ceiling was exceeded");
  }
  mutate();
  const auto identity = next_identity_++;
  HeartbeatEntry stored;
  stored.snapshot.identity = identity;
  stored.snapshot.name = std::move(name);
  stored.snapshot.event = event;
  stored.snapshot.mode = mode;
  heartbeats_.emplace(identity, std::move(stored));
  return identity;
}

void SystemVerilogUvmSynchronizationService::heartbeat_add(
    const SystemVerilogUvmHeartbeatHandle heartbeat,
    const SystemVerilogClassHandle participant) {
  auto& stored = heartbeat_entry(heartbeat);
  if (!objects_->contains(participant) || stored.snapshot.active) {
    fail(kInvalidPolicyObject,
         "UVM heartbeat participant is stale or heartbeat is active");
  }
  if (stored.participants.contains(participant)) return;
  if (heartbeat_participant_count_
      >= limits_.maximum_heartbeat_participants) {
    fail(kPolicyResource, "UVM heartbeat participant ceiling was exceeded");
  }
  mutate();
  stored.participants.insert(participant);
  ++heartbeat_participant_count_;
  stored.snapshot.participant_count = stored.participants.size();
}

bool SystemVerilogUvmSynchronizationService::heartbeat_remove(
    const SystemVerilogUvmHeartbeatHandle heartbeat,
    const SystemVerilogClassHandle participant) {
  auto& stored = heartbeat_entry(heartbeat);
  if (stored.snapshot.active) {
    fail(kInvalidPolicyObject, "active UVM heartbeat cannot change members");
  }
  const auto erased = stored.participants.erase(participant);
  if (erased != 0) {
    mutate();
    stored.observed.erase(participant);
    --heartbeat_participant_count_;
    stored.snapshot.participant_count = stored.participants.size();
    stored.snapshot.observed_count = stored.observed.size();
  }
  return erased != 0;
}

void SystemVerilogUvmSynchronizationService::heartbeat_start(
    const SystemVerilogUvmHeartbeatHandle heartbeat) {
  auto& stored = heartbeat_entry(heartbeat);
  if (stored.participants.empty()) {
    fail(kInvalidPolicyObject, "UVM heartbeat has no participants");
  }
  mutate();
  stored.observed.clear();
  stored.snapshot.observed_count = 0;
  stored.snapshot.active = true;
}

void SystemVerilogUvmSynchronizationService::heartbeat_stop(
    const SystemVerilogUvmHeartbeatHandle heartbeat) noexcept {
  const auto found = heartbeats_.find(heartbeat);
  if (found == heartbeats_.end()) return;
  found->second.snapshot.active = false;
  found->second.observed.clear();
  found->second.snapshot.observed_count = 0;
  if (mutations_ < limits_.maximum_mutations) ++mutations_;
}

void SystemVerilogUvmSynchronizationService::heartbeat_beat(
    const SystemVerilogUvmHeartbeatHandle heartbeat,
    const SystemVerilogClassHandle participant) {
  auto& stored = heartbeat_entry(heartbeat);
  if (!stored.snapshot.active || !stored.participants.contains(participant)
      || !objects_->contains(participant)) {
    fail(kInvalidPolicyObject,
         "UVM heartbeat beat is inactive, unregistered, or stale");
  }
  mutate();
  stored.observed.insert(participant);
  stored.snapshot.observed_count = stored.observed.size();
}

bool SystemVerilogUvmSynchronizationService::heartbeat_check(
    const SystemVerilogUvmHeartbeatHandle heartbeat) {
  auto& stored = heartbeat_entry(heartbeat);
  if (!stored.snapshot.active) {
    fail(kInvalidPolicyObject, "UVM heartbeat is not active");
  }
  mutate();
  ++stored.snapshot.checks;
  const auto success =
      stored.snapshot.mode == SystemVerilogUvmHeartbeatMode::Any
          ? !stored.observed.empty()
          : stored.snapshot.mode == SystemVerilogUvmHeartbeatMode::All
              ? stored.observed.size() == stored.participants.size()
              : stored.observed.size() == 1;
  if (!success) ++stored.snapshot.failures;
  stored.observed.clear();
  stored.snapshot.observed_count = 0;
  return success;
}

SystemVerilogUvmHeartbeatSnapshot
SystemVerilogUvmSynchronizationService::heartbeat_snapshot(
    const SystemVerilogUvmHeartbeatHandle heartbeat) const {
  return heartbeat_entry(heartbeat).snapshot;
}

std::vector<std::string>
SystemVerilogUvmSynchronizationService::spell_challenge(
    const std::string_view word,
    const std::vector<std::string>& candidates,
    const std::size_t maximum_distance) const {
  validate_text(word);
  if (maximum_distance > limits_.maximum_spell_distance
      || candidates.size() > limits_.maximum_pool_entries) {
    fail(kPolicyResource, "UVM spell challenge exceeds its work ceiling");
  }
  std::vector<std::pair<std::size_t, std::string>> ordered;
  for (const auto& candidate : candidates) {
    validate_text(candidate);
    const auto distance = edit_distance(word, candidate);
    if (distance <= maximum_distance) {
      ordered.emplace_back(distance, candidate);
    }
  }
  std::sort(ordered.begin(), ordered.end());
  if (ordered.size() > limits_.maximum_spell_candidates) {
    ordered.resize(limits_.maximum_spell_candidates);
  }
  std::vector<std::string> result;
  result.reserve(ordered.size());
  for (auto& [distance, candidate] : ordered) {
    (void)distance;
    result.push_back(std::move(candidate));
  }
  return result;
}

void SystemVerilogUvmSynchronizationService::reset() {
  mutate();
  std::vector<Waiter> waiters;
  for (auto& [identity, event] : events_) {
    (void)identity;
    for (auto& [handle, waiter] : event.waiters) {
      (void)handle;
      waiters.push_back(std::move(waiter));
    }
  }
  for (auto& [identity, barrier] : barriers_) {
    (void)identity;
    for (auto& [handle, waiter] : barrier.waiters) {
      (void)handle;
      waiters.push_back(std::move(waiter));
    }
  }
  events_.clear();
  event_names_.clear();
  barriers_.clear();
  pools_.clear();
  queues_.clear();
  heartbeats_.clear();
  callback_failures_.clear();
  waiter_count_ = 0;
  callback_count_ = 0;
  pool_entry_count_ = 0;
  queue_entry_count_ = 0;
  heartbeat_participant_count_ = 0;
  complete_waiters(
      std::move(waiters), SystemVerilogUvmWaitOutcome::Cancelled, 0);
}

}  // namespace fsim::runtime

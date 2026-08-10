// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/uvm_packer.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

using SystemVerilogUvmEventHandle = std::uint64_t;
using SystemVerilogUvmBarrierHandle = std::uint64_t;
using SystemVerilogUvmPolicyWaitHandle = std::uint64_t;
using SystemVerilogUvmEventCallbackToken = std::uint64_t;
using SystemVerilogUvmPolicyPoolHandle = std::uint64_t;
using SystemVerilogUvmPolicyQueueHandle = std::uint64_t;
using SystemVerilogUvmHeartbeatHandle = std::uint64_t;

enum class SystemVerilogUvmEventWaitKind : std::uint8_t {
  Trigger,
  PersistentTrigger,
  On,
  Off,
};

enum class SystemVerilogUvmWaitOutcome : std::uint8_t {
  Triggered,
  Reset,
  Cancelled,
  BarrierReleased,
};

enum class SystemVerilogUvmHeartbeatMode : std::uint8_t {
  Any,
  All,
  One,
};

using SystemVerilogUvmWaitCompletion = std::function<void(
    SystemVerilogUvmWaitOutcome, SystemVerilogClassHandle)>;
using SystemVerilogUvmEventCallback = std::function<void(
    SystemVerilogUvmEventHandle, SystemVerilogClassHandle)>;

struct SystemVerilogUvmPolicyLimits {
  std::size_t maximum_events{4096};
  std::size_t maximum_barriers{4096};
  std::size_t maximum_waiters{1U << 20U};
  std::size_t maximum_callbacks{1U << 16U};
  std::size_t maximum_pools{4096};
  std::size_t maximum_pool_entries{1U << 20U};
  std::size_t maximum_queues{4096};
  std::size_t maximum_queue_entries{1U << 20U};
  std::size_t maximum_heartbeats{4096};
  std::size_t maximum_heartbeat_participants{1U << 20U};
  std::size_t maximum_callback_failures{4096};
  std::size_t maximum_spell_candidates{256};
  std::size_t maximum_spell_distance{256};
  std::size_t maximum_text_bytes{1U << 20U};
  std::size_t maximum_mutations{1U << 24U};
};

struct SystemVerilogUvmEventSnapshot {
  std::uint64_t identity{};
  std::string name;
  bool on{};
  std::uint64_t trigger_count{};
  SystemVerilogClassHandle trigger_data{};
  SimulationTick trigger_time{};
  std::uint64_t trigger_delta{};
  std::size_t waiter_count{};
  std::size_t callback_count{};
};

struct SystemVerilogUvmBarrierSnapshot {
  std::uint64_t identity{};
  std::string name;
  std::size_t threshold{1};
  std::size_t waiter_count{};
  std::uint64_t release_count{};
  bool auto_reset{true};
  bool open{};
};

struct SystemVerilogUvmHeartbeatSnapshot {
  std::uint64_t identity{};
  std::string name;
  SystemVerilogUvmEventHandle event{};
  SystemVerilogUvmHeartbeatMode mode{SystemVerilogUvmHeartbeatMode::Any};
  bool active{};
  std::size_t participant_count{};
  std::size_t observed_count{};
  std::uint64_t checks{};
  std::uint64_t failures{};
};

class SystemVerilogUvmPolicyError final : public std::runtime_error {
 public:
  SystemVerilogUvmPolicyError(std::string code, std::string message);
  [[nodiscard]] const std::string& diagnostic_code() const noexcept {
    return diagnostic_code_;
  }

 private:
  std::string diagnostic_code_;
};

class SystemVerilogUvmSynchronizationService final {
 public:
  explicit SystemVerilogUvmSynchronizationService(
      SystemVerilogUvmObjectService& objects,
      SystemVerilogUvmPolicyLimits limits = {});

  void set_scheduler(Scheduler& scheduler) noexcept;

  [[nodiscard]] SystemVerilogUvmEventHandle event(std::string name);
  [[nodiscard]] std::optional<SystemVerilogUvmEventHandle> find_event(
      std::string_view name) const noexcept;
  [[nodiscard]] SystemVerilogUvmEventSnapshot event_snapshot(
      SystemVerilogUvmEventHandle event) const;
  [[nodiscard]] SystemVerilogUvmPolicyWaitHandle wait_event(
      SystemVerilogUvmEventHandle event,
      std::uint64_t process,
      SystemVerilogUvmEventWaitKind kind,
      SystemVerilogUvmWaitCompletion completion = {});
  [[nodiscard]] bool cancel_wait(SystemVerilogUvmPolicyWaitHandle waiter);
  [[nodiscard]] SystemVerilogUvmEventCallbackToken add_event_callback(
      SystemVerilogUvmEventHandle event, SystemVerilogUvmEventCallback callback);
  [[nodiscard]] bool remove_event_callback(
      SystemVerilogUvmEventHandle event,
      SystemVerilogUvmEventCallbackToken token) noexcept;
  void trigger_event(
      SystemVerilogUvmEventHandle event,
      SystemVerilogClassHandle data = 0);
  void reset_event(SystemVerilogUvmEventHandle event, bool wakeup = false);
  [[nodiscard]] bool erase_event(SystemVerilogUvmEventHandle event);

  [[nodiscard]] SystemVerilogUvmBarrierHandle create_barrier(
      std::string name,
      std::size_t threshold = 1,
      bool auto_reset = true);
  [[nodiscard]] SystemVerilogUvmBarrierSnapshot barrier_snapshot(
      SystemVerilogUvmBarrierHandle barrier) const;
  [[nodiscard]] SystemVerilogUvmPolicyWaitHandle wait_barrier(
      SystemVerilogUvmBarrierHandle barrier,
      std::uint64_t process,
      SystemVerilogUvmWaitCompletion completion = {});
  void set_barrier_threshold(
      SystemVerilogUvmBarrierHandle barrier, std::size_t threshold);
  void reset_barrier(
      SystemVerilogUvmBarrierHandle barrier, bool wakeup = false);
  [[nodiscard]] bool erase_barrier(SystemVerilogUvmBarrierHandle barrier);

  [[nodiscard]] SystemVerilogUvmPolicyPoolHandle create_pool(std::string name);
  void pool_put(
      SystemVerilogUvmPolicyPoolHandle pool,
      std::string key,
      SystemVerilogUvmPackItem value,
      bool replace = true);
  [[nodiscard]] std::optional<SystemVerilogUvmPackItem> pool_get(
      SystemVerilogUvmPolicyPoolHandle pool, std::string_view key) const;
  [[nodiscard]] bool pool_erase(
      SystemVerilogUvmPolicyPoolHandle pool, std::string_view key);
  [[nodiscard]] std::vector<std::string> pool_keys(
      SystemVerilogUvmPolicyPoolHandle pool) const;

  [[nodiscard]] SystemVerilogUvmPolicyQueueHandle create_queue(std::string name);
  void queue_push_back(
      SystemVerilogUvmPolicyQueueHandle queue,
      SystemVerilogUvmPackItem value);
  void queue_push_front(
      SystemVerilogUvmPolicyQueueHandle queue,
      SystemVerilogUvmPackItem value);
  void queue_insert(
      SystemVerilogUvmPolicyQueueHandle queue,
      std::size_t index,
      SystemVerilogUvmPackItem value);
  [[nodiscard]] std::optional<SystemVerilogUvmPackItem> queue_pop_back(
      SystemVerilogUvmPolicyQueueHandle queue);
  [[nodiscard]] std::optional<SystemVerilogUvmPackItem> queue_pop_front(
      SystemVerilogUvmPolicyQueueHandle queue);
  [[nodiscard]] std::optional<SystemVerilogUvmPackItem> queue_get(
      SystemVerilogUvmPolicyQueueHandle queue, std::size_t index) const;
  [[nodiscard]] bool queue_erase(
      SystemVerilogUvmPolicyQueueHandle queue, std::size_t index);
  [[nodiscard]] std::size_t queue_size(
      SystemVerilogUvmPolicyQueueHandle queue) const;

  [[nodiscard]] SystemVerilogUvmHeartbeatHandle create_heartbeat(
      std::string name,
      SystemVerilogUvmEventHandle event,
      SystemVerilogUvmHeartbeatMode mode);
  void heartbeat_add(
      SystemVerilogUvmHeartbeatHandle heartbeat,
      SystemVerilogClassHandle participant);
  [[nodiscard]] bool heartbeat_remove(
      SystemVerilogUvmHeartbeatHandle heartbeat,
      SystemVerilogClassHandle participant);
  void heartbeat_start(SystemVerilogUvmHeartbeatHandle heartbeat);
  void heartbeat_stop(SystemVerilogUvmHeartbeatHandle heartbeat) noexcept;
  void heartbeat_beat(
      SystemVerilogUvmHeartbeatHandle heartbeat,
      SystemVerilogClassHandle participant);
  [[nodiscard]] bool heartbeat_check(SystemVerilogUvmHeartbeatHandle heartbeat);
  [[nodiscard]] SystemVerilogUvmHeartbeatSnapshot heartbeat_snapshot(
      SystemVerilogUvmHeartbeatHandle heartbeat) const;

  [[nodiscard]] std::vector<std::string> spell_challenge(
      std::string_view word,
      const std::vector<std::string>& candidates,
      std::size_t maximum_distance) const;
  [[nodiscard]] const std::vector<std::string>& callback_failures() const noexcept {
    return callback_failures_;
  }
  [[nodiscard]] std::size_t mutations() const noexcept { return mutations_; }
  void reset();

 private:
  struct Waiter {
    SystemVerilogUvmPolicyWaitHandle handle{};
    std::uint64_t process{};
    SystemVerilogUvmEventWaitKind kind{SystemVerilogUvmEventWaitKind::Trigger};
    SystemVerilogUvmWaitCompletion completion;
  };
  struct EventEntry {
    SystemVerilogUvmEventSnapshot snapshot;
    std::map<SystemVerilogUvmPolicyWaitHandle, Waiter> waiters;
    std::map<SystemVerilogUvmEventCallbackToken, SystemVerilogUvmEventCallback>
        callbacks;
  };
  struct BarrierEntry {
    SystemVerilogUvmBarrierSnapshot snapshot;
    std::map<SystemVerilogUvmPolicyWaitHandle, Waiter> waiters;
  };
  struct PoolEntry {
    std::string name;
    std::map<std::string, SystemVerilogUvmPackItem, std::less<>> entries;
  };
  struct QueueEntry {
    std::string name;
    std::vector<SystemVerilogUvmPackItem> entries;
  };
  struct HeartbeatEntry {
    SystemVerilogUvmHeartbeatSnapshot snapshot;
    std::set<SystemVerilogClassHandle> participants;
    std::set<SystemVerilogClassHandle> observed;
  };

  [[nodiscard]] EventEntry& event_entry(SystemVerilogUvmEventHandle event);
  [[nodiscard]] const EventEntry& event_entry(
      SystemVerilogUvmEventHandle event) const;
  [[nodiscard]] BarrierEntry& barrier_entry(
      SystemVerilogUvmBarrierHandle barrier);
  [[nodiscard]] const BarrierEntry& barrier_entry(
      SystemVerilogUvmBarrierHandle barrier) const;
  [[nodiscard]] PoolEntry& pool_entry(SystemVerilogUvmPolicyPoolHandle pool);
  [[nodiscard]] const PoolEntry& pool_entry(
      SystemVerilogUvmPolicyPoolHandle pool) const;
  [[nodiscard]] QueueEntry& queue_entry(SystemVerilogUvmPolicyQueueHandle queue);
  [[nodiscard]] const QueueEntry& queue_entry(
      SystemVerilogUvmPolicyQueueHandle queue) const;
  [[nodiscard]] HeartbeatEntry& heartbeat_entry(
      SystemVerilogUvmHeartbeatHandle heartbeat);
  [[nodiscard]] const HeartbeatEntry& heartbeat_entry(
      SystemVerilogUvmHeartbeatHandle heartbeat) const;
  void validate_text(std::string_view text) const;
  void mutate();
  void complete_waiters(
      std::vector<Waiter> waiters,
      SystemVerilogUvmWaitOutcome outcome,
      SystemVerilogClassHandle data);
  void release_barrier(BarrierEntry& barrier);

  SystemVerilogUvmObjectService* objects_{};
  Scheduler* scheduler_{};
  SystemVerilogUvmPolicyLimits limits_;
  std::map<SystemVerilogUvmEventHandle, EventEntry> events_;
  std::map<std::string, SystemVerilogUvmEventHandle, std::less<>> event_names_;
  std::map<SystemVerilogUvmBarrierHandle, BarrierEntry> barriers_;
  std::map<SystemVerilogUvmPolicyPoolHandle, PoolEntry> pools_;
  std::map<SystemVerilogUvmPolicyQueueHandle, QueueEntry> queues_;
  std::map<SystemVerilogUvmHeartbeatHandle, HeartbeatEntry> heartbeats_;
  std::vector<std::string> callback_failures_;
  std::uint64_t next_identity_{1};
  std::uint64_t next_waiter_{1};
  std::uint64_t next_callback_{1};
  std::size_t waiter_count_{};
  std::size_t callback_count_{};
  std::size_t pool_entry_count_{};
  std::size_t queue_entry_count_{};
  std::size_t heartbeat_participant_count_{};
  std::size_t mutations_{};
};

}  // namespace fsim::runtime

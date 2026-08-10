// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/uvm_activity.hpp"
#include "fsim/runtime/uvm_phase.hpp"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime {

class SystemVerilogUvmObjectionService;

class SystemVerilogUvmObjectionSourceHandle final {
 public:
  SystemVerilogUvmObjectionSourceHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && object_ != 0 && root_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(
      const SystemVerilogUvmObjectionSourceHandle&,
      const SystemVerilogUvmObjectionSourceHandle&) = default;

 private:
  friend class SystemVerilogUvmObjectionService;
  SystemVerilogUvmObjectionSourceHandle(
      std::shared_ptr<const void> owner,
      const SystemVerilogClassHandle object,
      const SystemVerilogUvmRootHandle root)
      : owner_(std::move(owner)), object_(object), root_(root) {}

  std::shared_ptr<const void> owner_;
  SystemVerilogClassHandle object_{};
  SystemVerilogUvmRootHandle root_{};
};

enum class SystemVerilogUvmObjectionOperation : std::uint8_t {
  Raise,
  Drop,
  Set,
};

enum class SystemVerilogUvmObjectionEventKind : std::uint8_t {
  Raised,
  Dropped,
  DrainStarted,
  DrainCancelled,
  DrainCompleted,
  AllDropped,
  ReadyToEnd,
};

struct SystemVerilogUvmObjectionLimits {
  std::size_t maximum_sources{65'536};
  std::size_t maximum_descriptions{65'536};
  std::size_t maximum_entries{262'144};
  std::size_t maximum_description_bytes{4'096};
  std::uint64_t maximum_count{1ULL << 60U};
  std::size_t maximum_propagation_depth{256};
  std::size_t maximum_callbacks_per_operation{512};
  std::size_t maximum_trace_records{1U << 20U};
  std::size_t maximum_trace_output_bytes{16U * 1'024U * 1'024U};
  std::size_t maximum_mutations{1U << 20U};
  std::size_t maximum_drain_settings{65'536};
  std::size_t maximum_pending_drains{65'536};
  SimulationTick maximum_drain_time{1ULL << 50U};
  SimulationTick maximum_aggregate_drain_time{1ULL << 54U};
  std::size_t maximum_callback_reentries{256};
};

class SystemVerilogUvmObjectionError final : public std::runtime_error {
 public:
  SystemVerilogUvmObjectionError(std::string code, std::string message);
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return code_;
  }

 private:
  std::string code_;
};

struct SystemVerilogUvmObjectionEvent {
  SystemVerilogUvmObjectionEventKind kind{
      SystemVerilogUvmObjectionEventKind::Raised};
  SystemVerilogUvmObjectionOperation operation{
      SystemVerilogUvmObjectionOperation::Raise};
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmObjectionSourceHandle source;
  std::string description;
  SystemVerilogClassHandle target{};
  bool root_target{};
  std::uint64_t amount{};
  std::uint64_t source_count{};
  std::uint64_t target_count{};
  std::uint64_t sequence{};
  SimulationTick time{};
  SimulationTick drain_time{};
};

struct SystemVerilogUvmObjectionCallbackFailure {
  std::string diagnostic_code;
  SystemVerilogUvmObjectionEvent event;
  std::string message;
};

struct SystemVerilogUvmObjectionMutationResult {
  std::vector<SystemVerilogUvmObjectionEvent> events;
  std::vector<SystemVerilogUvmObjectionCallbackFailure> failures;
  bool all_dropped{};

  [[nodiscard]] bool success() const noexcept { return failures.empty(); }
};

enum class SystemVerilogUvmObjectionSnapshotKind : std::uint8_t {
  SourceDescription,
  Component,
  Root,
};

struct SystemVerilogUvmObjectionSnapshot {
  SystemVerilogUvmObjectionSnapshotKind kind{
      SystemVerilogUvmObjectionSnapshotKind::SourceDescription};
  std::uint64_t phase_slot{};
  SystemVerilogUvmRootHandle root{};
  SystemVerilogClassHandle source{};
  SystemVerilogClassHandle component{};
  std::string description;
  std::uint64_t count{};
};

struct SystemVerilogUvmDrainSnapshot {
  std::uint64_t phase_slot{};
  SystemVerilogUvmRootHandle root{};
  SystemVerilogClassHandle source{};
  SimulationTick delay{};
  bool pending{};
  SimulationTick due{};
};

/// Simulation-owned UVM objection counts. Source handles carry service
/// ownership, phase handles retain phase-service ownership, and every count
/// update is fully checked before local or propagated state is published.
class SystemVerilogUvmObjectionService final {
 public:
  using Callback = std::function<void(
      const SystemVerilogUvmObjectionEvent&)>;
  using AllDroppedCallback = std::function<void(
      const SystemVerilogUvmObjectionEvent&)>;
  using ReadyToEndCallback = std::function<void(
      const SystemVerilogUvmObjectionEvent&)>;

  SystemVerilogUvmObjectionService(
      SystemVerilogUvmObjectService& objects,
      SystemVerilogUvmComponentService& components,
      SystemVerilogUvmPhaseService& phases,
      SystemVerilogUvmObjectionLimits limits = {});
  SystemVerilogUvmObjectionService(
      SystemVerilogUvmObjectService& objects,
      SystemVerilogUvmComponentService& components,
      SystemVerilogUvmPhaseService& phases,
      Scheduler& scheduler,
      SystemVerilogUvmObjectionLimits limits = {});
  ~SystemVerilogUvmObjectionService();
  SystemVerilogUvmObjectionService(
      const SystemVerilogUvmObjectionService&) = delete;
  SystemVerilogUvmObjectionService& operator=(
      const SystemVerilogUvmObjectionService&) = delete;

  void set_scheduler(Scheduler& scheduler) noexcept { scheduler_ = &scheduler; }
  void set_activity_service(SystemVerilogUvmActivityService& activity)
      noexcept {
    activity_ = &activity;
  }

  [[nodiscard]] SystemVerilogUvmObjectionSourceHandle bind_source(
      SystemVerilogClassHandle object,
      SystemVerilogUvmRootHandle root = 0) const;

  [[nodiscard]] SystemVerilogUvmObjectionMutationResult raise(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source,
      std::string description = {},
      std::int64_t count = 1);
  [[nodiscard]] SystemVerilogUvmObjectionMutationResult drop(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source,
      std::string_view description = {},
      std::int64_t count = 1);
  [[nodiscard]] SystemVerilogUvmObjectionMutationResult set(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source,
      std::string description,
      std::int64_t count);

  void set_drain_time(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source,
      SimulationTick delay);
  [[nodiscard]] SimulationTick drain_time(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source) const;
  [[nodiscard]] bool has_pending_drain(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source) const;
  [[nodiscard]] std::size_t pending_drain_count() const noexcept {
    return pending_drains_.size();
  }
  [[nodiscard]] std::vector<SystemVerilogUvmObjectionSnapshot>
  snapshots() const;
  [[nodiscard]] std::vector<SystemVerilogUvmDrainSnapshot>
  drain_snapshots() const;

  [[nodiscard]] std::uint64_t source_count(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source,
      std::optional<std::string_view> description = std::nullopt) const;
  [[nodiscard]] std::uint64_t propagated_count(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmRootHandle root,
      SystemVerilogClassHandle component = 0) const;
  [[nodiscard]] bool all_dropped(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmRootHandle root) const;
  [[nodiscard]] bool phase_quiescent(
      SystemVerilogUvmPhaseHandle phase) const;
  void cancel_phase(SystemVerilogUvmPhaseHandle phase);
  void cancel_root(SystemVerilogUvmRootHandle root);
  void cancel_all() noexcept;

  void set_callback(Callback callback) {
    callback_ = std::move(callback);
  }
  void set_all_dropped_callback(AllDroppedCallback callback) {
    all_dropped_callback_ = std::move(callback);
  }
  void set_ready_to_end_callback(ReadyToEndCallback callback) {
    ready_to_end_callback_ = std::move(callback);
  }
  [[nodiscard]] std::span<const SystemVerilogUvmObjectionEvent> trace()
      const noexcept {
    return trace_;
  }
  void clear_trace() noexcept { trace_.clear(); }
  void set_trace_enabled(bool enabled) noexcept;
  [[nodiscard]] bool trace_enabled() const noexcept { return trace_enabled_; }
  [[nodiscard]] std::string trace_text() const;
  [[nodiscard]] std::span<const SystemVerilogUvmObjectionCallbackFailure>
  callback_failures() const noexcept {
    return callback_failures_;
  }
  void clear_callback_failures() noexcept { callback_failures_.clear(); }
  [[nodiscard]] std::size_t mutation_count() const noexcept {
    return mutations_;
  }
  [[nodiscard]] const SystemVerilogUvmObjectionLimits& limits() const
      noexcept {
    return limits_;
  }

 private:
  SystemVerilogUvmObjectionService(
      SystemVerilogUvmObjectService& objects,
      SystemVerilogUvmComponentService& components,
      SystemVerilogUvmPhaseService& phases,
      Scheduler* scheduler,
      SystemVerilogUvmObjectionLimits limits);

  struct EntryKey {
    std::uint64_t phase{};
    SystemVerilogUvmRootHandle root{};
    SystemVerilogClassHandle source{};
    std::string description;
    friend auto operator<=>(const EntryKey&, const EntryKey&) = default;
  };
  struct SourceKey {
    std::uint64_t phase{};
    SystemVerilogUvmRootHandle root{};
    SystemVerilogClassHandle source{};
    friend auto operator<=>(const SourceKey&, const SourceKey&) = default;
  };
  struct ComponentKey {
    std::uint64_t phase{};
    SystemVerilogUvmRootHandle root{};
    SystemVerilogClassHandle component{};
    friend auto operator<=>(
        const ComponentKey&, const ComponentKey&) = default;
  };
  struct RootKey {
    std::uint64_t phase{};
    SystemVerilogUvmRootHandle root{};
    friend auto operator<=>(const RootKey&, const RootKey&) = default;
  };
  struct PendingDrain {
    SystemVerilogUvmPhaseHandle phase;
    SystemVerilogUvmObjectionSourceHandle source;
    std::string description;
    SimulationTick delay{};
    SimulationTick due{};
    std::uint64_t generation{};
    ScheduledTaskHandle task;
  };

  [[nodiscard]] std::vector<SystemVerilogClassHandle> validate_context(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source,
      bool require_executing) const;
  [[nodiscard]] std::uint64_t phase_slot(
      SystemVerilogUvmPhaseHandle phase) const;
  [[nodiscard]] SystemVerilogUvmObjectionMutationResult mutate(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source,
      std::string description,
      std::int64_t count,
      SystemVerilogUvmObjectionOperation operation);
  void append_event(
      SystemVerilogUvmObjectionEvent event,
      SystemVerilogUvmObjectionMutationResult* result = nullptr);
  void cancel_pending_drain(
      const SourceKey& key,
      SystemVerilogUvmObjectionOperation operation,
      std::string_view description,
      SystemVerilogUvmObjectionMutationResult& result);
  void start_pending_drain(
      const SourceKey& key,
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source,
      SystemVerilogUvmObjectionOperation operation,
      std::string_view description,
      SystemVerilogUvmObjectionMutationResult& result);
  void finish_pending_drain(
      SourceKey key,
      std::uint64_t generation);
  void try_finalize_root(
      RootKey key,
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmObjectionSourceHandle source,
      SystemVerilogUvmObjectionOperation operation,
      std::string_view description,
      SystemVerilogUvmObjectionMutationResult* result = nullptr);
  [[nodiscard]] bool root_has_pending_drain(const RootKey& key) const;

  SystemVerilogUvmObjectService* objects_{};
  SystemVerilogUvmComponentService* components_{};
  SystemVerilogUvmPhaseService* phases_{};
  Scheduler* scheduler_{};
  SystemVerilogUvmActivityService* activity_{};
  SystemVerilogUvmObjectionLimits limits_;
  std::shared_ptr<const void> owner_;
  std::map<EntryKey, std::uint64_t> entries_;
  std::map<SourceKey, std::uint64_t> source_totals_;
  std::map<ComponentKey, std::uint64_t> component_totals_;
  std::map<RootKey, std::uint64_t> root_totals_;
  std::map<SourceKey, SimulationTick> drain_times_;
  std::map<SourceKey, PendingDrain> pending_drains_;
  std::vector<SystemVerilogUvmObjectionEvent> trace_;
  bool trace_enabled_{};
  std::uint64_t trace_start_sequence_{};
  std::vector<SystemVerilogUvmObjectionCallbackFailure> callback_failures_;
  Callback callback_;
  AllDroppedCallback all_dropped_callback_;
  ReadyToEndCallback ready_to_end_callback_;
  std::uint64_t next_sequence_{};
  std::uint64_t next_drain_generation_{1};
  StableOrder next_drain_order_{};
  std::size_t mutations_{};
  std::size_t reserved_trace_records_{};
  std::map<RootKey, std::size_t> finalization_depth_;
  std::map<RootKey, bool> finalization_recheck_;
};

}  // namespace fsim::runtime

// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_objection.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-OBJ-001"};
constexpr std::string_view kInvalidOperation{"FSIM-UVM-OBJ-002"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-OBJ-003"};
constexpr std::string_view kCallbackFailure{"FSIM-UVM-OBJ-004"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmObjectionError{
      std::string{code}, std::string{message}};
}

[[nodiscard]] std::string_view event_kind_name(
    const SystemVerilogUvmObjectionEventKind kind) {
  switch (kind) {
    case SystemVerilogUvmObjectionEventKind::Raised: return "raised";
    case SystemVerilogUvmObjectionEventKind::Dropped: return "dropped";
    case SystemVerilogUvmObjectionEventKind::DrainStarted:
      return "drain-started";
    case SystemVerilogUvmObjectionEventKind::DrainCancelled:
      return "drain-cancelled";
    case SystemVerilogUvmObjectionEventKind::DrainCompleted:
      return "drain-completed";
    case SystemVerilogUvmObjectionEventKind::AllDropped: return "all-dropped";
    case SystemVerilogUvmObjectionEventKind::ReadyToEnd: return "ready-to-end";
  }
  fail(kInvalidOperation, "UVM objection trace event kind is invalid");
}

[[nodiscard]] std::string_view operation_name(
    const SystemVerilogUvmObjectionOperation operation) {
  switch (operation) {
    case SystemVerilogUvmObjectionOperation::Raise: return "raise";
    case SystemVerilogUvmObjectionOperation::Drop: return "drop";
    case SystemVerilogUvmObjectionOperation::Set: return "set";
  }
  fail(kInvalidOperation, "UVM objection trace operation is invalid");
}

[[nodiscard]] std::string escaped_trace_text(const std::string_view text) {
  std::string result;
  result.reserve(text.size());
  constexpr char hex[] = "0123456789abcdef";
  for (const auto character : text) {
    const auto value = static_cast<unsigned char>(character);
    if (character == '\\' || character == '"') {
      result.push_back('\\');
      result.push_back(character);
    } else if (value < 0x20U || value == 0x7fU) {
      result += "\\x";
      result.push_back(hex[value >> 4U]);
      result.push_back(hex[value & 0x0fU]);
    } else {
      result.push_back(character);
    }
  }
  return result;
}

[[nodiscard]] std::uint64_t checked_increase(
    const std::uint64_t value,
    const std::uint64_t amount,
    const std::uint64_t maximum) {
  if (amount > maximum || value > maximum - amount) {
    fail(kResourceLimit, "UVM objection count exceeds its configured ceiling");
  }
  return value + amount;
}

template <typename Map, typename Key>
[[nodiscard]] std::uint64_t count_of(
    const Map& values,
    const Key& key) {
  const auto found = values.find(key);
  return found == values.end() ? 0 : found->second;
}

template <typename Map, typename Key>
void publish_count(
    Map& values,
    Key key,
    const std::uint64_t value) {
  if (value == 0) {
    values.erase(key);
  } else {
    values.insert_or_assign(std::move(key), value);
  }
}

}  // namespace

SystemVerilogUvmObjectionError::SystemVerilogUvmObjectionError(
    std::string code,
    std::string message)
    : std::runtime_error{std::move(message)}, code_(std::move(code)) {}

SystemVerilogUvmObjectionService::SystemVerilogUvmObjectionService(
    SystemVerilogUvmObjectService& objects,
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmPhaseService& phases,
    SystemVerilogUvmObjectionLimits limits)
    : SystemVerilogUvmObjectionService(
          objects, components, phases, nullptr, limits) {}

SystemVerilogUvmObjectionService::SystemVerilogUvmObjectionService(
    SystemVerilogUvmObjectService& objects,
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmPhaseService& phases,
    Scheduler& scheduler,
    SystemVerilogUvmObjectionLimits limits)
    : SystemVerilogUvmObjectionService(
          objects, components, phases, &scheduler, limits) {}

SystemVerilogUvmObjectionService::SystemVerilogUvmObjectionService(
    SystemVerilogUvmObjectService& objects,
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmPhaseService& phases,
    Scheduler* scheduler,
    SystemVerilogUvmObjectionLimits limits)
    : objects_(&objects),
      components_(&components),
      phases_(&phases),
      scheduler_(scheduler),
      limits_(limits),
      owner_(std::make_shared<unsigned char>()) {
  if (limits_.maximum_sources == 0
      || limits_.maximum_descriptions == 0
      || limits_.maximum_entries == 0
      || limits_.maximum_description_bytes == 0
      || limits_.maximum_count == 0
      || limits_.maximum_propagation_depth == 0
      || limits_.maximum_callbacks_per_operation == 0
      || limits_.maximum_trace_records == 0
      || limits_.maximum_trace_output_bytes == 0
      || limits_.maximum_mutations == 0
      || limits_.maximum_drain_settings == 0
      || limits_.maximum_pending_drains == 0
      || limits_.maximum_drain_time == 0
      || limits_.maximum_aggregate_drain_time == 0
      || limits_.maximum_callback_reentries == 0) {
    fail(kResourceLimit, "UVM objection limits must all be nonzero");
  }
}

SystemVerilogUvmObjectionService::~SystemVerilogUvmObjectionService() {
  cancel_all();
}

void SystemVerilogUvmObjectionService::set_trace_enabled(
    const bool enabled) noexcept {
  if (enabled && !trace_enabled_) trace_start_sequence_ = next_sequence_;
  trace_enabled_ = enabled;
}

std::string SystemVerilogUvmObjectionService::trace_text() const {
  if (!trace_enabled_) return {};
  std::string result;
  const auto append = [&](const std::string_view text) {
    if (text.size() > limits_.maximum_trace_output_bytes - result.size()) {
      fail(kResourceLimit,
           "UVM objection trace output exceeds its configured ceiling");
    }
    result.append(text);
  };
  for (const auto& event : trace_) {
    if (event.sequence < trace_start_sequence_) continue;
    const auto line =
        std::string{"UVM_OBJECTION_TRACE sequence="}
        + std::to_string(event.sequence) + " time="
        + std::to_string(event.time) + " kind="
        + std::string{event_kind_name(event.kind)} + " operation="
        + std::string{operation_name(event.operation)} + " root="
        + std::to_string(event.root) + " source="
        + std::to_string(event.source.object_) + " target="
        + (event.root_target ? std::string{"root"}
                             : std::to_string(event.target))
        + " amount=" + std::to_string(event.amount) + " source_count="
        + std::to_string(event.source_count) + " target_count="
        + std::to_string(event.target_count) + " drain="
        + std::to_string(event.drain_time) + " description=\""
        + escaped_trace_text(event.description) + "\"\n";
    append(line);
  }
  return result;
}

SystemVerilogUvmObjectionSourceHandle
SystemVerilogUvmObjectionService::bind_source(
    const SystemVerilogClassHandle object,
    SystemVerilogUvmRootHandle root) const {
  if (object == 0 || !objects_->contains(object)) {
    fail(kInvalidHandle, "UVM objection source object is empty or stale");
  }
  if (components_->contains(object)) {
    const auto component_root = components_->root_of(object);
    if (root != 0 && root != component_root) {
      fail(
          kInvalidOperation,
          "UVM objection component source crosses root contexts");
    }
    root = component_root;
  }
  if (root == 0 || !components_->contains_root(root)) {
    fail(kInvalidHandle, "UVM objection source root is empty or stale");
  }
  return SystemVerilogUvmObjectionSourceHandle{owner_, object, root};
}

std::uint64_t SystemVerilogUvmObjectionService::phase_slot(
    const SystemVerilogUvmPhaseHandle phase_handle) const {
  if (!phases_->contains(phase_handle)) {
    fail(
        kInvalidHandle,
        "UVM objection phase is empty, stale, or owned by another simulation");
  }
  return phase_handle.slot_;
}

std::vector<SystemVerilogClassHandle>
SystemVerilogUvmObjectionService::validate_context(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source,
    const bool require_executing) const {
  (void)phase_slot(phase_handle);
  if (!source.valid() || source.owner_.get() != owner_.get()) {
    fail(
        kInvalidHandle,
        "UVM objection source is empty, stale, or owned by another simulation");
  }
  if (!objects_->contains(source.object_)
      || !components_->contains_root(source.root_)) {
    fail(kInvalidHandle, "UVM objection source object or root is stale");
  }

  const auto phase_snapshot = phases_->snapshot(phase_handle);
  if (std::ranges::find(phase_snapshot.roots, source.root_)
      == phase_snapshot.roots.end()) {
    fail(
        kInvalidOperation,
        "UVM objection source root does not participate in the phase");
  }
  if (require_executing
      && (phase_snapshot.execution
              != SystemVerilogUvmPhaseExecutionKind::Task
          || (phase_snapshot.state != SystemVerilogUvmPhaseState::Executing
              && phase_snapshot.state
                  != SystemVerilogUvmPhaseState::ReadyToEnd))) {
    fail(
        kInvalidOperation,
        "UVM objections may mutate only an executing task phase");
  }

  std::vector<SystemVerilogClassHandle> ancestors;
  if (!components_->contains(source.object_)) return ancestors;
  if (components_->root_of(source.object_) != source.root_) {
    fail(
        kInvalidOperation,
        "UVM objection component source changed root association");
  }
  for (auto current = source.object_; current != 0;
       current = components_->parent(current)) {
    if (ancestors.size() >= limits_.maximum_propagation_depth) {
      fail(kResourceLimit, "UVM objection propagation depth exceeded");
    }
    if (components_->root_of(current) != source.root_) {
      fail(
          kInvalidOperation,
          "UVM objection propagation crosses root contexts");
    }
    ancestors.push_back(current);
  }
  return ancestors;
}

SystemVerilogUvmObjectionMutationResult
SystemVerilogUvmObjectionService::raise(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source,
    std::string description,
    const std::int64_t count) {
  return mutate(
      phase_handle,
      source,
      std::move(description),
      count,
      SystemVerilogUvmObjectionOperation::Raise);
}

SystemVerilogUvmObjectionMutationResult
SystemVerilogUvmObjectionService::drop(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source,
    const std::string_view description,
    const std::int64_t count) {
  return mutate(
      phase_handle,
      source,
      std::string{description},
      count,
      SystemVerilogUvmObjectionOperation::Drop);
}

SystemVerilogUvmObjectionMutationResult
SystemVerilogUvmObjectionService::set(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source,
    std::string description,
    const std::int64_t count) {
  return mutate(
      phase_handle,
      source,
      std::move(description),
      count,
      SystemVerilogUvmObjectionOperation::Set);
}

void SystemVerilogUvmObjectionService::set_drain_time(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source,
    const SimulationTick delay) {
  (void)validate_context(phase_handle, source, false);
  if (delay > limits_.maximum_drain_time) {
    fail(kResourceLimit, "UVM objection drain time exceeds its ceiling");
  }
  const SourceKey key{phase_slot(phase_handle), source.root_, source.object_};
  if (delay == 0) {
    drain_times_.erase(key);
    return;
  }
  if (!scheduler_) {
    fail(kInvalidOperation, "nonzero UVM objection drains require a scheduler");
  }
  if (!drain_times_.contains(key)
      && drain_times_.size() >= limits_.maximum_drain_settings) {
    fail(kResourceLimit, "UVM objection drain-setting ceiling exceeded");
  }
  drain_times_.insert_or_assign(key, delay);
}

SimulationTick SystemVerilogUvmObjectionService::drain_time(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source) const {
  (void)validate_context(phase_handle, source, false);
  return count_of(
      drain_times_,
      SourceKey{phase_slot(phase_handle), source.root_, source.object_});
}

bool SystemVerilogUvmObjectionService::has_pending_drain(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source) const {
  (void)validate_context(phase_handle, source, false);
  return pending_drains_.contains(
      SourceKey{phase_slot(phase_handle), source.root_, source.object_});
}

std::vector<SystemVerilogUvmObjectionSnapshot>
SystemVerilogUvmObjectionService::snapshots() const {
  std::vector<SystemVerilogUvmObjectionSnapshot> result;
  result.reserve(entries_.size() + component_totals_.size()
                 + root_totals_.size());
  for (const auto& [key, count] : entries_) {
    result.push_back({
        SystemVerilogUvmObjectionSnapshotKind::SourceDescription,
        key.phase, key.root, key.source, 0, key.description, count});
  }
  for (const auto& [key, count] : component_totals_) {
    result.push_back({
        SystemVerilogUvmObjectionSnapshotKind::Component,
        key.phase, key.root, 0, key.component, {}, count});
  }
  for (const auto& [key, count] : root_totals_) {
    result.push_back({
        SystemVerilogUvmObjectionSnapshotKind::Root,
        key.phase, key.root, 0, 0, {}, count});
  }
  return result;
}

std::vector<SystemVerilogUvmDrainSnapshot>
SystemVerilogUvmObjectionService::drain_snapshots() const {
  std::vector<SystemVerilogUvmDrainSnapshot> result;
  result.reserve(drain_times_.size());
  for (const auto& [key, delay] : drain_times_) {
    const auto pending = pending_drains_.find(key);
    result.push_back({
        key.phase, key.root, key.source, delay,
        pending != pending_drains_.end(),
        pending == pending_drains_.end() ? 0 : pending->second.due});
  }
  return result;
}

SystemVerilogUvmObjectionMutationResult
SystemVerilogUvmObjectionService::mutate(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source,
    std::string description,
    const std::int64_t requested_count,
    const SystemVerilogUvmObjectionOperation operation) {
  if (requested_count < 0
      || (operation != SystemVerilogUvmObjectionOperation::Set
          && requested_count == 0)) {
    fail(kInvalidOperation, "UVM objection count is negative or zero");
  }
  if (description.size() > limits_.maximum_description_bytes) {
    fail(kResourceLimit, "UVM objection description exceeds its byte ceiling");
  }
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM objection mutation budget exceeded");
  }

  const auto ancestors = validate_context(phase_handle, source, true);
  const auto slot = phase_slot(phase_handle);
  const EntryKey entry_key{slot, source.root_, source.object_, description};
  const SourceKey source_key{slot, source.root_, source.object_};
  const RootKey root_key{slot, source.root_};
  const auto before = count_of(entries_, entry_key);
  const auto amount = static_cast<std::uint64_t>(requested_count);

  std::uint64_t after{};
  switch (operation) {
    case SystemVerilogUvmObjectionOperation::Raise:
      after = checked_increase(before, amount, limits_.maximum_count);
      break;
    case SystemVerilogUvmObjectionOperation::Drop:
      if (amount > before) {
        fail(kInvalidOperation, "UVM objection drop exceeds its local count");
      }
      after = before - amount;
      break;
    case SystemVerilogUvmObjectionOperation::Set:
      if (amount > limits_.maximum_count) {
        fail(kResourceLimit, "UVM objection count exceeds its configured ceiling");
      }
      after = amount;
      break;
  }
  if (after == before) return {};

  const bool increasing = after > before;
  const auto delta = increasing ? after - before : before - after;
  const auto source_before = count_of(source_totals_, source_key);
  const auto source_after = increasing
      ? checked_increase(source_before, delta, limits_.maximum_count)
      : source_before - delta;
  const auto root_before = count_of(root_totals_, root_key);
  const auto root_after = increasing
      ? checked_increase(root_before, delta, limits_.maximum_count)
      : root_before - delta;

  std::vector<std::pair<ComponentKey, std::uint64_t>> component_after;
  component_after.reserve(ancestors.size());
  for (const auto component : ancestors) {
    const ComponentKey key{slot, source.root_, component};
    const auto previous = count_of(component_totals_, key);
    component_after.emplace_back(
        key,
        increasing
            ? checked_increase(previous, delta, limits_.maximum_count)
            : previous - delta);
  }

  const bool new_entry = before == 0 && after != 0;
  if (new_entry) {
    if (entries_.size() >= limits_.maximum_entries) {
      fail(kResourceLimit, "UVM objection entry ceiling exceeded");
    }
    if (!source_totals_.contains(source_key)
        && source_totals_.size() >= limits_.maximum_sources) {
      fail(kResourceLimit, "UVM objection source ceiling exceeded");
    }
    bool known_description{};
    std::set<std::string_view> descriptions;
    for (const auto& [key, ignored] : entries_) {
      (void)ignored;
      descriptions.insert(key.description);
      known_description = known_description || key.description == description;
    }
    if (!known_description
        && descriptions.size() >= limits_.maximum_descriptions) {
      fail(kResourceLimit, "UVM objection description ceiling exceeded");
    }
  }

  const bool detected_all_dropped = root_before != 0 && root_after == 0;
  const auto configured_drain = count_of(drain_times_, source_key);
  const bool starts_drain = !increasing && source_after == 0
      && configured_drain != 0;
  const bool cancels_drain = increasing
      && pending_drains_.contains(source_key);
  if (starts_drain) {
    if (!scheduler_) {
      fail(kInvalidOperation, "nonzero UVM objection drains require a scheduler");
    }
    if (pending_drains_.size() >= limits_.maximum_pending_drains) {
      fail(kResourceLimit, "UVM objection pending-drain ceiling exceeded");
    }
    if (configured_drain > limits_.maximum_drain_time
        || configured_drain
            > std::numeric_limits<SimulationTick>::max() - scheduler_->now()) {
      fail(kResourceLimit, "UVM objection drain time is not representable");
    }
    SimulationTick aggregate{};
    for (const auto& [key, pending] : pending_drains_) {
      if (key.phase == slot && key.root == source.root_) {
        if (pending.delay
            > limits_.maximum_aggregate_drain_time - aggregate) {
          fail(kResourceLimit, "UVM objection aggregate drain time exceeded");
        }
        aggregate += pending.delay;
      }
    }
    if (configured_drain
        > limits_.maximum_aggregate_drain_time - aggregate) {
      fail(kResourceLimit, "UVM objection aggregate drain time exceeded");
    }
  }
  const auto callback_count = ancestors.size() + 1U;
  const bool root_pending_after = starts_drain
      || (root_has_pending_drain(root_key) && !cancels_drain);
  const auto immediate_finalization = detected_all_dropped
      && !root_pending_after;
  const auto event_count = callback_count
      + static_cast<std::size_t>(starts_drain)
      + static_cast<std::size_t>(cancels_drain)
      + (immediate_finalization ? 2U : 0U);
  if (callback_count > limits_.maximum_callbacks_per_operation) {
    fail(kResourceLimit, "UVM objection callback fanout exceeded");
  }
  const auto released_reservation = cancels_drain ? 3U : 0U;
  const auto new_reservation = starts_drain ? 3U : 0U;
  const auto occupied = trace_.size() + reserved_trace_records_
      - released_reservation;
  if (event_count + new_reservation
      > limits_.maximum_trace_records - occupied) {
    fail(kResourceLimit, "UVM objection trace record ceiling exceeded");
  }

  publish_count(entries_, entry_key, after);
  publish_count(source_totals_, source_key, source_after);
  for (auto& [key, value] : component_after) {
    publish_count(component_totals_, std::move(key), value);
  }
  publish_count(root_totals_, root_key, root_after);
  ++mutations_;

  SystemVerilogUvmObjectionMutationResult result;
  result.events.reserve(event_count);
  const auto kind = increasing
      ? SystemVerilogUvmObjectionEventKind::Raised
      : SystemVerilogUvmObjectionEventKind::Dropped;

  for (const auto component : ancestors) {
    append_event(SystemVerilogUvmObjectionEvent{
        kind,
        operation,
        phase_handle,
        source.root_,
        source,
        description,
        component,
        false,
        delta,
        source_after,
        count_of(
            component_totals_,
            ComponentKey{slot, source.root_, component})}, &result);
  }
  append_event(SystemVerilogUvmObjectionEvent{
      kind,
      operation,
      phase_handle,
      source.root_,
      source,
      description,
      0,
      true,
      delta,
      source_after,
      root_after}, &result);
  if (increasing && pending_drains_.contains(source_key)) {
    cancel_pending_drain(source_key, operation, description, result);
  }
  if (!increasing && source_after == 0 && configured_drain != 0
      && count_of(source_totals_, source_key) == 0) {
    start_pending_drain(
        source_key, phase_handle, source, operation, description, result);
  }
  if (detected_all_dropped) {
    try_finalize_root(
        root_key, phase_handle, source, operation, description, &result);
  }
  return result;
}

void SystemVerilogUvmObjectionService::append_event(
    SystemVerilogUvmObjectionEvent event,
    SystemVerilogUvmObjectionMutationResult* result) {
  event.sequence = next_sequence_++;
  event.time = scheduler_ ? scheduler_->now() : 0;
  trace_.push_back(event);
  if (result) result->events.push_back(event);
  if (activity_) {
    auto action = SystemVerilogUvmActivityAction::Updated;
    if (event.kind == SystemVerilogUvmObjectionEventKind::Raised) {
      action = SystemVerilogUvmActivityAction::Raised;
    } else if (event.kind == SystemVerilogUvmObjectionEventKind::Dropped) {
      action = SystemVerilogUvmActivityAction::Dropped;
    } else if (
        event.kind == SystemVerilogUvmObjectionEventKind::DrainStarted) {
      action = SystemVerilogUvmActivityAction::Scheduled;
    } else if (
        event.kind == SystemVerilogUvmObjectionEventKind::DrainCompleted
        || event.kind == SystemVerilogUvmObjectionEventKind::AllDropped
        || event.kind == SystemVerilogUvmObjectionEventKind::ReadyToEnd) {
      action = SystemVerilogUvmActivityAction::Completed;
    } else if (
        event.kind == SystemVerilogUvmObjectionEventKind::DrainCancelled) {
      action = SystemVerilogUvmActivityAction::Cancelled;
    }
    activity_->publish({
        event.kind == SystemVerilogUvmObjectionEventKind::DrainStarted
                || event.kind
                    == SystemVerilogUvmObjectionEventKind::DrainCompleted
                || event.kind
                    == SystemVerilogUvmObjectionEventKind::DrainCancelled
            ? SystemVerilogUvmActivityKind::Drain
            : SystemVerilogUvmActivityKind::Objection,
        action,
        phases_->snapshot(event.phase).identity,
        event.description,
        event.root,
        event.target_count});
  }
  if (!callback_
      || (event.kind != SystemVerilogUvmObjectionEventKind::Raised
          && event.kind != SystemVerilogUvmObjectionEventKind::Dropped)) {
    return;
  }
  try {
    callback_(event);
  } catch (const std::exception& error) {
    SystemVerilogUvmObjectionCallbackFailure failure{
        std::string{kCallbackFailure}, event, error.what()};
    callback_failures_.push_back(failure);
    if (result) result->failures.push_back(std::move(failure));
  } catch (...) {
    SystemVerilogUvmObjectionCallbackFailure failure{
        std::string{kCallbackFailure}, event,
        "unknown UVM objection callback failure"};
    callback_failures_.push_back(failure);
    if (result) result->failures.push_back(std::move(failure));
  }
}

bool SystemVerilogUvmObjectionService::root_has_pending_drain(
    const RootKey& root) const {
  return std::ranges::any_of(
      pending_drains_,
      [&](const auto& entry) {
        return entry.first.phase == root.phase
            && entry.first.root == root.root;
      });
}

void SystemVerilogUvmObjectionService::cancel_pending_drain(
    const SourceKey& key,
    const SystemVerilogUvmObjectionOperation operation,
    const std::string_view description,
    SystemVerilogUvmObjectionMutationResult& result) {
  const auto found = pending_drains_.find(key);
  if (found == pending_drains_.end()) return;
  auto pending = std::move(found->second);
  scheduler_->cancel(pending.task);
  pending_drains_.erase(found);
  reserved_trace_records_ -= 3U;
  SystemVerilogUvmObjectionEvent event{
      SystemVerilogUvmObjectionEventKind::DrainCancelled,
      operation,
      pending.phase,
      key.root,
      pending.source,
      std::string{description},
      0,
      true,
      0,
      count_of(source_totals_, key),
      count_of(root_totals_, RootKey{key.phase, key.root})};
  event.drain_time = pending.delay;
  append_event(std::move(event), &result);
}

void SystemVerilogUvmObjectionService::start_pending_drain(
    const SourceKey& key,
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source,
    const SystemVerilogUvmObjectionOperation operation,
    const std::string_view description,
    SystemVerilogUvmObjectionMutationResult& result) {
  const auto delay = count_of(drain_times_, key);
  if (delay == 0 || pending_drains_.contains(key)) return;
  const auto generation = next_drain_generation_++;
  PendingDrain pending;
  pending.phase = phase_handle;
  pending.source = source;
  pending.description = description;
  pending.delay = delay;
  pending.due = scheduler_->now() + delay;
  pending.generation = generation;
  pending.task = scheduler_->schedule_after_cancelable(
      delay,
      SchedulerPhase::reactive,
      next_drain_order_++,
      [this, key, generation](Scheduler&) {
        finish_pending_drain(key, generation);
      });
  pending_drains_.emplace(key, std::move(pending));
  reserved_trace_records_ += 3U;
  SystemVerilogUvmObjectionEvent event{
      SystemVerilogUvmObjectionEventKind::DrainStarted,
      operation,
      phase_handle,
      key.root,
      source,
      std::string{description},
      0,
      true,
      0,
      0,
      count_of(root_totals_, RootKey{key.phase, key.root})};
  event.drain_time = delay;
  append_event(std::move(event), &result);
}

void SystemVerilogUvmObjectionService::finish_pending_drain(
    SourceKey key,
    const std::uint64_t generation) {
  const auto found = pending_drains_.find(key);
  if (found == pending_drains_.end()
      || found->second.generation != generation) {
    return;
  }
  auto pending = std::move(found->second);
  pending_drains_.erase(found);
  reserved_trace_records_ -= 3U;
  SystemVerilogUvmObjectionEvent event{
      SystemVerilogUvmObjectionEventKind::DrainCompleted,
      SystemVerilogUvmObjectionOperation::Drop,
      pending.phase,
      key.root,
      pending.source,
      pending.description,
      0,
      true,
      0,
      count_of(source_totals_, key),
      count_of(root_totals_, RootKey{key.phase, key.root})};
  event.drain_time = pending.delay;
  append_event(std::move(event));
  try_finalize_root(
      RootKey{key.phase, key.root},
      pending.phase,
      pending.source,
      SystemVerilogUvmObjectionOperation::Drop,
      pending.description);
}

void SystemVerilogUvmObjectionService::try_finalize_root(
    const RootKey key,
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source,
    const SystemVerilogUvmObjectionOperation operation,
    const std::string_view description,
    SystemVerilogUvmObjectionMutationResult* result) {
  if (count_of(root_totals_, key) != 0 || root_has_pending_drain(key)) return;
  if (finalization_depth_.contains(key)) {
    finalization_recheck_[key] = true;
    return;
  }
  finalization_depth_[key] = 1;
  std::size_t iterations{};
  for (;;) {
    if (count_of(root_totals_, key) != 0 || root_has_pending_drain(key)) break;
    if (iterations++ >= limits_.maximum_callback_reentries) {
      callback_failures_.push_back(SystemVerilogUvmObjectionCallbackFailure{
          std::string{kResourceLimit}, {},
          "UVM objection all-dropped callback re-entry ceiling exceeded"});
      break;
    }
    finalization_recheck_[key] = false;
    SystemVerilogUvmObjectionEvent all_event{
        SystemVerilogUvmObjectionEventKind::AllDropped,
        operation,
        phase_handle,
        key.root,
        source,
        std::string{description},
        0,
        true,
        0,
        count_of(source_totals_, SourceKey{key.phase, key.root, source.object_}),
        0};
    append_event(all_event, result);
    all_event = trace_.back();
    if (result) result->all_dropped = true;
    if (all_dropped_callback_) {
      try {
        all_dropped_callback_(all_event);
      } catch (const std::exception& error) {
        callback_failures_.push_back(SystemVerilogUvmObjectionCallbackFailure{
            std::string{kCallbackFailure}, all_event, error.what()});
      } catch (...) {
        callback_failures_.push_back(SystemVerilogUvmObjectionCallbackFailure{
            std::string{kCallbackFailure}, all_event,
            "unknown UVM all-dropped callback failure"});
      }
    }
    if (count_of(root_totals_, key) != 0 || root_has_pending_drain(key)) break;

    SystemVerilogUvmObjectionEvent ready_event = all_event;
    ready_event.kind = SystemVerilogUvmObjectionEventKind::ReadyToEnd;
    append_event(ready_event, result);
    ready_event = trace_.back();
    if (ready_to_end_callback_) {
      try {
        ready_to_end_callback_(ready_event);
      } catch (const std::exception& error) {
        callback_failures_.push_back(SystemVerilogUvmObjectionCallbackFailure{
            std::string{kCallbackFailure}, ready_event, error.what()});
      } catch (...) {
        callback_failures_.push_back(SystemVerilogUvmObjectionCallbackFailure{
            std::string{kCallbackFailure}, ready_event,
            "unknown UVM ready-to-end callback failure"});
      }
    }
    if (!finalization_recheck_[key]) break;
  }
  finalization_depth_.erase(key);
  finalization_recheck_.erase(key);
}

std::uint64_t SystemVerilogUvmObjectionService::source_count(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmObjectionSourceHandle source,
    const std::optional<std::string_view> description) const {
  (void)validate_context(phase_handle, source, false);
  const auto slot = phase_slot(phase_handle);
  if (description.has_value()) {
    return count_of(
        entries_,
        EntryKey{slot, source.root_, source.object_,
                 std::string{*description}});
  }
  return count_of(
      source_totals_, SourceKey{slot, source.root_, source.object_});
}

std::uint64_t SystemVerilogUvmObjectionService::propagated_count(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmRootHandle root,
    const SystemVerilogClassHandle component) const {
  const auto slot = phase_slot(phase_handle);
  if (!components_->contains_root(root)) {
    fail(kInvalidHandle, "UVM objection count root is empty or stale");
  }
  const auto phase_snapshot = phases_->snapshot(phase_handle);
  if (std::ranges::find(phase_snapshot.roots, root)
      == phase_snapshot.roots.end()) {
    fail(kInvalidOperation, "UVM objection count root does not participate");
  }
  if (component == 0) {
    return count_of(root_totals_, RootKey{slot, root});
  }
  if (!components_->contains(component)
      || components_->root_of(component) != root) {
    fail(
        kInvalidHandle,
        "UVM objection count component is stale or belongs to another root");
  }
  return count_of(
      component_totals_, ComponentKey{slot, root, component});
}

bool SystemVerilogUvmObjectionService::all_dropped(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmRootHandle root) const {
  return propagated_count(phase_handle, root) == 0;
}

bool SystemVerilogUvmObjectionService::phase_quiescent(
    const SystemVerilogUvmPhaseHandle phase_handle) const {
  const auto slot = phase_slot(phase_handle);
  const auto phase_snapshot = phases_->snapshot(phase_handle);
  return std::ranges::all_of(
      phase_snapshot.roots,
      [&](const auto root) {
        const RootKey key{slot, root};
        return count_of(root_totals_, key) == 0
            && !root_has_pending_drain(key);
      });
}

void SystemVerilogUvmObjectionService::cancel_phase(
    const SystemVerilogUvmPhaseHandle phase_handle) {
  const auto slot = phase_slot(phase_handle);
  for (auto current = pending_drains_.begin();
       current != pending_drains_.end();) {
    if (current->first.phase != slot) {
      ++current;
      continue;
    }
    if (scheduler_) scheduler_->cancel(current->second.task);
    reserved_trace_records_ -= 3U;
    current = pending_drains_.erase(current);
  }
  std::erase_if(entries_, [&](const auto& entry) {
    return entry.first.phase == slot;
  });
  std::erase_if(source_totals_, [&](const auto& entry) {
    return entry.first.phase == slot;
  });
  std::erase_if(component_totals_, [&](const auto& entry) {
    return entry.first.phase == slot;
  });
  std::erase_if(root_totals_, [&](const auto& entry) {
    return entry.first.phase == slot;
  });
  std::erase_if(finalization_depth_, [&](const auto& entry) {
    return entry.first.phase == slot;
  });
  std::erase_if(finalization_recheck_, [&](const auto& entry) {
    return entry.first.phase == slot;
  });
}

void SystemVerilogUvmObjectionService::cancel_root(
    const SystemVerilogUvmRootHandle root) {
  if (!components_->contains_root(root)) {
    fail(kInvalidHandle, "UVM objection cancellation root is stale");
  }
  for (auto current = pending_drains_.begin();
       current != pending_drains_.end();) {
    if (current->first.root != root) {
      ++current;
      continue;
    }
    if (scheduler_) scheduler_->cancel(current->second.task);
    reserved_trace_records_ -= 3U;
    current = pending_drains_.erase(current);
  }
  const auto matches_root = [root](const auto& entry) {
    return entry.first.root == root;
  };
  std::erase_if(entries_, matches_root);
  std::erase_if(source_totals_, matches_root);
  std::erase_if(component_totals_, matches_root);
  std::erase_if(root_totals_, matches_root);
  std::erase_if(drain_times_, matches_root);
  std::erase_if(finalization_depth_, matches_root);
  std::erase_if(finalization_recheck_, matches_root);
}

void SystemVerilogUvmObjectionService::cancel_all() noexcept {
  if (scheduler_) {
    for (const auto& [ignored, pending] : pending_drains_) {
      (void)ignored;
      scheduler_->cancel(pending.task);
    }
  }
  pending_drains_.clear();
  entries_.clear();
  source_totals_.clear();
  component_totals_.clear();
  root_totals_.clear();
  drain_times_.clear();
  finalization_depth_.clear();
  finalization_recheck_.clear();
  reserved_trace_records_ = 0;
}

}  // namespace fsim::runtime

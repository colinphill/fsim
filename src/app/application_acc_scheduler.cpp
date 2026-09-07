// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application_acc_scheduler.hpp"

#include <array>
#include <map>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace fsim::app {
namespace {

constexpr std::size_t phase_index(const runtime::SchedulerPhase phase) {
  return static_cast<std::size_t>(phase);
}

bool valid_phase(const runtime::SchedulerPhase phase) {
  return phase_index(phase) <= phase_index(runtime::SchedulerPhase::postponed);
}

AccSchedulerDrainResult drain_failure(const AccSchedulerError error) {
  return {.error = error};
}

}  // namespace

struct AccSchedulerCoordinator::Impl {
  Impl(runtime::Scheduler& scheduler_value, AccSchedulerPublishHook hook)
      : scheduler(&scheduler_value), publish(std::move(hook)) {}

  runtime::Scheduler* scheduler{};
  AccSchedulerPublishHook publish;
  mutable std::recursive_mutex mutex;
  bool dispatching{};
  std::array<std::uint64_t, 8> completed_epochs{};
  std::map<AccSchedulerSequence, AccSchedulerRequest> pending;
};

AccSchedulerCoordinator::AccSchedulerCoordinator(
    runtime::Scheduler& scheduler, AccSchedulerPublishHook publish)
    : impl_(std::make_shared<Impl>(scheduler, std::move(publish))) {}

AccSchedulerCoordinator::~AccSchedulerCoordinator() = default;

AccSchedulerError AccSchedulerCoordinator::stage(
    AccSchedulerRequest request) noexcept {
  if (!impl_ || impl_->scheduler == nullptr) {
    return AccSchedulerError::InvalidCoordinator;
  }
  if (request.sequence.epoch == 0U ||
      request.sequence.source_order == 0U ||
      !valid_phase(request.sequence.phase) || !request.operation) {
    return AccSchedulerError::InvalidRequest;
  }
  std::lock_guard lock{impl_->mutex};
  if (impl_->dispatching) return AccSchedulerError::Reentrant;
  if (request.sequence.epoch <=
      impl_->completed_epochs[phase_index(request.sequence.phase)]) {
    return AccSchedulerError::StaleEpoch;
  }
  if (impl_->pending.size() >= kMaximumAccSchedulerRequests) {
    return AccSchedulerError::ResourceLimit;
  }
  try {
    const auto [position, inserted] =
        impl_->pending.emplace(request.sequence, std::move(request));
    (void)position;
    return inserted ? AccSchedulerError::None
                    : AccSchedulerError::DuplicateSequence;
  } catch (const std::bad_alloc&) {
    return AccSchedulerError::Allocation;
  } catch (...) {
    return AccSchedulerError::Allocation;
  }
}

AccSchedulerDrainResult AccSchedulerCoordinator::drain(
    const std::uint64_t epoch) noexcept {
  if (!impl_ || impl_->scheduler == nullptr) {
    return drain_failure(AccSchedulerError::InvalidCoordinator);
  }
  std::lock_guard lock{impl_->mutex};
  if (!impl_->scheduler->running() ||
      !impl_->scheduler->current_phase().has_value()) {
    return drain_failure(AccSchedulerError::InactiveScheduler);
  }
  if (impl_->dispatching) return drain_failure(AccSchedulerError::Reentrant);
  const auto phase = *impl_->scheduler->current_phase();
  if (!valid_phase(phase)) return drain_failure(AccSchedulerError::WrongPhase);
  if (epoch == 0U || epoch <= impl_->completed_epochs[phase_index(phase)]) {
    return drain_failure(AccSchedulerError::StaleEpoch);
  }

  std::vector<AccSchedulerSequence> selected;
  try {
    for (const auto& [sequence, request] : impl_->pending) {
      (void)request;
      if (sequence.phase == phase && sequence.epoch < epoch) {
        return drain_failure(AccSchedulerError::OutOfOrderEpoch);
      }
      if (sequence.epoch == epoch && sequence.phase == phase) {
        selected.push_back(sequence);
      }
    }
  } catch (const std::bad_alloc&) {
    return drain_failure(AccSchedulerError::Allocation);
  } catch (...) {
    return drain_failure(AccSchedulerError::Allocation);
  }

  struct DispatchGuard {
    bool& value;
    ~DispatchGuard() { value = false; }
  } guard{impl_->dispatching};
  impl_->dispatching = true;
  AccSchedulerDrainResult result;
  for (const auto& sequence : selected) {
    auto position = impl_->pending.find(sequence);
    if (position == impl_->pending.end()) continue;
    const auto kind = position->second.kind;
    bool succeeded{};
    try {
      succeeded = position->second.operation();
    } catch (...) {
      succeeded = false;
    }
    ++result.attempted;
    if (succeeded) {
      ++result.succeeded;
    } else {
      ++result.failed;
      result.error = AccSchedulerError::OperationFailure;
    }
    if (impl_->publish) {
      bool published{};
      try {
        published = impl_->publish(
            {.sequence = sequence,
             .kind = kind,
             .time = impl_->scheduler->now(),
             .delta = impl_->scheduler->delta(),
             .succeeded = succeeded});
      } catch (...) {
        published = false;
      }
      if (!published) {
        ++result.publication_failures;
        if (result.error == AccSchedulerError::None) {
          result.error = AccSchedulerError::PublicationFailure;
        }
      }
    }
    impl_->pending.erase(position);
  }
  impl_->completed_epochs[phase_index(phase)] = epoch;
  return result;
}

AccSchedulerSnapshot AccSchedulerCoordinator::snapshot() const noexcept {
  if (!impl_ || impl_->scheduler == nullptr) {
    return {.error = AccSchedulerError::InvalidCoordinator};
  }
  std::lock_guard lock{impl_->mutex};
  return {.error = AccSchedulerError::None,
          .pending = impl_->pending.size(),
          .completed_active_epoch = impl_->completed_epochs[phase_index(
              runtime::SchedulerPhase::active)],
          .completed_observed_epoch = impl_->completed_epochs[phase_index(
              runtime::SchedulerPhase::observed)],
          .completed_reactive_epoch = impl_->completed_epochs[phase_index(
              runtime::SchedulerPhase::reactive)],
          .dispatching = impl_->dispatching};
}

}  // namespace fsim::app

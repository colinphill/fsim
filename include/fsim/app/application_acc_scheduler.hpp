// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace fsim::app {

inline constexpr std::size_t kMaximumAccSchedulerRequests = 1U << 16U;

enum class AccSchedulerOperationKind : std::uint8_t {
  Observation,
  Mutation,
  Traversal,
  Callback,
};

struct AccSchedulerSequence {
  std::uint64_t epoch{};
  runtime::SchedulerPhase phase{runtime::SchedulerPhase::active};
  runtime::StableOrder stable_order{};
  std::uint32_t source_order{};

  friend auto operator<=>(const AccSchedulerSequence&,
                          const AccSchedulerSequence&) = default;
};

enum class AccSchedulerError : std::uint8_t {
  None,
  InvalidCoordinator,
  InvalidRequest,
  DuplicateSequence,
  StaleEpoch,
  OutOfOrderEpoch,
  InactiveScheduler,
  WrongPhase,
  Reentrant,
  ResourceLimit,
  OperationFailure,
  PublicationFailure,
  Allocation,
};

struct AccSchedulerRequest {
  AccSchedulerSequence sequence;
  AccSchedulerOperationKind kind{AccSchedulerOperationKind::Observation};
  std::function<bool()> operation;
};

struct AccSchedulerPublication {
  AccSchedulerSequence sequence;
  AccSchedulerOperationKind kind{AccSchedulerOperationKind::Observation};
  runtime::SimulationTick time{};
  std::uint64_t delta{};
  bool succeeded{};
};

using AccSchedulerPublishHook =
    std::function<bool(const AccSchedulerPublication&)>;

struct AccSchedulerDrainResult {
  AccSchedulerError error{AccSchedulerError::None};
  std::uint32_t attempted{};
  std::uint32_t succeeded{};
  std::uint32_t failed{};
  std::uint32_t publication_failures{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == AccSchedulerError::None;
  }
};

struct AccSchedulerSnapshot {
  AccSchedulerError error{AccSchedulerError::None};
  std::size_t pending{};
  std::uint64_t completed_active_epoch{};
  std::uint64_t completed_observed_epoch{};
  std::uint64_t completed_reactive_epoch{};
  bool dispatching{};
};

class AccSchedulerCoordinator final {
public:
  explicit AccSchedulerCoordinator(
      runtime::Scheduler& scheduler, AccSchedulerPublishHook publish = {});
  ~AccSchedulerCoordinator();

  AccSchedulerCoordinator(const AccSchedulerCoordinator&) = delete;
  AccSchedulerCoordinator& operator=(const AccSchedulerCoordinator&) = delete;
  AccSchedulerCoordinator(AccSchedulerCoordinator&&) = delete;
  AccSchedulerCoordinator& operator=(AccSchedulerCoordinator&&) = delete;

  [[nodiscard]] AccSchedulerError stage(AccSchedulerRequest request) noexcept;
  [[nodiscard]] AccSchedulerDrainResult drain(std::uint64_t epoch) noexcept;
  [[nodiscard]] AccSchedulerSnapshot snapshot() const noexcept;

  struct Impl;

private:
  std::shared_ptr<Impl> impl_;
};

}  // namespace fsim::app

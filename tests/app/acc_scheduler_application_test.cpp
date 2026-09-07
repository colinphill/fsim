// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application_acc_scheduler.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using Kind = fsim::app::AccSchedulerOperationKind;
using Error = fsim::app::AccSchedulerError;

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error{message};
}

struct TraceEntry {
  std::uint64_t stable_order{};
  std::uint32_t source_order{};
  Kind kind{Kind::Observation};
  bool succeeded{};

  friend bool operator==(const TraceEntry&, const TraceEntry&) = default;
};

std::vector<TraceEntry> run_order(const std::array<std::size_t, 4>& order) {
  fsim::runtime::Scheduler scheduler;
  std::vector<TraceEntry> execution;
  std::vector<TraceEntry> publications;
  fsim::app::AccSchedulerCoordinator coordinator{
      scheduler,
      [&](const fsim::app::AccSchedulerPublication& publication) {
        require(scheduler.running() &&
                    scheduler.current_phase() == publication.sequence.phase &&
                    publication.time == scheduler.now() &&
                    publication.delta == scheduler.delta(),
                "publication retains the exact scheduler boundary");
        publications.push_back({publication.sequence.stable_order,
                                publication.sequence.source_order,
                                publication.kind, publication.succeeded});
        return true;
      }};

  constexpr std::array kinds{Kind::Observation, Kind::Mutation,
                             Kind::Traversal, Kind::Callback};
  constexpr std::array stable_orders{40ULL, 10ULL, 30ULL, 20ULL};
  std::array<Error, 4> staged{};
  std::array<std::thread, 4> workers;
  for (std::size_t slot = 0; slot < workers.size(); ++slot) {
    const auto index = order[slot];
    workers[slot] = std::thread([&, index] {
      const fsim::app::AccSchedulerSequence sequence{
          .epoch = 7,
          .phase = fsim::runtime::SchedulerPhase::active,
          .stable_order = stable_orders[index],
          .source_order = static_cast<std::uint32_t>(index + 1U)};
      staged[index] = coordinator.stage(
          {.sequence = sequence,
           .kind = kinds[index],
           .operation = [&, sequence, index] {
             execution.push_back({sequence.stable_order,
                                  sequence.source_order, kinds[index], true});
             return true;
           }});
    });
  }
  for (auto& worker : workers) worker.join();
  require(std::ranges::all_of(staged,
                              [](const Error error) {
                                return error == Error::None;
                              }),
          "parallel workers stage every ACC operation family");
  require(coordinator.stage(
              {.sequence = {.epoch = 7,
                            .phase = fsim::runtime::SchedulerPhase::active,
                            .stable_order = 10,
                            .source_order = 2},
               .kind = Kind::Observation,
               .operation = [] { return true; }}) ==
              Error::DuplicateSequence,
          "duplicate canonical sequence identities are rejected");
  require(coordinator.drain(7).error == Error::InactiveScheduler,
          "ACC operations cannot bypass the scheduler boundary");

  fsim::app::AccSchedulerDrainResult drained;
  scheduler.schedule(fsim::runtime::SchedulerPhase::active, 100,
                     [&](fsim::runtime::Scheduler&) {
                       drained = coordinator.drain(7);
                     });
  require(scheduler.run().status == fsim::runtime::RunStatus::completed &&
              drained && drained.attempted == 4 && drained.succeeded == 4 &&
              drained.failed == 0 && drained.publication_failures == 0,
          "one scheduler boundary serializes the complete ACC epoch");
  require(execution == publications && execution.size() == 4 &&
              execution[0].stable_order == 10 &&
              execution[1].stable_order == 20 &&
              execution[2].stable_order == 30 &&
              execution[3].stable_order == 40,
          "ACC operations publish in canonical order, not worker arrival order");
  const auto snapshot = coordinator.snapshot();
  require(snapshot.error == Error::None && snapshot.pending == 0 &&
              snapshot.completed_active_epoch == 7 &&
              coordinator.stage(
                  {.sequence = {.epoch = 7,
                                .phase = fsim::runtime::SchedulerPhase::active,
                                .stable_order = 50,
                                .source_order = 5},
                   .kind = Kind::Observation,
                   .operation = [] { return true; }}) == Error::StaleEpoch,
          "completed epochs reject late worker publication");
  return execution;
}

}  // namespace

int main() {
  const auto reverse = run_order({3, 2, 1, 0});
  const auto shuffled = run_order({1, 3, 0, 2});
  require(reverse == shuffled,
          "worker completion order cannot change ACC execution order");

  fsim::runtime::Scheduler epoch_scheduler;
  fsim::app::AccSchedulerCoordinator epoch_coordinator{epoch_scheduler};
  for (const auto epoch : {1ULL, 2ULL}) {
    require(epoch_coordinator.stage(
                {.sequence = {.epoch = epoch,
                              .phase = fsim::runtime::SchedulerPhase::active,
                              .stable_order = 1,
                              .source_order = 1},
                 .kind = Kind::Observation,
                 .operation = [] { return true; }}) == Error::None,
            "consecutive worker epochs stage before the barrier");
  }
  std::array<fsim::app::AccSchedulerDrainResult, 3> epochs;
  epoch_scheduler.schedule(fsim::runtime::SchedulerPhase::active, 1,
                           [&](fsim::runtime::Scheduler&) {
                             epochs[0] = epoch_coordinator.drain(2);
                             epochs[1] = epoch_coordinator.drain(1);
                             epochs[2] = epoch_coordinator.drain(2);
                           });
  require(epoch_scheduler.run().status ==
                  fsim::runtime::RunStatus::completed &&
              epochs[0].error == Error::OutOfOrderEpoch && epochs[1] &&
              epochs[2] && epoch_coordinator.snapshot().pending == 0,
          "a later epoch cannot strand earlier staged ACC operations");

  fsim::runtime::Scheduler scheduler;
  fsim::app::AccSchedulerCoordinator coordinator{scheduler};
  Error nested = Error::None;
  require(coordinator.stage(
              {.sequence = {.epoch = 1,
                            .phase = fsim::runtime::SchedulerPhase::observed,
                            .stable_order = 1,
                            .source_order = 1},
               .kind = Kind::Callback,
               .operation = [&]() -> bool {
                 nested = coordinator.stage(
                     {.sequence = {.epoch = 2,
                                   .phase = fsim::runtime::SchedulerPhase::observed,
                                   .stable_order = 1,
                                   .source_order = 1},
                      .kind = Kind::Callback,
                      .operation = [] { return true; }});
                 throw std::runtime_error{"contained foreign failure"};
               }}) == Error::None,
          "callback request stages before its observed-region barrier");
  fsim::app::AccSchedulerDrainResult failed;
  scheduler.schedule(fsim::runtime::SchedulerPhase::observed, 1,
                     [&](fsim::runtime::Scheduler&) {
                       failed = coordinator.drain(1);
                     });
  require(scheduler.run().status == fsim::runtime::RunStatus::completed &&
              nested == Error::Reentrant &&
              failed.error == Error::OperationFailure &&
              failed.attempted == 1 && failed.failed == 1 &&
              coordinator.snapshot().completed_observed_epoch == 1,
          "foreign exceptions and callback re-entry are contained deterministically");
  return 0;
}

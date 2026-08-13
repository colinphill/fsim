// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/vhpi_time.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require_vhpi_time(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

void test_vhdl_vhpi_time() {
  using fsim::runtime::Scheduler;
  using fsim::runtime::SchedulerPhase;
  using fsim::runtime::VhdlVhpiPhase;
  using fsim::runtime::VhdlVhpiPhaseEvent;
  using fsim::runtime::VhdlVhpiTimeError;
  using fsim::runtime::VhdlVhpiTimeProfile;
  using fsim::runtime::VhdlVhpiTimeSnapshot;
  using fsim::runtime::VhdlVhpiTimeSystem;

  Scheduler scheduler;
  VhdlVhpiTimeSystem time{
      1101, scheduler, VhdlVhpiTimeProfile{-9, -12}};
  const auto initial = time.query();
  require_vhpi_time(
      time.valid() && initial && initial.value.ticks == 0
          && initial.value.delta == 0
          && initial.value.profile.unit_exponent == -9
          && initial.value.profile.precision_exponent == -12
          && !initial.value.scheduler_phase
          && !initial.value.next_time,
      "VHPI exact initial time profile is incorrect");

  std::vector<VhdlVhpiPhaseEvent> events;
  const auto update = time.register_callback(
      VhdlVhpiPhase::Update,
      true,
      [&](const VhdlVhpiPhaseEvent& event) {
        events.push_back(event);
      });
  const auto one_shot = time.register_callback(
      VhdlVhpiPhase::Update,
      false,
      [&](const VhdlVhpiPhaseEvent& event) {
        events.push_back(event);
      });
  const auto synchronization = time.register_callback(
      VhdlVhpiPhase::Synchronization,
      true,
      [&](const VhdlVhpiPhaseEvent& event) {
        events.push_back(event);
      });
  const auto read_only = time.register_callback(
      VhdlVhpiPhase::ReadOnly,
      true,
      [&](const VhdlVhpiPhaseEvent& event) {
        events.push_back(event);
      });
  const auto next_time = time.register_callback(
      VhdlVhpiPhase::NextTime,
      true,
      [&](const VhdlVhpiPhaseEvent& event) {
        events.push_back(event);
      });
  const auto removed = time.register_callback(
      VhdlVhpiPhase::Update,
      true,
      [&](const VhdlVhpiPhaseEvent&) {
        throw std::runtime_error("removed VHPI callback ran");
      });
  require_vhpi_time(
      update && one_shot && synchronization && read_only && next_time
          && removed
          && time.remove_callback(removed.value.identity)
              == VhdlVhpiTimeError::None
          && time.remove_callback(removed.value.identity)
              == VhdlVhpiTimeError::Removed
          && !time.callback(removed.value.identity).value.active,
      "VHPI phase callback registration/removal failed");

  VhdlVhpiTimeSnapshot active_snapshot;
  scheduler.schedule(
      SchedulerPhase::active,
      0,
      [&](Scheduler&) {
        active_snapshot = time.query().value;
      });
  scheduler.schedule_after(
      5,
      SchedulerPhase::active,
      0,
      [&](Scheduler&) {
        active_snapshot = time.query().value;
      });
  static_cast<void>(scheduler.run());
  require_vhpi_time(
      scheduler.now() == 5 && active_snapshot.ticks == 5
          && active_snapshot.scheduler_phase == SchedulerPhase::active,
      "VHPI in-phase time query is incorrect");

  std::array<std::size_t, 4> phase_counts{};
  std::optional<fsim::runtime::SimulationTick> announced;
  for (const auto& event : events) {
    switch (event.phase) {
    case VhdlVhpiPhase::Update:
      ++phase_counts[0];
      break;
    case VhdlVhpiPhase::Synchronization:
      ++phase_counts[1];
      break;
    case VhdlVhpiPhase::ReadOnly:
      ++phase_counts[2];
      break;
    case VhdlVhpiPhase::NextTime:
      ++phase_counts[3];
      announced = event.next_time;
      break;
    default:
      break;
    }
  }
  require_vhpi_time(
      phase_counts[0] == 5
          && phase_counts[1] == 2
          && phase_counts[2] == 2
          && phase_counts[3] == 1 && announced == 5
          && time.callback(update.value.identity).value.invocations == 4
          && time.callback(one_shot.value.identity).value.invocations == 1
          && !time.callback(one_shot.value.identity).value.active,
      "VHPI scheduler phase or next-time callback counts are incorrect");

  std::vector<VhdlVhpiPhase> lifecycle;
  const auto save = time.register_callback(
      VhdlVhpiPhase::Save,
      true,
      [&](const VhdlVhpiPhaseEvent& event) {
        lifecycle.push_back(event.phase);
      });
  const auto restart = time.register_callback(
      VhdlVhpiPhase::Restart,
      true,
      [&](const VhdlVhpiPhaseEvent& event) {
        lifecycle.push_back(event.phase);
      });
  const auto reset = time.register_callback(
      VhdlVhpiPhase::Reset,
      true,
      [&](const VhdlVhpiPhaseEvent& event) {
        lifecycle.push_back(event.phase);
      });
  const auto terminal = time.register_callback(
      VhdlVhpiPhase::Terminal,
      true,
      [&](const VhdlVhpiPhaseEvent& event) {
        lifecycle.push_back(event.phase);
      });
  require_vhpi_time(
      save && restart && reset && terminal
          && time.notify(VhdlVhpiPhase::Save)
              == VhdlVhpiTimeError::None
          && time.notify(VhdlVhpiPhase::Restart)
              == VhdlVhpiTimeError::None
          && time.notify(VhdlVhpiPhase::Reset)
              == VhdlVhpiTimeError::None
          && time.notify(VhdlVhpiPhase::Terminal)
              == VhdlVhpiTimeError::None
          && time.notify(VhdlVhpiPhase::Update)
              == VhdlVhpiTimeError::InvalidPhase
          && lifecycle
              == std::vector<VhdlVhpiPhase>{
                  VhdlVhpiPhase::Save,
                  VhdlVhpiPhase::Restart,
                  VhdlVhpiPhase::Reset,
                  VhdlVhpiPhase::Terminal},
      "VHPI explicit lifecycle regions are incorrect");

  Scheduler invalid_scheduler;
  VhdlVhpiTimeSystem invalid_profile{
      1102, invalid_scheduler, VhdlVhpiTimeProfile{-19, -19}};
  require_vhpi_time(
      !invalid_profile.valid()
          && invalid_profile.query().error
              == VhdlVhpiTimeError::InvalidProfile
          && invalid_profile
                 .register_callback(
                     VhdlVhpiPhase::Update, true, [](const auto&) {})
                 .error
              == VhdlVhpiTimeError::InvalidProfile,
      "VHPI invalid time profile was accepted");

  Scheduler foreign_scheduler;
  VhdlVhpiTimeSystem foreign{
      1103, foreign_scheduler, VhdlVhpiTimeProfile{0, -3}};
  require_vhpi_time(
      foreign.callback(update.value.identity).error
          == VhdlVhpiTimeError::CrossSimulation
          && foreign.remove_callback(update.value.identity)
              == VhdlVhpiTimeError::CrossSimulation,
      "VHPI foreign callback identity was accepted");
}

}  // namespace fsim::tests::runtime

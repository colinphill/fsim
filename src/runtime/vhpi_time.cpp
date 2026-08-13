// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_time.hpp"

#include <algorithm>
#include <atomic>
#include <ranges>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::uint8_t k_callback_identity = 0xc1U;
std::atomic<std::uint32_t> next_time_system{1U};

[[nodiscard]] bool manual_phase(const VhdlVhpiPhase phase) noexcept {
  return phase == VhdlVhpiPhase::Save
      || phase == VhdlVhpiPhase::Restart
      || phase == VhdlVhpiPhase::Reset
      || phase == VhdlVhpiPhase::Terminal;
}

}  // namespace

VhdlVhpiTimeSystem::VhdlVhpiTimeSystem(
    const std::uint64_t simulation_identity,
    Scheduler& scheduler,
    const VhdlVhpiTimeProfile profile)
    : simulation_identity_(simulation_identity),
      system_identity_(
          next_time_system.fetch_add(1U, std::memory_order_relaxed)
          & 0x00ffffffU),
      scheduler_(&scheduler),
      profile_(profile) {
  if (system_identity_ == 0U) {
    system_identity_ =
        next_time_system.fetch_add(1U, std::memory_order_relaxed)
        & 0x00ffffffU;
  }
  if (valid()) {
    scheduler_->set_safe_point_hook(
        [this](Scheduler&, const SchedulerPhase phase) {
          safe_point(phase);
        });
  }
}

VhdlVhpiTimeSystem::~VhdlVhpiTimeSystem() {
  if (scheduler_ != nullptr) {
    scheduler_->set_safe_point_hook({});
  }
}

bool VhdlVhpiTimeSystem::valid_profile(
    const VhdlVhpiTimeProfile& profile) noexcept {
  return profile.unit_exponent >= -18
      && profile.unit_exponent <= 18
      && profile.precision_exponent >= -18
      && profile.precision_exponent <= profile.unit_exponent;
}

bool VhdlVhpiTimeSystem::valid_phase(
    const VhdlVhpiPhase phase) noexcept {
  return static_cast<std::uint32_t>(phase)
      <= static_cast<std::uint32_t>(VhdlVhpiPhase::Terminal);
}

bool VhdlVhpiTimeSystem::valid() const noexcept {
  return simulation_identity_ != 0U && scheduler_ != nullptr
      && valid_profile(profile_);
}

std::uint64_t VhdlVhpiTimeSystem::make_identity(
    const std::uint32_t local) const noexcept {
  return (static_cast<std::uint64_t>(k_callback_identity) << 56U)
      | (static_cast<std::uint64_t>(system_identity_) << 32U)
      | local;
}

VhdlVhpiTimeError VhdlVhpiTimeSystem::validate_identity(
    const std::uint64_t identity) const noexcept {
  if (identity == 0U
      || static_cast<std::uint8_t>(identity >> 56U)
          != k_callback_identity
      || static_cast<std::uint32_t>(identity) == 0U) {
    return VhdlVhpiTimeError::InvalidCallback;
  }
  const auto owner =
      static_cast<std::uint32_t>((identity >> 32U) & 0x00ffffffU);
  return owner == system_identity_ ? VhdlVhpiTimeError::None
                                   : VhdlVhpiTimeError::CrossSimulation;
}

VhdlVhpiTimeQueryResult VhdlVhpiTimeSystem::query() const {
  if (!valid()) {
    return {{}, simulation_identity_ == 0U
        ? VhdlVhpiTimeError::InvalidSimulation
        : VhdlVhpiTimeError::InvalidProfile};
  }
  return {
      VhdlVhpiTimeSnapshot{
          scheduler_->now(),
          scheduler_->delta(),
          profile_,
          scheduler_->current_phase(),
          scheduler_->next_pending_time()},
      VhdlVhpiTimeError::None};
}

VhdlVhpiCallbackResult VhdlVhpiTimeSystem::register_callback(
    const VhdlVhpiPhase phase,
    const bool repeat,
    VhdlVhpiPhaseCallback callback_function) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, simulation_identity_ == 0U
        ? VhdlVhpiTimeError::InvalidSimulation
        : VhdlVhpiTimeError::InvalidProfile};
  }
  if (!valid_phase(phase) || !callback_function) {
    return {{}, VhdlVhpiTimeError::InvalidPhase};
  }
  if (next_callback_ == 0U) {
    return {{}, VhdlVhpiTimeError::ResourceLimit};
  }
  const auto identity = make_identity(next_callback_++);
  VhdlVhpiCallbackDescriptor descriptor{
      identity, phase, next_ordinal_++, repeat, true, 0};
  try {
    callbacks_.emplace(
        identity,
        CallbackEntry{descriptor, std::move(callback_function)});
  } catch (...) {
    return {{}, VhdlVhpiTimeError::ResourceLimit};
  }
  return {descriptor, VhdlVhpiTimeError::None};
}

VhdlVhpiCallbackResult VhdlVhpiTimeSystem::callback(
    const std::uint64_t identity) const {
  std::scoped_lock lock{mutex_};
  const auto identity_error = validate_identity(identity);
  if (identity_error != VhdlVhpiTimeError::None) {
    return {{}, identity_error};
  }
  const auto found = callbacks_.find(identity);
  if (found == callbacks_.end()) {
    return {{}, VhdlVhpiTimeError::InvalidCallback};
  }
  return {found->second.descriptor, VhdlVhpiTimeError::None};
}

VhdlVhpiTimeError VhdlVhpiTimeSystem::remove_callback(
    const std::uint64_t identity) {
  std::scoped_lock lock{mutex_};
  const auto identity_error = validate_identity(identity);
  if (identity_error != VhdlVhpiTimeError::None) {
    return identity_error;
  }
  const auto found = callbacks_.find(identity);
  if (found == callbacks_.end()) {
    return VhdlVhpiTimeError::InvalidCallback;
  }
  if (!found->second.descriptor.active) {
    return VhdlVhpiTimeError::Removed;
  }
  found->second.descriptor.active = false;
  return VhdlVhpiTimeError::None;
}

void VhdlVhpiTimeSystem::dispatch(
    const VhdlVhpiPhase phase,
    const std::optional<SimulationTick> next_time) {
  struct Invocation {
    std::uint64_t ordinal{};
    VhdlVhpiPhaseCallback callback;
  };
  std::vector<Invocation> invocations;
  {
    std::scoped_lock lock{mutex_};
    try {
      invocations.reserve(callbacks_.size());
      for (auto& [identity, entry] : callbacks_) {
        static_cast<void>(identity);
        if (!entry.descriptor.active
            || entry.descriptor.phase != phase) {
          continue;
        }
        invocations.push_back(
            {entry.descriptor.ordinal, entry.callback});
        ++entry.descriptor.invocations;
        if (!entry.descriptor.repeat) {
          entry.descriptor.active = false;
        }
      }
    } catch (...) {
      return;
    }
  }
  std::ranges::sort(
      invocations,
      [](const Invocation& lhs, const Invocation& rhs) {
        return lhs.ordinal < rhs.ordinal;
      });
  const VhdlVhpiPhaseEvent event{
      phase, scheduler_->now(), scheduler_->delta(), next_time};
  for (const auto& invocation : invocations) {
    invocation.callback(event);
  }
}

void VhdlVhpiTimeSystem::safe_point(const SchedulerPhase phase) {
  switch (phase) {
  case SchedulerPhase::update:
  case SchedulerPhase::re_update:
      dispatch(VhdlVhpiPhase::Update);
      break;
  case SchedulerPhase::reactive:
    dispatch(VhdlVhpiPhase::Synchronization);
    break;
  case SchedulerPhase::postponed:
    dispatch(VhdlVhpiPhase::ReadOnly);
    break;
  case SchedulerPhase::active:
  case SchedulerPhase::inactive:
  case SchedulerPhase::observed:
  case SchedulerPhase::re_inactive:
      break;
  }
  const auto next = scheduler_->next_pending_time();
  if (next && *next > scheduler_->now()
      && announced_next_time_ != next) {
    announced_next_time_ = next;
    dispatch(VhdlVhpiPhase::NextTime, next);
  }
  if (!next || *next <= scheduler_->now()) {
    announced_next_time_.reset();
  }
}

VhdlVhpiTimeError VhdlVhpiTimeSystem::notify(
    const VhdlVhpiPhase phase) {
  if (!valid()) {
    return simulation_identity_ == 0U
        ? VhdlVhpiTimeError::InvalidSimulation
        : VhdlVhpiTimeError::InvalidProfile;
  }
  if (!manual_phase(phase)) {
    return VhdlVhpiTimeError::InvalidPhase;
  }
  dispatch(phase);
  return VhdlVhpiTimeError::None;
}

}  // namespace fsim::runtime

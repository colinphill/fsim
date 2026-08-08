// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_activity.hpp"

#include <limits>
#include <utility>
#include <vector>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidActivity{"FSIM-UVM-ACTIVITY-001"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-ACTIVITY-002"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmActivityError{
      std::string{code}, std::string{message}};
}

}  // namespace

SystemVerilogUvmActivityError::SystemVerilogUvmActivityError(
    std::string code,
    std::string message)
    : std::runtime_error(std::move(message)), code_(std::move(code)) {}

SystemVerilogUvmActivityService::SystemVerilogUvmActivityService(
    SystemVerilogUvmActivityLimits limits)
    : limits_(limits) {
  if (limits_.maximum_events == 0 || limits_.maximum_observers == 0
      || limits_.maximum_callbacks_per_event == 0
      || limits_.maximum_reentry_depth == 0
      || limits_.maximum_failures == 0
      || limits_.maximum_identity_bytes == 0
      || limits_.maximum_detail_bytes == 0) {
    fail(kResourceLimit, "UVM activity limits must be nonzero");
  }
}

SystemVerilogUvmActivityService::SystemVerilogUvmActivityService(
    Scheduler& scheduler,
    SystemVerilogUvmActivityLimits limits)
    : SystemVerilogUvmActivityService(std::move(limits)) {
  scheduler_ = &scheduler;
}

std::uint64_t SystemVerilogUvmActivityService::add_observer(
    Observer observer) {
  if (!observer) {
    fail(kInvalidActivity, "UVM activity observer is empty");
  }
  if (observers_.size() >= limits_.maximum_observers
      || next_observer_ == 0) {
    fail(kResourceLimit, "UVM activity observer ceiling exceeded");
  }
  const auto token = next_observer_++;
  observers_.emplace(token, std::move(observer));
  return token;
}

void SystemVerilogUvmActivityService::remove_observer(
    const std::uint64_t token) noexcept {
  observers_.erase(token);
}

void SystemVerilogUvmActivityService::publish(
    SystemVerilogUvmActivityEvent event) {
  if (event.identity.size() > limits_.maximum_identity_bytes
      || event.detail.size() > limits_.maximum_detail_bytes) {
    fail(kResourceLimit, "UVM activity text ceiling exceeded");
  }
  if (events_.size() >= limits_.maximum_events
      || observers_.size() > limits_.maximum_callbacks_per_event
      || reentry_depth_ >= limits_.maximum_reentry_depth
      || next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM activity publication ceiling exceeded");
  }
  event.sequence = ++next_sequence_;
  if (scheduler_) {
    event.time = scheduler_->now();
    event.delta = scheduler_->delta();
  }
  events_.push_back(event);
  std::vector<std::pair<std::uint64_t, Observer>> callbacks{
      observers_.begin(), observers_.end()};
  ++reentry_depth_;
  const auto retain_failure = [&](
      const std::uint64_t token,
      std::string message) {
    if (failures_.size() < limits_.maximum_failures) {
      failures_.push_back({
          std::string{kInvalidActivity}, event, token, std::move(message)});
    }
  };
  for (const auto& [token, callback] : callbacks) {
    try {
      callback(event);
    } catch (const std::exception& error) {
      retain_failure(token, error.what());
    } catch (...) {
      retain_failure(token, "unknown UVM activity observer failure");
    }
  }
  --reentry_depth_;
}

}  // namespace fsim::runtime

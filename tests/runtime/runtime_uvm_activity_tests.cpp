// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_activity.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require_activity(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

}  // namespace

void test_systemverilog_uvm_activity() {
  using namespace fsim::runtime;
  Scheduler scheduler;
  SystemVerilogUvmActivityService activity{scheduler};
  std::vector<unsigned> callbacks;
  std::uint64_t second{};
  const auto first = activity.add_observer([&](const auto& event) {
    callbacks.push_back(1);
    if (event.sequence == 1) {
      activity.remove_observer(second);
      (void)activity.add_observer(
          [&](const auto&) { callbacks.push_back(3); });
      throw std::runtime_error{"contained activity observer"};
    }
  });
  second = activity.add_observer(
      [&](const auto&) { callbacks.push_back(2); });
  (void)scheduler.schedule_after(
      3, SchedulerPhase::reactive, 0,
      [&](Scheduler&) {
        activity.publish({
            SystemVerilogUvmActivityKind::PhaseState,
            SystemVerilogUvmActivityAction::Updated,
            "common.run", "2", 7, 2});
      });
  require_activity(
      scheduler.run().status == RunStatus::completed
          && activity.events().size() == 1
          && activity.events().front().sequence == 1
          && activity.events().front().time == 3
          && activity.events().front().identity == "common.run"
          && callbacks == std::vector<unsigned>({1, 2})
          && activity.failures().size() == 1
          && activity.failures().front().observer == first
          && activity.failures().front().diagnostic_code
              == "FSIM-UVM-ACTIVITY-001",
      "UVM activity callbacks must retain time/order, frozen mutation, and "
      "exception containment");
  activity.publish({
      SystemVerilogUvmActivityKind::Quiescence,
      SystemVerilogUvmActivityAction::Completed,
      "uvm.main", "quiescence", 7, 4});
  require_activity(
      callbacks == std::vector<unsigned>({1, 2, 1, 3})
          && activity.events().back().sequence == 2,
      "UVM activity observer mutation must affect only later publications");

  SystemVerilogUvmActivityLimits bounded_limits;
  bounded_limits.maximum_events = 1;
  SystemVerilogUvmActivityService bounded{bounded_limits};
  bounded.publish({});
  bool bounded_rejected{};
  try {
    bounded.publish({});
  } catch (const SystemVerilogUvmActivityError& error) {
    bounded_rejected = error.diagnostic_code() == "FSIM-UVM-ACTIVITY-002";
  }
  require_activity(
      bounded_rejected && bounded.events().size() == 1,
      "UVM activity resource rejection must preserve prior event state");

  SystemVerilogUvmActivityLimits failure_limits;
  failure_limits.maximum_failures = 1;
  SystemVerilogUvmActivityService contained{failure_limits};
  (void)contained.add_observer(
      [](const auto&) { throw std::runtime_error{"first"}; });
  (void)contained.add_observer(
      [](const auto&) { throw std::runtime_error{"second"}; });
  contained.publish({});
  require_activity(
      contained.events().size() == 1 && contained.failures().size() == 1,
      "UVM activity observer failures must remain contained when the "
      "failure log is full");
}

}  // namespace fsim::tests::runtime

// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/vhpi_callback.hpp"

#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require_vhpi_callback(
    const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult callback_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

struct TeardownToken {
  TeardownToken(
      std::vector<std::uint64_t>& destruction_order,
      const std::uint64_t token_value) noexcept
      : order(&destruction_order), value(token_value) {}

  std::vector<std::uint64_t>* order{};
  std::uint64_t value{};

  ~TeardownToken() {
    order->push_back(value);
  }
};

}  // namespace

void test_vhdl_vhpi_callbacks() {
  using fsim::runtime::PackedLogic9;
  using fsim::runtime::Scheduler;
  using fsim::runtime::SchedulerPhase;
  using fsim::runtime::VhdlVhpiCallbackError;
  using fsim::runtime::VhdlVhpiCallbackEvent;
  using fsim::runtime::VhdlVhpiCallbackKind;
  using fsim::runtime::VhdlVhpiCallbackStatus;
  using fsim::runtime::VhdlVhpiCallbackSystem;
  using fsim::runtime::VhdlVhpiEventData;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiSourceLocation;

  VhdlVhpiObjectRegistry objects{1201};
  const auto root = callback_object(
      objects, VhdlVhpiObjectKind::Root, 0, "work");
  require_vhpi_callback(
      static_cast<bool>(root), "VHPI callback root creation failed");
  const auto create =
      [&](const VhdlVhpiObjectKind kind, const std::string& name) {
        const auto object = callback_object(
            objects, kind, root.value, name);
        require_vhpi_callback(
            static_cast<bool>(object),
            "VHPI callback object creation failed");
        return object.value;
      };
  const auto signal = create(VhdlVhpiObjectKind::Signal, "data");
  const auto process = create(VhdlVhpiObjectKind::Process, "producer");
  const auto region = create(VhdlVhpiObjectKind::Region, "scope");

  Scheduler scheduler;
  VhdlVhpiCallbackSystem callbacks{objects, scheduler};
  std::vector<VhdlVhpiCallbackEvent> copied_events;
  const auto collect = [&](const VhdlVhpiCallbackEvent& event) {
    copied_events.push_back(event);
  };
  const auto signal_callback = callbacks.register_callback(
      {VhdlVhpiCallbackKind::Signal, signal, true, 11, collect});
  const auto process_callback = callbacks.register_callback(
      {VhdlVhpiCallbackKind::Process, process, true, 12, collect});
  const auto event_callback = callbacks.register_callback(
      {VhdlVhpiCallbackKind::Event, root.value, true, 13, collect});
  const auto transaction_callback = callbacks.register_callback(
      {VhdlVhpiCallbackKind::Transaction, 0, true, 14, collect});
  const auto assertion_callback = callbacks.register_callback(
      {VhdlVhpiCallbackKind::Assertion, region, true, 15, collect});
  require_vhpi_callback(
      signal_callback && process_callback && event_callback
          && transaction_callback && assertion_callback
          && signal_callback.value.ordinal
              < process_callback.value.ordinal,
      "VHPI event callback registration failed");

  VhdlVhpiEventData signal_data{
      signal,
      501,
      PackedLogic9::from_msb_string("10XZ"),
      "signal changed",
      VhdlVhpiSourceLocation{"callback.vhd", 14, 7},
      0};
  scheduler.schedule_after(
      3,
      SchedulerPhase::active,
      0,
      [&](Scheduler&) {
        require_vhpi_callback(
            callbacks.publish(VhdlVhpiCallbackKind::Signal, signal_data)
                    == VhdlVhpiCallbackError::None
                && callbacks.publish(
                       VhdlVhpiCallbackKind::Process,
                       VhdlVhpiEventData{
                           process, 502, std::nullopt, {}, std::nullopt, 0})
                    == VhdlVhpiCallbackError::None
                && callbacks.publish(
                       VhdlVhpiCallbackKind::Event,
                       VhdlVhpiEventData{
                           root.value,
                           503,
                           std::nullopt,
                           {},
                           std::nullopt,
                           0})
                    == VhdlVhpiCallbackError::None
                && callbacks.publish(
                       VhdlVhpiCallbackKind::Transaction,
                       VhdlVhpiEventData{
                           signal,
                           504,
                           PackedLogic9::from_msb_string("01"),
                           {},
                           std::nullopt,
                           0})
                    == VhdlVhpiCallbackError::None
                && callbacks.publish(
                       VhdlVhpiCallbackKind::Assertion,
                       VhdlVhpiEventData{
                           region,
                           505,
                           std::nullopt,
                           "constraint failed",
                           VhdlVhpiSourceLocation{
                               "callback.vhd", 28, 3},
                           2})
                    == VhdlVhpiCallbackError::None,
            "VHPI event publication failed");
        signal_data.message = "mutated after publication";
        signal_data.source->file = "mutated.vhd";
      });
  static_cast<void>(scheduler.run());
  require_vhpi_callback(
      copied_events.size() == 5
          && copied_events[0].kind == VhdlVhpiCallbackKind::Signal
          && copied_events[0].simulation_identity == 1201
          && copied_events[0].user_data == 11
          && copied_events[0].time == 3
          && copied_events[0].data.related_identity == 501
          && copied_events[0].data.value->to_msb_string() == "10XZ"
          && copied_events[0].data.message == "signal changed"
          && copied_events[0].data.source->file == "callback.vhd"
          && copied_events[4].kind == VhdlVhpiCallbackKind::Assertion
          && copied_events[4].data.message == "constraint failed"
          && copied_events[4].data.severity == 2,
      "VHPI callback event data was not copied exactly");

  std::vector<std::string_view> lifecycle_order;
  std::uint64_t self_identity{};
  std::uint64_t peer_identity{};
  std::uint64_t nested_identity{};
  const auto self = callbacks.register_callback(
      {VhdlVhpiCallbackKind::StartOfSimulation,
       0,
       true,
       21,
       [&](const VhdlVhpiCallbackEvent& event) {
         lifecycle_order.push_back("self");
         require_vhpi_callback(
             event.registration == self_identity
                 && callbacks.remove_callback(self_identity)
                     == VhdlVhpiCallbackError::None
                 && callbacks.remove_callback(peer_identity)
                     == VhdlVhpiCallbackError::None,
             "VHPI self/peer callback removal failed");
         const auto nested = callbacks.register_callback(
             {VhdlVhpiCallbackKind::StartOfSimulation,
              0,
              false,
              22,
              [&](const VhdlVhpiCallbackEvent&) {
                lifecycle_order.push_back("nested");
              }});
         require_vhpi_callback(
             static_cast<bool>(nested),
             "VHPI nested callback registration failed");
         nested_identity = nested.value.identity;
         require_vhpi_callback(
             callbacks.publish(
                 VhdlVhpiCallbackKind::Process,
                 VhdlVhpiEventData{
                     process, 506, std::nullopt, {}, std::nullopt, 0})
                 == VhdlVhpiCallbackError::None,
             "VHPI callback re-entry failed");
         lifecycle_order.push_back("reentry");
       }});
  const auto peer = callbacks.register_callback(
      {VhdlVhpiCallbackKind::StartOfSimulation,
       0,
       true,
       23,
       [&](const VhdlVhpiCallbackEvent&) {
         lifecycle_order.push_back("peer");
       }});
  self_identity = self.value.identity;
  peer_identity = peer.value.identity;
  const auto throwing = callbacks.register_callback(
      {VhdlVhpiCallbackKind::StartOfSimulation,
       0,
       true,
       24,
       [&](const VhdlVhpiCallbackEvent&) {
         lifecycle_order.push_back("throwing");
         throw std::runtime_error("contained VHPI callback");
       }});
  const auto last = callbacks.register_callback(
      {VhdlVhpiCallbackKind::StartOfSimulation,
       0,
       false,
       25,
       [&](const VhdlVhpiCallbackEvent&) {
         lifecycle_order.push_back("last");
       }});
  require_vhpi_callback(
      self && peer && throwing && last
          && callbacks.publish(VhdlVhpiCallbackKind::StartOfSimulation)
              == VhdlVhpiCallbackError::None
          && lifecycle_order
              == std::vector<std::string_view>{
                  "self", "reentry", "throwing", "last"}
          && callbacks.status(self_identity).value.status
              == VhdlVhpiCallbackStatus::Removed
          && callbacks.status(peer_identity).value.status
              == VhdlVhpiCallbackStatus::Removed
          && callbacks.status(throwing.value.identity).value.status
              == VhdlVhpiCallbackStatus::CallbackFailed
          && callbacks.status(last.value.identity).value.status
              == VhdlVhpiCallbackStatus::Fired,
      "VHPI callback snapshot/removal/exception semantics failed");
  require_vhpi_callback(
      callbacks.publish(VhdlVhpiCallbackKind::StartOfSimulation)
              == VhdlVhpiCallbackError::None
          && lifecycle_order.back() == "nested"
          && callbacks.status(nested_identity).value.status
              == VhdlVhpiCallbackStatus::Fired,
      "VHPI nested registration was not deferred to the next event");

  const VhdlVhpiCallbackKind lifecycle_kinds[] = {
      VhdlVhpiCallbackKind::EndOfSimulation,
      VhdlVhpiCallbackKind::StartOfSave,
      VhdlVhpiCallbackKind::EndOfSave,
      VhdlVhpiCallbackKind::StartOfRestart,
      VhdlVhpiCallbackKind::EndOfRestart,
      VhdlVhpiCallbackKind::StartOfReset,
      VhdlVhpiCallbackKind::EndOfReset};
  std::vector<VhdlVhpiCallbackKind> lifecycle_events;
  for (const auto kind : lifecycle_kinds) {
    const auto registration = callbacks.register_callback(
        {kind,
         0,
         false,
         30,
         [&](const VhdlVhpiCallbackEvent& event) {
           lifecycle_events.push_back(event.kind);
         }});
    require_vhpi_callback(
        registration && callbacks.publish(kind)
                == VhdlVhpiCallbackError::None
            && callbacks.status(registration.value.identity).value.status
                == VhdlVhpiCallbackStatus::Fired,
        "VHPI lifecycle callback did not fire exactly once");
  }
  require_vhpi_callback(
      lifecycle_events
          == std::vector<VhdlVhpiCallbackKind>(
              std::begin(lifecycle_kinds), std::end(lifecycle_kinds)),
      "VHPI lifecycle callback kinds lost identity or order");

  std::vector<std::string> tool_events;
  const auto tool_callback = callbacks.register_callback(
      {VhdlVhpiCallbackKind::ToolExecution,
       0,
       true,
       31,
       [&](const VhdlVhpiCallbackEvent& event) {
         require_vhpi_callback(
             event.kind == VhdlVhpiCallbackKind::ToolExecution
                 && event.data.object == 0U
                 && event.data.related_identity == FSIM_VHPI_TOOL_SAVE
                 && event.user_data == 31U,
             "VHPI tool callback lost action or ownership");
         tool_events.push_back(event.data.message);
       }});
  const VhdlVhpiEventData tool_data{
      0,
      FSIM_VHPI_TOOL_SAVE,
      std::nullopt,
      "checkpoint.fsim",
      std::nullopt,
      0};
  require_vhpi_callback(
      tool_callback
          && callbacks.publish(
                 VhdlVhpiCallbackKind::ToolExecution, tool_data)
              == VhdlVhpiCallbackError::None
          && tool_events == std::vector<std::string>{"checkpoint.fsim"},
      "VHPI tool execution callback publishes bounded action data");

  VhdlVhpiObjectRegistry foreign_objects{1202};
  const auto foreign_root = callback_object(
      foreign_objects, VhdlVhpiObjectKind::Root, 0, "foreign");
  const auto foreign_signal = callback_object(
      foreign_objects,
      VhdlVhpiObjectKind::Signal,
      foreign_root.value,
      "data");
  Scheduler foreign_scheduler;
  VhdlVhpiCallbackSystem foreign{foreign_objects, foreign_scheduler};
  require_vhpi_callback(
      foreign.status(signal_callback.value.identity).error
              == VhdlVhpiCallbackError::CrossSimulation
          && foreign.remove_callback(signal_callback.value.identity)
              == VhdlVhpiCallbackError::CrossSimulation
          && callbacks
                 .register_callback(
                     {VhdlVhpiCallbackKind::Signal,
                      foreign_signal.value,
                      true,
                      0,
                      [](const auto&) {}})
                 .error
              == VhdlVhpiCallbackError::CrossSimulation
          && callbacks
                 .register_callback(
                     {VhdlVhpiCallbackKind::Signal,
                      process,
                      true,
                      0,
                      [](const auto&) {}})
                 .error
              == VhdlVhpiCallbackError::InvalidObject
          && callbacks
                 .register_callback(
                     {VhdlVhpiCallbackKind::StartOfSave,
                      signal,
                      true,
                      0,
                      [](const auto&) {}})
                 .error
              == VhdlVhpiCallbackError::InvalidRequest
          && callbacks.publish(VhdlVhpiCallbackKind::Signal)
              == VhdlVhpiCallbackError::InvalidRequest,
      "VHPI callback ownership/request validation failed");

  std::vector<std::uint64_t> teardown_order;
  auto first_token = std::make_shared<TeardownToken>(
      teardown_order, 1);
  auto second_token = std::make_shared<TeardownToken>(
      teardown_order, 2);
  const auto first_teardown = callbacks.register_callback(
      {VhdlVhpiCallbackKind::Event,
       0,
       true,
       0,
       [token = first_token](const auto&) {
         static_cast<void>(token);
       }});
  const auto second_teardown = callbacks.register_callback(
      {VhdlVhpiCallbackKind::Event,
       0,
       true,
       0,
       [token = second_token](const auto&) {
         static_cast<void>(token);
       }});
  first_token.reset();
  second_token.reset();
  callbacks.teardown();
  require_vhpi_callback(
      first_teardown && second_teardown
          && teardown_order == std::vector<std::uint64_t>{1, 2}
          && callbacks.status(first_teardown.value.identity).value.status
              == VhdlVhpiCallbackStatus::TornDown
          && callbacks.status(second_teardown.value.identity).value.status
              == VhdlVhpiCallbackStatus::TornDown
          && callbacks.publish(
                 VhdlVhpiCallbackKind::Event,
                 VhdlVhpiEventData{
                     root.value, 0, std::nullopt, {}, std::nullopt, 0})
              == VhdlVhpiCallbackError::TornDown
          && callbacks
                 .register_callback(
                     {VhdlVhpiCallbackKind::Event,
                      0,
                      true,
                      0,
                      [](const auto&) {}})
                 .error
              == VhdlVhpiCallbackError::TornDown,
      "VHPI deterministic callback teardown failed");

  VhdlVhpiObjectRegistry invalid_objects{0};
  Scheduler invalid_scheduler;
  VhdlVhpiCallbackSystem invalid{invalid_objects, invalid_scheduler};
  require_vhpi_callback(
      !invalid.valid()
          && invalid
                 .register_callback(
                     {VhdlVhpiCallbackKind::Event,
                      0,
                      true,
                      0,
                      [](const auto&) {}})
                 .error
              == VhdlVhpiCallbackError::InvalidSimulation,
      "VHPI callback system accepted an invalid simulation");
}

}  // namespace fsim::tests::runtime

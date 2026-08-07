// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_callback.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {

namespace {

using fsim::runtime::Logic9;
using fsim::runtime::Scheduler;
using fsim::runtime::SystemVerilogVpiCallbackError;
using fsim::runtime::SystemVerilogVpiCallbackEvent;
using fsim::runtime::SystemVerilogVpiCallbackKind;
using fsim::runtime::SystemVerilogVpiCallbackManager;
using fsim::runtime::SystemVerilogVpiCallbackRegistration;
using fsim::runtime::SystemVerilogVpiCallbackStatus;
using fsim::runtime::SystemVerilogVpiObjectDescriptor;
using fsim::runtime::SystemVerilogVpiObjectError;
using fsim::runtime::SystemVerilogVpiObjectKind;
using fsim::runtime::SystemVerilogVpiObjectRegistry;
using fsim::runtime::SystemVerilogVpiStoredValue;
using fsim::runtime::SystemVerilogVpiTimeFormat;
using fsim::runtime::SystemVerilogVpiTimeProfile;
using fsim::runtime::SystemVerilogVpiTimeService;
using fsim::runtime::SystemVerilogVpiTimeValue;
using fsim::runtime::SystemVerilogVpiTypeInfo;
using fsim::runtime::SystemVerilogVpiValueCategory;
using fsim::runtime::SystemVerilogVpiValueError;
using fsim::runtime::SystemVerilogVpiValueFormat;
using fsim::runtime::SystemVerilogVpiValueWriteData;
using fsim::runtime::make_systemverilog_vpi_stored_value;

void require_vpi_callback(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

SystemVerilogVpiTimeValue callback_delay(const std::uint64_t ticks) {
  SystemVerilogVpiTimeValue result;
  result.high = static_cast<std::uint32_t>(ticks >> 32U);
  result.low = static_cast<std::uint32_t>(ticks & 0xffffffffULL);
  return result;
}

SystemVerilogVpiTypeInfo callback_logic_type() {
  SystemVerilogVpiTypeInfo result;
  result.category = SystemVerilogVpiValueCategory::Logic4;
  result.width = 1;
  return result;
}

SystemVerilogVpiStoredValue callback_scalar(
    const SystemVerilogVpiTypeInfo& type,
    const Logic9 state) {
  SystemVerilogVpiValueWriteData input;
  input.scalar = state;
  const auto converted = make_systemverilog_vpi_stored_value(
      type, SystemVerilogVpiValueFormat::Scalar, input);
  require_vpi_callback(
      static_cast<bool>(converted),
      "VPI callback scalar fixture conversion failed");
  return *converted.value;
}

const SystemVerilogVpiCallbackEvent* find_event(
    const std::vector<SystemVerilogVpiCallbackEvent>& events,
    const SystemVerilogVpiCallbackKind kind) {
  const auto found = std::find_if(
      events.begin(), events.end(), [kind](const auto& event) {
        return event.kind == kind;
      });
  return found == events.end() ? nullptr : &*found;
}

}  // namespace

void test_systemverilog_vpi_callbacks() {
  SystemVerilogVpiObjectRegistry registry{901};
  Scheduler scheduler;
  SystemVerilogVpiTimeService time_service{
      scheduler, SystemVerilogVpiTimeProfile{-9, -12}};
  SystemVerilogVpiCallbackManager manager{
      registry, scheduler, time_service, 10'000};
  require_vpi_callback(
      manager.valid(),
      "VPI callback manager binds one valid simulation, scheduler, and time profile");

  const auto root_a =
      registry.create(SystemVerilogVpiObjectKind::Root, 0, "top_a");
  const auto root_b =
      registry.create(SystemVerilogVpiObjectKind::Root, 0, "top_b");
  const auto type = callback_logic_type();
  const auto signal_a = registry.create(SystemVerilogVpiObjectDescriptor{
      SystemVerilogVpiObjectKind::Variable,
      root_a.value,
      "value",
      std::nullopt,
      type});
  const auto signal_b = registry.create(SystemVerilogVpiObjectDescriptor{
      SystemVerilogVpiObjectKind::Variable,
      root_b.value,
      "value",
      std::nullopt,
      type});
  const auto zero = callback_scalar(type, Logic9::zero);
  const auto one = callback_scalar(type, Logic9::one);
  const auto unknown = callback_scalar(type, Logic9::x);
  require_vpi_callback(
      root_a && root_b && signal_a && signal_b
          && registry.bind_value(signal_a.value, zero)
              == SystemVerilogVpiValueError::None
          && registry.bind_value(signal_b.value, zero)
              == SystemVerilogVpiValueError::None,
      "VPI callback fixtures retain two independent roots and bound values");

  std::vector<SystemVerilogVpiCallbackEvent> events;
  const auto collect = [&](const SystemVerilogVpiCallbackEvent& event) {
    events.push_back(event);
  };
  const auto no_future = manager.register_callback(
      {SystemVerilogVpiCallbackKind::NextTime,
       std::nullopt,
       std::nullopt,
       1,
       collect});
  require_vpi_callback(
      no_future.error == SystemVerilogVpiCallbackError::NoFutureTime
          && manager.registrations() == 0,
      "VPI next-time registration rejects an absent future scheduler time transactionally");

  const auto value_a_first = manager.register_callback(
      {SystemVerilogVpiCallbackKind::ValueChange,
       signal_a.value,
       std::nullopt,
       11,
       collect});
  const auto value_a_second = manager.register_callback(
      {SystemVerilogVpiCallbackKind::ValueChange,
       signal_a.value,
       std::nullopt,
       12,
       collect});
  const auto value_b = manager.register_callback(
      {SystemVerilogVpiCallbackKind::ValueChange,
       signal_b.value,
       std::nullopt,
       13,
       collect});
  const auto after_delay = manager.register_callback(
      {SystemVerilogVpiCallbackKind::AfterDelay,
       std::nullopt,
       callback_delay(5),
       14,
       collect});
  const auto next_time = manager.register_callback(
      {SystemVerilogVpiCallbackKind::NextTime,
       std::nullopt,
       std::nullopt,
       15,
       collect});
  const auto read_write = manager.register_callback(
      {SystemVerilogVpiCallbackKind::ReadWrite,
       signal_a.value,
       std::nullopt,
       16,
       collect});
  const auto read_only = manager.register_callback(
      {SystemVerilogVpiCallbackKind::ReadOnly,
       std::nullopt,
       std::nullopt,
       17,
       collect});
  const auto synchronization = manager.register_callback(
      {SystemVerilogVpiCallbackKind::Synchronization,
       std::nullopt,
       std::nullopt,
       18,
       collect});
  require_vpi_callback(
      value_a_first && value_a_second && value_b && after_delay
          && next_time && read_write && read_only && synchronization
          && manager.registrations() == 8,
      "VPI callback manager accepts every supported callback kind with retained registration identity");

  SystemVerilogVpiObjectRegistry other_registry{902};
  const auto other_root = other_registry.create(
      SystemVerilogVpiObjectKind::Root, 0, "other");
  const auto foreign = manager.register_callback(
      {SystemVerilogVpiCallbackKind::ValueChange,
       other_root.value,
       std::nullopt,
       19,
       collect});
  require_vpi_callback(
      other_root
          && foreign.error
              == SystemVerilogVpiCallbackError::CrossSimulation
          && foreign.object_error
              == SystemVerilogVpiObjectError::CrossSimulation
          && manager.registrations() == 8,
      "VPI callback registration rejects cross-simulation object handles before publication");

  bool observer_reentered{};
  const auto observer = registry.add_value_observer(
      [&](const fsim_vpi_handle_v1 object,
          const SystemVerilogVpiStoredValue&) {
        if (object == signal_a.value) {
          observer_reentered = registry.read_value(
              object, SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::one;
        }
      });
  require_vpi_callback(
      observer.has_value()
          && registry.deposit_value(signal_a.value, one)
              == SystemVerilogVpiValueError::None
          && registry.deposit_value(signal_a.value, one)
              == SystemVerilogVpiValueError::None
          && registry.deposit_value(signal_b.value, one)
              == SystemVerilogVpiValueError::None
          && observer_reentered
          && registry.remove_value_observer(*observer),
      "VPI value observers run after publication and registry unlock while duplicate values remain silent");

  (void)scheduler.run();
  const std::vector<SystemVerilogVpiCallbackKind> expected_order{
      SystemVerilogVpiCallbackKind::Synchronization,
      SystemVerilogVpiCallbackKind::ValueChange,
      SystemVerilogVpiCallbackKind::ValueChange,
      SystemVerilogVpiCallbackKind::ValueChange,
      SystemVerilogVpiCallbackKind::ReadWrite,
      SystemVerilogVpiCallbackKind::ReadOnly,
      SystemVerilogVpiCallbackKind::AfterDelay,
      SystemVerilogVpiCallbackKind::NextTime,
  };
  std::vector<SystemVerilogVpiCallbackKind> actual_order;
  for (const auto& event : events) {
    actual_order.push_back(event.kind);
  }
  require_vpi_callback(
      actual_order == expected_order,
      "VPI callback dispatch maps synchronization, reactive, read-only, and future work to deterministic scheduler regions");

  require_vpi_callback(
      events[1].registration == value_a_first.value
          && events[2].registration == value_a_second.value
          && events[3].registration == value_b.value
          && events[1].object == signal_a.value
          && events[3].object == signal_b.value
          && events[1].value == one && events[3].value == one
          && events[1].user_data == 11
          && events[2].registration_order
              == value_a_second.value.id
          && events[1].simulation_identity == 901
          && events[3].simulation_identity == 901,
      "VPI value-change callbacks retain exact object, copied value, user data, registration order, and multi-root identity");

  const auto* delayed_event = find_event(
      events, SystemVerilogVpiCallbackKind::AfterDelay);
  const auto* next_event = find_event(
      events, SystemVerilogVpiCallbackKind::NextTime);
  require_vpi_callback(
      delayed_event && next_event
          && delayed_event->time.ticks == 5
          && next_event->time.ticks == 5
          && delayed_event->time.value.format
              == SystemVerilogVpiTimeFormat::IntegerTicks
          && delayed_event->time.value.low == 5
          && manager.status(after_delay.value).status
              == SystemVerilogVpiCallbackStatus::Fired
          && manager.status(next_time.value).status
              == SystemVerilogVpiCallbackStatus::Fired
          && manager.status(value_a_first.value).status
              == SystemVerilogVpiCallbackStatus::Active,
      "VPI future callbacks share exact scheduler time while value-change registrations remain active");

  const auto first_count = events.size();
  require_vpi_callback(
      registry.force_value(signal_a.value, unknown)
              == SystemVerilogVpiValueError::None
          && registry.deposit_value(signal_a.value, zero)
              == SystemVerilogVpiValueError::None
          && registry.release_forced_value(signal_a.value)
              == SystemVerilogVpiValueError::None,
      "VPI callback force fixture publishes visible changes and hides deposits beneath force");
  (void)scheduler.run();
  require_vpi_callback(
      events.size() == first_count + 4
          && events[first_count].registration
              == value_a_first.value
          && events[first_count + 1].registration
              == value_a_second.value
          && events[first_count].value == unknown
          && events[first_count + 2].registration
              == value_a_first.value
          && events[first_count + 3].registration
              == value_a_second.value
          && events[first_count + 2].value == zero,
      "VPI value-change dispatch preserves change sequence and registration order across force, hidden deposit, and release");

  SystemVerilogVpiCallbackManager other_manager{
      registry, scheduler, time_service, 20'000};
  require_vpi_callback(
      other_manager.status(value_a_first.value).error
          == SystemVerilogVpiCallbackError::CrossManager,
      "VPI callback handles preserve manager ownership within one simulation");
}
void test_systemverilog_vpi_callback_lifecycle() {
  SystemVerilogVpiObjectRegistry registry{903};
  Scheduler scheduler;
  SystemVerilogVpiTimeService time_service{
      scheduler, SystemVerilogVpiTimeProfile{-9, -12}};
  SystemVerilogVpiCallbackManager manager{
      registry, scheduler, time_service, 30'000};

  const auto root =
      registry.create(SystemVerilogVpiObjectKind::Root, 0, "top");
  const auto type = callback_logic_type();
  const auto signal = registry.create(SystemVerilogVpiObjectDescriptor{
      SystemVerilogVpiObjectKind::Variable,
      root.value,
      "value",
      std::nullopt,
      type});
  const auto zero = callback_scalar(type, Logic9::zero);
  const auto one = callback_scalar(type, Logic9::one);
  require_vpi_callback(
      root && signal
          && registry.bind_value(signal.value, zero)
              == SystemVerilogVpiValueError::None,
      "VPI lifecycle fixtures publish one bound simulation value");

  std::vector<std::string_view> order;
  fsim::runtime::SystemVerilogVpiCallbackHandle victim_handle;
  fsim::runtime::SystemVerilogVpiCallbackHandle nested_handle;
  const auto value_change = manager.register_callback(
      {SystemVerilogVpiCallbackKind::ValueChange,
       signal.value,
       std::nullopt,
       31,
       [&](const SystemVerilogVpiCallbackEvent& event) {
         if (event.value != one
             || event.time.ticks != scheduler.now()) {
           throw std::runtime_error{
               "re-entered VPI value callback observed partial state"};
         }
         order.push_back("value");
       }});
  const auto start_self = manager.register_callback(
      {SystemVerilogVpiCallbackKind::StartOfSimulation,
       std::nullopt,
       std::nullopt,
       32,
       [&](const SystemVerilogVpiCallbackEvent& event) {
         order.push_back("self");
         if (manager.remove_callback(event.registration)
                 != SystemVerilogVpiCallbackError::None
             || manager.remove_callback(victim_handle)
                 != SystemVerilogVpiCallbackError::None) {
           throw std::runtime_error{
               "VPI lifecycle self or peer removal failed"};
         }
         const auto nested = manager.register_callback(
             {SystemVerilogVpiCallbackKind::StartOfSimulation,
              std::nullopt,
              std::nullopt,
              33,
              [&](const SystemVerilogVpiCallbackEvent&) {
                order.push_back("nested");
              }});
         if (!nested) {
           throw std::runtime_error{
               "nested VPI lifecycle registration failed"};
         }
         nested_handle = nested.value;
         if (registry.deposit_value(signal.value, one)
                 != SystemVerilogVpiValueError::None
             || manager.dispatch_lifecycle(
                    SystemVerilogVpiCallbackKind::StartOfSave)
                 != SystemVerilogVpiCallbackError::None) {
           throw std::runtime_error{
               "VPI lifecycle scheduler re-entry failed"};
         }
       }});
  const auto victim = manager.register_callback(
      {SystemVerilogVpiCallbackKind::StartOfSimulation,
       std::nullopt,
       std::nullopt,
       34,
       [&](const SystemVerilogVpiCallbackEvent&) {
         order.push_back("victim");
       }});
  victim_handle = victim.value;
  const auto throwing = manager.register_callback(
      {SystemVerilogVpiCallbackKind::StartOfSimulation,
       std::nullopt,
       std::nullopt,
       35,
       [&](const SystemVerilogVpiCallbackEvent&) {
         order.push_back("throwing");
         throw std::runtime_error{"contained lifecycle callback"};
       }});
  const auto last = manager.register_callback(
      {SystemVerilogVpiCallbackKind::StartOfSimulation,
       std::nullopt,
       std::nullopt,
       36,
       [&](const SystemVerilogVpiCallbackEvent& event) {
         if (registry.read_value(
                 signal.value,
                 SystemVerilogVpiValueFormat::Scalar).scalar
                 != Logic9::one
             || event.user_data != 36) {
           throw std::runtime_error{
               "VPI lifecycle callback observed partial publication"};
         }
         order.push_back("last");
       }});
  const auto nested_save = manager.register_callback(
      {SystemVerilogVpiCallbackKind::StartOfSave,
       std::nullopt,
       std::nullopt,
       37,
       [&](const SystemVerilogVpiCallbackEvent&) {
         order.push_back("save");
       }});
  require_vpi_callback(
      value_change && start_self && victim && throwing && last
          && nested_save
          && manager.dispatch_lifecycle(
                 SystemVerilogVpiCallbackKind::StartOfSimulation)
              == SystemVerilogVpiCallbackError::None,
      "VPI lifecycle start dispatch publishes a complete ordered snapshot");

  (void)scheduler.run();
  require_vpi_callback(
      order == std::vector<std::string_view>{
          "self", "throwing", "last", "save", "value"}
          && manager.status(start_self.value).status
              == SystemVerilogVpiCallbackStatus::Removed
          && manager.status(victim.value).status
              == SystemVerilogVpiCallbackStatus::Removed
          && manager.status(throwing.value).status
              == SystemVerilogVpiCallbackStatus::CallbackFailed
          && manager.status(last.value).status
              == SystemVerilogVpiCallbackStatus::Fired
          && manager.status(nested_save.value).status
              == SystemVerilogVpiCallbackStatus::Fired
          && manager.status(nested_handle).status
              == SystemVerilogVpiCallbackStatus::Active
          && manager.status(value_change.value).status
              == SystemVerilogVpiCallbackStatus::Active,
      "VPI lifecycle dispatch contains exceptions, honors removals, defers nested registration, and schedules re-entry safely");

  require_vpi_callback(
      manager.remove_callback(start_self.value)
              == SystemVerilogVpiCallbackError::NotActive
          && manager.dispatch_lifecycle(
                 SystemVerilogVpiCallbackKind::StartOfSimulation)
              == SystemVerilogVpiCallbackError::None,
      "VPI lifecycle removal distinguishes inactive registrations");
  (void)scheduler.run();
  require_vpi_callback(
      order.back() == std::string_view{"nested"}
          && manager.status(nested_handle).status
              == SystemVerilogVpiCallbackStatus::Fired,
      "VPI nested lifecycle registration begins at the next notification");

  bool delayed_fired{};
  const auto delayed = manager.register_callback(
      {SystemVerilogVpiCallbackKind::AfterDelay,
       std::nullopt,
       callback_delay(10),
       38,
       [&](const SystemVerilogVpiCallbackEvent&) {
         delayed_fired = true;
       }});
  require_vpi_callback(
      delayed
          && manager.remove_callback(delayed.value)
              == SystemVerilogVpiCallbackError::None
          && manager.status(delayed.value).status
              == SystemVerilogVpiCallbackStatus::Removed,
      "VPI callback removal cancels pending scheduler-backed work");
  (void)scheduler.run();
  require_vpi_callback(
      !delayed_fired,
      "removed VPI after-delay callbacks cannot publish");

  const SystemVerilogVpiCallbackKind remaining_lifecycle[] = {
      SystemVerilogVpiCallbackKind::EndOfSimulation,
      SystemVerilogVpiCallbackKind::StartOfReset,
      SystemVerilogVpiCallbackKind::EndOfReset,
      SystemVerilogVpiCallbackKind::EndOfSave,
      SystemVerilogVpiCallbackKind::StartOfRestart,
      SystemVerilogVpiCallbackKind::EndOfRestart,
  };
  bool exact_lifecycle_identity{true};
  for (std::size_t index = 0;
       index < std::size(remaining_lifecycle);
       ++index) {
    const auto kind = remaining_lifecycle[index];
    const auto registration = manager.register_callback(
        {kind,
         std::nullopt,
         std::nullopt,
         100U + index,
         [&, kind, index](const SystemVerilogVpiCallbackEvent& event) {
           exact_lifecycle_identity =
               exact_lifecycle_identity
               && event.kind == kind
               && event.user_data == 100U + index
               && !event.object && !event.value
               && event.simulation_identity == 903
               && event.time.value.format
                   == SystemVerilogVpiTimeFormat::IntegerTicks
               && event.time.ticks == scheduler.now();
         }});
    require_vpi_callback(
        registration
            && manager.dispatch_lifecycle(kind)
                == SystemVerilogVpiCallbackError::None,
        "VPI lifecycle callback registration or dispatch failed");
    (void)scheduler.run();
    require_vpi_callback(
        manager.status(registration.value).status
            == SystemVerilogVpiCallbackStatus::Fired,
        "VPI lifecycle callback did not retain fired status");
  }
  require_vpi_callback(
      exact_lifecycle_identity,
      "VPI lifecycle callbacks retain exact kind, user data, simulation, and scheduler time");

  const auto invalid_lifecycle = manager.register_callback(
      {SystemVerilogVpiCallbackKind::StartOfReset,
       signal.value,
       std::nullopt,
       200,
       [](const SystemVerilogVpiCallbackEvent&) {}});
  SystemVerilogVpiCallbackManager other_manager{
      registry, scheduler, time_service, 40'000};
  require_vpi_callback(
      invalid_lifecycle.error
              == SystemVerilogVpiCallbackError::InvalidRequest
          && manager.dispatch_lifecycle(
                 SystemVerilogVpiCallbackKind::ValueChange)
              == SystemVerilogVpiCallbackError::InvalidKind
          && manager.remove_callback({})
              == SystemVerilogVpiCallbackError::InvalidHandle
          && other_manager.remove_callback(value_change.value)
              == SystemVerilogVpiCallbackError::CrossManager,
      "VPI lifecycle APIs reject object payloads, nonlifecycle dispatch, malformed handles, and cross-manager removal");
}

}  // namespace fsim::tests::runtime

// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_control.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <string>
#include <vector>

namespace fsim::tests::runtime {

namespace {

using namespace fsim::runtime;

void require_vpi_control(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

SystemVerilogVpiTypeInfo control_logic_type() {
  SystemVerilogVpiTypeInfo result;
  result.category = SystemVerilogVpiValueCategory::Logic4;
  result.width = 1;
  return result;
}

SystemVerilogVpiStoredValue control_scalar(
    const SystemVerilogVpiTypeInfo& type,
    const Logic9 state) {
  SystemVerilogVpiValueWriteData input;
  input.scalar = state;
  const auto converted = make_systemverilog_vpi_stored_value(
      type, SystemVerilogVpiValueFormat::Scalar, input);
  require_vpi_control(
      static_cast<bool>(converted),
      "VPI control scalar fixture conversion failed");
  return *converted.value;
}

struct ControlFixture {
  SystemVerilogVpiObjectRegistry registry;
  Scheduler scheduler;
  SystemVerilogVpiTimeService time;
  SystemVerilogVpiCallbackManager callbacks;
  SystemVerilogVpiControlService control;
  SystemVerilogVpiTypeInfo type;
  fsim_vpi_handle_v1 object{};
  SystemVerilogVpiStoredValue zero;
  SystemVerilogVpiStoredValue one;
  SystemVerilogVpiStoredValue unknown;

  explicit ControlFixture(const std::uint64_t identity)
      : registry(identity),
        scheduler(),
        time(scheduler, SystemVerilogVpiTimeProfile{-9, -12}),
        callbacks(registry, scheduler, time, 1'000),
        control(registry, scheduler, callbacks, 50'000),
        type(control_logic_type()),
        zero(control_scalar(type, Logic9::zero)),
        one(control_scalar(type, Logic9::one)),
        unknown(control_scalar(type, Logic9::x)) {
    const auto root =
        registry.create(SystemVerilogVpiObjectKind::Root, 0, "top");
    const auto variable = registry.create(SystemVerilogVpiObjectDescriptor{
        SystemVerilogVpiObjectKind::Variable,
        root.value,
        "value",
        std::nullopt,
        type});
    require_vpi_control(
        root && variable
            && registry.bind_value(variable.value, zero)
                == SystemVerilogVpiValueError::None,
        "VPI control fixture object publication failed");
    object = variable.value;
  }
};

}  // namespace

void test_systemverilog_vpi_control() {
  ControlFixture fixture{904};
  std::vector<SystemVerilogVpiStoredValue> changes;
  const auto value_callback = fixture.callbacks.register_callback(
      {SystemVerilogVpiCallbackKind::ValueChange,
       fixture.object,
       std::nullopt,
       1,
       [&](const SystemVerilogVpiCallbackEvent& event) {
         require_vpi_control(
             event.object == fixture.object && event.value.has_value()
                 && event.simulation_identity == 904,
             "VPI control value callback lost object identity");
         changes.push_back(*event.value);
       }});
  const auto force = fixture.control.submit(
      {SystemVerilogVpiControlOperation::Force,
       fixture.object,
       fixture.one});
  const auto release = fixture.control.submit(
      {SystemVerilogVpiControlOperation::Release,
       fixture.object,
       std::nullopt});
  require_vpi_control(
      value_callback && force && release
          && fixture.control.simulation_identity() == 904
          && fixture.control.state()
              == SystemVerilogVpiControlState::Running,
      "VPI control accepts ordered force/release requests in one simulation");
  (void)fixture.scheduler.run();
  require_vpi_control(
      fixture.control.status(force.value).status
              == SystemVerilogVpiControlStatus::Applied
          && fixture.control.status(release.value).status
              == SystemVerilogVpiControlStatus::Applied
          && changes
              == std::vector<SystemVerilogVpiStoredValue>{
                  fixture.one, fixture.zero}
          && fixture.registry.read_value(
                 fixture.object,
                 SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::zero,
      "VPI force/release controls execute in update order and publish exact reactive values");

  ControlFixture foreign_fixture{905};
  const auto foreign = fixture.control.submit(
      {SystemVerilogVpiControlOperation::Force,
       foreign_fixture.object,
       fixture.one});
  const auto malformed = fixture.control.submit(
      {SystemVerilogVpiControlOperation::Force,
       fixture.object,
       std::nullopt});
  SystemVerilogVpiStoredValue wrong_width;
  wrong_width.payload = PackedLogic4{2, Logic4::zero};
  const auto mismatched = fixture.control.submit(
      {SystemVerilogVpiControlOperation::Force,
       fixture.object,
       wrong_width});
  require_vpi_control(
      foreign.error == SystemVerilogVpiControlError::CrossSimulation
          && foreign.object_error
              == SystemVerilogVpiObjectError::CrossSimulation
          && malformed.error
              == SystemVerilogVpiControlError::InvalidRequest
          && mismatched.error
              == SystemVerilogVpiControlError::InvalidRequest
          && mismatched.value_error
              == SystemVerilogVpiValueError::TypeMismatch,
      "VPI control preflight rejects cross-simulation, incomplete, and mistyped force requests");

  bool retained_future{};
  fixture.scheduler.schedule_after(
      5,
      SchedulerPhase::active,
      1,
      [&](Scheduler&) { retained_future = true; });
  const auto stop = fixture.control.submit(
      {SystemVerilogVpiControlOperation::Stop, std::nullopt, std::nullopt});
  const auto stopped = fixture.scheduler.run();
  require_vpi_control(
      stop && stopped.status == RunStatus::stopped
          && !retained_future
          && fixture.control.status(stop.value).status
              == SystemVerilogVpiControlStatus::Applied
          && fixture.control.state()
              == SystemVerilogVpiControlState::Stopped
          && fixture.control.submit(
                 {SystemVerilogVpiControlOperation::Stop,
                  std::nullopt,
                  std::nullopt}).error
              == SystemVerilogVpiControlError::NotRunning
          && fixture.control.resume()
              == SystemVerilogVpiControlError::None,
      "VPI stop control pauses at postponed safe point with future work retained and resumable status");
  require_vpi_control(
      fixture.scheduler.run().status == RunStatus::completed
          && retained_future
          && fixture.control.state()
              == SystemVerilogVpiControlState::Running,
      "VPI stop resume continues retained scheduler work");

  const auto interactive = fixture.control.submit(
      {SystemVerilogVpiControlOperation::Interactive,
       std::nullopt,
       std::nullopt});
  require_vpi_control(
      interactive
          && fixture.scheduler.run().status == RunStatus::stopped
          && fixture.control.state()
              == SystemVerilogVpiControlState::Interactive
          && fixture.control.resume()
              == SystemVerilogVpiControlError::None
          && fixture.control.resume()
              == SystemVerilogVpiControlError::NotStopped,
      "VPI interactive control retains a distinct resumable state");

  SystemVerilogVpiControlService other_control{
      fixture.registry,
      fixture.scheduler,
      fixture.callbacks,
      60'000};
  require_vpi_control(
      other_control.status(stop.value).error
              == SystemVerilogVpiControlError::CrossControl
          && fixture.control.status({}).error
              == SystemVerilogVpiControlError::InvalidHandle
          && fixture.control.operations() == 4,
      "VPI control handles preserve controller ownership and malformed identity");
}

void test_systemverilog_vpi_reset_and_finish_control() {
  ControlFixture reset_fixture{906};
  std::vector<std::string_view> reset_order;
  const auto start_reset = reset_fixture.callbacks.register_callback(
      {SystemVerilogVpiCallbackKind::StartOfReset,
       std::nullopt,
       std::nullopt,
       1,
       [&](const SystemVerilogVpiCallbackEvent&) {
         reset_order.push_back("start");
       }});
  const auto end_reset = reset_fixture.callbacks.register_callback(
      {SystemVerilogVpiCallbackKind::EndOfReset,
       std::nullopt,
       std::nullopt,
       2,
       [&](const SystemVerilogVpiCallbackEvent&) {
         reset_order.push_back("end");
       }});
  const auto reset_value = reset_fixture.callbacks.register_callback(
      {SystemVerilogVpiCallbackKind::ValueChange,
       reset_fixture.object,
       std::nullopt,
       3,
       [&](const SystemVerilogVpiCallbackEvent& event) {
         if (event.value == reset_fixture.zero) {
           reset_order.push_back("value");
         }
       }});
  require_vpi_control(
      start_reset && end_reset && reset_value
          && reset_fixture.registry.deposit_value(
                 reset_fixture.object, reset_fixture.one)
              == SystemVerilogVpiValueError::None
          && reset_fixture.registry.force_value(
                 reset_fixture.object, reset_fixture.unknown)
              == SystemVerilogVpiValueError::None,
      "VPI reset fixture publishes changed and forced state");
  (void)reset_fixture.scheduler.run();
  reset_order.clear();

  bool discarded_future{};
  reset_fixture.scheduler.schedule_after(
      20,
      SchedulerPhase::active,
      1,
      [&](Scheduler&) { discarded_future = true; });
  const auto reset = reset_fixture.control.submit(
      {SystemVerilogVpiControlOperation::Reset,
       std::nullopt,
       std::nullopt});
  const auto reset_run = reset_fixture.scheduler.run();
  if (!reset || reset_run.status != RunStatus::stopped
      || reset_order
          != std::vector<std::string_view>{"start", "value", "end"}) {
    std::string message{"VPI reset callback order:"};
    for (const auto entry : reset_order) {
      message += " ";
      message += entry;
    }
    throw std::runtime_error{message};
  }
  require_vpi_control(
      reset_fixture.control.status(reset.value).status
              == SystemVerilogVpiControlStatus::Applied
          && reset_fixture.control.state()
              == SystemVerilogVpiControlState::Reset,
      "VPI reset control reaches applied resumable reset status");
  require_vpi_control(
      reset_fixture.registry.read_value(
          reset_fixture.object,
          SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::zero,
      "VPI reset control restores the initial object value");
  require_vpi_control(
      reset_fixture.registry.release_forced_value(
          reset_fixture.object)
              == SystemVerilogVpiValueError::NotForced,
      "VPI reset control clears force state transactionally");
  require_vpi_control(
      reset_fixture.control.resume()
              == SystemVerilogVpiControlError::None
          && reset_fixture.scheduler.now() == 0
          && !reset_fixture.scheduler.has_pending()
          && !discarded_future
          && reset_fixture.control.state()
              == SystemVerilogVpiControlState::Running,
      "VPI reset resume discards old work and rewinds scheduler time/delta identity");

  ControlFixture finish_fixture{907};
  std::vector<std::string_view> finish_order;
  const auto end_simulation = finish_fixture.callbacks.register_callback(
      {SystemVerilogVpiCallbackKind::EndOfSimulation,
       std::nullopt,
       std::nullopt,
       1,
       [&](const SystemVerilogVpiCallbackEvent&) {
         finish_order.push_back("end");
       }});
  bool finish_future{};
  finish_fixture.scheduler.schedule_after(
      10,
      SchedulerPhase::active,
      1,
      [&](Scheduler&) { finish_future = true; });
  const auto finish = finish_fixture.control.submit(
      {SystemVerilogVpiControlOperation::Finish,
       std::nullopt,
       std::nullopt});
  const auto finish_run = finish_fixture.scheduler.run();
  require_vpi_control(
      end_simulation && finish
          && finish_run.status == RunStatus::stopped
          && finish_order == std::vector<std::string_view>{"end"}
          && !finish_future
          && finish_fixture.control.status(finish.value).status
              == SystemVerilogVpiControlStatus::Applied
          && finish_fixture.control.state()
              == SystemVerilogVpiControlState::Finished
          && finish_fixture.control.resume()
              == SystemVerilogVpiControlError::Terminal
          && finish_fixture.control.submit(
                 {SystemVerilogVpiControlOperation::Stop,
                  std::nullopt,
                  std::nullopt}).error
              == SystemVerilogVpiControlError::Terminal,
      "VPI finish control runs end callbacks before entering nonresumable terminal state");
}

void test_systemverilog_vpi_assertion_api() {
  ControlFixture fixture{908};
  const auto root = fixture.registry.find("top");
  const auto assertion = fixture.registry.create(
      SystemVerilogVpiObjectKind::Assertion,
      root.value->handle,
      "request_is_valid");
  const auto non_assertion = fixture.registry.create(
      SystemVerilogVpiObjectKind::Process,
      root.value->handle,
      "monitor");
  std::vector<std::pair<SystemVerilogVpiAssertionControlOperation,
                        std::optional<fsim_vpi_handle_v1>>>
      controls;
  SystemVerilogVpiAssertionApi api{
      fixture.registry,
      fixture.callbacks,
      [&](const SystemVerilogVpiAssertionControlOperation operation,
          const std::optional<fsim_vpi_handle_v1> object) {
        controls.emplace_back(operation, object);
        return true;
      }};
  std::vector<SystemVerilogVpiAssertionOutcome> outcomes;
  for (const auto kind : {
           SystemVerilogVpiCallbackKind::AssertionSuccess,
           SystemVerilogVpiCallbackKind::AssertionFailure,
           SystemVerilogVpiCallbackKind::AssertionVacuous,
           SystemVerilogVpiCallbackKind::AssertionDisabled,
           SystemVerilogVpiCallbackKind::AssertionAborted}) {
    const auto callback = fixture.callbacks.register_callback(
        {kind,
         assertion.value,
         std::nullopt,
         static_cast<std::uint64_t>(outcomes.size()),
         [&](const SystemVerilogVpiCallbackEvent& event) {
           require_vpi_control(
               event.assertion.has_value()
                   && event.object == assertion.value,
               "assertion API callback lost standardized event identity");
           outcomes.push_back(event.assertion->outcome);
         }});
    require_vpi_control(
        static_cast<bool>(callback),
        "assertion API callback registration failed");
  }

  SystemVerilogVpiAssertionEvent event;
  event.kind = SystemVerilogVpiAssertionKind::Assertion;
  event.name = "request_is_valid";
  event.process = "top.monitor";
  event.instance_identity = "top";
  event.slot = 4;
  event.source_span = 17;
  for (const auto outcome : {
           SystemVerilogVpiAssertionOutcome::Success,
           SystemVerilogVpiAssertionOutcome::Failure,
           SystemVerilogVpiAssertionOutcome::Vacuous,
           SystemVerilogVpiAssertionOutcome::Disabled,
           SystemVerilogVpiAssertionOutcome::Aborted}) {
    event.outcome = outcome;
    require_vpi_control(
        api.observe(assertion.value, event)
            == SystemVerilogVpiAssertionApiError::None,
        "assertion API rejected a standardized completion outcome");
  }
  const auto initial = api.status(assertion.value);
  require_vpi_control(
      assertion && non_assertion && api.valid()
          && api.simulation_identity() == 908
          && api.observed_assertions() == 1
          && initial && initial.enabled
          && initial.kind == SystemVerilogVpiAssertionKind::Assertion
          && initial.statistics
              == SystemVerilogVpiAssertionStatistics{4, 1, 1, 1, 1, 1, false}
          && outcomes
              == std::vector<SystemVerilogVpiAssertionOutcome>{
                  SystemVerilogVpiAssertionOutcome::Success,
                  SystemVerilogVpiAssertionOutcome::Failure,
                  SystemVerilogVpiAssertionOutcome::Vacuous,
                  SystemVerilogVpiAssertionOutcome::Disabled,
                  SystemVerilogVpiAssertionOutcome::Aborted},
      "assertion API retains exact non-disabled attempts and per-outcome counts");

  for (const auto [name, kind] : {
           std::pair{"assumption", SystemVerilogVpiAssertionKind::Assumption},
           std::pair{"cover", SystemVerilogVpiAssertionKind::Cover},
           std::pair{"restriction",
                     SystemVerilogVpiAssertionKind::Restriction}}) {
    const auto object = fixture.registry.create(
        SystemVerilogVpiObjectKind::Assertion, root.value->handle, name);
    event.name = name;
    event.kind = kind;
    event.outcome = SystemVerilogVpiAssertionOutcome::Success;
    require_vpi_control(
        object
            && api.observe(object.value, event)
                == SystemVerilogVpiAssertionApiError::None
            && api.status(object.value).kind == kind
            && api.status(object.value).statistics.attempts == 1
            && api.status(object.value).statistics.successes == 1,
        "assertion API lost an assume, cover, or restrict completion kind");
  }
  require_vpi_control(
      api.observed_assertions() == 4,
      "assertion API did not retain all four standardized assertion kinds");

  require_vpi_control(
      api.control(SystemVerilogVpiAssertionControlOperation::Disable,
                  assertion.value)
              == SystemVerilogVpiAssertionApiError::None
          && !api.status(assertion.value).enabled
          && api.control(SystemVerilogVpiAssertionControlOperation::Kill,
                         assertion.value)
              == SystemVerilogVpiAssertionApiError::None
          && api.control(SystemVerilogVpiAssertionControlOperation::Enable)
              == SystemVerilogVpiAssertionApiError::None
          && api.status(assertion.value).enabled
          && api.control(SystemVerilogVpiAssertionControlOperation::Reset)
              == SystemVerilogVpiAssertionApiError::None
          && api.status(assertion.value).statistics
              == SystemVerilogVpiAssertionStatistics{}
          && controls.size() == 4,
      "assertion API delegates object and global controls before updating observable state");

  ControlFixture foreign{909};
  const auto foreign_root = foreign.registry.find("top");
  const auto foreign_assertion = foreign.registry.create(
      SystemVerilogVpiObjectKind::Assertion,
      foreign_root.value->handle,
      "foreign_assertion");
  event.outcome = SystemVerilogVpiAssertionOutcome::Success;
  event.kind = SystemVerilogVpiAssertionKind::Assertion;
  event.name.assign((1U << 20U) + 1U, 'x');
  const auto oversized = api.observe(assertion.value, event);
  event.name = "request_is_valid";
  event.kind = static_cast<SystemVerilogVpiAssertionKind>(99);
  const auto unknown_kind = api.observe(assertion.value, event);
  event.kind = SystemVerilogVpiAssertionKind::Assertion;
  require_vpi_control(
      oversized == SystemVerilogVpiAssertionApiError::InvalidRequest
          && unknown_kind == SystemVerilogVpiAssertionApiError::InvalidRequest
          && api.status(non_assertion.value).error
              == SystemVerilogVpiAssertionApiError::InvalidObject
          && api.status(foreign_assertion.value).error
              == SystemVerilogVpiAssertionApiError::CrossSimulation
          && api.observe(foreign_assertion.value, event)
              == SystemVerilogVpiAssertionApiError::CrossSimulation
          && api.control(
                 static_cast<SystemVerilogVpiAssertionControlOperation>(99))
              == SystemVerilogVpiAssertionApiError::InvalidOperation,
      "assertion API rejects excessive text, unknown enums, wrong-kind, cross-simulation, and unknown control requests");

  SystemVerilogVpiAssertionApi failing{
      fixture.registry,
      fixture.callbacks,
      [](SystemVerilogVpiAssertionControlOperation,
         std::optional<fsim_vpi_handle_v1>) { return false; }};
  require_vpi_control(
      failing.control(SystemVerilogVpiAssertionControlOperation::Disable,
                      assertion.value)
              == SystemVerilogVpiAssertionApiError::ControlFailure
          && failing.status(assertion.value).enabled,
      "assertion API publishes no partial state when common control rejects a request");
}

}  // namespace fsim::tests::runtime

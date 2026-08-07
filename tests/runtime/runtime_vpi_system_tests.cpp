// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_system.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::tests::runtime {

namespace {

using namespace fsim::runtime;

void require_vpi_system(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

SystemVerilogVpiTypeInfo system_logic_type(const std::uint32_t width) {
  SystemVerilogVpiTypeInfo result;
  result.category = SystemVerilogVpiValueCategory::Logic4;
  result.width = width;
  return result;
}

SystemVerilogVpiStoredValue system_logic_value(
    const std::uint32_t width,
    const Logic4 fill) {
  SystemVerilogVpiStoredValue result;
  result.payload = PackedLogic4{width, fill};
  return result;
}

struct SystemFixture {
  SystemVerilogVpiObjectRegistry objects;
  SystemVerilogVpiSystemRegistry systems;
  fsim_vpi_handle_v1 root{};
  fsim_vpi_handle_v1 module{};
  fsim_vpi_handle_v1 variable{};

  explicit SystemFixture(const std::uint64_t identity)
      : objects(identity), systems(objects) {
    const auto created_root =
        objects.create(SystemVerilogVpiObjectKind::Root, 0, "top");
    const auto created_module = objects.create(
        SystemVerilogVpiObjectKind::Module,
        created_root.value,
        "dut");
    const auto created_variable = objects.create(
        SystemVerilogVpiObjectKind::Variable,
        created_module.value,
        "value");
    require_vpi_system(
        created_root && created_module && created_variable,
        "VPI system-call fixture scope creation failed");
    root = created_root.value;
    module = created_module.value;
    variable = created_variable.value;
  }
};

SystemVerilogVpiSystemRegistration passing_task(std::string name) {
  SystemVerilogVpiSystemRegistration registration;
  registration.kind = SystemVerilogVpiSystemCallableKind::Task;
  registration.name = std::move(name);
  registration.compiletf = [](const auto&) {
    return SystemVerilogVpiSystemCallbackResult{};
  };
  registration.calltf = [](const auto&) {
    return SystemVerilogVpiSystemCallbackResult{};
  };
  return registration;
}

SystemVerilogVpiSystemRegistration passing_function(
    std::string name,
    const std::uint32_t width) {
  SystemVerilogVpiSystemRegistration registration;
  registration.kind = SystemVerilogVpiSystemCallableKind::Function;
  registration.name = std::move(name);
  registration.return_type = system_logic_type(width);
  registration.compiletf = [](const auto&) {
    return SystemVerilogVpiSystemCallbackResult{};
  };
  registration.sizetf = [width](const auto&) {
    return SystemVerilogVpiSystemSizeResult{true, width, {}};
  };
  registration.calltf = [](const auto&) {
    return SystemVerilogVpiSystemCallbackResult{};
  };
  return registration;
}

}  // namespace

void test_systemverilog_vpi_system_registration_and_execution() {
  SystemFixture fixture{910};
  std::vector<std::string> phases;
  const std::vector<SystemVerilogVpiStoredValue> expected_arguments{
      system_logic_value(2, Logic4::one),
      SystemVerilogVpiStoredValue{std::string{"second"}, std::nullopt},
      SystemVerilogVpiStoredValue{std::uint64_t{37}, std::nullopt}};

  SystemVerilogVpiSystemRegistration function;
  function.kind = SystemVerilogVpiSystemCallableKind::Function;
  function.name = "$ordered_function";
  function.return_type = system_logic_type(17);
  const auto inspect = [&](const SystemVerilogVpiSystemInvocation& call,
                           const std::string& phase) {
    require_vpi_system(
        call.kind == SystemVerilogVpiSystemCallableKind::Function
            && call.name == "$ordered_function"
            && call.return_type.has_value()
            && call.return_type->category
                == SystemVerilogVpiValueCategory::Logic4
            && call.return_type->width == 17
            && call.scope.handle == fixture.module
            && call.scope.full_name == "top.dut"
            && call.arguments.size() == expected_arguments.size()
            && std::equal(
                call.arguments.begin(),
                call.arguments.end(),
                expected_arguments.begin()),
        "VPI system function lost kind, return profile, scope, or argument order");
    phases.push_back(phase);
  };
  function.compiletf = [&](const auto& call) {
    inspect(call, "compile");
    return SystemVerilogVpiSystemCallbackResult{};
  };
  function.sizetf = [&](const auto& call) {
    inspect(call, "size");
    return SystemVerilogVpiSystemSizeResult{true, 17, {}};
  };
  function.calltf = [&](const auto& call) {
    inspect(call, "call");
    require_vpi_system(
        fixture.systems.publish_result(
            call.call, system_logic_value(17, Logic4::zero))
            == SystemVerilogVpiSystemError::None,
        "VPI system function could not publish its exact typed result");
    return SystemVerilogVpiSystemCallbackResult{};
  };
  require_vpi_system(
      static_cast<bool>(
          fixture.systems.register_callable(std::move(function))),
      "VPI system function registration failed");
  const auto function_result = fixture.systems.execute(
      "$ordered_function", fixture.module, expected_arguments);
  require_vpi_system(
      function_result
          && function_result.phase == SystemVerilogVpiSystemPhase::Call
          && function_result.kind
              == SystemVerilogVpiSystemCallableKind::Function
          && function_result.return_type.has_value()
          && function_result.return_type->width == 17
          && function_result.value
              == system_logic_value(17, Logic4::zero)
          && phases
              == std::vector<std::string>{"compile", "size", "call"},
      "VPI system function did not execute compiletf, sizetf, and calltf in order");

  bool task_size_called{};
  auto task = passing_task("$ordered_task");
  task.compiletf = [&](const auto& call) {
    require_vpi_system(
        call.kind == SystemVerilogVpiSystemCallableKind::Task
            && !call.return_type && call.scope.handle == fixture.root,
        "VPI system task exposed a function return profile");
    phases.push_back("task-compile");
    return SystemVerilogVpiSystemCallbackResult{};
  };
  task.calltf = [&](const auto&) {
    phases.push_back("task-call");
    return SystemVerilogVpiSystemCallbackResult{};
  };
  require_vpi_system(
      fixture.systems.register_callable(std::move(task))
          && fixture.systems.execute("$ordered_task", fixture.root)
          && !task_size_called
          && phases[3] == "task-compile"
          && phases[4] == "task-call",
      "VPI system task did not skip sizetf while preserving phase order");
}

void test_systemverilog_vpi_system_failures() {
  SystemFixture fixture{911};
  const auto initial_count = fixture.systems.registrations();
  auto duplicate = passing_task("$duplicate");
  require_vpi_system(
      static_cast<bool>(
          fixture.systems.register_callable(std::move(duplicate))),
      "VPI system duplicate fixture registration failed");
  const auto duplicate_result =
      fixture.systems.register_callable(passing_task("$duplicate"));

  auto malformed_name = passing_task("not_a_system_task");
  const auto malformed_name_result =
      fixture.systems.register_callable(std::move(malformed_name));
  auto invalid_kind = passing_task("$invalid_kind");
  invalid_kind.kind = static_cast<SystemVerilogVpiSystemCallableKind>(99);
  const auto invalid_kind_result =
      fixture.systems.register_callable(std::move(invalid_kind));
  auto invalid_task = passing_task("$invalid_task");
  invalid_task.return_type = system_logic_type(1);
  const auto invalid_task_result =
      fixture.systems.register_callable(std::move(invalid_task));
  auto invalid_function = passing_function("$invalid_function", 8);
  invalid_function.sizetf = {};
  const auto invalid_function_result =
      fixture.systems.register_callable(std::move(invalid_function));
  require_vpi_system(
      duplicate_result.error
              == SystemVerilogVpiSystemError::DuplicateName
          && malformed_name_result.error
              == SystemVerilogVpiSystemError::InvalidName
          && invalid_kind_result.error
              == SystemVerilogVpiSystemError::InvalidKind
          && invalid_task_result.error
              == SystemVerilogVpiSystemError::InvalidProfile
          && invalid_function_result.error
              == SystemVerilogVpiSystemError::InvalidProfile
          && fixture.systems.registrations() == initial_count + 1,
      "VPI system registration failures published partial entries");

  SystemFixture foreign{912};
  const auto foreign_scope =
      fixture.systems.execute("$duplicate", foreign.module);
  const auto value_scope =
      fixture.systems.execute("$duplicate", fixture.variable);
  const auto missing =
      fixture.systems.execute("$missing", fixture.root);
  require_vpi_system(
      foreign_scope.error
              == SystemVerilogVpiSystemError::CrossSimulation
          && value_scope.error
              == SystemVerilogVpiSystemError::InvalidScope
          && missing.error == SystemVerilogVpiSystemError::NotFound,
      "VPI system execution accepted a foreign, non-scope, or missing callable");

  bool size_mismatch_called{};
  auto size_mismatch = passing_function("$size_mismatch", 8);
  size_mismatch.sizetf = [](const auto&) {
    return SystemVerilogVpiSystemSizeResult{true, 7, {}};
  };
  size_mismatch.calltf = [&](const auto&) {
    size_mismatch_called = true;
    return SystemVerilogVpiSystemCallbackResult{};
  };
  require_vpi_system(
      static_cast<bool>(
          fixture.systems.register_callable(std::move(size_mismatch))),
      "VPI system size mismatch fixture registration failed");
  const auto size_mismatch_result =
      fixture.systems.execute("$size_mismatch", fixture.root);
  require_vpi_system(
      size_mismatch_result.error
              == SystemVerilogVpiSystemError::SizeMismatch
          && size_mismatch_result.phase
              == SystemVerilogVpiSystemPhase::Size
          && !size_mismatch_called,
      "VPI system sizetf mismatch did not prevent calltf");

  bool rejected_call_called{};
  auto rejected_size = passing_function("$rejected_size", 8);
  rejected_size.sizetf = [](const auto&) {
    return SystemVerilogVpiSystemSizeResult{
        false, 8, "owned size diagnostic"};
  };
  rejected_size.calltf = [&](const auto&) {
    rejected_call_called = true;
    return SystemVerilogVpiSystemCallbackResult{};
  };
  require_vpi_system(
      static_cast<bool>(
          fixture.systems.register_callable(std::move(rejected_size))),
      "VPI system rejected-size fixture registration failed");
  const auto rejected_size_result =
      fixture.systems.execute("$rejected_size", fixture.root);
  require_vpi_system(
      rejected_size_result.error
              == SystemVerilogVpiSystemError::SizeRejected
          && rejected_size_result.phase
              == SystemVerilogVpiSystemPhase::Size
          && rejected_size_result.diagnostic == "owned size diagnostic"
          && !rejected_call_called,
      "VPI system sizetf rejection lost its owned diagnostic or continued");

  bool compile_tail_called{};
  auto compile_exception = passing_task("$compile_exception");
  compile_exception.compiletf = [](const auto&)
      -> SystemVerilogVpiSystemCallbackResult {
    throw std::runtime_error("compile boundary");
  };
  compile_exception.calltf = [&](const auto&) {
    compile_tail_called = true;
    return SystemVerilogVpiSystemCallbackResult{};
  };
  require_vpi_system(
      static_cast<bool>(
          fixture.systems.register_callable(std::move(compile_exception))),
      "VPI system compile exception fixture registration failed");
  const auto compile_result =
      fixture.systems.execute("$compile_exception", fixture.root);

  auto size_exception = passing_function("$size_exception", 8);
  size_exception.sizetf = [](const auto&)
      -> SystemVerilogVpiSystemSizeResult {
    throw std::runtime_error("size boundary");
  };
  require_vpi_system(
      static_cast<bool>(
          fixture.systems.register_callable(std::move(size_exception))),
      "VPI system size exception fixture registration failed");
  const auto size_result =
      fixture.systems.execute("$size_exception", fixture.root);

  auto call_exception = passing_function("$call_exception", 8);
  call_exception.calltf = [](const auto&)
      -> SystemVerilogVpiSystemCallbackResult { throw 17; };
  require_vpi_system(
      static_cast<bool>(
          fixture.systems.register_callable(std::move(call_exception))),
      "VPI system call exception fixture registration failed");
  const auto call_result =
      fixture.systems.execute("$call_exception", fixture.root);
  require_vpi_system(
      compile_result.error
              == SystemVerilogVpiSystemError::CallbackException
          && compile_result.phase
              == SystemVerilogVpiSystemPhase::Compile
          && compile_result.diagnostic == "compile boundary"
          && !compile_tail_called
          && size_result.error
              == SystemVerilogVpiSystemError::CallbackException
          && size_result.phase == SystemVerilogVpiSystemPhase::Size
          && size_result.diagnostic == "size boundary"
          && call_result.error
              == SystemVerilogVpiSystemError::CallbackException
          && call_result.phase == SystemVerilogVpiSystemPhase::Call
          && call_result.diagnostic
              == "calltf raised a non-standard exception",
      "VPI system callback exceptions crossed a phase boundary or lost diagnostics");
}

void test_systemverilog_vpi_system_handles_and_reentry() {
  SystemFixture fixture{913};
  const auto first = system_logic_value(3, Logic4::one);
  const auto second = system_logic_value(2, Logic4::zero);
  const auto published = system_logic_value(4, Logic4::one);
  SystemVerilogVpiSystemRegistrationHandle inner_registration;
  SystemVerilogVpiSystemError early_publication{
      SystemVerilogVpiSystemError::None};
  SystemVerilogVpiSystemError wrong_publication{
      SystemVerilogVpiSystemError::None};
  SystemVerilogVpiSystemError duplicate_publication{
      SystemVerilogVpiSystemError::None};
  SystemVerilogVpiSystemCallHandle inner_call;

  auto inner = passing_function("$inner_handle", 4);
  inner.user_data = 0x111U;
  inner.compiletf = [&](const auto& invocation) {
    require_vpi_system(
        invocation.registration == inner_registration
            && invocation.registration_user_data == 0x111U
            && invocation.call_user_data == 0x444U,
        "VPI system compiletf lost registration or call user data");
    early_publication = fixture.systems.publish_result(
        invocation.call, published);
    return SystemVerilogVpiSystemCallbackResult{};
  };
  inner.calltf = [&](const auto& invocation) {
    inner_call = invocation.call;
    require_vpi_system(
        invocation.argument_handles.size() == 2
            && invocation.argument_handles[0].call == invocation.call.id
            && invocation.argument_handles[0].ordinal == 1
            && invocation.argument_handles[1].ordinal == 2,
        "VPI system calltf lost ordered argument handles");
    const auto first_argument =
        fixture.systems.argument(invocation.call, 0);
    const auto second_argument =
        fixture.systems.argument(invocation.argument_handles[1]);
    const auto active = fixture.systems.call(invocation.call);
    require_vpi_system(
        first_argument && second_argument && active
            && first_argument.value == first
            && second_argument.value == second
            && active.state == SystemVerilogVpiSystemCallState::Active
            && active.registration == inner_registration
            && active.argument_count == 2
            && active.registration_user_data == 0x111U
            && active.call_user_data == 0x444U,
        "VPI system active call/argument lookup lost identity, values, or user data");
    wrong_publication = fixture.systems.publish_result(
        invocation.call, system_logic_value(3, Logic4::one));
    require_vpi_system(
        fixture.systems.publish_result(invocation.call, published)
            == SystemVerilogVpiSystemError::None,
        "VPI system function rejected its exact typed result");
    duplicate_publication = fixture.systems.publish_result(
        invocation.call, published);
    return SystemVerilogVpiSystemCallbackResult{};
  };
  const auto registered_inner =
      fixture.systems.register_callable(std::move(inner));
  require_vpi_system(
      static_cast<bool>(registered_inner),
      "VPI system inner function registration failed");
  inner_registration = registered_inner.value;

  SystemVerilogVpiSystemRegistrationHandle outer_registration;
  auto outer = passing_function("$outer_handle", 4);
  outer.user_data = 0x222U;
  outer.calltf = [&](const auto& invocation) {
    require_vpi_system(
        invocation.registration == outer_registration
            && invocation.registration_user_data == 0x222U
            && invocation.call_user_data == 0x333U,
        "VPI system outer call lost independent user data");
    const auto nested = fixture.systems.execute(
        "$inner_handle",
        fixture.module,
        {first, second},
        0x444U);
    require_vpi_system(
        nested && nested.call != invocation.call
            && nested.value == published,
        "VPI system nested call did not retain an independent result/handle");
    require_vpi_system(
        fixture.systems.publish_result(invocation.call, published)
            == SystemVerilogVpiSystemError::None,
        "VPI system outer function result publication failed");
    return SystemVerilogVpiSystemCallbackResult{};
  };
  const auto registered_outer =
      fixture.systems.register_callable(std::move(outer));
  require_vpi_system(
      static_cast<bool>(registered_outer),
      "VPI system outer function registration failed");
  outer_registration = registered_outer.value;

  require_vpi_system(
      static_cast<bool>(fixture.systems.register_callable(
          passing_function("$missing_result", 4))),
      "VPI system missing-result fixture registration failed");

  std::vector<SystemVerilogVpiSystemCallHandle> reentrant_calls;
  std::vector<SystemVerilogVpiSystemError> active_release_errors;
  auto reentrant = passing_task("$reentrant_task");
  reentrant.user_data = 0x555U;
  reentrant.calltf = [&](const auto& invocation) {
    reentrant_calls.push_back(invocation.call);
    active_release_errors.push_back(
        fixture.systems.release_call(invocation.call));
    require_vpi_system(
        invocation.registration_user_data == 0x555U
            && (invocation.call_user_data == 1U
                || invocation.call_user_data == 2U)
            && fixture.systems.publish_result(
                   invocation.call, published)
                == SystemVerilogVpiSystemError::InvalidResult,
        "VPI system reentrant task lost user data or accepted a result");
    if (invocation.call_user_data == 1U) {
      require_vpi_system(
          static_cast<bool>(fixture.systems.execute(
              "$reentrant_task", fixture.root, {}, 2U)),
          "VPI system same-registration reentry failed");
    }
    return SystemVerilogVpiSystemCallbackResult{};
  };
  require_vpi_system(
      static_cast<bool>(
          fixture.systems.register_callable(std::move(reentrant)))
          && fixture.systems.seal_registrations()
              == SystemVerilogVpiSystemError::None,
      "VPI system reentry registration/sealing failed");

  const auto outer_result = fixture.systems.execute(
      "$outer_handle", fixture.module, {first}, 0x333U);
  const auto reentrant_result = fixture.systems.execute(
      "$reentrant_task", fixture.root, {}, 1U);
  const auto missing_result = fixture.systems.execute(
      "$missing_result", fixture.root);
  const auto outer_call = fixture.systems.call(outer_result.call);
  const auto outer_argument =
      fixture.systems.argument(outer_result.call, 0);
  require_vpi_system(
      outer_result && reentrant_result && outer_call && outer_argument
          && outer_result.value == published
          && outer_call.state == SystemVerilogVpiSystemCallState::Completed
          && outer_call.registration == outer_registration
          && outer_call.value == published
          && outer_call.call_user_data == 0x333U
          && outer_argument.value == first
          && early_publication
              == SystemVerilogVpiSystemError::InvalidResult
          && wrong_publication
              == SystemVerilogVpiSystemError::ResultTypeMismatch
          && duplicate_publication
              == SystemVerilogVpiSystemError::ResultAlreadyPublished
          && missing_result.error
              == SystemVerilogVpiSystemError::ResultNotPublished
          && fixture.systems.call(missing_result.call).state
              == SystemVerilogVpiSystemCallState::Failed
          && reentrant_calls.size() == 2
          && reentrant_calls[0] != reentrant_calls[1]
          && active_release_errors
              == std::vector<SystemVerilogVpiSystemError>{
                  SystemVerilogVpiSystemError::ActiveCall,
                  SystemVerilogVpiSystemError::ActiveCall}
          && fixture.systems.calls() == 5,
      "VPI system retained handles/results or nested/reentrant isolation failed");

  const auto retained_argument = outer_argument.handle;
  require_vpi_system(
      fixture.systems.release_call(inner_call)
              == SystemVerilogVpiSystemError::None
          && fixture.systems.release_call(outer_result.call)
              == SystemVerilogVpiSystemError::None
          && fixture.systems.release_call(reentrant_calls[0])
              == SystemVerilogVpiSystemError::None
          && fixture.systems.release_call(reentrant_calls[1])
              == SystemVerilogVpiSystemError::None
          && fixture.systems.release_call(missing_result.call)
              == SystemVerilogVpiSystemError::None
          && fixture.systems.calls() == 0
          && fixture.systems.call(outer_result.call).error
              == SystemVerilogVpiSystemError::StaleHandle
          && fixture.systems.argument(retained_argument).error
              == SystemVerilogVpiSystemError::StaleHandle,
      "VPI system explicit call release did not stale call/argument handles");
}

void test_systemverilog_vpi_system_registration_lifecycle() {
  SystemFixture fixture{914};
  auto lifecycle_task = passing_task("$lifecycle_task");
  lifecycle_task.user_data = 0x777U;
  SystemVerilogVpiSystemError active_release{
      SystemVerilogVpiSystemError::None};
  lifecycle_task.calltf = [&](const auto& invocation) {
    active_release = fixture.systems.release_call(invocation.call);
    return SystemVerilogVpiSystemCallbackResult{};
  };
  const auto registered =
      fixture.systems.register_callable(std::move(lifecycle_task));
  const auto duplicate =
      fixture.systems.register_callable(passing_task("$lifecycle_task"));
  require_vpi_system(
      registered
          && duplicate.error
              == SystemVerilogVpiSystemError::DuplicateName
          && !duplicate.diagnostic.empty()
          && fixture.systems.seal_registrations()
              == SystemVerilogVpiSystemError::None
          && fixture.systems.registrations_sealed(),
      "VPI system duplicate diagnostics or explicit sealing failed");
  const auto late =
      fixture.systems.register_callable(passing_task("$late_task"));
  require_vpi_system(
      late.error == SystemVerilogVpiSystemError::LateRegistration
          && !late.diagnostic.empty()
          && fixture.systems.registrations() == 1,
      "VPI system late registration did not reject transactionally");

  const auto execution = fixture.systems.execute(
      "$lifecycle_task",
      fixture.root,
      {system_logic_value(1, Logic4::zero)},
      0x888U);
  const auto argument = fixture.systems.argument(execution.call, 0);
  require_vpi_system(
      execution && argument
          && active_release == SystemVerilogVpiSystemError::ActiveCall
          && fixture.systems.unregister_callable(registered.value)
              == SystemVerilogVpiSystemError::None
          && fixture.systems.unregister_callable(registered.value)
              == SystemVerilogVpiSystemError::StaleHandle
          && fixture.systems.registrations() == 0
          && fixture.systems.execute(
                 "$lifecycle_task", fixture.root).error
              == SystemVerilogVpiSystemError::NotFound,
      "VPI system unregister did not preserve active-call completion and stale registration identity");

  SystemFixture foreign{915};
  require_vpi_system(
      foreign.systems.call(execution.call).error
              == SystemVerilogVpiSystemError::CrossRegistry
          && foreign.systems.argument(argument.handle).error
              == SystemVerilogVpiSystemError::CrossRegistry
          && foreign.systems.release_call(execution.call)
              == SystemVerilogVpiSystemError::CrossRegistry
          && foreign.systems.unregister_callable(registered.value)
              == SystemVerilogVpiSystemError::CrossRegistry,
      "VPI system handles crossed registry ownership");

  fixture.systems.teardown();
  fixture.systems.teardown();
  const auto after_teardown =
      fixture.systems.register_callable(passing_task("$after_teardown"));
  require_vpi_system(
      !fixture.systems.valid() && fixture.systems.calls() == 0
          && fixture.systems.registrations() == 0
          && fixture.systems.call(execution.call).error
              == SystemVerilogVpiSystemError::Closed
          && fixture.systems.argument(argument.handle).error
              == SystemVerilogVpiSystemError::Closed
          && fixture.systems.release_call(execution.call)
              == SystemVerilogVpiSystemError::Closed
          && fixture.systems.unregister_callable(registered.value)
              == SystemVerilogVpiSystemError::Closed
          && fixture.systems.seal_registrations()
              == SystemVerilogVpiSystemError::Closed
          && fixture.systems.execute(
                 "$lifecycle_task", fixture.root).error
              == SystemVerilogVpiSystemError::Closed
          && after_teardown.error
              == SystemVerilogVpiSystemError::Closed,
      "VPI system deterministic teardown did not invalidate every retained surface");

  SystemFixture active_teardown{916};
  bool size_after_teardown{};
  bool call_after_teardown{};
  auto teardown_function = passing_function("$teardown_active", 4);
  teardown_function.compiletf = [&](const auto&) {
    active_teardown.systems.teardown();
    return SystemVerilogVpiSystemCallbackResult{};
  };
  teardown_function.sizetf = [&](const auto&) {
    size_after_teardown = true;
    return SystemVerilogVpiSystemSizeResult{true, 4, {}};
  };
  teardown_function.calltf = [&](const auto&) {
    call_after_teardown = true;
    return SystemVerilogVpiSystemCallbackResult{};
  };
  require_vpi_system(
      static_cast<bool>(active_teardown.systems.register_callable(
          std::move(teardown_function))),
      "VPI system active-teardown fixture registration failed");
  const auto torn_down = active_teardown.systems.execute(
      "$teardown_active", active_teardown.root);
  require_vpi_system(
      torn_down.error == SystemVerilogVpiSystemError::Closed
          && torn_down.phase == SystemVerilogVpiSystemPhase::Compile
          && !size_after_teardown && !call_after_teardown
          && active_teardown.systems.calls() == 0
          && active_teardown.systems.registrations() == 0,
      "VPI system callback-initiated teardown executed later phases or retained handles");
}

}  // namespace fsim::tests::runtime

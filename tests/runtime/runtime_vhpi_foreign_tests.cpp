// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/vhpi_foreign.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require_vhpi_foreign(
    const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult foreign_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

}  // namespace

void test_vhdl_vhpi_foreign() {
  using fsim::runtime::PackedLogic9;
  using fsim::runtime::VhdlVhpiCallState;
  using fsim::runtime::VhdlVhpiForeignError;
  using fsim::runtime::VhdlVhpiForeignKind;
  using fsim::runtime::VhdlVhpiForeignParameter;
  using fsim::runtime::VhdlVhpiForeignRegistration;
  using fsim::runtime::VhdlVhpiForeignState;
  using fsim::runtime::VhdlVhpiForeignSystem;
  using fsim::runtime::VhdlVhpiForeignValue;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiParameterMode;
  using fsim::runtime::VhdlVhpiScalarKind;
  using fsim::runtime::VhdlVhpiTypeDescriptor;
  using fsim::runtime::VhdlVhpiTypeError;
  using fsim::runtime::VhdlVhpiTypeSystem;

  VhdlVhpiObjectRegistry objects{1301};
  const auto root = foreign_object(
      objects, VhdlVhpiObjectKind::Root, 0, "work");
  require_vhpi_foreign(
      static_cast<bool>(root), "VHPI foreign root creation failed");
  const auto create =
      [&](const VhdlVhpiObjectKind kind, const std::string& name) {
        const auto object = foreign_object(
            objects, kind, root.value, name);
        require_vhpi_foreign(
            static_cast<bool>(object),
            "VHPI foreign object creation failed");
        return object.value;
      };
  const auto input_type = create(VhdlVhpiObjectKind::Type, "input_t");
  const auto result_type = create(VhdlVhpiObjectKind::Type, "result_t");
  const auto scope = create(VhdlVhpiObjectKind::Region, "scope");
  const auto signal = create(VhdlVhpiObjectKind::Signal, "not_a_scope");
  VhdlVhpiTypeSystem types{objects};
  require_vhpi_foreign(
      types.publish(
          input_type,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Logic9, 0, {}, false, 0})
              == VhdlVhpiTypeError::None
          && types.publish(
                 result_type,
                 VhdlVhpiTypeDescriptor{
                     VhdlVhpiScalarKind::Logic9, 0, {}, false, 0})
              == VhdlVhpiTypeError::None,
      "VHPI foreign type publication failed");

  VhdlVhpiForeignSystem foreign{objects, types};
  const VhdlVhpiForeignValue argument{
      input_type, PackedLogic9::from_msb_string("10")};
  const VhdlVhpiForeignValue returned{
      result_type, PackedLogic9::from_msb_string("101")};
  std::uint64_t registration_identity{};
  std::vector<std::uint64_t> calls;
  VhdlVhpiForeignError active_unregister{};
  VhdlVhpiForeignRegistration callable;
  callable.kind = VhdlVhpiForeignKind::Subprogram;
  callable.name = "Foreign_Add";
  callable.parameters = {
      VhdlVhpiForeignParameter{
          "lhs", input_type, VhdlVhpiParameterMode::In}};
  callable.result_type = result_type;
  callable.user_data = 0x111U;
  callable.invoke = [&](const auto& invocation) {
    calls.push_back(invocation.call);
    const auto value = foreign.argument(invocation.arguments[0]);
    require_vhpi_foreign(
        value && value.value == argument
            && invocation.registration == registration_identity
            && invocation.registration_user_data == 0x111U,
        "VHPI foreign invocation lost argument or user data");
    active_unregister = foreign.unregister_foreign(
        invocation.registration);
    if (invocation.call_user_data == 1U) {
      const auto nested = foreign.invoke(
          invocation.registration, scope, {argument}, 2U);
      require_vhpi_foreign(
          nested && nested.value.identity != invocation.call,
          "VHPI same-registration re-entry failed");
    }
    require_vhpi_foreign(
        foreign.publish_result(invocation.call, returned)
            == VhdlVhpiForeignError::None,
        "VHPI typed result publication failed");
  };
  const auto registered = foreign.register_foreign(std::move(callable));
  require_vhpi_foreign(
      registered && registered.value.name == "Foreign_Add"
          && registered.value.parameters[0].name == "lhs",
      "VHPI foreign registration failed or did not copy its profile");
  registration_identity = registered.value.identity;

  const auto execution = foreign.invoke(
      registration_identity, scope, {argument}, 1U);
  require_vhpi_foreign(
      execution && execution.value.state == VhdlVhpiCallState::Completed
          && execution.value.result != 0U
          && foreign.result(execution.value.result).value == returned
          && foreign.call(execution.value.identity).value.user_data == 1U
          && foreign.registration(registration_identity).value.invocations
              == 2
          && calls.size() == 2 && calls[0] != calls[1]
          && active_unregister == VhdlVhpiForeignError::ActiveCall,
      "VHPI foreign call/result handles or re-entry state failed");

  std::vector<std::string_view> model_lifecycle;
  std::uint64_t model_identity{};
  VhdlVhpiForeignRegistration model;
  model.kind = VhdlVhpiForeignKind::Model;
  model.name = "Clock_Model";
  model.start = [&](const auto& descriptor) {
    require_vhpi_foreign(
        descriptor.identity == model_identity,
        "VHPI model start lost identity");
    model_lifecycle.push_back("start");
  };
  model.invoke = [&](const auto&) {
    model_lifecycle.push_back("invoke");
  };
  model.stop = [&](const auto&) {
    model_lifecycle.push_back("stop");
  };
  const auto registered_model = foreign.register_foreign(std::move(model));
  model_identity = registered_model.value.identity;
  require_vhpi_foreign(
      registered_model
          && foreign.invoke(model_identity, scope, {}).error
              == VhdlVhpiForeignError::InvalidState
          && foreign.start(model_identity) == VhdlVhpiForeignError::None
          && foreign.invoke(model_identity, scope, {})
          && foreign.stop(model_identity) == VhdlVhpiForeignError::None
          && foreign.registration(model_identity).value.state
              == VhdlVhpiForeignState::Stopped
          && model_lifecycle
              == std::vector<std::string_view>{"start", "invoke", "stop"},
      "VHPI foreign model lifecycle failed");

  VhdlVhpiForeignRegistration throwing;
  throwing.name = "Throwing";
  throwing.invoke = [](const auto&) {
    throw std::runtime_error("contained foreign exception");
  };
  const auto throwing_registration =
      foreign.register_foreign(std::move(throwing));
  const auto failed = foreign.invoke(
      throwing_registration.value.identity, scope, {});
  VhdlVhpiForeignRegistration missing;
  missing.name = "Missing_Result";
  missing.result_type = result_type;
  missing.invoke = [](const auto&) {};
  const auto missing_registration =
      foreign.register_foreign(std::move(missing));
  const auto missing_call = foreign.invoke(
      missing_registration.value.identity, scope, {});
  require_vhpi_foreign(
      failed.error == VhdlVhpiForeignError::CallbackFailed
          && foreign.call(failed.value.identity).value.state
              == VhdlVhpiCallState::Failed
          && missing_call.error == VhdlVhpiForeignError::ResultMissing
          && foreign.invoke(registration_identity, signal, {argument}).error
              == VhdlVhpiForeignError::InvalidScope
          && foreign.invoke(registration_identity, scope, {}).error
              == VhdlVhpiForeignError::InvalidArguments,
      "VHPI foreign failure containment or validation failed");

  const auto retained_argument = execution.value.arguments[0];
  const auto retained_result = execution.value.result;
  require_vhpi_foreign(
      foreign.release_call(calls[1]) == VhdlVhpiForeignError::None
          && foreign.release_call(execution.value.identity)
              == VhdlVhpiForeignError::None
          && foreign.argument(retained_argument).error
              == VhdlVhpiForeignError::InvalidHandle
          && foreign.result(retained_result).error
              == VhdlVhpiForeignError::InvalidHandle
          && foreign.unregister_foreign(registration_identity)
              == VhdlVhpiForeignError::None
          && foreign.unregister_foreign(registration_identity)
              == VhdlVhpiForeignError::Unregistered
          && foreign.invoke(registration_identity, scope, {argument}).error
              == VhdlVhpiForeignError::Unregistered,
      "VHPI foreign release/unregister lifecycle failed");

  VhdlVhpiObjectRegistry other_objects{1302};
  const auto other_root = foreign_object(
      other_objects, VhdlVhpiObjectKind::Root, 0, "other");
  const auto other_type = foreign_object(
      other_objects, VhdlVhpiObjectKind::Type, other_root.value, "logic_t");
  VhdlVhpiTypeSystem other_types{other_objects};
  require_vhpi_foreign(
      other_types.publish(
          other_type.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Logic9, 0, {}, false, 0})
          == VhdlVhpiTypeError::None,
      "VHPI foreign other type publication failed");
  VhdlVhpiForeignSystem other{other_objects, other_types};
  require_vhpi_foreign(
      other.registration(model_identity).error
              == VhdlVhpiForeignError::CrossSimulation
          && other.call(failed.value.identity).error
              == VhdlVhpiForeignError::CrossSimulation
          && other.argument(retained_argument).error
              == VhdlVhpiForeignError::CrossSimulation,
      "VHPI foreign identities crossed simulations");

  foreign.teardown();
  foreign.teardown();
  VhdlVhpiForeignRegistration after_close;
  after_close.name = "After_Close";
  after_close.invoke = [](const auto&) {};
  require_vhpi_foreign(
      !foreign.valid() || foreign.register_foreign(std::move(after_close)).error
              == VhdlVhpiForeignError::Closed,
      "VHPI foreign teardown was not idempotent and closed");
}

}  // namespace fsim::tests::runtime

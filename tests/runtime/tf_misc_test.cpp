// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_misc.hpp"
#include "fsim/runtime/tf_plugin.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

struct Observation {
  PLI_INT32 user_data{};
  PLI_INT32 reason{};
  PLI_INT32 parameter{};
};

std::vector<Observation> observations;

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL observe_misc(
    const PLI_INT32 user_data, const PLI_INT32 reason,
    const PLI_INT32 parameter) {
  observations.push_back({user_data, reason, parameter});
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL noop_routine(
    const PLI_INT32, const PLI_INT32) {
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL fail_misc(
    const PLI_INT32, const PLI_INT32, const PLI_INT32) {
  return 31;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL throw_misc(
    const PLI_INT32, const PLI_INT32, const PLI_INT32) {
  throw std::runtime_error{"contained misctf"};
}

fsim::runtime::TfMiscDispatcher* active_dispatcher{};
fsim::runtime::TfMiscError nested_error{fsim::runtime::TfMiscError::None};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL reenter_misc(
    const PLI_INT32, const PLI_INT32, const PLI_INT32) {
  const auto nested = active_dispatcher->dispatch(
      fsim::runtime::TfMiscReason::Synchronize);
  nested_error = nested.error;
  return 0;
}

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

fsim::runtime::TfRegistration registration(
    const PLI_INT32 user_data, const fsim_tf_misc_routine_v3 misctf) {
  return {
      .kind = fsim::runtime::TfRegistrationKind::Task,
      .user_data = user_data,
      .checktf = nullptr,
      .sizetf = nullptr,
      .calltf = noop_routine,
      .misctf = misctf,
      .name = "$misc",
  };
}

}  // namespace

int main() {
  using fsim::runtime::TfMiscError;
  using fsim::runtime::TfMiscReason;
  using fsim::runtime::make_tf_misc_dispatcher;

  const std::array registrations{
      registration(11, observe_misc), registration(22, nullptr),
      registration(33, observe_misc)};
  auto dispatcher = make_tf_misc_dispatcher(registrations);
  require(dispatcher && dispatcher.value->callback_count() == 2,
          "dispatcher retains only misctf registrations in source order");

  const std::array lifecycle{
      TfMiscReason::Save,
      TfMiscReason::Restart,
      TfMiscReason::Disable,
      TfMiscReason::ParameterValueChange,
      TfMiscReason::Synchronize,
      TfMiscReason::Finish,
      TfMiscReason::Reactivate,
      TfMiscReason::ReadOnlySynchronize,
      TfMiscReason::ParameterDelayChange,
      TfMiscReason::EndOfCompile,
      TfMiscReason::Scope,
      TfMiscReason::Interactive,
      TfMiscReason::Reset,
      TfMiscReason::EndOfReset,
      TfMiscReason::Force,
      TfMiscReason::Release,
      TfMiscReason::StartOfSave,
      TfMiscReason::StartOfRestart,
  };
  for (const auto reason : lifecycle) {
    const bool parameterized =
        reason == TfMiscReason::ParameterValueChange ||
        reason == TfMiscReason::ParameterDelayChange;
    const PLI_INT32 parameter = parameterized ? 3 : 0;
    observations.clear();
    const auto result = dispatcher.value->dispatch(reason, parameter);
    require(result && result.callbacks_invoked == 2 &&
                observations.size() == 2 &&
                observations[0].user_data == 11 &&
                observations[1].user_data == 33 &&
                observations[0].reason == static_cast<PLI_INT32>(reason) &&
                observations[1].reason == static_cast<PLI_INT32>(reason) &&
                observations[0].parameter == parameter &&
                observations[1].parameter == parameter,
            "each standard misctf reason dispatches in registration order");
  }

  const auto invalid_reason = dispatcher.value->dispatch(
      static_cast<TfMiscReason>(reason_checktf));
  require(!invalid_reason && invalid_reason.error == TfMiscError::InvalidReason,
          "ordinary TF callback reasons cannot enter misctf");
  require(dispatcher.value->dispatch(TfMiscReason::Synchronize, 1).error ==
              TfMiscError::InvalidParameter &&
              dispatcher.value
                      ->dispatch(TfMiscReason::ParameterValueChange, 0)
                      .error == TfMiscError::InvalidParameter,
          "misctf validates synchronization and object parameters");

  const std::array failure_registrations{
      registration(1, observe_misc), registration(2, observe_misc),
      registration(3, fail_misc)};
  auto failure_dispatcher =
      make_tf_misc_dispatcher(failure_registrations);
  observations.clear();
  const auto nonzero =
      failure_dispatcher.value->dispatch(TfMiscReason::Finish);
  require(nonzero && nonzero.last_callback_value == 31 &&
              nonzero.callbacks_invoked == 3 && observations.size() == 2,
          "misctf return values are ignored and do not truncate dispatch");

  const std::array exception_registrations{registration(1, throw_misc)};
  auto exception_dispatcher =
      make_tf_misc_dispatcher(exception_registrations);
  const auto exception =
      exception_dispatcher.value->dispatch(TfMiscReason::Reset);
  require(!exception && exception.error == TfMiscError::CallbackException &&
              exception.registration_index == 0 &&
              exception.callbacks_invoked == 0,
          "misctf exceptions do not cross the dispatcher boundary");

  const std::array reentrant_registrations{registration(1, reenter_misc)};
  auto reentrant_dispatcher =
      make_tf_misc_dispatcher(reentrant_registrations);
  active_dispatcher = reentrant_dispatcher.value.get();
  const auto outer =
      active_dispatcher->dispatch(TfMiscReason::ReadOnlySynchronize);
  require(outer && nested_error == TfMiscError::Reentrant,
          "misctf cannot recursively replace its active lifecycle dispatch");
  active_dispatcher = nullptr;

  const std::array no_callbacks{registration(1, nullptr)};
  auto empty_dispatcher = make_tf_misc_dispatcher(no_callbacks);
  const auto empty = empty_dispatcher.value->dispatch(TfMiscReason::Finish);
  require(empty && empty.callbacks_invoked == 0 &&
              empty_dispatcher.value->callback_count() == 0,
          "a table without misctf remains a valid empty dispatcher");

  auto loaded = fsim::runtime::load_tf_plugin(
      FSIM_TF_LINK_PROBE_PLUGIN_PATH);
  require(static_cast<bool>(loaded),
          "real C plug-in loads before misctf dispatch");
  auto loaded_dispatcher = loaded.value->make_misc_dispatcher();
  require(loaded_dispatcher && loaded_dispatcher.value->callback_count() == 2,
          "real C plug-in publishes both misctf callbacks");
  loaded.value.reset();
  const auto loaded_finish =
      loaded_dispatcher.value->dispatch(TfMiscReason::Finish);
  require(loaded_finish && loaded_finish.callbacks_invoked == 2,
          "misctf dispatcher retains its plug-in image");

  return 0;
}

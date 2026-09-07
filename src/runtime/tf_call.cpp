// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_call.hpp"

#include "fsim/runtime/tf_call_bridge.h"
#include "fsim/runtime/tf_containment.hpp"

#include <memory>
#include <new>
#include <array>
#include <mutex>
#include <utility>

namespace fsim::runtime {

namespace {

[[nodiscard]] TfInvokeResult invoke_failure(
    const TfCallError error, const PLI_INT32 status = 0) noexcept {
  return {.error = error,
          .callback_value = status,
          .function_result = std::nullopt,
          .argument_updates = {},
          .delay_requests = {},
          .control_effects = {},
          .synchronization_requests = {}};
}

[[nodiscard]] TfBindResult bind_failure(
    const TfCallError error, const PLI_INT32 status = 0) noexcept {
  return {.value = nullptr,
          .error = error,
          .callback_value = status,
          .control_effects = {}};
}

[[nodiscard]] std::uint32_t bridge_value_kind(
    const TfValueKind kind) noexcept {
  switch (kind) {
    case TfValueKind::Null:
      return FSIM_TF_VALUE_NONE;
    case TfValueKind::Integral:
      return FSIM_TF_VALUE_INTEGRAL;
    case TfValueKind::Real:
      return FSIM_TF_VALUE_REAL;
    case TfValueKind::String:
      return FSIM_TF_VALUE_STRING;
  }
  return FSIM_TF_VALUE_NONE;
}

}  // namespace

struct TfBoundCall::Impl {
  TfRegistration registration;
  std::vector<TfArgument> arguments;
  std::vector<fsim_tf_argument_bridge_v3> bridge_arguments;
  TfInstanceIdentity instance;
  fsim_tf_instance_bridge_v3 bridge_instance{};
  TfTimeProfile time_profile;
  TfContextProfile context_profile;
  PLI_BYTE8* work_area{};
  mutable std::recursive_mutex mutex;
  std::uint32_t result_width{};
  std::shared_ptr<const void> owner;
};

TfBoundCall::TfBoundCall(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

TfBoundCall::TfBoundCall(TfBoundCall&&) noexcept = default;
TfBoundCall& TfBoundCall::operator=(TfBoundCall&&) noexcept = default;
TfBoundCall::~TfBoundCall() = default;

std::string_view TfBoundCall::name() const noexcept {
  return impl_->registration.name;
}

TfRegistrationKind TfBoundCall::kind() const noexcept {
  return impl_->registration.kind;
}

std::uint32_t TfBoundCall::result_width() const noexcept {
  return impl_->result_width;
}

const TfInstanceIdentity& TfBoundCall::instance_identity() const noexcept {
  return impl_->instance;
}

const TfTimeProfile& TfBoundCall::time_profile() const noexcept {
  return impl_->time_profile;
}

const TfContextProfile& TfBoundCall::context_profile() const noexcept {
  return impl_->context_profile;
}

TfInvokeResult TfBoundCall::invoke() const noexcept {
  auto values = make_default_tf_values(impl_->arguments);
  if (!values) {
    return invoke_failure(values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(std::move(values.value), {}, std::nullopt);
}

TfInvokeResult TfBoundCall::invoke(const TfTimeState time) const noexcept {
  auto values = make_default_tf_values(impl_->arguments);
  if (!values) {
    return invoke_failure(values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(std::move(values.value), time, std::nullopt);
}

TfInvokeResult TfBoundCall::invoke(
    const std::span<const TfArgumentValue> values) const noexcept {
  auto validated_values = validate_and_copy_tf_values(impl_->arguments, values);
  if (!validated_values) {
    return invoke_failure(validated_values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(std::move(validated_values.value), {}, std::nullopt);
}

TfInvokeResult TfBoundCall::invoke(
    const std::span<const TfArgumentValue> values,
    const TfTimeState time) const noexcept {
  auto validated_values = validate_and_copy_tf_values(impl_->arguments, values);
  if (!validated_values) {
    return invoke_failure(validated_values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(std::move(validated_values.value), time, std::nullopt);
}

TfInvokeResult TfBoundCall::synchronize(
    const TfSynchronizationKind kind) const noexcept {
  if (!valid_tf_synchronization_kind(kind)) {
    return invoke_failure(TfCallError::InvalidSynchronization);
  }
  auto values = make_default_tf_values(impl_->arguments);
  if (!values) {
    return invoke_failure(values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(
      std::move(values.value), {},
      kind == TfSynchronizationKind::ReadWrite
          ? TfMiscReason::Synchronize
          : TfMiscReason::ReadOnlySynchronize);
}

TfInvokeResult TfBoundCall::synchronize(
    const TfSynchronizationKind kind, const TfTimeState time) const noexcept {
  if (!valid_tf_synchronization_kind(kind)) {
    return invoke_failure(TfCallError::InvalidSynchronization);
  }
  auto values = make_default_tf_values(impl_->arguments);
  if (!values) {
    return invoke_failure(values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(
      std::move(values.value), time,
      kind == TfSynchronizationKind::ReadWrite
          ? TfMiscReason::Synchronize
          : TfMiscReason::ReadOnlySynchronize);
}

TfInvokeResult TfBoundCall::synchronize(
    const TfSynchronizationKind kind,
    const std::span<const TfArgumentValue> values) const noexcept {
  if (!valid_tf_synchronization_kind(kind)) {
    return invoke_failure(TfCallError::InvalidSynchronization);
  }
  auto validated_values = validate_and_copy_tf_values(impl_->arguments, values);
  if (!validated_values) {
    return invoke_failure(validated_values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(
      std::move(validated_values.value), {},
      kind == TfSynchronizationKind::ReadWrite
          ? TfMiscReason::Synchronize
          : TfMiscReason::ReadOnlySynchronize);
}

TfInvokeResult TfBoundCall::synchronize(
    const TfSynchronizationKind kind,
    const std::span<const TfArgumentValue> values,
    const TfTimeState time) const noexcept {
  if (!valid_tf_synchronization_kind(kind)) {
    return invoke_failure(TfCallError::InvalidSynchronization);
  }
  auto validated_values = validate_and_copy_tf_values(impl_->arguments, values);
  if (!validated_values) {
    return invoke_failure(validated_values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(
      std::move(validated_values.value), time,
      kind == TfSynchronizationKind::ReadWrite
          ? TfMiscReason::Synchronize
          : TfMiscReason::ReadOnlySynchronize);
}

TfInvokeResult TfBoundCall::reactivate() const noexcept {
  auto values = make_default_tf_values(impl_->arguments);
  if (!values) {
    return invoke_failure(values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(
      std::move(values.value), {}, TfMiscReason::Reactivate);
}

TfInvokeResult TfBoundCall::reactivate(const TfTimeState time) const noexcept {
  auto values = make_default_tf_values(impl_->arguments);
  if (!values) {
    return invoke_failure(values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(
      std::move(values.value), time, TfMiscReason::Reactivate);
}

TfInvokeResult TfBoundCall::reactivate(
    const std::span<const TfArgumentValue> values) const noexcept {
  auto validated_values = validate_and_copy_tf_values(impl_->arguments, values);
  if (!validated_values) {
    return invoke_failure(validated_values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(
      std::move(validated_values.value), {}, TfMiscReason::Reactivate);
}

TfInvokeResult TfBoundCall::reactivate(
    const std::span<const TfArgumentValue> values,
    const TfTimeState time) const noexcept {
  auto validated_values = validate_and_copy_tf_values(impl_->arguments, values);
  if (!validated_values) {
    return invoke_failure(validated_values.error == TfValueError::Allocation
                              ? TfCallError::Allocation
                              : TfCallError::InvalidValues);
  }
  return invoke_owned(
      std::move(validated_values.value), time, TfMiscReason::Reactivate);
}

TfInvokeResult TfBoundCall::invoke_owned(
    std::vector<TfArgumentValue> values, const TfTimeState time,
    const std::optional<TfMiscReason> misc_reason) const noexcept {
  if (misc_reason.has_value() &&
      *misc_reason != TfMiscReason::Synchronize &&
      *misc_reason != TfMiscReason::ReadOnlySynchronize &&
      *misc_reason != TfMiscReason::Reactivate) {
    return invoke_failure(TfCallError::InvalidSynchronization);
  }
  if (misc_reason.has_value() && impl_->registration.misctf == nullptr) {
    return invoke_failure(TfCallError::MissingMiscCallback);
  }
  const std::lock_guard lock{impl_->mutex};
  TfInvokeResult result;
  TfControlCapture control_capture;
  std::vector<fsim_tf_value_bridge_v3> bridge_values;
  std::array<std::uint64_t, kMaxTfDelayRequests> delay_ticks{};
  std::array<std::uint32_t, kMaxTfSynchronizationRequests>
      synchronization_kinds{};
  const auto phase = !misc_reason.has_value()
      ? FSIM_TF_CALL_PHASE_CALL
      : *misc_reason == TfMiscReason::Synchronize
          ? FSIM_TF_CALL_PHASE_SYNCHRONIZE
          : *misc_reason == TfMiscReason::ReadOnlySynchronize
              ? FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE
              : FSIM_TF_CALL_PHASE_REACTIVATE;
  const bool read_only =
      phase == FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE;
  fsim_tf_time_bridge_v3 bridge_time{
      .unit_exponent = impl_->time_profile.unit_exponent,
      .precision_exponent = impl_->time_profile.precision_exponent,
      .reserved16 = 0,
      .reserved32 = 0,
      .tick_multiplier = impl_->time_profile.tick_multiplier,
      .scheduler_ticks = time.scheduler_ticks,
      .next_event_ticks = time.next_event_ticks,
      .has_next_event = static_cast<std::uint32_t>(time.has_next_event),
      .delay_count = 0,
      .delay_capacity = kMaxTfDelayRequests,
      .reserved = 0,
      .delay_ticks = delay_ticks.data(),
  };
  fsim_tf_call_context_v3 bridge{
      .phase = phase,
      .kind = FSIM_TF_CALL_RESULT_NONE,
      .width = misc_reason.has_value() ? 0U : impl_->result_width,
      .assigned = 0,
      .reserved = 0,
      .word_count = 0,
      .aval_words = nullptr,
      .bval_words = nullptr,
      .real = 0.0,
      .argument_count = static_cast<std::uint32_t>(
          impl_->bridge_arguments.size()),
      .arguments = impl_->bridge_arguments.data(),
      .values = nullptr,
      .instance = &impl_->bridge_instance,
      .time = &bridge_time,
      .user_data = impl_->registration.user_data,
      .context_reserved = 0,
      .module_instance_name = impl_->context_profile.module_instance_name.data(),
      .scope_name = impl_->context_profile.scope_name.data(),
      .routine_name = impl_->registration.name.data(),
      .work_area = impl_->work_area,
      .control_user_data = &control_capture,
      .control_emit = capture_tf_control_effect_v3,
      .control_failed = 0,
      .control_reserved = 0,
      .synchronization_count = 0,
      .synchronization_capacity = kMaxTfSynchronizationRequests,
      .synchronization_reserved = 0,
      .synchronization_kinds = synchronization_kinds.data(),
  };
  try {
    bridge_values.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
      auto& value = values[index];
      bridge_values.push_back(fsim_tf_value_bridge_v3{
          .kind = bridge_value_kind(value.kind),
          .width = impl_->arguments[index].width,
          .word_count = static_cast<std::uint32_t>(value.vector_words.size()),
          .writable = static_cast<std::uint32_t>(
              !read_only &&
              impl_->arguments[index].direction() ==
              TfArgumentDirection::InOut),
          .assigned = 0,
          .reserved = 0,
          .vector_words = value.vector_words.data(),
          .real = value.real,
          .string_size = static_cast<std::uint32_t>(value.string.size()),
          .string_value = value.kind == TfValueKind::String
              ? value.string.data()
              : nullptr,
      });
    }
    bridge.values = bridge_values.data();
    if (!misc_reason.has_value() &&
        impl_->registration.kind == TfRegistrationKind::Function) {
      bridge.kind = FSIM_TF_CALL_RESULT_INTEGRAL;
      bridge.word_count = (bridge.width + 31U) / 32U;
      result.function_result.emplace();
      result.function_result->kind = TfFunctionResultKind::Integral;
      result.function_result->width = bridge.width;
      result.function_result->aval_words.resize(bridge.word_count);
      result.function_result->bval_words.resize(bridge.word_count);
      bridge.aval_words = result.function_result->aval_words.data();
      bridge.bval_words = result.function_result->bval_words.data();
    } else if (!misc_reason.has_value() &&
               impl_->registration.kind ==
               TfRegistrationKind::RealFunction) {
      bridge.kind = FSIM_TF_CALL_RESULT_REAL;
      result.function_result.emplace();
      result.function_result->kind = TfFunctionResultKind::Real;
      result.function_result->width = bridge.width;
    }
  } catch (const std::bad_alloc&) {
    return invoke_failure(TfCallError::Allocation);
  } catch (...) {
    return invoke_failure(TfCallError::Allocation);
  }
  if (fsim_tf_call_context_enter_v3(&bridge) != 0) {
    return invoke_failure(TfCallError::ContextBusy);
  }

  PLI_INT32 status{};
  try {
    status = misc_reason.has_value()
        ? impl_->registration.misctf(
              impl_->registration.user_data,
              static_cast<PLI_INT32>(*misc_reason), 0)
        : impl_->registration.calltf(impl_->registration.user_data,
                                     reason_calltf);
  } catch (...) {
    fsim_tf_call_context_leave_v3(&bridge);
    return invoke_failure(TfCallError::CallbackException);
  }
  fsim_tf_call_context_leave_v3(&bridge);
  if (bridge.control_failed != 0 ||
      control_capture.error != TfControlError::None) {
    return invoke_failure(TfCallError::ControlLimit);
  }
  if (bridge.kind != FSIM_TF_CALL_RESULT_NONE && bridge.assigned == 0) {
    return invoke_failure(TfCallError::UnassignedResult);
  }

  if (bridge.kind == FSIM_TF_CALL_RESULT_INTEGRAL) {
    const auto remainder = bridge.width % 32U;
    if (remainder != 0) {
      const auto mask = (UINT32_C(1) << remainder) - UINT32_C(1);
      result.function_result->aval_words.back() &= mask;
      result.function_result->bval_words.back() &= mask;
    }
  } else if (bridge.kind == FSIM_TF_CALL_RESULT_REAL) {
    result.function_result->real = bridge.real;
  }
  try {
    result.argument_updates.reserve(bridge_values.size());
    result.delay_requests.reserve(bridge_time.delay_count);
    result.synchronization_requests.reserve(bridge.synchronization_count);
    for (std::size_t index = 0; index < bridge_values.size(); ++index) {
      auto& bridge_value = bridge_values[index];
      if (bridge_value.assigned == 0) {
        continue;
      }
      if (values[index].kind == TfValueKind::Real) {
        values[index].real = bridge_value.real;
      }
      result.argument_updates.push_back(TfArgumentUpdate{
          .parameter = static_cast<PLI_INT32>(index + 1U),
          .value = std::move(values[index]),
      });
    }
    for (std::uint32_t index = 0; index < bridge_time.delay_count; ++index) {
      result.delay_requests.push_back(
          {.scheduler_ticks = delay_ticks[index]});
    }
    for (std::uint32_t index = 0; index < bridge.synchronization_count;
         ++index) {
      result.synchronization_requests.push_back(TfSynchronizationRequest{
          .kind = synchronization_kinds[index] ==
                  FSIM_TF_SYNCHRONIZATION_READ_WRITE
              ? TfSynchronizationKind::ReadWrite
              : TfSynchronizationKind::ReadOnly,
          .instance = impl_->instance,
      });
    }
  } catch (const std::bad_alloc&) {
    return invoke_failure(TfCallError::Allocation);
  } catch (...) {
    return invoke_failure(TfCallError::Allocation);
  }
  result.callback_value = status;
  result.control_effects = std::move(control_capture.effects);
  impl_->work_area = bridge.work_area;
  return result;
}

TfBindResult bind_tf_call_with_owner(
    const TfRegistration& registration,
    const std::span<const TfArgument> arguments,
    std::shared_ptr<const void> owner,
    const TfInstanceIdentity instance,
    const TfTimeProfile time_profile,
    const TfContextProfile& context_profile) noexcept {
  const auto valid_callback = [](const auto callback) {
    return callback == nullptr ||
           validate_tf_callback_pointer(callback) == TfContainmentError::None;
  };
  if (registration.calltf == nullptr ||
      !valid_callback(registration.calltf) ||
      !valid_callback(registration.checktf) ||
      !valid_callback(registration.sizetf) ||
      !valid_callback(registration.misctf)) {
    return bind_failure(registration.calltf == nullptr
                            ? TfCallError::InvalidRegistration
                            : TfCallError::InvalidPointer);
  }
  if (validate_tf_instance_identity(instance) != TfInstanceError::None) {
    return bind_failure(TfCallError::InvalidInstance);
  }
  if (validate_tf_time_profile(time_profile) != TfTimeError::None) {
    return bind_failure(TfCallError::InvalidTime);
  }
  auto validated_context = validate_and_copy_tf_context(context_profile);
  if (!validated_context) {
    return bind_failure(validated_context.error == TfContextError::Allocation
                            ? TfCallError::Allocation
                            : TfCallError::InvalidContext);
  }
  auto validated_arguments = validate_and_copy_tf_arguments(arguments);
  if (!validated_arguments) {
    return bind_failure(TfCallError::InvalidArguments);
  }
  try {
    TfControlCapture lifecycle_capture;
    auto impl = std::make_unique<TfBoundCall::Impl>();
    impl->registration = registration;
    impl->arguments = std::move(validated_arguments.value);
    impl->instance = instance;
    impl->time_profile = time_profile;
    impl->context_profile = std::move(validated_context.value);
    impl->bridge_instance = {
        .abi_version = FSIM_TF_INSTANCE_ABI_VERSION,
        .struct_size = sizeof(fsim_tf_instance_bridge_v3),
        .design_id = instance.design_id,
        .hierarchy_id = instance.hierarchy_id,
        .generation = instance.generation,
        .reserved = 0,
    };
    impl->bridge_arguments.reserve(impl->arguments.size());
    for (auto& argument : impl->arguments) {
      impl->bridge_arguments.push_back(fsim_tf_argument_bridge_v3{
          .kind = static_cast<PLI_INT32>(argument.kind),
          .width = argument.width,
          .is_signed = static_cast<std::uint32_t>(argument.is_signed),
          .lhs_select = argument.lhs_select,
          .rhs_select = argument.rhs_select,
          .expression_size =
              static_cast<std::uint32_t>(argument.expression.size()),
          .expression = argument.expression.empty()
              ? nullptr
              : argument.expression.data(),
      });
    }
    auto lifecycle_context = fsim_tf_call_context_v3{
        .phase = FSIM_TF_CALL_PHASE_CHECK,
        .kind = FSIM_TF_CALL_RESULT_NONE,
        .width = 0,
        .assigned = 0,
        .reserved = 0,
        .word_count = 0,
        .aval_words = nullptr,
        .bval_words = nullptr,
        .real = 0.0,
        .argument_count =
            static_cast<std::uint32_t>(impl->bridge_arguments.size()),
        .arguments = impl->bridge_arguments.data(),
        .values = nullptr,
        .instance = &impl->bridge_instance,
        .time = nullptr,
        .user_data = impl->registration.user_data,
        .context_reserved = 0,
        .module_instance_name =
            impl->context_profile.module_instance_name.data(),
        .scope_name = impl->context_profile.scope_name.data(),
        .routine_name = impl->registration.name.data(),
        .work_area = nullptr,
        .control_user_data = &lifecycle_capture,
        .control_emit = capture_tf_control_effect_v3,
        .control_failed = 0,
        .control_reserved = 0,
        .synchronization_count = 0,
        .synchronization_capacity = 0,
        .synchronization_reserved = 0,
        .synchronization_kinds = nullptr,
    };
    if (registration.checktf != nullptr) {
      PLI_INT32 status{};
      if (fsim_tf_call_context_enter_v3(&lifecycle_context) != 0) {
        return bind_failure(TfCallError::ContextBusy);
      }
      try {
        status = registration.checktf(registration.user_data, reason_checktf);
      } catch (...) {
        fsim_tf_call_context_leave_v3(&lifecycle_context);
        return bind_failure(TfCallError::CallbackException);
      }
      fsim_tf_call_context_leave_v3(&lifecycle_context);
      if (lifecycle_context.control_failed != 0 ||
          lifecycle_capture.error != TfControlError::None) {
        return bind_failure(TfCallError::ControlLimit);
      }
      (void)status;
    }

    std::uint32_t width{};
    if (registration.kind == TfRegistrationKind::Function) {
      PLI_INT32 callback_width{};
      lifecycle_context.phase = FSIM_TF_CALL_PHASE_SIZE;
      if (fsim_tf_call_context_enter_v3(&lifecycle_context) != 0) {
        return bind_failure(TfCallError::ContextBusy);
      }
      try {
        callback_width =
            registration.sizetf(registration.user_data, reason_sizetf);
      } catch (...) {
        fsim_tf_call_context_leave_v3(&lifecycle_context);
        return bind_failure(TfCallError::CallbackException);
      }
      fsim_tf_call_context_leave_v3(&lifecycle_context);
      if (lifecycle_context.control_failed != 0 ||
          lifecycle_capture.error != TfControlError::None) {
        return bind_failure(TfCallError::ControlLimit);
      }
      if (callback_width <= 0 ||
          static_cast<std::uint32_t>(callback_width) > kMaxTfFunctionWidth) {
        return bind_failure(TfCallError::InvalidSize, callback_width);
      }
      width = static_cast<std::uint32_t>(callback_width);
    } else if (registration.kind == TfRegistrationKind::RealFunction) {
      width = 64;
    }

    impl->result_width = width;
    impl->work_area = lifecycle_context.work_area;
    impl->owner = std::move(owner);
    return {.value = std::unique_ptr<TfBoundCall>{
                new TfBoundCall{std::move(impl)}},
            .error = TfCallError::None,
            .callback_value = 0,
            .control_effects = std::move(lifecycle_capture.effects)};
  } catch (const std::bad_alloc&) {
    return bind_failure(TfCallError::Allocation);
  } catch (...) {
    return bind_failure(TfCallError::CallbackException);
  }
}

TfBindResult bind_tf_call(
    const TfRegistration& registration,
    const std::span<const TfArgument> arguments,
    const TfInstanceIdentity instance,
    const TfTimeProfile time_profile,
    const TfContextProfile& context_profile) noexcept {
  return bind_tf_call_with_owner(
      registration, arguments, {}, instance, time_profile, context_profile);
}

}  // namespace fsim::runtime

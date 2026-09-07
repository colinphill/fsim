// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_call.hpp"
#include "fsim/runtime/tf_plugin.hpp"
#include "fsim/runtime/veriuser.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

std::uint32_t check_calls{};
std::uint32_t size_calls{};
std::uint32_t call_calls{};
PLI_INT32 last_check_reason{};
PLI_INT32 last_size_reason{};
PLI_INT32 last_call_reason{};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL task_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_checktf) {
    ++check_calls;
    last_check_reason = reason;
    return 0;
  }
  ++call_calls;
  last_call_reason = reason;
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL integral_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  if (reason == reason_checktf) {
    ++check_calls;
    last_check_reason = reason;
    return 0;
  }
  if (reason == reason_sizetf) {
    ++size_calls;
    last_size_reason = reason;
    return 17;
  }
  ++call_calls;
  last_call_reason = reason;
  return tf_putp(0, user_data);
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL real_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  if (reason == reason_checktf) {
    return 0;
  }
  return tf_putrealp(0, static_cast<double>(user_data) / 4.0);
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL wide_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_sizetf) {
    return 65;
  }
  return tf_putlongp(0, static_cast<PLI_INT32>(UINT32_C(0x89abcdef)),
                     static_cast<PLI_INT32>(UINT32_C(0x76543210)));
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL check_failure(
    const PLI_INT32, const PLI_INT32) {
  return 19;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL zero_size(
    const PLI_INT32, const PLI_INT32 reason) {
  return reason == reason_sizetf ? 0 : 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL large_size(
    const PLI_INT32, const PLI_INT32 reason) {
  return reason == reason_sizetf
             ? static_cast<PLI_INT32>(fsim::runtime::kMaxTfFunctionWidth + 1U)
             : 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL throwing_callback(
    const PLI_INT32, const PLI_INT32) {
  throw std::runtime_error{"contained TF callback"};
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL call_failure(
    const PLI_INT32, const PLI_INT32 reason) {
  return reason == reason_calltf ? 29 : 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL unassigned_function(
    const PLI_INT32, const PLI_INT32 reason) {
  return reason == reason_sizetf ? 8 : 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL wrong_result_kind(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_sizetf) {
    return 8;
  }
  return tf_putrealp(0, 1.0);
}

const fsim::runtime::TfBoundCall* reentrant_bound{};
fsim::runtime::TfCallError nested_error{fsim::runtime::TfCallError::None};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL reentrant_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_sizetf) {
    return 8;
  }
  const auto nested = reentrant_bound->invoke();
  nested_error = nested.error;
  return tf_putp(0, 41);
}

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

fsim::runtime::TfRegistration registration(
    const fsim::runtime::TfRegistrationKind kind, std::string name,
    const fsim_tf_routine_v3 checktf, const fsim_tf_routine_v3 sizetf,
    const fsim_tf_routine_v3 calltf, const PLI_INT32 user_data = 0) {
  return {
      .kind = kind,
      .user_data = user_data,
      .checktf = checktf,
      .sizetf = sizetf,
      .calltf = calltf,
      .misctf = nullptr,
      .name = std::move(name),
  };
}

}  // namespace

int main() {
  using fsim::runtime::TfCallError;
  using fsim::runtime::TfFunctionResultKind;
  using fsim::runtime::TfRegistrationKind;
  using fsim::runtime::bind_tf_call;

  require(tf_putp(0, 1) != 0 && tf_putrealp(0, 1.0) != 0,
          "result writes require an active TF call context");

  const auto task = registration(TfRegistrationKind::Task, "$task",
                                 task_callback, nullptr, task_callback);
  auto bound_task = bind_tf_call(task);
  require(bound_task && bound_task.value->name() == "$task" &&
              bound_task.value->kind() == TfRegistrationKind::Task &&
              bound_task.value->result_width() == 0 && check_calls == 1 &&
              last_check_reason == reason_checktf,
          "task binding invokes checktf once and owns no result");
  const auto task_call = bound_task.value->invoke();
  require(task_call && !task_call.function_result.has_value() &&
              call_calls == 1 && last_call_reason == reason_calltf,
          "task calltf completes without a function result");

  const auto integral = registration(
      TfRegistrationKind::Function, "$integral", integral_callback,
      integral_callback, integral_callback, 37);
  auto bound_integral = bind_tf_call(integral);
  require(bound_integral && bound_integral.value->result_width() == 17 &&
              size_calls == 1 && last_size_reason == reason_sizetf,
          "integral binding invokes sizetf once");
  const auto first_integral = bound_integral.value->invoke();
  require(first_integral && first_integral.function_result.has_value() &&
              first_integral.function_result->kind ==
                  TfFunctionResultKind::Integral &&
              first_integral.function_result->width == 17 &&
              first_integral.function_result->aval_words.size() == 1 &&
              first_integral.function_result->aval_words[0] == 37 &&
              first_integral.function_result->bval_words[0] == 0,
          "integral calltf writes its simulator-owned result");
  const auto second_integral = bound_integral.value->invoke();
  require(second_integral && second_integral.function_result.has_value() &&
              second_integral.function_result->aval_words[0] == 37 &&
              first_integral.function_result->aval_words[0] == 37,
          "each invocation receives independent result storage");

  auto bound_wide = bind_tf_call(registration(
      TfRegistrationKind::Function, "$wide", nullptr, wide_callback,
      wide_callback));
  const auto wide_result = bound_wide.value->invoke();
  require(wide_result && wide_result.function_result.has_value() &&
              wide_result.function_result->width == 65 &&
              wide_result.function_result->aval_words.size() == 3 &&
              wide_result.function_result->bval_words.size() == 3 &&
              wide_result.function_result->aval_words[0] ==
                  UINT32_C(0x89abcdef) &&
              wide_result.function_result->aval_words[1] ==
                  UINT32_C(0x76543210) &&
              wide_result.function_result->aval_words[2] == 0,
          "wide function results own width-qualified four-state word storage");

  const auto real = registration(TfRegistrationKind::RealFunction, "$real",
                                 real_callback, nullptr, real_callback, 13);
  auto bound_real = bind_tf_call(real);
  const auto real_result = bound_real.value->invoke();
  require(bound_real && bound_real.value->result_width() == 64 && real_result &&
              real_result.function_result.has_value() &&
              real_result.function_result->kind == TfFunctionResultKind::Real &&
              real_result.function_result->real == 3.25,
          "real calltf writes its simulator-owned result");

  const auto nonzero_check = bind_tf_call(registration(
      TfRegistrationKind::Task, "$nonzero_check", check_failure, nullptr,
      task_callback));
  require(static_cast<bool>(nonzero_check),
          "checktf return value is not interpreted as a failure status");
  const auto thrown_check = bind_tf_call(registration(
      TfRegistrationKind::Task, "$thrown_check", throwing_callback, nullptr,
      task_callback));
  require(!thrown_check &&
              thrown_check.error == TfCallError::CallbackException,
          "checktf exceptions do not cross the host boundary");
  const auto invalid_zero = bind_tf_call(registration(
      TfRegistrationKind::Function, "$zero_size", nullptr, zero_size,
      integral_callback));
  require(!invalid_zero && invalid_zero.error == TfCallError::InvalidSize &&
              invalid_zero.callback_value == 0,
          "sizetf rejects zero width");
  const auto invalid_large = bind_tf_call(registration(
      TfRegistrationKind::Function, "$large_size", nullptr, large_size,
      integral_callback));
  require(!invalid_large && invalid_large.error == TfCallError::InvalidSize,
          "sizetf bounds function width");
  const auto thrown_size = bind_tf_call(registration(
      TfRegistrationKind::Function, "$thrown_size", nullptr,
      throwing_callback, integral_callback));
  require(!thrown_size &&
              thrown_size.error == TfCallError::CallbackException,
          "sizetf exceptions do not cross the host boundary");

  auto nonzero_call = bind_tf_call(registration(
      TfRegistrationKind::Task, "$nonzero_call", nullptr, nullptr,
      call_failure));
  const auto nonzero_call_result = nonzero_call.value->invoke();
  require(nonzero_call_result && nonzero_call_result.callback_value == 29,
          "calltf return value is preserved but does not signal failure");
  auto thrown_call = bind_tf_call(registration(
      TfRegistrationKind::Task, "$thrown_call", nullptr, nullptr,
      throwing_callback));
  require(thrown_call.value->invoke().error == TfCallError::CallbackException,
          "calltf exceptions do not cross the host boundary");
  auto unassigned = bind_tf_call(registration(
      TfRegistrationKind::Function, "$unassigned", nullptr,
      unassigned_function, unassigned_function));
  require(unassigned.value->invoke().error == TfCallError::UnassignedResult,
          "function calltf must assign its result");
  auto wrong_kind = bind_tf_call(registration(
      TfRegistrationKind::Function, "$wrong_kind", nullptr,
      wrong_result_kind, wrong_result_kind));
  require(wrong_kind.value->invoke().error == TfCallError::UnassignedResult,
          "a function cannot write a result of the wrong kind");

  auto reentrant = bind_tf_call(registration(
      TfRegistrationKind::Function, "$reentrant", nullptr,
      reentrant_callback, reentrant_callback));
  reentrant_bound = reentrant.value.get();
  const auto outer = reentrant.value->invoke();
  require(outer && outer.function_result->aval_words[0] == 41 &&
              nested_error == TfCallError::ContextBusy,
          "nested calltf cannot replace the active result context");
  reentrant_bound = nullptr;

  auto loaded = fsim::runtime::load_tf_plugin(
      FSIM_TF_LINK_PROBE_PLUGIN_PATH);
  require(loaded && loaded.value->registrations().size() == 2,
          "real C plug-in publishes task and function registrations");
  const auto invalid_index = loaded.value->bind(2);
  require(!invalid_index &&
              invalid_index.error == TfCallError::InvalidRegistration,
          "loaded plug-in rejects an invalid registration index");
  auto loaded_function = loaded.value->bind(1);
  require(loaded_function && loaded_function.value->result_width() == 17,
          "real C plug-in checktf and sizetf bind successfully");
  loaded.value.reset();
  const auto loaded_result = loaded_function.value->invoke();
  require(loaded_result && loaded_result.function_result.has_value() &&
              loaded_result.function_result->aval_words[0] == 23,
          "bound callbacks retain their plug-in image through calltf");

  return 0;
}

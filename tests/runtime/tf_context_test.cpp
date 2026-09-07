// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_call.hpp"
#include "fsim/runtime/tf_context.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::array<PLI_BYTE8, 1> check_area{};
std::array<PLI_BYTE8, 1> size_area{};
std::array<PLI_BYTE8, 1> call_area{};
std::array<PLI_BYTE8, 1> rejected_area{};
PLI_BYTE8* expected_instance{};
bool callback_ok{true};
std::array<std::uint32_t, 4> reason_calls{};
std::uint32_t call_count{};

bool text_matches(const PLI_BYTE8* const value,
                  const std::string_view expected) {
  return value != nullptr && std::string_view{value} == expected;
}

bool names_match(PLI_BYTE8* const instance) {
  return text_matches(tf_mipname(), "top.u_codec") &&
         text_matches(tf_spname(), "top.u_codec.decode.block") &&
         text_matches(tf_getroutine(), "$context") &&
         text_matches(tf_imipname(instance), "top.u_codec") &&
         text_matches(tf_ispname(instance), "top.u_codec.decode.block") &&
         text_matches(tf_igetroutine(instance), "$context");
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL context_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  if (reason >= 0 && static_cast<std::size_t>(reason) < reason_calls.size()) {
    ++reason_calls[static_cast<std::size_t>(reason)];
  }
  auto* const instance = tf_getinstance();
  expected_instance = expected_instance == nullptr ? instance : expected_instance;
  callback_ok = callback_ok && user_data == 0x157 &&
                instance == expected_instance && names_match(instance) &&
                tf_imipname(reinterpret_cast<PLI_BYTE8*>(1)) == nullptr &&
                tf_ispname(reinterpret_cast<PLI_BYTE8*>(1)) == nullptr &&
                tf_igetroutine(reinterpret_cast<PLI_BYTE8*>(1)) == nullptr;
  if (reason == reason_checktf) {
    callback_ok = callback_ok && tf_getworkarea() == nullptr &&
                  tf_setworkarea(check_area.data()) == 0 &&
                  tf_getworkarea() == check_area.data();
    return 0;
  }
  if (reason == reason_sizetf) {
    callback_ok = callback_ok && tf_getworkarea() == check_area.data() &&
                  tf_isetworkarea(size_area.data(), instance) == 0 &&
                  tf_igetworkarea(instance) == size_area.data();
    return 8;
  }
  callback_ok = callback_ok && reason == reason_calltf;
  if (call_count == 0) {
    callback_ok = callback_ok && tf_getworkarea() == size_area.data() &&
                  tf_setworkarea(call_area.data()) == 0;
    auto* const mutable_name = tf_mipname();
    callback_ok = callback_ok && mutable_name != nullptr;
    if (mutable_name != nullptr) {
      mutable_name[0] = 'X';
    }
  } else {
    callback_ok = callback_ok && tf_getworkarea() == call_area.data() &&
                  tf_isetworkarea(nullptr, instance) == 0;
  }
  callback_ok = callback_ok &&
                tf_isetworkarea(rejected_area.data(),
                                reinterpret_cast<PLI_BYTE8*>(1)) != 0 &&
                tf_putp(0, static_cast<PLI_INT32>(++call_count)) == 0;
  return 0;
}

std::uint32_t rollback_calls{};
bool rollback_ok{true};
std::array<std::array<PLI_BYTE8, 1>, 2> instance_areas{};
std::array<std::uint32_t, 2> instance_calls{};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL rollback_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason != reason_calltf) return 0;
  if (rollback_calls == 0) {
    rollback_ok = rollback_ok && tf_getworkarea() == nullptr &&
                  tf_setworkarea(check_area.data()) == 0;
  } else if (rollback_calls == 1) {
    rollback_ok = rollback_ok && tf_getworkarea() == check_area.data() &&
                  tf_setworkarea(rejected_area.data()) == 0;
    ++rollback_calls;
    throw std::runtime_error{"contained work-area callback"};
  } else {
    rollback_ok = rollback_ok && tf_getworkarea() == check_area.data();
  }
  ++rollback_calls;
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL isolation_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  if (reason != reason_calltf || user_data < 1 || user_data > 2) return 1;
  const auto index = static_cast<std::size_t>(user_data - 1);
  callback_ok = callback_ok &&
                tf_getworkarea() ==
                    (instance_calls[index] == 0 ? nullptr
                                                : instance_areas[index].data()) &&
                tf_setworkarea(instance_areas[index].data()) == 0;
  ++instance_calls[index];
  return 0;
}

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error{message};
}

fsim::runtime::TfRegistration task_registration(
    std::string name, const fsim_tf_routine_v3 callback,
    const PLI_INT32 user_data = 0) {
  return {.kind = fsim::runtime::TfRegistrationKind::Task,
          .user_data = user_data,
          .checktf = nullptr,
          .sizetf = nullptr,
          .calltf = callback,
          .misctf = nullptr,
          .name = std::move(name)};
}

}  // namespace

int main() {
  using fsim::runtime::TfCallError;
  using fsim::runtime::TfContextError;
  using fsim::runtime::TfContextProfile;
  using fsim::runtime::TfInstanceIdentity;
  using fsim::runtime::TfRegistration;
  using fsim::runtime::TfRegistrationKind;
  using fsim::runtime::bind_tf_call;
  using fsim::runtime::validate_and_copy_tf_context;

  require(tf_mipname() == nullptr && tf_spname() == nullptr &&
              tf_getroutine() == nullptr && tf_getworkarea() == nullptr &&
              tf_setworkarea(call_area.data()) != 0,
          "TF context services are neutral outside a callback");
  const TfContextProfile original{.module_instance_name = "top.u_codec",
                                  .scope_name =
                                      "top.u_codec.decode.block"};
  auto source = original;
  TfRegistration registration{
      .kind = TfRegistrationKind::Function,
      .user_data = 0x157,
      .checktf = context_callback,
      .sizetf = context_callback,
      .calltf = context_callback,
      .misctf = nullptr,
      .name = "$context"};
  auto bound = bind_tf_call(registration, {},
                            TfInstanceIdentity{.design_id = 1,
                                               .hierarchy_id = 2,
                                               .generation = 3},
                            {}, source);
  source.module_instance_name = "mutated";
  source.scope_name = "mutated";
  registration.name = "$mutated";
  registration.user_data = 0;
  require(static_cast<bool>(bound) && bound.value->context_profile() == original,
          "TF binding owns immutable scope names");
  const auto first = bound.value->invoke();
  const auto second = bound.value->invoke();
  require(first && second && callback_ok && call_count == 2 &&
              reason_calls[reason_checktf] == 1 &&
              reason_calls[reason_sizetf] == 1 &&
              reason_calls[reason_calltf] == 2 &&
              first.function_result->aval_words[0] == 1 &&
              second.function_result->aval_words[0] == 2,
          "scope, routine, user data, and work area survive their lifetimes");

  auto rollback = bind_tf_call(
      task_registration("$rollback", rollback_callback), {},
      TfInstanceIdentity{.design_id = 1, .hierarchy_id = 4, .generation = 3},
      {}, {.module_instance_name = "top.u_other",
           .scope_name = "top.u_other"});
  require(static_cast<bool>(rollback) && rollback.value->invoke(),
          "TF work-area rollback fixture initialized");
  const auto rejected = rollback.value->invoke();
  const auto retained = rollback.value->invoke();
  require(rejected.error == TfCallError::CallbackException && retained &&
              rollback_ok && rollback_calls == 3,
          "failed callbacks cannot replace an instance work area");

  auto first_instance = bind_tf_call(
      task_registration("$instance_a", isolation_callback, 1), {},
      TfInstanceIdentity{.design_id = 1, .hierarchy_id = 5, .generation = 3});
  auto second_instance = bind_tf_call(
      task_registration("$instance_b", isolation_callback, 2), {},
      TfInstanceIdentity{.design_id = 1, .hierarchy_id = 6, .generation = 3});
  require(static_cast<bool>(first_instance) &&
              static_cast<bool>(second_instance) &&
              first_instance.value->invoke() && second_instance.value->invoke() &&
              first_instance.value->invoke() && second_instance.value->invoke() &&
              instance_calls[0] == 2 && instance_calls[1] == 2 && callback_ok,
          "TF work areas remain isolated between bound instances");

  require(validate_and_copy_tf_context(original).error == TfContextError::None &&
              validate_and_copy_tf_context(
                  {.module_instance_name = "", .scope_name = "top"})
                      .error == TfContextError::EmptyModuleInstance &&
              validate_and_copy_tf_context(
                  {.module_instance_name = "top", .scope_name = ""})
                      .error == TfContextError::EmptyScope &&
              validate_and_copy_tf_context(
                  {.module_instance_name = "top", .scope_name = "other"})
                      .error == TfContextError::ScopeOwnership &&
              validate_and_copy_tf_context(
                  {.module_instance_name = "top", .scope_name = "top\nblock"})
                      .error == TfContextError::ControlCharacter &&
              validate_and_copy_tf_context(
                  {.module_instance_name =
                       std::string(fsim::runtime::kMaxTfContextNameSize + 1U,
                                   'a'),
                   .scope_name = "top"})
                      .error == TfContextError::NameSize,
          "TF scope validation is exact and bounded");
  const auto invalid = bind_tf_call(
      task_registration("$invalid", rollback_callback), {}, {}, {},
      {.module_instance_name = "top", .scope_name = "not_top"});
  require(!invalid && invalid.error == TfCallError::InvalidContext &&
              rollback_calls == 3,
          "invalid TF scope ownership prevents callback entry");
  return 0;
}

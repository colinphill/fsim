// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_call.hpp"
#include "fsim/runtime/tf_instance.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace {

std::array<PLI_BYTE8*, 3> instance_tokens{};
std::array<std::uint32_t, 3> check_calls{};
std::array<std::uint32_t, 3> call_calls{};
bool callback_ok{true};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL instance_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  const auto index = static_cast<std::size_t>(user_data);
  auto* const instance = tf_getinstance();
  callback_ok = callback_ok && index > 0 && index < instance_tokens.size() &&
                instance != nullptr;
  if (reason == reason_checktf) {
    instance_tokens[index] = instance;
    ++check_calls[index];
    return 0;
  }
  if (reason != reason_calltf) {
    callback_ok = false;
    return 0;
  }
  ++call_calls[index];
  s_tfexprinfo info{};
  s_tfnodeinfo node{};
  const auto expected = static_cast<PLI_INT32>(index * 10U + 1U);
  auto* const other = instance_tokens[index == 1 ? 2 : 1];
  callback_ok = callback_ok && instance == instance_tokens[index] &&
                other != nullptr && other != instance &&
                tf_inump(instance) == 2 &&
                tf_itypep(1, instance) == tf_readonly &&
                tf_isizep(2, instance) == 16 &&
                tf_igetp(1, instance) == expected &&
                std::string_view{tf_istrgetp(1, 'h', instance)} ==
                    (index == 1 ? "0b" : "15") &&
                tf_iexprinfo(1, &info, instance) == &info &&
                info.expr_value_p[0].avalbits == expected &&
                tf_inodeinfo(2, &node, instance) == &node &&
                node.node_type == tf_reg_node && node.node_vec_size == 16 &&
                std::string_view{node.node_symbol, 6} == "output" &&
                node.node_value.vecval_p != nullptr &&
                tf_ievaluatep(1, instance) == 0 &&
                tf_inump(other) == 0 && tf_igetp(1, other) == 0 &&
                tf_iexprinfo(1, nullptr, other) == nullptr &&
                tf_inodeinfo(1, nullptr, other) == nullptr &&
                tf_iputp(2, 99, other) != 0 &&
                tf_iputp(2, static_cast<PLI_INT32>(100U + index), instance) ==
                    0;
  return 0;
}

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

}  // namespace

int main() {
  using fsim::runtime::TfArgument;
  using fsim::runtime::TfArgumentKind;
  using fsim::runtime::TfArgumentValue;
  using fsim::runtime::TfCallError;
  using fsim::runtime::TfInstanceError;
  using fsim::runtime::TfInstanceIdentity;
  using fsim::runtime::TfRegistration;
  using fsim::runtime::TfRegistrationKind;
  using fsim::runtime::TfValueKind;
  using fsim::runtime::bind_tf_call;
  using fsim::runtime::validate_tf_instance_identity;

  require(tf_getinstance() == nullptr && tf_inump(nullptr) == 0 &&
              tf_igetp(1, nullptr) == 0 && tf_iputp(1, 1, nullptr) != 0,
          "instance services are neutral outside a TF callback");
  require(validate_tf_instance_identity({}) == TfInstanceError::None &&
              validate_tf_instance_identity(
                  {.design_id = 0, .hierarchy_id = 1, .generation = 1}) ==
                  TfInstanceError::DesignIdentity &&
              validate_tf_instance_identity(
                  {.design_id = 1, .hierarchy_id = 0, .generation = 1}) ==
                  TfInstanceError::HierarchyIdentity &&
              validate_tf_instance_identity(
                  {.design_id = 1, .hierarchy_id = 1, .generation = 0}) ==
                  TfInstanceError::Generation,
          "TF instance identities require all generation-qualified fields");

  const std::array arguments{
      TfArgument{.kind = TfArgumentKind::ReadOnly,
                 .width = 8,
                 .is_signed = false,
                 .lhs_select = -1,
                 .rhs_select = -1,
                 .expression = "input"},
      TfArgument{.kind = TfArgumentKind::ReadWrite,
                 .width = 16,
                 .is_signed = false,
                 .lhs_select = -1,
                 .rhs_select = -1,
                 .expression = "output"},
  };
  const TfRegistration first_registration{
      .kind = TfRegistrationKind::Task,
      .user_data = 1,
      .checktf = instance_callback,
      .sizetf = nullptr,
      .calltf = instance_callback,
      .misctf = nullptr,
      .name = "$first",
  };
  auto second_registration = first_registration;
  second_registration.user_data = 2;
  second_registration.name = "$second";
  const TfInstanceIdentity first_identity{
      .design_id = 17, .hierarchy_id = 23, .generation = 1};
  const TfInstanceIdentity second_identity{
      .design_id = 17, .hierarchy_id = 23, .generation = 2};
  auto first = bind_tf_call(first_registration, arguments, first_identity);
  auto second = bind_tf_call(second_registration, arguments, second_identity);
  require(first && second &&
              first.value->instance_identity() == first_identity &&
              second.value->instance_identity() == second_identity &&
              check_calls[1] == 1 && check_calls[2] == 1 &&
              instance_tokens[1] != instance_tokens[2],
          "binding publishes distinct generation-qualified instance tokens");

  const auto make_values = [](const PLI_INT32 input) {
    return std::array{
        TfArgumentValue{.kind = TfValueKind::Integral,
                        .vector_words = {{input, 0}},
                        .real = 0.0,
                        .string = {}},
        TfArgumentValue{.kind = TfValueKind::Integral,
                        .vector_words = {{0, 0}},
                        .real = 0.0,
                        .string = {}},
    };
  };
  const auto first_result = first.value->invoke(make_values(11));
  const auto second_result = second.value->invoke(make_values(21));
  require(first_result && second_result && callback_ok &&
              call_calls[1] == 1 && call_calls[2] == 1 &&
              first_result.argument_updates.size() == 1 &&
              first_result.argument_updates[0].parameter == 2 &&
              first_result.argument_updates[0].value.vector_words[0].avalbits ==
                  101 &&
              second_result.argument_updates.size() == 1 &&
              second_result.argument_updates[0]
                      .value.vector_words[0].avalbits == 102,
          "explicit access resolves only the active call instance");

  const auto callbacks_before = check_calls;
  const auto invalid = bind_tf_call(
      first_registration, arguments,
      {.design_id = 17, .hierarchy_id = 23, .generation = 0});
  require(!invalid && invalid.error == TfCallError::InvalidInstance &&
              check_calls == callbacks_before,
          "invalid instance identity prevents all plug-in callbacks");
  return 0;
}

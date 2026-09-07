// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_argument.hpp"
#include "fsim/runtime/tf_call.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool inspection_ok{true};
std::array<std::uint32_t, 4> phase_calls{};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL inspect_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  if (reason >= reason_checktf && reason <= reason_calltf) {
    ++phase_calls[static_cast<std::size_t>(reason)];
  } else {
    inspection_ok = false;
    return 0;
  }
  s_tfexprinfo info{};
  const auto* const returned = tf_exprinfo(2, &info);
  inspection_ok = inspection_ok && tf_nump() == 4 &&
                  tf_typep(1) == tf_readonly && tf_sizep(1) == 8 &&
                  tf_typep(2) == tf_rwpartselect && tf_sizep(2) == 4 &&
                  tf_typep(3) == tf_readonlyreal && tf_sizep(3) == 64 &&
                  tf_typep(4) == tf_nullparam && tf_sizep(4) == 0 &&
                  tf_typep(0) == tf_nullparam && tf_sizep(5) == 0 &&
                  tf_exprinfo(0, nullptr) == nullptr && returned == &info &&
                  info.expr_type == tf_rwpartselect && info.expr_vec_size == 4 &&
                  info.expr_ngroups == 1 && info.expr_sign == 1 &&
                  info.expr_lhs_select == 7 && info.expr_rhs_select == 4 &&
                  std::string_view{info.expr_string, 8} == "bus[7:4]";
  if (reason == reason_sizetf) {
    return 12;
  }
  if (reason == reason_calltf) {
    return tf_putp(0, user_data);
  }
  return 73;
}

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

}  // namespace

int main() {
  using fsim::runtime::TfArgument;
  using fsim::runtime::TfArgumentDirection;
  using fsim::runtime::TfArgumentError;
  using fsim::runtime::TfArgumentKind;
  using fsim::runtime::TfCallError;
  using fsim::runtime::TfRegistration;
  using fsim::runtime::TfRegistrationKind;
  using fsim::runtime::bind_tf_call;
  using fsim::runtime::validate_and_copy_tf_arguments;

  const std::array arguments{
      TfArgument{.kind = TfArgumentKind::ReadOnly,
                 .width = 8,
                 .is_signed = false,
                 .lhs_select = -1,
                 .rhs_select = -1,
                 .expression = "input_signal"},
      TfArgument{.kind = TfArgumentKind::ReadWritePartSelect,
                 .width = 4,
                 .is_signed = true,
                 .lhs_select = 7,
                 .rhs_select = 4,
                 .expression = "bus[7:4]"},
      TfArgument{.kind = TfArgumentKind::ReadOnlyReal,
                 .width = 64,
                 .is_signed = false,
                 .lhs_select = -1,
                 .rhs_select = -1,
                 .expression = "gain"},
      TfArgument{},
  };
  const auto valid = validate_and_copy_tf_arguments(arguments);
  require(valid && valid.value.size() == 4 &&
              valid.value[0].direction() == TfArgumentDirection::Input &&
              valid.value[1].direction() == TfArgumentDirection::InOut &&
              valid.value[3].direction() == TfArgumentDirection::None &&
              valid.value[1].expression == "bus[7:4]",
          "valid TF argument metadata is copied with exact direction");

  require(tf_nump() == 0 && tf_typep(1) == tf_nullparam &&
              tf_sizep(1) == 0 && tf_exprinfo(1, nullptr) == nullptr,
          "argument inspection is neutral outside a TF callback");

  const TfRegistration registration{
      .kind = TfRegistrationKind::Function,
      .user_data = 51,
      .checktf = inspect_callback,
      .sizetf = inspect_callback,
      .calltf = inspect_callback,
      .misctf = nullptr,
      .name = "$inspect",
  };
  auto bound = bind_tf_call(registration, arguments);
  require(bound && bound.value->result_width() == 12 && inspection_ok &&
              phase_calls[reason_checktf] == 1 &&
              phase_calls[reason_sizetf] == 1 &&
              phase_calls[reason_calltf] == 0,
          "checktf and sizetf inspect one immutable argument inventory");
  const auto invoked = bound.value->invoke();
  require(invoked && inspection_ok &&
              phase_calls[reason_calltf] == 1 &&
              invoked.function_result->aval_words[0] == 51 &&
              invoked.callback_value == 0,
          "calltf sees the same inventory and owns its result");

  const auto expect = [&](const std::size_t index, const auto mutate,
                          const TfArgumentError expected,
                          const char* const message) {
    auto invalid = arguments;
    mutate(invalid[index]);
    const auto result = validate_and_copy_tf_arguments(invalid);
    require(!result && result.value.empty() && result.error == expected &&
                result.argument_index == index,
            message);
  };
  expect(0,
         [](auto& value) {
           value.kind = static_cast<TfArgumentKind>(999);
         },
         TfArgumentError::Kind, "unknown TF argument kind is rejected");
  expect(3, [](auto& value) { value.width = 1; }, TfArgumentError::Width,
         "null TF argument has no width or metadata");
  expect(0, [](auto& value) { value.width = 0; }, TfArgumentError::Width,
         "non-null TF argument requires a width");
  expect(0,
         [](auto& value) {
           value.width = fsim::runtime::kMaxTfArgumentWidth + 1U;
         },
         TfArgumentError::Width, "TF argument width is bounded");
  expect(2, [](auto& value) { value.width = 32; }, TfArgumentError::Width,
         "real TF argument has exact width 64");
  expect(0,
         [](auto& value) {
           value.kind = TfArgumentKind::String;
           value.width = 7;
         },
         TfArgumentError::Width, "string TF argument width is byte aligned");
  expect(1, [](auto& value) { value.lhs_select = -1; },
         TfArgumentError::Selection,
         "selected TF argument requires nonnegative source indexes");
  expect(0, [](auto& value) { value.lhs_select = 0; },
         TfArgumentError::Selection,
         "whole TF argument rejects selection indexes");
  expect(0, [](auto& value) { value.expression.clear(); },
         TfArgumentError::Expression,
         "non-null TF argument requires expression identity");
  expect(0,
         [](auto& value) {
           value.expression.assign(
               fsim::runtime::kMaxTfExpressionTextSize + 1U, 'x');
         },
         TfArgumentError::Expression, "TF expression identity is bounded");
  expect(0, [](auto& value) { value.expression = "bad\nexpression"; },
         TfArgumentError::Expression,
         "TF expression identity rejects control bytes");

  std::vector<TfArgument> too_many(fsim::runtime::kMaxTfArguments + 1U);
  const auto count_failure = validate_and_copy_tf_arguments(too_many);
  require(!count_failure && count_failure.error == TfArgumentError::Count &&
              count_failure.value.empty(),
          "TF argument inventory is bounded before entry inspection");

  auto bad_arguments = arguments;
  bad_arguments[2].width = 32;
  const auto calls_before = phase_calls;
  const auto rejected_bind = bind_tf_call(registration, bad_arguments);
  require(!rejected_bind &&
              rejected_bind.error == TfCallError::InvalidArguments &&
              phase_calls == calls_before,
          "invalid argument metadata prevents all plug-in callbacks");

  require(tf_inump(nullptr) == 0 && tf_itypep(1, nullptr) == 0 &&
              tf_isizep(1, nullptr) == 0 &&
              tf_iexprinfo(1, nullptr, nullptr) == nullptr,
          "explicit-instance inspection remains deferred and neutral");
  return 0;
}

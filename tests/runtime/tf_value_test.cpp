// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_call.hpp"
#include "fsim/runtime/tf_value.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool callback_ok{true};
std::uint32_t call_count{};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL value_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_sizetf) {
    return 33;
  }
  if (reason != reason_calltf) {
    return 0;
  }
  ++call_count;
  PLI_INT32 high{};
  s_tfexprinfo input_info{};
  s_tfexprinfo vector_info{};
  s_tfexprinfo real_info{};
  s_tfexprinfo string_info{};
  callback_ok =
      callback_ok && tf_getp(1) == static_cast<PLI_INT32>(UINT32_C(0xa5)) &&
      tf_getlongp(&high, 2) ==
          static_cast<PLI_INT32>(UINT32_C(0x11223344)) &&
      high == static_cast<PLI_INT32>(UINT32_C(0x55667788)) &&
      tf_getrealp(3) == 2.5 &&
      std::string_view{tf_getcstringp(5)} == "hello" &&
      std::string_view{tf_strgetp(1, 'b')} == "101zz101" &&
      std::string_view{tf_strgetp(1, 'h')} == "xx" &&
      tf_strgetp(1, 'q') == nullptr &&
      tf_exprinfo(1, &input_info) == &input_info &&
      input_info.expr_value_p != nullptr &&
      static_cast<PLI_UINT32>(input_info.expr_value_p[0].avalbits) ==
          UINT32_C(0xa5) &&
      static_cast<PLI_UINT32>(input_info.expr_value_p[0].bvalbits) ==
          UINT32_C(0x18) &&
      std::string_view{input_info.expr_string, 5} == "input" &&
      tf_exprinfo(3, &real_info) == &real_info &&
      real_info.real_value == 2.5 &&
      tf_exprinfo(5, &string_info) == &string_info &&
      std::string_view{string_info.expr_string} == "hello" &&
      tf_putp(1, 0) != 0 && tf_putrealp(3, 1.0) != 0 &&
      tf_putrealp(1, 1.0) != 0;

  callback_ok = callback_ok && tf_exprinfo(2, &vector_info) == &vector_info &&
                vector_info.expr_ngroups == 3 &&
                vector_info.expr_value_p[2].avalbits == 1 &&
                vector_info.expr_value_p[2].bvalbits == 1;
  vector_info.expr_value_p[0].avalbits =
      static_cast<PLI_INT32>(UINT32_C(0xdeadbeef));
  vector_info.expr_value_p[0].bvalbits =
      static_cast<PLI_INT32>(UINT32_C(0x01010101));
  vector_info.expr_value_p[2].avalbits = 1;
  vector_info.expr_value_p[2].bvalbits = 0;
  callback_ok = callback_ok && tf_propagatep(2) == 0;

  callback_ok = callback_ok && tf_exprinfo(4, &real_info) == &real_info;
  real_info.real_value = 6.5;
  callback_ok = callback_ok && tf_propagatep(4) == 0 &&
                tf_putp(6, 0x1234) == 0 &&
                tf_putlongp(7, static_cast<PLI_INT32>(UINT32_C(0x89abcdef)),
                            static_cast<PLI_INT32>(UINT32_C(0x76543210))) == 0 &&
                tf_propagatep(1) != 0 && tf_putp(0, 0x13579bdf) == 0;
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL default_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_calltf) {
    callback_ok = callback_ok && tf_getp(1) == 0 &&
                  tf_getrealp(2) == 0.0 &&
                  std::string_view{tf_getcstringp(3)}.empty();
  }
  return 0;
}

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

fsim::runtime::TfArgument integral_argument(
    const fsim::runtime::TfArgumentKind kind, const std::uint32_t width,
    const char* const expression) {
  return {.kind = kind,
          .width = width,
          .is_signed = false,
          .lhs_select = -1,
          .rhs_select = -1,
          .expression = expression};
}

fsim::runtime::TfArgumentValue integral_value(
    std::initializer_list<s_vecval> words) {
  return {.kind = fsim::runtime::TfValueKind::Integral,
          .vector_words = words,
          .real = 0.0,
          .string = {}};
}

}  // namespace

int main() {
  using fsim::runtime::TfArgument;
  using fsim::runtime::TfArgumentKind;
  using fsim::runtime::TfArgumentValue;
  using fsim::runtime::TfCallError;
  using fsim::runtime::TfRegistration;
  using fsim::runtime::TfRegistrationKind;
  using fsim::runtime::TfValueError;
  using fsim::runtime::TfValueKind;
  using fsim::runtime::bind_tf_call;
  using fsim::runtime::make_default_tf_values;
  using fsim::runtime::validate_and_copy_tf_values;

  require(tf_getp(1) == 0 && tf_getlongp(nullptr, 1) == 0 &&
              tf_getrealp(1) == 0.0 && tf_getcstringp(1) == nullptr &&
              tf_strgetp(1, 'b') == nullptr && tf_evaluatep(1) != 0 &&
              tf_propagatep(1) != 0,
          "TF value services are neutral without an active call");

  const std::array arguments{
      integral_argument(TfArgumentKind::ReadOnly, 8, "input"),
      integral_argument(TfArgumentKind::ReadWrite, 65, "wide"),
      TfArgument{.kind = TfArgumentKind::ReadOnlyReal,
                 .width = 64,
                 .is_signed = false,
                 .lhs_select = -1,
                 .rhs_select = -1,
                 .expression = "real_input"},
      TfArgument{.kind = TfArgumentKind::ReadWriteReal,
                 .width = 64,
                 .is_signed = false,
                 .lhs_select = -1,
                 .rhs_select = -1,
                 .expression = "real_output"},
      TfArgument{.kind = TfArgumentKind::String,
                 .width = 64,
                 .is_signed = false,
                 .lhs_select = -1,
                 .rhs_select = -1,
                 .expression = "label"},
      integral_argument(TfArgumentKind::ReadWrite, 16, "short_output"),
      integral_argument(TfArgumentKind::ReadWrite, 65, "long_output"),
  };
  const std::array values{
      integral_value({{static_cast<PLI_INT32>(UINT32_C(0xa5)),
                       static_cast<PLI_INT32>(UINT32_C(0x18))}}),
      integral_value({{static_cast<PLI_INT32>(UINT32_C(0x11223344)), 0},
                      {static_cast<PLI_INT32>(UINT32_C(0x55667788)),
                       static_cast<PLI_INT32>(UINT32_C(0x80000000))},
                      {1, 1}}),
      TfArgumentValue{.kind = TfValueKind::Real,
                      .vector_words = {},
                      .real = 2.5,
                      .string = {}},
      TfArgumentValue{.kind = TfValueKind::Real,
                      .vector_words = {},
                      .real = 4.25,
                      .string = {}},
      TfArgumentValue{.kind = TfValueKind::String,
                      .vector_words = {},
                      .real = 0.0,
                      .string = "hello"},
      integral_value({{0, 0}}),
      integral_value({{0, 0}, {0, 0}, {0, 0}}),
  };
  const auto validated = validate_and_copy_tf_values(arguments, values);
  require(validated && validated.value.size() == values.size() &&
              validated.value[1].vector_words[2].bvalbits == 1,
          "valid TF values retain exact four-state words");

  const TfRegistration registration{
      .kind = TfRegistrationKind::Function,
      .user_data = 0,
      .checktf = nullptr,
      .sizetf = value_callback,
      .calltf = value_callback,
      .misctf = nullptr,
      .name = "$values",
  };
  auto bound = bind_tf_call(registration, arguments);
  require(static_cast<bool>(bound), "TF value callback binds");
  const auto invoked = bound.value->invoke(values);
  require(invoked && callback_ok && call_count == 1 &&
              invoked.function_result->width == 33 &&
              invoked.function_result->aval_words[0] == UINT32_C(0x13579bdf) &&
              invoked.argument_updates.size() == 4,
          "TF value callback returns one atomic function and update result");
  require(invoked.argument_updates[0].parameter == 2 &&
              static_cast<PLI_UINT32>(invoked.argument_updates[0]
                                          .value.vector_words[0].avalbits) ==
                  UINT32_C(0xdeadbeef) &&
              static_cast<PLI_UINT32>(invoked.argument_updates[0]
                                          .value.vector_words[0].bvalbits) ==
                  UINT32_C(0x01010101) &&
              invoked.argument_updates[0].value.vector_words[2].avalbits == 1 &&
              invoked.argument_updates[0].value.vector_words[2].bvalbits == 0 &&
              invoked.argument_updates[1].parameter == 4 &&
              invoked.argument_updates[1].value.real == 6.5 &&
              invoked.argument_updates[2].parameter == 6 &&
              invoked.argument_updates[2].value.vector_words[0].avalbits ==
                  0x1234 &&
              invoked.argument_updates[3].parameter == 7 &&
              static_cast<PLI_UINT32>(invoked.argument_updates[3]
                                          .value.vector_words[1].avalbits) ==
                  UINT32_C(0x76543210),
          "TF writes preserve source order and exact integer real and X/Z data");

  const std::array default_arguments{
      integral_argument(TfArgumentKind::ReadOnly, 8, "zero"),
      TfArgument{.kind = TfArgumentKind::ReadOnlyReal,
                 .width = 64,
                 .is_signed = false,
                 .lhs_select = -1,
                 .rhs_select = -1,
                 .expression = "zero_real"},
      TfArgument{.kind = TfArgumentKind::String,
                 .width = 64,
                 .is_signed = false,
                 .lhs_select = -1,
                 .rhs_select = -1,
                 .expression = "empty"},
  };
  const TfRegistration default_registration{
      .kind = TfRegistrationKind::Task,
      .user_data = 0,
      .checktf = nullptr,
      .sizetf = nullptr,
      .calltf = default_callback,
      .misctf = nullptr,
      .name = "$defaults",
  };
  auto defaults = make_default_tf_values(default_arguments);
  auto default_bound = bind_tf_call(default_registration, default_arguments);
  require(defaults && default_bound && default_bound.value->invoke() &&
              callback_ok,
          "legacy invoke overload supplies bounded zero values");

  const auto expect = [&](std::vector<TfArgument> argument_copy,
                          std::vector<TfArgumentValue> value_copy,
                          const TfValueError error,
                          const std::uint32_t index,
                          const char* const message) {
    const auto result = validate_and_copy_tf_values(argument_copy, value_copy);
    require(!result && result.value.empty() && result.error == error &&
                result.argument_index == index,
            message);
  };
  expect({arguments.begin(), arguments.end()},
         {values.begin(), values.end() - 1}, TfValueError::Count, 0,
         "TF value count must match immutable metadata");
  auto wrong_kind = std::vector<TfArgumentValue>{values.begin(), values.end()};
  wrong_kind[0].kind = TfValueKind::Real;
  expect({arguments.begin(), arguments.end()}, std::move(wrong_kind),
         TfValueError::Kind, 0, "TF value kind must match argument kind");
  auto wrong_words = std::vector<TfArgumentValue>{values.begin(), values.end()};
  wrong_words[1].vector_words.pop_back();
  expect({arguments.begin(), arguments.end()}, std::move(wrong_words),
         TfValueError::WordCount, 1, "TF vector word count is exact");
  auto high_bits = std::vector<TfArgumentValue>{values.begin(), values.end()};
  high_bits[1].vector_words[2].avalbits = 2;
  expect({arguments.begin(), arguments.end()}, std::move(high_bits),
         TfValueError::HighBits, 1, "TF vector unused high bits are rejected");
  auto bad_string = std::vector<TfArgumentValue>{values.begin(), values.end()};
  bad_string[4].string = std::string{"bad\0value", 9};
  expect({arguments.begin(), arguments.end()}, std::move(bad_string),
         TfValueError::String, 4, "TF string values reject embedded nulls");

  std::vector<TfArgument> resource_arguments;
  std::vector<TfArgumentValue> resource_values;
  resource_arguments.reserve(65);
  resource_values.reserve(65);
  for (std::uint32_t index = 0; index < 65; ++index) {
    resource_arguments.push_back(integral_argument(
        TfArgumentKind::ReadOnly, fsim::runtime::kMaxTfArgumentWidth,
        "bounded"));
    TfArgumentValue value{.kind = TfValueKind::Integral,
                          .vector_words = {},
                          .real = 0.0,
                          .string = {}};
    value.vector_words.resize(fsim::runtime::kMaxTfArgumentWidth / 32U);
    resource_values.push_back(std::move(value));
  }
  expect(std::move(resource_arguments), std::move(resource_values),
         TfValueError::ResourceLimit, 64,
         "TF invocation value storage has an aggregate byte ceiling");

  const auto calls_before = call_count;
  const std::array short_values{values[0]};
  const auto rejected = bound.value->invoke(short_values);
  require(!rejected && rejected.error == TfCallError::InvalidValues &&
              rejected.argument_updates.empty() && call_count == calls_before,
          "invalid invocation values run no callback and publish no updates");
  return 0;
}

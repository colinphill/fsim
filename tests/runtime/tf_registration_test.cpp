// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_registration.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

std::uint32_t callback_calls{};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  ++callback_calls;
  return user_data + reason;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL misc_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason,
    const PLI_INT32 parameter) {
  ++callback_calls;
  return user_data + reason + parameter;
}

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

fsim_tf_registration_v3 make_registration(
    const std::uint32_t kind, const PLI_BYTE8* const name,
    const std::uint32_t name_size) {
  return {
      .struct_size = sizeof(fsim_tf_registration_v3),
      .kind = kind,
      .user_data = static_cast<PLI_INT32>(kind * 10U),
      .flags = 0,
      .checktf = callback,
      .sizetf = kind == FSIM_TF_REGISTRATION_FUNCTION ? callback : nullptr,
      .calltf = callback,
      .misctf = misc_callback,
      .name_size = name_size,
      .reserved = 0,
      .name = name,
  };
}

}  // namespace

int main() {
  using fsim::runtime::TfRegistrationError;
  using fsim::runtime::TfRegistrationKind;
  using fsim::runtime::kNoTfRegistrationIndex;
  using fsim::runtime::validate_and_copy_tf_registrations;

  static constexpr PLI_BYTE8 task_name[] = "$task";
  static constexpr PLI_BYTE8 function_name[] = "$function_2";
  static constexpr PLI_BYTE8 real_name[] = "$real$function";
  const std::array registrations{
      make_registration(FSIM_TF_REGISTRATION_TASK, task_name,
                        sizeof(task_name) - 1U),
      make_registration(FSIM_TF_REGISTRATION_FUNCTION, function_name,
                        sizeof(function_name) - 1U),
      make_registration(FSIM_TF_REGISTRATION_REAL_FUNCTION, real_name,
                        sizeof(real_name) - 1U),
  };
  const fsim_tf_registration_table_v3 table{
      .abi_version = FSIM_TF_REGISTRATION_TABLE_ABI_VERSION,
      .struct_size = sizeof(fsim_tf_registration_table_v3),
      .flags = 0,
      .entry_count = registrations.size(),
      .entry_stride = sizeof(fsim_tf_registration_v3),
      .reserved = 0,
      .entries = registrations.data(),
  };

  const auto valid = validate_and_copy_tf_registrations(table);
  require(valid && valid.entry_index == kNoTfRegistrationIndex &&
              valid.value.size() == 3 &&
              valid.value[0].kind == TfRegistrationKind::Task &&
              valid.value[1].kind == TfRegistrationKind::Function &&
              valid.value[2].kind == TfRegistrationKind::RealFunction &&
              valid.value[0].name == "$task" &&
              valid.value[1].name == "$function_2" &&
              valid.value[2].name == "$real$function" &&
              valid.value[1].user_data == 20 &&
              valid.value[1].sizetf == callback &&
              valid.value[0].sizetf == nullptr &&
              valid.value[0].calltf == callback && callback_calls == 0,
          "valid TF descriptors are copied in source order without execution");

  auto invalid_table = table;
  invalid_table.entry_count = 0;
  const auto table_failure =
      validate_and_copy_tf_registrations(invalid_table);
  require(!table_failure && table_failure.value.empty() &&
              table_failure.error == TfRegistrationError::Table &&
              table_failure.entry_index == kNoTfRegistrationIndex,
          "invalid table yields no registration transaction");

  const auto expect_entry = [&](const std::uint32_t index, const auto mutate,
                                const TfRegistrationError expected,
                                const char* const message) {
    auto invalid = registrations;
    mutate(invalid[index]);
    auto invalid_header = table;
    invalid_header.entries = invalid.data();
    const auto result = validate_and_copy_tf_registrations(invalid_header);
    require(!result && result.value.empty() && result.error == expected &&
                result.entry_index == index && callback_calls == 0,
            message);
  };

  expect_entry(0, [](auto& value) { --value.struct_size; },
               TfRegistrationError::StructSize,
               "truncated TF descriptor is rejected transactionally");
  expect_entry(1,
               [&](auto& value) { value.struct_size = table.entry_stride + 1U; },
               TfRegistrationError::StructSize,
               "TF descriptor cannot exceed its table stride");
  expect_entry(0, [](auto& value) { value.kind = 0; },
               TfRegistrationError::Kind,
               "TF descriptor rejects a low registration kind");
  expect_entry(0,
               [](auto& value) {
                 value.kind = FSIM_TF_REGISTRATION_REAL_FUNCTION + 1U;
               },
               TfRegistrationError::Kind,
               "TF descriptor rejects a high registration kind");
  expect_entry(0, [](auto& value) { value.flags = 1; },
               TfRegistrationError::Flags,
               "TF descriptor rejects flags");
  expect_entry(0, [](auto& value) { value.reserved = 1; },
               TfRegistrationError::Flags,
               "TF descriptor rejects reserved state");
  expect_entry(0, [](auto& value) { value.calltf = nullptr; },
               TfRegistrationError::Callbacks,
               "TF descriptor requires calltf");
  expect_entry(0, [](auto& value) { value.sizetf = callback; },
               TfRegistrationError::Callbacks,
               "TF task descriptor rejects sizetf");
  expect_entry(1, [](auto& value) { value.sizetf = nullptr; },
               TfRegistrationError::Callbacks,
               "TF integral function descriptor requires sizetf");
  expect_entry(2, [](auto& value) { value.sizetf = callback; },
               TfRegistrationError::Callbacks,
               "TF real function descriptor rejects sizetf");
  expect_entry(0, [](auto& value) { value.name = nullptr; },
               TfRegistrationError::Name,
               "TF descriptor rejects a null name");
  expect_entry(0, [](auto& value) { value.name_size = 1; },
               TfRegistrationError::Name,
               "TF descriptor rejects a one-byte name");
  expect_entry(
      0,
      [](auto& value) {
        value.name_size = FSIM_TF_MAX_REGISTRATION_NAME_SIZE + 1U;
      },
      TfRegistrationError::Name,
      "TF descriptor bounds its name before reading it");
  static constexpr PLI_BYTE8 no_dollar[] = "task";
  expect_entry(
      0,
      [](auto& value) {
        value.name = no_dollar;
        value.name_size = sizeof(no_dollar) - 1U;
      },
      TfRegistrationError::Name,
      "TF descriptor requires a system identifier");
  static constexpr PLI_BYTE8 punctuation[] = "$bad-name";
  expect_entry(
      0,
      [](auto& value) {
        value.name = punctuation;
        value.name_size = sizeof(punctuation) - 1U;
      },
      TfRegistrationError::Name,
      "TF descriptor rejects punctuation in a system identifier");
  static constexpr PLI_BYTE8 digit_first[] = "$2bad";
  expect_entry(
      0,
      [](auto& value) {
        value.name = digit_first;
        value.name_size = sizeof(digit_first) - 1U;
      },
      TfRegistrationError::Name,
      "TF descriptor requires an identifier-start character");
  static constexpr PLI_BYTE8 embedded_zero[]{'$', 'b', '\0', 'd'};
  expect_entry(
      0,
      [](auto& value) {
        value.name = embedded_zero;
        value.name_size = sizeof(embedded_zero);
      },
      TfRegistrationError::Name,
      "TF descriptor rejects embedded terminators");
  expect_entry(
      1,
      [](auto& value) {
        value.name = task_name;
        value.name_size = sizeof(task_name) - 1U;
      },
      TfRegistrationError::DuplicateName,
      "duplicate TF names leave no published registration");

  return 0;
}

// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/tf_plugin_abi.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace fsim::runtime {

inline constexpr std::uint32_t kNoTfRegistrationIndex =
    std::numeric_limits<std::uint32_t>::max();

enum class TfRegistrationKind : std::uint32_t {
  Task = FSIM_TF_REGISTRATION_TASK,
  Function = FSIM_TF_REGISTRATION_FUNCTION,
  RealFunction = FSIM_TF_REGISTRATION_REAL_FUNCTION,
};

enum class TfRegistrationError {
  None,
  Table,
  StructSize,
  Kind,
  Flags,
  Callbacks,
  Pointer,
  Name,
  DuplicateName,
  Allocation,
};

struct TfRegistration {
  TfRegistrationKind kind{TfRegistrationKind::Task};
  PLI_INT32 user_data{};
  fsim_tf_routine_v3 checktf{};
  fsim_tf_routine_v3 sizetf{};
  fsim_tf_routine_v3 calltf{};
  fsim_tf_misc_routine_v3 misctf{};
  std::string name;
};

struct TfRegistrationResult {
  std::vector<TfRegistration> value;
  TfRegistrationError error{TfRegistrationError::None};
  std::uint32_t entry_index{kNoTfRegistrationIndex};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfRegistrationError::None;
  }
};

[[nodiscard]] TfRegistrationResult validate_and_copy_tf_registrations(
    const fsim_tf_registration_table_v3& table) noexcept;

}  // namespace fsim::runtime

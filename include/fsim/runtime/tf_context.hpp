// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

namespace fsim::runtime {

inline constexpr std::uint32_t kMaxTfContextNameSize = 4096;

struct TfContextProfile {
  std::string module_instance_name{"$root"};
  std::string scope_name{"$root"};

  [[nodiscard]] friend bool operator==(
      const TfContextProfile&, const TfContextProfile&) = default;
};

enum class TfContextError {
  None,
  EmptyModuleInstance,
  EmptyScope,
  NameSize,
  ControlCharacter,
  ScopeOwnership,
  Allocation,
};

struct TfContextValidationResult {
  TfContextProfile value;
  TfContextError error{TfContextError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfContextError::None;
  }
};

[[nodiscard]] TfContextValidationResult validate_and_copy_tf_context(
    const TfContextProfile& profile) noexcept;

}  // namespace fsim::runtime

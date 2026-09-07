// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/tf_argument.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime {

inline constexpr std::uint32_t kMaxTfCallValueBytes = 16U << 20U;

enum class TfValueKind {
  Null,
  Integral,
  Real,
  String,
};

enum class TfValueError {
  None,
  Count,
  Kind,
  WordCount,
  HighBits,
  String,
  ResourceLimit,
  Allocation,
};

struct TfArgumentValue {
  TfValueKind kind{TfValueKind::Null};
  std::vector<s_vecval> vector_words;
  double real{};
  std::string string;
};

struct TfValueValidationResult {
  std::vector<TfArgumentValue> value;
  TfValueError error{TfValueError::None};
  std::uint32_t argument_index{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfValueError::None;
  }
};

[[nodiscard]] TfValueValidationResult validate_and_copy_tf_values(
    std::span<const TfArgument> arguments,
    std::span<const TfArgumentValue> values) noexcept;

[[nodiscard]] TfValueValidationResult make_default_tf_values(
    std::span<const TfArgument> arguments) noexcept;

}  // namespace fsim::runtime

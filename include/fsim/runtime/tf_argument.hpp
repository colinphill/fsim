// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/veriuser.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime {

inline constexpr std::uint32_t kMaxTfArguments = 4096;
inline constexpr std::uint32_t kMaxTfArgumentWidth = 1U << 20U;
inline constexpr std::uint32_t kMaxTfExpressionTextSize = 4096;

enum class TfArgumentKind : PLI_INT32 {
  Null = tf_nullparam,
  String = tf_string,
  Special = tf_specialparam,
  ReadOnly = tf_readonly,
  ReadWrite = tf_readwrite,
  ReadWriteBitSelect = tf_rwbitselect,
  ReadWritePartSelect = tf_rwpartselect,
  ReadWriteMemorySelect = tf_rwmemselect,
  ReadOnlyReal = tf_readonlyreal,
  ReadWriteReal = tf_readwritereal,
};

enum class TfArgumentDirection {
  None,
  Input,
  InOut,
};

enum class TfArgumentError {
  None,
  Count,
  Kind,
  Width,
  Selection,
  Expression,
  Allocation,
};

struct TfArgument {
  TfArgumentKind kind{TfArgumentKind::Null};
  std::uint32_t width{};
  bool is_signed{};
  PLI_INT32 lhs_select{-1};
  PLI_INT32 rhs_select{-1};
  std::string expression;

  [[nodiscard]] TfArgumentDirection direction() const noexcept;
};

struct TfArgumentValidationResult {
  std::vector<TfArgument> value;
  TfArgumentError error{TfArgumentError::None};
  std::uint32_t argument_index{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfArgumentError::None;
  }
};

[[nodiscard]] TfArgumentValidationResult validate_and_copy_tf_arguments(
    std::span<const TfArgument> arguments) noexcept;

}  // namespace fsim::runtime

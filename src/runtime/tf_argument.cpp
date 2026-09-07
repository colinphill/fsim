// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_argument.hpp"

#include <cstddef>
#include <new>
#include <utility>

namespace fsim::runtime {
namespace {

[[nodiscard]] bool valid_kind(const TfArgumentKind kind) noexcept {
  switch (kind) {
    case TfArgumentKind::Null:
    case TfArgumentKind::String:
    case TfArgumentKind::Special:
    case TfArgumentKind::ReadOnly:
    case TfArgumentKind::ReadWrite:
    case TfArgumentKind::ReadWriteBitSelect:
    case TfArgumentKind::ReadWritePartSelect:
    case TfArgumentKind::ReadWriteMemorySelect:
    case TfArgumentKind::ReadOnlyReal:
    case TfArgumentKind::ReadWriteReal:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_expression(const std::string& expression) noexcept {
  if (expression.empty() || expression.size() > kMaxTfExpressionTextSize) {
    return false;
  }
  for (const char character : expression) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x20U || byte == 0x7fU) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] TfArgumentValidationResult failure(
    const TfArgumentError error, const std::uint32_t index) noexcept {
  return {.value = {}, .error = error, .argument_index = index};
}

}  // namespace

TfArgumentDirection TfArgument::direction() const noexcept {
  switch (kind) {
    case TfArgumentKind::ReadWrite:
    case TfArgumentKind::ReadWriteBitSelect:
    case TfArgumentKind::ReadWritePartSelect:
    case TfArgumentKind::ReadWriteMemorySelect:
    case TfArgumentKind::ReadWriteReal:
      return TfArgumentDirection::InOut;
    case TfArgumentKind::Null:
      return TfArgumentDirection::None;
    default:
      return TfArgumentDirection::Input;
  }
}

TfArgumentValidationResult validate_and_copy_tf_arguments(
    const std::span<const TfArgument> arguments) noexcept {
  if (arguments.size() > kMaxTfArguments) {
    return failure(TfArgumentError::Count, 0);
  }
  try {
    std::vector<TfArgument> result;
    result.reserve(arguments.size());
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const auto& argument = arguments[index];
      const auto failure_index = static_cast<std::uint32_t>(index);
      if (!valid_kind(argument.kind)) {
        return failure(TfArgumentError::Kind, failure_index);
      }
      if (argument.kind == TfArgumentKind::Null) {
        if (argument.width != 0 || argument.is_signed ||
            argument.lhs_select != -1 || argument.rhs_select != -1 ||
            !argument.expression.empty()) {
          return failure(TfArgumentError::Width, failure_index);
        }
        result.push_back(argument);
        continue;
      }
      if (argument.width == 0 || argument.width > kMaxTfArgumentWidth ||
          ((argument.kind == TfArgumentKind::ReadOnlyReal ||
            argument.kind == TfArgumentKind::ReadWriteReal) &&
           argument.width != 64) ||
          (argument.kind == TfArgumentKind::String &&
           argument.width % 8U != 0)) {
        return failure(TfArgumentError::Width, failure_index);
      }
      const bool selection =
          argument.kind == TfArgumentKind::ReadWriteBitSelect ||
          argument.kind == TfArgumentKind::ReadWritePartSelect ||
          argument.kind == TfArgumentKind::ReadWriteMemorySelect;
      if ((selection &&
           (argument.lhs_select < 0 || argument.rhs_select < 0 ||
            (argument.kind == TfArgumentKind::ReadWriteBitSelect &&
             argument.lhs_select != argument.rhs_select))) ||
          (!selection &&
           (argument.lhs_select != -1 || argument.rhs_select != -1))) {
        return failure(TfArgumentError::Selection, failure_index);
      }
      if (!valid_expression(argument.expression)) {
        return failure(TfArgumentError::Expression, failure_index);
      }
      result.push_back(argument);
    }
    return {.value = std::move(result),
            .error = TfArgumentError::None,
            .argument_index = 0};
  } catch (const std::bad_alloc&) {
    return failure(TfArgumentError::Allocation, 0);
  } catch (...) {
    return failure(TfArgumentError::Allocation, 0);
  }
}

}  // namespace fsim::runtime

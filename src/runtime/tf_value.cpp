// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_value.hpp"

#include <new>
#include <utility>

namespace fsim::runtime {

namespace {

[[nodiscard]] TfValueValidationResult failure(
    const TfValueError error, const std::uint32_t index) noexcept {
  return {.value = {}, .error = error, .argument_index = index};
}

[[nodiscard]] TfValueKind expected_kind(const TfArgumentKind kind) noexcept {
  if (kind == TfArgumentKind::Null) {
    return TfValueKind::Null;
  }
  if (kind == TfArgumentKind::String) {
    return TfValueKind::String;
  }
  if (kind == TfArgumentKind::ReadOnlyReal ||
      kind == TfArgumentKind::ReadWriteReal) {
    return TfValueKind::Real;
  }
  return TfValueKind::Integral;
}

[[nodiscard]] bool add_size(
    std::uint64_t& total, const std::uint64_t addition) noexcept {
  if (addition > kMaxTfCallValueBytes ||
      total > kMaxTfCallValueBytes - addition) {
    return false;
  }
  total += addition;
  return true;
}

[[nodiscard]] TfValueError validate_value(
    const TfArgument& argument, const TfArgumentValue& value,
    std::uint64_t& total_bytes) noexcept {
  if (value.kind != expected_kind(argument.kind)) {
    return TfValueError::Kind;
  }
  if (value.kind == TfValueKind::Null) {
    return value.vector_words.empty() && value.real == 0.0 &&
                   value.string.empty()
               ? TfValueError::None
               : TfValueError::Kind;
  }
  if (value.kind == TfValueKind::Real) {
    if (!value.vector_words.empty() || !value.string.empty()) {
      return TfValueError::Kind;
    }
    return add_size(total_bytes, sizeof(double))
               ? TfValueError::None
               : TfValueError::ResourceLimit;
  }
  if (value.kind == TfValueKind::String) {
    if (!value.vector_words.empty() || value.real != 0.0 ||
        value.string.size() > argument.width / 8U ||
        value.string.find('\0') != std::string::npos) {
      return TfValueError::String;
    }
    return add_size(total_bytes, value.string.size() + 1U)
               ? TfValueError::None
               : TfValueError::ResourceLimit;
  }

  const auto expected_words = (argument.width + 31U) / 32U;
  if (value.vector_words.size() != expected_words || value.real != 0.0 ||
      !value.string.empty()) {
    return TfValueError::WordCount;
  }
  const auto remainder = argument.width % 32U;
  if (remainder != 0U) {
    const auto mask = (UINT32_C(1) << remainder) - UINT32_C(1);
    const auto aval = static_cast<PLI_UINT32>(
        value.vector_words.back().avalbits);
    const auto bval = static_cast<PLI_UINT32>(
        value.vector_words.back().bvalbits);
    if ((aval & ~mask) != 0U || (bval & ~mask) != 0U) {
      return TfValueError::HighBits;
    }
  }
  const auto bytes = static_cast<std::uint64_t>(expected_words) *
                     sizeof(s_vecval);
  return add_size(total_bytes, bytes) ? TfValueError::None
                                      : TfValueError::ResourceLimit;
}

}  // namespace

TfValueValidationResult validate_and_copy_tf_values(
    const std::span<const TfArgument> arguments,
    const std::span<const TfArgumentValue> values) noexcept {
  if (arguments.size() != values.size()) {
    return failure(TfValueError::Count, 0);
  }
  try {
    std::uint64_t total_bytes{};
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const auto error = validate_value(
          arguments[index], values[index], total_bytes);
      if (error != TfValueError::None) {
        return failure(error, static_cast<std::uint32_t>(index));
      }
    }
    return {.value = {values.begin(), values.end()},
            .error = TfValueError::None,
            .argument_index = 0};
  } catch (const std::bad_alloc&) {
    return failure(TfValueError::Allocation, 0);
  } catch (...) {
    return failure(TfValueError::Allocation, 0);
  }
}

TfValueValidationResult make_default_tf_values(
    const std::span<const TfArgument> arguments) noexcept {
  try {
    std::vector<TfArgumentValue> values;
    values.reserve(arguments.size());
    std::uint64_t total_bytes{};
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const auto& argument = arguments[index];
      TfArgumentValue value;
      value.kind = expected_kind(argument.kind);
      if (value.kind == TfValueKind::Integral) {
        const auto word_count = (argument.width + 31U) / 32U;
        const auto bytes = static_cast<std::uint64_t>(word_count) *
                           sizeof(s_vecval);
        if (!add_size(total_bytes, bytes)) {
          return failure(TfValueError::ResourceLimit,
                         static_cast<std::uint32_t>(index));
        }
        value.vector_words.resize(word_count);
      } else if (value.kind == TfValueKind::Real) {
        if (!add_size(total_bytes, sizeof(double))) {
          return failure(TfValueError::ResourceLimit,
                         static_cast<std::uint32_t>(index));
        }
      } else if (value.kind == TfValueKind::String &&
                 !add_size(total_bytes, 1U)) {
        return failure(TfValueError::ResourceLimit,
                       static_cast<std::uint32_t>(index));
      }
      values.push_back(std::move(value));
    }
    return {.value = std::move(values),
            .error = TfValueError::None,
            .argument_index = 0};
  } catch (const std::bad_alloc&) {
    return failure(TfValueError::Allocation, 0);
  } catch (...) {
    return failure(TfValueError::Allocation, 0);
  }
}

}  // namespace fsim::runtime

// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/systemverilog_string.hpp"

#include <stdexcept>

namespace fsim::runtime {
namespace {

[[nodiscard]] bool continuation(const unsigned char value) noexcept {
  return (value & 0xc0U) == 0x80U;
}

[[nodiscard]] SystemVerilogStringCodePoint decode_one(
    const std::string_view value,
    const std::size_t offset) {
  const auto remaining = value.size() - offset;
  const auto first = static_cast<unsigned char>(value[offset]);
  if (first <= 0x7fU) return {first, offset, 1};

  const auto require_continuation = [&](const std::size_t relative) {
    if (relative >= remaining
        || !continuation(static_cast<unsigned char>(value[offset + relative]))) {
      throw std::invalid_argument{"invalid UTF-8 continuation byte"};
    }
    return static_cast<unsigned char>(value[offset + relative]);
  };
  if (first >= 0xc2U && first <= 0xdfU) {
    const auto second = require_continuation(1);
    return {
        static_cast<std::uint32_t>((first & 0x1fU) << 6U)
            | static_cast<std::uint32_t>(second & 0x3fU),
        offset,
        2};
  }
  if (first >= 0xe0U && first <= 0xefU) {
    const auto second = require_continuation(1);
    const auto third = require_continuation(2);
    if ((first == 0xe0U && second < 0xa0U)
        || (first == 0xedU && second >= 0xa0U)) {
      throw std::invalid_argument{"invalid UTF-8 scalar value"};
    }
    return {
        static_cast<std::uint32_t>((first & 0x0fU) << 12U)
            | static_cast<std::uint32_t>((second & 0x3fU) << 6U)
            | static_cast<std::uint32_t>(third & 0x3fU),
        offset,
        3};
  }
  if (first >= 0xf0U && first <= 0xf4U) {
    const auto second = require_continuation(1);
    const auto third = require_continuation(2);
    const auto fourth = require_continuation(3);
    if ((first == 0xf0U && second < 0x90U)
        || (first == 0xf4U && second >= 0x90U)) {
      throw std::invalid_argument{"invalid UTF-8 scalar value"};
    }
    return {
        static_cast<std::uint32_t>((first & 0x07U) << 18U)
            | static_cast<std::uint32_t>((second & 0x3fU) << 12U)
            | static_cast<std::uint32_t>((third & 0x3fU) << 6U)
            | static_cast<std::uint32_t>(fourth & 0x3fU),
        offset,
        4};
  }
  throw std::invalid_argument{"invalid UTF-8 leading byte"};
}

[[nodiscard]] std::string encode(const std::uint32_t value) {
  std::string result;
  if (value <= 0x7fU) {
    result.push_back(static_cast<char>(value));
  } else if (value <= 0x7ffU) {
    result.push_back(static_cast<char>(0xc0U | (value >> 6U)));
    result.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
  } else if (value <= 0xffffU) {
    if (value >= 0xd800U && value <= 0xdfffU) {
      throw std::invalid_argument{"string replacement is a surrogate"};
    }
    result.push_back(static_cast<char>(0xe0U | (value >> 12U)));
    result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
    result.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
  } else if (value <= 0x10ffffU) {
    result.push_back(static_cast<char>(0xf0U | (value >> 18U)));
    result.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3fU)));
    result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
    result.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
  } else {
    throw std::invalid_argument{"string replacement exceeds U+10FFFF"};
  }
  return result;
}

}  // namespace

std::vector<SystemVerilogStringCodePoint>
systemverilog_string_code_points(const std::string_view value) {
  std::vector<SystemVerilogStringCodePoint> result;
  result.reserve(value.size());
  for (std::size_t offset = 0; offset < value.size();) {
    auto decoded = decode_one(value, offset);
    offset += decoded.byte_count;
    result.push_back(decoded);
  }
  return result;
}

std::size_t systemverilog_string_length(const std::string_view value) {
  std::size_t result{};
  for (std::size_t offset = 0; offset < value.size(); ++result) {
    offset += decode_one(value, offset).byte_count;
  }
  return result;
}

bool systemverilog_string_is_valid(const std::string_view value) noexcept {
  try {
    (void)systemverilog_string_length(value);
    return true;
  } catch (...) {
    return false;
  }
}

std::uint32_t systemverilog_string_at(
    const std::string_view value,
    const std::size_t index) {
  std::size_t ordinal{};
  for (std::size_t offset = 0; offset < value.size(); ++ordinal) {
    const auto decoded = decode_one(value, offset);
    if (ordinal == index) return decoded.value;
    offset += decoded.byte_count;
  }
  throw std::out_of_range{"string index is outside the code-point range"};
}

std::string systemverilog_string_slice(
    const std::string_view value,
    const std::size_t first,
    const std::size_t last) {
  if (last < first) return {};
  std::size_t first_offset = value.size();
  std::size_t end_offset = value.size();
  bool found_first{};
  bool found_last{};
  std::size_t ordinal{};
  for (std::size_t offset = 0; offset < value.size(); ++ordinal) {
    const auto decoded = decode_one(value, offset);
    if (ordinal == first) {
      first_offset = offset;
      found_first = true;
    }
    offset += decoded.byte_count;
    if (ordinal == last) {
      end_offset = offset;
      found_last = true;
      break;
    }
  }
  if (!found_first || !found_last) {
    throw std::out_of_range{"string slice is outside the code-point range"};
  }
  return std::string{value.substr(first_offset, end_offset - first_offset)};
}

void systemverilog_string_replace(
    std::string& value,
    const std::size_t index,
    const std::uint32_t code_point,
    const std::size_t maximum_bytes) {
  const auto replacement = encode(code_point);
  const auto points = systemverilog_string_code_points(value);
  if (index >= points.size()) {
    throw std::out_of_range{"string index is outside the code-point range"};
  }
  const auto& selected = points[index];
  const auto retained_bytes = value.size() - selected.byte_count;
  if (retained_bytes > maximum_bytes
      || replacement.size() > maximum_bytes - retained_bytes) {
    throw std::length_error{"string replacement exceeds the byte limit"};
  }
  value.replace(selected.byte_offset, selected.byte_count, replacement);
}

int systemverilog_string_compare(
    const std::string_view lhs,
    const std::string_view rhs) {
  const auto left = systemverilog_string_code_points(lhs);
  const auto right = systemverilog_string_code_points(rhs);
  const auto count = left.size() < right.size() ? left.size() : right.size();
  for (std::size_t index = 0; index < count; ++index) {
    if (left[index].value != right[index].value) {
      return left[index].value < right[index].value ? -1 : 1;
    }
  }
  return left.size() == right.size() ? 0 : left.size() < right.size() ? -1 : 1;
}

}  // namespace fsim::runtime

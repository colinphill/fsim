// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <stdexcept>

namespace fsim::runtime {

std::size_t systemverilog_string_length(
    const std::string_view value) {
  return value.size();
}

std::uint32_t systemverilog_string_at(
    const std::string_view value,
    const std::size_t index) {
  if (index >= value.size()) {
    throw std::out_of_range{"string index is outside the byte range"};
  }
  return static_cast<std::uint32_t>(
      static_cast<unsigned char>(value[index]));
}

std::string systemverilog_string_slice(
    const std::string_view value,
    const std::size_t first,
    const std::size_t last) {
  if (last < first) return {};
  if (first >= value.size() || last >= value.size()) {
    throw std::out_of_range{"string slice is outside the byte range"};
  }
  return std::string{value.substr(first, last - first + 1U)};
}

void systemverilog_string_replace(
    std::string& value,
    const std::size_t index,
    const std::uint32_t byte,
    const std::size_t maximum_bytes) {
  if (index >= value.size()) {
    throw std::out_of_range{"string index is outside the byte range"};
  }
  if (value.size() > maximum_bytes) {
    throw std::length_error{"string value exceeds the byte limit"};
  }
  value[index] = static_cast<char>(byte & 0xffU);
}

int systemverilog_string_compare(
    const std::string_view lhs,
    const std::string_view rhs) {
  const auto count = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0; index < count; ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (left != right) {
      return left < right ? -1 : 1;
    }
  }
  return lhs.size() == rhs.size() ? 0 : lhs.size() < rhs.size() ? -1 : 1;
}

}  // namespace fsim::runtime

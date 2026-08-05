// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

struct SystemVerilogStringCodePoint {
  std::uint32_t value{};
  std::size_t byte_offset{};
  std::size_t byte_count{};
};

/// Decode strict UTF-8 into Unicode scalar values in source order.
/// Overlong encodings, surrogate values, truncated sequences, and values
/// beyond U+10FFFF reject with std::invalid_argument.
[[nodiscard]] std::vector<SystemVerilogStringCodePoint>
systemverilog_string_code_points(std::string_view value);

[[nodiscard]] std::size_t systemverilog_string_length(
    std::string_view value);
[[nodiscard]] bool systemverilog_string_is_valid(
    std::string_view value) noexcept;
[[nodiscard]] std::uint32_t systemverilog_string_at(
    std::string_view value, std::size_t index);
[[nodiscard]] std::string systemverilog_string_slice(
    std::string_view value, std::size_t first, std::size_t last);
void systemverilog_string_replace(
    std::string& value,
    std::size_t index,
    std::uint32_t code_point,
    std::size_t maximum_bytes);
[[nodiscard]] int systemverilog_string_compare(
    std::string_view lhs, std::string_view rhs);

}  // namespace fsim::runtime

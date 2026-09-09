// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fsim::runtime {

[[nodiscard]] std::size_t systemverilog_string_length(
    std::string_view value);
[[nodiscard]] std::uint32_t systemverilog_string_at(
    std::string_view value, std::size_t index);
[[nodiscard]] std::string systemverilog_string_slice(
    std::string_view value, std::size_t first, std::size_t last);
void systemverilog_string_replace(
    std::string& value,
    std::size_t index,
    std::uint32_t byte,
    std::size_t maximum_bytes);
[[nodiscard]] int systemverilog_string_compare(
    std::string_view lhs, std::string_view rhs);

}  // namespace fsim::runtime

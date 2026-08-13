// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::runtime::simir {

enum class StringMethodOperator : std::uint8_t;
struct StringMethod;
struct SystemVerilogTimeFormat;
struct StringMethodResult {
    std::optional<std::uint32_t> integer;
    std::optional<std::uint64_t> scalar_bits;
    std::optional<std::string> string;
};

[[nodiscard]] std::uint32_t string_getc(
    std::string_view value, std::optional<std::int32_t> index);
[[nodiscard]] std::int32_t string_compare(
    std::string_view lhs, std::string_view rhs,
    bool case_insensitive);
[[nodiscard]] std::string string_change_case(
    std::string_view value, bool uppercase);
[[nodiscard]] std::string string_substr(
    std::string_view value,
    std::optional<std::int32_t> first,
    std::optional<std::int32_t> last);
void string_putc(
    std::string& value,
    std::optional<std::int32_t> index,
    std::optional<std::uint32_t> character);
[[nodiscard]] std::int32_t string_to_integer(
    std::string_view value, unsigned radix);
[[nodiscard]] std::string string_from_integer(
    std::int32_t value, unsigned radix);
[[nodiscard]] StringMethodResult execute_string_method(
    StringMethodOperator operation,
    std::string& source,
    std::string_view argument,
    std::optional<std::int32_t> first,
    std::optional<std::int32_t> second,
    std::optional<std::uint64_t> first_bits = std::nullopt);
void execute_string_format(
    const StringMethod& operation,
    std::string& destination,
    const PackedLogic4& packed_value,
    std::string_view string_value,
    std::uint64_t tick,
    const SystemVerilogTimeFormat& time_format);

} // namespace fsim::runtime::simir

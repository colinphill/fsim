// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

using Value = SystemVerilogConstantValue;

inline constexpr std::uint32_t maximum_constant_width = 16U * 1024U * 1024U;

inline constexpr std::uint64_t maximum_constant_work_units = 64U * 1024U * 1024U;

[[nodiscard]] char ascii_lower(const char value) noexcept;

[[nodiscard]] std::size_t checked_constant_width(
    const std::uint64_t width);

[[nodiscard]] PackedLogic4 packed_from_low_word(
    const std::uint64_t bits,
    const std::uint64_t unknown_bits,
    const std::uint64_t high_impedance_bits,
    const std::uint32_t width);

[[nodiscard]] std::uint64_t width_mask(const std::uint32_t width) noexcept;

void normalize(Value& value) noexcept;

void convert_to_two_state(Value& value);

void append_packed(Value& destination, const Value& operand);

[[nodiscard]] Value make_known(
    const std::uint64_t bits,
    const std::uint32_t width,
    const bool is_signed,
    const bool unsized,
    const frontend::SourceSpan& source);

[[nodiscard]] Value make_unknown(
    const std::uint32_t width,
    const bool is_signed,
    const frontend::SourceSpan& source);

[[nodiscard]] std::string cleaned_digits(const std::string_view text);

[[nodiscard]] std::optional<std::uint64_t> parse_unsigned(
    const std::string_view text,
    const int base);

struct ParsedDecimal {
    PackedLogic4 packed;
    std::uint32_t width { };
};

[[nodiscard]] std::optional<ParsedDecimal> parse_decimal_packed(
    std::string_view digits,
    const std::optional<std::uint32_t> explicit_width,
    const bool reserve_sign,
    std::string& error);

[[nodiscard]] std::optional<Value> parse_based_literal(
    const Expression& expression,
    std::string& error);

[[nodiscard]] Value resized(
    Value value,
    const std::uint32_t width);

[[nodiscard]] Value common_operand(
    Value value,
    const std::uint32_t width,
    const bool common_signed);

[[nodiscard]] std::optional<std::uint64_t> nonnegative_count(
    const Value& value,
    std::string& error);

[[nodiscard]] bool packed_one(
    const PackedLogic4& value,
    const std::uint32_t bit) noexcept;

[[nodiscard]] bool packed_is_zero(
    const PackedLogic4& value,
    const std::uint32_t width) noexcept;

[[nodiscard]] bool packed_is_one(
    const PackedLogic4& value,
    const std::uint32_t width) noexcept;

[[nodiscard]] bool packed_is_all_ones(
    const PackedLogic4& value,
    const std::uint32_t width) noexcept;

[[nodiscard]] bool packed_is_signed_minimum(
    const PackedLogic4& value,
    const std::uint32_t width) noexcept;

[[nodiscard]] PackedLogic4 packed_add(
    const PackedLogic4& left,
    const PackedLogic4& right,
    const std::uint32_t width);

[[nodiscard]] PackedLogic4 packed_negate(
    const PackedLogic4& value,
    const std::uint32_t width);

[[nodiscard]] int packed_compare_unsigned(
    const PackedLogic4& left,
    const PackedLogic4& right,
    const std::uint32_t width) noexcept;

[[nodiscard]] PackedLogic4 packed_subtract(
    const PackedLogic4& left,
    const PackedLogic4& right,
    const std::uint32_t width);

[[nodiscard]] PackedLogic4 packed_multiply(
    const PackedLogic4& left,
    const PackedLogic4& right,
    const std::uint32_t width);

struct PackedDivision {
    PackedLogic4 quotient;
    PackedLogic4 remainder;
};

[[nodiscard]] std::optional<PackedDivision> packed_divide_unsigned(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor,
    const std::uint32_t width);

[[nodiscard]] std::optional<Value> evaluate_wide_arithmetic(
    const Value& left,
    const Value& right,
    const std::string_view operation,
    const frontend::SourceSpan& source,
    std::string& error);

enum class Truth { False,
    True,
    Unknown };

[[nodiscard]] Truth truth(const Value& value) noexcept;

[[nodiscard]] Value logical_result(
    const Truth value,
    const frontend::SourceSpan& source);

[[nodiscard]] std::optional<std::uint64_t> nonnegative_count(
    const Value& value,
    std::string& error);

[[nodiscard]] Value bitwise(
    const Value& left,
    const Value& right,
    const std::string_view operation,
    const frontend::SourceSpan& source);

[[nodiscard]] int compare_known(
    const Value& left,
    const Value& right) noexcept;

[[nodiscard]] Truth wildcard_equal(
    const Value& left,
    const Value& right) noexcept;

[[nodiscard]] Truth logical_and(
    const Truth lhs,
    const Truth rhs) noexcept;

[[nodiscard]] Truth relational(
    const Value& lhs,
    const Value& rhs,
    const bool less_equal) noexcept;

[[nodiscard]] std::optional<Value> evaluate_impl(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error);

struct ConstantProfile {
    std::uint32_t width { };
    bool is_signed { };
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
};

[[nodiscard]] std::optional<ConstantProfile> constant_profile(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment);

[[nodiscard]] std::optional<Value> evaluate_binary(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error);

} // namespace fsim::elaboration::elaboration_detail

// SPDX-License-Identifier: Apache-2.0
#include "elaboration_sv_constant_evaluator_internal.hpp"

#include <bit>
#include <sstream>

namespace fsim::elaboration::elaboration_detail {

SystemVerilogConstantValue::SystemVerilogConstantValue(
    const std::uint64_t initial_bits,
    const std::uint64_t initial_unknown_bits,
    const std::uint64_t initial_high_impedance_bits,
    const std::uint32_t initial_width,
    const bool initial_signed,
    const bool initial_unsized,
    frontend::SourceSpan initial_source)
    : packed { packed_from_low_word(
          initial_bits,
          initial_unknown_bits,
          initial_high_impedance_bits,
          initial_width) }
    , bits { initial_bits }
    , unknown_bits { initial_unknown_bits }
    , high_impedance_bits { initial_high_impedance_bits }
    , width { initial_width }
    , is_signed { initial_signed }
    , unsized { initial_unsized }
    , source { std::move(initial_source) }
    , packed_range { frontend::PackedRange {
          static_cast<std::int64_t>(initial_width - 1U), 0, true } }
{
    refresh_low_word_mirrors();
}

SystemVerilogConstantValue::SystemVerilogConstantValue(
    PackedLogic4 initial_packed,
    const bool initial_signed,
    const bool initial_unsized,
    const frontend::ValueDomain initial_domain,
    std::string initial_nominal_type,
    frontend::SourceSpan initial_source)
    : packed { std::move(initial_packed) }
    , width { static_cast<std::uint32_t>(packed.width()) }
    , is_signed { initial_signed }
    , unsized { initial_unsized }
    , domain { initial_domain }
    , nominal_type { std::move(initial_nominal_type) }
    , source { std::move(initial_source) }
    , packed_range { frontend::PackedRange {
          static_cast<std::int64_t>(packed.width() - 1U), 0, true } }
{
    (void)checked_constant_width(packed.width());
    refresh_low_word_mirrors();
}

void SystemVerilogConstantValue::refresh_low_word_mirrors() noexcept
{
    const auto mask = width_mask(width);
    if (!packed.is_logic9()) {
        const auto aval = packed.aval_words().front() & mask;
        const auto bval = packed.bval_words().front() & mask;
        bits = aval & ~bval;
        unknown_bits = bval;
        high_impedance_bits = bval & ~aval;
        return;
    }

    const auto plane0 = packed.logic9_plane_words(0).front();
    const auto plane1 = packed.logic9_plane_words(1).front();
    const auto plane2 = packed.logic9_plane_words(2).front();
    const auto plane3 = packed.logic9_plane_words(3).front();
    bits = plane0 & plane1 & ~plane3 & mask;
    unknown_bits = (~plane1 | plane3) & mask;
    high_impedance_bits = plane2
        & ~(plane0 | plane1 | plane3) & mask;
}

std::uint64_t SystemVerilogConstantValue::mask() const noexcept
{
    return width_mask(width);
}

std::optional<SystemVerilogConstantValue>
evaluate_systemverilog_constant_expression(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error)
{
    error.clear();
    auto result = evaluate_impl(
        expression, environment, fallback_environment, error);
    if (result) {
        normalize(*result);
    }
    return result;
}

bool is_systemverilog_nominal_packed_type(
    const frontend::Type& type) noexcept
{
    return !type.nominal_type.empty()
        && (type.packed_aggregate
                != frontend::PackedAggregateKind::None
            || !type.enumeration_literals.empty());
}

std::optional<SystemVerilogConstantValue>
convert_systemverilog_parameter_value(
    const SystemVerilogConstantValue& value,
    const frontend::Type& type,
    std::string& error)
{
    auto result = value;
    if (result.unbounded) {
        if (type.spelling == "implicit" && type.named_type.empty()) {
            return result;
        }
        error = "symbolic unbounded '$' requires an implicit parameter type";
        return std::nullopt;
    }
    if (is_systemverilog_nominal_packed_type(type)
        && result.nominal_type != type.nominal_type) {
        error = "nominal packed parameter type '" + type.spelling
            + "' requires the same nominal value, a matching explicit "
              "cast, or a contextual assignment pattern (expected '"
            + type.nominal_type + "', received '"
            + (result.nominal_type.empty()
                    ? std::string { "<none>" }
                    : result.nominal_type)
            + "')";
        return std::nullopt;
    }
    if (!(type.spelling == "implicit" && type.named_type.empty())) {
        const auto width = type.width();
        if (!width || *width == 0
            || *width > maximum_constant_width) {
            error = "declared SystemVerilog parameter type does not have a "
                    "width within the 16,777,216-bit resource limit";
            return std::nullopt;
        }
        result = resized(result, static_cast<std::uint32_t>(*width));
        result.is_signed = type.is_signed;
        result.unsized = false;
        result.packed_range = type.packed_range.value_or(
            frontend::PackedRange {
                static_cast<std::int64_t>(*width - 1U), 0, true });
    }
    const bool two_state = type.domain == frontend::ValueDomain::Bit2
        || type.spelling == "bit" || type.spelling == "byte"
        || type.spelling == "shortint" || type.spelling == "int"
        || type.spelling == "longint";
    if (two_state) {
        convert_to_two_state(result);
    }
    if (type.domain != frontend::ValueDomain::Unknown) {
        result.domain = type.domain;
    }
    result.nominal_type = !type.nominal_type.empty()
        ? type.nominal_type
        : !type.named_type.empty() ? type.named_type
                                   : type.spelling;
    if (result.domain == frontend::ValueDomain::Logic9
        && !result.packed.is_logic9()) {
        result.packed = result.packed.promoted_to_logic9();
        result.refresh_low_word_mirrors();
    }
    normalize(result);
    return result;
}

} // namespace fsim::elaboration::elaboration_detail

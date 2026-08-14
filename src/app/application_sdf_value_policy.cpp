// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_value_policy.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::app {
namespace {
    using boost::multiprecision::cpp_int;
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SdfExactDecimal;
    using frontend::SdfExactValue;
    using frontend::SdfExactValueKind;
    using frontend::SdfNormalizedTimescale;
    using frontend::SourceSpan;

    [[nodiscard]] SdfValueSelectionResult failure(SdfValuePolicyError error,
        std::string code, std::string message, const SourceSpan& span)
    {
        SdfValueSelectionResult result;
        result.error = error;
        result.diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
        return result;
    }

    [[nodiscard]] SdfTimingCheckValueSelectionResult timing_failure(
        SdfValuePolicyError error, std::string code, std::string message,
        const SourceSpan& span)
    {
        SdfTimingCheckValueSelectionResult result;
        result.error = error;
        result.diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
        return result;
    }

    [[nodiscard]] SdfPercentageSelectionResult percentage_failure(
        SdfValuePolicyError error, std::string code, std::string message,
        const SourceSpan& span)
    {
        SdfPercentageSelectionResult result;
        result.error = error;
        result.diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
        return result;
    }

    [[nodiscard]] std::string_view selection_name(
        const SdfDelaySelection selection) noexcept
    {
        switch (selection) {
        case SdfDelaySelection::Minimum:
            return "minimum";
        case SdfDelaySelection::Typical:
            return "typical";
        case SdfDelaySelection::Maximum:
            return "maximum";
        }
        return "invalid";
    }

    [[nodiscard]] std::optional<std::size_t> selection_index(
        const SdfDelaySelection selection) noexcept
    {
        switch (selection) {
        case SdfDelaySelection::Minimum:
            return 0U;
        case SdfDelaySelection::Typical:
            return 1U;
        case SdfDelaySelection::Maximum:
            return 2U;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool valid_decimal(
        const SdfExactDecimal& value, const std::size_t max_digits) noexcept
    {
        if (value.coefficient.empty() || value.coefficient.size() > max_digits)
            return false;
        for (const auto character : value.coefficient) {
            if (character < '0' || character > '9')
                return false;
        }
        return true;
    }

    [[nodiscard]] cpp_int decimal_coefficient(const std::string_view digits)
    {
        cpp_int value { };
        for (const auto character : digits) {
            value *= 10U;
            value += static_cast<unsigned>(character - '0');
        }
        return value;
    }

    [[nodiscard]] cpp_int power_of_ten(const std::size_t exponent)
    {
        cpp_int value { 1U };
        for (std::size_t index = 0; index < exponent; ++index)
            value *= 10U;
        return value;
    }

    [[nodiscard]] std::optional<std::int64_t> add_exponents(
        const std::int64_t left, const std::int64_t right) noexcept
    {
        if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
            || (right < 0
                && left < std::numeric_limits<std::int64_t>::min() - right)) {
            return std::nullopt;
        }
        return left + right;
    }

    [[nodiscard]] bool valid_policy(const SdfValuePolicy& policy) noexcept
    {
        return selection_index(policy.selection).has_value()
            && policy.design_time_unit_femtoseconds != 0U
            && policy.simulation_precision_femtoseconds != 0U
            && policy.design_time_unit_femtoseconds
            >= policy.simulation_precision_femtoseconds
            && policy.design_time_unit_femtoseconds
                % policy.simulation_precision_femtoseconds
            == 0U
            && policy.max_decimal_digits != 0U && policy.max_power10 != 0U;
    }

    [[nodiscard]] const SdfExactDecimal* selected_component(
        const SdfExactValue& value, const SdfDelaySelection selection)
    {
        if (value.kind == SdfExactValueKind::Scalar)
            return value.components[0] ? &*value.components[0] : nullptr;
        if (value.kind != SdfExactValueKind::Triple)
            return nullptr;
        const auto index = selection_index(selection);
        if (!index || !value.components[*index])
            return nullptr;
        return &*value.components[*index];
    }

    [[nodiscard]] std::string canonical_identity(
        const SdfDelaySelection selection, const SdfExactDecimal& source,
        const SdfNormalizedTimescale& timescale,
        const SdfValuePolicy& policy, const std::uint64_t ticks,
        const bool rounded_up)
    {
        return "sdf-delay-selection-v1|" + std::string { selection_name(selection) }
        + '|' + source.canonical + '|' + timescale.femtoseconds.canonical + '|'
            + std::to_string(policy.design_time_unit_femtoseconds) + '|'
            + std::to_string(policy.simulation_precision_femtoseconds) + '|'
            + std::to_string(ticks) + '|' + (rounded_up ? "up" : "down");
    }

    [[nodiscard]] std::string timing_check_identity(
        const SdfDelaySelection selection, const SdfExactDecimal& source,
        const SdfNormalizedTimescale& timescale,
        const SdfValuePolicy& policy, const std::int64_t ticks,
        const bool rounded_away)
    {
        return "sdf-timing-check-selection-v1|"
            + std::string { selection_name(selection) } + '|'
            + source.canonical + '|' + timescale.femtoseconds.canonical + '|'
            + std::to_string(policy.design_time_unit_femtoseconds) + '|'
            + std::to_string(policy.simulation_precision_femtoseconds) + '|'
            + std::to_string(ticks) + '|'
            + (rounded_away ? "away" : "toward");
    }

    [[nodiscard]] bool percentage_in_range(
        const SdfExactDecimal& source, const SdfValuePolicy& policy)
    {
        if (source.negative && source.coefficient != "0")
            return false;
        if (source.coefficient == "0")
            return true;
        if (source.exponent10 >= 0) {
            if (static_cast<std::uint64_t>(source.exponent10)
                > policy.max_power10) {
                return false;
            }
            return decimal_coefficient(source.coefficient)
                * power_of_ten(
                    static_cast<std::size_t>(source.exponent10))
                <= 100U;
        }
        if (source.exponent10 == std::numeric_limits<std::int64_t>::min()
            || static_cast<std::uint64_t>(-source.exponent10)
                > policy.max_power10) {
            return false;
        }
        return decimal_coefficient(source.coefficient)
            <= cpp_int { 100U }
            * power_of_ten(
                static_cast<std::size_t>(-source.exponent10));
    }
} // namespace

bool SdfValueSelectionResult::ok() const noexcept
{
    return selected.has_value() && error == SdfValuePolicyError::None;
}

bool SdfTimingCheckValueSelectionResult::ok() const noexcept
{
    return selected.has_value() && error == SdfValuePolicyError::None;
}

bool SdfPercentageSelectionResult::ok() const noexcept
{
    return selected.has_value() && error == SdfValuePolicyError::None;
}

SdfValueSelectionResult select_sdf_delay(const SdfExactValue& value,
    const SdfNormalizedTimescale& sdf_timescale,
    const SdfValuePolicy& policy, const SourceSpan& span)
{
    if (!valid_policy(policy)) {
        return failure(SdfValuePolicyError::InvalidInput,
            "FSIM-SDF-VALUE-001",
            "SDF delay selection has an invalid selection, design time unit, "
            "simulation precision, or exact-conversion resource policy",
            span);
    }
    if (value.kind != SdfExactValueKind::Scalar
        && value.kind != SdfExactValueKind::Triple) {
        return failure(SdfValuePolicyError::InvalidInput,
            "FSIM-SDF-VALUE-001",
            "SDF delay selection requires a scalar or min:typ:max exact value",
            span);
    }

    const auto* source = selected_component(value, policy.selection);
    if (source == nullptr) {
        return failure(SdfValuePolicyError::MissingSelection,
            "FSIM-SDF-VALUE-002",
            "SDF delay has no " + std::string { selection_name(policy.selection) }
                + " component",
            span);
    }
    const auto& scale = sdf_timescale.femtoseconds;
    if (!valid_decimal(*source, policy.max_decimal_digits)
        || !valid_decimal(scale, policy.max_decimal_digits)
        || scale.coefficient == "0"
        || source->coefficient.size() + scale.coefficient.size()
            > policy.max_decimal_digits
        || scale.negative) {
        return failure(SdfValuePolicyError::ExpansionLimit,
            "FSIM-SDF-VALUE-004",
            "SDF delay or timescale exceeds the exact decimal expansion limit",
            span);
    }
    if (source->negative && source->coefficient != "0") {
        return failure(SdfValuePolicyError::NegativeDelay,
            "FSIM-SDF-VALUE-003",
            "SDF delay selection produced a negative effective delay", span);
    }

    const auto exponent = add_exponents(source->exponent10, scale.exponent10);
    if (!exponent
        || (*exponent < 0
            && (*exponent == std::numeric_limits<std::int64_t>::min()
                || static_cast<std::uint64_t>(-*exponent)
                    > policy.max_power10))
        || (*exponent >= 0
            && static_cast<std::uint64_t>(*exponent) > policy.max_power10)) {
        return failure(SdfValuePolicyError::ExpansionLimit,
            "FSIM-SDF-VALUE-004",
            "SDF delay scaling exceeds the governed exact power-of-ten limit",
            span);
    }

    auto numerator = decimal_coefficient(source->coefficient)
        * decimal_coefficient(scale.coefficient);
    auto denominator = cpp_int { policy.simulation_precision_femtoseconds };
    if (*exponent >= 0) {
        numerator *= power_of_ten(static_cast<std::size_t>(*exponent));
    } else {
        denominator *= power_of_ten(static_cast<std::size_t>(-*exponent));
    }
    cpp_int quotient = numerator / denominator;
    const cpp_int remainder = numerator % denominator;
    const auto rounded_up = remainder * 2U >= denominator;
    quotient += static_cast<unsigned>(rounded_up);
    if (quotient > std::numeric_limits<std::uint64_t>::max()) {
        return failure(SdfValuePolicyError::TickOverflow,
            "FSIM-SDF-VALUE-005",
            "SDF delay selection exceeds the simulator tick range", span);
    }

    SdfSelectedDelay selected;
    selected.selection = policy.selection;
    selected.exact_source_value = *source;
    selected.exact_sdf_timescale_femtoseconds = scale;
    selected.design_time_unit_femtoseconds
        = policy.design_time_unit_femtoseconds;
    selected.simulation_precision_femtoseconds
        = policy.simulation_precision_femtoseconds;
    selected.ticks = quotient.convert_to<std::uint64_t>();
    selected.negative_zero = source->negative && source->coefficient == "0";
    selected.rounded_up = rounded_up;
    selected.canonical_identity = canonical_identity(policy.selection, *source,
        sdf_timescale, policy, selected.ticks, rounded_up);
    return { std::move(selected), SdfValuePolicyError::None, { } };
}

SdfTimingCheckValueSelectionResult select_sdf_timing_check_value(
    const SdfExactValue& value,
    const SdfNormalizedTimescale& sdf_timescale,
    const SdfValuePolicy& policy, const SourceSpan& span)
{
    if (!valid_policy(policy)) {
        return timing_failure(SdfValuePolicyError::InvalidInput,
            "FSIM-SDF-VALUE-001",
            "SDF timing-check selection has an invalid selection, design time unit, simulation precision, or exact-conversion resource policy",
            span);
    }
    if (value.kind != SdfExactValueKind::Scalar
        && value.kind != SdfExactValueKind::Triple) {
        return timing_failure(SdfValuePolicyError::InvalidInput,
            "FSIM-SDF-VALUE-001",
            "SDF timing-check selection requires a scalar or min:typ:max exact value",
            span);
    }
    const auto* source = selected_component(value, policy.selection);
    if (source == nullptr) {
        return timing_failure(SdfValuePolicyError::MissingSelection,
            "FSIM-SDF-VALUE-002",
            "SDF timing check has no "
                + std::string { selection_name(policy.selection) }
                + " component",
            span);
    }
    const auto& scale = sdf_timescale.femtoseconds;
    if (!valid_decimal(*source, policy.max_decimal_digits)
        || !valid_decimal(scale, policy.max_decimal_digits)
        || scale.coefficient == "0"
        || source->coefficient.size() + scale.coefficient.size()
            > policy.max_decimal_digits
        || scale.negative) {
        return timing_failure(SdfValuePolicyError::ExpansionLimit,
            "FSIM-SDF-VALUE-004",
            "SDF timing-check value or timescale exceeds the exact decimal expansion limit",
            span);
    }
    const auto exponent = add_exponents(source->exponent10, scale.exponent10);
    if (!exponent
        || (*exponent < 0
            && (*exponent == std::numeric_limits<std::int64_t>::min()
                || static_cast<std::uint64_t>(-*exponent)
                    > policy.max_power10))
        || (*exponent >= 0
            && static_cast<std::uint64_t>(*exponent) > policy.max_power10)) {
        return timing_failure(SdfValuePolicyError::ExpansionLimit,
            "FSIM-SDF-VALUE-004",
            "SDF timing-check scaling exceeds the governed exact power-of-ten limit",
            span);
    }
    auto numerator = decimal_coefficient(source->coefficient)
        * decimal_coefficient(scale.coefficient);
    auto denominator = cpp_int { policy.simulation_precision_femtoseconds };
    if (*exponent >= 0) {
        numerator *= power_of_ten(static_cast<std::size_t>(*exponent));
    } else {
        denominator *= power_of_ten(static_cast<std::size_t>(-*exponent));
    }
    cpp_int magnitude = numerator / denominator;
    const cpp_int remainder = numerator % denominator;
    const bool rounded_away = remainder * 2U >= denominator;
    magnitude += static_cast<unsigned>(rounded_away);
    const bool negative = source->negative && source->coefficient != "0";
    cpp_int maximum_magnitude { 1U };
    maximum_magnitude <<= 63U;
    const cpp_int positive_maximum
        = std::numeric_limits<std::int64_t>::max();
    if ((!negative && magnitude > positive_maximum)
        || (negative && magnitude > maximum_magnitude)) {
        return timing_failure(SdfValuePolicyError::TickOverflow,
            "FSIM-SDF-VALUE-005",
            "SDF timing-check selection exceeds the runtime signed tick range",
            span);
    }
    std::int64_t ticks { };
    if (negative && magnitude == maximum_magnitude) {
        ticks = std::numeric_limits<std::int64_t>::min();
    } else {
        ticks = magnitude.convert_to<std::int64_t>();
        if (negative)
            ticks = -ticks;
    }
    SdfSelectedTimingCheckValue selected;
    selected.selection = policy.selection;
    selected.exact_source_value = *source;
    selected.exact_sdf_timescale_femtoseconds = scale;
    selected.design_time_unit_femtoseconds
        = policy.design_time_unit_femtoseconds;
    selected.simulation_precision_femtoseconds
        = policy.simulation_precision_femtoseconds;
    selected.ticks = ticks;
    selected.negative_zero = source->negative && source->coefficient == "0";
    selected.rounded_away_from_zero = rounded_away;
    selected.canonical_identity = timing_check_identity(policy.selection,
        *source, sdf_timescale, policy, ticks, rounded_away);
    return { std::move(selected), SdfValuePolicyError::None, { } };
}

SdfPercentageSelectionResult select_sdf_percentage(
    const SdfExactValue& value, const SdfValuePolicy& policy,
    const SourceSpan& span)
{
    if (!valid_policy(policy)) {
        return percentage_failure(SdfValuePolicyError::InvalidInput,
            "FSIM-SDF-VALUE-001",
            "SDF percentage selection has an invalid value-selection or resource policy",
            span);
    }
    if (value.kind != SdfExactValueKind::Scalar
        && value.kind != SdfExactValueKind::Triple) {
        return percentage_failure(SdfValuePolicyError::InvalidInput,
            "FSIM-SDF-VALUE-001",
            "SDF percentage selection requires a scalar or min:typ:max exact value",
            span);
    }
    const auto* source = selected_component(value, policy.selection);
    if (source == nullptr) {
        return percentage_failure(SdfValuePolicyError::MissingSelection,
            "FSIM-SDF-VALUE-002",
            "SDF percentage has no "
                + std::string { selection_name(policy.selection) }
                + " component",
            span);
    }
    if (!valid_decimal(*source, policy.max_decimal_digits)
        || !percentage_in_range(*source, policy)) {
        return percentage_failure(SdfValuePolicyError::InvalidInput,
            "FSIM-SDF-VALUE-001",
            "SDF percentage is malformed, outside 0 through 100, or exceeds its exact expansion policy",
            span);
    }
    SdfSelectedPercentage selected;
    selected.selection = policy.selection;
    selected.exact_source_value = *source;
    selected.negative_zero = source->negative && source->coefficient == "0";
    selected.canonical_identity = "sdf-percentage-selection-v1|"
        + std::string { selection_name(policy.selection) } + '|'
        + source->canonical;
    return { std::move(selected), SdfValuePolicyError::None, { } };
}

} // namespace fsim::app

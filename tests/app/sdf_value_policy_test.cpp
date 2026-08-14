// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_value_policy.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

fsim::frontend::SdfExactDecimal decimal(std::string coefficient,
    const std::int64_t exponent, const bool negative = false)
{
    fsim::frontend::SdfExactDecimal value;
    value.negative = negative;
    value.coefficient = std::move(coefficient);
    value.exponent10 = exponent;
    value.canonical = std::string { negative ? "-" : "" }
        + value.coefficient + 'e' + std::to_string(exponent);
    return value;
}

fsim::frontend::SdfExactValue scalar(
    fsim::frontend::SdfExactDecimal value)
{
    fsim::frontend::SdfExactValue result;
    result.kind = fsim::frontend::SdfExactValueKind::Scalar;
    result.components[0] = std::move(value);
    return result;
}

fsim::frontend::SdfExactValue triple(
    std::optional<fsim::frontend::SdfExactDecimal> minimum,
    std::optional<fsim::frontend::SdfExactDecimal> typical,
    std::optional<fsim::frontend::SdfExactDecimal> maximum)
{
    fsim::frontend::SdfExactValue result;
    result.kind = fsim::frontend::SdfExactValueKind::Triple;
    result.components = { std::move(minimum), std::move(typical),
        std::move(maximum) };
    return result;
}

fsim::frontend::SdfNormalizedTimescale nanoseconds()
{
    fsim::frontend::SdfNormalizedTimescale result;
    result.unit = fsim::frontend::SdfTimeUnit::Nanosecond;
    result.magnitude = decimal("1", 0);
    result.femtoseconds = decimal("1", 6);
    result.canonical = "1ns";
    return result;
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfValueSelectionResult& result,
    const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error("missing value-policy diagnostic "
            + std::string { code });
    return *found;
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfTimingCheckValueSelectionResult& result,
    const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error("missing timing-check value diagnostic "
            + std::string { code });
    return *found;
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfPercentageSelectionResult& result,
    const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error("missing percentage diagnostic "
            + std::string { code });
    return *found;
}

void test_scalar_selection_and_single_rounding()
{
    fsim::app::SdfValuePolicy policy;
    policy.design_time_unit_femtoseconds = 1'000'000U;
    policy.simulation_precision_femtoseconds = 1'000U;
    for (const auto selection : std::to_array({ fsim::app::SdfDelaySelection::Minimum,
             fsim::app::SdfDelaySelection::Typical,
             fsim::app::SdfDelaySelection::Maximum })) {
        policy.selection = selection;
        const auto result = fsim::app::select_sdf_delay(
            scalar(decimal("125", -2)), nanoseconds(), policy);
        require(result.ok(), "scalar selection should succeed for every mode");
        require(result.selected->ticks == 1'250U,
            "1.25 ns should select exactly 1250 ps ticks");
        require(!result.selected->rounded_up,
            "exact scalar conversion should not round upward");
        require(result.selected->canonical_identity.find("sdf-delay-selection-v1")
                == 0U,
            "selected delay should publish a versioned identity");
    }

    policy.simulation_precision_femtoseconds = 1U;
    const auto half = fsim::app::select_sdf_delay(
        scalar(decimal("5", -7)), nanoseconds(), policy);
    require(half.ok() && half.selected->ticks == 1U
            && half.selected->rounded_up,
        "half-femtosecond conversion should round upward exactly once");
}

void test_partial_triple_selection()
{
    const auto value = triple(decimal("1", 0), std::nullopt, decimal("3", 0));
    fsim::app::SdfValuePolicy policy;
    policy.selection = fsim::app::SdfDelaySelection::Minimum;
    auto result = fsim::app::select_sdf_delay(value, nanoseconds(), policy);
    require(result.ok() && result.selected->ticks == 1'000U,
        "minimum triple component should select independently");

    policy.selection = fsim::app::SdfDelaySelection::Maximum;
    result = fsim::app::select_sdf_delay(value, nanoseconds(), policy);
    require(result.ok() && result.selected->ticks == 3'000U,
        "maximum triple component should select independently");

    policy.selection = fsim::app::SdfDelaySelection::Typical;
    fsim::frontend::SourceSpan span;
    span.source_name = "partial.sdf";
    span.begin = { 7U, 2U, 3U };
    span.end = { 8U, 2U, 4U };
    result = fsim::app::select_sdf_delay(
        value, nanoseconds(), policy, span);
    require(!result.ok()
            && result.error == fsim::app::SdfValuePolicyError::MissingSelection,
        "missing typical component should reject the annotation");
    const auto& diagnostic = require_diagnostic(result, "FSIM-SDF-VALUE-002");
    require(diagnostic.span.begin.offset == 7U
            && diagnostic.span.begin.line == 2U,
        "missing component diagnostic should retain the SDF coordinate");
}

void test_negative_zero_and_negative_rejection()
{
    fsim::app::SdfValuePolicy policy;
    auto result = fsim::app::select_sdf_delay(
        scalar(decimal("0", -999, true)), nanoseconds(), policy);
    require(result.ok() && result.selected->ticks == 0U
            && result.selected->negative_zero,
        "negative zero should retain identity while selecting zero ticks");

    result = fsim::app::select_sdf_delay(
        scalar(decimal("1", 0, true)), nanoseconds(), policy);
    require(!result.ok()
            && result.error == fsim::app::SdfValuePolicyError::NegativeDelay,
        "negative effective delays must reject");
    require_diagnostic(result, "FSIM-SDF-VALUE-003");
}

void test_signed_timing_check_selection()
{
    auto policy = fsim::app::SdfValuePolicy { };
    auto result = fsim::app::select_sdf_timing_check_value(
        scalar(decimal("125", -2, true)), nanoseconds(), policy);
    require(result.ok() && result.selected->ticks == -1'250
            && !result.selected->rounded_away_from_zero,
        "negative timing-check limits must retain their sign and exact scale");
    require(result.selected->canonical_identity.find(
                "sdf-timing-check-selection-v1")
            == 0U,
        "signed timing-check values must publish a distinct identity");

    policy.design_time_unit_femtoseconds = 1U;
    policy.simulation_precision_femtoseconds = 1U;
    auto unit_scale = nanoseconds();
    unit_scale.femtoseconds = decimal("1", 0);
    unit_scale.canonical = "1fs";
    result = fsim::app::select_sdf_timing_check_value(
        scalar(decimal("5", -1, true)), unit_scale, policy);
    require(result.ok() && result.selected->ticks == -1
            && result.selected->rounded_away_from_zero,
        "negative half ticks must round once away from zero");

    result = fsim::app::select_sdf_timing_check_value(
        scalar(decimal("0", -999, true)), unit_scale, policy);
    require(result.ok() && result.selected->ticks == 0
            && result.selected->negative_zero,
        "signed selection must retain negative-zero identity");

    result = fsim::app::select_sdf_timing_check_value(
        scalar(decimal("9223372036854775808", 0, true)), unit_scale, policy);
    require(result.ok()
            && result.selected->ticks == std::numeric_limits<std::int64_t>::min(),
        "the exact signed minimum timing-check tick must remain representable");
    result = fsim::app::select_sdf_timing_check_value(
        scalar(decimal("9223372036854775808", 0)), unit_scale, policy);
    require(!result.ok()
            && result.error == fsim::app::SdfValuePolicyError::TickOverflow,
        "positive signed-range overflow must reject before narrowing");
    require_diagnostic(result, "FSIM-SDF-VALUE-005");
}

void test_exact_percentage_selection()
{
    auto policy = fsim::app::SdfValuePolicy { };
    const auto values = triple(
        decimal("125", -1), decimal("50", 0), decimal("100", 0));
    policy.selection = fsim::app::SdfDelaySelection::Minimum;
    auto result = fsim::app::select_sdf_percentage(values, policy);
    require(result.ok()
            && result.selected->exact_source_value.canonical == "125e-1",
        "minimum percentage component should retain exact decimal identity");
    policy.selection = fsim::app::SdfDelaySelection::Typical;
    result = fsim::app::select_sdf_percentage(values, policy);
    require(result.ok()
            && result.selected->exact_source_value.canonical == "50e0",
        "typical percentage component should select independently");
    policy.selection = fsim::app::SdfDelaySelection::Maximum;
    result = fsim::app::select_sdf_percentage(values, policy);
    require(result.ok()
            && result.selected->exact_source_value.canonical == "100e0",
        "the inclusive 100 percent boundary should be accepted");

    result = fsim::app::select_sdf_percentage(
        scalar(decimal("0", -999, true)), policy);
    require(result.ok() && result.selected->negative_zero,
        "percentage selection should preserve negative-zero identity");
    require(result.selected->canonical_identity.find(
                "sdf-percentage-selection-v1")
            == 0U,
        "percentage selection should publish a versioned identity");

    result = fsim::app::select_sdf_percentage(
        scalar(decimal("1", 0, true)), policy);
    require(!result.ok()
            && result.error == fsim::app::SdfValuePolicyError::InvalidInput,
        "negative percentages must reject");
    require_diagnostic(result, "FSIM-SDF-VALUE-001");
    result = fsim::app::select_sdf_percentage(
        scalar(decimal("1000001", -4)), policy);
    require(!result.ok(), "percentages above 100 must reject exactly");
    require_diagnostic(result, "FSIM-SDF-VALUE-001");

    policy.selection = fsim::app::SdfDelaySelection::Typical;
    result = fsim::app::select_sdf_percentage(
        triple(decimal("1", 0), std::nullopt, decimal("2", 0)), policy);
    require(!result.ok()
            && result.error
                == fsim::app::SdfValuePolicyError::MissingSelection,
        "a missing selected percentage component must reject");
    require_diagnostic(result, "FSIM-SDF-VALUE-002");

    policy = fsim::app::SdfValuePolicy { };
    policy.max_power10 = 2U;
    result = fsim::app::select_sdf_percentage(
        scalar(decimal("1", -3)), policy);
    require(!result.ok(),
        "percentage expansion beyond the governed power must reject");
    require_diagnostic(result, "FSIM-SDF-VALUE-001");
}

void test_policy_resource_and_overflow_rejection()
{
    auto policy = fsim::app::SdfValuePolicy { };
    policy.simulation_precision_femtoseconds = 0U;
    auto result = fsim::app::select_sdf_delay(
        scalar(decimal("1", 0)), nanoseconds(), policy);
    require(!result.ok()
            && result.error == fsim::app::SdfValuePolicyError::InvalidInput,
        "zero simulation precision must reject");
    require_diagnostic(result, "FSIM-SDF-VALUE-001");

    policy = fsim::app::SdfValuePolicy { };
    policy.max_power10 = 2U;
    result = fsim::app::select_sdf_delay(
        scalar(decimal("1", 100)), nanoseconds(), policy);
    require(!result.ok()
            && result.error == fsim::app::SdfValuePolicyError::ExpansionLimit,
        "governed power-of-ten expansion must reject deterministically");
    require_diagnostic(result, "FSIM-SDF-VALUE-004");

    policy = fsim::app::SdfValuePolicy { };
    result = fsim::app::select_sdf_delay(
        scalar(decimal(std::to_string(
                           std::numeric_limits<std::uint64_t>::max()),
            0)),
        nanoseconds(), policy);
    require(!result.ok()
            && result.error == fsim::app::SdfValuePolicyError::TickOverflow,
        "tick overflow must reject without narrowing");
    require_diagnostic(result, "FSIM-SDF-VALUE-005");

    auto malformed_scale = nanoseconds();
    malformed_scale.femtoseconds.coefficient = "1x";
    result = fsim::app::select_sdf_delay(
        scalar(decimal("1", 0)), malformed_scale, policy);
    require(!result.ok()
            && result.error == fsim::app::SdfValuePolicyError::ExpansionLimit,
        "noncanonical exact input must reject before arithmetic");

    malformed_scale = nanoseconds();
    malformed_scale.femtoseconds.coefficient = "0";
    result = fsim::app::select_sdf_delay(
        scalar(decimal("1", 0)), malformed_scale, policy);
    require(!result.ok()
            && result.error == fsim::app::SdfValuePolicyError::ExpansionLimit,
        "zero SDF timescale must reject before arithmetic");
}
} // namespace

int main()
{
    try {
        test_scalar_selection_and_single_rounding();
        test_partial_triple_selection();
        test_negative_zero_and_negative_rejection();
        test_signed_timing_check_selection();
        test_exact_percentage_selection();
        test_policy_resource_and_overflow_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}

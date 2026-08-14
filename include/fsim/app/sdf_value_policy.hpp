// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/diagnostic.hpp"
#include "fsim/frontend/sdf.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fsim::app {

enum class SdfDelaySelection {
    Minimum,
    Typical,
    Maximum,
};

enum class SdfValuePolicyError {
    None,
    InvalidInput,
    MissingSelection,
    NegativeDelay,
    ExpansionLimit,
    TickOverflow,
};

struct SdfValuePolicy {
    SdfDelaySelection selection { SdfDelaySelection::Typical };
    std::uint64_t design_time_unit_femtoseconds { 1'000'000U };
    std::uint64_t simulation_precision_femtoseconds { 1'000U };
    std::size_t max_decimal_digits { 8'192U };
    std::size_t max_power10 { 4'096U };
};

struct SdfSelectedDelay {
    SdfDelaySelection selection { SdfDelaySelection::Typical };
    frontend::SdfExactDecimal exact_source_value;
    frontend::SdfExactDecimal exact_sdf_timescale_femtoseconds;
    std::uint64_t design_time_unit_femtoseconds { };
    std::uint64_t simulation_precision_femtoseconds { };
    std::uint64_t ticks { };
    bool negative_zero { };
    bool rounded_up { };
    std::string canonical_identity;

    friend bool operator==(const SdfSelectedDelay&,
        const SdfSelectedDelay&) = default;
};

struct SdfValueSelectionResult {
    std::optional<SdfSelectedDelay> selected;
    SdfValuePolicyError error { SdfValuePolicyError::None };
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

struct SdfSelectedTimingCheckValue {
    SdfDelaySelection selection { SdfDelaySelection::Typical };
    frontend::SdfExactDecimal exact_source_value;
    frontend::SdfExactDecimal exact_sdf_timescale_femtoseconds;
    std::uint64_t design_time_unit_femtoseconds { };
    std::uint64_t simulation_precision_femtoseconds { };
    std::int64_t ticks { };
    bool negative_zero { };
    bool rounded_away_from_zero { };
    std::string canonical_identity;

    friend bool operator==(const SdfSelectedTimingCheckValue&,
        const SdfSelectedTimingCheckValue&) = default;
};

struct SdfTimingCheckValueSelectionResult {
    std::optional<SdfSelectedTimingCheckValue> selected;
    SdfValuePolicyError error { SdfValuePolicyError::None };
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

struct SdfSelectedPercentage {
    SdfDelaySelection selection { SdfDelaySelection::Typical };
    frontend::SdfExactDecimal exact_source_value;
    bool negative_zero { };
    std::string canonical_identity;

    friend bool operator==(const SdfSelectedPercentage&,
        const SdfSelectedPercentage&) = default;
};

struct SdfPercentageSelectionResult {
    std::optional<SdfSelectedPercentage> selected;
    SdfValuePolicyError error { SdfValuePolicyError::None };
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfValueSelectionResult select_sdf_delay(
    const frontend::SdfExactValue& value,
    const frontend::SdfNormalizedTimescale& sdf_timescale,
    const SdfValuePolicy& policy, const frontend::SourceSpan& span = { });

[[nodiscard]] SdfTimingCheckValueSelectionResult
select_sdf_timing_check_value(const frontend::SdfExactValue& value,
    const frontend::SdfNormalizedTimescale& sdf_timescale,
    const SdfValuePolicy& policy, const frontend::SourceSpan& span = { });

[[nodiscard]] SdfPercentageSelectionResult select_sdf_percentage(
    const frontend::SdfExactValue& value, const SdfValuePolicy& policy,
    const frontend::SourceSpan& span = { });

} // namespace fsim::app

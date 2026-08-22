// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/code_coverage.hpp"
#include "fsim/runtime/simir.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fsim::runtime::simir {

inline constexpr std::string_view kSimIRCoverageDiagnostic = "FSIM-COV-009";
inline constexpr std::string_view kCodeCoverageCounterDiagnostic
    = "FSIM-COV-010";

struct CodeCoverageHitLimits {
    std::size_t maximum_points { 1U << 20U };
    std::size_t maximum_operations { 1U << 24U };
};

enum class CodeCoverageHitError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidPointIdentity,
    InvalidMetric,
    CounterOutOfRange,
    CounterOwnershipMismatch,
    PointOwnershipMismatch,
    DuplicatePointOwnership,
    NoncanonicalPointOwnership,
    DuplicateHit,
};

struct CodeCoverageHitValidationResult {
    CodeCoverageHitError error { CodeCoverageHitError::None };
    std::size_t instruction { };
    std::size_t owner { };

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return error == CodeCoverageHitError::None;
    }
};

enum class CodeCoverageCounterUpdate : std::uint8_t {
    Unavailable,
    OutOfRange,
    Incremented,
    FirstOverflow,
    Saturated,
};

/// Simulation-owned reference counters. Saturation is sticky and the first
/// overflow of each counter is separately observable without allocating on
/// the execution path.
class CodeCoverageCounters {
public:
    void reset(
        std::vector<std::uint64_t> values,
        std::size_t maximum_points = 1U << 20U);
    [[nodiscard]] CodeCoverageCounterUpdate record(
        ::fsim::runtime::CodeCoverageCounterId counter) noexcept;
    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] std::span<const std::uint64_t> values() const noexcept;
    [[nodiscard]] std::span<std::uint64_t> mutable_values() noexcept;
    [[nodiscard]] bool overflowed(
        ::fsim::runtime::CodeCoverageCounterId counter) const noexcept;
    [[nodiscard]] std::size_t overflow_count() const noexcept;

private:
    std::vector<std::uint64_t> values_;
    std::vector<std::uint8_t> overflowed_;
    std::size_t overflow_count_ { };
    bool configured_ { };
};

/// Validate one typed hit against its exact elaborated point owner.
[[nodiscard]] CodeCoverageHitValidationResult validate_code_coverage_hit(
    const CodeCoverageHit& hit,
    const ::fsim::runtime::CodeCoveragePoint& owner,
    std::size_t counter_count) noexcept;

/// Validate every hit in a process against one instance's canonical, dense
/// point inventory. Validation is read-only and publishes no partial state.
[[nodiscard]] CodeCoverageHitValidationResult validate_code_coverage_hits(
    const Process& process,
    std::span<const ::fsim::runtime::CodeCoveragePoint> owners,
    std::size_t counter_count,
    CodeCoverageHitLimits limits = { }) noexcept;

} // namespace fsim::runtime::simir

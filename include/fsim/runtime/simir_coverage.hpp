// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/code_coverage.hpp"
#include "fsim/runtime/simir.hpp"

#include <algorithm>
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
    Ignored,
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
    [[nodiscard]] bool enabled() const noexcept;
    void set_enabled(bool enabled) noexcept;
    [[nodiscard]] bool set_enabled(
        std::span<const ::fsim::runtime::CodeCoverageCounterId> counters,
        bool enabled) noexcept;
    void clear() noexcept;
    [[nodiscard]] bool clear(
        std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
        noexcept;
    [[nodiscard]] std::span<const std::uint64_t> values() const noexcept;
    [[nodiscard]] std::span<std::uint64_t> mutable_values() noexcept;
    [[nodiscard]] std::span<std::uint64_t> direct_values() noexcept;
    [[nodiscard]] bool overflowed(
        ::fsim::runtime::CodeCoverageCounterId counter) const noexcept;
    [[nodiscard]] std::size_t overflow_count() const noexcept;
    /// The per-counter saturation and enable flags, for the run-state image
    /// (#128): a counter that saturated stays flagged after a restart, and a
    /// counter switched off stays off.
    [[nodiscard]] std::span<const std::uint8_t> overflow_flags() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> enabled_flags() const noexcept;
    /// Puts saved flags back; false, changing nothing, when a size differs
    /// from the counter table.
    [[nodiscard]] bool restore_flags(std::span<const std::uint8_t> overflowed,
        std::span<const std::uint8_t> enabled) noexcept;
    [[nodiscard]] std::optional<std::size_t> overflow_count(
        std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
        const noexcept;

private:
    std::vector<std::uint64_t> values_;
    std::vector<std::uint8_t> overflowed_;
    std::vector<std::uint8_t> enabled_counters_;
    std::size_t overflow_count_ { };
    bool configured_ { };
    bool enabled_ { true };
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

/// One bit of a toggle sample: 0, 1, or unknown (2). A STD_ULOGIC bit
/// reads through TO_X01, so L is 0 and H is 1.
[[nodiscard]] inline std::uint8_t code_coverage_toggle_bit(
    const PackedLogic4& value, const std::size_t index)
{
    if (value.is_logic9()) {
        switch (value.get_logic9(index)) {
        case Logic9::zero:
        case Logic9::l:
            return 0U;
        case Logic9::one:
        case Logic9::h:
            return 1U;
        default:
            return 2U;
        }
    }
    const auto bit = value.get(index);
    return bit == Logic4::zero ? 0U : bit == Logic4::one ? 1U : 2U;
}

/// Calls `record(counter)` for each bit that went from a known 0 to a known
/// 1 (its rise counter) or from a known 1 to a known 0 (its fall counter).
/// A transition to or from an unknown value counts in neither direction.
template <typename Record>
void record_code_coverage_toggles(const PackedLogic4& previous,
    const PackedLogic4& current, const CodeCoverageToggle& toggle,
    Record&& record)
{
    if (previous == current) {
        return;
    }
    const auto width = std::min({ previous.width(), current.width(),
        toggle.counters.size() / 2U });
    for (std::size_t bit = 0U; bit < width; ++bit) {
        const auto before = code_coverage_toggle_bit(previous, bit);
        const auto after = code_coverage_toggle_bit(current, bit);
        if (before == 0U && after == 1U) {
            record(toggle.counters[2U * bit]);
        } else if (before == 1U && after == 0U) {
            record(toggle.counters[2U * bit + 1U]);
        }
    }
}

} // namespace fsim::runtime::simir

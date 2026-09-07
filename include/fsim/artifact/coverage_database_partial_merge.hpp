// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_database_merge.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace fsim::artifact {

inline constexpr std::string_view kCoverageDatabasePartialMergeDiagnostic
    = "FSIM-COV-037";

struct CoverageDatabasePartialMergeStatistics {
    std::size_t unchanged_source_matches { };
    std::size_t retained_runs { };
    std::size_t omitted_runs { };
    std::size_t retained_metrics { };
    std::size_t omitted_metrics { };
    std::size_t omitted_exclusions { };
};

struct CoverageDatabasePartialMergeLimits {
    CoverageDatabaseMergeLimits merge;
    std::size_t maximum_historical_sources { 1U << 22U };
    std::size_t maximum_historical_runs { 1U << 20U };
    std::size_t maximum_historical_metrics { 1U << 26U };
    std::size_t maximum_historical_exclusions { 1U << 24U };
};

struct CoverageDatabasePartialMergeResult {
    std::optional<CoverageDatabaseContents> contents;
    CoverageDatabasePartialMergeStatistics statistics;
    CoverageDatabaseMergeError error { CoverageDatabaseMergeError::None };
    CoverageDatabaseModelError model_error {
        CoverageDatabaseModelError::None
    };
    std::size_t input_index { };
    std::size_t record_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return contents.has_value()
            && error == CoverageDatabaseMergeError::None;
    }
};

// Explicit partial merge keeps the target fingerprint, source inventory,
// point inventory, and exclusion policy. Historical records survive only when
// their exact source record and language-neutral point key still exist.
[[nodiscard]] CoverageDatabasePartialMergeResult
merge_coverage_databases_partially(
    const CoverageDatabaseContents& target,
    std::span<const CoverageDatabaseContents> history,
    const CoverageDatabasePartialMergeLimits& limits = { }) noexcept;

} // namespace fsim::artifact

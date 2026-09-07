// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_database_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::artifact {

inline constexpr std::string_view kCoverageExclusionReportDiagnostic
    = "FSIM-COV-043";

struct CoverageExclusionReportPoint {
    CoverageDatabaseNamespace name_space { CoverageDatabaseNamespace::Code };
    CoverageDatabaseMetricFamily family {
        CoverageDatabaseMetricFamily::Statement
    };
    CoverageDatabaseMetricScope scope { CoverageDatabaseMetricScope::Source };
    CoverageDatabaseIdentity point_identity;
    CoverageDatabaseIdentity source_identity;
    CoverageDatabaseIdentity instance_identity;
    std::vector<std::string> reasons;
    std::uint64_t source_line { };

    friend bool operator==(const CoverageExclusionReportPoint&,
        const CoverageExclusionReportPoint&)
        = default;
};

struct CoverageExclusionReport {
    std::vector<CoverageExclusionReportPoint> points;
    std::size_t total_reasons { };

    friend bool operator==(
        const CoverageExclusionReport&, const CoverageExclusionReport&)
        = default;
};

struct CoverageExclusionReportLimits {
    std::size_t maximum_points { 1U << 24U };
    std::size_t maximum_reasons { 1U << 24U };
    std::size_t maximum_reason_bytes { 1U << 20U };
    std::size_t maximum_total_reason_bytes { 1U << 30U };
    CoverageDatabaseModelLimits database;
};

enum class CoverageExclusionReportError : std::uint8_t {
    None,
    InvalidDatabase,
    ResourceLimit,
    AllocationFailure,
};

struct CoverageExclusionReportResult {
    std::optional<CoverageExclusionReport> report;
    CoverageExclusionReportError error { CoverageExclusionReportError::None };
    CoverageDatabaseModelError database_error {
        CoverageDatabaseModelError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return report.has_value() && error == CoverageExclusionReportError::None;
    }
};

[[nodiscard]] CoverageExclusionReportResult make_coverage_exclusion_report(
    const CoverageDatabaseContents& contents,
    CoverageExclusionReportLimits limits = { }) noexcept;

} // namespace fsim::artifact

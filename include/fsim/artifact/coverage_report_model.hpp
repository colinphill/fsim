// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_exclusion_report.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::artifact {

inline constexpr std::string_view kCoverageReportModelDiagnostic
    = "FSIM-COV-044";

enum class CoverageReportPointStatus : std::uint8_t {
    Covered,
    Uncovered,
    Excluded,
};

struct CoverageReportPoint {
    CoverageDatabaseNamespace name_space { CoverageDatabaseNamespace::Code };
    CoverageDatabaseMetricFamily family {
        CoverageDatabaseMetricFamily::Statement
    };
    CoverageDatabaseIdentity point_identity;
    CoverageDatabaseIdentity source_identity;
    CoverageDatabaseIdentity instance_identity;
    std::uint64_t hits { };
    std::uint64_t excluded_hits { };
    bool hits_saturated { };
    bool excluded_hits_saturated { };
    CoverageReportPointStatus status { CoverageReportPointStatus::Uncovered };
    std::uint64_t source_line { };

    friend bool operator==(const CoverageReportPoint&,
        const CoverageReportPoint&)
        = default;
};

struct CoverageReportMetric {
    CoverageDatabaseNamespace name_space { CoverageDatabaseNamespace::Code };
    CoverageDatabaseMetricFamily family {
        CoverageDatabaseMetricFamily::Statement
    };
    std::uint64_t total { };
    std::uint64_t covered { };
    std::uint64_t uncovered { };
    std::uint64_t excluded { };
    std::uint64_t hits { };
    std::uint64_t excluded_hits { };
    bool hits_saturated { };
    bool excluded_hits_saturated { };

    friend bool operator==(const CoverageReportMetric&,
        const CoverageReportMetric&)
        = default;
};

struct CoverageSourceReport {
    CoverageDatabaseIdentity source_identity;
    std::string logical_path;
    std::vector<CoverageReportPoint> points;
    std::vector<CoverageReportMetric> metrics;

    friend bool operator==(const CoverageSourceReport&,
        const CoverageSourceReport&)
        = default;
};

struct CoverageInstanceReport {
    CoverageDatabaseIdentity instance_identity;
    std::vector<CoverageReportPoint> points;
    std::vector<CoverageReportMetric> metrics;

    friend bool operator==(const CoverageInstanceReport&,
        const CoverageInstanceReport&)
        = default;
};

struct CoverageCombinedReport {
    // Per-family summaries over source-union points. Deliberately no overall
    // percentage or synthetic grand score exists.
    std::vector<CoverageReportMetric> metrics;

    friend bool operator==(const CoverageCombinedReport&,
        const CoverageCombinedReport&)
        = default;
};

struct CoverageReportModel {
    std::vector<CoverageSourceReport> sources;
    std::vector<CoverageInstanceReport> instances;
    CoverageCombinedReport combined;
    CoverageExclusionReport exclusions;

    friend bool operator==(const CoverageReportModel&, const CoverageReportModel&)
        = default;
};

struct CoverageReportModelLimits {
    std::size_t maximum_exact_points { 1U << 24U };
    std::size_t maximum_source_points { 1U << 24U };
    std::size_t maximum_instance_points { 1U << 24U };
    std::size_t maximum_instances { 1U << 20U };
    CoverageDatabaseModelLimits database;
    CoverageExclusionReportLimits exclusions;
};

enum class CoverageReportModelError : std::uint8_t {
    None,
    InvalidDatabase,
    InvalidExclusionReport,
    ResourceLimit,
    AllocationFailure,
};

struct CoverageReportModelResult {
    std::optional<CoverageReportModel> report;
    CoverageReportModelError error { CoverageReportModelError::None };
    CoverageDatabaseModelError database_error {
        CoverageDatabaseModelError::None
    };
    CoverageExclusionReportError exclusion_error {
        CoverageExclusionReportError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return report.has_value() && error == CoverageReportModelError::None;
    }
};

[[nodiscard]] CoverageReportModelResult make_coverage_report_model(
    const CoverageDatabaseContents& contents,
    CoverageReportModelLimits limits = { }) noexcept;

} // namespace fsim::artifact

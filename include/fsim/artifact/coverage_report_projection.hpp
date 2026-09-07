// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_report_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::artifact {

inline constexpr std::string_view kCoverageReportProjectionDiagnostic
    = "FSIM-COV-046";

enum class CoverageReportProjectionFormat : std::uint8_t {
    Lcov = 1U,
    Cobertura = 2U,
};

struct CoverageReportProjectionLimits {
    std::size_t maximum_lines { 1U << 24U };
    std::size_t maximum_branches { 1U << 24U };
    std::size_t maximum_output_bytes { 1U << 30U };
    CoverageReportModelLimits model;
};

enum class CoverageReportProjectionError : std::uint8_t {
    None,
    InvalidFormat,
    InvalidReportModel,
    InvalidSourcePath,
    MissingSourceLine,
    ResourceLimit,
    AllocationFailure,
};

struct CoverageReportProjectionResult {
    std::optional<std::string> output;
    CoverageReportProjectionError error {
        CoverageReportProjectionError::None
    };
    CoverageReportModelError model_error { CoverageReportModelError::None };
    CoverageDatabaseModelError database_error {
        CoverageDatabaseModelError::None
    };
    CoverageExclusionReportError exclusion_error {
        CoverageExclusionReportError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return output.has_value()
            && error == CoverageReportProjectionError::None;
    }
};

[[nodiscard]] CoverageReportProjectionResult project_coverage_report(
    const CoverageDatabaseContents& contents,
    CoverageReportProjectionFormat format,
    CoverageReportProjectionLimits limits = { }) noexcept;

} // namespace fsim::artifact

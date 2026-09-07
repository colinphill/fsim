// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_report_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::artifact {

inline constexpr std::string_view kCoverageReportRenderDiagnostic
    = "FSIM-COV-045";
inline constexpr std::string_view kCoverageReportJsonSchema
    = "fsim-coverage-report-v3";

enum class CoverageReportFormat : std::uint8_t {
    Text = 1U,
    Html = 2U,
    Json = 3U,
};

struct CoverageReportRenderLimits {
    std::size_t maximum_output_bytes { 1U << 30U };
    CoverageReportModelLimits model;
};

enum class CoverageReportRenderError : std::uint8_t {
    None,
    InvalidReportFormat,
    InvalidReportModel,
    ResourceLimit,
    AllocationFailure,
};

struct CoverageReportRenderResult {
    std::optional<std::string> output;
    CoverageReportRenderError error { CoverageReportRenderError::None };
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
        return output.has_value() && error == CoverageReportRenderError::None;
    }
};

[[nodiscard]] CoverageReportRenderResult render_coverage_report(
    const CoverageDatabaseContents& contents, CoverageReportFormat format,
    CoverageReportRenderLimits limits = { }) noexcept;

} // namespace fsim::artifact

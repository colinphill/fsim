// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_database.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::artifact {

inline constexpr std::string_view kCoverageDatabaseModel
    = "fsim-unified-coverage-database-v3";
inline constexpr std::string_view kCoverageDatabaseModelDiagnostic
    = "FSIM-COV-032";

struct CoverageDatabaseIdentity {
    std::uint64_t high { };
    std::uint64_t low { };

    friend auto operator<=>(
        const CoverageDatabaseIdentity&, const CoverageDatabaseIdentity&)
        = default;
};

enum class CoverageDatabaseRunStatus : std::uint8_t {
    Complete = 1U,
    Stopped = 2U,
    Failed = 3U,
};

enum class CoverageDatabaseMetricFamily : std::uint8_t {
    Statement = 1U,
    Branch = 2U,
    Line = 3U,
    Condition = 4U,
    Expression = 5U,
    Toggle = 6U,
    FsmState = 7U,
    FsmTransition = 8U,
    SystemVerilogCoverpoint = 9U,
    SystemVerilogCross = 10U,
    PslDirective = 11U,
    PslProperty = 12U,
};

enum class CoverageDatabaseMetricScope : std::uint8_t {
    Source = 1U,
    Instance = 2U,
};

struct CoverageDatabaseModelFingerprint {
    std::uint32_t schema { kCoverageDatabaseSchema };
    std::string model { kCoverageDatabaseModel };
    CoverageDatabaseDigest digest { };

    friend bool operator==(
        const CoverageDatabaseModelFingerprint&,
        const CoverageDatabaseModelFingerprint&)
        = default;
};

struct CoverageDatabaseSourceRecord {
    CoverageDatabaseIdentity identity;
    std::string logical_path;
    std::uint64_t content_bytes { };
    CoverageDatabaseDigest content_digest { };

    friend bool operator==(
        const CoverageDatabaseSourceRecord&,
        const CoverageDatabaseSourceRecord&)
        = default;
};

struct CoverageDatabaseRunRecord {
    CoverageDatabaseIdentity identity;
    std::string label;
    std::string producer;
    std::uint64_t seed { };
    std::uint64_t final_tick { };
    std::uint64_t final_delta { };
    CoverageDatabaseRunStatus status { CoverageDatabaseRunStatus::Complete };

    friend bool operator==(
        const CoverageDatabaseRunRecord&, const CoverageDatabaseRunRecord&)
        = default;
};

struct CoverageDatabaseMetricRecord {
    CoverageDatabaseNamespace name_space { CoverageDatabaseNamespace::Code };
    CoverageDatabaseMetricFamily family {
        CoverageDatabaseMetricFamily::Statement
    };
    CoverageDatabaseMetricScope scope { CoverageDatabaseMetricScope::Source };
    CoverageDatabaseIdentity bin_identity;
    CoverageDatabaseIdentity source_identity;
    CoverageDatabaseIdentity instance_identity;
    CoverageDatabaseIdentity run_identity;
    std::uint64_t hits { };
    std::uint64_t excluded_hits { };
    bool overflow { };
    bool excluded_overflow { };
    // One-based physical source line. Required for projectable code metrics.
    std::uint64_t source_line { };

    friend bool operator==(
        const CoverageDatabaseMetricRecord&,
        const CoverageDatabaseMetricRecord&)
        = default;
};

struct CoverageDatabaseExclusionRecord {
    CoverageDatabaseNamespace name_space { CoverageDatabaseNamespace::Code };
    CoverageDatabaseMetricFamily family {
        CoverageDatabaseMetricFamily::Statement
    };
    CoverageDatabaseMetricScope scope { CoverageDatabaseMetricScope::Source };
    CoverageDatabaseIdentity point_identity;
    CoverageDatabaseIdentity source_identity;
    CoverageDatabaseIdentity instance_identity;
    std::string reason;
    // One-based physical source line. Required for projectable code metrics.
    std::uint64_t source_line { };

    friend bool operator==(
        const CoverageDatabaseExclusionRecord&,
        const CoverageDatabaseExclusionRecord&)
        = default;
};

struct CoverageDatabaseContents {
    CoverageDatabaseModelFingerprint fingerprint;
    std::vector<CoverageDatabaseSourceRecord> sources;
    std::vector<CoverageDatabaseRunRecord> runs;
    std::vector<CoverageDatabaseMetricRecord> metrics;
    std::vector<CoverageDatabaseExclusionRecord> exclusions;

    friend bool operator==(
        const CoverageDatabaseContents&, const CoverageDatabaseContents&)
        = default;
};

struct CoverageDatabaseModelLimits {
    std::size_t maximum_sources { 1U << 20U };
    std::size_t maximum_runs { 1U << 16U };
    std::size_t maximum_metrics { 1U << 24U };
    std::size_t maximum_exclusions { 1U << 24U };
    std::size_t maximum_logical_path_bytes { 1U << 20U };
    std::size_t maximum_label_bytes { 1U << 16U };
    std::size_t maximum_producer_bytes { 1U << 16U };
    std::size_t maximum_reason_bytes { 1U << 20U };
    std::size_t maximum_text_bytes { 1U << 30U };
    std::uint64_t maximum_source_line { 1U << 31U };
};

enum class CoverageDatabaseModelError : std::uint8_t {
    None,
    ResourceLimit,
    ArithmeticOverflow,
    AllocationFailure,
    InvalidFingerprint,
    InvalidSource,
    DuplicateSource,
    InvalidRun,
    DuplicateRun,
    InvalidNamespace,
    InvalidMetricFamily,
    InvalidMetricScope,
    InvalidMetric,
    DuplicateMetric,
    InvalidExclusion,
    DuplicateExclusion,
    NonCanonicalOrder,
};

struct CoverageDatabaseModelResult {
    std::optional<CoverageDatabaseContents> contents;
    CoverageDatabaseModelError error { CoverageDatabaseModelError::None };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return contents.has_value()
            && error == CoverageDatabaseModelError::None;
    }
};

struct CoverageDatabaseModelValidationResult {
    CoverageDatabaseModelError error { CoverageDatabaseModelError::None };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageDatabaseModelError::None;
    }
};

[[nodiscard]] std::string_view coverage_database_metric_family_name(
    CoverageDatabaseMetricFamily value) noexcept;

[[nodiscard]] CoverageDatabaseModelResult make_coverage_database_contents(
    CoverageDatabaseContents contents,
    const CoverageDatabaseModelLimits& limits = { }) noexcept;

[[nodiscard]] CoverageDatabaseModelValidationResult
validate_coverage_database_contents(
    const CoverageDatabaseContents& contents,
    const CoverageDatabaseModelLimits& limits = { }) noexcept;

} // namespace fsim::artifact

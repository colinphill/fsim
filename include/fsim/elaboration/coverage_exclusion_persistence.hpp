// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_database_model.hpp"
#include "fsim/elaboration/coverage_external_exclusions.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageExclusionPersistenceDiagnostic
    = "FSIM-COV-043";

// One elaborated point candidate. Source-control reasons become source-scoped
// records; matching external rules become instance-scoped records.
struct CoverageExclusionCandidate {
    artifact::CoverageDatabaseNamespace name_space {
        artifact::CoverageDatabaseNamespace::Code
    };
    artifact::CoverageDatabaseMetricFamily family {
        artifact::CoverageDatabaseMetricFamily::Statement
    };
    artifact::CoverageDatabaseIdentity point_identity;
    artifact::CoverageDatabaseIdentity source_identity;
    artifact::CoverageDatabaseIdentity instance_identity;
    std::string source_path;
    std::string hierarchy_path;
    std::string object_path;
    std::vector<std::string> source_reasons;
    std::uint64_t source_line { };
};

struct CoverageExclusionPersistenceLimits {
    std::size_t maximum_candidates { 1U << 20U };
    std::size_t maximum_source_reasons { 1U << 24U };
    std::size_t maximum_records { 1U << 24U };
    std::size_t maximum_reason_bytes { 1U << 20U };
    std::size_t maximum_total_reason_bytes { 1U << 30U };
    std::uint64_t maximum_source_line { 1U << 31U };
    CoverageExternalExclusionLimits external;
};

enum class CoverageExclusionPersistenceError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidCandidate,
    DuplicateCandidate,
    InvalidReason,
    ExternalExclusion,
    AllocationFailure,
};

struct CoverageExclusionPersistenceResult {
    std::vector<artifact::CoverageDatabaseExclusionRecord> records;
    CoverageExclusionPersistenceError error {
        CoverageExclusionPersistenceError::None
    };
    CoverageExternalExclusionError external_error {
        CoverageExternalExclusionError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageExclusionPersistenceError::None;
    }
};

[[nodiscard]] CoverageExclusionPersistenceResult
make_coverage_exclusion_records(
    const CoverageExternalExclusionPlan& external_plan,
    std::span<const CoverageExclusionCandidate> candidates,
    CoverageExclusionPersistenceLimits limits = { }) noexcept;

[[nodiscard]] std::string_view coverage_exclusion_persistence_error_name(
    CoverageExclusionPersistenceError error) noexcept;

} // namespace fsim::elaboration

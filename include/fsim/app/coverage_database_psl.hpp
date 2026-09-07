// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/artifact/coverage_database_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fsim::app {

inline constexpr std::string_view kPslCoverageDatabaseDiagnostic
    = "FSIM-COV-034";

struct PslCoverageDatabaseSource {
    std::uint32_t source { };
    artifact::CoverageDatabaseIdentity source_identity;
};

struct PslCoverageDatabaseLimits {
    std::size_t maximum_source_bindings { 1U << 20U };
    std::size_t maximum_directives { 1U << 20U };
    std::size_t maximum_identity_bytes { 1U << 20U };
};

enum class PslCoverageDatabaseError : std::uint8_t {
    None,
    ResourceLimit,
    AllocationFailure,
    InvalidRun,
    InvalidSourceBinding,
    DuplicateSourceBinding,
    UnknownSource,
    InvalidDirective,
    DuplicateDirective,
    NamespaceNotEmpty,
    InvalidDatabaseModel,
};

struct PslCoverageDatabaseResult {
    std::optional<artifact::CoverageDatabaseContents> contents;
    PslCoverageDatabaseError error { PslCoverageDatabaseError::None };
    artifact::CoverageDatabaseModelError model_error {
        artifact::CoverageDatabaseModelError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return contents.has_value() && error == PslCoverageDatabaseError::None;
    }
};

[[nodiscard]] PslCoverageDatabaseResult project_psl_coverage_namespace(
    artifact::CoverageDatabaseContents contents,
    std::span<const ConcurrentAssertionCoverage> coverage,
    std::span<const PslCoverageDatabaseSource> sources,
    artifact::CoverageDatabaseIdentity run_identity,
    const PslCoverageDatabaseLimits& limits = { }) noexcept;

} // namespace fsim::app

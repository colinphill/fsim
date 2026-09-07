// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_database_model.hpp"
#include "fsim/frontend/coverage_persistence.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::frontend {

inline constexpr std::string_view kSystemVerilogCoverageDatabaseDiagnostic
    = "FSIM-COV-033";

struct SystemVerilogCoverageDatabaseSource {
    std::string source_name;
    artifact::CoverageDatabaseIdentity source_identity;
};

struct SystemVerilogCoverageDatabaseLimits {
    std::size_t maximum_source_bindings { 1U << 20U };
    std::size_t maximum_declarations { 1U << 20U };
    std::size_t maximum_instances { 1U << 20U };
    std::size_t maximum_bins { 1U << 24U };
    std::size_t maximum_identity_bytes { 1U << 20U };
    std::size_t maximum_source_name_bytes { 1U << 20U };
};

enum class SystemVerilogCoverageDatabaseError : std::uint8_t {
    None,
    ResourceLimit,
    AllocationFailure,
    InvalidRun,
    InvalidSourceBinding,
    DuplicateSourceBinding,
    UnknownSource,
    InvalidDeclaration,
    DuplicateDeclaration,
    InvalidInstance,
    DuplicateInstance,
    InvalidBin,
    NamespaceNotEmpty,
    InvalidDatabaseModel,
};

struct SystemVerilogCoverageDatabaseResult {
    std::optional<artifact::CoverageDatabaseContents> contents;
    SystemVerilogCoverageDatabaseError error {
        SystemVerilogCoverageDatabaseError::None
    };
    artifact::CoverageDatabaseModelError model_error {
        artifact::CoverageDatabaseModelError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return contents.has_value()
            && error == SystemVerilogCoverageDatabaseError::None;
    }
};

[[nodiscard]] SystemVerilogCoverageDatabaseResult
project_systemverilog_coverage_namespace(
    artifact::CoverageDatabaseContents contents,
    const SystemVerilogCoverageState& state,
    std::span<const SystemVerilogCoverageDatabaseSource> sources,
    artifact::CoverageDatabaseIdentity run_identity,
    const SystemVerilogCoverageDatabaseLimits& limits = { }) noexcept;

} // namespace fsim::frontend

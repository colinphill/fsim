// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_database_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fsim::artifact {

inline constexpr std::string_view kCoverageDatabaseMergeDiagnostic
    = "FSIM-COV-036";

enum class CoverageDatabaseMergeError : std::uint8_t {
    None,
    EmptyInput,
    ResourceLimit,
    AllocationFailure,
    InvalidInput,
    FingerprintMismatch,
    SourceInventoryMismatch,
    ExclusionInventoryMismatch,
    DuplicateRun,
};

struct CoverageDatabaseMergeResult {
    std::optional<CoverageDatabaseContents> contents;
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

struct CoverageDatabaseMergeLimits {
    CoverageDatabaseModelLimits model;
    std::size_t maximum_inputs { 1U << 16U };
};

// The default merge is deliberately strict. Every input must describe the
// exact same design and exclusion policy, and every run identity must be new.
[[nodiscard]] CoverageDatabaseMergeResult merge_coverage_databases(
    std::span<const CoverageDatabaseContents> inputs,
    const CoverageDatabaseMergeLimits& limits = { }) noexcept;

} // namespace fsim::artifact

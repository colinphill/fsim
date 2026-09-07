// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_database_model.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fsim::artifact {

inline constexpr std::string_view kCoverageDatabaseCodecDiagnostic
    = "FSIM-COV-035";

struct CoverageDatabaseCodecLimits {
    CoverageDatabaseLimits container;
    CoverageDatabaseModelLimits model;
};

enum class CoverageDatabaseCodecError : std::uint8_t {
    None,
    ResourceLimit,
    AllocationFailure,
    InvalidModel,
    InvalidSchema,
    Malformed,
    DigestMismatch,
    IoFailure,
    AtomicReplaceFailure,
};

struct CoverageDatabaseEncodeResult {
    std::vector<std::byte> bytes;
    CoverageDatabaseCodecError error { CoverageDatabaseCodecError::None };
    CoverageDatabaseModelError model_error {
        CoverageDatabaseModelError::None
    };
    CoverageDatabaseSchemaError schema_error {
        CoverageDatabaseSchemaError::None
    };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageDatabaseCodecError::None;
    }
};

struct CoverageDatabaseDecodeResult {
    std::optional<CoverageDatabaseContents> contents;
    CoverageDatabaseCodecError error { CoverageDatabaseCodecError::None };
    CoverageDatabaseModelError model_error {
        CoverageDatabaseModelError::None
    };
    CoverageDatabaseSchemaError schema_error {
        CoverageDatabaseSchemaError::None
    };
    std::size_t offset { };

    [[nodiscard]] bool ok() const noexcept
    {
        return contents.has_value()
            && error == CoverageDatabaseCodecError::None;
    }
};

struct CoverageDatabaseFileResult {
    CoverageDatabaseCodecError error { CoverageDatabaseCodecError::None };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageDatabaseCodecError::None;
    }
};

[[nodiscard]] CoverageDatabaseEncodeResult serialize_coverage_database(
    CoverageDatabaseContents contents,
    const CoverageDatabaseCodecLimits& limits = { }) noexcept;

[[nodiscard]] CoverageDatabaseDecodeResult deserialize_coverage_database(
    std::span<const std::byte> bytes,
    const CoverageDatabaseCodecLimits& limits = { }) noexcept;

[[nodiscard]] CoverageDatabaseFileResult write_coverage_database_atomically(
    const std::filesystem::path& path,
    CoverageDatabaseContents contents,
    const CoverageDatabaseCodecLimits& limits = { }) noexcept;

[[nodiscard]] CoverageDatabaseDecodeResult read_coverage_database(
    const std::filesystem::path& path,
    const CoverageDatabaseCodecLimits& limits = { }) noexcept;

} // namespace fsim::artifact

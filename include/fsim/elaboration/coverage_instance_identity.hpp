// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/token.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageInstanceIdentitySchema
    = "fsim-code-coverage-instance-v3";
inline constexpr std::string_view kCoverageInstanceIdentityDiagnostic
    = "FSIM-COV-012";

struct CoverageInstanceIdentity {
    std::uint64_t high { };
    std::uint64_t low { };

    friend constexpr bool operator==(
        const CoverageInstanceIdentity&, const CoverageInstanceIdentity&)
        = default;
};

struct CoverageInstanceIdentityInput {
    std::string_view hierarchy_path;
    frontend::Language language { frontend::Language::SystemVerilog2017 };
    std::string_view library;
    std::string_view unit;
    std::span<const std::pair<std::string, std::string>>
        parameter_identities;
};

struct CoverageInstanceIdentityLimits {
    std::size_t maximum_hierarchy_bytes { 1U << 20U };
    std::size_t maximum_library_bytes { 1U << 16U };
    std::size_t maximum_unit_bytes { 1U << 16U };
    std::size_t maximum_parameter_count { 1U << 16U };
    std::size_t maximum_parameter_bytes { 1U << 20U };
};

enum class CoverageInstanceIdentityError : std::uint8_t {
    None,
    ResourceLimit,
    EmptyHierarchyPath,
    InvalidHierarchyPath,
    InvalidLanguage,
    EmptyLibrary,
    EmptyUnit,
    InvalidParameterIdentity,
    DuplicateParameterName,
    ZeroIdentity,
};

struct CoverageInstanceIdentityResult {
    std::optional<CoverageInstanceIdentity> identity;
    CoverageInstanceIdentityError error {
        CoverageInstanceIdentityError::None
    };

    [[nodiscard]] bool ok() const noexcept
    {
        return identity.has_value()
            && error == CoverageInstanceIdentityError::None;
    }
};

[[nodiscard]] CoverageInstanceIdentityResult
make_coverage_instance_identity(const CoverageInstanceIdentityInput& input,
    CoverageInstanceIdentityLimits limits = { }) noexcept;

[[nodiscard]] constexpr bool is_coverage_instance_identity_valid(
    const CoverageInstanceIdentity identity) noexcept
{
    return identity.high != 0U || identity.low != 0U;
}

[[nodiscard]] std::string coverage_instance_identity_hex(
    CoverageInstanceIdentity identity);

} // namespace fsim::elaboration

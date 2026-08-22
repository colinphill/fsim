// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/sha256.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fsim::frontend {

inline constexpr std::string_view kCodeCoverageSourceIdentitySchema = "fsim-code-coverage-source-v1";
inline constexpr std::string_view kCodeCoverageSourceIdentityDiagnostic = "FSIM-COV-002";

struct CodeCoverageSourceIdentity {
    std::string logical_path;
    std::size_t content_bytes { };
    support::Sha256::Digest content_digest { };
    support::Sha256::Digest digest { };

    friend bool operator==(const CodeCoverageSourceIdentity&,
        const CodeCoverageSourceIdentity&)
        = default;
};

struct CodeCoverageSourceIdentityLimits {
    std::size_t maximum_logical_path_bytes { 1U << 20U };
    std::size_t maximum_content_bytes { 1U << 30U };
};

enum class CodeCoverageSourceIdentityError {
    None,
    CheckoutRootRequired,
    SourcePathRequired,
    SourceOutsideCheckout,
    InvalidPathEncoding,
    PathLimit,
    ContentLimit,
};

struct CodeCoverageSourceIdentityResult {
    std::optional<CodeCoverageSourceIdentity> identity;
    CodeCoverageSourceIdentityError error {
        CodeCoverageSourceIdentityError::None
    };

    [[nodiscard]] bool ok() const noexcept
    {
        return identity.has_value()
            && error == CodeCoverageSourceIdentityError::None;
    }
};

[[nodiscard]] CodeCoverageSourceIdentityResult
make_code_coverage_source_identity(const std::filesystem::path& checkout_root,
    const std::filesystem::path& source_path,
    std::span<const std::byte> contents,
    CodeCoverageSourceIdentityLimits limits = { }) noexcept;

[[nodiscard]] std::string code_coverage_source_content_hex(
    const CodeCoverageSourceIdentity& identity);

[[nodiscard]] std::string code_coverage_source_identity_hex(
    const CodeCoverageSourceIdentity& identity);

[[nodiscard]] bool is_code_coverage_source_identity_valid(
    const CodeCoverageSourceIdentity& identity) noexcept;

} // namespace fsim::frontend

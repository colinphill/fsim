// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_source_identity.hpp"
#include "fsim/runtime/code_coverage.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::frontend {

inline constexpr std::string_view kCodeCoveragePointIdentitySchema = "fsim-code-coverage-point-v1";
inline constexpr std::string_view kCodeCoveragePointIdentityDiagnostic = "FSIM-COV-003";

enum class CodeCoverageLanguage : std::uint8_t {
    Verilog = 1U,
    SystemVerilog = 2U,
    Vhdl = 3U,
};

enum class CodeCoverageConstructKind : std::uint8_t {
    Statement = 1U,
    BranchTrueArm = 2U,
    BranchFalseArm = 3U,
    BranchCaseArm = 4U,
    BranchDefaultArm = 5U,
    BranchImplicitArm = 6U,
};

struct CodeCoverageSourceSpan {
    std::uint64_t begin_offset { };
    std::uint64_t end_offset { };

    friend constexpr bool operator==(
        const CodeCoverageSourceSpan&, const CodeCoverageSourceSpan&)
        = default;
};

enum class CodeCoveragePointIdentityError : std::uint8_t {
    None,
    InvalidSourceIdentity,
    InvalidLanguage,
    InvalidConstructKind,
    ReversedSpan,
    EmptySpan,
    SpanOutsideSource,
    ZeroIdentity,
};

struct CodeCoveragePointIdentityResult {
    std::optional<runtime::CodeCoveragePointId> identity;
    CodeCoveragePointIdentityError error {
        CodeCoveragePointIdentityError::None
    };

    [[nodiscard]] bool ok() const noexcept
    {
        return identity.has_value()
            && error == CodeCoveragePointIdentityError::None;
    }
};

[[nodiscard]] constexpr std::string_view code_coverage_language_name(
    CodeCoverageLanguage language) noexcept;

[[nodiscard]] constexpr std::string_view code_coverage_construct_kind_name(
    CodeCoverageConstructKind kind) noexcept;

[[nodiscard]] CodeCoveragePointIdentityResult
make_code_coverage_point_identity(const CodeCoverageSourceIdentity& source,
    CodeCoverageLanguage language, CodeCoverageConstructKind construct,
    CodeCoverageSourceSpan span) noexcept;

[[nodiscard]] std::string code_coverage_point_identity_hex(
    runtime::CodeCoveragePointId identity);

constexpr std::string_view code_coverage_language_name(
    const CodeCoverageLanguage language) noexcept
{
    switch (language) {
    case CodeCoverageLanguage::Verilog:
        return "verilog";
    case CodeCoverageLanguage::SystemVerilog:
        return "systemverilog";
    case CodeCoverageLanguage::Vhdl:
        return "vhdl";
    }
    return { };
}

constexpr std::string_view code_coverage_construct_kind_name(
    const CodeCoverageConstructKind kind) noexcept
{
    switch (kind) {
    case CodeCoverageConstructKind::Statement:
        return "statement";
    case CodeCoverageConstructKind::BranchTrueArm:
        return "branch-true";
    case CodeCoverageConstructKind::BranchFalseArm:
        return "branch-false";
    case CodeCoverageConstructKind::BranchCaseArm:
        return "branch-case";
    case CodeCoverageConstructKind::BranchDefaultArm:
        return "branch-default";
    case CodeCoverageConstructKind::BranchImplicitArm:
        return "branch-implicit";
    }
    return { };
}

} // namespace fsim::frontend

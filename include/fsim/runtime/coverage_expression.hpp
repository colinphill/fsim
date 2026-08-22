// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/code_coverage.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::string_view kCoverageExpressionDiagnostic
    = "FSIM-COV-019";

struct CoverageExpressionCombination {
    std::uint64_t ordinal { };
    std::vector<std::uint64_t> true_words;

    friend bool operator==(const CoverageExpressionCombination&,
        const CoverageExpressionCombination&)
        = default;
};

struct CoverageExpressionOmission {
    std::size_t atomic_conditions { };
    std::size_t retained_combinations { };
    std::uint64_t omitted_combinations { };
    bool omitted_count_exact { true };

    friend bool operator==(const CoverageExpressionOmission&,
        const CoverageExpressionOmission&)
        = default;
};

struct CoverageExpressionInventory {
    std::vector<CodeCoveragePointId> atoms;
    std::vector<CoverageExpressionCombination> combinations;
    CoverageExpressionOmission omission;
};

struct CoverageExpressionLimits {
    std::size_t maximum_atoms { 1U << 20U };
    std::size_t maximum_combinations { 1U << 20U };
    std::size_t maximum_combination_words { 1U << 22U };
};

enum class CoverageExpressionError : std::uint8_t {
    None,
    EmptyExpression,
    ResourceLimit,
    InvalidPointIdentity,
    DuplicatePointIdentity,
};

struct CoverageExpressionResult {
    CoverageExpressionInventory inventory;
    CoverageExpressionError error { CoverageExpressionError::None };
    std::size_t atom_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageExpressionError::None;
    }
};

[[nodiscard]] CoverageExpressionResult build_coverage_expression_inventory(
    std::span<const CodeCoveragePointId> atoms,
    CoverageExpressionLimits limits = { }) noexcept;

[[nodiscard]] bool coverage_expression_combination_truth(
    const CoverageExpressionCombination& combination,
    std::size_t atom_index) noexcept;

[[nodiscard]] std::string coverage_expression_omission_description(
    const CoverageExpressionOmission& omission);

} // namespace fsim::runtime

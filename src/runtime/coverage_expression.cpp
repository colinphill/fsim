// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_expression.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace fsim::runtime {
namespace {

    using PointKey = std::pair<std::uint64_t, std::uint64_t>;

} // namespace

CoverageExpressionResult build_coverage_expression_inventory(
    const std::span<const CodeCoveragePointId> atoms,
    const CoverageExpressionLimits limits) noexcept
{
    CoverageExpressionResult result;
    const auto reject = [&](const CoverageExpressionError error,
                            const std::size_t atom_index = 0U) {
        result.inventory = { };
        result.error = error;
        result.atom_index = atom_index;
        return result;
    };

    try {
        if (atoms.empty()) {
            return reject(CoverageExpressionError::EmptyExpression);
        }
        if (atoms.size() > limits.maximum_atoms) {
            return reject(CoverageExpressionError::ResourceLimit);
        }
        std::set<PointKey> identities;
        for (std::size_t index = 0U; index < atoms.size(); ++index) {
            if (!is_code_coverage_identity_valid(atoms[index])) {
                return reject(
                    CoverageExpressionError::InvalidPointIdentity, index);
            }
            if (!identities.emplace(
                               atoms[index].high, atoms[index].low)
                    .second) {
                return reject(
                    CoverageExpressionError::DuplicatePointIdentity, index);
            }
        }

        const auto word_count = (atoms.size() - 1U) / 64U + 1U;
        auto retained = limits.maximum_combinations;
        const bool total_exact = atoms.size() < 64U;
        std::uint64_t total { };
        if (total_exact) {
            total = std::uint64_t { 1U } << atoms.size();
            retained = std::min(retained,
                static_cast<std::size_t>(total));
        }
        retained = std::min(retained,
            limits.maximum_combination_words / word_count);

        result.inventory.atoms.assign(atoms.begin(), atoms.end());
        result.inventory.combinations.reserve(retained);
        for (std::size_t index = 0U; index < retained; ++index) {
            CoverageExpressionCombination combination;
            combination.ordinal = static_cast<std::uint64_t>(index);
            combination.true_words.assign(word_count, 0U);
            for (std::size_t atom_index = 0U;
                atom_index < atoms.size(); ++atom_index) {
                const auto distance = atoms.size() - atom_index - 1U;
                const bool truth = distance < 64U
                    && ((combination.ordinal >> distance) & 1U) != 0U;
                if (truth) {
                    combination.true_words[atom_index / 64U]
                        |= std::uint64_t { 1U } << (atom_index % 64U);
                }
            }
            result.inventory.combinations.push_back(
                std::move(combination));
        }

        result.inventory.omission.atomic_conditions = atoms.size();
        result.inventory.omission.retained_combinations = retained;
        result.inventory.omission.omitted_count_exact = total_exact;
        result.inventory.omission.omitted_combinations = total_exact
            ? total - static_cast<std::uint64_t>(retained)
            : std::numeric_limits<std::uint64_t>::max();
        return result;
    } catch (...) {
        return reject(CoverageExpressionError::ResourceLimit);
    }
}

bool coverage_expression_combination_truth(
    const CoverageExpressionCombination& combination,
    const std::size_t atom_index) noexcept
{
    const auto word = atom_index / 64U;
    return word < combination.true_words.size()
        && ((combination.true_words[word] >> (atom_index % 64U)) & 1U)
        != 0U;
}

std::string coverage_expression_omission_description(
    const CoverageExpressionOmission& omission)
{
    if (omission.omitted_count_exact) {
        return std::to_string(omission.omitted_combinations);
    }
    return "2^" + std::to_string(omission.atomic_conditions) + "-"
        + std::to_string(omission.retained_combinations);
}

} // namespace fsim::runtime

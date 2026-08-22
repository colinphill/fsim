// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_expression.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

fsim::runtime::CodeCoveragePointId point(const std::size_t index)
{
    return { 0x1790000000000006ULL,
        static_cast<std::uint64_t>(index + 1U) };
}

std::vector<fsim::runtime::CodeCoveragePointId> points(
    const std::size_t count)
{
    std::vector<fsim::runtime::CodeCoveragePointId> result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        result.push_back(point(index));
    }
    return result;
}

std::string bits(const fsim::runtime::CoverageExpressionCombination& value,
    const std::size_t atom_count)
{
    std::string result;
    result.reserve(atom_count);
    for (std::size_t index = 0U; index < atom_count; ++index) {
        result.push_back(
            fsim::runtime::coverage_expression_combination_truth(value, index)
                ? '1'
                : '0');
    }
    return result;
}

void test_complete_canonical_expansion()
{
    const auto atoms = points(3U);
    const auto result
        = fsim::runtime::build_coverage_expression_inventory(atoms);
    require(result.ok() && result.inventory.atoms == atoms
            && result.inventory.combinations.size() == 8U,
        "three atomic conditions must produce eight canonical combinations");
    constexpr std::array expected {
        std::string_view { "000" },
        std::string_view { "001" },
        std::string_view { "010" },
        std::string_view { "011" },
        std::string_view { "100" },
        std::string_view { "101" },
        std::string_view { "110" },
        std::string_view { "111" },
    };
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        require(result.inventory.combinations[index].ordinal == index
                && bits(result.inventory.combinations[index], atoms.size())
                    == expected[index],
            "combination ordinals must use stable lexical binary order");
    }
    require(result.inventory.omission
                == fsim::runtime::CoverageExpressionOmission {
                    3U, 8U, 0U, true }
            && fsim::runtime::coverage_expression_omission_description(result.inventory.omission) == "0",
        "complete expansion must explicitly report zero omissions");
}

void test_bounded_prefix_and_exact_omissions()
{
    const auto atoms = points(3U);
    fsim::runtime::CoverageExpressionLimits limits;
    limits.maximum_combinations = 3U;
    const auto result
        = fsim::runtime::build_coverage_expression_inventory(atoms, limits);
    require(result.ok() && result.inventory.combinations.size() == 3U
            && bits(result.inventory.combinations.back(), atoms.size())
                == "010",
        "combination ceiling must retain a deterministic canonical prefix");
    require(result.inventory.omission.omitted_count_exact
            && result.inventory.omission.omitted_combinations == 5U
            && fsim::runtime::coverage_expression_omission_description(
                   result.inventory.omission)
                == "5",
        "every bounded-away binary combination must be reported exactly");

    limits.maximum_combinations = 8U;
    limits.maximum_combination_words = 2U;
    const auto storage_limited
        = fsim::runtime::build_coverage_expression_inventory(atoms, limits);
    require(storage_limited.ok()
            && storage_limited.inventory.combinations.size() == 2U
            && storage_limited.inventory.omission.omitted_combinations == 6U,
        "storage ceiling must also truncate with explicit omission accounting");

    limits.maximum_combination_words = 0U;
    const auto none
        = fsim::runtime::build_coverage_expression_inventory(atoms, limits);
    require(none.ok() && none.inventory.combinations.empty()
            && none.inventory.omission.omitted_combinations == 8U,
        "a zero expansion budget must report the complete omitted space");
}

void test_symbolic_wide_omission_and_storage()
{
    const auto atoms = points(70U);
    fsim::runtime::CoverageExpressionLimits limits;
    limits.maximum_combinations = 4U;
    limits.maximum_combination_words = 8U;
    const auto result
        = fsim::runtime::build_coverage_expression_inventory(atoms, limits);
    require(result.ok() && result.inventory.combinations.size() == 4U
            && !result.inventory.omission.omitted_count_exact
            && result.inventory.omission.omitted_combinations
                == std::numeric_limits<std::uint64_t>::max(),
        "wide combination spaces must remain bounded without a false exact count");
    require(fsim::runtime::coverage_expression_omission_description(
                result.inventory.omission)
            == "2^70-4",
        "wide omissions must publish the exact symbolic space expression");
    require(bits(result.inventory.combinations[0], atoms.size())
                == std::string(70U, '0')
            && bits(result.inventory.combinations[3], atoms.size())
                == std::string(68U, '0') + "11",
        "wide retained combinations must preserve canonical binary order");

    limits.maximum_combination_words = 3U;
    const auto one
        = fsim::runtime::build_coverage_expression_inventory(atoms, limits);
    require(one.ok() && one.inventory.combinations.size() == 1U
            && fsim::runtime::coverage_expression_omission_description(
                   one.inventory.omission)
                == "2^70-1",
        "whole-combination word storage must be bounded without partial bins");
}

void test_rejections_and_determinism()
{
    using Error = fsim::runtime::CoverageExpressionError;
    require(fsim::runtime::build_coverage_expression_inventory({ }).error
            == Error::EmptyExpression,
        "an expression without atomic conditions must be rejected");

    auto atoms = points(3U);
    atoms[1] = { };
    auto result = fsim::runtime::build_coverage_expression_inventory(atoms);
    require(result.error == Error::InvalidPointIdentity
            && result.atom_index == 1U
            && result.inventory.atoms.empty()
            && result.inventory.combinations.empty(),
        "invalid identities must not publish a partial inventory");
    atoms = points(3U);
    atoms[2] = atoms[0];
    result = fsim::runtime::build_coverage_expression_inventory(atoms);
    require(result.error == Error::DuplicatePointIdentity
            && result.atom_index == 2U
            && result.inventory.combinations.empty(),
        "duplicate identities must fail transactionally");

    atoms = points(3U);
    fsim::runtime::CoverageExpressionLimits limits;
    limits.maximum_atoms = 2U;
    result = fsim::runtime::build_coverage_expression_inventory(atoms, limits);
    require(result.error == Error::ResourceLimit
            && result.inventory.atoms.empty(),
        "atom ceilings must apply before retained inventory allocation");

    limits = { };
    limits.maximum_combinations = 5U;
    const auto first
        = fsim::runtime::build_coverage_expression_inventory(atoms, limits);
    const auto repeated
        = fsim::runtime::build_coverage_expression_inventory(atoms, limits);
    require(first.ok() && repeated.ok()
            && first.inventory.atoms == repeated.inventory.atoms
            && first.inventory.combinations
                == repeated.inventory.combinations
            && first.inventory.omission == repeated.inventory.omission,
        "bounded expansion and omissions must be deterministic across runs");
}

} // namespace

int main()
{
    test_complete_canonical_expansion();
    test_bounded_prefix_and_exact_omissions();
    test_symbolic_wide_omission_and_storage();
    test_rejections_and_determinism();
    std::cout << "coverage expression tests passed\n";
    return 0;
}

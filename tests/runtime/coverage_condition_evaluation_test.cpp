// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_condition_evaluation.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fsim::runtime::CoverageConditionEvaluationAtom;
using fsim::runtime::CoverageConditionLogicalOperator;
using fsim::runtime::CoverageConditionOperand;
using fsim::runtime::CoverageConditionPathStep;
using fsim::runtime::CoverageConditionTruth;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct OwnedAtom {
    fsim::runtime::CodeCoveragePointId point;
    std::size_t condition_index { };
    std::vector<CoverageConditionPathStep> path;
};

fsim::runtime::CodeCoveragePointId point(const std::size_t index)
{
    return { 0x1790000000000004ULL,
        static_cast<std::uint64_t>(index + 1U) };
}

std::vector<CoverageConditionEvaluationAtom> views(
    const std::span<const OwnedAtom> atoms)
{
    std::vector<CoverageConditionEvaluationAtom> result;
    result.reserve(atoms.size());
    for (const auto& atom : atoms) {
        result.push_back(CoverageConditionEvaluationAtom {
            atom.point, atom.condition_index, atom.path });
    }
    return result;
}

std::vector<OwnedAtom> binary_atoms(
    const CoverageConditionLogicalOperator operation)
{
    return {
        { point(0U), 0U, { { operation, CoverageConditionOperand::Left } } },
        { point(1U), 1U, { { operation, CoverageConditionOperand::Right } } },
    };
}

void require_indices(const auto& values,
    const std::span<const std::size_t> expected,
    const std::string_view message)
{
    if (values.size() != expected.size()) {
        require(false, message);
    }
    for (std::size_t index = 0U; index < values.size(); ++index) {
        require(values[index].condition_index == expected[index], message);
    }
}

void test_nested_systemverilog_short_circuit_order()
{
    using enum CoverageConditionLogicalOperator;
    using enum CoverageConditionOperand;
    // (a && b) || ((!c) && d)
    const std::vector<OwnedAtom> owned {
        { point(0U), 0U, { { Or, Left }, { And, Left } } },
        { point(1U), 1U, { { Or, Left }, { And, Right } } },
        { point(2U), 2U, { { Or, Right }, { And, Left }, { Not, Only } } },
        { point(3U), 3U, { { Or, Right }, { And, Right } } },
    };
    const auto atoms = views(owned);
    const std::array truth {
        CoverageConditionTruth::False,
        CoverageConditionTruth::True,
        CoverageConditionTruth::False,
        CoverageConditionTruth::True,
    };
    std::vector<std::size_t> callbacks;
    const auto result = fsim::runtime::evaluate_coverage_condition(atoms,
        [&](const CoverageConditionEvaluationAtom& atom) {
            callbacks.push_back(atom.condition_index);
            return std::optional { truth[atom.condition_index] };
        });
    require(result.ok()
            && result.decision_truth == CoverageConditionTruth::True,
        "nested SystemVerilog decision must retain its actual result");
    constexpr std::array<std::size_t, 3U> expected_callbacks { 0U, 2U, 3U };
    constexpr std::array<std::size_t, 1U> expected_skipped { 1U };
    require(std::ranges::equal(callbacks, expected_callbacks),
        "atom callbacks must follow actual expression evaluation order");
    require_indices(result.observations, expected_callbacks,
        "only evaluated atoms may be observed");
    require_indices(result.skipped, expected_skipped,
        "the skipped left-and operand must remain explicit");

    const std::array left_true {
        CoverageConditionTruth::True,
        CoverageConditionTruth::True,
        CoverageConditionTruth::Unknown,
        CoverageConditionTruth::Unknown,
    };
    callbacks.clear();
    const auto outer_skipped = fsim::runtime::evaluate_coverage_condition(atoms,
        [&](const CoverageConditionEvaluationAtom& atom) {
            callbacks.push_back(atom.condition_index);
            return std::optional { left_true[atom.condition_index] };
        });
    constexpr std::array<std::size_t, 2U> expected_left_callbacks { 0U, 1U };
    constexpr std::array<std::size_t, 2U> expected_right_skipped { 2U, 3U };
    require(outer_skipped.ok()
            && outer_skipped.decision_truth == CoverageConditionTruth::True
            && std::ranges::equal(callbacks, expected_left_callbacks),
        "a true left side of logical-or must skip the complete right subtree");
    require_indices(outer_skipped.skipped, expected_right_skipped,
        "subtree skipping must publish every skipped atom in lexical order");
}

void test_four_state_short_circuit_boundaries()
{
    using enum CoverageConditionLogicalOperator;
    struct Case {
        CoverageConditionLogicalOperator operation;
        CoverageConditionTruth left;
        CoverageConditionTruth right;
        CoverageConditionTruth expected;
        std::size_t callbacks;
    };
    constexpr std::array cases {
        Case { And, CoverageConditionTruth::False,
            CoverageConditionTruth::Unknown, CoverageConditionTruth::False,
            1U },
        Case { And, CoverageConditionTruth::Unknown,
            CoverageConditionTruth::False, CoverageConditionTruth::False,
            2U },
        Case { And, CoverageConditionTruth::Unknown,
            CoverageConditionTruth::True, CoverageConditionTruth::Unknown,
            2U },
        Case { Or, CoverageConditionTruth::True,
            CoverageConditionTruth::Unknown, CoverageConditionTruth::True,
            1U },
        Case { Or, CoverageConditionTruth::Unknown,
            CoverageConditionTruth::True, CoverageConditionTruth::True, 2U },
        Case { Or, CoverageConditionTruth::Unknown,
            CoverageConditionTruth::False, CoverageConditionTruth::Unknown,
            2U },
    };
    for (const auto& test : cases) {
        const auto owned = binary_atoms(test.operation);
        const auto atoms = views(owned);
        const std::array truth { test.left, test.right };
        std::size_t callbacks { };
        const auto result = fsim::runtime::evaluate_coverage_condition(atoms,
            [&](const CoverageConditionEvaluationAtom& atom) {
                ++callbacks;
                return std::optional { truth[atom.condition_index] };
            });
        require(result.ok() && result.decision_truth == test.expected
                && callbacks == test.callbacks,
            "four-state logical evaluation must skip only determined results");
        require(result.observations.size() == test.callbacks
                && result.skipped.size() == 2U - test.callbacks,
            "four-state observations and skips must partition the atom set");
    }
}

void test_vhdl_operator_execution_contract()
{
    using enum CoverageConditionLogicalOperator;
    struct Case {
        CoverageConditionLogicalOperator operation;
        CoverageConditionTruth left;
        CoverageConditionTruth right;
        CoverageConditionTruth expected;
        std::size_t callbacks;
    };
    constexpr std::array cases {
        Case { Nand, CoverageConditionTruth::False,
            CoverageConditionTruth::True, CoverageConditionTruth::True, 1U },
        Case { Nor, CoverageConditionTruth::True,
            CoverageConditionTruth::False, CoverageConditionTruth::False, 1U },
        Case { Xor, CoverageConditionTruth::False,
            CoverageConditionTruth::True, CoverageConditionTruth::True, 2U },
        Case { Xnor, CoverageConditionTruth::True,
            CoverageConditionTruth::True, CoverageConditionTruth::True, 2U },
    };
    for (const auto& test : cases) {
        const auto owned = binary_atoms(test.operation);
        const auto atoms = views(owned);
        const std::array truth { test.left, test.right };
        std::size_t callbacks { };
        const auto result = fsim::runtime::evaluate_coverage_condition(atoms,
            [&](const CoverageConditionEvaluationAtom& atom) {
                ++callbacks;
                return std::optional { truth[atom.condition_index] };
            });
        require(result.ok() && result.decision_truth == test.expected
                && callbacks == test.callbacks,
            "VHDL Boolean operators must match the governed lowering contract");
    }
}

void test_input_order_and_callback_failures()
{
    using enum CoverageConditionLogicalOperator;
    using enum CoverageConditionOperand;
    const std::vector<OwnedAtom> owned {
        { point(1U), 1U, { { And, Right } } },
        { point(0U), 0U, { { And, Left } } },
    };
    const auto atoms = views(owned);
    std::vector<std::size_t> callbacks;
    const auto ordered = fsim::runtime::evaluate_coverage_condition(atoms,
        [&](const CoverageConditionEvaluationAtom& atom) {
            callbacks.push_back(atom.condition_index);
            return std::optional { CoverageConditionTruth::True };
        });
    constexpr std::array<std::size_t, 2U> expected { 0U, 1U };
    require(ordered.ok() && std::ranges::equal(callbacks, expected),
        "tree order must be independent of input storage order");

    const auto failed = fsim::runtime::evaluate_coverage_condition(atoms,
        [](const CoverageConditionEvaluationAtom& atom)
            -> std::optional<CoverageConditionTruth> {
            if (atom.condition_index == 1U) {
                return std::nullopt;
            }
            return CoverageConditionTruth::True;
        });
    require(!failed.ok()
            && failed.error
                == fsim::runtime::CoverageConditionEvaluationError::EvaluationFailure
            && failed.condition_index == 1U
            && failed.observations.empty() && failed.skipped.empty(),
        "callback failure must discard every partial observation");

    const auto threw = fsim::runtime::evaluate_coverage_condition(atoms,
        [](const CoverageConditionEvaluationAtom&)
            -> std::optional<CoverageConditionTruth> {
            throw std::runtime_error { "independently authored failure" };
        });
    require(!threw.ok()
            && threw.error
                == fsim::runtime::CoverageConditionEvaluationError::EvaluationFailure
            && threw.observations.empty() && threw.skipped.empty(),
        "throwing evaluators must be contained transactionally");
}

void test_malformed_plans_and_resource_bounds()
{
    using Error = fsim::runtime::CoverageConditionEvaluationError;
    using enum CoverageConditionLogicalOperator;
    using enum CoverageConditionOperand;
    const auto always_true = [](const CoverageConditionEvaluationAtom&) {
        return std::optional { CoverageConditionTruth::True };
    };
    require(fsim::runtime::evaluate_coverage_condition({ }, always_true).error
            == Error::EmptyDecision,
        "empty decisions must be rejected");

    std::vector<OwnedAtom> owned {
        { point(0U), 0U, { { And, Left } } },
        { point(1U), 1U, { { And, Right } } },
    };
    auto atoms = views(owned);
    atoms[0].point = { };
    require(fsim::runtime::evaluate_coverage_condition(atoms, always_true).error
            == Error::InvalidPointIdentity,
        "zero point identities must be rejected");
    atoms = views(owned);
    atoms[1].point = atoms[0].point;
    require(fsim::runtime::evaluate_coverage_condition(atoms, always_true).error
            == Error::DuplicatePointIdentity,
        "duplicate point identities must be rejected");
    atoms = views(owned);
    atoms[1].condition_index = 0U;
    require(fsim::runtime::evaluate_coverage_condition(atoms, always_true).error
            == Error::DuplicateConditionIndex,
        "duplicate condition ordinals must be rejected");
    atoms = views(owned);
    atoms[1].condition_index = 2U;
    require(fsim::runtime::evaluate_coverage_condition(atoms, always_true).error
            == Error::NonCanonicalConditionIndex,
        "condition ordinals must be dense and bounded");

    owned[1].path = { { Or, Right } };
    atoms = views(owned);
    require(fsim::runtime::evaluate_coverage_condition(atoms, always_true).error
            == Error::MalformedEvaluationPath,
        "one tree prefix cannot carry conflicting operators");
    owned[1].path = { { And, Only } };
    atoms = views(owned);
    require(fsim::runtime::evaluate_coverage_condition(atoms, always_true).error
            == Error::MalformedEvaluationPath,
        "binary operators cannot carry unary operands");
    owned.resize(1U);
    atoms = views(owned);
    require(fsim::runtime::evaluate_coverage_condition(atoms, always_true).error
            == Error::MalformedEvaluationPath,
        "incomplete binary expression trees must be rejected");

    owned = binary_atoms(And);
    atoms = views(owned);
    fsim::runtime::CoverageConditionEvaluationLimits limits;
    limits.maximum_atoms = 1U;
    require(fsim::runtime::evaluate_coverage_condition(
                atoms, always_true, limits)
                .error
            == Error::ResourceLimit,
        "atom count ceilings must fail before evaluation");
    limits = { };
    limits.maximum_nodes = 2U;
    require(fsim::runtime::evaluate_coverage_condition(
                atoms, always_true, limits)
                .error
            == Error::ResourceLimit,
        "tree node ceilings must fail before evaluation");
    limits = { };
    limits.maximum_path_steps = 1U;
    require(fsim::runtime::evaluate_coverage_condition(
                atoms, always_true, limits)
                .error
            == Error::ResourceLimit,
        "retained path storage ceilings must be enforced cumulatively");
    limits = { };
    limits.maximum_nesting = 0U;
    require(fsim::runtime::evaluate_coverage_condition(
                atoms, always_true, limits)
                .error
            == Error::ResourceLimit,
        "expression nesting ceilings must be enforced before evaluation");
}

} // namespace

int main()
{
    test_nested_systemverilog_short_circuit_order();
    test_four_state_short_circuit_boundaries();
    test_vhdl_operator_execution_contract();
    test_input_order_and_callback_failures();
    test_malformed_plans_and_resource_bounds();
    std::cout << "coverage condition evaluation tests passed\n";
    return 0;
}

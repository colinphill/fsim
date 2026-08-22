// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_condition_evaluation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace fsim::runtime {
namespace {

    constexpr auto kMissing = std::numeric_limits<std::size_t>::max();

    struct EvaluationNode {
        std::optional<CoverageConditionLogicalOperator> logical_operator;
        std::array<std::size_t, 3U> children {
            kMissing, kMissing, kMissing
        };
        std::size_t atom { kMissing };
    };

    enum class EvaluationPhase : std::uint8_t {
        Enter,
        AfterOnly,
        AfterLeft,
        AfterRight,
    };

    struct EvaluationFrame {
        std::size_t node { };
        EvaluationPhase phase { EvaluationPhase::Enter };
        CoverageConditionTruth left { CoverageConditionTruth::Unknown };
    };

    using PointKey = std::pair<std::uint64_t, std::uint64_t>;

    std::optional<std::size_t> operand_slot(
        const CoverageConditionLogicalOperator operation,
        const CoverageConditionOperand operand) noexcept
    {
        if (operation == CoverageConditionLogicalOperator::Not) {
            return operand == CoverageConditionOperand::Only
                ? std::optional<std::size_t> { 2U }
                : std::nullopt;
        }
        if (operand == CoverageConditionOperand::Left) {
            return 0U;
        }
        if (operand == CoverageConditionOperand::Right) {
            return 1U;
        }
        return std::nullopt;
    }

    bool valid_truth(const CoverageConditionTruth truth) noexcept
    {
        switch (truth) {
        case CoverageConditionTruth::False:
        case CoverageConditionTruth::True:
        case CoverageConditionTruth::Unknown:
            return true;
        }
        return false;
    }

    CoverageConditionTruth invert(const CoverageConditionTruth truth) noexcept
    {
        switch (truth) {
        case CoverageConditionTruth::False:
            return CoverageConditionTruth::True;
        case CoverageConditionTruth::True:
            return CoverageConditionTruth::False;
        case CoverageConditionTruth::Unknown:
            return CoverageConditionTruth::Unknown;
        }
        return CoverageConditionTruth::Unknown;
    }

    CoverageConditionTruth combine_and(const CoverageConditionTruth left,
        const CoverageConditionTruth right) noexcept
    {
        if (left == CoverageConditionTruth::False
            || right == CoverageConditionTruth::False) {
            return CoverageConditionTruth::False;
        }
        if (left == CoverageConditionTruth::True
            && right == CoverageConditionTruth::True) {
            return CoverageConditionTruth::True;
        }
        return CoverageConditionTruth::Unknown;
    }

    CoverageConditionTruth combine_or(const CoverageConditionTruth left,
        const CoverageConditionTruth right) noexcept
    {
        if (left == CoverageConditionTruth::True
            || right == CoverageConditionTruth::True) {
            return CoverageConditionTruth::True;
        }
        if (left == CoverageConditionTruth::False
            && right == CoverageConditionTruth::False) {
            return CoverageConditionTruth::False;
        }
        return CoverageConditionTruth::Unknown;
    }

    CoverageConditionTruth combine(
        const CoverageConditionLogicalOperator operation,
        const CoverageConditionTruth left,
        const CoverageConditionTruth right) noexcept
    {
        switch (operation) {
        case CoverageConditionLogicalOperator::And:
            return combine_and(left, right);
        case CoverageConditionLogicalOperator::Or:
            return combine_or(left, right);
        case CoverageConditionLogicalOperator::Nand:
            return invert(combine_and(left, right));
        case CoverageConditionLogicalOperator::Nor:
            return invert(combine_or(left, right));
        case CoverageConditionLogicalOperator::Xor:
        case CoverageConditionLogicalOperator::Xnor:
            if (left == CoverageConditionTruth::Unknown
                || right == CoverageConditionTruth::Unknown) {
                return CoverageConditionTruth::Unknown;
            }
            if (operation == CoverageConditionLogicalOperator::Xor) {
                return left == right ? CoverageConditionTruth::False
                                     : CoverageConditionTruth::True;
            }
            return left == right ? CoverageConditionTruth::True
                                 : CoverageConditionTruth::False;
        case CoverageConditionLogicalOperator::Not:
            return CoverageConditionTruth::Unknown;
        }
        return CoverageConditionTruth::Unknown;
    }

    std::optional<CoverageConditionTruth> short_circuit_result(
        const CoverageConditionLogicalOperator operation,
        const CoverageConditionTruth left) noexcept
    {
        if ((operation == CoverageConditionLogicalOperator::And
                || operation == CoverageConditionLogicalOperator::Nand)
            && left == CoverageConditionTruth::False) {
            return operation == CoverageConditionLogicalOperator::And
                ? CoverageConditionTruth::False
                : CoverageConditionTruth::True;
        }
        if ((operation == CoverageConditionLogicalOperator::Or
                || operation == CoverageConditionLogicalOperator::Nor)
            && left == CoverageConditionTruth::True) {
            return operation == CoverageConditionLogicalOperator::Or
                ? CoverageConditionTruth::True
                : CoverageConditionTruth::False;
        }
        return std::nullopt;
    }

} // namespace

CoverageConditionEvaluationResult evaluate_coverage_condition(
    const std::span<const CoverageConditionEvaluationAtom> atoms,
    const CoverageConditionAtomEvaluator& evaluate,
    const CoverageConditionEvaluationLimits limits) noexcept
{
    CoverageConditionEvaluationResult result;
    const auto reject = [&](const CoverageConditionEvaluationError error,
                            const std::size_t condition_index = 0U) {
        result.observations.clear();
        result.skipped.clear();
        result.decision_truth = CoverageConditionTruth::Unknown;
        result.error = error;
        result.condition_index = condition_index;
        return result;
    };

    try {
        if (atoms.empty()) {
            return reject(CoverageConditionEvaluationError::EmptyDecision);
        }
        if (atoms.size() > limits.maximum_atoms
            || limits.maximum_nodes == 0U) {
            return reject(CoverageConditionEvaluationError::ResourceLimit);
        }

        std::set<PointKey> point_ids;
        std::vector<bool> condition_indices(atoms.size(), false);
        std::vector<EvaluationNode> nodes(1U);
        std::size_t retained_path_steps { };
        for (std::size_t atom_index = 0U; atom_index < atoms.size();
            ++atom_index) {
            const auto& atom = atoms[atom_index];
            if (!is_code_coverage_identity_valid(atom.point)) {
                return reject(
                    CoverageConditionEvaluationError::InvalidPointIdentity,
                    atom.condition_index);
            }
            if (!point_ids.emplace(atom.point.high, atom.point.low).second) {
                return reject(
                    CoverageConditionEvaluationError::DuplicatePointIdentity,
                    atom.condition_index);
            }
            if (atom.condition_index >= atoms.size()) {
                return reject(
                    CoverageConditionEvaluationError::NonCanonicalConditionIndex,
                    atom.condition_index);
            }
            if (condition_indices[atom.condition_index]) {
                return reject(
                    CoverageConditionEvaluationError::DuplicateConditionIndex,
                    atom.condition_index);
            }
            condition_indices[atom.condition_index] = true;
            if (atom.evaluation_path.size() > limits.maximum_nesting
                || atom.evaluation_path.size()
                    > limits.maximum_path_steps - retained_path_steps) {
                return reject(CoverageConditionEvaluationError::ResourceLimit,
                    atom.condition_index);
            }
            retained_path_steps += atom.evaluation_path.size();

            std::size_t node_index { };
            for (const auto& step : atom.evaluation_path) {
                auto& node = nodes[node_index];
                if (node.atom != kMissing
                    || (node.logical_operator
                        && *node.logical_operator != step.logical_operator)) {
                    return reject(
                        CoverageConditionEvaluationError::MalformedEvaluationPath,
                        atom.condition_index);
                }
                const auto slot
                    = operand_slot(step.logical_operator, step.operand);
                if (!slot) {
                    return reject(
                        CoverageConditionEvaluationError::MalformedEvaluationPath,
                        atom.condition_index);
                }
                node.logical_operator = step.logical_operator;
                if (node.children[*slot] == kMissing) {
                    if (nodes.size() >= limits.maximum_nodes) {
                        return reject(
                            CoverageConditionEvaluationError::ResourceLimit,
                            atom.condition_index);
                    }
                    const auto child = nodes.size();
                    nodes.emplace_back();
                    nodes[node_index].children[*slot] = child;
                }
                node_index = nodes[node_index].children[*slot];
            }
            auto& leaf = nodes[node_index];
            if (leaf.atom != kMissing || leaf.logical_operator) {
                return reject(
                    CoverageConditionEvaluationError::MalformedEvaluationPath,
                    atom.condition_index);
            }
            leaf.atom = atom_index;
        }

        for (const auto& node : nodes) {
            if (!node.logical_operator) {
                if (node.atom == kMissing
                    || std::ranges::any_of(node.children,
                        [](const std::size_t child) {
                            return child != kMissing;
                        })) {
                    return reject(
                        CoverageConditionEvaluationError::MalformedEvaluationPath);
                }
                continue;
            }
            if (node.atom != kMissing) {
                return reject(
                    CoverageConditionEvaluationError::MalformedEvaluationPath);
            }
            const bool unary = *node.logical_operator
                == CoverageConditionLogicalOperator::Not;
            const bool valid_children = unary
                ? node.children[0] == kMissing
                    && node.children[1] == kMissing
                    && node.children[2] != kMissing
                : node.children[0] != kMissing
                    && node.children[1] != kMissing
                    && node.children[2] == kMissing;
            if (!valid_children) {
                return reject(
                    CoverageConditionEvaluationError::MalformedEvaluationPath);
            }
        }

        if (!evaluate) {
            return reject(CoverageConditionEvaluationError::EvaluationFailure);
        }
        result.observations.reserve(atoms.size());
        result.skipped.reserve(atoms.size());

        const auto mark_skipped = [&](const std::size_t root) {
            std::vector<std::size_t> pending { root };
            while (!pending.empty()) {
                const auto node_index = pending.back();
                pending.pop_back();
                const auto& node = nodes[node_index];
                if (node.atom != kMissing) {
                    const auto& atom = atoms[node.atom];
                    result.skipped.push_back(CoverageConditionSkippedAtom {
                        atom.point, atom.condition_index });
                    continue;
                }
                for (const auto child : node.children) {
                    if (child != kMissing) {
                        pending.push_back(child);
                    }
                }
            }
        };

        std::vector<EvaluationFrame> stack {
            EvaluationFrame { 0U, EvaluationPhase::Enter,
                CoverageConditionTruth::Unknown }
        };
        std::optional<CoverageConditionTruth> returned;
        while (!stack.empty()) {
            auto& frame = stack.back();
            const auto& node = nodes[frame.node];
            if (returned) {
                if (frame.phase == EvaluationPhase::AfterOnly) {
                    const auto value = invert(*returned);
                    stack.pop_back();
                    returned = value;
                    continue;
                }
                if (frame.phase == EvaluationPhase::AfterLeft) {
                    frame.left = *returned;
                    returned.reset();
                    const auto operation = *node.logical_operator;
                    if (const auto done
                        = short_circuit_result(operation, frame.left)) {
                        mark_skipped(node.children[1]);
                        stack.pop_back();
                        returned = *done;
                        continue;
                    }
                    frame.phase = EvaluationPhase::AfterRight;
                    stack.push_back(EvaluationFrame {
                        node.children[1], EvaluationPhase::Enter,
                        CoverageConditionTruth::Unknown });
                    continue;
                }
                if (frame.phase == EvaluationPhase::AfterRight) {
                    const auto value = combine(*node.logical_operator,
                        frame.left, *returned);
                    stack.pop_back();
                    returned = value;
                    continue;
                }
                return reject(
                    CoverageConditionEvaluationError::MalformedEvaluationPath);
            }

            if (node.atom != kMissing) {
                const auto& atom = atoms[node.atom];
                result.condition_index = atom.condition_index;
                const auto truth = evaluate(atom);
                if (!truth || !valid_truth(*truth)) {
                    return reject(
                        CoverageConditionEvaluationError::EvaluationFailure,
                        atom.condition_index);
                }
                result.observations.push_back(CoverageConditionObservation {
                    atom.point, atom.condition_index, *truth });
                stack.pop_back();
                returned = *truth;
                continue;
            }

            if (*node.logical_operator
                == CoverageConditionLogicalOperator::Not) {
                frame.phase = EvaluationPhase::AfterOnly;
                stack.push_back(EvaluationFrame {
                    node.children[2], EvaluationPhase::Enter,
                    CoverageConditionTruth::Unknown });
            } else {
                frame.phase = EvaluationPhase::AfterLeft;
                stack.push_back(EvaluationFrame {
                    node.children[0], EvaluationPhase::Enter,
                    CoverageConditionTruth::Unknown });
            }
        }

        if (!returned) {
            return reject(
                CoverageConditionEvaluationError::MalformedEvaluationPath);
        }
        std::ranges::sort(result.skipped, { },
            &CoverageConditionSkippedAtom::condition_index);
        result.decision_truth = *returned;
        result.error = CoverageConditionEvaluationError::None;
        result.condition_index = 0U;
        return result;
    } catch (...) {
        return reject(CoverageConditionEvaluationError::EvaluationFailure,
            result.condition_index);
    }
}

} // namespace fsim::runtime

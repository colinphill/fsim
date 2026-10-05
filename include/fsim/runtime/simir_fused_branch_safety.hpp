// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

namespace fsim::runtime::simir::detail {

/// Check the forward, fully reachable control-flow shape required by masked
/// activation. This remains separate from register-knownness analysis so
/// callers can preserve CFG validation when no error-policy Branch exists.
[[nodiscard]] inline bool masked_forward_control_flow_is_valid(
    const OperationList& operations)
{
    const auto operation_count = operations.size();
    if (operation_count < 3U) {
        return false;
    }
    const auto body_count = operation_count - 2U;
    if (!operation_holds<WaitSensitivity>(operations.expanded(body_count))) {
        return false;
    }
    const auto tail = operations.expanded(body_count + 1U);
    const auto* tail_jump = operation_get_if<Jump>(&tail);
    if (tail_jump == nullptr || tail_jump->target != 0U) {
        return false;
    }

    std::vector<std::uint8_t> reachable(body_count + 1U, 0U);
    reachable[0U] = 1U;
    for (std::size_t index = 0U; index < body_count; ++index) {
        if (reachable[index] == 0U) {
            return false;
        }
        const auto operation = operations.expanded(index);
        auto successors = std::array<std::size_t, 2U> { index + 1U, 0U };
        auto successor_count = std::size_t { 1U };
        if (const auto* branch = operation_get_if<Branch>(&operation)) {
            successors[0U] = branch->when_true;
            successors[1U] = branch->when_false;
            successor_count = 2U;
        } else if (const auto* jump = operation_get_if<Jump>(&operation)) {
            successors[0U] = jump->target;
        }
        for (std::size_t successor = 0U;
             successor < successor_count; ++successor) {
            const auto target = successors[successor];
            if (target <= index || target > body_count) {
                return false;
            }
            reachable[target] = 1U;
        }
    }
    return reachable[body_count] != 0U;
}

/// Prove that every error-policy Branch in a forward masked-process body sees
/// a known Logic4 condition. Bodies without an error-policy Branch need no
/// knownness analysis. The caller must still perform its ordinary CFG,
/// operation, width, value-kind, and effect validation. Unknown or unsupported
/// operations fail closed when they could affect an error-branch proof.
[[nodiscard]] inline bool masked_branch_conditions_are_proven_known(
    const OperationList& operations, const std::size_t register_count)
{
    const auto operation_count = operations.size();
    if (operation_count < 2U) {
        return false;
    }
    const auto body_count = operation_count - 2U;

    auto has_error_policy_branch = false;
    for (std::size_t index = 0U; index < body_count; ++index) {
        const auto operation = operations.expanded(index);
        const auto* branch = operation_get_if<Branch>(&operation);
        if (branch == nullptr) {
            continue;
        }
        if (branch->unknown_policy == UnknownBranchPolicy::error) {
            has_error_policy_branch = true;
        } else if (branch->unknown_policy != UnknownBranchPolicy::when_false) {
            return false;
        }
    }
    if (!has_error_policy_branch) {
        // CFG/effect and enum validation belongs to each caller. Without an
        // error-policy branch, no knownness fact can prevent a runtime error.
        return true;
    }
    if (operation_count < 3U || register_count == 0U
        || !operation_holds<WaitSensitivity>(operations.expanded(body_count))) {
        return false;
    }
    const auto tail = operations.expanded(body_count + 1U);
    const auto* tail_jump = operation_get_if<Jump>(&tail);
    if (tail_jump == nullptr || tail_jump->target != 0U) {
        return false;
    }

    struct State {
        std::vector<std::uint8_t> definitely_known;
    };
    std::vector<std::optional<State>> incoming(body_count + 1U);
    incoming[0U] = State {
        std::vector<std::uint8_t>(register_count, 0U) };

    const auto register_is_known = [](const State& state,
                                      const RegisterId id) {
        return id < state.definitely_known.size()
            && state.definitely_known[id] != 0U;
    };
    const auto set_known = [](State& state, const RegisterId id,
                              const bool known) {
        if (id >= state.definitely_known.size()) {
            return false;
        }
        state.definitely_known[id] = static_cast<std::uint8_t>(known);
        return true;
    };
    const auto known_constant = [](const PackedLogic4& value) {
        return !value.empty() && !value.is_logic9()
            && std::ranges::all_of(value.bval_words(),
                [](const std::uint64_t word) { return word == 0U; });
    };
    const auto merge = [](std::optional<State>& target,
                          const State& source) {
        if (!target) {
            target = source;
            return;
        }
        for (std::size_t id = 0U;
             id < target->definitely_known.size(); ++id) {
            target->definitely_known[id]
                &= source.definitely_known[id];
        }
    };

    for (std::size_t index = 0U; index < body_count; ++index) {
        if (!incoming[index]) {
            return false;
        }
        auto state = *incoming[index];
        const auto operation = operations.expanded(index);
        auto successors = std::array<std::size_t, 2U> { index + 1U, 0U };
        auto successor_count = std::size_t { 1U };
        const auto accepted = visit_operation([&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, DebugPoint>
                || std::is_same_v<Type, WriteUpdate>
                || std::is_same_v<Type, WriteUpdateSlice>
                || std::is_same_v<Type, WriteProjected>) {
                return true;
            } else if constexpr (std::is_same_v<Type, LoadConstant>) {
                return set_known(state, value.destination,
                    known_constant(value.value));
            } else if constexpr (std::is_same_v<Type, ReadSignal>) {
                return set_known(state, value.destination, false);
            } else if constexpr (std::is_same_v<Type, CopyRegister>
                || std::is_same_v<Type, Extract>) {
                if (value.source >= state.definitely_known.size()) {
                    return false;
                }
                return set_known(state, value.destination,
                    register_is_known(state, value.source));
            } else if constexpr (std::is_same_v<Type, Concatenate>) {
                if (value.operands.empty()
                    || std::ranges::any_of(value.operands,
                        [&](const RegisterId id) {
                            return id >= state.definitely_known.size();
                        })) {
                    return false;
                }
                const auto operands_known = std::ranges::all_of(
                    value.operands, [&](const RegisterId id) {
                        return register_is_known(state, id);
                    });
                return set_known(state, value.destination, operands_known);
            } else if constexpr (std::is_same_v<Type, ConditionalSelect>) {
                if (value.condition >= state.definitely_known.size()
                    || value.when_true >= state.definitely_known.size()
                    || value.when_false >= state.definitely_known.size()) {
                    return false;
                }
                const auto result_known
                    = register_is_known(state, value.condition)
                    && register_is_known(state, value.when_true)
                    && register_is_known(state, value.when_false);
                return set_known(state, value.destination, result_known);
            } else if constexpr (std::is_same_v<Type, Binary>) {
                if (value.lhs >= state.definitely_known.size()
                    || value.rhs >= state.definitely_known.size()) {
                    return false;
                }
                const bool known = value.operation == BinaryOperator::case_equal
                    || ((value.operation == BinaryOperator::bit_and
                            || value.operation == BinaryOperator::bit_or
                            || value.operation == BinaryOperator::bit_xor
                            || value.operation == BinaryOperator::equal)
                        && register_is_known(state, value.lhs)
                        && register_is_known(state, value.rhs));
                const bool supported
                    = value.operation == BinaryOperator::case_equal
                    || value.operation == BinaryOperator::bit_and
                    || value.operation == BinaryOperator::bit_or
                    || value.operation == BinaryOperator::bit_xor
                    || value.operation == BinaryOperator::equal;
                return supported
                    && set_known(state, value.destination, known);
            } else if constexpr (std::is_same_v<Type, Reduction>) {
                if (value.source >= state.definitely_known.size()) {
                    return false;
                }
                const bool supported
                    = value.operation == ReductionOperator::bit_and
                    || value.operation == ReductionOperator::bit_or
                    || value.operation == ReductionOperator::bit_xor;
                return supported
                    && set_known(state, value.destination,
                        register_is_known(state, value.source));
            } else if constexpr (std::is_same_v<Type, Branch>) {
                if (value.condition >= state.definitely_known.size()) {
                    return false;
                }
                if (value.unknown_policy == UnknownBranchPolicy::error
                    && !register_is_known(state, value.condition)) {
                    return false;
                }
                if (value.unknown_policy != UnknownBranchPolicy::error
                    && value.unknown_policy
                        != UnknownBranchPolicy::when_false) {
                    return false;
                }
                successors[0U] = value.when_true;
                successors[1U] = value.when_false;
                successor_count = 2U;
                return true;
            } else if constexpr (std::is_same_v<Type, Jump>) {
                successors[0U] = value.target;
                return true;
            } else {
                return false;
            }
        }, operation);
        if (!accepted) {
            return false;
        }
        for (std::size_t successor = 0U;
             successor < successor_count; ++successor) {
            const auto target = successors[successor];
            if (target <= index || target > body_count) {
                return false;
            }
            merge(incoming[target], state);
        }
    }
    return incoming[body_count].has_value();
}

} // namespace fsim::runtime::simir::detail

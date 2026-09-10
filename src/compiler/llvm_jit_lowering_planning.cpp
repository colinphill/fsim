// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"
#include "llvm_jit_lowering_context.hpp"

#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

namespace fsim::compiler::llvm_detail {
using namespace runtime::simir;

[[nodiscard]] NativeCallablePlan analyze_native_callables(
    const Process& process)
{
    NativeCallablePlan result {
        std::vector<bool>(process.operations.size()),
        std::vector<bool>(process.operations.size()),
        std::vector<bool>(process.operations.size()),
        std::vector<std::optional<InstructionIndex>>(
            process.operations.size()),
        { },
        { }
    };
    std::vector<std::optional<std::uint32_t>> call_identities(
        process.operations.size());
    std::map<std::uint32_t, InstructionIndex> targets;
    std::set<std::uint32_t> conflicting_identities;

    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        const auto* push
            = fsim::runtime::simir::operation_get_if<CallableFramePush>(
                &process.operations[index]);
        if (push == nullptr) {
            continue;
        }
        if (!push->native_isolated) {
            conflicting_identities.insert(push->identity);
        }
        for (std::size_t cursor = index + 1U;
            cursor < process.operations.size(); ++cursor) {
            if (fsim::runtime::simir::operation_holds<CallableFramePush>(
                    process.operations[cursor])) {
                break;
            }
            const auto* call = fsim::runtime::simir::operation_get_if<Call>(
                &process.operations[cursor]);
            if (call == nullptr) {
                continue;
            }
            if (call->stack.capacity == 0) {
                call_identities[cursor] = push->identity;
                const auto [found, inserted]
                    = targets.emplace(push->identity, call->target);
                if (!inserted && found->second != call->target) {
                    conflicting_identities.insert(push->identity);
                }
            }
            break;
        }
    }

    std::map<std::uint32_t, InstructionIndex> returns;
    std::map<std::uint32_t, std::set<std::uint32_t>> dependencies;
    std::set<std::uint32_t> unknown_dependencies;
    for (const auto& [identity, target] : targets) {
        if (target >= process.operations.size()) {
            conflicting_identities.insert(identity);
            continue;
        }
        for (std::size_t cursor = target;
            cursor < process.operations.size(); ++cursor) {
            const auto* return_operation
                = fsim::runtime::simir::operation_get_if<Return>(
                    &process.operations[cursor]);
            if (return_operation != nullptr
                && return_operation->stack.capacity == 0) {
                returns.emplace(
                    identity, static_cast<InstructionIndex>(cursor));
                break;
            }
            const auto* call = fsim::runtime::simir::operation_get_if<Call>(
                &process.operations[cursor]);
            if (call != nullptr && call->stack.capacity == 0) {
                if (call_identities[cursor]) {
                    dependencies[identity].insert(*call_identities[cursor]);
                } else {
                    unknown_dependencies.insert(identity);
                }
            }
        }
        if (!returns.contains(identity)) {
            conflicting_identities.insert(identity);
        }
    }

    const auto reaches = [&](const std::uint32_t source,
                             const std::uint32_t target,
                             auto&& self,
                             std::set<std::uint32_t>& visited) -> bool {
        if (!visited.insert(source).second) {
            return false;
        }
        const auto found = dependencies.find(source);
        if (found == dependencies.end()) {
            return false;
        }
        for (const auto dependency : found->second) {
            if (dependency == target
                || self(dependency, target, self, visited)) {
                return true;
            }
        }
        return false;
    };

    std::set<std::uint32_t> safe_identities;
    for (const auto& [identity, target] : targets) {
        (void)target;
        std::set<std::uint32_t> visited;
        const bool recursive = reaches(
            identity, identity, reaches, visited);
        if (!recursive && !conflicting_identities.contains(identity)
            && !unknown_dependencies.contains(identity)) {
            safe_identities.insert(identity);
        }
    }
    if (safe_identities.size()
        > FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V1) {
        safe_identities.clear();
    }
    if (std::ranges::any_of(
            process.operations, [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                           VhdlEnvironmentCallPath>(operation)
                    || fsim::runtime::simir::operation_holds<
                           VhdlEnvironmentGetCallPath>(operation);
            })) {
        // GET_CALL_PATH observes the complete language-level caller chain.
        // Keep that rare process on the runtime-owned call stack instead of
        // hiding callable frames in LLVM-native calls.
        safe_identities.clear();
    }

    std::map<InstructionIndex, std::set<InstructionIndex>> return_entries;
    for (const auto identity : safe_identities) {
        return_entries[returns.at(identity)].insert(targets.at(identity));
    }
    std::erase_if(safe_identities, [&](const std::uint32_t identity) {
        return return_entries.at(returns.at(identity)).size() != 1U;
    });

    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        if (call_identities[index]
            && safe_identities.contains(*call_identities[index])) {
            result.call_operations[index] = true;
            const auto& call = fsim::runtime::simir::operation_get<Call>(
                process.operations[index]);
            result.return_targets.push_back(call.return_target);
        }
        fsim::runtime::simir::visit_operation(
            [&](const auto& operation) {
                using OperationType = std::decay_t<decltype(operation)>;
                if constexpr (
                    std::is_same_v<OperationType, CallableFramePush>
                    || std::is_same_v<OperationType, CallableFramePop>) {
                    result.frame_operations[index]
                        = safe_identities.contains(operation.identity);
                }
            },
            process.operations[index]);
    }
    for (const auto identity : safe_identities) {
        const auto returned = returns.at(identity);
        result.return_operations[returned] = true;
        result.return_entries[returned] = targets.at(identity);
    }
    for (const auto& [identity, target] : targets) {
        const auto returned = returns.find(identity);
        if (returned != returns.end()
            && !conflicting_identities.contains(identity)) {
            result.regions.emplace_back(target, returned->second);
        }
    }
    std::ranges::sort(result.return_targets);
    result.return_targets.erase(
        std::ranges::unique(result.return_targets).begin(),
        result.return_targets.end());
    return result;
}

[[nodiscard]] std::vector<InstructionIndex> optimized_resume_entries(
    const Process& process,
    const NativeCallablePlan& native_callables,
    const bool debug_instrumentation)
{
    std::vector<bool> selected(process.operations.size());
    const auto select = [&](const InstructionIndex instruction) {
        if (instruction < selected.size()) {
            selected[instruction] = true;
        }
    };
    select(0);

    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        const auto next_instruction = static_cast<InstructionIndex>(index + 1U);
        const auto& operation = process.operations[index];
        const auto* read = fsim::runtime::simir::operation_get_if<ReadSignal>(
            &operation);
        const auto* call = fsim::runtime::simir::operation_get_if<Call>(
            &operation);
        const auto* return_operation
            = fsim::runtime::simir::operation_get_if<Return>(&operation);
        const bool dynamic_call = call != nullptr && call->stack.capacity == 0
            && !native_callables.call_operations[index];
        const bool dynamic_return
            = return_operation != nullptr
            && return_operation->stack.capacity == 0
            && !native_callables.return_operations[index];
        const bool callable_frame
            = (fsim::runtime::simir::operation_holds<CallableFramePush>(operation)
                  || fsim::runtime::simir::operation_holds<CallableFramePop>(operation))
            && !native_callables.frame_operations[index];
        const bool host_boundary = is_resume_boundary(operation)
            || (debug_instrumentation
                && fsim::runtime::simir::operation_holds<DebugPoint>(
                    operation))
            || fsim::runtime::simir::operation_holds<Stop>(operation)
            || (read != nullptr
                && read->kind != runtime::simir::SignalReadKind::current)
            || fsim::runtime::simir::operation_holds<PlusArgSelect>(operation)
            || fsim::runtime::simir::operation_holds<
                CoverageDatabaseControl>(operation)
            || fsim::runtime::simir::operation_holds<RandomDistribution>(
                operation);
        if (host_boundary || callable_frame) {
            select(next_instruction);
        }
        if (dynamic_call) {
            select(call->target);
            select(call->return_target);
        }
        if (dynamic_return) {
            for (const auto& candidate : process.operations) {
                const auto* candidate_call
                    = fsim::runtime::simir::operation_get_if<Call>(&candidate);
                if (candidate_call != nullptr
                    && candidate_call->stack.capacity == 0) {
                    select(candidate_call->return_target);
                }
            }
        }
        if (const auto* fork
            = fsim::runtime::simir::operation_get_if<Fork>(&operation)) {
            for (const auto branch : fork->branches) {
                select(branch);
            }
        }
        if (const auto* disable
            = fsim::runtime::simir::operation_get_if<DisableBlock>(&operation)) {
            select(disable->end);
        }
    }

    std::vector<InstructionIndex> result;
    result.reserve(selected.size());
    for (std::size_t index = 0; index < selected.size(); ++index) {
        if (selected[index]) {
            result.push_back(static_cast<InstructionIndex>(index));
        }
    }
    return result;
}

ProcessLoweringPlan make_process_lowering_plan(
    const Process& process,
    const bool debug_instrumentation)
{
    const auto native_callables = analyze_native_callables(process);
    auto resume_entries = optimized_resume_entries(
        process, native_callables, debug_instrumentation);
    ProcessLoweringPlan full {
        std::vector<bool>(process.operations.size(), true),
        std::move(resume_entries),
        false
    };
    constexpr std::size_t minimum_partial_process_operations = 4096U;
    constexpr std::size_t maximum_partial_process_operations = 2048U;
    if (debug_instrumentation
        || process.operations.size() < minimum_partial_process_operations
        || full.entry_points.size() < 2U
        || std::ranges::any_of(
            process.operations,
            [](const Operation& operation) {
                if (is_resume_boundary(operation)) {
                    return !fsim::runtime::simir::operation_holds<
                               WaitSensitivity>(operation)
                        && !fsim::runtime::simir::operation_holds<
                            WaitForever>(operation);
                }
                return fsim::runtime::simir::operation_holds<Fork>(operation)
                    || fsim::runtime::simir::operation_holds<ForkEnd>(operation)
                    || fsim::runtime::simir::operation_holds<DisableBlock>(
                        operation);
            })) {
        return full;
    }

    std::vector<bool> selected(process.operations.size());
    std::vector<InstructionIndex> pending;
    for (const auto entry : full.entry_points) {
        if (entry != 0U) {
            pending.push_back(entry);
        }
    }
    while (!pending.empty()) {
        const auto instruction = pending.back();
        pending.pop_back();
        if (instruction >= process.operations.size()
            || selected[instruction]) {
            continue;
        }
        selected[instruction] = true;
        const auto& operation = process.operations[instruction];
        const auto select = [&](const InstructionIndex successor) {
            if (successor < process.operations.size()
                && !selected[successor]) {
                pending.push_back(successor);
            }
        };
        if (is_resume_boundary(operation)
            || fsim::runtime::simir::operation_holds<Halt>(operation)
            || fsim::runtime::simir::operation_holds<Stop>(operation)
            || fsim::runtime::simir::operation_holds<Return>(operation)
            || fsim::runtime::simir::operation_holds<ForkEnd>(operation)) {
            continue;
        }
        if (const auto* jump
            = fsim::runtime::simir::operation_get_if<Jump>(&operation)) {
            select(jump->target);
        } else if (const auto* call
            = fsim::runtime::simir::operation_get_if<Call>(&operation)) {
            select(call->target);
            select(call->return_target);
        } else if (const auto* branch
            = fsim::runtime::simir::operation_get_if<Branch>(&operation)) {
            select(branch->when_true);
            select(branch->when_false);
        } else {
            select(static_cast<InstructionIndex>(instruction + 1U));
        }
    }

    const auto selected_count = static_cast<std::size_t>(
        std::ranges::count(selected, true));
    if (selected[0]
        || selected_count == 0U
        || selected_count > maximum_partial_process_operations
        || selected_count * 2U >= process.operations.size()) {
        return full;
    }
    std::vector<InstructionIndex> partial_entries;
    for (const auto entry : full.entry_points) {
        if (entry < selected.size() && selected[entry]) {
            partial_entries.push_back(entry);
        }
    }
    if (partial_entries.empty()) {
        return full;
    }
    return {
        std::move(selected),
        std::move(partial_entries),
        true
    };
}

[[nodiscard]] std::vector<InstructionIndex> static_return_targets(
    const Process& process)
{
    std::vector<InstructionIndex> result;
    for (const auto& operation : process.operations) {
        const auto* call
            = fsim::runtime::simir::operation_get_if<Call>(&operation);
        if (call != nullptr && call->stack.capacity != 0) {
            result.push_back(call->return_target);
        }
    }
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}

[[nodiscard]] std::size_t coalesce_linear_blocks(llvm::Function& function)
{
    std::size_t merged = 0;
    bool changed = false;
    do {
        changed = false;
        for (auto block = function.begin(); block != function.end();) {
            auto* candidate = &*block++;
            if (candidate == &function.getEntryBlock()) {
                continue;
            }
            if (llvm::MergeBlockIntoPredecessor(candidate)) {
                ++merged;
                changed = true;
            }
        }
    } while (changed);
    return merged;
}


} // namespace fsim::compiler::llvm_detail

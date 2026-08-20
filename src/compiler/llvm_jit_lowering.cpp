// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"
#include "llvm_jit_lowering_internal.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
#include <llvm/IR/Constants.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/CodeExtractor.h>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>
namespace fsim::compiler::llvm_detail {
using runtime::Logic9;
using runtime::simir::Assert;
using runtime::simir::Binary;
using runtime::simir::BinaryOperator;
using runtime::simir::Branch;
using runtime::simir::Call;
using runtime::simir::CallableFramePop;
using runtime::simir::CallableFramePush;
using runtime::simir::ClassOperationGroup;
using runtime::simir::Concatenate;
using runtime::simir::ConditionalSelect;
using runtime::simir::ConvertToTwoState;
using runtime::simir::CopyRegister;
using runtime::simir::CountBits;
using runtime::simir::CountOnes;
using runtime::simir::CoverageDatabaseControl;
using runtime::simir::CoverageQuery;
using runtime::simir::CoverageSample;
using runtime::simir::DebugPoint;
using runtime::simir::DisableBlock;
using runtime::simir::DisableFork;
using runtime::simir::Display;
using runtime::simir::DynamicExtract;
using runtime::simir::DynamicIndex;
using runtime::simir::DynamicInsert;
using runtime::simir::DynamicPartIndex;
using runtime::simir::DynamicPartInsert;
using runtime::simir::DynamicPartSelect;
using runtime::simir::EdgeKind;
using runtime::simir::EventAlias;
using runtime::simir::EventTriggered;
using runtime::simir::Extract;
using runtime::simir::ForceSignalSlice;
using runtime::simir::Fork;
using runtime::simir::ForkEnd;
using runtime::simir::FormatDisplay;
using runtime::simir::Halt;
using runtime::simir::Insert;
using runtime::simir::InstructionIndex;
using runtime::simir::IntegerBinary;
using runtime::simir::IntegerBinaryOperator;
using runtime::simir::IntegerCheck;
using runtime::simir::IntegerUnary;
using runtime::simir::IntegerUnaryOperator;
using runtime::simir::Jump;
using runtime::simir::LoadConstant;
using runtime::simir::LogicalBinary;
using runtime::simir::LogicalBinaryOperator;
using runtime::simir::LogicalNot;
using runtime::simir::MailboxCreate;
using runtime::simir::MailboxGet;
using runtime::simir::MailboxNum;
using runtime::simir::MailboxPut;
using runtime::simir::MonitorControl;
using runtime::simir::MonitorInstall;
using runtime::simir::MonitorValueKind;
using runtime::simir::Operation;
using runtime::simir::operation_group_contains_v;
using runtime::simir::Pause;
using runtime::simir::PlaEvaluate;
using runtime::simir::PlusArgSelect;
using runtime::simir::Process;
using runtime::simir::ProcessAwait;
using runtime::simir::ProcessCompleted;
using runtime::simir::ProcessGetRandState;
using runtime::simir::ProcessKill;
using runtime::simir::ProcessResume;
using runtime::simir::ProcessSelf;
using runtime::simir::ProcessSetRandState;
using runtime::simir::ProcessSrandom;
using runtime::simir::ProcessStatusQuery;
using runtime::simir::ProcessSuspend;
using runtime::simir::RandomDistribution;
using runtime::simir::RandomValue;
using runtime::simir::ReadSignal;
using runtime::simir::ReadSimulationTime;
using runtime::simir::Reduction;
using runtime::simir::ReductionOperator;
using runtime::simir::RegisterId;
using runtime::simir::ReleaseSignalSlice;
using runtime::simir::Report;
using runtime::simir::Return;
using runtime::simir::SemaphoreCreate;
using runtime::simir::SemaphoreGet;
using runtime::simir::SemaphorePut;
using runtime::simir::Shift;
using runtime::simir::ShiftOperator;
using runtime::simir::SignalActive;
using runtime::simir::SignalDriving;
using runtime::simir::SignalDrivingValue;
using runtime::simir::SignalEvent;
using runtime::simir::SignalLastActive;
using runtime::simir::SignalLastEvent;
using runtime::simir::SignalLastValue;
using runtime::simir::StochasticQueueOperation;
using runtime::simir::Stop;
using runtime::simir::StringReport;
using runtime::simir::SystemCommand;
using runtime::simir::TimeDisplay;
using runtime::simir::TimeFormatControl;
using runtime::simir::UnaryNot;
using runtime::simir::UnknownBranchPolicy;
using runtime::simir::ValueKind;
using runtime::simir::VcdControl;
using runtime::simir::VitalDelay;
using runtime::simir::VitalTimingCheck;
using runtime::simir::WaitFor;
using runtime::simir::WaitForever;
using runtime::simir::WaitFork;
using runtime::simir::WaitOn;
using runtime::simir::WaitOrder;
using runtime::simir::WaitPla;
using runtime::simir::WaitRegion;
using runtime::simir::WaitSensitivity;
using runtime::simir::WriteAfter;
using runtime::simir::WriteAfterDynamicPartSlice;
using runtime::simir::WriteAfterDynamicSlice;
using runtime::simir::WriteAfterSlice;
using runtime::simir::WriteBlocking;
using runtime::simir::WriteBlockingDynamicPartSlice;
using runtime::simir::WriteBlockingDynamicSlice;
using runtime::simir::WriteBlockingSlice;
using runtime::simir::WriteInertial;
using runtime::simir::WriteInertialDynamicPartSlice;
using runtime::simir::WriteInertialDynamicSlice;
using runtime::simir::WriteInertialSlice;
using runtime::simir::WriteProjected;
using runtime::simir::WriteProjectedDynamicSlice;
using runtime::simir::WriteProjectedSlice;
using runtime::simir::WriteProjectedWaveform;
using runtime::simir::WriteProjectedWaveformDynamicSlice;
using runtime::simir::WriteProjectedWaveformSlice;
using runtime::simir::WriteUpdate;
using runtime::simir::WriteUpdateDynamicPartSlice;
using runtime::simir::WriteUpdateDynamicSlice;
using runtime::simir::WriteUpdateSlice;
using runtime::simir::Yield;

struct NativeCallablePlan {
    std::vector<bool> call_operations;
    std::vector<bool> return_operations;
    std::vector<bool> frame_operations;
    std::vector<std::optional<InstructionIndex>> return_entries;
    std::vector<InstructionIndex> return_targets;
    std::vector<std::pair<InstructionIndex, InstructionIndex>> regions;
};

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

void lower_process(llvm::Module& module, const std::string& symbol,
    const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const std::span<const runtime::simir::SignalId> direct_read_signals,
    const std::span<const runtime::simir::SignalId> direct_update_signals,
    const ValidatedProcess& validated,
    const JitOptimizationLevel optimization,
    const bool debug_instrumentation,
    const bool require_direct_update_slots)
{
    auto& context = module.getContext();
    auto* i32 = llvm::Type::getInt32Ty(context);
    auto* i64 = llvm::Type::getInt64Ty(context);
    auto* pointer = llvm::PointerType::getUnqual(context);
    auto* runtime_type = create_jit_runtime_type(context);
    const auto* runtime_layout
        = module.getDataLayout().getStructLayout(runtime_type);
    const auto require_runtime_member = [&](const unsigned member,
                                            const std::size_t offset) {
        if (member >= runtime_type->getNumElements()
            || runtime_layout->getElementOffset(member) != offset) {
            throw LlvmJitError(
                "generated LLVM runtime layout disagrees with the native ABI");
        }
    };
    require_runtime_member(
        83U, offsetof(fsim_jit_runtime_v1, direct_update_slots));
    require_runtime_member(
        97U, offsetof(fsim_jit_runtime_v1, direct_update_active_words));
    require_runtime_member(
        100U, offsetof(fsim_jit_runtime_v1, static_trigger_mask));
    require_runtime_member(
        101U, offsetof(fsim_jit_runtime_v1, read_signal_dynamic_part));
    require_runtime_member(
        102U,
        offsetof(fsim_jit_runtime_v1, direct_wide_signal_logic9_plane2));
    require_runtime_member(
        103U,
        offsetof(fsim_jit_runtime_v1, direct_wide_signal_logic9_plane3));
    require_runtime_member(
        104U, offsetof(fsim_jit_runtime_v1, direct_signal_logic9_plane0));
    require_runtime_member(
        105U, offsetof(fsim_jit_runtime_v1, direct_signal_logic9_plane1));
    require_runtime_member(
        106U, offsetof(fsim_jit_runtime_v1, direct_signal_logic9_plane2));
    require_runtime_member(
        107U, offsetof(fsim_jit_runtime_v1, direct_signal_logic9_plane3));
    auto* direct_update_slot_type = llvm::StructType::create(
        context,
        { i64, i64, i64, i64, i64, i32, i32,
            pointer, pointer, pointer, i32, i32 },
        "fsim_jit_update_slot_v1");
    auto* native_return_stack_type = llvm::ArrayType::get(
        i32, FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V1);
    auto* frame_type = llvm::StructType::create(
        context,
        { i32, i32, i64, i64, i32, i32, i32, i32, pointer, pointer,
            pointer, pointer, pointer, i32, i32, native_return_stack_type },
        "fsim_jit_frame_v1");
    auto* result_type = llvm::StructType::create(
        context, { i32, i32, i32, i32, i64 },
        "fsim_jit_resume_result_v1");
    auto* function_type = llvm::FunctionType::get(i32, { pointer, pointer, pointer }, false);
    auto* function = llvm::Function::Create(
        function_type, llvm::Function::ExternalLinkage, symbol, module);
    function->setCallingConv(llvm::CallingConv::C);
    function->getArg(0)->setName("runtime");
    function->getArg(1)->setName("frame");
    function->getArg(2)->setName("result");
    auto* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* runtime_argument = function->getArg(0);
    auto* frame_argument = function->getArg(1);
    auto* result_argument = function->getArg(2);
    auto* context_pointer = builder.CreateLoad(
        pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 2),
        "context");
    auto* read_callback = builder.CreateLoad(
        pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 3),
        "read_signal");
    auto* write_callback = builder.CreateLoad(
        pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 4),
        "write_signal");
    auto* assert_callback = builder.CreateLoad(
        pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 5),
        "assert_failed");
    llvm::Value* direct_update_slots = nullptr;
    llvm::Value* direct_update_active_words = nullptr;
    llvm::Value* static_trigger_mask = nullptr;
    if (!debug_instrumentation
        && !process.static_trigger_regions.empty()) {
        static_trigger_mask = builder.CreateLoad(
            i64,
            builder.CreateStructGEP(runtime_type, runtime_argument, 100),
            "static.trigger.mask");
    }
    if (!direct_update_signals.empty()) {
        direct_update_slots = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 83),
            "direct_update_slots");
        direct_update_active_words = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 97),
            "direct_update_active_words");
    }
    llvm::Value* direct_signal_aval = nullptr;
    llvm::Value* direct_signal_bval = nullptr;
    llvm::Value* direct_signal_logic9_plane0 = nullptr;
    llvm::Value* direct_signal_logic9_plane1 = nullptr;
    llvm::Value* direct_signal_logic9_plane2 = nullptr;
    llvm::Value* direct_signal_logic9_plane3 = nullptr;
    llvm::Value* direct_read_signal_map = nullptr;
    llvm::Value* direct_read_signal_count = nullptr;
    llvm::Value* direct_signal_count = nullptr;
    llvm::Value* direct_wide_signal_aval = nullptr;
    llvm::Value* direct_wide_signal_bval = nullptr;
    llvm::Value* direct_wide_signal_logic9_plane2 = nullptr;
    llvm::Value* direct_wide_signal_logic9_plane3 = nullptr;
    llvm::Value* direct_wide_signal_offsets = nullptr;
    llvm::Value* direct_wide_signal_offset_count = nullptr;
    llvm::Value* direct_wide_word_count = nullptr;
    if (!direct_read_signals.empty() || !direct_update_signals.empty()) {
        direct_signal_aval = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 86),
            "direct_signal_aval");
        direct_signal_bval = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 87),
            "direct_signal_bval");
        direct_signal_logic9_plane0 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 104),
            "direct_signal_logic9_plane0");
        direct_signal_logic9_plane1 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 105),
            "direct_signal_logic9_plane1");
        direct_signal_logic9_plane2 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 106),
            "direct_signal_logic9_plane2");
        direct_signal_logic9_plane3 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 107),
            "direct_signal_logic9_plane3");
        direct_signal_count = builder.CreateLoad(
            i32,
            builder.CreateStructGEP(runtime_type, runtime_argument, 90),
            "direct_signal_count");
        if (!direct_read_signals.empty()) {
            direct_read_signal_map = builder.CreateLoad(
                pointer,
                builder.CreateStructGEP(runtime_type, runtime_argument, 88),
                "direct_read_signals");
            direct_read_signal_count = builder.CreateLoad(
                i32,
                builder.CreateStructGEP(runtime_type, runtime_argument, 89),
                "direct_read_signal_count");
        }
    }
    if (validated.uses_wide_signal_read
        && !direct_read_signals.empty()) {
        direct_wide_signal_aval = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 92),
            "direct_wide_signal_aval");
        direct_wide_signal_bval = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 93),
            "direct_wide_signal_bval");
        direct_wide_signal_logic9_plane2 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 102),
            "direct_wide_signal_logic9_plane2");
        direct_wide_signal_logic9_plane3 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 103),
            "direct_wide_signal_logic9_plane3");
        direct_wide_signal_offsets = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 94),
            "direct_wide_signal_offsets");
        direct_wide_signal_offset_count = builder.CreateLoad(
            i32,
            builder.CreateStructGEP(runtime_type, runtime_argument, 95),
            "direct_wide_signal_offset_count");
        direct_wide_word_count = builder.CreateLoad(
            i32,
            builder.CreateStructGEP(runtime_type, runtime_argument, 96),
            "direct_wide_word_count");
    }
    llvm::Value* write_update_callback = nullptr;
    if (validated.uses_write_update) {
        write_update_callback = builder.CreateLoad(
            pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 6),
            "write_update");
    }
    llvm::Value* write_after_callback = nullptr;
    if (validated.uses_write_after) {
        write_after_callback = builder.CreateLoad(
            pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 7),
            "write_after");
    }
    llvm::Value* write_blocking_slice_callback = nullptr;
    if (validated.uses_write_blocking_slice) {
        write_blocking_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 10),
            "write_signal_slice");
    }
    llvm::Value* write_update_slice_callback = nullptr;
    if (validated.uses_write_update_slice) {
        write_update_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 11),
            "write_update_slice");
    }
    llvm::Value* write_after_slice_callback = nullptr;
    if (validated.uses_write_after_slice) {
        write_after_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 12),
            "write_after_slice");
    }
    llvm::Value* force_signal_slice_callback = nullptr;
    llvm::Value* force_signal_slice_logic9_callback = nullptr;
    if (validated.uses_force_signal_slice) {
        force_signal_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 63),
            "force_signal_slice");
        if (validated.uses_logic9) {
            force_signal_slice_logic9_callback = builder.CreateLoad(
                pointer,
                builder.CreateStructGEP(
                    runtime_type, runtime_argument, 64),
                "force_signal_slice_logic9");
        }
    }
    llvm::Value* release_signal_slice_callback = nullptr;
    if (validated.uses_release_signal_slice) {
        release_signal_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 65),
            "release_signal_slice");
    }
    llvm::Value* force_driver_signal_slice_callback = nullptr;
    llvm::Value* force_driver_signal_slice_logic9_callback = nullptr;
    if (validated.uses_force_driver_signal_slice) {
        force_driver_signal_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 73),
            "force_driver_signal_slice");
        if (validated.uses_logic9) {
            force_driver_signal_slice_logic9_callback = builder.CreateLoad(
                pointer,
                builder.CreateStructGEP(
                    runtime_type, runtime_argument, 74),
                "force_driver_signal_slice_logic9");
        }
    }
    llvm::Value* release_driver_signal_slice_callback = nullptr;
    if (validated.uses_release_driver_signal_slice) {
        release_driver_signal_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 75),
            "release_driver_signal_slice");
    }
    llvm::Value* runtime_flags = nullptr;
    if (validated.uses_debug_points && debug_instrumentation) {
        runtime_flags = builder.CreateLoad(
            i32, builder.CreateStructGEP(runtime_type, runtime_argument, 8),
            "runtime.flags");
    }
    llvm::Value* signal_event_callback = nullptr;
    if (validated.uses_signal_event) {
        signal_event_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 13),
            "signal_event");
    }
    llvm::Value* signal_last_value_callback = nullptr;
    if (validated.uses_signal_last_value) {
        signal_last_value_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 14),
            "signal_last_value");
    }
    llvm::Value* signal_last_event_callback = nullptr;
    if (validated.uses_signal_last_event) {
        signal_last_event_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 15),
            "signal_last_event");
    }
    llvm::Value* signal_active_callback = nullptr;
    if (validated.uses_signal_active) {
        signal_active_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 16),
            "signal_active");
    }
    llvm::Value* signal_last_active_callback = nullptr;
    if (validated.uses_signal_last_active) {
        signal_last_active_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 66),
            "signal_last_active");
    }
    llvm::Value* signal_driving_callback = nullptr;
    if (validated.uses_signal_driving) {
        signal_driving_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 67),
            "signal_driving");
    }
    llvm::Value* signal_driving_value_callback = nullptr;
    llvm::Value* signal_driving_value_logic9_callback = nullptr;
    if (validated.uses_signal_driving_value) {
        signal_driving_value_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 68),
            "signal_driving_value");
        signal_driving_value_logic9_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 69),
            "signal_driving_value_logic9");
    }
    llvm::Value* read_simulation_time_callback = nullptr;
    if (validated.uses_simulation_time) {
        read_simulation_time_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 70),
            "read_simulation_time");
    }
    llvm::Value* vital_timing_check_callback = nullptr;
    if (validated.uses_vital_timing) {
        vital_timing_check_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 71),
            "vital_timing_check");
    }
    llvm::Value* vital_delay_callback = nullptr;
    if (validated.uses_vital_delay) {
        vital_delay_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(runtime_type, runtime_argument, 72),
            "vital_delay");
    }
    llvm::Value* output_callback = nullptr;
    if (validated.uses_output) {
        output_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 17),
            "write_output");
    }
    llvm::Value* postponed_output_callback = nullptr;
    if (validated.uses_postponed_output) {
        postponed_output_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 18),
            "schedule_output");
    }
    llvm::Value* report_callback = nullptr;
    if (validated.uses_report) {
        report_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 19),
            "write_report");
    }
    llvm::Value* formatted_output_callback = nullptr;
    if (validated.uses_formatted_output) {
        formatted_output_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 20),
            "write_formatted");
    }
    llvm::Value* time_output_callback = nullptr;
    if (validated.uses_time_output) {
        time_output_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 21),
            "write_time");
    }
    llvm::Value* monitor_install_callback = nullptr;
    if (validated.uses_monitor_install) {
        monitor_install_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 22),
            "install_monitor");
    }
    llvm::Value* monitor_control_callback = nullptr;
    if (validated.uses_monitor_control) {
        monitor_control_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 23),
            "control_monitor");
    }
    llvm::Value* random_value_callback = nullptr;
    if (validated.uses_random_value) {
        random_value_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 24),
            "random_value");
    }
    llvm::Value* write_inertial_callback = nullptr;
    if (validated.uses_write_inertial) {
        write_inertial_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 25),
            "write_inertial");
    }
    llvm::Value* write_inertial_slice_callback = nullptr;
    if (validated.uses_write_inertial_slice) {
        write_inertial_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 26),
            "write_inertial_slice");
    }
    llvm::Value* exact_signal_callback = nullptr;
    if (validated.uses_exact_signal_operation) {
        exact_signal_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 76),
            "execute_signal_operation");
    }
    llvm::Value* read_signal_packed_callback = nullptr;
    if (validated.uses_wide_signal_read) {
        read_signal_packed_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 81),
            "read_signal_packed");
    }
    llvm::Value* write_signal_packed_callback = nullptr;
    if (validated.uses_wide_signal_write) {
        write_signal_packed_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 82),
            "write_signal_packed");
    }
    auto* read_signal_dynamic_part_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(runtime_type, runtime_argument, 101),
        "read_signal_dynamic_part");
    llvm::Value* write_projected_callback = nullptr;
    if (validated.uses_write_projected) {
        write_projected_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 27),
            "write_projected");
    }
    llvm::Value* write_projected_slice_callback = nullptr;
    if (validated.uses_write_projected_slice) {
        write_projected_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 28),
            "write_projected_slice");
    }
    llvm::Value* write_projected_waveform_callback = nullptr;
    if (validated.uses_write_projected_waveform) {
        write_projected_waveform_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 29),
            "write_projected_waveform");
    }
    llvm::Value* write_projected_waveform_slice_callback = nullptr;
    if (validated.uses_write_projected_waveform_slice) {
        write_projected_waveform_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 30),
            "write_projected_waveform_slice");
    }
    llvm::Value* read_logic9_callback = nullptr;
    llvm::Value* write_logic9_callback = nullptr;
    llvm::Value* write_update_logic9_callback = nullptr;
    llvm::Value* write_after_logic9_callback = nullptr;
    llvm::Value* write_blocking_slice_logic9_callback = nullptr;
    llvm::Value* write_update_slice_logic9_callback = nullptr;
    llvm::Value* write_after_slice_logic9_callback = nullptr;
    llvm::Value* signal_last_value_logic9_callback = nullptr;
    llvm::Value* write_inertial_logic9_callback = nullptr;
    llvm::Value* write_inertial_slice_logic9_callback = nullptr;
    llvm::Value* write_projected_logic9_callback = nullptr;
    llvm::Value* write_projected_slice_logic9_callback = nullptr;
    llvm::Value* write_projected_waveform_logic9_callback = nullptr;
    llvm::Value* write_projected_waveform_slice_logic9_callback = nullptr;
    llvm::Value* write_formatted_logic9_callback = nullptr;
    if (validated.uses_logic9) {
        const auto load_callback =
            [&](const unsigned index,
                const llvm::Twine& name) -> llvm::Value* {
            return builder.CreateLoad(
                pointer,
                builder.CreateStructGEP(
                    runtime_type, runtime_argument, index),
                name);
        };
        read_logic9_callback = load_callback(31, "read_signal_logic9");
        write_logic9_callback = load_callback(32, "write_signal_logic9");
        write_update_logic9_callback = load_callback(33, "write_update_logic9");
        write_after_logic9_callback = load_callback(34, "write_after_logic9");
        write_blocking_slice_logic9_callback = load_callback(35, "write_signal_slice_logic9");
        write_update_slice_logic9_callback = load_callback(36, "write_update_slice_logic9");
        write_after_slice_logic9_callback = load_callback(37, "write_after_slice_logic9");
        signal_last_value_logic9_callback = load_callback(38, "signal_last_value_logic9");
        write_inertial_logic9_callback = load_callback(39, "write_inertial_logic9");
        write_inertial_slice_logic9_callback = load_callback(40, "write_inertial_slice_logic9");
        write_projected_logic9_callback = load_callback(41, "write_projected_logic9");
        write_projected_slice_logic9_callback = load_callback(42, "write_projected_slice_logic9");
        write_projected_waveform_logic9_callback = load_callback(43, "write_projected_waveform_logic9");
        write_projected_waveform_slice_logic9_callback = load_callback(44, "write_projected_waveform_slice_logic9");
        write_formatted_logic9_callback = load_callback(45, "write_formatted_logic9");
    }
    auto* read_type = llvm::FunctionType::get(i64, { pointer, i32, pointer }, false);
    auto* write_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64 }, false);
    auto* assert_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
        { pointer, i32, i32, pointer, i64 }, false);
    auto* write_after_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i64 }, false);
    auto* write_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64 },
        false);
    auto* write_after_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i64 },
        false);
    auto* release_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32 },
        false);
    auto* write_inertial_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i64, i64, i64 },
        false);
    auto* write_inertial_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i64, i64, i64 },
        false);
    auto* exact_signal_type = llvm::FunctionType::get(
        i32, { pointer, i32, i32, pointer }, false);
    auto* read_signal_packed_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, pointer, pointer, pointer, pointer },
        false);
    auto* read_signal_dynamic_part_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i64, i64, i64, i64,
            i32, i32, i32, pointer },
        false);
    auto* write_signal_packed_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i32, i32, i64,
            pointer, pointer, pointer, pointer },
        false);
    auto* write_projected_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i64, i64, i32 },
        false);
    auto* write_projected_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i64, i64, i32 },
        false);
    auto* projected_element_type = llvm::StructType::create(
        context, { i64, i64, i64 }, "fsim_jit_projected_element_v1");
    auto* write_projected_waveform_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, pointer, i32, i64, i32 },
        false);
    auto* write_projected_waveform_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i32, i64, i32 },
        false);
    auto* signal_event_type = llvm::FunctionType::get(i32, { pointer, i32 }, false);
    auto* signal_last_value_type = llvm::FunctionType::get(i64, { pointer, i32, pointer }, false);
    auto* signal_last_event_type = llvm::FunctionType::get(i64, { pointer, i32 }, false);
    auto* signal_active_type = llvm::FunctionType::get(i32, { pointer, i32 }, false);
    auto* signal_last_active_type = llvm::FunctionType::get(i64, { pointer, i32 }, false);
    auto* signal_driving_type = llvm::FunctionType::get(i32, { pointer, i32 }, false);
    auto* signal_driving_value_type = llvm::FunctionType::get(i64, { pointer, i32, pointer }, false);
    auto* read_simulation_time_type = llvm::FunctionType::get(i64, { pointer }, false);
    auto* vital_timing_check_type = llvm::FunctionType::get(i32, { pointer, i32, i32 }, false);
    auto* vital_delay_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), { pointer, i32, i32 }, false);
    auto* output_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64, i32 },
        false);
    auto* report_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32 },
        false);
    auto* formatted_output_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64 },
        false);
    auto* time_output_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32 },
        false);
    auto* random_value_type = llvm::FunctionType::get(
        i64,
        { pointer, i32, i32, i64, i64, i64, i64, pointer },
        false);
    auto* logic9_word_type = llvm::ArrayType::get(i64, 4);
    auto* read_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer },
        false);
    auto* write_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer },
        false);
    auto* write_after_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64 },
        false);
    auto* write_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer },
        false);
    auto* write_after_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i64 },
        false);
    auto* write_inertial_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64, i64, i64 },
        false);
    auto* write_inertial_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i64, i64, i64 },
        false);
    auto* write_projected_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64, i64, i32 },
        false);
    auto* write_projected_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i64, i64, i32 },
        false);
    auto* logic9_projected_element_type = llvm::StructType::create(
        context,
        { logic9_word_type, i64 },
        "fsim_jit_logic9_projected_element_v1");
    auto* write_projected_waveform_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, pointer, i32, i64, i32 },
        false);
    auto* write_projected_waveform_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i32, i64, i32 },
        false);
    auto* formatted_output_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer },
        false);
    const auto native_callables = analyze_native_callables(process);
    const auto lowering_plan = make_process_lowering_plan(
        process, debug_instrumentation);
    const auto& resume_entries = lowering_plan.entry_points;
    const bool transient_boundaries_safe = std::ranges::all_of(
        process.operations,
        [](const runtime::simir::Operation& operation) {
            return !is_resume_boundary(operation)
                || fsim::runtime::simir::operation_holds<WaitSensitivity>(
                    operation)
                || fsim::runtime::simir::operation_holds<WaitForever>(
                    operation);
        });
    const bool restartable_register_frame = !debug_instrumentation
        && !lowering_plan.partial
        && transient_boundaries_safe
        && std::ranges::all_of(
            resume_entries,
            [&](const InstructionIndex resume_entry) {
                if (resume_entry == 0U) {
                    return true;
                }
                const auto* jump = resume_entry < process.operations.size()
                    ? fsim::runtime::simir::operation_get_if<Jump>(
                          &process.operations[resume_entry])
                    : nullptr;
                return jump != nullptr && jump->target == 0U;
            });
    std::vector<bool> callable_transient_registers(
        process.register_count);
    if (restartable_register_frame) {
        for (std::size_t index = 0;
             index < process.operations.size(); ++index) {
            const auto* push = fsim::runtime::simir::operation_get_if<
                CallableFramePush>(&process.operations[index]);
            if (push == nullptr
                || !native_callables.frame_operations[index]) {
                continue;
            }
            for (const auto register_id : push->packed) {
                if (register_id < callable_transient_registers.size()) {
                    callable_transient_registers[register_id] = true;
                }
            }
        }
    }
    std::vector<bool> persistent_debug_registers(process.register_count);
    for (const auto& local : process.debug_locals) {
        if (local.register_id < persistent_debug_registers.size()
            && !callable_transient_registers[local.register_id]) {
            persistent_debug_registers[local.register_id] = true;
        }
    }
    const bool transient_register_frame = restartable_register_frame
        && process.debug_string_locals.empty()
        && process.debug_container_locals.empty()
        && std::ranges::none_of(
            persistent_debug_registers, std::identity { });
    const bool hybrid_transient_register_frame = restartable_register_frame
        && !transient_register_frame
        && process.debug_string_locals.empty()
        && process.debug_container_locals.empty();
    // Keep large generated processes in compact aggregate storage. Thousands
    // of independent packed allocas make the optimizer rediscover the original
    // frame layout one scalar at a time and dominate cold compilation.
    constexpr std::size_t split_transient_register_operation_threshold = 2048U;
    const bool split_transient_register_frame = transient_register_frame
        && process.operations.size()
            < split_transient_register_operation_threshold;
    std::uint64_t register_word_count { };
    for (const auto width : validated.register_widths) {
        register_word_count += (static_cast<std::uint64_t>(width) + 63U) / 64U;
    }
    auto* local_register_type = llvm::ArrayType::get(
        i64, std::max<std::uint64_t>(register_word_count, 1U));
    llvm::Value* register_aval = nullptr;
    llvm::Value* register_bval = nullptr;
    if (transient_register_frame && !split_transient_register_frame) {
        register_aval = builder.CreateAlloca(
            local_register_type, nullptr, "register.aval.local");
        register_bval = builder.CreateAlloca(
            local_register_type, nullptr, "register.bval.local");
    } else if (!transient_register_frame) {
        register_aval = builder.CreateLoad(
            pointer, builder.CreateStructGEP(frame_type, frame_argument, 8),
            "register.aval.base");
        register_bval = builder.CreateLoad(
            pointer, builder.CreateStructGEP(frame_type, frame_argument, 9),
            "register.bval.base");
    }
    llvm::Value* register_initialized = nullptr;
    if (debug_instrumentation || !process.debug_locals.empty()) {
        register_initialized = builder.CreateLoad(
            pointer, builder.CreateStructGEP(frame_type, frame_argument, 10),
            "register.initialized.base");
    }
    llvm::Value* register_logic9_plane2 = nullptr;
    llvm::Value* register_logic9_plane3 = nullptr;
    if (transient_register_frame && !split_transient_register_frame
        && validated.uses_logic9) {
        register_logic9_plane2 = builder.CreateAlloca(
            local_register_type, nullptr, "register.logic9.plane2.local");
        register_logic9_plane3 = builder.CreateAlloca(
            local_register_type, nullptr, "register.logic9.plane3.local");
    } else if (!transient_register_frame) {
        register_logic9_plane2 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(frame_type, frame_argument, 11),
            "register.logic9.plane2.base");
        register_logic9_plane3 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(frame_type, frame_argument, 12),
            "register.logic9.plane3.base");
    }
    auto* i8 = llvm::Type::getInt8Ty(context);
    std::vector<RegisterSlot> registers(process.register_count);
    std::uint64_t register_word_offset { };
    for (std::size_t index = 0; index < process.register_count; ++index) {
        const auto width = validated.register_widths[index];
        if (width == 0) {
            continue;
        }
        const auto kind = process.register_value_kinds.empty()
            ? ValueKind::logic4
            : process.register_value_kinds[index];
        llvm::Value* aval_base = register_aval;
        llvm::Value* bval_base = register_bval;
        llvm::Value* plane2_base = register_logic9_plane2;
        llvm::Value* plane3_base = register_logic9_plane3;
        auto word_offset = register_word_offset;
        const bool transient_register = transient_register_frame
            || (hybrid_transient_register_frame
                && !persistent_debug_registers[index]);
        if (split_transient_register_frame
            || (hybrid_transient_register_frame
                && transient_register)) {
            const auto storage_width = ((width + 63U) / 64U) * 64U;
            auto* const packed_type = packed_integer_type(
                context, storage_width);
            auto* const aval = builder.CreateAlloca(
                packed_type, nullptr, "register.aval.local");
            auto* const bval = builder.CreateAlloca(
                packed_type, nullptr, "register.bval.local");
            aval->setAlignment(llvm::Align { 8 });
            bval->setAlignment(llvm::Align { 8 });
            aval_base = aval;
            bval_base = bval;
            if (kind == ValueKind::logic9) {
                auto* const plane2 = builder.CreateAlloca(
                    packed_type, nullptr, "register.logic9.plane2.local");
                auto* const plane3 = builder.CreateAlloca(
                    packed_type, nullptr, "register.logic9.plane3.local");
                plane2->setAlignment(llvm::Align { 8 });
                plane3->setAlignment(llvm::Align { 8 });
                plane2_base = plane2;
                plane3_base = plane3;
            }
            word_offset = 0U;
        }
        registers[index] = {
            aval_base,
            bval_base,
            transient_register ? nullptr : register_initialized,
            plane2_base,
            plane3_base,
            word_offset,
            static_cast<std::uint32_t>(index),
            width,
            kind,
        };
        register_word_offset += (static_cast<std::uint64_t>(width) + 63U) / 64U;
    }
    auto* read_bval_slot = builder.CreateAlloca(i64, nullptr, "read.bval");
    auto* logic9_word_slot = builder.CreateAlloca(logic9_word_type, nullptr, "logic9.word");
    auto* container_result_aval_slot = builder.CreateAlloca(
        i64, nullptr, "container.result.aval");
    auto* container_result_bval_slot = builder.CreateAlloca(
        i64, nullptr, "container.result.bval");
    const auto logic9_plane_pointer =
        [&](llvm::Value* storage,
            const std::uint32_t plane) -> llvm::Value* {
        return builder.CreateInBoundsGEP(
            logic9_word_type,
            storage,
            { llvm::ConstantInt::get(i32, 0),
                llvm::ConstantInt::get(i32, plane) });
    };
    const auto store_logic9_word =
        [&](llvm::Value* storage, EncodedValue value) {
            value = coerce_value_kind(
                builder, value, ValueKind::logic9);
            const std::array planes {
                value.aval,
                value.bval,
                value.logic9_plane2,
                value.logic9_plane3
            };
            for (std::uint32_t plane = 0; plane < 4; ++plane) {
                builder.CreateStore(
                    planes[plane],
                    logic9_plane_pointer(storage, plane));
            }
        };
    const auto load_logic9_word =
        [&](llvm::Value* storage,
            const std::uint32_t width) -> EncodedValue {
        return {
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 0)),
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 1)),
            width,
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 2)),
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 3)),
            ValueKind::logic9
        };
    };
    const auto return_result =
        [&](const std::uint32_t status, const std::uint32_t instruction,
            const std::uint64_t delay, const std::uint32_t frame_state,
            const std::uint32_t next_pc) {
            builder.CreateStore(
                llvm::ConstantInt::get(i32, next_pc),
                builder.CreateStructGEP(frame_type, frame_argument, 5));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, frame_state),
                builder.CreateStructGEP(frame_type, frame_argument, 6));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, instruction),
                builder.CreateStructGEP(frame_type, frame_argument, 7));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, status),
                builder.CreateStructGEP(result_type, result_argument, 2));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, instruction),
                builder.CreateStructGEP(result_type, result_argument, 3));
            builder.CreateStore(
                constant_i64(context, delay),
                builder.CreateStructGEP(result_type, result_argument, 4));
            builder.CreateRet(llvm::ConstantInt::get(i32, status));
        };
    std::vector<bool> elided_operations(process.operations.size());
    for (std::size_t index = 0; index + 1U < process.operations.size();
         ++index) {
        elided_operations[index]
            = lowering_plan.operations[index]
            && ((!debug_instrumentation
                  && fsim::runtime::simir::operation_holds<DebugPoint>(
                      process.operations[index]))
                || native_callables.frame_operations[index]);
    }
    const auto register_reference_count = [](const auto& instructions,
                                             const RegisterId id) {
        return std::accumulate(
            instructions.begin(),
            instructions.end(),
            std::size_t { 0 },
            [id](const std::size_t count, const auto& referenced) {
                return count + static_cast<std::size_t>(
                    std::ranges::count(referenced, id));
            });
    };
    std::vector<bool> alternate_container_read_entry(
        process.operations.size());
    for (const auto entry_point : resume_entries) {
        alternate_container_read_entry[entry_point] = true;
    }
    const auto mark_alternate_entry = [&](const InstructionIndex target) {
        if (target < alternate_container_read_entry.size()) {
            alternate_container_read_entry[target] = true;
        }
    };
    for (const auto& candidate : process.operations) {
        fsim::runtime::simir::visit_operation(
            [&](const auto& operation) {
                using OperationType = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<OperationType, Jump>) {
                    mark_alternate_entry(operation.target);
                } else if constexpr (std::is_same_v<OperationType, Branch>) {
                    mark_alternate_entry(operation.when_true);
                    mark_alternate_entry(operation.when_false);
                } else if constexpr (std::is_same_v<OperationType, Call>) {
                    mark_alternate_entry(operation.target);
                    mark_alternate_entry(operation.return_target);
                } else if constexpr (std::is_same_v<OperationType, Fork>) {
                    for (const auto target : operation.branches) {
                        mark_alternate_entry(target);
                    }
                }
            },
            candidate);
    }
    struct FusedAffineDynamicExtract {
        RegisterId destination { };
        RegisterId source { };
        RegisterId index { };
        std::int64_t scale { };
        std::int64_t constant_offset { };
        std::int64_t source_right { };
        std::uint32_t source_base_offset { };
        std::uint32_t width { };
        std::size_t resume { };
    };
    std::vector<std::optional<FusedAffineDynamicExtract>>
        fused_affine_dynamic_extracts(process.operations.size());
    if (!debug_instrumentation) {
        for (std::size_t start = 0; start < process.operations.size(); ++start) {
            const auto* initial
                = runtime::simir::operation_get_if<LoadConstant>(
                    &process.operations[start]);
            if (initial == nullptr || initial->value.width() < 128U) {
                continue;
            }
            const auto zero_words = [](const auto words) {
                return std::ranges::all_of(
                    words, [](const auto word) { return word == 0U; });
            };
            bool initial_is_zero = zero_words(initial->value.aval_words())
                && zero_words(initial->value.bval_words());
            if (initial->value.is_logic9()) {
                for (std::size_t plane = 0U; plane < 4U; ++plane) {
                    initial_is_zero = initial_is_zero
                        && zero_words(
                            initial->value.logic9_plane_words(plane));
                }
            }
            if (!initial_is_zero) {
                continue;
            }
            std::size_t cursor = start + 1U;
            std::size_t element = 0U;
            std::size_t last_insert = start;
            RegisterId source { };
            RegisterId runtime_index { };
            std::int64_t scale { };
            std::int64_t constant_offset { };
            DynamicIndex selection { };
            bool initialized { };
            while (cursor < process.operations.size()) {
                while (cursor < process.operations.size()
                    && runtime::simir::operation_holds<DebugPoint>(
                        process.operations[cursor])) {
                    ++cursor;
                }
                if (cursor + 7U >= process.operations.size()) {
                    break;
                }
                const auto* base_constant
                    = runtime::simir::operation_get_if<LoadConstant>(
                        &process.operations[cursor]);
                const auto* scale_constant
                    = runtime::simir::operation_get_if<LoadConstant>(
                        &process.operations[cursor + 1U]);
                const auto* multiply
                    = runtime::simir::operation_get_if<IntegerBinary>(
                        &process.operations[cursor + 2U]);
                const auto* add_base
                    = runtime::simir::operation_get_if<IntegerBinary>(
                        &process.operations[cursor + 3U]);
                const auto* element_constant
                    = runtime::simir::operation_get_if<LoadConstant>(
                        &process.operations[cursor + 4U]);
                const auto* add_element
                    = runtime::simir::operation_get_if<IntegerBinary>(
                        &process.operations[cursor + 5U]);
                const auto* extract
                    = runtime::simir::operation_get_if<DynamicExtract>(
                        &process.operations[cursor + 6U]);
                const auto* insert
                    = runtime::simir::operation_get_if<Insert>(
                        &process.operations[cursor + 7U]);
                if (base_constant == nullptr || scale_constant == nullptr
                    || multiply == nullptr || add_base == nullptr
                    || element_constant == nullptr || add_element == nullptr
                    || extract == nullptr || insert == nullptr) {
                    break;
                }
                const auto base_value
                    = base_constant->value.known_signed_value();
                const auto scale_value
                    = scale_constant->value.known_signed_value();
                const auto element_value
                    = element_constant->value.known_signed_value();
                const auto multiply_index
                    = multiply->lhs == scale_constant->destination
                    ? multiply->rhs : multiply->lhs;
                const bool multiply_matches
                    = multiply->operation == IntegerBinaryOperator::multiply
                    && (multiply->lhs == scale_constant->destination
                        || multiply->rhs == scale_constant->destination);
                const bool add_base_matches
                    = add_base->operation == IntegerBinaryOperator::add
                    && ((add_base->lhs == base_constant->destination
                            && add_base->rhs == multiply->destination)
                        || (add_base->rhs == base_constant->destination
                            && add_base->lhs == multiply->destination));
                const bool add_element_matches
                    = add_element->operation == IntegerBinaryOperator::add
                    && ((add_element->lhs == add_base->destination
                            && add_element->rhs
                                == element_constant->destination)
                        || (add_element->rhs == add_base->destination
                            && add_element->lhs
                                == element_constant->destination));
                if (!base_value || !scale_value || !element_value
                    || !multiply_matches || !add_base_matches
                    || !add_element_matches
                    || *element_value != static_cast<std::int64_t>(element)
                    || extract->selection.index != add_element->destination
                    || insert->destination != initial->destination
                    || insert->target != initial->destination
                    || insert->source != extract->destination
                    || insert->offset != element) {
                    break;
                }
                const std::array temporary_registers {
                    base_constant->destination,
                    scale_constant->destination,
                    multiply->destination,
                    add_base->destination,
                    element_constant->destination,
                    add_element->destination,
                    extract->destination,
                };
                const bool temporaries_are_local = std::ranges::all_of(
                    temporary_registers,
                    [&](const RegisterId temporary) {
                        return register_reference_count(
                                   validated.instruction_uses, temporary)
                                == 1U
                            && register_reference_count(
                                   validated.instruction_definitions,
                                   temporary)
                                == 1U;
                    });
                if (!temporaries_are_local) {
                    break;
                }
                if (!initialized) {
                    source = extract->source;
                    runtime_index = multiply_index;
                    scale = *scale_value;
                    constant_offset = *base_value;
                    selection = extract->selection;
                    initialized = true;
                } else if (extract->source != source
                    || multiply_index != runtime_index
                    || *scale_value != scale
                    || *base_value != constant_offset
                    || extract->selection.left != selection.left
                    || extract->selection.right != selection.right
                    || extract->selection.base_offset
                        != selection.base_offset) {
                    break;
                }
                last_insert = cursor + 7U;
                cursor += 8U;
                ++element;
            }
            if (!initialized || element != initial->value.width()
                || scale != static_cast<std::int64_t>(element)
                || selection.left <= selection.right
                || source >= validated.register_widths.size()
                || runtime_index >= validated.register_widths.size()
                || initial->destination >= validated.register_widths.size()
                || validated.register_widths[initial->destination] != element
                || validated.register_widths[runtime_index] > 32U
                || last_insert + 1U >= process.operations.size()
                || !std::ranges::all_of(
                    std::views::iota(start, last_insert + 1U),
                    [&](const std::size_t operation) {
                        return lowering_plan.operations[operation];
                    })
                || std::ranges::any_of(
                    std::views::iota(start + 1U, last_insert + 1U),
                    [&](const std::size_t operation) {
                        return alternate_container_read_entry[operation];
                    })) {
                continue;
            }
            const auto register_kind = [&](const RegisterId id) {
                return process.register_value_kinds.empty()
                    ? ValueKind::logic4
                    : process.register_value_kinds[id];
            };
            if (register_kind(source)
                != register_kind(initial->destination)) {
                continue;
            }
            fused_affine_dynamic_extracts[start]
                = FusedAffineDynamicExtract {
                    initial->destination,
                    source,
                    runtime_index,
                    scale,
                    constant_offset,
                    selection.right,
                    selection.base_offset,
                    static_cast<std::uint32_t>(element),
                    last_insert + 1U,
                };
            for (auto index = start + 1U; index <= last_insert; ++index) {
                elided_operations[index] = true;
            }
            start = last_insert;
        }
    }
    std::vector<const runtime::PackedLogic4*> constant_part_select_sources(
        process.operations.size(), nullptr);
    std::vector<std::optional<runtime::simir::SignalId>>
        dynamic_part_signal_sources(process.operations.size());
    if (!debug_instrumentation) {
        for (std::size_t index = 1U; index < process.operations.size();
             ++index) {
            const auto* select
                = runtime::simir::operation_get_if<DynamicPartSelect>(
                    &process.operations[index]);
            const auto* constant
                = runtime::simir::operation_get_if<LoadConstant>(
                    &process.operations[index - 1U]);
            if (select == nullptr || constant == nullptr
                || constant->destination != select->source
                || select->width > 64U
                || constant->value.width()
                    != validated.register_widths[select->source]
                || !lowering_plan.operations[index - 1U]
                || !lowering_plan.operations[index]
                || elided_operations[index - 1U]) {
                continue;
            }
            const auto source_kind = process.register_value_kinds.empty()
                ? ValueKind::logic4
                : process.register_value_kinds[select->source];
            if (constant->value.is_logic9()
                    != (source_kind == ValueKind::logic9)
                || register_reference_count(
                       validated.instruction_uses, select->source)
                    != 1U
                || register_reference_count(
                       validated.instruction_definitions, select->source)
                    != 1U) {
                continue;
            }
            const auto edge = static_cast<std::int64_t>(select->width - 1U);
            const auto lower = std::min(select->left, select->right);
            const auto upper = std::max(select->left, select->right);
            const auto table_min = select->increasing ? lower - edge : lower;
            const auto table_max = select->increasing ? upper : upper + edge;
            constexpr std::int64_t maximum_constant_select_entries = 65536;
            if (table_max - table_min + 1
                > maximum_constant_select_entries) {
                continue;
            }
            constant_part_select_sources[index] = &constant->value;
            elided_operations[index - 1U] = true;
        }
        for (std::size_t index = 1U; index < process.operations.size();
             ++index) {
            const auto* select
                = runtime::simir::operation_get_if<DynamicPartSelect>(
                    &process.operations[index]);
            const auto* read = runtime::simir::operation_get_if<ReadSignal>(
                &process.operations[index - 1U]);
            if (select == nullptr || read == nullptr
                || read->kind != runtime::simir::SignalReadKind::current
                || read->destination != select->source
                || select->width > 64U
                || validated.register_widths[select->source] <= 64U
                || !lowering_plan.operations[index - 1U]
                || !lowering_plan.operations[index]
                || elided_operations[index - 1U]
                || register_reference_count(
                       validated.instruction_uses, select->source)
                    != 1U
                || register_reference_count(
                       validated.instruction_definitions, select->source)
                    != 1U) {
                continue;
            }
            dynamic_part_signal_sources[index] = read->signal;
            elided_operations[index - 1U] = true;
        }
    }
    std::vector<std::uint32_t> fused_container_object_reads(
        process.operations.size());
    if (!debug_instrumentation) {
        for (std::size_t index = 0; index + 1U < process.operations.size();
             ++index) {
            const auto* object_read
                = fsim::runtime::simir::operation_get_if<
                    runtime::simir::ReadContainerObject>(
                    &process.operations[index]);
            if (object_read == nullptr || !lowering_plan.operations[index]
                || elided_operations[index]) {
                continue;
            }
            for (std::size_t distance = 1U; distance <= 16U; ++distance) {
                const auto candidate_index = index + distance;
                if (candidate_index >= process.operations.size()
                    || alternate_container_read_entry[candidate_index]
                    || !lowering_plan.operations[candidate_index]
                    || elided_operations[candidate_index]) {
                    break;
                }
                const auto& candidate
                    = process.operations[candidate_index];
                const auto* element_read
                    = fsim::runtime::simir::operation_get_if<
                        runtime::simir::ContainerRead>(&candidate);
                if (element_read != nullptr
                    && element_read->source == object_read->destination) {
                    const auto& type = process.container_register_types[
                        element_read->source];
                    const bool packed_fast_path
                        = !element_read->string_index && !type.associative
                        && (type.element_kind
                                == runtime::simir::ContainerElementKind::Packed
                            || type.element_kind
                                == runtime::simir::ContainerElementKind::Scalar)
                        && registers[element_read->index].width <= 64U
                        && registers[element_read->destination].width
                            == type.element_width;
                    if (packed_fast_path) {
                        elided_operations[index] = true;
                        fused_container_object_reads[candidate_index]
                            = static_cast<std::uint32_t>(distance);
                    }
                    break;
                }
                bool container_operation { };
                fsim::runtime::simir::visit_operation(
                    [&](const auto& operation) {
                        if constexpr (
                            requires(ContainerOperationLowerer& lowerer) {
                                lowerer.lower(operation);
                            }) {
                            container_operation = true;
                        }
                    },
                    candidate);
                if (container_operation || is_resume_boundary(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Jump>(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Branch>(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Call>(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Return>(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Fork>(candidate)) {
                    break;
                }
            }
        }
    }
    std::vector<llvm::BasicBlock*> instruction_blocks(
        process.operations.size());
    std::vector<std::vector<llvm::BasicBlock*>> instruction_regions(
        process.operations.size());
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        if (!lowering_plan.operations[index]
            || elided_operations[index]) {
            continue;
        }
        auto* block = llvm::BasicBlock::Create(
            context, "instruction." + std::to_string(index), function);
        instruction_blocks[index] = block;
        instruction_regions[index].push_back(block);
    }
    for (std::size_t index = process.operations.size(); index-- > 0;) {
        if (lowering_plan.operations[index]
            && elided_operations[index]) {
            instruction_blocks[index] = instruction_blocks[index + 1U];
        }
    }
    std::vector<const Process::StaticTriggerRegion*>
        static_trigger_region_entries(process.operations.size(), nullptr);
    if (static_trigger_mask != nullptr) {
        for (const auto& region : process.static_trigger_regions) {
            auto first = static_cast<std::size_t>(region.begin);
            while (first < region.end
                && (first >= lowering_plan.operations.size()
                    || !lowering_plan.operations[first]
                    || elided_operations[first])) {
                ++first;
            }
            if (first < region.end
                && region.end < instruction_blocks.size()
                && instruction_blocks[region.end] != nullptr) {
                static_trigger_region_entries[first] = &region;
            }
        }
    }
    std::set<InstructionIndex> ssa_callable_entries;
    if (!debug_instrumentation) {
        for (const auto& [first, last] : native_callables.regions) {
            const auto suspended = std::ranges::any_of(
                process.operations.begin() + first,
                process.operations.begin() + last + 1U,
                [](const Operation& operation) {
                    return is_resume_boundary(operation);
                });
            if (!suspended) {
                ssa_callable_entries.insert(first);
            }
        }
        // An SSA continuation is local to one native resume invocation.  A
        // callable that reaches a suspending nested callable must therefore
        // retain its continuation in the persistent native return stack even
        // when its own linear region has no resume boundary.  Propagate that
        // restriction through the native-call graph to cover deeper nesting.
        bool changed = false;
        do {
            changed = false;
            for (const auto& [first, last] : native_callables.regions) {
                if (!ssa_callable_entries.contains(first)) {
                    continue;
                }
                bool reaches_non_ssa_callable = false;
                for (std::size_t index = first; index <= last; ++index) {
                    const auto* call
                        = fsim::runtime::simir::operation_get_if<Call>(
                            &process.operations[index]);
                    if (call != nullptr
                        && native_callables.call_operations[index]
                        && !ssa_callable_entries.contains(call->target)) {
                        reaches_non_ssa_callable = true;
                        break;
                    }
                }
                if (reaches_non_ssa_callable) {
                    ssa_callable_entries.erase(first);
                    changed = true;
                }
            }
        } while (changed);
    }
    std::set<InstructionIndex> ssa_callable_targets;
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        const auto* call = fsim::runtime::simir::operation_get_if<Call>(
            &process.operations[index]);
        if (call != nullptr && native_callables.call_operations[index]
            && ssa_callable_entries.contains(call->target)) {
            ssa_callable_targets.insert(call->target);
        }
    }
    std::map<InstructionIndex, llvm::AllocaInst*> ssa_callable_returns;
    for (const auto target : ssa_callable_targets) {
        auto* slot = builder.CreateAlloca(
            i32,
            nullptr,
            "native.return.continuation." + std::to_string(target));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, FSIM_JIT_INVALID_INSTRUCTION),
            slot);
        ssa_callable_returns.emplace(target, slot);
    }
    auto* invalid_pc = llvm::BasicBlock::Create(context, "invalid.pc", function);
    auto* program_counter = builder.CreateLoad(
        i32, builder.CreateStructGEP(frame_type, frame_argument, 5),
        "program.counter");
    auto* dispatch = builder.CreateSwitch(
        program_counter, invalid_pc,
        static_cast<unsigned>(resume_entries.size()));
    for (const auto index : resume_entries) {
        dispatch->addCase(
            llvm::ConstantInt::get(i32, index),
            instruction_blocks[index]);
    }

    builder.SetInsertPoint(invalid_pc);
    return_result(
        FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR, FSIM_JIT_INVALID_INSTRUCTION, 0,
        FSIM_JIT_FRAME_STATE_RUNTIME_ERROR, FSIM_JIT_INVALID_INSTRUCTION);
    auto return_targets = static_return_targets(process);
    std::erase_if(return_targets, [&](const InstructionIndex target) {
        return target >= lowering_plan.operations.size()
            || !lowering_plan.operations[target];
    });
    auto native_return_targets = native_callables.return_targets;
    std::erase_if(native_return_targets, [&](const InstructionIndex target) {
        return target >= lowering_plan.operations.size()
            || !lowering_plan.operations[target];
    });
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        if (!lowering_plan.operations[index]
            || elided_operations[index]) {
            continue;
        }
        const auto instruction = static_cast<InstructionIndex>(index);
        const auto next_instruction = static_cast<InstructionIndex>(index + 1U);
        builder.SetInsertPoint(instruction_blocks[index]);
        if (const auto* trigger_region
            = static_trigger_region_entries[index]) {
            auto* execute_region = llvm::BasicBlock::Create(
                context,
                "static.trigger.execute." + std::to_string(index),
                function);
            const auto accepted_mask = trigger_region->mask
                | Process::full_static_trigger_mask;
            builder.CreateCondBr(
                builder.CreateICmpNE(
                    builder.CreateAnd(
                        static_trigger_mask,
                        constant_i64(context, accepted_mask)),
                    constant_i64(context, 0U)),
                execute_region,
                instruction_blocks[trigger_region->end]);
            builder.SetInsertPoint(execute_region);
        }
        auto* const last_block_before_lowering = &function->back();
        const auto branch_to_next = [&] {
            builder.CreateBr(instruction_blocks[index + 1]);
        };
        const auto runtime_error_if =
            [&](llvm::Value* condition,
                const JitGeneratedRuntimeErrorReason reason,
                const std::string_view label) {
                auto* error_block = llvm::BasicBlock::Create(
                    context,
                    std::string { label } + ".error."
                        + std::to_string(index),
                    function);
                auto* continue_block = llvm::BasicBlock::Create(
                    context,
                    std::string { label } + ".continue."
                        + std::to_string(index),
                    function);
                builder.CreateCondBr(
                    condition, error_block, continue_block);
                builder.SetInsertPoint(error_block);
                return_result(
                    FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR,
                    instruction,
                    static_cast<std::uint64_t>(reason),
                    FSIM_JIT_FRAME_STATE_RUNTIME_ERROR,
                    static_cast<std::uint32_t>(reason));
                builder.SetInsertPoint(continue_block);
            };
        const auto execute_exact_signal = [&] {
            auto* status = builder.CreateCall(
                exact_signal_type,
                exact_signal_callback,
                { context_pointer,
                    llvm::ConstantInt::get(i32, process.id),
                    llvm::ConstantInt::get(i32, instruction),
                    frame_argument });
            runtime_error_if(
                builder.CreateICmpNE(
                    status, llvm::ConstantInt::get(i32, 0)),
                JitGeneratedRuntimeErrorReason::signal_callback_failure,
                "signal.operation");
            branch_to_next();
        };
        const auto read_wide_signal = [&](const ReadSignal& operation) {
            const auto& slot = registers[operation.destination];
            auto* aval = builder.CreateGEP(
                i64,
                slot.aval_base,
                constant_i64(context, slot.word_offset),
                "wide.signal.aval");
            auto* bval = builder.CreateGEP(
                i64,
                slot.bval_base,
                constant_i64(context, slot.word_offset),
                "wide.signal.bval");
            auto* const null_pointer = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(pointer));
            llvm::Value* plane2 = null_pointer;
            llvm::Value* plane3 = null_pointer;
            if (slot.kind == ValueKind::logic9) {
                plane2 = builder.CreateGEP(
                    i64,
                    slot.logic9_plane2_base,
                    constant_i64(context, slot.word_offset),
                    "wide.signal.logic9.plane2");
                plane3 = builder.CreateGEP(
                    i64,
                    slot.logic9_plane3_base,
                    constant_i64(context, slot.word_offset),
                    "wide.signal.logic9.plane3");
            }
            const auto direct = std::ranges::find(
                direct_read_signals, operation.signal);
            if (direct != direct_read_signals.end()
                && direct_wide_signal_aval != nullptr
                && direct_wide_signal_bval != nullptr
                && direct_wide_signal_offsets != nullptr
                && direct_wide_signal_offset_count != nullptr
                && direct_wide_word_count != nullptr
                && direct_read_signal_map != nullptr
                && direct_read_signal_count != nullptr) {
                auto* current_function = builder.GetInsertBlock()->getParent();
                auto* map_block = llvm::BasicBlock::Create(
                    context, "wide.signal.direct.map", current_function);
                auto* bounds_block = llvm::BasicBlock::Create(
                    context, "wide.signal.direct.bounds", current_function);
                auto* direct_block = llvm::BasicBlock::Create(
                    context, "wide.signal.direct", current_function);
                auto* callback_block = llvm::BasicBlock::Create(
                    context, "wide.signal.callback", current_function);
                auto* merge_block = llvm::BasicBlock::Create(
                    context, "wide.signal.merge", current_function);
                const auto nonnull = [&](llvm::Value* value) {
                    return builder.CreateICmpNE(
                        value,
                        llvm::ConstantPointerNull::get(
                            llvm::cast<llvm::PointerType>(value->getType())));
                };
                const auto direct_slot = static_cast<std::uint32_t>(
                    std::distance(direct_read_signals.begin(), direct));
                auto* pointers_available = builder.CreateAnd(
                    builder.CreateAnd(
                        builder.CreateAnd(
                            nonnull(direct_wide_signal_aval),
                            nonnull(direct_wide_signal_bval)),
                        nonnull(direct_wide_signal_offsets)),
                    nonnull(direct_read_signal_map));
                if (slot.kind == ValueKind::logic9) {
                    pointers_available = builder.CreateAnd(
                        pointers_available,
                        builder.CreateAnd(
                            nonnull(direct_wide_signal_logic9_plane2),
                            nonnull(direct_wide_signal_logic9_plane3)));
                }
                builder.CreateCondBr(
                    builder.CreateAnd(
                        pointers_available,
                        builder.CreateICmpULT(
                            llvm::ConstantInt::get(i32, direct_slot),
                            direct_read_signal_count)),
                    map_block,
                    callback_block);

                builder.SetInsertPoint(map_block);
                auto* actual = builder.CreateLoad(
                    i32,
                    builder.CreateInBoundsGEP(
                        i32,
                        direct_read_signal_map,
                        llvm::ConstantInt::get(i32, direct_slot)),
                    "wide.signal.actual");
                builder.CreateCondBr(
                    builder.CreateICmpULT(
                        actual, direct_wide_signal_offset_count),
                    bounds_block,
                    callback_block);

                builder.SetInsertPoint(bounds_block);
                auto* word_offset = builder.CreateLoad(
                    i32,
                    builder.CreateInBoundsGEP(
                        i32, direct_wide_signal_offsets, actual),
                    "wide.signal.word_offset");
                const auto words = static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(slot.width) + 63U) / 64U);
                auto* available = builder.CreateICmpULE(
                    word_offset, direct_wide_word_count);
                auto* remaining = builder.CreateSub(
                    direct_wide_word_count, word_offset);
                auto* enough = builder.CreateICmpUGE(
                    remaining, llvm::ConstantInt::get(i32, words));
                builder.CreateCondBr(
                    builder.CreateAnd(available, enough),
                    direct_block,
                    callback_block);

                builder.SetInsertPoint(direct_block);
                auto* wide_aval = builder.CreateInBoundsGEP(
                    i64, direct_wide_signal_aval, word_offset);
                auto* wide_bval = builder.CreateInBoundsGEP(
                    i64, direct_wide_signal_bval, word_offset);
                const auto bytes = static_cast<std::uint64_t>(words) * 8U;
                builder.CreateMemCpy(
                    aval, llvm::Align(8),
                    wide_aval, llvm::Align(8), bytes);
                builder.CreateMemCpy(
                    bval, llvm::Align(8),
                    wide_bval, llvm::Align(8), bytes);
                if (slot.kind == ValueKind::logic9) {
                    auto* wide_plane2 = builder.CreateInBoundsGEP(
                        i64,
                        direct_wide_signal_logic9_plane2,
                        word_offset);
                    auto* wide_plane3 = builder.CreateInBoundsGEP(
                        i64,
                        direct_wide_signal_logic9_plane3,
                        word_offset);
                    builder.CreateMemCpy(
                        plane2, llvm::Align(8),
                        wide_plane2, llvm::Align(8), bytes);
                    builder.CreateMemCpy(
                        plane3, llvm::Align(8),
                        wide_plane3, llvm::Align(8), bytes);
                }
                builder.CreateBr(merge_block);

                builder.SetInsertPoint(callback_block);
                auto* status = builder.CreateCall(
                    read_signal_packed_type,
                    read_signal_packed_callback,
                    { context_pointer,
                        llvm::ConstantInt::get(i32, operation.signal),
                        llvm::ConstantInt::get(i32, slot.width),
                        aval,
                        bval,
                        plane2,
                        plane3 });
                runtime_error_if(
                    builder.CreateICmpNE(
                        status, llvm::ConstantInt::get(i32, 0)),
                    JitGeneratedRuntimeErrorReason::signal_callback_failure,
                    "wide.signal.read");
                builder.CreateBr(merge_block);
                builder.SetInsertPoint(merge_block);
            } else {
                auto* status = builder.CreateCall(
                    read_signal_packed_type,
                    read_signal_packed_callback,
                    { context_pointer,
                        llvm::ConstantInt::get(i32, operation.signal),
                        llvm::ConstantInt::get(i32, slot.width),
                        aval,
                        bval,
                        plane2,
                        plane3 });
                runtime_error_if(
                    builder.CreateICmpNE(
                        status, llvm::ConstantInt::get(i32, 0)),
                    JitGeneratedRuntimeErrorReason::signal_callback_failure,
                    "wide.signal.read");
            }
            if (slot.initialized_base != nullptr) {
                auto* initialized = builder.CreateGEP(
                    i8,
                    slot.initialized_base,
                    llvm::ConstantInt::get(i32, slot.index),
                    "wide.signal.initialized");
                builder.CreateStore(
                    llvm::ConstantInt::get(i8, 1), initialized);
            }
            branch_to_next();
        };
        const auto write_wide_signal = [&](const std::uint32_t signal,
                                           const RegisterId source,
                                           const std::uint32_t offset,
                                           const std::uint32_t mode,
                                           const runtime::SimulationTick delay) {
            const auto& slot = registers[source];
            auto* aval = builder.CreateGEP(
                i64,
                slot.aval_base,
                constant_i64(context, slot.word_offset),
                "wide.signal.write.aval");
            auto* bval = builder.CreateGEP(
                i64,
                slot.bval_base,
                constant_i64(context, slot.word_offset),
                "wide.signal.write.bval");
            auto* const null_pointer = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(pointer));
            llvm::Value* plane2 = null_pointer;
            llvm::Value* plane3 = null_pointer;
            if (slot.kind == ValueKind::logic9) {
                plane2 = builder.CreateGEP(
                    i64,
                    slot.logic9_plane2_base,
                    constant_i64(context, slot.word_offset),
                    "wide.signal.write.logic9.plane2");
                plane3 = builder.CreateGEP(
                    i64,
                    slot.logic9_plane3_base,
                    constant_i64(context, slot.word_offset),
                    "wide.signal.write.logic9.plane3");
            }
            auto* status = builder.CreateCall(
                write_signal_packed_type,
                write_signal_packed_callback,
                { context_pointer,
                    llvm::ConstantInt::get(i32, signal),
                    llvm::ConstantInt::get(i32, offset),
                    llvm::ConstantInt::get(i32, slot.width),
                    llvm::ConstantInt::get(i32, mode),
                    constant_i64(context, delay),
                    aval,
                    bval,
                    plane2,
                    plane3 });
            runtime_error_if(
                builder.CreateICmpNE(
                    status, llvm::ConstantInt::get(i32, 0)),
                JitGeneratedRuntimeErrorReason::signal_callback_failure,
                "wide.signal.write");
            branch_to_next();
        };
        const auto dynamic_offset =
            [&](const DynamicIndex& selection) -> llvm::Value* {
            const auto selected = coerce_value_kind(
                builder,
                load_register(
                    builder, registers, selection.index),
                ValueKind::logic4);
            runtime_error_if(
                builder.CreateICmpNE(
                    builder.CreateAnd(
                        selected.bval,
                        constant_i64(
                            context,
                            std::numeric_limits<std::uint32_t>::max())),
                    constant_i64(context, 0)),
                JitGeneratedRuntimeErrorReason::dynamic_index_unknown,
                "dynamic.index.unknown");
            auto* signed_index = builder.CreateSExt(
                builder.CreateTrunc(selected.aval, i32), i64);
            auto* left = llvm::ConstantInt::getSigned(
                i64, selection.left);
            auto* right = llvm::ConstantInt::getSigned(
                i64, selection.right);
            auto* lower = selection.left <= selection.right ? left : right;
            auto* upper = selection.left <= selection.right ? right : left;
            runtime_error_if(
                builder.CreateOr(
                    builder.CreateICmpSLT(signed_index, lower),
                    builder.CreateICmpSGT(signed_index, upper)),
                JitGeneratedRuntimeErrorReason::dynamic_index_range,
                "dynamic.index.range");
            auto* distance = builder.CreateSelect(
                builder.CreateICmpSGE(signed_index, right),
                builder.CreateSub(signed_index, right),
                builder.CreateSub(right, signed_index));
            return builder.CreateAdd(
                distance,
                constant_i64(context, selection.base_offset),
                "dynamic.index.offset");
        };
        const auto dynamic_offset_i32 =
            [&](const DynamicIndex& selection) {
                return builder.CreateTrunc(
                    dynamic_offset(selection), i32);
            };
        const auto emit_dynamic_slice =
            [&](const std::uint32_t signal,
                const RegisterId source_register,
                llvm::Value* offset,
                llvm::Value* logic4_callback,
                llvm::Value* logic9_callback) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[signal];
                const auto source = coerce_value_kind(
                    builder,
                    load_register(
                        builder, registers, source_register),
                    signal_kind);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, source);
                    builder.CreateCall(
                        write_slice_logic9_type,
                        logic9_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            logic9_word_slot });
                } else {
                    builder.CreateCall(
                        write_slice_type,
                        logic4_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            source.aval,
                            source.bval });
                }
                branch_to_next();
            };
        const auto emit_dynamic_part_slice =
            [&](const std::uint32_t signal,
                const RegisterId source_register,
                const DynamicPartIndex& selection,
                llvm::Value* logic4_callback,
                llvm::Value* logic9_callback,
                const std::optional<runtime::SimulationTick> delay) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[signal];
                const auto write = lower_dynamic_part_write(
                    builder,
                    context,
                    i32,
                    i64,
                    registers,
                    source_register,
                    selection,
                    signal_kind);
                auto* write_block = llvm::BasicBlock::Create(
                    context,
                    "dynamic.part.write." + std::to_string(index),
                    function);
                builder.CreateCondBr(
                    builder.CreateICmpNE(
                        write.width, llvm::ConstantInt::get(i32, 0)),
                    write_block,
                    instruction_blocks[index + 1]);
                builder.SetInsertPoint(write_block);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, write.value);
                    if (delay) {
                        builder.CreateCall(
                            write_after_slice_logic9_type,
                            logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(i32, signal),
                                write.offset,
                                write.width,
                                logic9_word_slot,
                                constant_i64(context, *delay) });
                    } else {
                        builder.CreateCall(
                            write_slice_logic9_type,
                            logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(i32, signal),
                                write.offset,
                                write.width,
                                logic9_word_slot });
                    }
                } else if (delay) {
                    builder.CreateCall(
                        write_after_slice_type,
                        logic4_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, signal),
                            write.offset,
                            write.width,
                            write.value.aval,
                            write.value.bval,
                            constant_i64(context, *delay) });
                } else {
                    builder.CreateCall(
                        write_slice_type,
                        logic4_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, signal),
                            write.offset,
                            write.width,
                            write.value.aval,
                            write.value.bval });
                }
                branch_to_next();
            };
        const auto emit_dynamic_after_slice =
            [&](const WriteAfterDynamicSlice& operation,
                llvm::Value* offset) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[operation.signal];
                const auto source = coerce_value_kind(
                    builder,
                    load_register(
                        builder, registers, operation.source),
                    signal_kind);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, source);
                    builder.CreateCall(
                        write_after_slice_logic9_type,
                        write_after_slice_logic9_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            logic9_word_slot,
                            constant_i64(context, operation.delay) });
                } else {
                    builder.CreateCall(
                        write_after_slice_type,
                        write_after_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delay) });
                }
                branch_to_next();
            };
        const auto emit_dynamic_inertial_slice =
            [&](const WriteInertialDynamicSlice& operation,
                llvm::Value* offset) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[operation.signal];
                const auto source = coerce_value_kind(
                    builder,
                    load_register(
                        builder, registers, operation.source),
                    signal_kind);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, source);
                    builder.CreateCall(
                        write_inertial_slice_logic9_type,
                        write_inertial_slice_logic9_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            logic9_word_slot,
                            constant_i64(
                                context, operation.delays.rise),
                            constant_i64(
                                context, operation.delays.fall),
                            constant_i64(
                                context, operation.delays.turnoff) });
                } else {
                    builder.CreateCall(
                        write_inertial_slice_type,
                        write_inertial_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(
                                context, operation.delays.rise),
                            constant_i64(
                                context, operation.delays.fall),
                            constant_i64(
                                context, operation.delays.turnoff) });
                }
                branch_to_next();
            };
        const auto emit_dynamic_projected_slice =
            [&](const WriteProjectedDynamicSlice& operation,
                llvm::Value* offset) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[operation.signal];
                const auto source = coerce_value_kind(
                    builder,
                    load_register(
                        builder, registers, operation.source),
                    signal_kind);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, source);
                    builder.CreateCall(
                        write_projected_slice_logic9_type,
                        write_projected_slice_logic9_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            logic9_word_slot,
                            constant_i64(context, operation.delay),
                            constant_i64(
                                context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                } else {
                    builder.CreateCall(
                        write_projected_slice_type,
                        write_projected_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delay),
                            constant_i64(
                                context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                }
                branch_to_next();
            };
        ValueOperationLowerer value_lowerer {
            builder,
            registers,
            context,
            i32,
            i64,
            branch_to_next,
            runtime_error_if,
            dynamic_offset,
            constant_part_select_sources[index],
            dynamic_part_signal_sources[index],
            context_pointer,
            process.id,
            instruction,
            read_signal_dynamic_part_callback,
            read_signal_dynamic_part_type,
            logic9_word_slot
        };
        SignalOperationLowerer signal_lowerer {
            builder,
            registers,
            signal_widths,
            signal_value_kinds,
            direct_read_signals,
            direct_update_signals,
            direct_update_slot_type,
            direct_update_slots,
            direct_update_active_words,
            require_direct_update_slots,
            context,
            i32,
            i64,
            context_pointer,
            direct_signal_aval,
            direct_signal_bval,
            direct_signal_logic9_plane0,
            direct_signal_logic9_plane1,
            direct_signal_logic9_plane2,
            direct_signal_logic9_plane3,
            direct_read_signal_map,
            direct_read_signal_count,
            direct_signal_count,
            process.id,
            instruction,
            read_callback,
            read_logic9_callback,
            write_callback,
            write_update_callback,
            write_after_callback,
            write_logic9_callback,
            write_update_logic9_callback,
            write_after_logic9_callback,
            write_blocking_slice_callback,
            write_update_slice_callback,
            write_after_slice_callback,
            write_blocking_slice_logic9_callback,
            write_update_slice_logic9_callback,
            write_after_slice_logic9_callback,
            force_signal_slice_callback,
            force_signal_slice_logic9_callback,
            release_signal_slice_callback,
            force_driver_signal_slice_callback,
            force_driver_signal_slice_logic9_callback,
            release_driver_signal_slice_callback,
            write_projected_waveform_callback,
            write_projected_waveform_logic9_callback,
            write_projected_callback,
            write_projected_logic9_callback,
            write_inertial_callback,
            write_inertial_logic9_callback,
            signal_event_callback,
            signal_last_value_callback,
            signal_last_value_logic9_callback,
            signal_last_event_callback,
            signal_active_callback,
            signal_last_active_callback,
            signal_driving_callback,
            signal_driving_value_callback,
            signal_driving_value_logic9_callback,
            read_simulation_time_callback,
            vital_timing_check_callback,
            vital_delay_callback,
            read_type,
            read_logic9_type,
            write_type,
            write_after_type,
            write_logic9_type,
            write_after_logic9_type,
            write_slice_type,
            write_after_slice_type,
            write_slice_logic9_type,
            write_after_slice_logic9_type,
            release_slice_type,
            write_projected_waveform_type,
            write_projected_waveform_logic9_type,
            write_projected_type,
            write_projected_logic9_type,
            write_inertial_type,
            write_inertial_logic9_type,
            signal_event_type,
            signal_last_value_type,
            signal_last_event_type,
            signal_active_type,
            signal_last_active_type,
            signal_driving_type,
            signal_driving_value_type,
            read_simulation_time_type,
            vital_timing_check_type,
            vital_delay_type,
            projected_element_type,
            logic9_projected_element_type,
            read_bval_slot,
            logic9_word_slot,
            branch_to_next,
            store_logic9_word,
            load_logic9_word,
            runtime_error_if,
            dynamic_offset
        };
        OutputOperationLowerer output_lowerer {
            builder,
            context,
            i32,
            context_pointer,
            process.id,
            instruction,
            index,
            symbol,
            output_type,
            time_output_type,
            report_type,
            output_callback,
            postponed_output_callback,
            time_output_callback,
            monitor_install_callback,
            monitor_control_callback,
            report_callback,
            branch_to_next,
            return_result
        };
        llvm::Value* ssa_callable_return = nullptr;
        if (native_callables.call_operations[index]) {
            const auto& call = fsim::runtime::simir::operation_get<Call>(
                process.operations[index]);
            const auto found = ssa_callable_returns.find(call.target);
            if (found != ssa_callable_returns.end()) {
                ssa_callable_return = found->second;
            }
        } else if (native_callables.return_operations[index]
            && native_callables.return_entries[index]) {
            const auto found = ssa_callable_returns.find(
                *native_callables.return_entries[index]);
            if (found != ssa_callable_returns.end()) {
                ssa_callable_return = found->second;
            }
        }
        ControlFlowOperationLowerer control_lowerer {
            builder,
            registers,
            context,
            i8,
            i32,
            i64,
            register_aval,
            register_bval,
            register_initialized,
            instruction_blocks,
            return_targets,
            native_return_targets,
            frame_type,
            frame_argument,
            native_callables.call_operations[index],
            native_callables.return_operations[index],
            native_callables.frame_operations[index],
            ssa_callable_return,
            invalid_pc,
            function,
            instruction,
            index,
            runtime_error_if,
            return_result
        };
        if (const auto& fused = fused_affine_dynamic_extracts[index]; fused) {
            const auto index_value = coerce_value_kind(
                builder,
                load_register(builder, registers, fused->index),
                ValueKind::logic4);
            runtime_error_if(
                builder.CreateICmpNE(
                    builder.CreateAnd(
                        index_value.bval,
                        constant_i64(
                            context,
                            std::numeric_limits<std::uint32_t>::max())),
                    constant_i64(context, 0U)),
                JitGeneratedRuntimeErrorReason::integer_operand_unknown,
                "affine.dynamic.extract.index.unknown");
            auto* signed_index = builder.CreateSExt(
                builder.CreateTrunc(index_value.aval, i32), i64);
            auto* selected_first = builder.CreateAdd(
                builder.CreateMul(
                    signed_index,
                    llvm::ConstantInt::getSigned(i64, fused->scale)),
                llvm::ConstantInt::getSigned(
                    i64, fused->constant_offset));
            auto* selected_last = builder.CreateAdd(
                selected_first,
                constant_i64(context, fused->width - 1U));
            auto* integer_minimum = llvm::ConstantInt::getSigned(
                i64, std::numeric_limits<std::int32_t>::min());
            auto* integer_maximum = llvm::ConstantInt::getSigned(
                i64, std::numeric_limits<std::int32_t>::max());
            runtime_error_if(
                builder.CreateOr(
                    builder.CreateOr(
                        builder.CreateICmpSLT(
                            selected_first, integer_minimum),
                        builder.CreateICmpSGT(
                            selected_first, integer_maximum)),
                    builder.CreateOr(
                        builder.CreateICmpSLT(
                            selected_last, integer_minimum),
                        builder.CreateICmpSGT(
                            selected_last, integer_maximum))),
                JitGeneratedRuntimeErrorReason::integer_overflow,
                "affine.dynamic.extract.index.overflow");
            auto* normalized_first = builder.CreateAdd(
                builder.CreateSub(
                    selected_first,
                    llvm::ConstantInt::getSigned(
                        i64, fused->source_right)),
                constant_i64(context, fused->source_base_offset));
            const auto source
                = load_register(builder, registers, fused->source);
            const auto work_width = std::max(source.width, fused->width);
            auto* work_type = packed_integer_type(context, work_width);
            auto* packed_result_type
                = packed_integer_type(context, fused->width);
            auto* zero_i64 = constant_i64(context, 0U);
            auto* work_width_i64 = constant_i64(context, work_width);
            auto* negative = builder.CreateICmpSLT(
                normalized_first, zero_i64);
            auto* magnitude = builder.CreateSelect(
                negative,
                builder.CreateNeg(normalized_first),
                normalized_first);
            auto* shift_in_range = builder.CreateICmpULT(
                magnitude, work_width_i64);
            auto* safe_shift = builder.CreateSelect(
                shift_in_range,
                magnitude,
                constant_i64(context, work_width - 1U));
            auto* packed_shift = builder.CreateZExtOrTrunc(
                safe_shift, work_type);
            const auto shift_plane = [&](llvm::Value* plane) {
                auto* widened = builder.CreateZExtOrTrunc(plane, work_type);
                auto* shifted = builder.CreateSelect(
                    negative,
                    builder.CreateShl(widened, packed_shift),
                    builder.CreateLShr(widened, packed_shift));
                shifted = builder.CreateSelect(
                    shift_in_range,
                    shifted,
                    llvm::ConstantInt::get(work_type, 0U));
                return builder.CreateZExtOrTrunc(
                    shifted, packed_result_type);
            };

            auto* result_width_i64 = constant_i64(context, fused->width);
            auto* lower_invalid = builder.CreateSelect(
                negative,
                builder.CreateSelect(
                    builder.CreateICmpULT(magnitude, result_width_i64),
                    magnitude,
                    result_width_i64),
                zero_i64);
            auto* upper_valid = builder.CreateSub(
                constant_i64(context, source.width), normalized_first);
            upper_valid = builder.CreateSelect(
                builder.CreateICmpSLT(upper_valid, zero_i64),
                zero_i64,
                builder.CreateSelect(
                    builder.CreateICmpSGT(
                        upper_valid, result_width_i64),
                    result_width_i64,
                    upper_valid));
            auto* has_valid = builder.CreateICmpULT(
                lower_invalid, upper_valid);
            auto* maximum_mask_shift
                = constant_i64(context, fused->width - 1U);
            auto* lower_shift = builder.CreateSelect(
                builder.CreateICmpULT(
                    lower_invalid, result_width_i64),
                lower_invalid,
                maximum_mask_shift);
            auto* upper_shift_count = builder.CreateSub(
                result_width_i64, upper_valid);
            auto* upper_shift = builder.CreateSelect(
                builder.CreateICmpULT(
                    upper_shift_count, result_width_i64),
                upper_shift_count,
                maximum_mask_shift);
            auto* result_mask = packed_mask(context, fused->width);
            auto* valid_mask = builder.CreateAnd(
                builder.CreateShl(
                    result_mask,
                    builder.CreateZExtOrTrunc(
                        lower_shift, packed_result_type)),
                builder.CreateLShr(
                    result_mask,
                    builder.CreateZExtOrTrunc(
                        upper_shift, packed_result_type)));
            valid_mask = builder.CreateSelect(
                has_valid,
                valid_mask,
                llvm::ConstantInt::get(packed_result_type, 0U));
            auto* invalid_mask = builder.CreateXor(valid_mask, result_mask);
            const auto destination_kind
                = registers[fused->destination].kind;
            const auto finish_plane = [&](llvm::Value* plane,
                                          const bool invalid_one) {
                auto* selected = builder.CreateAnd(
                    shift_plane(plane), valid_mask);
                return invalid_one
                    ? builder.CreateOr(selected, invalid_mask)
                    : selected;
            };
            store_register(
                builder,
                registers,
                fused->destination,
                EncodedValue {
                    finish_plane(source.aval, true),
                    finish_plane(
                        source.bval,
                        destination_kind != ValueKind::logic9),
                    fused->width,
                    finish_plane(source.logic9_plane2, false),
                    finish_plane(source.logic9_plane3, false),
                    destination_kind });
            builder.CreateBr(instruction_blocks[fused->resume]);
            continue;
        }
        fsim::runtime::simir::visit_operation(
            [&](const auto& operation) {
                using OperationType = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<OperationType, LoadConstant>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
                    if (signal_widths[operation.signal] > 64) {
                        if (operation.delay == 0U
                            && operation.rejection == 0U
                            && operation.mode
                                == runtime::simir::ProjectedDelayMode::inertial) {
                            write_wide_signal(
                                operation.signal,
                                operation.source,
                                0U,
                                FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE,
                                0U);
                        } else {
                            execute_exact_signal();
                        }
                    } else {
                        signal_lowerer.lower(operation);
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                    } else {
                        signal_lowerer.lower(operation);
                    }
                } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
                    if (operation.kind
                        != runtime::simir::SignalReadKind::current) {
                        return_result(
                            FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                            instruction,
                            0,
                            FSIM_JIT_FRAME_STATE_READY,
                            next_instruction);
                    } else if (signal_widths[operation.signal] > 64) {
                        read_wide_signal(operation);
                    } else {
                        signal_lowerer.lower(operation);
                    }
                } else if constexpr (std::is_same_v<OperationType, SignalEvent>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, SignalLastValue>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, SignalLastEvent>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, ReadSimulationTime>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, VitalTimingCheck>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, SignalActive>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalLastActive>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDriving>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDrivingValue>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
                    value_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, ConvertToTwoState>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, UnaryNot>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, LogicalNot>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, LogicalBinary>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Reduction>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, CountOnes>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, CountBits>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Shift>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Extract>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, DynamicExtract>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, DynamicPartSelect>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Insert>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, DynamicInsert>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, DynamicPartInsert>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Concatenate>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Binary>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, IntegerUnary>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, IntegerBinary>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, IntegerCheck>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, ConditionalSelect>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
                    if (signal_widths[operation.signal] > 64) {
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            0,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING,
                            0);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
                    if (signal_widths[operation.signal] > 64) {
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (std::ranges::find(
                                direct_update_signals, operation.signal)
                            != direct_update_signals.end()) {
                            if (!signal_lowerer.begin_direct_update(
                                    operation.signal, 0U, source)) {
                                throw LlvmJitError(
                                    "wide update accumulator layout mismatch");
                            }
                            if (require_direct_update_slots) {
                                return;
                            }
                        }
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            0,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE,
                            0);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
                    if (signal_widths[operation.signal] > 64) {
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            0,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER,
                            operation.delay);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            operation.offset,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_SLICE,
                            0);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (std::ranges::find(
                                direct_update_signals, operation.signal)
                            != direct_update_signals.end()) {
                            if (!signal_lowerer.begin_direct_update(
                                    operation.signal,
                                    operation.offset,
                                    source)) {
                                throw LlvmJitError(
                                    "wide slice-update accumulator layout mismatch");
                            }
                            if (require_direct_update_slots) {
                                return;
                            }
                        }
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            operation.offset,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE,
                            0);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            operation.offset,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_SLICE,
                            operation.delay);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    const auto source = coerce_value_kind(
                        builder,
                        load_register(
                            builder, registers, operation.source),
                        signal_kind);
                    if (signal_kind == ValueKind::logic9) {
                        store_logic9_word(logic9_word_slot, source);
                        builder.CreateCall(
                            write_inertial_slice_logic9_type,
                            write_inertial_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                llvm::ConstantInt::get(
                                    i32, operation.offset),
                                llvm::ConstantInt::get(i32, source.width),
                                logic9_word_slot,
                                constant_i64(
                                    context, operation.delays.rise),
                                constant_i64(
                                    context, operation.delays.fall),
                                constant_i64(
                                    context, operation.delays.turnoff) });
                        branch_to_next();
                        return;
                    }
                    builder.CreateCall(
                        write_inertial_slice_type,
                        write_inertial_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            llvm::ConstantInt::get(
                                i32, operation.offset),
                            llvm::ConstantInt::get(
                                i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delays.rise),
                            constant_i64(context, operation.delays.fall),
                            constant_i64(
                                context, operation.delays.turnoff) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    const auto source = coerce_value_kind(
                        builder,
                        load_register(
                            builder, registers, operation.source),
                        signal_kind);
                    if (signal_kind == ValueKind::logic9
                        && operation.delay == 0U
                        && operation.rejection == 0U
                        && operation.mode
                            == runtime::simir::ProjectedDelayMode::inertial
                        && signal_lowerer.begin_direct_update(
                            operation.signal, operation.offset, source)
                        && require_direct_update_slots) {
                        return;
                    }
                    if (signal_kind == ValueKind::logic9) {
                        store_logic9_word(logic9_word_slot, source);
                        builder.CreateCall(
                            write_projected_slice_logic9_type,
                            write_projected_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                llvm::ConstantInt::get(
                                    i32, operation.offset),
                                llvm::ConstantInt::get(i32, source.width),
                                logic9_word_slot,
                                constant_i64(
                                    context, operation.delay),
                                constant_i64(
                                    context, operation.rejection),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.mode)) });
                        branch_to_next();
                        return;
                    }
                    builder.CreateCall(
                        write_projected_slice_type,
                        write_projected_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            llvm::ConstantInt::get(
                                i32, operation.offset),
                            llvm::ConstantInt::get(
                                i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delay),
                            constant_i64(context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    if (signal_kind == ValueKind::logic9) {
                        auto* array_type = llvm::ArrayType::get(
                            logic9_projected_element_type,
                            operation.elements.size());
                        auto* storage = builder.CreateAlloca(
                            array_type,
                            nullptr,
                            "projected.logic9.slice.waveform");
                        for (std::size_t element_index = 0;
                            element_index < operation.elements.size();
                            ++element_index) {
                            const auto& element = operation.elements[element_index];
                            auto source = coerce_value_kind(
                                builder,
                                load_register(
                                    builder,
                                    registers,
                                    element.source),
                                ValueKind::logic9);
                            auto* slot = builder.CreateInBoundsGEP(
                                array_type,
                                storage,
                                { llvm::ConstantInt::get(i32, 0),
                                    llvm::ConstantInt::get(
                                        i32,
                                        static_cast<std::uint32_t>(
                                            element_index)) });
                            store_logic9_word(
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    0),
                                source);
                            builder.CreateStore(
                                constant_i64(context, element.delay),
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    1));
                        }
                        const auto first = load_register(
                            builder,
                            registers,
                            operation.elements.front().source);
                        builder.CreateCall(
                            write_projected_waveform_slice_logic9_type,
                            write_projected_waveform_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                llvm::ConstantInt::get(
                                    i32, operation.offset),
                                llvm::ConstantInt::get(i32, first.width),
                                storage,
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.elements.size())),
                                constant_i64(
                                    context, operation.rejection),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.mode)) });
                        branch_to_next();
                        return;
                    }
                    auto* array_type = llvm::ArrayType::get(
                        projected_element_type, operation.elements.size());
                    auto* storage = builder.CreateAlloca(
                        array_type, nullptr, "projected.slice.waveform");
                    for (std::size_t element_index = 0;
                        element_index < operation.elements.size();
                        ++element_index) {
                        const auto& element = operation.elements[element_index];
                        const auto source = load_register(builder, registers, element.source);
                        auto* slot = builder.CreateInBoundsGEP(
                            array_type,
                            storage,
                            { llvm::ConstantInt::get(i32, 0),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(element_index)) });
                        builder.CreateStore(
                            source.aval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 0));
                        builder.CreateStore(
                            source.bval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 1));
                        builder.CreateStore(
                            constant_i64(context, element.delay),
                            builder.CreateStructGEP(
                                projected_element_type, slot, 2));
                    }
                    const auto first = load_register(
                        builder, registers, operation.elements.front().source);
                    builder.CreateCall(
                        write_projected_waveform_slice_type,
                        write_projected_waveform_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, operation.signal),
                            llvm::ConstantInt::get(i32, operation.offset),
                            llvm::ConstantInt::get(i32, first.width),
                            storage,
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.elements.size())),
                            constant_i64(context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_slice(
                        operation.signal,
                        operation.source,
                        dynamic_offset_i32(operation.selection),
                        write_blocking_slice_callback,
                        write_blocking_slice_logic9_callback);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        auto* offset = dynamic_offset_i32(operation.selection);
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (std::ranges::find(
                                direct_update_signals, operation.signal)
                            != direct_update_signals.end()) {
                            if (!signal_lowerer.begin_direct_update(
                                    operation.signal,
                                    offset,
                                    llvm::ConstantInt::get(i32, source.width),
                                    source)) {
                                throw LlvmJitError(
                                    "wide dynamic-update accumulator layout mismatch");
                            }
                            if (require_direct_update_slots) {
                                return;
                            }
                        }
                        execute_exact_signal();
                        return;
                    }
                    auto* offset = dynamic_offset_i32(operation.selection);
                    if (std::ranges::find(
                            direct_update_signals, operation.signal)
                        == direct_update_signals.end()) {
                        emit_dynamic_slice(
                            operation.signal,
                            operation.source,
                            offset,
                            write_update_slice_callback,
                            write_update_slice_logic9_callback);
                    } else {
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (!signal_lowerer.begin_direct_update(
                                operation.signal,
                                offset,
                                llvm::ConstantInt::get(i32, source.width),
                                source)) {
                            throw LlvmJitError(
                                "dynamic update accumulator layout mismatch");
                        }
                        if (require_direct_update_slots) {
                            return;
                        }
                        builder.CreateCall(
                            write_slice_type,
                            write_update_slice_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                offset,
                                llvm::ConstantInt::get(i32, source.width),
                                source.aval,
                                source.bval });
                        branch_to_next();
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_after_slice(
                        operation,
                        dynamic_offset_i32(operation.selection));
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
                    if (signal_widths[operation.signal] > 64
                        || operation.selection.width > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_part_slice(
                        operation.signal,
                        operation.source,
                        operation.selection,
                        write_blocking_slice_callback,
                        write_blocking_slice_logic9_callback,
                        std::nullopt);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
                    if (signal_widths[operation.signal] > 64
                        || operation.selection.width > 64) {
                        if (signal_widths[operation.signal] > 64
                            && operation.selection.width <= 64U) {
                            const auto write = lower_dynamic_part_write(
                                builder,
                                context,
                                i32,
                                i64,
                                registers,
                                operation.source,
                                operation.selection,
                                ValueKind::logic4);
                            auto* write_block = llvm::BasicBlock::Create(
                                context,
                                "dynamic.part.wide.update."
                                    + std::to_string(index),
                                function);
                            builder.CreateCondBr(
                                builder.CreateICmpNE(
                                    write.width,
                                    llvm::ConstantInt::get(i32, 0)),
                                write_block,
                                instruction_blocks[index + 1]);
                            builder.SetInsertPoint(write_block);
                            if (std::ranges::find(
                                    direct_update_signals,
                                    operation.signal)
                                != direct_update_signals.end()) {
                                if (!signal_lowerer.begin_direct_update(
                                        operation.signal,
                                        write.offset,
                                        write.width,
                                        write.value)) {
                                    throw LlvmJitError(
                                        "wide dynamic part-update accumulator "
                                        "layout mismatch");
                                }
                                if (require_direct_update_slots) {
                                    return;
                                }
                            }
                        }
                        execute_exact_signal();
                        return;
                    }
                    if (std::ranges::find(
                            direct_update_signals, operation.signal)
                        == direct_update_signals.end()) {
                        emit_dynamic_part_slice(
                            operation.signal,
                            operation.source,
                            operation.selection,
                            write_update_slice_callback,
                            write_update_slice_logic9_callback,
                            std::nullopt);
                    } else {
                        const auto write = lower_dynamic_part_write(
                            builder,
                            context,
                            i32,
                            i64,
                            registers,
                            operation.source,
                            operation.selection,
                            ValueKind::logic4);
                        auto* write_block = llvm::BasicBlock::Create(
                            context,
                            "dynamic.part.update." + std::to_string(index),
                            function);
                        builder.CreateCondBr(
                            builder.CreateICmpNE(
                                write.width,
                                llvm::ConstantInt::get(i32, 0)),
                            write_block,
                            instruction_blocks[index + 1]);
                        builder.SetInsertPoint(write_block);
                        if (!signal_lowerer.begin_direct_update(
                                operation.signal,
                                write.offset,
                                write.width,
                                write.value)) {
                            throw LlvmJitError(
                                "dynamic part-update accumulator layout mismatch");
                        }
                        if (require_direct_update_slots) {
                            return;
                        }
                        builder.CreateCall(
                            write_slice_type,
                            write_update_slice_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                write.offset,
                                write.width,
                                write.value.aval,
                                write.value.bval });
                        branch_to_next();
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
                    if (signal_widths[operation.signal] > 64
                        || operation.selection.width > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_part_slice(
                        operation.signal,
                        operation.source,
                        operation.selection,
                        write_after_slice_callback,
                        write_after_slice_logic9_callback,
                        operation.delay);
                } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_inertial_slice(
                        operation,
                        dynamic_offset_i32(operation.selection));
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         WriteInertialDynamicPartSlice>) {
                    execute_exact_signal();
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
                    emit_dynamic_projected_slice(
                        operation,
                        dynamic_offset_i32(operation.selection));
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    auto* offset = dynamic_offset_i32(operation.selection);
                    if (signal_kind == ValueKind::logic9) {
                        auto* array_type = llvm::ArrayType::get(
                            logic9_projected_element_type,
                            operation.elements.size());
                        auto* storage = builder.CreateAlloca(
                            array_type,
                            nullptr,
                            "projected.logic9.dynamic.slice.waveform");
                        for (std::size_t element_index = 0;
                            element_index < operation.elements.size();
                            ++element_index) {
                            const auto& element = operation.elements[element_index];
                            auto source = coerce_value_kind(
                                builder,
                                load_register(
                                    builder,
                                    registers,
                                    element.source),
                                ValueKind::logic9);
                            auto* slot = builder.CreateInBoundsGEP(
                                array_type,
                                storage,
                                { llvm::ConstantInt::get(i32, 0),
                                    llvm::ConstantInt::get(
                                        i32,
                                        static_cast<std::uint32_t>(
                                            element_index)) });
                            store_logic9_word(
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    0),
                                source);
                            builder.CreateStore(
                                constant_i64(context, element.delay),
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    1));
                        }
                        const auto first = load_register(
                            builder,
                            registers,
                            operation.elements.front().source);
                        builder.CreateCall(
                            write_projected_waveform_slice_logic9_type,
                            write_projected_waveform_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                offset,
                                llvm::ConstantInt::get(
                                    i32, first.width),
                                storage,
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.elements.size())),
                                constant_i64(
                                    context, operation.rejection),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.mode)) });
                        branch_to_next();
                        return;
                    }
                    auto* array_type = llvm::ArrayType::get(
                        projected_element_type,
                        operation.elements.size());
                    auto* storage = builder.CreateAlloca(
                        array_type,
                        nullptr,
                        "projected.dynamic.slice.waveform");
                    for (std::size_t element_index = 0;
                        element_index < operation.elements.size();
                        ++element_index) {
                        const auto& element = operation.elements[element_index];
                        const auto source = load_register(
                            builder, registers, element.source);
                        auto* slot = builder.CreateInBoundsGEP(
                            array_type,
                            storage,
                            { llvm::ConstantInt::get(i32, 0),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        element_index)) });
                        builder.CreateStore(
                            source.aval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 0));
                        builder.CreateStore(
                            source.bval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 1));
                        builder.CreateStore(
                            constant_i64(context, element.delay),
                            builder.CreateStructGEP(
                                projected_element_type, slot, 2));
                    }
                    const auto first = load_register(
                        builder,
                        registers,
                        operation.elements.front().source);
                    builder.CreateCall(
                        write_projected_waveform_slice_type,
                        write_projected_waveform_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, first.width),
                            storage,
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.elements.size())),
                            constant_i64(
                                context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, Assert>) {
                    const auto condition = load_register(builder, registers, operation.condition);
                    auto* known = builder.CreateICmpEQ(
                        condition.bval, constant_i64(context, 0));
                    auto* one = builder.CreateICmpEQ(condition.aval,
                        constant_i64(context, 1));
                    auto* passed = builder.CreateAnd(known, one);
                    auto* failed_block = llvm::BasicBlock::Create(
                        context, "assert.failed." + std::to_string(index),
                        function);
                    builder.CreateCondBr(
                        passed, instruction_blocks[index + 1], failed_block);

                    builder.SetInsertPoint(failed_block);
                    if (operation.severity
                        == runtime::simir::AssertionSeverity::failure) {
                        const auto& message = operation.message.empty()
                            ? std::string { "assertion failed" }
                            : operation.message;
                        auto* message_pointer = builder.CreateGlobalString(
                            message,
                            symbol + ".assert." + std::to_string(index));
                        builder.CreateCall(
                            assert_type,
                            assert_callback,
                            {
                                context_pointer,
                                llvm::ConstantInt::get(i32, process.id),
                                llvm::ConstantInt::get(i32, instruction),
                                message_pointer,
                                constant_i64(context, message.size()),
                            });
                        return_result(
                            FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED,
                            instruction,
                            0,
                            FSIM_JIT_FRAME_STATE_ASSERTION_FAILED,
                            instruction);
                    } else {
                        builder.CreateCall(
                            report_type,
                            report_callback,
                            {
                                context_pointer,
                                llvm::ConstantInt::get(i32, process.id),
                                llvm::ConstantInt::get(i32, instruction),
                            });
                        builder.CreateBr(
                            instruction_blocks[index + 1]);
                    }
                } else if constexpr (std::is_same_v<OperationType, DebugPoint>) {
                    if (!debug_instrumentation) {
                        branch_to_next();
                        return;
                    }
                    auto* enabled = builder.CreateICmpNE(
                        builder.CreateAnd(
                            runtime_flags,
                            llvm::ConstantInt::get(
                                i32, FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS)),
                        llvm::ConstantInt::get(i32, 0));
                    auto* enabled_block = llvm::BasicBlock::Create(
                        context,
                        "debug.enabled." + std::to_string(index),
                        function);
                    builder.CreateCondBr(
                        enabled, enabled_block,
                        instruction_blocks[index + 1]);
                    builder.SetInsertPoint(enabled_block);
                    return_result(
                        FSIM_JIT_RESUME_STATUS_DEBUG_POINT, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Display>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, FormatDisplay>) {
                    const auto value = load_register(builder, registers, operation.source);
                    if (value.kind == ValueKind::logic9) {
                        store_logic9_word(logic9_word_slot, value);
                        builder.CreateCall(
                            formatted_output_logic9_type,
                            write_formatted_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(i32, process.id),
                                llvm::ConstantInt::get(i32, instruction),
                                llvm::ConstantInt::get(i32, value.width),
                                logic9_word_slot });
                        branch_to_next();
                        return;
                    }
                    builder.CreateCall(
                        formatted_output_type,
                        formatted_output_callback,
                        {
                            context_pointer,
                            llvm::ConstantInt::get(i32, process.id),
                            llvm::ConstantInt::get(i32, instruction),
                            llvm::ConstantInt::get(i32, value.width),
                            value.aval,
                            value.bval,
                        });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, TimeDisplay>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, MonitorInstall>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
                    output_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, TimeFormatControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, PlusArgSelect>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, SystemCommand>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, VcdControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType,
                        CoverageDatabaseControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, StochasticQueueOperation>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, PlaEvaluate>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, CoverageSample>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, CoverageQuery>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, RandomValue>) {
                    const auto zero = constant_i64(context, 0);
                    const auto maximum = operation.maximum
                        ? load_register(
                              builder, registers, *operation.maximum)
                        : EncodedValue { zero, zero, 32 };
                    const auto minimum = operation.minimum
                        ? load_register(
                              builder, registers, *operation.minimum)
                        : EncodedValue { zero, zero, 32 };
                    builder.CreateStore(zero, read_bval_slot);
                    auto* aval = builder.CreateCall(
                        random_value_type,
                        random_value_callback,
                        {
                            context_pointer,
                            llvm::ConstantInt::get(i32, process.id),
                            llvm::ConstantInt::get(i32, instruction),
                            maximum.aval,
                            maximum.bval,
                            minimum.aval,
                            minimum.bval,
                            read_bval_slot,
                        });
                    auto* bval = builder.CreateLoad(
                        i64, read_bval_slot, "random.bval");
                    store_register(
                        builder,
                        registers,
                        operation.destination,
                        EncodedValue { aval, bval, 32 });
                    branch_to_next();
                } else if constexpr (
                    std::is_same_v<OperationType, RandomDistribution>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Report>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, StringReport>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Jump>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Call>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Return>) {
                    control_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePush>) {
                    control_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePop>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Branch>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WaitRegion>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WaitPla>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitOrder>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, EventTriggered>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, EventAlias>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    operation_group_contains_v<OperationType, ClassOperationGroup>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitSensitivity>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitForever>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_WAIT_FOREVER, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Yield>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_YIELDED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Fork>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_FORK, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, ForkEnd>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_FORK_END, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitFork>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_WAIT_FORK, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, DisableFork>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_DISABLE_FORK, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, DisableBlock>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessSelf>
                    || std::is_same_v<OperationType, ProcessStatusQuery>
                    || std::is_same_v<OperationType, ProcessCompleted>
                    || std::is_same_v<OperationType, ProcessAwait>
                    || std::is_same_v<OperationType, ProcessKill>
                    || std::is_same_v<OperationType, ProcessSuspend>
                    || std::is_same_v<OperationType, ProcessResume>
                    || std::is_same_v<OperationType, ProcessGetRandState>
                    || std::is_same_v<OperationType, ProcessSetRandState>
                    || std::is_same_v<OperationType, ProcessSrandom>
                    || std::is_same_v<OperationType, MailboxCreate>
                    || std::is_same_v<OperationType, MailboxPut>
                    || std::is_same_v<OperationType, MailboxGet>
                    || std::is_same_v<OperationType, MailboxNum>
                    || std::is_same_v<OperationType, SemaphoreCreate>
                    || std::is_same_v<OperationType, SemaphoreGet>
                    || std::is_same_v<OperationType, SemaphorePut>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Pause>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_PAUSED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Stop>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_STOPPED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_STOPPED, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Halt>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_COMPLETED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_COMPLETED, next_instruction);
                } else {
                    if constexpr (
                        requires(FileOperationLowerer& lowerer) {
                            lowerer.lower(operation);
                        }) {
                        FileOperationLowerer file_lowerer {
                            builder,
                            registers,
                            context,
                            i32,
                            i64,
                            context_pointer,
                            process.id,
                            instruction,
                            runtime_type,
                            runtime_argument,
                            runtime_error_if,
                            branch_to_next
                        };
                        file_lowerer.lower(operation);
                    } else if constexpr (
                        requires(StringOperationLowerer& lowerer) {
                            lowerer.lower(operation);
                        }) {
                        StringOperationLowerer string_lowerer {
                            module,
                            builder,
                            registers,
                            context,
                            i32,
                            i64,
                            context_pointer,
                            process.id,
                            instruction,
                            runtime_type,
                            runtime_argument,
                            runtime_error_if,
                            branch_to_next
                        };
                        string_lowerer.lower(operation);
                    } else if constexpr (
                        requires(ContainerOperationLowerer& lowerer) {
                            lowerer.lower(operation);
                        }) {
                        ContainerOperationLowerer container_lowerer {
                            builder,
                            registers,
                            context,
                            i32,
                            i64,
                            context_pointer,
                            process.id,
                            instruction,
                            runtime_type,
                            runtime_argument,
                            process.container_register_types,
                            container_result_aval_slot,
                            container_result_bval_slot,
                            fused_container_object_reads[index],
                            runtime_error_if,
                            branch_to_next
                        };
                        container_lowerer.lower(operation);
                    } else {
                        llvm_unreachable(
                            "unsupported operations were rejected before lowering");
                    }
                }
            },
            process.operations[index]);
        for (auto block = std::next(last_block_before_lowering->getIterator());
             block != function->end(); ++block) {
            instruction_regions[index].push_back(&*block);
        }
    }

    if (!lowering_plan.partial
        && optimization == JitOptimizationLevel::o1
        && process.operations.size() > 8192U) {
        auto regions = native_callables.regions;
        std::ranges::sort(regions, [](const auto& lhs, const auto& rhs) {
            const auto lhs_size = lhs.second - lhs.first;
            const auto rhs_size = rhs.second - rhs.first;
            return lhs_size != rhs_size ? lhs_size > rhs_size
                                        : lhs.first < rhs.first;
        });
        std::vector<std::pair<InstructionIndex, InstructionIndex>> selected;
        for (const auto& region : regions) {
            if (region.first > region.second
                || region.second >= instruction_regions.size()
                || std::ranges::any_of(selected, [&](const auto& existing) {
                       return region.first <= existing.second
                           && existing.first <= region.second;
                   })) {
                continue;
            }
            selected.push_back(region);
        }
        std::ranges::sort(selected);
        llvm::DominatorTree initial_dominators(*function);
        std::vector<std::vector<llvm::BasicBlock*>> outline_regions;
        const std::array whole_process {
            std::pair {
                InstructionIndex { 0 },
                static_cast<InstructionIndex>(process.operations.size() - 1U)
            }
        };
        for (const auto& [first, last] : whole_process) {
            constexpr std::size_t maximum_region_operations = 256U;
            for (std::size_t window_first = first; window_first <= last;
                 window_first += maximum_region_operations) {
                const auto window_last = std::min<std::size_t>(
                    last, window_first + maximum_region_operations - 1U);
                std::set<llvm::BasicBlock*> candidates;
                for (std::size_t instruction = window_first;
                     instruction <= window_last; ++instruction) {
                    candidates.insert(
                        instruction_regions[instruction].begin(),
                        instruction_regions[instruction].end());
                }
                std::vector<llvm::BasicBlock*> entries;
                for (auto* block : candidates) {
                    const bool external_predecessor = std::ranges::any_of(
                        llvm::predecessors(block), [&](auto* predecessor) {
                            return !candidates.contains(predecessor);
                        });
                    if (block == instruction_blocks[window_first]
                        || external_predecessor) {
                        entries.push_back(block);
                    }
                }
                std::map<llvm::BasicBlock*, std::vector<llvm::BasicBlock*>>
                    partitions;
                for (auto* block : candidates) {
                    llvm::BasicBlock* owner = nullptr;
                    unsigned owner_level = 0;
                    for (auto* region_entry : entries) {
                        if (!initial_dominators.dominates(
                                region_entry, block)) {
                            continue;
                        }
                        const auto level
                            = initial_dominators.getNode(region_entry)->getLevel();
                        if (owner == nullptr || level > owner_level) {
                            owner = region_entry;
                            owner_level = level;
                        }
                    }
                    if (owner != nullptr) {
                        partitions[owner].push_back(block);
                    }
                }
                for (auto& [region_entry, blocks] : partitions) {
                    const auto found = std::ranges::find(blocks, region_entry);
                    if (found != blocks.end()) {
                        std::iter_swap(blocks.begin(), found);
                    }
                    if (blocks.size() >= 16U) {
                        outline_regions.push_back(std::move(blocks));
                    }
                }
            }
        }
        std::ranges::sort(outline_regions, [](const auto& lhs, const auto& rhs) {
            return lhs.size() > rhs.size();
        });
        if (outline_regions.size() > 256U) {
            outline_regions.resize(256U);
        }
        std::size_t eligible_regions = 0;
        std::size_t extracted_regions = 0;
        llvm::DominatorTree dominators(*function);
        llvm::CodeExtractorAnalysisCache analysis(*function);
        for (auto& blocks : outline_regions) {
            llvm::CodeExtractor extractor(
                blocks,
                &dominators,
                false,
                nullptr,
                nullptr,
                nullptr,
                false,
                true,
                &function->getEntryBlock(),
                "region");
            if (extractor.isEligible()) {
                ++eligible_regions;
                extracted_regions += extractor.extractCodeRegion(analysis)
                    != nullptr;
            }
        }
        if (std::getenv("FSIM_PROFILE_LLVM_MODULES") != nullptr) {
            llvm::errs() << "FSIM-LLVM-OUTLINE-PROFILE process=" << process.id
                         << " candidates=" << regions.size()
                         << " selected=" << selected.size()
                         << " partitions=" << outline_regions.size()
                         << " eligible=" << eligible_regions
                         << " extracted=" << extracted_regions << '\n';
        }
    }

    if (!debug_instrumentation) {
        const auto blocks_before = function->size();
        const auto merged = coalesce_linear_blocks(*function);
        if (std::getenv("FSIM_PROFILE_LLVM_MODULES") != nullptr) {
            llvm::errs() << "FSIM-LLVM-CFG-COALESCE process=" << process.id
                         << " blocks_before=" << blocks_before
                         << " blocks_after=" << function->size()
                         << " merged=" << merged << '\n';
        }
    }
}
} // namespace fsim::compiler::llvm_detail

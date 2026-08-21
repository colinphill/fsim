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
#include "llvm_jit_lowering_setup.tpp"
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
#include "llvm_jit_lowering_operations_prefix.tpp"
#include "llvm_jit_lowering_operations_suffix.tpp"

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
            llvm::errs() << "fsim-profile: llvm-outline process=" << process.id
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
            llvm::errs() << "fsim-profile: llvm-cfg-coalesce process=" << process.id
                         << " blocks_before=" << blocks_before
                         << " blocks_after=" << function->size()
                         << " merged=" << merged << '\n';
        }
    }
}
} // namespace fsim::compiler::llvm_detail

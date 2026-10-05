// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_fast_isel_census.hpp"
#include "llvm_jit_internal.hpp"
#include "llvm_jit_test_support.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/raw_ostream.h>

#include <cassert>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace fsim::tests::compiler {
namespace {

using fsim::compiler::JitBackendTierHint;
using fsim::compiler::llvm_detail::FastIselCensus;
using fsim::compiler::llvm_detail::FastIselCensusSummary;
using fsim::compiler::llvm_detail::LlvmBackendTier;
using fsim::compiler::llvm_detail::kTieredDirectReadLoadMetadata;
using fsim::compiler::llvm_detail::kTieredSafeFrameStoreMetadata;
using fsim::compiler::llvm_detail::kLessBackendTierInstructionLimit;
using fsim::compiler::llvm_detail::make_native_module_cache_key;
using fsim::compiler::llvm_detail::run_tiered_direct_read_dedup;
using fsim::compiler::llvm_detail::select_backend_tier;

class ForwardingHandler final : public llvm::DiagnosticHandler {
public:
    ForwardingHandler(std::size_t& forwarded, bool forward_sdagisel)
        : forwarded_(forwarded)
        , forward_sdagisel_(forward_sdagisel)
    {
    }

    bool isMissedOptRemarkEnabled(llvm::StringRef pass) const override
    {
        return forward_sdagisel_ && pass == "sdagisel";
    }

    bool handleDiagnostics(const llvm::DiagnosticInfo&) override
    {
        ++forwarded_;
        return true;
    }

private:
    std::size_t& forwarded_;
    bool forward_sdagisel_ { };
};

class SyntheticMissedRemark final : public llvm::OptimizationRemarkMissed {
public:
    SyntheticMissedRemark(
        const char* pass,
        llvm::StringRef name,
        const llvm::DiagnosticLocation& location,
        const llvm::BasicBlock* block,
        llvm::StringRef message)
        : OptimizationRemarkMissed(pass, name, location, block)
    {
        *this << message;
    }

    bool isEnabled() const override { return true; }
};

struct DiagnosticFixture {
    llvm::Function* function { };
    llvm::BasicBlock* block { };
};

DiagnosticFixture add_function(
    llvm::LLVMContext& context,
    llvm::Module& module,
    const llvm::StringRef name)
{
    auto* const type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), false);
    auto* const function = llvm::Function::Create(
        type, llvm::Function::ExternalLinkage, name, module);
    auto* const block = llvm::BasicBlock::Create(context, "entry", function);
    return { function, block };
}

void emit_fallback(
    llvm::LLVMContext& context,
    const llvm::BasicBlock& block,
    const llvm::StringRef message)
{
    auto remark = SyntheticMissedRemark(
        "sdagisel", "FastISelFailure", llvm::DiagnosticLocation { },
        &block, message);
    context.diagnose(remark);
}

FastIselCensusSummary collect_one(
    llvm::LLVMContext& context,
    llvm::Module& module,
    const llvm::BasicBlock& block,
    const llvm::StringRef message,
    std::string& output_text)
{
    llvm::raw_string_ostream output { output_text };
    FastIselCensus census { module, true };
    emit_fallback(context, block, message);
    const auto summary = census.summary();
    census.report(true, output);
    output.flush();
    return summary;
}

void test_tier_selection_and_cache_identity()
{
    using fsim::compiler::JitProcessModuleEntry;
    using fsim::compiler::llvm_detail::backend_tier_eligible;
    using fsim::compiler::llvm_detail::module_backend_tier_eligible;
    using fsim::compiler::llvm_detail::select_backend_tier;
    using fsim::compiler::llvm_detail::valid_backend_tier_proof;

    assert(!backend_tier_eligible(
        JitProcessModuleEntry { .symbol = "shared_63",
            .backend_tier_hint
            = JitBackendTierHint::shared_process_template,
            .bound_instance_count = 63U }));
    assert(backend_tier_eligible(
        JitProcessModuleEntry { .symbol = "shared_64",
            .backend_tier_hint
            = JitBackendTierHint::shared_process_template,
            .bound_instance_count = 64U }));
    assert(!backend_tier_eligible(
        JitProcessModuleEntry { .symbol = "empty_static",
            .backend_tier_hint
            = JitBackendTierHint::fused_static_cohort,
            .bound_instance_count = 0U }));
    assert(backend_tier_eligible(
        JitProcessModuleEntry { .symbol = "one_static",
            .backend_tier_hint
            = JitBackendTierHint::fused_static_cohort,
            .bound_instance_count = 1U }));
    assert(backend_tier_eligible(
        JitProcessModuleEntry { .symbol = "masked_region",
            .backend_tier_hint
            = JitBackendTierHint::fused_masked_region,
            .bound_instance_count = 3U }));
    assert(!backend_tier_eligible(JitProcessModuleEntry { }));

    const std::array eligible_entries {
        JitProcessModuleEntry { .symbol = "shared_template",
            .backend_tier_hint
            = JitBackendTierHint::shared_process_template,
            .bound_instance_count = 64U },
        JitProcessModuleEntry { .symbol = "fused_cohort",
            .backend_tier_hint
            = JitBackendTierHint::fused_static_cohort,
            .bound_instance_count = 2U },
    };
    assert(module_backend_tier_eligible(eligible_entries));
    auto one_unselected_entry = eligible_entries;
    one_unselected_entry[1].backend_tier_hint = JitBackendTierHint::none;
    assert(!module_backend_tier_eligible(one_unselected_entry));
    assert(!module_backend_tier_eligible({ }));

    assert(select_backend_tier(false, 1U) == LlvmBackendTier::none);
    assert(select_backend_tier(true, 0U) == LlvmBackendTier::less);
    assert(select_backend_tier(
        true, kLessBackendTierInstructionLimit) == LlvmBackendTier::less);
    assert(select_backend_tier(
        true, kLessBackendTierInstructionLimit + 1U)
        == LlvmBackendTier::none);
    assert(valid_backend_tier_proof(
        LlvmBackendTier::less, true,
        kLessBackendTierInstructionLimit));
    assert(!valid_backend_tier_proof(
        LlvmBackendTier::less, true,
        kLessBackendTierInstructionLimit + 1U));

    const std::array process_keys { std::string { "process-key" } };
    const auto less_key = make_native_module_cache_key(
        "module", process_keys, LlvmBackendTier::less, true);
    const auto none_eligible_key = make_native_module_cache_key(
        "module", process_keys, LlvmBackendTier::none, true);
    const auto none_ineligible_key = make_native_module_cache_key(
        "module", process_keys, LlvmBackendTier::none, false);
    assert(less_key != none_eligible_key);
    assert(none_eligible_key != none_ineligible_key);
}

void test_tiered_direct_read_dedup_barriers()
{
    auto context = llvm::LLVMContext { };
    auto module = llvm::Module { "tiered-read-dedup", context };
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const function_type = llvm::FunctionType::get(
        i64, { pointer, pointer }, false);
    auto* const function = llvm::Function::Create(
        function_type, llvm::Function::ExternalLinkage,
        "tiered_read_dedup", module);
    auto argument = function->arg_begin();
    auto* const input = &*argument++;
    input->setName("input");
    auto* const frame = &*argument;
    frame->setName("frame");
    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    auto builder = llvm::IRBuilder<> { entry };
    auto* const first = builder.CreateLoad(i64, input, "first");
    const auto mark_load = [&](llvm::LoadInst* const load) {
        llvm::Metadata* const operands[] {
            llvm::ConstantAsMetadata::get(
                llvm::ConstantInt::get(i32, 7U)),
            llvm::ConstantAsMetadata::get(
                llvm::ConstantInt::get(i32, 1U)),
        };
        load->setMetadata(context.getMDKindID(
            kTieredDirectReadLoadMetadata),
            llvm::MDNode::get(context,
                llvm::ArrayRef<llvm::Metadata*> { operands }));
    };
    mark_load(first);
    auto* const safe_store = builder.CreateStore(
        llvm::ConstantInt::get(i64, 0U), frame);
    safe_store->setMetadata(context.getMDKindID(
        kTieredSafeFrameStoreMetadata),
        llvm::MDNode::get(context, llvm::ArrayRef<llvm::Metadata*> { }));
    auto* const repeated = builder.CreateLoad(i64, input, "repeated");
    mark_load(repeated);
    auto* const callback_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), false);
    auto* const callback = llvm::Function::Create(
        callback_type, llvm::Function::ExternalLinkage,
        "observable_callback", module);
    builder.CreateCall(callback);
    auto* const after_callback = builder.CreateLoad(
        i64, input, "after.callback");
    mark_load(after_callback);
    auto* const sum = builder.CreateAdd(first, repeated, "sum");
    auto* const total = builder.CreateAdd(sum, after_callback, "total");
    builder.CreateRet(total);

    const auto statistics = run_tiered_direct_read_dedup(
        module, LlvmBackendTier::less, true);
    assert(statistics.marked_loads == 3U);
    assert(statistics.eliminated_loads == 1U);
    assert(statistics.marked_value_loads == 3U);
    assert(statistics.eliminated_value_loads == 1U);
    assert(after_callback->getParent() != nullptr);
    std::size_t remaining_marked_loads { };
    for (const auto& block : *function) {
        for (const auto& instruction : block) {
            const auto* const load
                = llvm::dyn_cast<llvm::LoadInst>(&instruction);
            remaining_marked_loads += load != nullptr
                && load->getMetadata(context.getMDKindID(
                    kTieredDirectReadLoadMetadata)) != nullptr;
        }
    }
    assert(remaining_marked_loads == 2U);
    assert(fsim::compiler::llvm_detail::verify_error(module).empty());

    auto barrier_module = llvm::Module { "tiered-read-ordinary-store", context };
    auto* const barrier_function = llvm::Function::Create(
        function_type, llvm::Function::ExternalLinkage,
        "tiered_read_ordinary_store", barrier_module);
    auto barrier_argument = barrier_function->arg_begin();
    auto* const barrier_input = &*barrier_argument++;
    auto* const barrier_frame = &*barrier_argument;
    auto* const barrier_entry = llvm::BasicBlock::Create(
        context, "entry", barrier_function);
    auto barrier_builder = llvm::IRBuilder<> { barrier_entry };
    auto* const before_store = barrier_builder.CreateLoad(
        i64, barrier_input, "before.store");
    const auto barrier_tag = context.getMDKindID(
        kTieredDirectReadLoadMetadata);
    before_store->setMetadata(barrier_tag,
        llvm::MDNode::get(context,
            llvm::ArrayRef<llvm::Metadata*> {
                llvm::ConstantAsMetadata::get(
                    llvm::ConstantInt::get(i32, 7U)),
                llvm::ConstantAsMetadata::get(
                    llvm::ConstantInt::get(i32, 1U)) }));
    barrier_builder.CreateStore(llvm::ConstantInt::get(i64, 0U),
        barrier_frame);
    auto* const after_store = barrier_builder.CreateLoad(
        i64, barrier_input, "after.store");
    after_store->setMetadata(barrier_tag,
        llvm::MDNode::get(context,
            llvm::ArrayRef<llvm::Metadata*> {
                llvm::ConstantAsMetadata::get(
                    llvm::ConstantInt::get(i32, 7U)),
                llvm::ConstantAsMetadata::get(
                    llvm::ConstantInt::get(i32, 1U)) }));
    barrier_builder.CreateRet(barrier_builder.CreateAdd(
        before_store, after_store));
    const auto barrier_statistics
        = run_tiered_direct_read_dedup(
            barrier_module, LlvmBackendTier::less, true);
    assert(barrier_statistics.marked_loads == 2U);
    assert(barrier_statistics.eliminated_loads == 0U);

    auto join_module = llvm::Module { "tiered-read-dedup-join", context };
    auto* const join_type = llvm::FunctionType::get(
        i64, { pointer, llvm::Type::getInt1Ty(context) }, false);
    auto* const join_function = llvm::Function::Create(
        join_type, llvm::Function::ExternalLinkage,
        "tiered_read_dedup_join", join_module);
    auto join_argument = join_function->arg_begin();
    auto* const join_input = &*join_argument++;
    auto* const join_condition = &*join_argument;
    auto* const join_entry = llvm::BasicBlock::Create(
        context, "entry", join_function);
    auto* const left = llvm::BasicBlock::Create(
        context, "left", join_function);
    auto* const right = llvm::BasicBlock::Create(
        context, "right", join_function);
    auto* const merge = llvm::BasicBlock::Create(
        context, "merge", join_function);
    auto join_builder = llvm::IRBuilder<> { join_entry };
    auto* const before_branch = join_builder.CreateLoad(
        i64, join_input, "before.branch");
    mark_load(before_branch);
    join_builder.CreateCondBr(join_condition, left, right);
    join_builder.SetInsertPoint(left);
    join_builder.CreateBr(merge);
    join_builder.SetInsertPoint(right);
    join_builder.CreateBr(merge);
    join_builder.SetInsertPoint(merge);
    auto* const after_join = join_builder.CreateLoad(
        i64, join_input, "after.join");
    mark_load(after_join);
    join_builder.CreateRet(after_join);
    const auto join_statistics = run_tiered_direct_read_dedup(
        join_module, LlvmBackendTier::less, true);
    assert(join_statistics.marked_loads == 2U);
    assert(join_statistics.eliminated_loads == 0U);
    assert(after_join->getParent() != nullptr);
    assert(fsim::compiler::llvm_detail::verify_error(join_module).empty());

    const auto oversized_tier = select_backend_tier(
        true, kLessBackendTierInstructionLimit + 1U);
    assert(oversized_tier == LlvmBackendTier::none);
    const auto skipped_statistics = run_tiered_direct_read_dedup(
        barrier_module, oversized_tier, true);
    assert(skipped_statistics.marked_loads == 0U);
    assert(skipped_statistics.eliminated_loads == 0U);
    assert(skipped_statistics.marked_value_loads == 0U);
    assert(skipped_statistics.eliminated_value_loads == 0U);
    std::size_t after_oversized_marked_loads { };
    for (const auto& block : *barrier_function) {
        for (const auto& instruction : block) {
            const auto* const load
                = llvm::dyn_cast<llvm::LoadInst>(&instruction);
            after_oversized_marked_loads += load != nullptr
                && load->getMetadata(barrier_tag) != nullptr;
        }
    }
    assert(after_oversized_marked_loads == 2U);
}

void test_per_entry_required_reads_allow_equal_const_planes()
{
    using namespace fsim::runtime::simir;
    Process process;
    process.id = 1001U;
    process.name = "per_entry_equal_read_planes";
    process.register_count = 1U;
    process.operations = { ReadSignal { 0U, 0U }, Halt { } };
    const std::array<std::uint32_t, 1> widths { 8U };
    const std::array<ValueKind, 1> kinds { ValueKind::logic4 };

    fsim::compiler::LlvmJit jit { };
    jit.add_process(process.name, process, widths, kinds,
        JitBackendTierHint::none, 1U, true, false);
    const auto handle = jit.lookup(process.name);
    const auto layout = jit.frame_layout(handle);
    assert((layout.direct_read_signals == std::vector<SignalId> { 0U }));

    std::vector<std::uint64_t> register_aval(layout.register_word_count);
    std::vector<std::uint64_t> register_bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, register_aval, register_bval,
        initialized);
    TestRuntime callbacks;
    auto runtime = abi(callbacks);
    std::array<std::uint64_t, 1> shared_read_plane { 0U };
    std::array<std::uint32_t, 1> read_map { 0U };
    runtime.direct_signal_aval = shared_read_plane.data();
    runtime.direct_signal_bval = shared_read_plane.data();
    runtime.direct_signal_count = 1U;
    runtime.direct_read_signals = read_map.data();
    runtime.direct_read_signal_count = 1U;
    auto result = new_resume_result();
    assert(jit.resume(handle, runtime, frame, result)
        == fsim::compiler::JitResumeStatus::completed);
    assert(register_aval[0U] == 0U);
    assert(register_bval[0U] == 0U);
}

void test_fast_isel_census_context_scoping()
{
    auto first_context = llvm::LLVMContext { };
    auto first_module = llvm::Module { "first-census", first_context };
    const auto first_call = add_function(
        first_context, first_module, "first_call");
    const auto first_instruction = add_function(
        first_context, first_module, "first_instruction");
    std::size_t first_forwarded { };
    auto first_prior = std::make_unique<ForwardingHandler>(
        first_forwarded, false);
    const auto* const first_prior_address = first_prior.get();
    first_context.setDiagnosticHandler(std::move(first_prior), false);

    std::string first_output_text;
    FastIselCensusSummary first_summary;
    {
        llvm::raw_string_ostream output { first_output_text };
        FastIselCensus census { first_module, true };
        assert(first_context.getDiagHandlerPtr() != first_prior_address);
        assert(first_context.getDiagHandlerPtr()
            ->isMissedOptRemarkEnabled("sdagisel"));
        emit_fallback(
            first_context, *first_call.block, "FastISel missed call");
        emit_fallback(
            first_context, *first_instruction.block,
            "FastISel didn't lower all arguments");
        auto unrelated = SyntheticMissedRemark(
            "other-pass", "OtherRemark", llvm::DiagnosticLocation { },
            first_call.block, "unrelated diagnostic");
        first_context.diagnose(unrelated);
        first_summary = census.summary();
        census.report(true, output);
        output.flush();
    }
    assert(first_context.getDiagHandlerPtr() == first_prior_address);
    assert(first_forwarded == 1U);
    assert(first_summary.enabled);
    assert(first_summary.fallback_functions == 2U);
    assert(first_summary.call_fallbacks == 1U);
    assert(first_summary.argument_fallbacks == 1U);
    assert(first_summary.terminator_fallbacks == 0U);
    assert(first_summary.instruction_fallbacks == 0U);
    assert(first_output_text.find("fallback_functions=2")
        != std::string::npos);
    assert(first_output_text.find("arguments=1 call=1")
        != std::string::npos);

    auto second_context = llvm::LLVMContext { };
    auto second_module = llvm::Module { "second-census", second_context };
    const auto second_terminator = add_function(
        second_context, second_module, "second_terminator");
    std::size_t second_forwarded { };
    auto second_prior = std::make_unique<ForwardingHandler>(
        second_forwarded, true);
    const auto* const second_prior_address = second_prior.get();
    second_context.setDiagnosticHandler(std::move(second_prior), false);
    std::string second_output_text;
    const auto second_summary = collect_one(
        second_context, second_module, *second_terminator.block,
        "FastISel missed terminator", second_output_text);
    assert(second_context.getDiagHandlerPtr() == second_prior_address);
    assert(second_forwarded == 1U);
    assert(second_summary.enabled);
    assert(second_summary.fallback_functions == 1U);
    assert(second_summary.call_fallbacks == 0U);
    assert(second_summary.terminator_fallbacks == 1U);
    assert(second_output_text.find("fallback_functions=1")
        != std::string::npos);
    assert(second_output_text.find("terminator=1") != std::string::npos);

    std::size_t disabled_forwarded { };
    auto disabled_prior = std::make_unique<ForwardingHandler>(
        disabled_forwarded, false);
    const auto* const disabled_prior_address = disabled_prior.get();
    second_context.setDiagnosticHandler(std::move(disabled_prior), false);
    std::string disabled_output_text;
    {
        llvm::raw_string_ostream output { disabled_output_text };
        FastIselCensus census { second_module, false };
        assert(second_context.getDiagHandlerPtr() == disabled_prior_address);
        emit_fallback(
            second_context, *second_terminator.block,
            "FastISel missed instruction");
        assert(!census.summary().enabled);
        census.report(true, output);
        output.flush();
    }
    assert(second_context.getDiagHandlerPtr() == disabled_prior_address);
    assert(disabled_forwarded == 1U);
    assert(disabled_output_text.empty());
}

void run_fast_isel_census_native_fallback()
{
    using namespace fsim::runtime::simir;
    constexpr std::uint32_t width = 129U;
    Process process;
    process.id = 700U;
    process.name = "fast_isel_wide_arithmetic";
    process.register_count = 3U;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    process.operations = {
        ReadSignal { 0U, 0U },
        ReadSignal { 1U, 1U },
        Binary { BinaryOperator::add_unsigned, 2U, 0U, 1U },
        WriteBlocking { 2U, 2U },
        Halt { },
    };
    const std::array<std::uint32_t, 3> signal_widths {
        width, width, width
    };
    const std::array<ValueKind, 3> signal_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    fsim::compiler::LlvmJit jit {
        fsim::compiler::LlvmJitOptions {
            fsim::compiler::JitOptimizationLevel::o0, { } }
    };
    jit.add_process(
        "fsim_process_fast_isel_wide_arithmetic",
        process, signal_widths, signal_kinds);

    TestRuntime runtime;
    runtime.wide_signal_aval[0U] = { UINT64_MAX, UINT64_MAX, 1U };
    runtime.wide_signal_bval[0U] = { 0U, 0U, 0U };
    runtime.wide_signal_aval[1U] = { 1U, 1U, 0U };
    runtime.wide_signal_bval[1U] = { 0U, 0U, 0U };
    auto descriptor = abi(runtime);
    assert(jit.execute(
               jit.lookup("fsim_process_fast_isel_wide_arithmetic"),
               descriptor)
        == fsim::compiler::JitExecutionStatus::completed);
    assert(runtime.packed_signal_reads == 2U);
    assert(runtime.wide_signal_aval[2U].size() == 3U);
}

} // namespace

void test_backend_tier_and_fast_isel_census()
{
    test_tier_selection_and_cache_identity();
    test_tiered_direct_read_dedup_barriers();
    test_per_entry_required_reads_allow_equal_const_planes();
    test_fast_isel_census_context_scoping();
}

void test_fast_isel_census_native_fallback()
{
    run_fast_isel_census_native_fallback();
}

} // namespace fsim::tests::compiler

// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"
#include "llvm_jit_lowering_internal.hpp"
#include "llvm_jit_lowering_context.hpp"
#include "llvm_jit_coverage.hpp"
#include "fsim/runtime/simir_region_activation.hpp"
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
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
namespace fsim::compiler::llvm_detail {
using runtime::Logic9;
using namespace runtime::simir;

static_assert(
    static_cast<std::uint32_t>(SignalUpdateDomain::generic)
    == FSIM_JIT_SIGNAL_UPDATE_DOMAIN_GENERIC_V2);
static_assert(
    static_cast<std::uint32_t>(SignalUpdateDomain::systemverilog_active)
    == FSIM_JIT_SIGNAL_UPDATE_DOMAIN_SYSTEMVERILOG_ACTIVE_V2);
static_assert(
    static_cast<std::uint32_t>(SignalUpdateDomain::systemverilog_nba)
    == FSIM_JIT_SIGNAL_UPDATE_DOMAIN_SYSTEMVERILOG_NBA_V2);

namespace {

void sink_immutable_service_loads(llvm::Function& function)
{
    llvm::DominatorTree dominators(function);
    std::vector<llvm::LoadInst*> loads;
    for (auto& instruction : function.getEntryBlock()) {
        auto* const load = llvm::dyn_cast<llvm::LoadInst>(&instruction);
        if (load != nullptr && !load->isVolatile() && !load->isAtomic()
            && load->getMetadata(llvm::LLVMContext::MD_invariant_load)
            && !load->use_empty()) {
            loads.push_back(load);
        }
    }
    for (auto* load : loads) {
        llvm::BasicBlock* common = nullptr;
        bool supported = true;
        const auto include_block = [&](llvm::BasicBlock* block) {
            if (!dominators.isReachableFromEntry(block)) {
                supported = false;
                return;
            }
            common = common == nullptr ? block
                : dominators.findNearestCommonDominator(common, block);
        };
        for (auto* user : load->users()) {
            auto* const instruction = llvm::dyn_cast<llvm::Instruction>(user);
            if (instruction == nullptr) {
                supported = false;
                break;
            }
            if (auto* phi = llvm::dyn_cast<llvm::PHINode>(instruction)) {
                for (unsigned index = 0U; index < phi->getNumIncomingValues();
                     ++index) {
                    if (phi->getIncomingValue(index) == load) {
                        include_block(phi->getIncomingBlock(index));
                    }
                }
            } else {
                include_block(instruction->getParent());
            }
        }
        if (!supported || common == nullptr
            || common == &function.getEntryBlock()) {
            continue;
        }
        // ABI v2 service entries are immutable after publication. Their
        // address is computed in the entry block; sink only that immutable
        // leaf load, never the host-rewritten instance fields or context.
        load->moveBefore(common->getFirstInsertionPt());
    }
}

struct RegionPreparedOutputAbiTypes {
    llvm::StructType* slot { };
    llvm::StructType* batch { };
};

[[nodiscard]] RegionPreparedOutputAbiTypes
create_region_prepared_output_abi_types(llvm::LLVMContext& context)
{
    static_assert(std::is_standard_layout_v<
        runtime::simir::RegionPreparedOutputSlotV1>);
    static_assert(std::is_trivially_copyable_v<
        runtime::simir::RegionPreparedOutputSlotV1>);
    static_assert(std::is_standard_layout_v<
        runtime::simir::RegionPreparedOutputBatchV1>);
    static_assert(std::is_trivially_copyable_v<
        runtime::simir::RegionPreparedOutputBatchV1>);

    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const slot = llvm::StructType::create(
        context, "fsim.region.prepared_output_slot.v1");
    slot->setBody({ i32, i32, i32, i32, i32, i32, i32, i32,
        pointer, pointer, pointer, pointer, pointer,
        pointer, pointer, pointer, pointer, pointer, pointer, pointer,
        pointer, pointer, pointer, pointer }, false);
    auto* const batch = llvm::StructType::create(
        context, "fsim.region.prepared_output_batch.v1");
    batch->setBody({ i32, i32, i32, i32, pointer }, false);
    return { slot, batch };
}

void validate_region_prepared_output_abi_layout(
    const llvm::DataLayout& data_layout,
    const RegionPreparedOutputAbiTypes& types)
{
    using Slot = runtime::simir::RegionPreparedOutputSlotV1;
    using Batch = runtime::simir::RegionPreparedOutputBatchV1;
    constexpr std::array slot_offsets {
        offsetof(Slot, struct_size),
        offsetof(Slot, signal_id),
        offsetof(Slot, owner_id),
        offsetof(Slot, width),
        offsetof(Slot, word_count),
        offsetof(Slot, value_kind),
        offsetof(Slot, selected),
        offsetof(Slot, reserved),
        offsetof(Slot, owner_mask),
        offsetof(Slot, old_current_aval),
        offsetof(Slot, old_current_bval),
        offsetof(Slot, old_owner_aval),
        offsetof(Slot, old_owner_bval),
        offsetof(Slot, next_current_aval),
        offsetof(Slot, next_current_bval),
        offsetof(Slot, next_last_aval),
        offsetof(Slot, next_last_bval),
        offsetof(Slot, next_stored_aval),
        offsetof(Slot, next_stored_bval),
        offsetof(Slot, next_owner_aval),
        offsetof(Slot, next_owner_bval),
        offsetof(Slot, changed),
        offsetof(Slot, value_ready),
        offsetof(Slot, transaction_ready),
    };
    constexpr std::array batch_offsets {
        offsetof(Batch, abi_version),
        offsetof(Batch, struct_size),
        offsetof(Batch, slot_count),
        offsetof(Batch, reserved),
        offsetof(Batch, slots),
    };
    const auto* const slot_layout = data_layout.getStructLayout(types.slot);
    const auto* const batch_layout = data_layout.getStructLayout(types.batch);
    for (unsigned index = 0U; index < slot_offsets.size(); ++index) {
        if (slot_layout->getElementOffset(index) != slot_offsets[index]) {
            throw LlvmJitError(
                "prepared-output slot target layout is incompatible");
        }
    }
    for (unsigned index = 0U; index < batch_offsets.size(); ++index) {
        if (batch_layout->getElementOffset(index) != batch_offsets[index]) {
            throw LlvmJitError(
                "prepared-output batch target layout is incompatible");
        }
    }
    if (slot_layout->getSizeInBytes() != sizeof(Slot)
        || batch_layout->getSizeInBytes() != sizeof(Batch)
        || data_layout.getABITypeAlign(types.slot).value() != alignof(Slot)
        || data_layout.getABITypeAlign(types.batch).value() != alignof(Batch)
        || data_layout.getPointerSize() != sizeof(void*)) {
        throw LlvmJitError(
            "prepared-output descriptor size does not match the JIT target");
    }
}

void emit_region_prepared_output_entry(
    llvm::Module& module,
    llvm::Function& body,
    const std::string_view symbol,
    const std::span<const RegionPreparedOutputLoweringBinding> bindings)
{
    if (symbol.empty() || bindings.empty()) {
        throw LlvmJitError("prepared-output entry requires a complete binding");
    }
    auto& context = module.getContext();
    const auto types = create_region_prepared_output_abi_types(
        module.getContext());
    validate_region_prepared_output_abi_layout(module.getDataLayout(), types);
    const auto abi = create_jit_abi_v2_types(module.getContext());
    validate_jit_abi_v2_layout(module.getDataLayout(), abi);
    auto* const i8 = llvm::Type::getInt8Ty(context);
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const function_type = llvm::FunctionType::get(
        i32, { pointer, pointer, pointer, pointer }, false);
    auto* const function = llvm::Function::Create(
        function_type, llvm::Function::ExternalLinkage,
        std::string { symbol }, module);
    function->setCallingConv(llvm::CallingConv::C);
    function->getArg(0)->setName("runtime.instance");
    function->getArg(1)->setName("frame");
    function->getArg(2)->setName("result");
    function->getArg(3)->setName("prepared.outputs.v1");

    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    auto* const completed
        = llvm::BasicBlock::Create(context, "body.completed", function);
    auto* const declined
        = llvm::BasicBlock::Create(context, "body.declined", function);
    llvm::IRBuilder<> builder(entry);
    auto* const status = builder.CreateCall(&body,
        { function->getArg(0), function->getArg(1), function->getArg(2) },
        "activation.status");
    status->setCallingConv(llvm::CallingConv::C);
    builder.CreateCondBr(
        builder.CreateICmpEQ(status,
            llvm::ConstantInt::get(i32,
                FSIM_JIT_RESUME_STATUS_COMPLETED_V2)),
        completed, declined);
    llvm::IRBuilder<> declined_builder(declined);
    declined_builder.CreateRet(status);

    builder.SetInsertPoint(completed);
    auto* const runtime_type = abi.runtime_instance;
    auto* const update_slots = builder.CreateLoad(pointer,
        runtime_instance_field_address(builder,
            runtime_type,
            function->getArg(0), JitRuntimeInstanceField::direct_update_slots),
        "direct.update.slots");
    auto* const batch = function->getArg(3);
    auto* const batch_slots_address = builder.CreateStructGEP(
        types.batch, batch, 4U, "prepared.outputs.slots.address");
    auto* const batch_slots = builder.CreateLoad(
        pointer, batch_slots_address, "prepared.outputs.slots");
    auto* const word_pointer = llvm::PointerType::getUnqual(context);
    auto* const byte_pointer = llvm::PointerType::getUnqual(context);
    const auto slot_field_address = [&](llvm::Value* const slot,
                                        const unsigned field) {
        return builder.CreateStructGEP(types.slot, slot, field);
    };
    const auto load_word_pointer = [&](llvm::Value* const slot,
                                       const unsigned field,
                                       const llvm::Twine& name) {
        return builder.CreateBitCast(
            builder.CreateLoad(pointer, slot_field_address(slot, field)),
            word_pointer, name);
    };
    const auto load_byte_pointer = [&](llvm::Value* const slot,
                                       const unsigned field,
                                       const llvm::Twine& name) {
        return builder.CreateBitCast(
            builder.CreateLoad(pointer, slot_field_address(slot, field)),
            byte_pointer, name);
    };
    for (std::size_t index = 0U; index < bindings.size(); ++index) {
        const auto& binding = bindings[index];
        if (binding.width == 0U || binding.width > 64U
            || (index != 0U
                && bindings[index - 1U].descriptor_slot
                    >= binding.descriptor_slot)) {
            throw LlvmJitError(
                "prepared-output binding order is invalid");
        }
        auto* const descriptor = builder.CreateGEP(types.slot, batch_slots,
            llvm::ConstantInt::get(i32, binding.descriptor_slot),
            "prepared.output.slot");
        auto* const selected = builder.CreateLoad(i32,
            slot_field_address(descriptor, 6U), "prepared.output.selected");
        auto* const publish = llvm::BasicBlock::Create(
            context, "prepared.output.publish", function);
        auto* const next = llvm::BasicBlock::Create(
            context, "prepared.output.next", function);
        builder.CreateCondBr(
            builder.CreateICmpNE(selected, llvm::ConstantInt::get(i32, 0U)),
            publish, next);
        builder.SetInsertPoint(publish);

        auto* const slot = builder.CreateGEP(abi.update_slot, update_slots,
            llvm::ConstantInt::get(i32, binding.direct_update_slot),
            "direct.update.slot");
        auto* const source_aval = builder.CreateLoad(i64,
            builder.CreateStructGEP(abi.update_slot, slot, 0U),
            "prepared.source.aval");
        auto* const source_bval = builder.CreateLoad(i64,
            builder.CreateStructGEP(abi.update_slot, slot, 1U),
            "prepared.source.bval");
        const auto valid_mask = binding.width == 64U
            ? UINT64_MAX : (UINT64_C(1) << binding.width) - 1U;
        auto* const owner_mask_pointer
            = load_word_pointer(descriptor, 8U, "prepared.owner.mask.ptr");
        auto* const owner_mask = builder.CreateAnd(
            builder.CreateLoad(i64, owner_mask_pointer),
            llvm::ConstantInt::get(i64, valid_mask), "prepared.owner.mask");
        auto* const inverse_mask = builder.CreateNot(owner_mask);

        auto* const old_current_aval_pointer
            = load_word_pointer(descriptor, 9U, "prepared.old.current.aval.ptr");
        auto* const old_current_bval_pointer
            = load_word_pointer(descriptor, 10U, "prepared.old.current.bval.ptr");
        auto* const old_owner_aval_pointer
            = load_word_pointer(descriptor, 11U, "prepared.old.owner.aval.ptr");
        auto* const old_owner_bval_pointer
            = load_word_pointer(descriptor, 12U, "prepared.old.owner.bval.ptr");
        auto* const old_current_aval
            = builder.CreateLoad(i64, old_current_aval_pointer);
        auto* const old_current_bval
            = builder.CreateLoad(i64, old_current_bval_pointer);
        auto* const old_owner_aval
            = builder.CreateLoad(i64, old_owner_aval_pointer);
        auto* const old_owner_bval
            = builder.CreateLoad(i64, old_owner_bval_pointer);
        auto* const next_owner_aval = builder.CreateOr(
            builder.CreateAnd(old_owner_aval, inverse_mask),
            builder.CreateAnd(source_aval, owner_mask));
        auto* const next_owner_bval = builder.CreateOr(
            builder.CreateAnd(old_owner_bval, inverse_mask),
            builder.CreateAnd(source_bval, owner_mask));
        auto* const next_current_aval = builder.CreateOr(
            builder.CreateAnd(old_current_aval, inverse_mask),
            builder.CreateAnd(source_aval, owner_mask));
        auto* const next_current_bval = builder.CreateOr(
            builder.CreateAnd(old_current_bval, inverse_mask),
            builder.CreateAnd(source_bval, owner_mask));
        auto* const changed = builder.CreateOr(
            builder.CreateICmpNE(next_current_aval, old_current_aval),
            builder.CreateICmpNE(next_current_bval, old_current_bval),
            "prepared.value.changed");

        constexpr std::array<unsigned, 8U> next_fields {
            13U, 14U, 17U, 18U, 19U, 20U, 15U, 16U
        };
        const std::array<llvm::Value*, 8U> next_words {
            next_current_aval, next_current_bval,
            next_owner_aval, next_owner_bval,
            next_owner_aval, next_owner_bval,
            old_current_aval, old_current_bval
        };
        for (std::size_t output = 0U; output < next_fields.size(); ++output) {
            const auto field = next_fields[output];
            auto* const destination = load_word_pointer(
                descriptor, field, "prepared.destination.word.ptr");
            llvm::Value* value = next_words[output];
            if (field == 15U || field == 16U) {
                const auto old_last = builder.CreateLoad(i64, destination);
                value = builder.CreateSelect(changed, value, old_last);
            }
            builder.CreateStore(value, destination);
        }
        const auto flag = builder.CreateZExt(changed, i8);
        const std::array<unsigned, 3U> byte_fields { 21U, 22U, 23U };
        for (std::size_t byte = 0U; byte < byte_fields.size(); ++byte) {
            auto* const destination = load_byte_pointer(
                descriptor, byte_fields[byte], "prepared.ready.byte.ptr");
            const auto value = byte == 2U
                ? llvm::ConstantInt::get(i8, 1U)
                : static_cast<llvm::Value*>(flag);
            builder.CreateStore(value, destination);
        }
        builder.CreateBr(next);
        builder.SetInsertPoint(next);
    }
    builder.CreateRet(status);
}

void emit_region_prepared_output_successor_entry(
    llvm::Module& module,
    const std::string_view prepared_output_symbol,
    const std::string_view successor_symbol,
    const std::span<const RegionPreparedOutputLoweringBinding> bindings)
{
    if (prepared_output_symbol.empty() || successor_symbol.empty()
        || bindings.empty()) {
        throw LlvmJitError("prepared-output successor needs complete bindings");
    }
    auto* const prepared_entry
        = module.getFunction(std::string { prepared_output_symbol });
    if (prepared_entry == nullptr || prepared_entry->arg_size() != 4U) {
        throw LlvmJitError("prepared-output successor has no V1 target");
    }

    auto& context = module.getContext();
    const auto output_types = create_region_prepared_output_abi_types(context);
    validate_region_prepared_output_abi_layout(module.getDataLayout(), output_types);
    auto* const i8 = llvm::Type::getInt8Ty(context);
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const sidecar_type = llvm::StructType::create(
        context, "fsim.region.prepared_output_successor_masks.v1");
    sidecar_type->setBody({ i32, i32, i32, i32, pointer }, false);
    if (module.getDataLayout().getABITypeAlign(sidecar_type).value()
        != alignof(runtime::simir::
            RegionPreparedOutputSuccessorMasksV1)) {
        throw LlvmJitError("successor-mask sidecar alignment is incompatible");
    }
    const auto* const sidecar_layout
        = module.getDataLayout().getStructLayout(sidecar_type);
    constexpr std::array sidecar_offsets {
        offsetof(runtime::simir::RegionPreparedOutputSuccessorMasksV1,
            abi_version),
        offsetof(runtime::simir::RegionPreparedOutputSuccessorMasksV1,
            struct_size),
        offsetof(runtime::simir::RegionPreparedOutputSuccessorMasksV1,
            slot_count),
        offsetof(runtime::simir::RegionPreparedOutputSuccessorMasksV1,
            reserved),
        offsetof(runtime::simir::RegionPreparedOutputSuccessorMasksV1,
            member_masks),
    };
    for (unsigned index = 0U; index < sidecar_offsets.size(); ++index) {
        if (sidecar_layout->getElementOffset(index) != sidecar_offsets[index]) {
            throw LlvmJitError("successor-mask sidecar layout is incompatible");
        }
    }
    if (sidecar_layout->getSizeInBytes()
        != sizeof(runtime::simir::RegionPreparedOutputSuccessorMasksV1)
        || module.getDataLayout().getPointerSize() != sizeof(void*)) {
        throw LlvmJitError("successor-mask sidecar size is incompatible");
    }
    auto* const function_type = llvm::FunctionType::get(
        i32, { pointer, pointer, pointer, pointer, pointer }, false);
    auto* const function = llvm::Function::Create(
        function_type, llvm::Function::ExternalLinkage,
        std::string { successor_symbol }, module);
    function->setCallingConv(llvm::CallingConv::C);
    function->getArg(0)->setName("runtime.instance");
    function->getArg(1)->setName("frame");
    function->getArg(2)->setName("result");
    function->getArg(3)->setName("prepared.outputs.v1");
    function->getArg(4)->setName("successor.masks.v1");

    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    auto* const inspect = llvm::BasicBlock::Create(
        context, "descriptor.inspect", function);
    auto* const invoke = llvm::BasicBlock::Create(
        context, "prepared.invoke", function);
    auto* const invalid = llvm::BasicBlock::Create(
        context, "descriptor.invalid", function);
    auto* const declined = llvm::BasicBlock::Create(
        context, "prepared.declined", function);
    auto* const finish = llvm::BasicBlock::Create(
        context, "successor.finish", function);
    llvm::IRBuilder<> builder(entry);
    const auto pointer_bits = module.getDataLayout().getPointerSizeInBits();
    if (pointer_bits == 0U || pointer_bits > 64U) {
        throw LlvmJitError("successor-mask pointer width is unsupported");
    }
    auto* const sidecar_address = builder.CreatePtrToInt(
        function->getArg(4), i64);
    auto* const batch_address = builder.CreatePtrToInt(
        function->getArg(3), i64);
    const auto is_aligned = [&](llvm::Value* const address,
                                const std::size_t alignment) {
        return builder.CreateICmpEQ(
            builder.CreateAnd(address,
                llvm::ConstantInt::get(i64, alignment - 1U)),
            llvm::ConstantInt::get(i64, 0U));
    };
    const auto nonnull = [&](llvm::Value* const pointer_value) {
        return builder.CreateICmpNE(pointer_value,
            llvm::ConstantPointerNull::get(pointer));
    };
    auto* const pointers_present = builder.CreateAnd(
        nonnull(function->getArg(3)), nonnull(function->getArg(4)));
    auto* const descriptor_pointers_aligned = builder.CreateAnd(
        is_aligned(sidecar_address,
            alignof(runtime::simir::RegionPreparedOutputSuccessorMasksV1)),
        is_aligned(batch_address,
            alignof(runtime::simir::RegionPreparedOutputBatchV1)));
    builder.CreateCondBr(
        builder.CreateAnd(pointers_present, descriptor_pointers_aligned),
        inspect, invalid);

    builder.SetInsertPoint(inspect);
    auto* const sidecar = function->getArg(4);
    const auto load_sidecar_field = [&](const unsigned field) {
        return builder.CreateLoad(i32,
            builder.CreateStructGEP(sidecar_type, sidecar, field));
    };
    auto* const abi_version = load_sidecar_field(0U);
    auto* const struct_size = load_sidecar_field(1U);
    auto* const slot_count = load_sidecar_field(2U);
    auto* const reserved = load_sidecar_field(3U);
    auto* const masks = builder.CreateLoad(pointer,
        builder.CreateStructGEP(sidecar_type, sidecar, 4U));
    const auto masks_address = builder.CreatePtrToInt(masks, i64);
    const auto misaligned = builder.CreateICmpNE(
        builder.CreateAnd(masks_address, llvm::ConstantInt::get(i64, 7U)),
        llvm::ConstantInt::get(i64, 0U));
    auto* const batch = function->getArg(3);
    const auto load_batch_field = [&](const unsigned field) {
        return builder.CreateLoad(i32,
            builder.CreateStructGEP(output_types.batch, batch, field));
    };
    auto* const batch_version = load_batch_field(0U);
    auto* const batch_size = load_batch_field(1U);
    auto* const batch_count = builder.CreateLoad(i32,
        builder.CreateStructGEP(output_types.batch, batch, 2U));
    auto* const batch_reserved = load_batch_field(3U);
    auto* const batch_slots = builder.CreateLoad(pointer,
        builder.CreateStructGEP(output_types.batch, batch, 4U));
    const auto pointer_max = pointer_bits == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : ((UINT64_C(1) << pointer_bits) - 1U);
    const bool mask_range_fits
        = bindings.size()
            <= pointer_max / sizeof(std::uint64_t);
    const auto masks_bytes = mask_range_fits
        ? bindings.size() * sizeof(std::uint64_t) : 0U;
    const auto mask_end_fits = builder.CreateICmpULE(masks_address,
        llvm::ConstantInt::get(i64,
            mask_range_fits ? pointer_max - masks_bytes : 0U));
    auto* const batch_slots_address = builder.CreatePtrToInt(batch_slots, i64);
    const bool slot_range_fits
        = bindings.size()
            <= pointer_max
                / sizeof(runtime::simir::RegionPreparedOutputSlotV1);
    const auto slot_bytes = slot_range_fits
        ? bindings.size()
            * sizeof(runtime::simir::RegionPreparedOutputSlotV1) : 0U;
    const auto slots_end_fits = builder.CreateICmpULE(batch_slots_address,
        llvm::ConstantInt::get(i64,
            slot_range_fits ? pointer_max - slot_bytes : 0U));
    if (!mask_range_fits || !slot_range_fits) {
        throw LlvmJitUnsupportedError(
            "prepared-output successor descriptor exceeds pointer range");
    }
    const auto equal_i32 = [&](llvm::Value* const value,
                               const std::uint64_t expected) {
        return builder.CreateICmpEQ(value,
            llvm::ConstantInt::get(i32, expected));
    };
    const std::array checks {
        equal_i32(abi_version,
            runtime::simir::
                kRegionPreparedOutputSuccessorMasksAbiVersionV1),
        equal_i32(struct_size,
            sizeof(runtime::simir::RegionPreparedOutputSuccessorMasksV1)),
        equal_i32(slot_count, bindings.size()),
        equal_i32(batch_version,
            runtime::simir::kRegionPreparedOutputBatchAbiVersionV1),
        equal_i32(batch_size,
            sizeof(runtime::simir::RegionPreparedOutputBatchV1)),
        equal_i32(batch_count, bindings.size()),
        equal_i32(reserved, 0U),
        equal_i32(batch_reserved, 0U),
        nonnull(batch_slots),
        nonnull(masks),
        builder.CreateNot(misaligned),
        mask_end_fits,
        slots_end_fits,
    };
    auto* valid = checks.front();
    for (std::size_t index = 1U; index < checks.size(); ++index) {
        valid = builder.CreateAnd(valid, checks[index]);
    }
    builder.CreateCondBr(valid, invoke, invalid);
    llvm::IRBuilder<> invalid_builder(invalid);
    invalid_builder.CreateRet(llvm::ConstantInt::get(i32,
        FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2));

    builder.SetInsertPoint(invoke);
    auto* const status = builder.CreateCall(prepared_entry,
        { function->getArg(0), function->getArg(1), function->getArg(2), batch },
        "prepared.output.status");
    status->setCallingConv(llvm::CallingConv::C);
    builder.CreateCondBr(
        builder.CreateICmpEQ(status, llvm::ConstantInt::get(i32,
            FSIM_JIT_RESUME_STATUS_COMPLETED_V2)), finish, declined);
    llvm::IRBuilder<> declined_builder(declined);
    // Preserve the V1 status exactly; sidecar stores happen only on complete.
    declined_builder.CreateRet(status);

    builder.SetInsertPoint(finish);
    auto* const slots = builder.CreateLoad(pointer,
        builder.CreateStructGEP(output_types.batch, batch, 4U));
    for (const auto& binding : bindings) {
        auto* const descriptor = builder.CreateGEP(output_types.slot, slots,
            llvm::ConstantInt::get(i32, binding.descriptor_slot));
        auto* const selected = builder.CreateLoad(i32,
            builder.CreateStructGEP(output_types.slot, descriptor, 6U));
        auto* const selected_block = llvm::BasicBlock::Create(
            context, "successor.selected", function);
        auto* const unselected_block = llvm::BasicBlock::Create(
            context, "successor.unselected", function);
        auto* const next = llvm::BasicBlock::Create(
            context, "successor.next", function);
        auto* const mask_pointer = builder.CreateGEP(i64, masks,
            llvm::ConstantInt::get(i32, binding.descriptor_slot));
        builder.CreateCondBr(builder.CreateICmpNE(selected,
            llvm::ConstantInt::get(i32, 0U)), selected_block,
            unselected_block);
        builder.SetInsertPoint(selected_block);
        auto* const changed_pointer = builder.CreateLoad(pointer,
            builder.CreateStructGEP(output_types.slot, descriptor, 21U));
        auto* const changed = builder.CreateLoad(i8, changed_pointer);
        builder.CreateStore(builder.CreateSelect(
            builder.CreateICmpNE(changed, llvm::ConstantInt::get(i8, 0U)),
            llvm::ConstantInt::get(i64, binding.successor_member_mask),
            llvm::ConstantInt::get(i64, 0U)), mask_pointer);
        builder.CreateBr(next);
        builder.SetInsertPoint(unselected_block);
        builder.CreateStore(llvm::ConstantInt::get(i64, 0U), mask_pointer);
        builder.CreateBr(next);
        builder.SetInsertPoint(next);
    }
    builder.CreateRet(status);
}

struct RegionDirectReadyAbiTypes {
    llvm::StructType* input_slot { };
    llvm::StructType* window { };
};

[[nodiscard]] RegionDirectReadyAbiTypes
create_region_direct_ready_abi_types(llvm::LLVMContext& context)
{
    using Slot = runtime::simir::RegionDirectReadyInputSlotV1;
    using Window = runtime::simir::RegionDirectReadyWindowV1;
    static_assert(std::is_standard_layout_v<Slot>);
    static_assert(std::is_trivially_copyable_v<Slot>);
    static_assert(std::is_standard_layout_v<Window>);
    static_assert(std::is_trivially_copyable_v<Window>);
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const slot = llvm::StructType::create(
        context, "fsim.region.direct_ready_input_slot.v1");
    slot->setBody({ i32, i32, i32, i32, i32, i32, pointer, pointer }, false);
    auto* const window = llvm::StructType::create(
        context, "fsim.region.direct_ready_window.v1");
    window->setBody(
        { i32, i32, i64, i64, i32, i32, i32, i32, pointer, pointer }, false);
    return { slot, window };
}

void validate_region_direct_ready_abi_layout(
    const llvm::DataLayout& data_layout,
    const RegionDirectReadyAbiTypes& types)
{
    using Slot = runtime::simir::RegionDirectReadyInputSlotV1;
    using Window = runtime::simir::RegionDirectReadyWindowV1;
    constexpr std::array slot_offsets {
        offsetof(Slot, struct_size), offsetof(Slot, signal_id),
        offsetof(Slot, register_id), offsetof(Slot, width),
        offsetof(Slot, word_count), offsetof(Slot, reserved),
        offsetof(Slot, aval), offsetof(Slot, bval),
    };
    constexpr std::array window_offsets {
        offsetof(Window, abi_version), offsetof(Window, struct_size),
        offsetof(Window, activation_generation),
        offsetof(Window, frontier_generation),
        offsetof(Window, member_count), offsetof(Window, readiness_word_count),
        offsetof(Window, input_slot_count), offsetof(Window, reserved),
        offsetof(Window, readiness_mask), offsetof(Window, input_slots),
    };
    const auto* const slot_layout
        = data_layout.getStructLayout(types.input_slot);
    const auto* const window_layout = data_layout.getStructLayout(types.window);
    for (unsigned index = 0U; index < slot_offsets.size(); ++index) {
        if (slot_layout->getElementOffset(index) != slot_offsets[index]) {
            throw LlvmJitError(
                "direct-ready input slot target layout is incompatible");
        }
    }
    for (unsigned index = 0U; index < window_offsets.size(); ++index) {
        if (window_layout->getElementOffset(index) != window_offsets[index]) {
            throw LlvmJitError(
                "direct-ready window target layout is incompatible");
        }
    }
    if (slot_layout->getSizeInBytes() != sizeof(Slot)
        || window_layout->getSizeInBytes() != sizeof(Window)
        || data_layout.getPointerSize() != sizeof(void*)) {
        throw LlvmJitError(
            "direct-ready descriptor size does not match the JIT target");
    }
}

void emit_region_direct_ready_output_entry(
    llvm::Module& module,
    const std::string_view prepared_output_symbol,
    const std::string_view direct_ready_symbol,
    const std::span<const RegionDirectReadyLoweringBinding> bindings)
{
    if (prepared_output_symbol.empty() || direct_ready_symbol.empty()
        || bindings.empty()) {
        throw LlvmJitError(
            "direct-ready entry requires complete private bindings");
    }
    auto* const prepared_entry
        = module.getFunction(std::string { prepared_output_symbol });
    if (prepared_entry == nullptr || prepared_entry->arg_size() != 4U) {
        throw LlvmJitError(
            "direct-ready entry has no prepared-output target");
    }

    auto& context = module.getContext();
    const auto direct_types = create_region_direct_ready_abi_types(context);
    validate_region_direct_ready_abi_layout(module.getDataLayout(), direct_types);
    const auto abi = create_jit_abi_v2_types(context);
    validate_jit_abi_v2_layout(module.getDataLayout(), abi);
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const function_type = llvm::FunctionType::get(
        i32, { pointer, pointer, pointer, pointer, pointer }, false);
    auto* const function = llvm::Function::Create(
        function_type, llvm::Function::ExternalLinkage,
        std::string { direct_ready_symbol }, module);
    function->setCallingConv(llvm::CallingConv::C);
    function->getArg(0)->setName("runtime.instance");
    function->getArg(1)->setName("frame");
    function->getArg(2)->setName("result");
    function->getArg(3)->setName("direct.ready.window.v1");
    function->getArg(4)->setName("prepared.outputs.v1");

    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    auto* const returned = llvm::BasicBlock::Create(context, "prepared.return", function);
    llvm::IRBuilder<> builder(entry);
    auto* const direct_window = function->getArg(3);
    auto* const window_slots_address = builder.CreateStructGEP(
        direct_types.window, direct_window, 9U, "direct.ready.inputs.address");
    auto* const input_slots = builder.CreateLoad(
        pointer, window_slots_address, "direct.ready.inputs");
    auto* const runtime_type = abi.runtime_instance;
    auto* const direct_signal_aval = builder.CreateLoad(pointer,
        runtime_instance_field_address(builder, runtime_type,
            function->getArg(0), JitRuntimeInstanceField::direct_signal_aval),
        "direct.signal.aval");
    auto* const direct_signal_bval = builder.CreateLoad(pointer,
        runtime_instance_field_address(builder, runtime_type,
            function->getArg(0), JitRuntimeInstanceField::direct_signal_bval),
        "direct.signal.bval");
    auto* const readiness_address = builder.CreateStructGEP(
        direct_types.window, direct_window, 8U,
        "direct.ready.mask.address");
    auto* const readiness_mask = builder.CreateLoad(
        pointer, readiness_address, "direct.ready.mask");

    for (const auto& binding : bindings) {
        if (binding.width == 0U || binding.width > 64U) {
            throw LlvmJitError("direct-ready binding shape is invalid");
        }
        llvm::Value* aval { };
        llvm::Value* bval { };
        if (binding.kind == RegionDirectReadyBindingKind::input_slot
            || binding.kind == RegionDirectReadyBindingKind::prefix_current) {
            auto* const slot = builder.CreateGEP(direct_types.input_slot,
                input_slots,
                llvm::ConstantInt::get(i32, binding.source_index),
                binding.kind == RegionDirectReadyBindingKind::input_slot
                    ? "direct.ready.input.slot"
                    : "direct.ready.prefix.input.slot");
            auto* const aval_pointer = builder.CreateLoad(pointer,
                builder.CreateStructGEP(direct_types.input_slot, slot, 6U),
                "direct.ready.input.aval.pointer");
            auto* const bval_pointer = builder.CreateLoad(pointer,
                builder.CreateStructGEP(direct_types.input_slot, slot, 7U),
                "direct.ready.input.bval.pointer");
            aval = builder.CreateLoad(i64,
                builder.CreateBitCast(aval_pointer,
                    llvm::PointerType::getUnqual(context)),
                "direct.ready.input.aval");
            bval = builder.CreateLoad(i64,
                builder.CreateBitCast(bval_pointer,
                    llvm::PointerType::getUnqual(context)),
                "direct.ready.input.bval");
        } else if (binding.kind == RegionDirectReadyBindingKind::member_ready) {
            const auto mask_word = binding.source_index / 64U;
            const auto mask_bit = binding.source_index % 64U;
            auto* const word = builder.CreateLoad(i64,
                builder.CreateGEP(i64,
                    builder.CreateBitCast(readiness_mask,
                        llvm::PointerType::getUnqual(context)),
                    llvm::ConstantInt::get(i32, mask_word)),
                "direct.ready.mask.word");
            aval = builder.CreateZExt(
                builder.CreateICmpNE(
                    builder.CreateAnd(word,
                        llvm::ConstantInt::get(i64,
                            UINT64_C(1) << mask_bit)),
                    llvm::ConstantInt::get(i64, 0U)), i64,
                "direct.ready.bit");
            bval = llvm::ConstantInt::get(i64, 0U);
        } else {
            throw LlvmJitError("direct-ready binding kind is invalid");
        }

        auto* const signal_aval_word = builder.CreateGEP(
            i64,
            builder.CreateBitCast(direct_signal_aval,
                llvm::PointerType::getUnqual(context)),
            llvm::ConstantInt::get(i32, binding.synthetic_signal_id));
        auto* const signal_bval_word = builder.CreateGEP(
            i64,
            builder.CreateBitCast(direct_signal_bval,
                llvm::PointerType::getUnqual(context)),
            llvm::ConstantInt::get(i32, binding.synthetic_signal_id));
        builder.CreateStore(aval, signal_aval_word);
        builder.CreateStore(bval, signal_bval_word);
    }

    auto* const status = builder.CreateCall(prepared_entry,
        { function->getArg(0), function->getArg(1), function->getArg(2),
            function->getArg(4) },
        "prepared.output.status");
    status->setCallingConv(llvm::CallingConv::C);
    builder.CreateBr(returned);
    builder.SetInsertPoint(returned);
    builder.CreateRet(status);
}

void emit_region_direct_ready_successor_entry(
    llvm::Module& module,
    const std::string_view direct_ready_symbol,
    const std::string_view prepared_successor_symbol,
    const std::string_view direct_ready_successor_symbol,
    const std::span<const RegionPreparedOutputLoweringBinding> bindings)
{
    if (direct_ready_symbol.empty() || prepared_successor_symbol.empty()
        || direct_ready_successor_symbol.empty() || bindings.empty()) {
        throw LlvmJitError(
            "direct-ready successor needs complete private bindings");
    }
    if (bindings.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitUnsupportedError(
            "direct-ready successor slot count exceeds the V1 ABI");
    }
    auto* const direct_ready_entry
        = module.getFunction(std::string { direct_ready_symbol });
    auto* const prepared_successor_entry
        = module.getFunction(std::string { prepared_successor_symbol });
    if (direct_ready_entry == nullptr
        || direct_ready_entry->arg_size() != 5U
        || prepared_successor_entry == nullptr
        || prepared_successor_entry->arg_size() != 5U) {
        throw LlvmJitError(
            "direct-ready successor has no compatible V1 targets");
    }

    auto& context = module.getContext();
    const auto direct_types = create_region_direct_ready_abi_types(context);
    const auto output_types = create_region_prepared_output_abi_types(context);
    validate_region_direct_ready_abi_layout(
        module.getDataLayout(), direct_types);
    validate_region_prepared_output_abi_layout(
        module.getDataLayout(), output_types);
    auto* const i8 = llvm::Type::getInt8Ty(context);
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const sidecar_type = llvm::StructType::create(
        context, "fsim.region.prepared_output_successor_masks.v1");
    sidecar_type->setBody({ i32, i32, i32, i32, pointer }, false);
    using Successor = runtime::simir::RegionPreparedOutputSuccessorMasksV1;
    if (module.getDataLayout().getABITypeAlign(sidecar_type).value()
        != alignof(Successor)) {
        throw LlvmJitError(
            "direct-ready successor sidecar alignment is incompatible");
    }
    const auto* const sidecar_layout
        = module.getDataLayout().getStructLayout(sidecar_type);
    constexpr std::array sidecar_offsets {
        offsetof(Successor, abi_version), offsetof(Successor, struct_size),
        offsetof(Successor, slot_count), offsetof(Successor, reserved),
        offsetof(Successor, member_masks),
    };
    for (unsigned index = 0U; index < sidecar_offsets.size(); ++index) {
        if (sidecar_layout->getElementOffset(index) != sidecar_offsets[index]) {
            throw LlvmJitError(
                "direct-ready successor sidecar layout is incompatible");
        }
    }
    if (sidecar_layout->getSizeInBytes() != sizeof(Successor)
        || module.getDataLayout().getPointerSize() != sizeof(void*)) {
        throw LlvmJitError(
            "direct-ready successor sidecar size is incompatible");
    }

    auto* const function = llvm::Function::Create(
        llvm::FunctionType::get(i32,
            { pointer, pointer, pointer, pointer, pointer, pointer }, false),
        llvm::Function::ExternalLinkage,
        std::string { direct_ready_successor_symbol }, module);
    function->setCallingConv(llvm::CallingConv::C);
    function->getArg(0)->setName("runtime.instance");
    function->getArg(1)->setName("frame");
    function->getArg(2)->setName("result");
    function->getArg(3)->setName("direct.ready.window.v1");
    function->getArg(4)->setName("prepared.outputs.v1");
    function->getArg(5)->setName("successor.masks.v1");

    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    auto* const inspect = llvm::BasicBlock::Create(
        context, "sidecar.inspect", function);
    auto* const invoke = llvm::BasicBlock::Create(
        context, "direct_ready.invoke", function);
    auto* const invalid = llvm::BasicBlock::Create(
        context, "sidecar.invalid", function);
    auto* const store_masks = llvm::BasicBlock::Create(
        context, "successor.store", function);
    auto* const finish = llvm::BasicBlock::Create(
        context, "successor.finish", function);
    auto* const declined = llvm::BasicBlock::Create(
        context, "direct_ready.declined", function);
    llvm::IRBuilder<> builder(entry);
    const auto pointer_bits = module.getDataLayout().getPointerSizeInBits();
    if (pointer_bits == 0U || pointer_bits > 64U) {
        throw LlvmJitUnsupportedError(
            "direct-ready successor pointer width is unsupported");
    }
    const auto nonnull = [&](llvm::Value* const value) {
        return builder.CreateICmpNE(value,
            llvm::ConstantPointerNull::get(pointer));
    };
    const auto aligned = [&](llvm::Value* const value,
                             const std::size_t alignment) {
        auto* const address = builder.CreatePtrToInt(value, i64);
        return builder.CreateICmpEQ(
            builder.CreateAnd(address,
                llvm::ConstantInt::get(i64, alignment - 1U)),
            llvm::ConstantInt::get(i64, 0U));
    };
    auto* const pointers_ready = builder.CreateAnd(
        builder.CreateAnd(nonnull(function->getArg(3)),
            nonnull(function->getArg(4))), nonnull(function->getArg(5)));
    auto* const descriptors_aligned = builder.CreateAnd(
        aligned(function->getArg(3),
            alignof(runtime::simir::RegionDirectReadyWindowV1)),
        builder.CreateAnd(
            aligned(function->getArg(4),
                alignof(runtime::simir::RegionPreparedOutputBatchV1)),
            aligned(function->getArg(5), alignof(Successor))));
    builder.CreateCondBr(
        builder.CreateAnd(pointers_ready, descriptors_aligned), inspect,
        invalid);

    builder.SetInsertPoint(inspect);
    const auto load_field = [&](llvm::Value* const base,
                                llvm::StructType* const type,
                                const unsigned field) {
        return builder.CreateLoad(i32,
            builder.CreateStructGEP(type, base, field));
    };
    auto* const sidecar = function->getArg(5);
    auto* const batch = function->getArg(4);
    auto* const sidecar_masks = builder.CreateLoad(pointer,
        builder.CreateStructGEP(sidecar_type, sidecar, 4U));
    auto* const slots = builder.CreateLoad(pointer,
        builder.CreateStructGEP(output_types.batch, batch, 4U));
    const auto masks_address = builder.CreatePtrToInt(sidecar_masks, i64);
    const auto slots_address = builder.CreatePtrToInt(slots, i64);
    auto* const sidecar_address
        = builder.CreatePtrToInt(sidecar, i64);
    auto* const batch_address = builder.CreatePtrToInt(batch, i64);
    const auto pointer_max = pointer_bits == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : ((UINT64_C(1) << pointer_bits) - 1U);
    const bool mask_extent_fits
        = bindings.size() <= pointer_max / sizeof(std::uint64_t);
    const auto mask_bytes = mask_extent_fits
        ? bindings.size() * sizeof(std::uint64_t) : 0U;
    const bool slots_extent_fits
        = bindings.size()
            <= pointer_max
                / sizeof(runtime::simir::RegionPreparedOutputSlotV1);
    const auto slots_bytes = slots_extent_fits
        ? bindings.size()
            * sizeof(runtime::simir::RegionPreparedOutputSlotV1) : 0U;
    if (!mask_extent_fits || !slots_extent_fits) {
        throw LlvmJitUnsupportedError(
            "direct-ready successor descriptor exceeds pointer range");
    }
    const auto fits_end = [&](llvm::Value* const address,
                              const std::size_t bytes) {
        return builder.CreateICmpULE(address,
            llvm::ConstantInt::get(i64, pointer_max - bytes));
    };
    const auto ranges_overlap = [&](llvm::Value* const left_begin,
                                    const std::size_t left_size,
                                    llvm::Value* const right_begin,
                                    const std::size_t right_size) {
        return builder.CreateAnd(
            builder.CreateICmpULT(left_begin,
                builder.CreateAdd(right_begin,
                    llvm::ConstantInt::get(i64, right_size))),
            builder.CreateICmpULT(right_begin,
                builder.CreateAdd(left_begin,
                    llvm::ConstantInt::get(i64, left_size))));
    };
    auto* const mask_extent_valid = builder.CreateAnd(
        builder.CreateAnd(nonnull(sidecar_masks),
            aligned(sidecar_masks, alignof(std::uint64_t))),
        fits_end(masks_address, mask_bytes));
    auto* const slots_extent_valid = builder.CreateAnd(
        builder.CreateAnd(nonnull(slots),
            aligned(slots, alignof(
                runtime::simir::RegionPreparedOutputSlotV1))),
        fits_end(slots_address, slots_bytes));
    auto* const descriptor_extents_valid = builder.CreateAnd(
        fits_end(sidecar_address, sizeof(Successor)),
        fits_end(batch_address,
            sizeof(runtime::simir::RegionPreparedOutputBatchV1)));
    auto* valid = builder.CreateAnd(
        builder.CreateAnd(mask_extent_valid, slots_extent_valid),
        descriptor_extents_valid);
    const std::array field_checks {
        std::pair { sidecar, 0U }, std::pair { sidecar, 1U },
        std::pair { sidecar, 2U }, std::pair { sidecar, 3U },
        std::pair { batch, 0U }, std::pair { batch, 1U },
        std::pair { batch, 2U }, std::pair { batch, 3U },
    };
    const auto binding_count = static_cast<std::uint32_t>(bindings.size());
    const std::array<std::uint32_t, 8U> expected_fields {
        runtime::simir::kRegionPreparedOutputSuccessorMasksAbiVersionV1,
        static_cast<std::uint32_t>(sizeof(Successor)), binding_count, 0U,
        runtime::simir::kRegionPreparedOutputBatchAbiVersionV1,
        static_cast<std::uint32_t>(
            sizeof(runtime::simir::RegionPreparedOutputBatchV1)),
        binding_count, 0U,
    };
    for (std::size_t index = 0U; index < field_checks.size(); ++index) {
        auto* const type = index < 4U ? sidecar_type : output_types.batch;
        auto* const field = load_field(field_checks[index].first, type,
            field_checks[index].second);
        valid = builder.CreateAnd(valid,
            builder.CreateICmpEQ(field,
                llvm::ConstantInt::get(i32, expected_fields[index])));
    }
    const std::array overlap_checks {
        ranges_overlap(masks_address, mask_bytes,
            sidecar_address, sizeof(Successor)),
        ranges_overlap(masks_address, mask_bytes,
            batch_address,
            sizeof(runtime::simir::RegionPreparedOutputBatchV1)),
        ranges_overlap(masks_address, mask_bytes,
            slots_address, slots_bytes),
        ranges_overlap(sidecar_address, sizeof(Successor),
            batch_address,
            sizeof(runtime::simir::RegionPreparedOutputBatchV1)),
        ranges_overlap(sidecar_address, sizeof(Successor),
            slots_address, slots_bytes),
        ranges_overlap(batch_address,
            sizeof(runtime::simir::RegionPreparedOutputBatchV1),
            slots_address, slots_bytes),
    };
    for (auto* const overlap : overlap_checks) {
        valid = builder.CreateAnd(valid, builder.CreateNot(overlap));
    }
    builder.CreateCondBr(valid, invoke, invalid);

    llvm::IRBuilder<> invalid_builder(invalid);
    invalid_builder.CreateRet(llvm::ConstantInt::get(i32,
        FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2));

    builder.SetInsertPoint(invoke);
    auto* const status = builder.CreateCall(direct_ready_entry,
        { function->getArg(0), function->getArg(1), function->getArg(2),
            function->getArg(3), function->getArg(4) },
        "direct.ready.status");
    status->setCallingConv(llvm::CallingConv::C);
    builder.CreateCondBr(
        builder.CreateICmpEQ(status, llvm::ConstantInt::get(i32,
            FSIM_JIT_RESUME_STATUS_COMPLETED_V2)), store_masks, declined);
    llvm::IRBuilder<> declined_builder(declined);
    declined_builder.CreateRet(status);

    builder.SetInsertPoint(store_masks);
    auto* const output_slots = builder.CreateLoad(pointer,
        builder.CreateStructGEP(output_types.batch, batch, 4U));
    for (const auto& binding : bindings) {
        auto* const descriptor = builder.CreateGEP(output_types.slot,
            output_slots,
            llvm::ConstantInt::get(i32, binding.descriptor_slot));
        auto* const selected = builder.CreateLoad(i32,
            builder.CreateStructGEP(output_types.slot, descriptor, 6U));
        auto* const mask_pointer = builder.CreateGEP(i64, sidecar_masks,
            llvm::ConstantInt::get(i32, binding.descriptor_slot));
        auto* const selected_block = llvm::BasicBlock::Create(
            context, "successor.selected", function);
        auto* const unselected_block = llvm::BasicBlock::Create(
            context, "successor.unselected", function);
        auto* const next = llvm::BasicBlock::Create(
            context, "successor.next", function);
        builder.CreateCondBr(builder.CreateICmpNE(selected,
            llvm::ConstantInt::get(i32, 0U)), selected_block,
            unselected_block);
        builder.SetInsertPoint(selected_block);
        auto* const changed_pointer = builder.CreateLoad(pointer,
            builder.CreateStructGEP(output_types.slot, descriptor, 21U));
        auto* const changed = builder.CreateLoad(i8, changed_pointer);
        builder.CreateStore(builder.CreateSelect(
            builder.CreateICmpNE(changed, llvm::ConstantInt::get(i8, 0U)),
            llvm::ConstantInt::get(i64, binding.successor_member_mask),
            llvm::ConstantInt::get(i64, 0U)), mask_pointer);
        builder.CreateBr(next);
        builder.SetInsertPoint(unselected_block);
        builder.CreateStore(llvm::ConstantInt::get(i64, 0U), mask_pointer);
        builder.CreateBr(next);
        builder.SetInsertPoint(next);
    }
    builder.CreateBr(finish);
    builder.SetInsertPoint(finish);
    builder.CreateRet(status);
}

} // namespace

void lower_process(llvm::Module& module, const std::string& symbol,
    const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const std::span<const runtime::simir::SignalId> direct_read_signals,
    const std::span<const runtime::simir::SignalId> direct_update_signals,
    const bool signal_callback_ids_are_actual,
    const std::span<const runtime::simir::SignalId>
        signal_callback_operands,
    const std::uint32_t signal_callback_operand_word_base,
    const ValidatedProcess& validated,
    const ProcessLoweringPlan& lowering_plan,
    const JitOptimizationLevel optimization,
    const bool debug_instrumentation,
    const bool require_direct_update_slots,
    const bool require_direct_read_signals,
    const ProcessLoweringMode mode,
    const std::span<const runtime::simir::InstructionIndex>
        bound_literal_sites,
    const std::span<const FusedMaskedMemberGate> masked_member_gates,
    std::vector<std::uint8_t>& register_values_persistent,
    const std::string_view prepared_output_entry_symbol,
    const std::span<const RegionPreparedOutputLoweringBinding>
        prepared_output_bindings,
    const std::string_view prepared_output_successor_entry_symbol,
    const std::string_view direct_ready_entry_symbol,
    const std::string_view direct_ready_successor_entry_symbol,
    const std::span<const RegionDirectReadyLoweringBinding>
        direct_ready_bindings)
{
    const auto register_value_kinds
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process.register_value_kinds);
    const auto container_register_types
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process.container_register_types);
    const auto static_trigger_regions
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process.static_trigger_regions);
    auto& context = module.getContext();
    auto* i32 = llvm::Type::getInt32Ty(context);
    auto* i64 = llvm::Type::getInt64Ty(context);
    auto* pointer = llvm::PointerType::getUnqual(context);
    const auto jit_abi_v2 = create_jit_abi_v2_types(context);
    validate_jit_abi_v2_layout(module.getDataLayout(), jit_abi_v2);
    auto* runtime_type = jit_abi_v2.runtime_instance;
    auto* services_type = jit_abi_v2.services;
    auto* direct_update_slot_type = jit_abi_v2.update_slot;
    auto* frame_type = jit_abi_v2.frame;
    auto* result_type = jit_abi_v2.resume_result;
    auto* function_type = llvm::FunctionType::get(i32, { pointer, pointer, pointer }, false);
    auto* function = llvm::Function::Create(
        function_type, llvm::Function::ExternalLinkage, symbol, module);
    function->setCallingConv(llvm::CallingConv::C);
    function->getArg(0)->setName("runtime.instance");
    function->getArg(1)->setName("frame");
    function->getArg(2)->setName("result");
    auto* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* runtime_argument = function->getArg(0);
    auto* frame_argument = function->getArg(1);
    auto* result_argument = function->getArg(2);
    auto* services_argument = builder.CreateLoad(
        pointer,
        runtime_instance_field_address(
            builder, runtime_type, runtime_argument,
            JitRuntimeInstanceField::services),
        "services");
    auto* context_pointer = builder.CreateLoad(
        pointer, runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::context),
        "context");
    auto* read_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::read_signal, "read_signal");
    auto* write_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_signal, "write_signal");
    auto* assert_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::assert_failed, "assert_failed");
    llvm::Value* code_coverage_hit_counters = nullptr;
    llvm::Value* code_coverage_counter_values = nullptr;
    llvm::Value* code_coverage_hit_count = nullptr;
    llvm::Value* code_coverage_counter_count = nullptr;
    llvm::Value* record_code_coverage_counter = nullptr;
    llvm::FunctionType* record_code_coverage_counter_type = nullptr;
    if (validated.uses_code_coverage) {
        code_coverage_hit_counters = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::code_coverage_hit_counters),
            "code_coverage_hit_counters");
        code_coverage_counter_values = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::code_coverage_counter_values),
            "code_coverage_counter_values");
        code_coverage_hit_count = builder.CreateLoad(
            i32,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::code_coverage_hit_count),
            "code_coverage_hit_count");
        code_coverage_counter_count = builder.CreateLoad(
            i32,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::code_coverage_counter_count),
            "code_coverage_counter_count");
        record_code_coverage_counter = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::record_code_coverage_counter, "record_code_coverage_counter");
        record_code_coverage_counter_type = llvm::FunctionType::get(
            i32, { pointer, i32, i32, i32 }, false);
    }
    llvm::Value* sample_coverage_callback = nullptr;
    llvm::Value* class_property_operation_callback = nullptr;
    llvm::Value* event_triggered_callback = nullptr;
    llvm::FunctionType* native_service_callback_type = nullptr;
    if (validated.uses_coverage_sample
        || validated.uses_class_property_operation
        || validated.uses_event_triggered) {
        native_service_callback_type = llvm::FunctionType::get(
            i32, { pointer, i32, i32, pointer }, false);
        if (validated.uses_coverage_sample) {
            sample_coverage_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::sample_coverage, "sample_coverage");
        }
        if (validated.uses_class_property_operation) {
            class_property_operation_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::execute_class_property_operation, "execute_class_property_operation");
        }
        if (validated.uses_event_triggered) {
            event_triggered_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::query_event_triggered, "query_event_triggered");
        }
    }
    llvm::Value* direct_update_slots = nullptr;
    llvm::Value* direct_update_active_words = nullptr;
    llvm::Value* static_trigger_mask = nullptr;
    if (!debug_instrumentation
        && !static_trigger_regions.empty()) {
        static_trigger_mask = builder.CreateLoad(
            i64,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::static_trigger_mask),
            "static.trigger.mask");
    }
    if (!direct_update_signals.empty()) {
        direct_update_slots = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_update_slots),
            "direct_update_slots");
        direct_update_active_words = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_update_active_words),
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
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_signal_aval),
            "direct_signal_aval");
        direct_signal_bval = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_signal_bval),
            "direct_signal_bval");
        direct_signal_logic9_plane0 = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_signal_logic9_plane0),
            "direct_signal_logic9_plane0");
        direct_signal_logic9_plane1 = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_signal_logic9_plane1),
            "direct_signal_logic9_plane1");
        direct_signal_logic9_plane2 = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_signal_logic9_plane2),
            "direct_signal_logic9_plane2");
        direct_signal_logic9_plane3 = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_signal_logic9_plane3),
            "direct_signal_logic9_plane3");
        direct_signal_count = builder.CreateLoad(
            i32,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_signal_count),
            "direct_signal_count");
        if (!direct_read_signals.empty()) {
            direct_read_signal_map = builder.CreateLoad(
                pointer,
                runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_read_signals),
                "direct_read_signals");
            direct_read_signal_count = builder.CreateLoad(
                i32,
                runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_read_signal_count),
                "direct_read_signal_count");
        }
    }
    if (validated.uses_wide_signal_read
        && !direct_read_signals.empty()) {
        direct_wide_signal_aval = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_wide_signal_aval),
            "direct_wide_signal_aval");
        direct_wide_signal_bval = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_wide_signal_bval),
            "direct_wide_signal_bval");
        direct_wide_signal_logic9_plane2 = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_wide_signal_logic9_plane2),
            "direct_wide_signal_logic9_plane2");
        direct_wide_signal_logic9_plane3 = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_wide_signal_logic9_plane3),
            "direct_wide_signal_logic9_plane3");
        direct_wide_signal_offsets = builder.CreateLoad(
            pointer,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_wide_signal_offsets),
            "direct_wide_signal_offsets");
        direct_wide_signal_offset_count = builder.CreateLoad(
            i32,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_wide_signal_offset_count),
            "direct_wide_signal_offset_count");
        direct_wide_word_count = builder.CreateLoad(
            i32,
            runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::direct_wide_word_count),
            "direct_wide_word_count");
    }
    llvm::Value* write_update_callback = nullptr;
    if (validated.uses_write_update) {
        write_update_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_update, "write_update");
    }
    llvm::Value* write_after_callback = nullptr;
    if (validated.uses_write_after) {
        write_after_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_after, "write_after");
    }
    llvm::Value* write_blocking_slice_callback = nullptr;
    if (validated.uses_write_blocking_slice) {
        write_blocking_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_signal_slice, "write_signal_slice");
    }
    llvm::Value* write_update_slice_callback = nullptr;
    if (validated.uses_write_update_slice) {
        write_update_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_update_slice, "write_update_slice");
    }
    llvm::Value* write_after_slice_callback = nullptr;
    if (validated.uses_write_after_slice) {
        write_after_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_after_slice, "write_after_slice");
    }
    llvm::Value* force_signal_slice_callback = nullptr;
    llvm::Value* force_signal_slice_logic9_callback = nullptr;
    if (validated.uses_force_signal_slice) {
        force_signal_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::force_signal_slice, "force_signal_slice");
        if (validated.uses_logic9) {
            force_signal_slice_logic9_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::force_signal_slice_logic9, "force_signal_slice_logic9");
        }
    }
    llvm::Value* release_signal_slice_callback = nullptr;
    if (validated.uses_release_signal_slice) {
        release_signal_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::release_signal_slice, "release_signal_slice");
    }
    llvm::Value* force_driver_signal_slice_callback = nullptr;
    llvm::Value* force_driver_signal_slice_logic9_callback = nullptr;
    if (validated.uses_force_driver_signal_slice) {
        force_driver_signal_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::force_driver_signal_slice, "force_driver_signal_slice");
        if (validated.uses_logic9) {
            force_driver_signal_slice_logic9_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::force_driver_signal_slice_logic9, "force_driver_signal_slice_logic9");
        }
    }
    llvm::Value* release_driver_signal_slice_callback = nullptr;
    if (validated.uses_release_driver_signal_slice) {
        release_driver_signal_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::release_driver_signal_slice, "release_driver_signal_slice");
    }
    llvm::Value* runtime_flags = nullptr;
    if (validated.uses_debug_points && debug_instrumentation) {
        runtime_flags = builder.CreateLoad(
            i32, runtime_instance_field_address(
                builder, runtime_type, runtime_argument,
                JitRuntimeInstanceField::flags),
            "runtime.flags");
    }
    llvm::Value* signal_event_callback = nullptr;
    if (validated.uses_signal_event) {
        signal_event_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::signal_event, "signal_event");
    }
    llvm::Value* signal_last_value_callback = nullptr;
    if (validated.uses_signal_last_value) {
        signal_last_value_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::signal_last_value, "signal_last_value");
    }
    llvm::Value* signal_last_event_callback = nullptr;
    if (validated.uses_signal_last_event) {
        signal_last_event_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::signal_last_event, "signal_last_event");
    }
    llvm::Value* signal_active_callback = nullptr;
    if (validated.uses_signal_active) {
        signal_active_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::signal_active, "signal_active");
    }
    llvm::Value* signal_last_active_callback = nullptr;
    if (validated.uses_signal_last_active) {
        signal_last_active_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::signal_last_active, "signal_last_active");
    }
    llvm::Value* signal_driving_callback = nullptr;
    if (validated.uses_signal_driving) {
        signal_driving_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::signal_driving, "signal_driving");
    }
    llvm::Value* signal_driving_value_callback = nullptr;
    llvm::Value* signal_driving_value_logic9_callback = nullptr;
    if (validated.uses_signal_driving_value) {
        signal_driving_value_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::signal_driving_value, "signal_driving_value");
        signal_driving_value_logic9_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::signal_driving_value_logic9, "signal_driving_value_logic9");
    }
    llvm::Value* read_simulation_time_callback = nullptr;
    if (validated.uses_simulation_time) {
        read_simulation_time_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::read_simulation_time, "read_simulation_time");
    }
    llvm::Value* vital_timing_check_callback = nullptr;
    if (validated.uses_vital_timing) {
        vital_timing_check_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::vital_timing_check, "vital_timing_check");
    }
    llvm::Value* vital_delay_callback = nullptr;
    if (validated.uses_vital_delay) {
        vital_delay_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::vital_delay, "vital_delay");
    }
    llvm::Value* output_callback = nullptr;
    if (validated.uses_output) {
        output_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_output, "write_output");
    }
    llvm::Value* postponed_output_callback = nullptr;
    if (validated.uses_postponed_output) {
        postponed_output_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::schedule_output, "schedule_output");
    }
    llvm::Value* report_callback = nullptr;
    if (validated.uses_report) {
        report_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_report, "write_report");
    }
    llvm::Value* formatted_output_callback = nullptr;
    if (validated.uses_formatted_output) {
        formatted_output_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_formatted, "write_formatted");
    }
    llvm::Value* time_output_callback = nullptr;
    if (validated.uses_time_output) {
        time_output_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_time, "write_time");
    }
    llvm::Value* monitor_install_callback = nullptr;
    if (validated.uses_monitor_install) {
        monitor_install_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::install_monitor, "install_monitor");
    }
    llvm::Value* monitor_control_callback = nullptr;
    if (validated.uses_monitor_control) {
        monitor_control_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::control_monitor, "control_monitor");
    }
    llvm::Value* random_value_callback = nullptr;
    if (validated.uses_random_value) {
        random_value_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::random_value, "random_value");
    }
    llvm::Value* write_inertial_callback = nullptr;
    if (validated.uses_write_inertial) {
        write_inertial_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_inertial, "write_inertial");
    }
    llvm::Value* write_inertial_slice_callback = nullptr;
    if (validated.uses_write_inertial_slice) {
        write_inertial_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_inertial_slice, "write_inertial_slice");
    }
    llvm::Value* exact_signal_callback = nullptr;
    if (validated.uses_exact_signal_operation) {
        exact_signal_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::execute_signal_operation, "execute_signal_operation");
    }
    llvm::Value* read_signal_packed_callback = nullptr;
    if (validated.uses_wide_signal_read) {
        read_signal_packed_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::read_signal_packed, "read_signal_packed");
    }
    llvm::Value* write_signal_packed_callback = nullptr;
    if (validated.uses_wide_signal_write) {
        write_signal_packed_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_signal_packed, "write_signal_packed");
    }
    llvm::Value* write_projected_signal_packed_callback = nullptr;
    if (validated.uses_wide_projected_write) {
        write_projected_signal_packed_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_projected_signal_packed,
                "write_projected_signal_packed");
    }
    auto* read_signal_dynamic_part_callback = load_jit_service_callback(
        builder, services_type, services_argument,
        JitServiceField::read_signal_dynamic_part,
        "read_signal_dynamic_part");
    llvm::Value* write_projected_callback = nullptr;
    if (validated.uses_write_projected) {
        write_projected_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_projected, "write_projected");
    }
    llvm::Value* write_projected_slice_callback = nullptr;
    if (validated.uses_write_projected_slice) {
        write_projected_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_projected_slice, "write_projected_slice");
    }
    llvm::Value* write_projected_waveform_callback = nullptr;
    if (validated.uses_write_projected_waveform) {
        write_projected_waveform_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_projected_waveform, "write_projected_waveform");
    }
    llvm::Value* write_projected_waveform_slice_callback = nullptr;
    if (validated.uses_write_projected_waveform_slice) {
        write_projected_waveform_slice_callback = load_jit_service_callback(
                builder, services_type, services_argument,
                JitServiceField::write_projected_waveform_slice, "write_projected_waveform_slice");
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
            [&](const JitServiceField field,
                const llvm::Twine& name) -> llvm::Value* {
            return load_jit_service_callback(
                builder, services_type, services_argument, field,
                name.str());
        };
        read_logic9_callback = load_callback(
            JitServiceField::read_signal_logic9, "read_signal_logic9");
        write_logic9_callback = load_callback(
            JitServiceField::write_signal_logic9, "write_signal_logic9");
        write_update_logic9_callback = load_callback(
            JitServiceField::write_update_logic9, "write_update_logic9");
        write_after_logic9_callback = load_callback(
            JitServiceField::write_after_logic9, "write_after_logic9");
        write_blocking_slice_logic9_callback = load_callback(
            JitServiceField::write_signal_slice_logic9,
            "write_signal_slice_logic9");
        write_update_slice_logic9_callback = load_callback(
            JitServiceField::write_update_slice_logic9,
            "write_update_slice_logic9");
        write_after_slice_logic9_callback = load_callback(
            JitServiceField::write_after_slice_logic9,
            "write_after_slice_logic9");
        signal_last_value_logic9_callback = load_callback(
            JitServiceField::signal_last_value_logic9,
            "signal_last_value_logic9");
        write_inertial_logic9_callback = load_callback(
            JitServiceField::write_inertial_logic9, "write_inertial_logic9");
        write_inertial_slice_logic9_callback = load_callback(
            JitServiceField::write_inertial_slice_logic9,
            "write_inertial_slice_logic9");
        write_projected_logic9_callback = load_callback(
            JitServiceField::write_projected_logic9, "write_projected_logic9");
        write_projected_slice_logic9_callback = load_callback(
            JitServiceField::write_projected_slice_logic9,
            "write_projected_slice_logic9");
        write_projected_waveform_logic9_callback = load_callback(
            JitServiceField::write_projected_waveform_logic9,
            "write_projected_waveform_logic9");
        write_projected_waveform_slice_logic9_callback = load_callback(
            JitServiceField::write_projected_waveform_slice_logic9,
            "write_projected_waveform_slice_logic9");
        write_formatted_logic9_callback = load_callback(
            JitServiceField::write_formatted_logic9,
            "write_formatted_logic9");
    }
    auto* read_type = llvm::FunctionType::get(i64, { pointer, i32, pointer }, false);
    auto* write_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64 }, false);
    auto* write_update_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i32 },
        false);
    auto* assert_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
        { pointer, i32, i32, pointer, i64 }, false);
    auto* write_after_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i64, i32 }, false);
    auto* write_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64 },
        false);
    auto* write_update_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i32 },
        false);
    auto* write_after_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i64, i32 },
        false);
    auto* release_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32 },
        false);
    auto* write_inertial_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i64, i64, i64, i32 },
        false);
    auto* write_inertial_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i64, i64, i64, i32 },
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
            pointer, pointer, pointer, pointer, i32 },
        false);
    auto* write_projected_signal_packed_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, pointer, pointer, pointer, pointer },
        false);
    auto* write_projected_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i64, i64, i32 },
        false);
    auto* write_projected_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i64, i64, i32 },
        false);
    auto* projected_element_type = jit_abi_v2.projected_element;
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
    auto* logic9_word_type = llvm::cast<llvm::ArrayType>(
        jit_abi_v2.logic9_word->getElementType(0));
    auto* read_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer },
        false);
    auto* write_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer },
        false);
    auto* write_update_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i32 },
        false);
    auto* write_after_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64, i32 },
        false);
    auto* write_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer },
        false);
    auto* write_update_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i32 },
        false);
    auto* write_after_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i64, i32 },
        false);
    auto* write_inertial_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64, i64, i64, i32 },
        false);
    auto* write_inertial_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i64, i64, i64, i32 },
        false);
    auto* write_projected_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64, i64, i32 },
        false);
    auto* write_projected_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i64, i64, i32 },
        false);
    auto* logic9_projected_element_type
        = jit_abi_v2.logic9_projected_element;
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
    // Keep validated reference counts intact for diagnostics and all other
    // consumers. Only remove save-list references for native frame pushes
    // that this JIT plan actually lowers as no-op branches.
    auto effective_container_register_reference_counts
        = validated.container_register_reference_counts;
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        if (index >= lowering_plan.operations.size()
            || !lowering_plan.operations[index]
            || index >= native_callables.frame_operations.size()
            || !native_callables.frame_operations[index]) {
            continue;
        }
        const auto* push
            = fsim::runtime::simir::operation_get_if<CallableFramePush>(
                &process.operations[index]);
        if (push == nullptr) {
            continue;
        }
        for (const auto register_id : push->containers) {
            if (register_id
                    < effective_container_register_reference_counts.size()
                && effective_container_register_reference_counts[register_id]
                    != 0U) {
                --effective_container_register_reference_counts[register_id];
            }
        }
    }
    const auto& resume_entries = lowering_plan.entry_points;
    const bool transient_boundaries_safe = std::ranges::all_of(
        process.operations,
        [&validated](const runtime::simir::Operation& operation) {
            return !is_resume_boundary(
                       operation, validated.register_widths)
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
    if (std::getenv("FSIM_PROFILE_JIT_REGISTERS") != nullptr) {
        std::vector<std::size_t> definition_counts(process.register_count);
        for (const auto& definitions : validated.instruction_definitions) {
            for (const auto register_id : definitions) {
                if (register_id < definition_counts.size()) {
                    ++definition_counts[register_id];
                }
            }
        }
        std::vector<bool> non_elided_frame_registers(
            process.register_count);
        for (std::size_t index = 0;
             index < process.operations.size(); ++index) {
            const auto* push = operation_get_if<CallableFramePush>(
                &process.operations[index]);
            if (push == nullptr
                || (index < native_callables.frame_operations.size()
                    && native_callables.frame_operations[index])) {
                continue;
            }
            for (const auto register_id : push->packed) {
                if (register_id < non_elided_frame_registers.size()) {
                    non_elided_frame_registers[register_id] = true;
                }
            }
        }
        std::size_t registers_le64 { };
        std::size_t single_definition_registers { };
        std::size_t single_definition_le64 { };
        std::size_t frame_restore_excluded_le64 { };
        std::size_t candidate_upper_bound_le64 { };
        const auto register_count = std::min(
            validated.register_widths.size(), definition_counts.size());
        for (std::size_t index = 0; index < register_count; ++index) {
            const bool narrow = validated.register_widths[index] <= 64U;
            const bool single_definition = definition_counts[index] == 1U;
            if (narrow) {
                ++registers_le64;
            }
            if (single_definition) {
                ++single_definition_registers;
            }
            if (!narrow || !single_definition) {
                continue;
            }
            ++single_definition_le64;
            if (non_elided_frame_registers[index]) {
                ++frame_restore_excluded_le64;
            } else {
                ++candidate_upper_bound_le64;
            }
        }
        std::string profile_line;
        llvm::raw_string_ostream profile(profile_line);
        profile << "fsim-profile: jit-register-candidates process_id="
                << process.id
                << " operations=" << process.operations.size()
                << " restartable=" << restartable_register_frame
                << " transient=" << transient_register_frame
                << " split_transient=" << split_transient_register_frame
                << " hybrid_transient=" << hybrid_transient_register_frame
                << " debug=" << debug_instrumentation
                << " partial=" << lowering_plan.partial
                << " registers=" << process.register_count
                << " validated_widths=" << validated.register_widths.size()
                << " registers_le64=" << registers_le64
                << " single_definition_registers="
                << single_definition_registers
                << " single_definition_le64="
                << single_definition_le64
                << " frame_restore_excluded_le64="
                << frame_restore_excluded_le64
                << " candidate_upper_bound_le64="
                << candidate_upper_bound_le64
                << " frame_exclusions_only=1 escape_analysis=0"
                << " dominance_proof=0\n";
        profile.flush();
        llvm::errs() << profile_line;
    }
    std::uint64_t register_word_count { };
    for (const auto width : validated.register_widths) {
        register_word_count += (static_cast<std::uint64_t>(width) + 63U) / 64U;
    }
    auto* local_register_type = llvm::ArrayType::get(
        i64, std::max<std::uint64_t>(register_word_count, 1U));
    llvm::Value* frame_register_aval = nullptr;
    llvm::Value* frame_register_bval = nullptr;
    if (!transient_register_frame || validated.uses_exact_signal_operation
        || validated.uses_files || validated.uses_vital_delay
        || validated.uses_containers || validated.uses_strings
        || (signal_callback_ids_are_actual
            && !signal_callback_operands.empty())) {
        frame_register_aval = builder.CreateLoad(
            pointer, builder.CreateStructGEP(frame_type, frame_argument, 8),
            "register.aval.base");
        frame_register_bval = builder.CreateLoad(
            pointer, builder.CreateStructGEP(frame_type, frame_argument, 9),
            "register.bval.base");
    }
    llvm::Value* register_aval = frame_register_aval;
    llvm::Value* register_bval = frame_register_bval;
    if (transient_register_frame && !split_transient_register_frame) {
        register_aval = builder.CreateAlloca(
            local_register_type, nullptr, "register.aval.local");
        register_bval = builder.CreateAlloca(
            local_register_type, nullptr, "register.bval.local");
    }
    llvm::Value* register_initialized = nullptr;
    if (debug_instrumentation || !process.debug_locals.empty()) {
        register_initialized = builder.CreateLoad(
            pointer, builder.CreateStructGEP(frame_type, frame_argument, 10),
            "register.initialized.base");
    }
    llvm::Value* frame_register_logic9_plane2 = nullptr;
    llvm::Value* frame_register_logic9_plane3 = nullptr;
    if ((!transient_register_frame
            || validated.uses_files
            || validated.uses_exact_signal_operation
            || validated.uses_vital_delay
            || validated.uses_containers
            || validated.uses_strings)
        && validated.uses_logic9) {
        frame_register_logic9_plane2 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(frame_type, frame_argument, 11),
            "register.logic9.plane2.base");
        frame_register_logic9_plane3 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(frame_type, frame_argument, 12),
            "register.logic9.plane3.base");
    }
    llvm::Value* register_logic9_plane2 = frame_register_logic9_plane2;
    llvm::Value* register_logic9_plane3 = frame_register_logic9_plane3;
    if (transient_register_frame && !split_transient_register_frame
        && validated.uses_logic9) {
        register_logic9_plane2 = builder.CreateAlloca(
            local_register_type, nullptr, "register.logic9.plane2.local");
        register_logic9_plane3 = builder.CreateAlloca(
            local_register_type, nullptr, "register.logic9.plane3.local");
    }
    auto* i8 = llvm::Type::getInt8Ty(context);
    std::vector<RegisterSlot> registers(process.register_count);
    std::vector<RegisterSlot> frame_registers(process.register_count);
    register_values_persistent.assign(process.register_count, 0U);
    std::vector<ConstantPlaneForwarding> constant_plane_forwarding;
    std::size_t suppressed_false_guards { };
    std::uint64_t register_word_offset { };
    for (std::size_t index = 0; index < process.register_count; ++index) {
        const auto width = validated.register_widths[index];
        if (width == 0) {
            continue;
        }
        const auto kind = register_value_kinds.empty()
            ? ValueKind::logic4
            : register_value_kinds[index];
        llvm::Value* aval_base = register_aval;
        llvm::Value* bval_base = register_bval;
        llvm::Value* plane2_base = register_logic9_plane2;
        llvm::Value* plane3_base = register_logic9_plane3;
        auto word_offset = register_word_offset;
        const bool transient_register = transient_register_frame
            || (hybrid_transient_register_frame
                && !persistent_debug_registers[index]);
        register_values_persistent[index]
            = static_cast<std::uint8_t>(!transient_register);
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
            nullptr,
            mode == ProcessLoweringMode::region_known_logic4
                && kind == ValueKind::logic4,
        };
        frame_registers[index] = {
            frame_register_aval,
            frame_register_bval,
            register_initialized,
            frame_register_logic9_plane2,
            frame_register_logic9_plane3,
            register_word_offset,
            static_cast<std::uint32_t>(index),
            width,
            kind,
            nullptr,
            mode == ProcessLoweringMode::region_known_logic4
                && kind == ValueKind::logic4,
        };
        register_word_offset += (static_cast<std::uint64_t>(width) + 63U) / 64U;
    }
    const bool allow_constant_planes =
        optimization == JitOptimizationLevel::o2
        && !debug_instrumentation
        && !lowering_plan.partial
        && split_transient_register_frame
        && static_trigger_regions.empty()
        && !validated.uses_exact_signal_operation
        && !validated.uses_files
        && !validated.uses_strings
        && !validated.uses_containers
        && !validated.uses_vital_delay
        && !validated.uses_coverage_sample
        && !validated.uses_class_property_operation
        && !validated.uses_event_triggered;
    // A bound site is an instance-local dynamic value, even when the
    // representative SimIR carries a literal for ordinary lowering.
    const bool allow_unbound_constant_planes = allow_constant_planes
        && bound_literal_sites.empty();
    if (allow_unbound_constant_planes) {
        constant_plane_forwarding.resize(process.register_count);
        std::vector<std::size_t> definition_counts(process.register_count);
        std::vector<std::size_t> definition_indexes(process.register_count);
        std::vector<std::size_t> earliest_uses(
            process.register_count, process.operations.size());
        std::vector<bool> excluded(process.register_count);
        for (std::size_t index = 0;
             index < process.operations.size(); ++index) {
            for (const auto id : validated.instruction_definitions[index]) {
                ++definition_counts[id];
                definition_indexes[id] = index;
            }
            for (const auto id : validated.instruction_uses[index]) {
                earliest_uses[id] = std::min(earliest_uses[id], index);
            }
            const auto* push = operation_get_if<CallableFramePush>(
                &process.operations[index]);
            if (push != nullptr
                && !native_callables.frame_operations[index]) {
                for (const auto id : push->packed) {
                    excluded[id] = true;
                }
            }
            const auto exclude_stack = [&](const CallStack& stack) {
                if (stack.capacity == 0U) {
                    return;
                }
                excluded[stack.pointer] = true;
                for (std::size_t id = stack.entries;
                     id < process.register_count
                         && id - stack.entries < stack.capacity;
                     ++id) {
                    excluded[id] = true;
                }
            };
            if (const auto* call = operation_get_if<Call>(
                    &process.operations[index])) {
                exclude_stack(call->stack);
            }
            if (const auto* ret = operation_get_if<Return>(
                    &process.operations[index])) {
                exclude_stack(ret->stack);
            }
        }
        for (std::size_t id = 0; id < process.register_count; ++id) {
            if (excluded[id] || definition_counts[id] != 1U
                || registers[id].width == 0U
                || registers[id].width > 64U) {
                continue;
            }
            const auto definition = definition_indexes[id];
            if (earliest_uses[id] <= definition) {
                continue;
            }
            const auto& defining_operation = process.operations[definition];
            const bool guaranteed_store =
                operation_holds<LoadConstant>(defining_operation)
                || operation_holds<CopyRegister>(defining_operation)
                || operation_holds<ConvertToTwoState>(defining_operation)
                || operation_holds<UnaryNot>(defining_operation)
                || operation_holds<LogicalNot>(defining_operation)
                || operation_holds<LogicalBinary>(defining_operation)
                || operation_holds<Reduction>(defining_operation)
                || operation_holds<CountOnes>(defining_operation)
                || operation_holds<CountBits>(defining_operation)
                || operation_holds<Shift>(defining_operation)
                || operation_holds<Extract>(defining_operation)
                || operation_holds<Insert>(defining_operation)
                || operation_holds<DynamicInsert>(defining_operation)
                || operation_holds<DynamicPartInsert>(defining_operation)
                || operation_holds<Concatenate>(defining_operation)
                || operation_holds<Binary>(defining_operation)
                || operation_holds<IntegerUnary>(defining_operation)
                || operation_holds<IntegerBinary>(defining_operation)
                || operation_holds<IntegerCheck>(defining_operation)
                || operation_holds<ConditionalSelect>(defining_operation)
                || operation_holds<DynamicExtract>(defining_operation)
                || operation_holds<DynamicPartSelect>(defining_operation);
            if (!guaranteed_store) {
                continue;
            }
            constant_plane_forwarding[id].definition = definition;
            registers[id].constant_planes = &constant_plane_forwarding[id];
        }
    }
    if ((transient_register_frame || hybrid_transient_register_frame)
        && validated.uses_strings) {
        std::vector<bool> file_scan_output_registers(process.register_count);
        for (const auto& operation : process.operations) {
            const auto* scan = operation_get_if<FileScan>(&operation);
            if (scan == nullptr) {
                continue;
            }
            for (const auto& conversion : scan->conversions) {
                if (!conversion.suppress
                    && conversion.target.kind
                        == InputScanTargetKind::packed_register
                    && conversion.target.id
                        < file_scan_output_registers.size()) {
                    file_scan_output_registers[conversion.target.id] = true;
                }
            }
            if (scan->success
                && *scan->success < file_scan_output_registers.size()) {
                file_scan_output_registers[*scan->success] = true;
            }
        }
        for (std::size_t index = 0;
            index < file_scan_output_registers.size(); ++index) {
            if (!file_scan_output_registers[index]) {
                continue;
            }
            const auto& working = registers[index];
            const auto& frame = frame_registers[index];
            if (working.aval_base == frame.aval_base
                && working.bval_base == frame.bval_base
                && working.logic9_plane2_base == frame.logic9_plane2_base
                && working.logic9_plane3_base == frame.logic9_plane3_base
                && working.word_offset == frame.word_offset) {
                continue;
            }
            store_register(
                builder,
                registers,
                static_cast<RegisterId>(index),
                load_register(
                    builder,
                    frame_registers,
                    static_cast<RegisterId>(index)));
        }
    }
    auto* read_bval_slot = builder.CreateAlloca(i64, nullptr, "read.bval");
    auto* logic9_word_slot = builder.CreateAlloca(logic9_word_type, nullptr, "logic9.word");
    auto* container_result_aval_slot = builder.CreateAlloca(
        i64, nullptr, "container.result.aval");
    auto* container_result_bval_slot = builder.CreateAlloca(
        i64, nullptr, "container.result.bval");
    std::uint32_t wide_callback_scratch_word_stride = 0U;
    std::uint32_t wide_callback_scratch_plane_count = 2U;
    const auto register_kind = [&](const RegisterId register_id) {
        return register_id < register_value_kinds.size()
            ? register_value_kinds[register_id]
            : ValueKind::logic4;
    };
    const auto include_callback_width = [&](const std::uint32_t width) {
        if (width <= 64U) {
            return;
        }
        const auto words = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(width) + 63U) / 64U);
        wide_callback_scratch_word_stride = std::max(
            wide_callback_scratch_word_stride, words);
    };
    for (const auto& operation : process.operations) {
        if (const auto* const read
            = operation_get_if<ReadSignal>(&operation)) {
            if (read->signal < signal_widths.size()
                && read->destination < validated.register_widths.size()
                && !require_direct_read_signals) {
                const auto width
                    = validated.register_widths[read->destination];
                include_callback_width(width);
                if (width > 64U
                    && register_kind(read->destination) == ValueKind::logic9) {
                    wide_callback_scratch_plane_count = 4U;
                }
            }
        }
        visit_operation([&](const auto& write) {
            using T = std::decay_t<decltype(write)>;
            if constexpr (std::is_same_v<T, WriteBlocking>
                || std::is_same_v<T, WriteUpdate>
                || std::is_same_v<T, WriteAfter>
                || std::is_same_v<T, WriteBlockingSlice>
                || std::is_same_v<T, WriteUpdateSlice>
                || std::is_same_v<T, WriteAfterSlice>
                || std::is_same_v<T, WriteProjected>) {
                if (write.signal >= signal_widths.size()
                    || signal_widths[write.signal] <= 64U
                    || write.source >= validated.register_widths.size()) {
                    return;
                }
                if constexpr (std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>) {
                    if (require_direct_update_slots
                        && write.domain == SignalUpdateDomain::generic
                        && std::ranges::find(direct_update_signals, write.signal)
                            != direct_update_signals.end()) {
                        return;
                    }
                }
                if constexpr (std::is_same_v<T, WriteProjected>) {
                    if (write.delay != 0U || write.rejection != 0U
                        || write.mode != ProjectedDelayMode::inertial) {
                        return;
                    }
                    if (!signal_value_kinds.empty()
                        && write.signal < signal_value_kinds.size()
                        && signal_value_kinds[write.signal]
                            == ValueKind::logic9) {
                        wide_callback_scratch_plane_count = 4U;
                    }
                }
                // A narrow source slice of a wide signal also uses the
                // arbitrary-width callback, so include even one-word sources.
                const auto width = validated.register_widths[write.source];
                const auto words = static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(width) + 63U) / 64U);
                wide_callback_scratch_word_stride = std::max(
                    wide_callback_scratch_word_stride, words);
                if (register_kind(write.source) == ValueKind::logic9) {
                    wide_callback_scratch_plane_count = 4U;
                }
            }
        }, operation);
        const auto include_container_callback = [&](
            const RegisterId index_register,
            const RegisterId value_register,
            const std::uint32_t container_id,
            const bool allow_index64) {
            if (container_id >= container_register_types.size()
                || index_register >= validated.register_widths.size()
                || value_register >= validated.register_widths.size()) {
                return;
            }
            const auto& type = container_register_types[container_id];
            const auto value_width
                = validated.register_widths[value_register];
            const bool packed_fast_path = !type.associative
                && (type.element_kind == ContainerElementKind::Packed
                    || type.element_kind == ContainerElementKind::Scalar)
                && (validated.register_widths[index_register] == 32U
                    || (allow_index64
                        && validated.register_widths[index_register] == 64U))
                && register_kind(index_register) == ValueKind::logic4
                && register_kind(value_register) == ValueKind::logic4
                && value_width == type.element_width;
            if (packed_fast_path) {
                include_callback_width(type.element_width);
            }
        };
        if (const auto* const read
            = operation_get_if<ContainerRead>(&operation)) {
            if (!read->string_index) {
                include_container_callback(
                    read->index, read->destination, read->source, true);
            }
        }
        if (const auto* const write
            = operation_get_if<ContainerWrite>(&operation)) {
            if (!write->string_index) {
                include_container_callback(
                    write->index, write->source, write->target, false);
            }
        }
    }
    llvm::Value* wide_callback_scratch = nullptr;
    if (wide_callback_scratch_word_stride != 0U) {
        const auto scratch_words
            = static_cast<std::uint64_t>(wide_callback_scratch_word_stride)
            * wide_callback_scratch_plane_count;
        constexpr std::uint64_t max_wide_callback_scratch_bytes
            = UINT64_C(64) * 1024U;
        if (scratch_words
            > max_wide_callback_scratch_bytes / sizeof(std::uint64_t)) {
            throw LlvmJitUnsupportedError(
                "wide callback scratch exceeds the bounded stack budget");
        }
        wide_callback_scratch = builder.CreateAlloca(
            i64,
            llvm::ConstantInt::get(i64, scratch_words),
            "wide.callback.scratch");
        llvm::cast<llvm::AllocaInst>(wide_callback_scratch)->setAlignment(
            llvm::Align(8));
    }
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
        return canonicalize_logic9_value(
            builder,
            EncodedValue {
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
            });
    };
    const auto emit_result =
        [&](llvm::IRBuilder<>& result_builder, const std::uint32_t status,
            llvm::Value* instruction, llvm::Value* delay,
            const std::uint32_t frame_state, llvm::Value* next_pc) {
            result_builder.CreateStore(next_pc,
                result_builder.CreateStructGEP(frame_type, frame_argument, 5));
            result_builder.CreateStore(llvm::ConstantInt::get(i32, frame_state),
                result_builder.CreateStructGEP(frame_type, frame_argument, 6));
            result_builder.CreateStore(instruction,
                result_builder.CreateStructGEP(frame_type, frame_argument, 7));
            result_builder.CreateStore(llvm::ConstantInt::get(i32, status),
                result_builder.CreateStructGEP(result_type, result_argument, 2));
            result_builder.CreateStore(instruction,
                result_builder.CreateStructGEP(result_type, result_argument, 3));
            result_builder.CreateStore(delay,
                result_builder.CreateStructGEP(result_type, result_argument, 4));
            result_builder.CreateRet(llvm::ConstantInt::get(i32, status));
        };
    llvm::BasicBlock* error_exit = nullptr;
    llvm::PHINode* error_instruction = nullptr;
    llvm::PHINode* error_delay = nullptr;
    llvm::PHINode* error_next_pc = nullptr;
    std::vector<llvm::BasicBlock*> error_predecessors;
    const auto return_result =
        [&](const std::uint32_t status, const std::uint32_t instruction,
            const std::uint64_t delay, const std::uint32_t frame_state,
            const std::uint32_t next_pc) {
            if (status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2
                && frame_state == FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2) {
                // Keep per-site reason and PC metadata, but share the result
                // stores and return. Successful suspension/completion retains
                // its existing exit and frame-publication sequence.
                if (error_exit == nullptr) {
                    error_exit = llvm::BasicBlock::Create(
                        context, "runtime.error.exit", function);
                    llvm::IRBuilder<> error_builder(error_exit);
                    error_instruction = error_builder.CreatePHI(
                        i32, 0U, "error.instruction");
                    error_delay = error_builder.CreatePHI(
                        i64, 0U, "error.delay");
                    error_next_pc = error_builder.CreatePHI(
                        i32, 0U, "error.next.pc");
                    emit_result(error_builder, status, error_instruction,
                        error_delay, frame_state, error_next_pc);
                }
                auto* const predecessor = builder.GetInsertBlock();
                error_instruction->addIncoming(
                    llvm::ConstantInt::get(i32, instruction), predecessor);
                error_delay->addIncoming(
                    constant_i64(context, delay), predecessor);
                error_next_pc->addIncoming(
                    llvm::ConstantInt::get(i32, next_pc), predecessor);
                error_predecessors.push_back(predecessor);
                builder.CreateBr(error_exit);
                return;
            }
            emit_result(builder, status,
                llvm::ConstantInt::get(i32, instruction),
                constant_i64(context, delay), frame_state,
                llvm::ConstantInt::get(i32, next_pc));
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
    std::vector<std::optional<FusedAffineDynamicExtract>>
        fused_affine_dynamic_extracts(process.operations.size());
    if (!debug_instrumentation) {
        for (std::size_t start = 0; start < process.operations.size(); ++start) {
            const auto* initial
                = runtime::simir::operation_get_if<LoadConstant>(
                    &process.operations[start]);
            if (initial == nullptr || initial->value.width() < 128U
                || std::ranges::binary_search(bound_literal_sites,
                    static_cast<InstructionIndex>(start))) {
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
                    || extract == nullptr || insert == nullptr
                    || std::ranges::binary_search(bound_literal_sites,
                        static_cast<InstructionIndex>(cursor))
                    || std::ranges::binary_search(bound_literal_sites,
                        static_cast<InstructionIndex>(cursor + 1U))
                    || std::ranges::binary_search(bound_literal_sites,
                        static_cast<InstructionIndex>(cursor + 4U))) {
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
            const auto affine_register_kind = [&](const RegisterId id) {
                return register_value_kinds.empty()
                    ? ValueKind::logic4
                    : register_value_kinds[id];
            };
            if (affine_register_kind(source)
                != affine_register_kind(initial->destination)) {
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
                || std::ranges::binary_search(bound_literal_sites,
                    static_cast<InstructionIndex>(index - 1U))
                || constant->destination != select->source
                || select->width > 64U
                || constant->value.width()
                    != validated.register_widths[select->source]
                || !lowering_plan.operations[index - 1U]
                || !lowering_plan.operations[index]
                || elided_operations[index - 1U]) {
                continue;
            }
            const auto source_kind = register_value_kinds.empty()
                ? ValueKind::logic4
                : register_value_kinds[select->source];
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
    std::vector<bool> fused_container_object_single_use_reads(
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
                    const auto& type = container_register_types[
                        element_read->source];
                    const bool packed_fast_path
                        = !element_read->string_index && !type.associative
                        && (type.element_kind
                                == runtime::simir::ContainerElementKind::Packed
                            || type.element_kind
                                == runtime::simir::ContainerElementKind::Scalar)
                        && (registers[element_read->index].width == 32U
                            || registers[element_read->index].width == 64U)
                        && registers[element_read->index].kind
                            == runtime::simir::ValueKind::logic4
                        && registers[element_read->destination].kind
                            == runtime::simir::ValueKind::logic4
                        && registers[element_read->destination].width
                            == type.element_width;
                    if (packed_fast_path) {
                        elided_operations[index] = true;
                        fused_container_object_reads[candidate_index]
                            = static_cast<std::uint32_t>(distance);
                        fused_container_object_single_use_reads[
                            candidate_index]
                            = type.element_width <= 64U
                            && element_read->source
                                < effective_container_register_reference_counts.size()
                            && effective_container_register_reference_counts[
                                   element_read->source]
                                == 2U;
                    }
                    break;
                }
                // The packed read callback can evaluate the object read
                // later than its source instruction. Only literal register
                // loads are safe to cross before snapshot materialization.
                if (!fsim::runtime::simir::operation_holds<
                        runtime::simir::LoadConstant>(candidate)) {
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
        for (const auto& region : static_trigger_regions) {
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
                [&validated](const Operation& operation) {
                    return is_resume_boundary(
                        operation, validated.register_widths);
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
            llvm::ConstantInt::get(i32, FSIM_JIT_INVALID_INSTRUCTION_V2),
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
        FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2, FSIM_JIT_INVALID_INSTRUCTION_V2, 0,
        FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2, FSIM_JIT_INVALID_INSTRUCTION_V2);
ProcessLoweringContext lowering_context {
        module,
        symbol,
        process,
        signal_widths,
        signal_value_kinds,
        direct_read_signals,
        direct_update_signals,
        validated,
        bound_literal_sites,
        debug_instrumentation,
        require_direct_update_slots,
        require_direct_read_signals,
        mode,
        context,
        i32,
        i64,
        pointer,
        runtime_type,
        services_type,
        direct_update_slot_type,
        frame_type,
        function,
        builder,
        runtime_argument,
        services_argument,
        frame_argument,
        context_pointer,
        read_callback,
        write_callback,
        assert_callback,
        code_coverage_hit_counters,
        code_coverage_counter_values,
        code_coverage_hit_count,
        code_coverage_counter_count,
        record_code_coverage_counter,
        record_code_coverage_counter_type,
        sample_coverage_callback,
        class_property_operation_callback,
        event_triggered_callback,
        native_service_callback_type,
        direct_update_slots,
        direct_update_active_words,
        static_trigger_mask,
        direct_signal_aval,
        direct_signal_bval,
        direct_signal_logic9_plane0,
        direct_signal_logic9_plane1,
        direct_signal_logic9_plane2,
        direct_signal_logic9_plane3,
        direct_read_signal_map,
        direct_read_signal_count,
        direct_signal_count,
        direct_wide_signal_aval,
        direct_wide_signal_bval,
        direct_wide_signal_logic9_plane2,
        direct_wide_signal_logic9_plane3,
        direct_wide_signal_offsets,
        direct_wide_signal_offset_count,
        direct_wide_word_count,
        write_update_callback,
        write_after_callback,
        write_blocking_slice_callback,
        write_update_slice_callback,
        write_after_slice_callback,
        force_signal_slice_callback,
        force_signal_slice_logic9_callback,
        release_signal_slice_callback,
        force_driver_signal_slice_callback,
        force_driver_signal_slice_logic9_callback,
        release_driver_signal_slice_callback,
        runtime_flags,
        signal_event_callback,
        signal_last_value_callback,
        signal_last_event_callback,
        signal_active_callback,
        signal_last_active_callback,
        signal_driving_callback,
        signal_driving_value_callback,
        signal_driving_value_logic9_callback,
        read_simulation_time_callback,
        vital_timing_check_callback,
        vital_delay_callback,
        output_callback,
        postponed_output_callback,
        report_callback,
        formatted_output_callback,
        time_output_callback,
        monitor_install_callback,
        monitor_control_callback,
        random_value_callback,
        write_inertial_callback,
        write_inertial_slice_callback,
        exact_signal_callback,
        read_signal_packed_callback,
        write_signal_packed_callback,
        write_projected_signal_packed_callback,
        read_signal_dynamic_part_callback,
        write_projected_callback,
        write_projected_slice_callback,
        write_projected_waveform_callback,
        write_projected_waveform_slice_callback,
        read_logic9_callback,
        write_logic9_callback,
        write_update_logic9_callback,
        write_after_logic9_callback,
        write_blocking_slice_logic9_callback,
        write_update_slice_logic9_callback,
        write_after_slice_logic9_callback,
        signal_last_value_logic9_callback,
        write_inertial_logic9_callback,
        write_inertial_slice_logic9_callback,
        write_projected_logic9_callback,
        write_projected_slice_logic9_callback,
        write_projected_waveform_logic9_callback,
        write_projected_waveform_slice_logic9_callback,
        write_formatted_logic9_callback,
        read_type,
        write_type,
        write_update_type,
        assert_type,
        write_after_type,
        write_slice_type,
        write_update_slice_type,
        write_after_slice_type,
        release_slice_type,
        write_inertial_type,
        write_inertial_slice_type,
        exact_signal_type,
        read_signal_packed_type,
        read_signal_dynamic_part_type,
        write_signal_packed_type,
        write_projected_signal_packed_type,
        write_projected_type,
        write_projected_slice_type,
        projected_element_type,
        write_projected_waveform_type,
        write_projected_waveform_slice_type,
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
        output_type,
        report_type,
        formatted_output_type,
        time_output_type,
        random_value_type,
        read_logic9_type,
        write_logic9_type,
        write_update_logic9_type,
        write_after_logic9_type,
        write_slice_logic9_type,
        write_update_slice_logic9_type,
        write_after_slice_logic9_type,
        write_inertial_logic9_type,
        write_inertial_slice_logic9_type,
        write_projected_logic9_type,
        write_projected_slice_logic9_type,
        logic9_projected_element_type,
        write_projected_waveform_logic9_type,
        write_projected_waveform_slice_logic9_type,
        formatted_output_logic9_type,
        native_callables,
        lowering_plan,
        register_aval,
        register_bval,
        register_initialized,
        i8,
        registers,
        frame_registers,
        read_bval_slot,
        logic9_word_slot,
        container_result_aval_slot,
        container_result_bval_slot,
        wide_callback_scratch,
        wide_callback_scratch_word_stride,
        store_logic9_word,
        load_logic9_word,
        return_result,
        elided_operations,
        fused_affine_dynamic_extracts,
        constant_part_select_sources,
        dynamic_part_signal_sources,
        fused_container_object_reads,
        fused_container_object_single_use_reads,
        instruction_blocks,
        instruction_regions,
        static_trigger_region_entries,
        ssa_callable_returns,
        invalid_pc,
        constant_plane_forwarding,
        suppressed_false_guards,
        signal_callback_ids_are_actual,
        signal_callback_operands,
        signal_callback_operand_word_base,
        frame_register_aval
    };
    lower_process_operations(lowering_context);
    lower_masked_member_gates(*function, runtime_type,
        instruction_blocks, masked_member_gates);
    // Block collection for outlining is complete. Keep cold error paths
    // behind normal instruction bodies without changing their CFG edges.
    for (auto* block : error_predecessors) {
        if (block != &function->back()) {
            block->moveAfter(&function->back());
        }
    }
    if (error_exit != nullptr && error_exit != &function->back()) {
        error_exit->moveAfter(&function->back());
    }
    if (std::getenv("FSIM_PROFILE_JIT_REGISTERS") != nullptr
        && allow_unbound_constant_planes) {
        std::size_t eligible { };
        std::size_t published { };
        std::size_t forwarded { };
        for (std::size_t id = 0; id < constant_plane_forwarding.size(); ++id) {
            eligible += registers[id].constant_planes != nullptr;
            published += constant_plane_forwarding[id].published_planes;
            forwarded += constant_plane_forwarding[id].forwarded_loads;
        }
        llvm::errs() << "fsim-profile: jit-constant-planes process_id="
                     << process.id
                     << " eligible=" << eligible
                     << " published_planes=" << published
                     << " forwarded_loads=" << forwarded
                     << " suppressed_false_guards="
                     << suppressed_false_guards << '\n';
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

    sink_immutable_service_loads(*function);
    if (!prepared_output_entry_symbol.empty()) {
        emit_region_prepared_output_entry(module, *function,
            prepared_output_entry_symbol, prepared_output_bindings);
        if (!prepared_output_successor_entry_symbol.empty()) {
            emit_region_prepared_output_successor_entry(module,
                prepared_output_entry_symbol,
                prepared_output_successor_entry_symbol,
                prepared_output_bindings);
        }
    } else if (!prepared_output_successor_entry_symbol.empty()) {
        throw LlvmJitError(
            "prepared-output successor has no V1 output entry");
    } else if (!prepared_output_bindings.empty()) {
        throw LlvmJitError(
            "prepared-output bindings have no private entry symbol");
    }
    if (!direct_ready_entry_symbol.empty()) {
        emit_region_direct_ready_output_entry(module,
            prepared_output_entry_symbol,
            direct_ready_entry_symbol, direct_ready_bindings);
        if (!direct_ready_successor_entry_symbol.empty()) {
            if (prepared_output_successor_entry_symbol.empty()) {
                throw LlvmJitError(
                    "direct-ready successor has no prepared-output successor target");
            }
            emit_region_direct_ready_successor_entry(module,
                direct_ready_entry_symbol,
                prepared_output_successor_entry_symbol,
                direct_ready_successor_entry_symbol,
                prepared_output_bindings);
        }
    } else if (!direct_ready_bindings.empty()) {
        throw LlvmJitError(
            "direct-ready bindings have no private entry symbol");
    } else if (!direct_ready_successor_entry_symbol.empty()) {
        throw LlvmJitError(
            "direct-ready successor has no direct-ready entry symbol");
    }
    apply_jit_module_no_unwind_contract(module);
}
} // namespace fsim::compiler::llvm_detail

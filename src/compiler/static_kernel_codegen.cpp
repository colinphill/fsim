// SPDX-License-Identifier: Apache-2.0
//
// LLVM code generation for engine v4 static kernel templates (see
// src/runtime/simir_static_kernel_native.hpp). Each template becomes one
// function `i32 (ptr frame, ptr noalias registers)`. Registers are word pairs
// in the caller-owned register file; four-state bitwise, comparison, simple
// arithmetic, selection and slot accesses are emitted inline with the exact
// formulas of simir_kernel_word_ops.hpp. Shifts, dynamic selections, division
// and power, memories, host accesses and deferred stores call the runtime
// helpers, which share the interpreter tier's implementation.
#include "fsim/compiler/static_kernel_codegen.hpp"
#include "fsim/support/native_filesystem.hpp"

#include "../runtime/simir_static_kernel_native.hpp"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <chrono>
#include <fstream>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::compiler {

namespace {

using runtime::simir::StaticKernelCodegen;
using runtime::simir::StaticKernelNativeEntry;
using runtime::simir::StaticKernelNativeFrame;
using runtime::simir::StaticKernelNativeHelpers;
using runtime::simir::StaticKernelTemplate;
using runtime::simir::BinaryOperator;
using runtime::simir::LogicalBinaryOperator;
using runtime::simir::ReductionOperator;
namespace detail = runtime::simir::static_kernel_detail;
using detail::KInst;
using detail::KOp;

static_assert(offsetof(StaticKernelNativeFrame, arena) == 0U);
static_assert(offsetof(StaticKernelNativeFrame, bindings) == 8U);
static_assert(offsetof(StaticKernelNativeFrame, member) == 32U);
static_assert(offsetof(StaticKernelNativeFrame, position) == 36U);
static_assert(offsetof(StaticKernelNativeFrame, status) == 40U);
static_assert(offsetof(StaticKernelNativeFrame, reserved) == 44U);
static_assert(offsetof(StaticKernelNativeFrame, writes) == 56U);
static_assert(offsetof(StaticKernelNativeFrame, containers) == 64U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, slot) == 4U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, count) == 8U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, left) == 16U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, low) == 24U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, high) == 32U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, unknown_a) == 40U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, unknown_b) == 48U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, element_width)
    == 56U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, flags) == 60U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, elements) == 64U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, packed_offset)
    == 72U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, packed_words)
    == 76U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, packed_width)
    == 80U);
static_assert(offsetof(runtime::simir::StaticKernelWriteQueue, count) == 8U);
static_assert(offsetof(runtime::simir::StaticKernelWriteQueue, capacity) == 12U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, offset) == 4U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, width) == 8U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, member) == 12U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, a) == 16U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, b) == 24U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, unknown) == 32U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, kind) == 40U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, instruction) == 44U);

constexpr std::uint32_t silent_bit = std::uint32_t { 1 } << 31U;

std::once_flag native_target_once;

void initialize_native_target()
{
    std::call_once(native_target_once, [] {
        if (llvm::InitializeNativeTarget()
            || llvm::InitializeNativeTargetAsmPrinter()) {
            throw std::runtime_error(
                "static kernel codegen cannot initialize the native target");
        }
    });
}

[[nodiscard]] std::string error_text(llvm::Error error)
{
    std::string message;
    llvm::raw_string_ostream stream(message);
    llvm::logAllUnhandledErrors(std::move(error), stream);
    return message;
}

[[nodiscard]] std::uint64_t mask_of(const std::uint32_t width)
{
    return width >= 64U ? ~std::uint64_t { 0 }
                        : (std::uint64_t { 1 } << width) - 1U;
}

struct Pair {
    llvm::Value* a { };
    llvm::Value* b { };
};

class FunctionEmitter {
public:
    FunctionEmitter(llvm::Module& module, llvm::Function& function,
        const detail::CompiledBody& body, const StaticKernelNativeHelpers& helpers,
        const bool local_registers)
        : context_(module.getContext())
        , function_(function)
        , body_(body)
        , helpers_(helpers)
        , builder_(context_)
        , i32_(llvm::Type::getInt32Ty(context_))
        , i64_(llvm::Type::getInt64Ty(context_))
        , i8_(llvm::Type::getInt8Ty(context_))
        , ptr_(llvm::PointerType::getUnqual(context_))
        , local_registers_(local_registers)
    {
    }

    void emit()
    {
        const auto size = static_cast<std::uint32_t>(body_.code.size());
        frame_ = function_.getArg(0);
        registers_ = function_.getArg(1);
        auto* entry = llvm::BasicBlock::Create(context_, "entry", &function_);
        return_block_ = llvm::BasicBlock::Create(context_, "done", &function_);
        failure_block_ = llvm::BasicBlock::Create(context_, "failed", &function_);
        // Leaders: branch targets and the instructions after jumps/branches.
        std::vector<std::uint8_t> leader(size + 1U, 0U);
        leader[0] = 1U;
        leader[std::min(body_.entry, size)] = 1U;
        for (const auto target : body_.return_targets) {
            leader[std::min(target, size)] = 1U;
        }
        for (std::uint32_t at = 0U; at < size; ++at) {
            const auto& inst = body_.code[at];
            if (inst.op == KOp::jump || inst.op == KOp::call) {
                leader[std::min(inst.d, size)] = 1U;
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::branch) {
                leader[std::min(inst.y, size)] = 1U;
                leader[std::min(inst.z, size)] = 1U;
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::ret || inst.op == KOp::suspend) {
                leader[at + 1U] = 1U;
            }
        }
        for (const auto resume : body_.resume_entries) {
            leader[std::min(resume, size)] = 1U;
        }
        blocks_.assign(size + 1U, nullptr);
        for (std::uint32_t at = 0U; at < size; ++at) {
            if (leader[at] != 0U) {
                blocks_[at] = llvm::BasicBlock::Create(
                    context_, "i" + std::to_string(at), &function_);
            }
        }
        blocks_[size] = return_block_;
        builder_.SetInsertPoint(entry);
        out_ = builder_.CreateAlloca(i64_, constant(3U), "out");
        plan_local_registers();
        for (std::size_t reg = 0U; reg < local_.size(); ++reg) {
            if (local_[reg] != nullptr) {
                local_[reg] = builder_.CreateAlloca(
                    llvm::ArrayType::get(i64_, 2U), nullptr, "r");
            }
        }
        arena_ = builder_.CreateLoad(ptr_, frame_field(0U), "arena");
        bindings_ = builder_.CreateLoad(ptr_, frame_field(8U), "bindings");
        if (body_.resume_entries.empty()) {
            builder_.CreateBr(blocks_[std::min(body_.entry, size)]);
        } else {
            // A behavioral thread resumes at the instruction in `reserved`.
            auto* start = builder_.CreateLoad(i32_, frame_field(44U), "resume");
            auto* targets = builder_.CreateSwitch(start, failure_block_,
                static_cast<unsigned>(body_.resume_entries.size()));
            for (const auto resume : body_.resume_entries) {
                targets->addCase(llvm::ConstantInt::get(
                                     llvm::cast<llvm::IntegerType>(i32_), resume),
                    blocks_[std::min(resume, size)]);
            }
        }
        for (std::uint32_t at = 0U; at < size; ++at) {
            if (blocks_[at] != nullptr) {
                if (builder_.GetInsertBlock() != nullptr
                    && builder_.GetInsertBlock()->getTerminator() == nullptr) {
                    builder_.CreateBr(blocks_[at]);
                }
                builder_.SetInsertPoint(blocks_[at]);
                cache_.clear();
            }
            last_unknown_ = nullptr;
            insert_unknown_ = nullptr;
            emit_unknown_checks(at);
            const auto result_unknown = unknown_before(at);
            emit_instruction(at);
            unknown_after(at, result_unknown);
        }
        if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
            builder_.CreateBr(return_block_);
        }
        builder_.SetInsertPoint(return_block_);
        builder_.CreateRet(llvm::ConstantInt::get(i32_, 0U));
        builder_.SetInsertPoint(failure_block_);
        builder_.CreateRet(llvm::ConstantInt::get(i32_, 1U));
    }

private:
    [[nodiscard]] llvm::Value* constant(const std::uint64_t value)
    {
        return llvm::ConstantInt::get(i64_, value);
    }

    [[nodiscard]] llvm::Value* frame_field(const std::uint32_t offset)
    {
        return builder_.CreateConstInBoundsGEP1_64(i8_, frame_, offset);
    }

    /// The two words of register `reg`: in the frame's register file, or in
    /// a local slot (promoted to SSA values) when no run reads it before
    /// writing it.
    [[nodiscard]] std::pair<llvm::Value*, llvm::Value*> register_address(
        const std::uint32_t reg)
    {
        if (reg < local_.size() && local_[reg] != nullptr) {
            return { builder_.CreateConstInBoundsGEP1_64(i64_, local_[reg], 0U),
                builder_.CreateConstInBoundsGEP1_64(i64_, local_[reg], 1U) };
        }
        return { builder_.CreateConstInBoundsGEP1_64(i64_, registers_,
                     static_cast<std::uint64_t>(reg) * 2U),
            builder_.CreateConstInBoundsGEP1_64(i64_, registers_,
                static_cast<std::uint64_t>(reg) * 2U + 1U) };
    }

    /// Registers that every run writes before reading carry nothing from one
    /// run to the next. In bodies that never resume from the register file
    /// (no VHDL deoptimization, no behavioral suspension, no generic
    /// instruction reading it) they become local slots, so their values stay
    /// in machine registers instead of being stored to the frame.
    void plan_local_registers()
    {
        local_.clear();
        static const bool disabled = [] {
            const char* text = std::getenv("FSIM_STATIC_KERNEL_LOCAL_REGISTERS");
            return text != nullptr && std::string_view { text } == "0";
        }();
        if (!local_registers_ || disabled || !body_.resume_entries.empty()
            || body_.shadow_base != 0U || body_.call_stack_base != 0U) {
            return;
        }
        const auto size = static_cast<std::uint32_t>(body_.code.size());
        const auto count = static_cast<std::uint32_t>(body_.registers.size());
        const auto words = (static_cast<std::size_t>(count) + 63U) / 64U;
        using Bits = std::vector<std::uint64_t>;
        const auto test = [](const Bits& bits, const std::uint32_t reg) {
            return ((bits[reg / 64U] >> (reg % 64U)) & 1U) != 0U;
        };
        const auto set = [](Bits& bits, const std::uint32_t reg) {
            bits[reg / 64U] |= std::uint64_t { 1 } << (reg % 64U);
        };
        // Register operands of each instruction; false for an instruction
        // that may reach the register file another way.
        std::vector<std::uint32_t> reads;
        reads.reserve(8U);
        const auto operands = [&](const KInst& inst, bool& writes) {
            reads.clear();
            writes = false;
            switch (inst.op) {
            case KOp::nop:
            case KOp::mem_bind:
            case KOp::mark:
            case KOp::jump:
                return true;
            case KOp::constant:
            case KOp::load_slot:
            case KOp::load_host:
            case KOp::load_field:
                writes = true;
                return true;
            case KOp::copy:
            case KOp::reduce:
            case KOp::unary_not:
            case KOp::logical_not:
            case KOp::two_state:
            case KOp::extract:
                reads = { inst.x };
                writes = true;
                return true;
            case KOp::binary:
            case KOp::logical_binary:
            case KOp::shift:
            case KOp::insert:
                reads = { inst.x, inst.y };
                writes = true;
                return true;
            case KOp::concat:
                for (std::uint32_t index = 0U; index < inst.x; ++index) {
                    reads.push_back(body_.concat[inst.aux + index].reg);
                }
                writes = true;
                return true;
            case KOp::conditional:
            case KOp::dynamic_insert:
            case KOp::dynamic_part_insert:
                reads = { inst.x, inst.y, inst.z };
                writes = true;
                return true;
            case KOp::dynamic_extract:
            case KOp::dynamic_part_select:
                reads = { inst.x, inst.y };
                writes = true;
                return true;
            case KOp::mem_read:
                reads = { inst.y };
                writes = true;
                return true;
            case KOp::store_slot:
            case KOp::store_slot_nba:
            case KOp::store_host:
            case KOp::branch:
                reads = { inst.x };
                return true;
            case KOp::store_slot_dynamic:
            case KOp::store_slot_part:
            case KOp::mem_write:
                reads = { inst.x, inst.y, inst.z };
                return true;
            case KOp::wide_move:
                reads = { inst.y };
                return true;
            default:
                return false;
            }
        };
        // Basic blocks and their successors.
        const auto entry = std::min(body_.entry, size);
        std::vector<std::uint8_t> leader(size + 1U, 0U);
        leader[entry] = 1U;
        for (std::uint32_t at = 0U; at < size; ++at) {
            const auto& inst = body_.code[at];
            bool writes = false;
            if (!operands(inst, writes)) {
                return;
            }
            if (inst.op == KOp::jump) {
                leader[std::min(inst.d, size)] = 1U;
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::branch) {
                leader[std::min(inst.y, size)] = 1U;
                leader[std::min(inst.z, size)] = 1U;
                leader[at + 1U] = 1U;
            }
        }
        std::vector<std::uint32_t> block_begin;
        std::vector<std::uint32_t> block_of(size + 1U, 0U);
        for (std::uint32_t at = 0U; at < size; ++at) {
            if (leader[at] != 0U || at == 0U) {
                block_begin.push_back(at);
            }
            block_of[at] = static_cast<std::uint32_t>(block_begin.size() - 1U);
        }
        const auto blocks = static_cast<std::uint32_t>(block_begin.size());
        const auto exit_block = blocks;
        block_of[size] = exit_block;
        const auto block_end = [&](const std::uint32_t block) {
            return block + 1U < blocks ? block_begin[block + 1U] : size;
        };
        std::vector<std::vector<std::uint32_t>> successors(blocks);
        for (std::uint32_t block = 0U; block < blocks; ++block) {
            const auto last = block_end(block) - 1U;
            const auto& inst = body_.code[last];
            if (inst.op == KOp::jump) {
                successors[block] = { block_of[std::min(inst.d, size)] };
            } else if (inst.op == KOp::branch) {
                successors[block] = { block_of[std::min(inst.y, size)],
                    block_of[std::min(inst.z, size)] };
            } else {
                successors[block] = { block_of[last + 1U] };
            }
        }
        // Must-defined registers at block entry: the intersection over
        // predecessors, iterated to a fixed point (loops included).
        std::vector<Bits> in(blocks, Bits(words, ~std::uint64_t { 0 }));
        std::vector<std::uint8_t> reached(blocks, 0U);
        const auto entry_block = block_of[entry];
        if (entry_block == exit_block) {
            return;
        }
        std::ranges::fill(in[entry_block], 0U);
        reached[entry_block] = 1U;
        Bits state(words, 0U);
        for (bool changed = true; changed;) {
            changed = false;
            for (std::uint32_t block = 0U; block < blocks; ++block) {
                if (reached[block] == 0U) {
                    continue;
                }
                state = in[block];
                for (auto at = block_begin[block]; at < block_end(block); ++at) {
                    const auto& inst = body_.code[at];
                    bool writes = false;
                    (void)operands(inst, writes);
                    if (writes && inst.d < count) {
                        set(state, inst.d);
                    }
                }
                for (const auto successor : successors[block]) {
                    // Each run starts at the entry with nothing defined.
                    if (successor == exit_block || successor == entry_block) {
                        continue;
                    }
                    auto& target = in[successor];
                    if (reached[successor] == 0U) {
                        reached[successor] = 1U;
                        target = state;
                        changed = true;
                        continue;
                    }
                    for (std::size_t word = 0U; word < words; ++word) {
                        const auto merged = target[word] & state[word];
                        if (merged != target[word]) {
                            target[word] = merged;
                            changed = true;
                        }
                    }
                }
            }
        }
        Bits live_in(words, 0U);
        Bits written(words, 0U);
        for (std::uint32_t block = 0U; block < blocks; ++block) {
            if (reached[block] == 0U) {
                continue;
            }
            state = in[block];
            for (auto at = block_begin[block]; at < block_end(block); ++at) {
                const auto& inst = body_.code[at];
                bool writes = false;
                (void)operands(inst, writes);
                for (const auto reg : reads) {
                    if (reg < count && !test(state, reg)) {
                        set(live_in, reg);
                    }
                }
                if (writes && inst.d < count) {
                    set(state, inst.d);
                    set(written, inst.d);
                }
            }
        }
        local_.assign(count, nullptr);
        for (std::uint32_t reg = 0U; reg < count; ++reg) {
            if (test(written, reg) && !test(live_in, reg)
                && reg < body_.register_widths.size()
                && body_.register_widths[reg] <= 64U) {
                // Placeholder; emit() creates the slot.
                local_[reg] = registers_;
            }
        }
    }

    [[nodiscard]] Pair load_register(const std::uint32_t reg)
    {
        if (const auto found = cache_.find(reg); found != cache_.end()) {
            return found->second;
        }
        const auto [base, high] = register_address(reg);
        Pair value { builder_.CreateLoad(i64_, base),
            builder_.CreateLoad(i64_, high) };
        cache_[reg] = value;
        return value;
    }

    void store_register(const std::uint32_t reg, const Pair value)
    {
        const auto [base, high] = register_address(reg);
        builder_.CreateStore(value.a, base);
        builder_.CreateStore(value.b, high);
        cache_[reg] = value;
    }

    [[nodiscard]] llvm::Value* binding(const std::uint32_t index,
        const std::uint32_t word)
    {
        auto* address = builder_.CreateConstInBoundsGEP1_64(i32_, bindings_,
            static_cast<std::uint64_t>(index) * 2U + word);
        return builder_.CreateLoad(i32_, address);
    }

    [[nodiscard]] Pair scalar_bool(llvm::Value* condition)
    {
        return { builder_.CreateZExt(condition, i64_), constant(0U) };
    }

    [[nodiscard]] Pair select(llvm::Value* condition, const Pair left,
        const Pair right)
    {
        return { builder_.CreateSelect(condition, left.a, right.a),
            builder_.CreateSelect(condition, left.b, right.b) };
    }

    [[nodiscard]] Pair x_value(const std::uint32_t width)
    {
        return { constant(mask_of(width)), constant(mask_of(width)) };
    }

    [[nodiscard]] llvm::Value* nonzero(llvm::Value* value)
    {
        return builder_.CreateICmpNE(value, constant(0U));
    }

    [[nodiscard]] llvm::Value* bit_not(llvm::Value* value)
    {
        return builder_.CreateXor(value, constant(~std::uint64_t { 0 }));
    }

    /// truth_value(): returns (is_one, is_unknown) i1 values.
    [[nodiscard]] std::pair<llvm::Value*, llvm::Value*> truth(const Pair value)
    {
        auto* one = nonzero(builder_.CreateAnd(value.a, bit_not(value.b)));
        auto* unknown = builder_.CreateAnd(builder_.CreateNot(one),
            nonzero(value.b));
        return { one, unknown };
    }

    [[nodiscard]] Pair from_truth(llvm::Value* one, llvm::Value* unknown)
    {
        // one -> (1,0); unknown -> (1,1); else (0,0).
        auto* a = builder_.CreateZExt(builder_.CreateOr(one, unknown), i64_);
        auto* b = builder_.CreateZExt(unknown, i64_);
        return { a, b };
    }

    [[nodiscard]] llvm::Value* sign_extend(llvm::Value* value,
        const std::uint32_t width)
    {
        if (width >= 64U) {
            return value;
        }
        auto* shift = constant(64U - width);
        return builder_.CreateAShr(builder_.CreateShl(value, shift), shift);
    }

    [[nodiscard]] Pair emit_binary(const KInst& inst, const Pair l, const Pair r)
    {
        const auto width = inst.width;
        auto* m = constant(mask_of(width));
        auto* unknown = builder_.CreateAnd(builder_.CreateOr(l.b, r.b), m);
        auto* has_unknown = nonzero(unknown);
        const auto operation = static_cast<BinaryOperator>(inst.sub);
        switch (operation) {
        case BinaryOperator::bit_and: {
            auto* zero = builder_.CreateOr(
                builder_.CreateAnd(bit_not(l.a), bit_not(l.b)),
                builder_.CreateAnd(bit_not(r.a), bit_not(r.b)));
            auto* one = builder_.CreateAnd(
                builder_.CreateAnd(l.a, bit_not(l.b)),
                builder_.CreateAnd(r.a, bit_not(r.b)));
            auto* x = builder_.CreateAnd(bit_not(builder_.CreateOr(zero, one)), m);
            return { builder_.CreateAnd(builder_.CreateOr(one, x), m), x };
        }
        case BinaryOperator::bit_or: {
            auto* one = builder_.CreateOr(builder_.CreateAnd(l.a, bit_not(l.b)),
                builder_.CreateAnd(r.a, bit_not(r.b)));
            auto* zero = builder_.CreateAnd(
                builder_.CreateAnd(bit_not(l.a), bit_not(l.b)),
                builder_.CreateAnd(bit_not(r.a), bit_not(r.b)));
            auto* x = builder_.CreateAnd(bit_not(builder_.CreateOr(zero, one)), m);
            return { builder_.CreateAnd(builder_.CreateOr(one, x), m), x };
        }
        case BinaryOperator::bit_xor:
            return { builder_.CreateAnd(
                         builder_.CreateOr(builder_.CreateXor(l.a, r.a), unknown),
                         m),
                unknown };
        case BinaryOperator::equal:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpEQ(l.a, r.a)));
        case BinaryOperator::case_equal:
            return scalar_bool(builder_.CreateAnd(builder_.CreateICmpEQ(l.a, r.a),
                builder_.CreateICmpEQ(l.b, r.b)));
        case BinaryOperator::casez_equal: {
            auto* z = builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateAnd(bit_not(l.a), l.b),
                    builder_.CreateAnd(bit_not(r.a), r.b)),
                m);
            auto* differ = builder_.CreateAnd(
                builder_.CreateAnd(builder_.CreateOr(builder_.CreateXor(l.a, r.a),
                                       builder_.CreateXor(l.b, r.b)),
                    m),
                bit_not(z));
            return scalar_bool(builder_.CreateICmpEQ(differ, constant(0U)));
        }
        case BinaryOperator::casex_equal: {
            auto* differ = builder_.CreateAnd(
                builder_.CreateAnd(builder_.CreateOr(builder_.CreateXor(l.a, r.a),
                                       builder_.CreateXor(l.b, r.b)),
                    m),
                bit_not(unknown));
            return scalar_bool(builder_.CreateICmpEQ(differ, constant(0U)));
        }
        case BinaryOperator::wildcard_equal: {
            auto* care = builder_.CreateAnd(bit_not(r.b), m);
            auto* left_unknown = nonzero(builder_.CreateAnd(l.b, care));
            auto* equal = builder_.CreateICmpEQ(
                builder_.CreateAnd(builder_.CreateXor(l.a, r.a), care),
                constant(0U));
            return select(left_unknown, x_value(1U), scalar_bool(equal));
        }
        case BinaryOperator::not_equal:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpNE(l.a, r.a)));
        case BinaryOperator::less_unsigned:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpULT(l.a, r.a)));
        case BinaryOperator::less_equal_unsigned:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpULE(l.a, r.a)));
        case BinaryOperator::greater_unsigned:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpUGT(l.a, r.a)));
        case BinaryOperator::greater_equal_unsigned:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpUGE(l.a, r.a)));
        case BinaryOperator::less_signed:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpSLT(sign_extend(l.a, width),
                    sign_extend(r.a, width))));
        case BinaryOperator::less_equal_signed:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpSLE(sign_extend(l.a, width),
                    sign_extend(r.a, width))));
        case BinaryOperator::greater_signed:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpSGT(sign_extend(l.a, width),
                    sign_extend(r.a, width))));
        case BinaryOperator::greater_equal_signed:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpSGE(sign_extend(l.a, width),
                    sign_extend(r.a, width))));
        case BinaryOperator::add_unsigned:
        case BinaryOperator::add_signed:
            return select(has_unknown, x_value(width),
                { builder_.CreateAnd(builder_.CreateAdd(l.a, r.a), m),
                    constant(0U) });
        case BinaryOperator::subtract_unsigned:
        case BinaryOperator::subtract_signed:
            return select(has_unknown, x_value(width),
                { builder_.CreateAnd(builder_.CreateSub(l.a, r.a), m),
                    constant(0U) });
        case BinaryOperator::multiply_unsigned:
        case BinaryOperator::multiply_signed:
            return select(has_unknown, x_value(width),
                { builder_.CreateAnd(builder_.CreateMul(l.a, r.a), m),
                    constant(0U) });
        default: {
            // Pure: no frame, no failure status, never 'U'.
            auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { i64_, i64_, i64_, i64_, i64_, ptr_ }, false);
            auto* callee = builder_.CreateIntToPtr(
                constant(reinterpret_cast<std::uintptr_t>(helpers_.binary)), ptr_);
            builder_.CreateCall(type, callee,
                { constant(static_cast<std::uint64_t>(inst.sub)
                      | (static_cast<std::uint64_t>(width) << 8U)),
                    l.a, l.b, r.a, r.b, out_ });
            last_unknown_ = constant(0U);
            return { builder_.CreateLoad(i64_, out_),
                builder_.CreateLoad(i64_,
                    builder_.CreateConstInBoundsGEP1_64(i64_, out_, 1U)) };
        }
        }
    }

    [[nodiscard]] Pair emit_reduce(const KInst& inst, const Pair value)
    {
        auto* m = constant(mask_of(inst.width));
        auto* ones = builder_.CreateAnd(builder_.CreateAnd(value.a, bit_not(value.b)), m);
        auto* zeros = builder_.CreateAnd(
            builder_.CreateAnd(bit_not(value.a), bit_not(value.b)), m);
        auto* unknown = nonzero(builder_.CreateAnd(value.b, m));
        auto* count = builder_.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, ones);
        switch (static_cast<ReductionOperator>(inst.sub)) {
        case ReductionOperator::one_hot:
            return scalar_bool(builder_.CreateICmpEQ(count, constant(1U)));
        case ReductionOperator::one_hot_or_zero:
            return scalar_bool(builder_.CreateICmpULE(count, constant(1U)));
        case ReductionOperator::bit_and:
            return select(nonzero(zeros), Pair { constant(0U), constant(0U) },
                select(unknown, x_value(1U), Pair { constant(1U), constant(0U) }));
        case ReductionOperator::bit_or:
            return select(nonzero(ones), Pair { constant(1U), constant(0U) },
                select(unknown, x_value(1U), Pair { constant(0U), constant(0U) }));
        case ReductionOperator::bit_xor:
            return select(unknown, x_value(1U),
                Pair { builder_.CreateAnd(count, constant(1U)), constant(0U) });
        }
        return x_value(1U);
    }

    [[nodiscard]] Pair call_helper(void* helper, const std::uint32_t at,
        const Pair x, const Pair y, const Pair z)
    {
        auto* out = out_;
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i64_, i64_, i64_, i64_, i64_, i64_, ptr_ }, false);
        auto* callee = builder_.CreateIntToPtr(
            constant(reinterpret_cast<std::uintptr_t>(helper)), ptr_);
        builder_.CreateCall(type, callee,
            { frame_, llvm::ConstantInt::get(i32_, at), x.a, x.b, y.a, y.b, z.a,
                z.b, out });
        check_status();
        last_unknown_ = builder_.CreateLoad(i64_,
            builder_.CreateConstInBoundsGEP1_64(i64_, out, 2U));
        return { builder_.CreateLoad(i64_, out),
            builder_.CreateLoad(i64_,
                builder_.CreateConstInBoundsGEP1_64(i64_, out, 1U)) };
    }

    [[nodiscard]] Pair call_evaluate(const std::uint32_t at, const Pair x,
        const Pair y, const Pair z)
    {
        return call_helper(reinterpret_cast<void*>(helpers_.evaluate), at, x, y,
            z);
    }

    void call_effect(const std::uint32_t at, const Pair x, const Pair y,
        const Pair z)
    {
        (void)call_helper(reinterpret_cast<void*>(helpers_.effect), at, x, y, z);
    }

    void check_status()
    {
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        auto* failed = builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U));
        auto* next = llvm::BasicBlock::Create(context_, "ok", &function_);
        builder_.CreateCondBr(failed, failure_block_, next);
        builder_.SetInsertPoint(next);
        // Helpers may write slots; slot loads must not be reused across them.
    }

    [[nodiscard]] Pair zero_pair()
    {
        return { constant(0U), constant(0U) };
    }

    /// Fast path in a fresh block; the slow path calls the evaluate helper.
    /// Returns the merged result in the continuation block.
    [[nodiscard]] Pair with_fallback(llvm::Value* fast_ok, const Pair fast,
        const std::uint32_t at, const Pair x, const Pair y,
        llvm::Value* fast_unknown = nullptr)
    {
        auto* fast_block = builder_.GetInsertBlock();
        auto* slow = llvm::BasicBlock::Create(context_, "slow", &function_);
        auto* merge = llvm::BasicBlock::Create(context_, "merge", &function_);
        builder_.CreateCondBr(fast_ok, merge, slow);
        builder_.SetInsertPoint(slow);
        const auto slow_value = call_evaluate(at, x, y, zero_pair());
        auto* slow_end = builder_.GetInsertBlock();
        builder_.CreateBr(merge);
        builder_.SetInsertPoint(merge);
        auto* a = builder_.CreatePHI(i64_, 2U);
        a->addIncoming(fast.a, fast_block);
        a->addIncoming(slow_value.a, slow_end);
        auto* b = builder_.CreatePHI(i64_, 2U);
        b->addIncoming(fast.b, fast_block);
        b->addIncoming(slow_value.b, slow_end);
        // The fast path produces 'U' only where it says so.
        auto* unknown = builder_.CreatePHI(i64_, 2U);
        unknown->addIncoming(fast_unknown != nullptr ? fast_unknown : constant(0U),
            fast_block);
        unknown->addIncoming(last_unknown_, slow_end);
        last_unknown_ = unknown;
        return { a, b };
    }

    [[nodiscard]] Pair emit_shift(const KInst& inst, const std::uint32_t at,
        const Pair value, const Pair amount)
    {
        const auto operation = static_cast<runtime::simir::ShiftOperator>(inst.sub);
        const bool supported = (inst.flags & detail::flag_signed) == 0U
            && (operation == runtime::simir::ShiftOperator::logical_left
                || operation == runtime::simir::ShiftOperator::logical_right
                || operation == runtime::simir::ShiftOperator::arithmetic_right);
        if (!supported) {
            return call_evaluate(at, value, amount, zero_pair());
        }
        const auto width = inst.width;
        auto* m = constant(mask_of(width));
        auto* amount_mask = constant(mask_of(inst.offset));
        auto* unknown = nonzero(builder_.CreateAnd(amount.b, amount_mask));
        auto* magnitude = builder_.CreateAnd(amount.a, amount_mask);
        auto* full = builder_.CreateICmpUGE(magnitude, constant(width));
        auto* amt = builder_.CreateSelect(full, constant(0U), magnitude);
        Pair fill { constant(0U), constant(0U) };
        if (operation == runtime::simir::ShiftOperator::arithmetic_right) {
            const auto top = width - 1U;
            auto* sign_a = builder_.CreateAnd(
                builder_.CreateLShr(value.a, constant(top)), constant(1U));
            auto* sign_b = builder_.CreateAnd(
                builder_.CreateLShr(value.b, constant(top)), constant(1U));
            fill = { builder_.CreateAnd(builder_.CreateNeg(sign_a), m),
                builder_.CreateAnd(builder_.CreateNeg(sign_b), m) };
        }
        Pair shifted;
        if (operation == runtime::simir::ShiftOperator::logical_left) {
            shifted = { builder_.CreateAnd(builder_.CreateShl(value.a, amt), m),
                builder_.CreateAnd(builder_.CreateShl(value.b, amt), m) };
        } else {
            // keep = mask(width - amt); high = m & ~keep.
            auto* keep = builder_.CreateLShr(m, amt);
            auto* high = builder_.CreateAnd(m, bit_not(keep));
            shifted = {
                builder_.CreateOr(
                    builder_.CreateAnd(builder_.CreateLShr(value.a, amt), keep),
                    builder_.CreateAnd(fill.a, high)),
                builder_.CreateOr(
                    builder_.CreateAnd(builder_.CreateLShr(value.b, amt), keep),
                    builder_.CreateAnd(fill.b, high)) };
        }
        return select(unknown, x_value(width), select(full, fill, shifted));
    }

    [[nodiscard]] llvm::Value* signed_index(const Pair index)
    {
        return builder_.CreateSExt(builder_.CreateTrunc(index.a, i32_), i64_);
    }

    [[nodiscard]] Pair emit_dynamic_extract(const KInst& inst,
        const std::uint32_t at, const Pair source, const Pair index)
    {
        const auto& selection = body_.indices[inst.aux];
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(value, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(value, right),
            builder_.CreateSub(value, right), builder_.CreateSub(right, value));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        auto* fits = builder_.CreateICmpULT(offset, constant(inst.width));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
        auto* safe = builder_.CreateSelect(ok, offset, constant(0U));
        const Pair fast { builder_.CreateAnd(builder_.CreateLShr(source.a, safe), constant(1U)),
            builder_.CreateAnd(builder_.CreateLShr(source.b, safe), constant(1U)) };
        return with_fallback(ok, fast, at, source, index);
    }

    [[nodiscard]] Pair emit_dynamic_part_select(const KInst& inst,
        const std::uint32_t at, const Pair source, const Pair base)
    {
        const auto& part = body_.parts[inst.aux];
        const bool increasing = (inst.flags & detail::flag_increasing) != 0U;
        const bool descending = (inst.flags & detail::flag_descending) != 0U;
        const auto width = inst.width;
        const auto edge = static_cast<std::int64_t>(width - 1U);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(base);
        // selected(bit) = selected_right + bit (descending) or - bit.
        auto* selected_right = builder_.CreateAdd(value,
            constant(static_cast<std::uint64_t>(increasing ? (descending ? 0 : edge)
                                                           : (descending ? -edge : 0))));
        auto* selected_last = builder_.CreateAdd(selected_right,
            constant(static_cast<std::uint64_t>(descending ? edge : -edge)));
        auto* right = constant(static_cast<std::uint64_t>(part.right));
        auto* low = builder_.CreateSelect(
            builder_.CreateICmpSLT(selected_right, selected_last), selected_right,
            selected_last);
        auto* high = builder_.CreateSelect(
            builder_.CreateICmpSLT(selected_right, selected_last), selected_last,
            selected_right);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(low,
                constant(static_cast<std::uint64_t>(std::min(part.left, part.right)))),
            builder_.CreateICmpSLE(high,
                constant(static_cast<std::uint64_t>(std::max(part.left, part.right)))));
        // Offsets increase with the result bit when the selection moves away
        // from `right` as bits increase.
        llvm::Value* increasing_offsets = nullptr;
        if (descending) {
            increasing_offsets = builder_.CreateICmpSGE(low, right);
        } else {
            increasing_offsets = builder_.CreateICmpSLE(high, right);
        }
        auto* first = builder_.CreateSelect(builder_.CreateICmpSGE(selected_right, right),
            builder_.CreateSub(selected_right, right),
            builder_.CreateSub(right, selected_right));
        auto* offset = builder_.CreateAdd(first, constant(part.base_offset));
        auto* fits = builder_.CreateICmpULE(builder_.CreateAdd(offset, constant(width)),
            constant(inst.offset));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range),
            builder_.CreateAnd(increasing_offsets, fits));
        auto* safe = builder_.CreateSelect(ok, offset, constant(0U));
        auto* m = constant(mask_of(width));
        const Pair fast { builder_.CreateAnd(builder_.CreateLShr(source.a, safe), m),
            builder_.CreateAnd(builder_.CreateLShr(source.b, safe), m) };
        return with_fallback(ok, fast, at, source, base);
    }

    /// Appends a deferred VHDL assignment to the round's queue.
    // Kernel-owned memories (StaticKernelContainerInfo). Accesses to a
    // memory with storage 0 go through the helpers.
    struct MemoryElement {
        llvm::Value* info { };
        llvm::Value* storage { };
        /// The index is known and inside the declared range.
        llvm::Value* in_range { };
        /// The element ordinal (0 when out of range, so loads stay in
        /// bounds).
        llvm::Value* ordinal { };
    };

    [[nodiscard]] llvm::Value* info_field(llvm::Value* info, llvm::Type* type,
        const std::uint64_t offset)
    {
        return builder_.CreateLoad(type,
            builder_.CreateConstInBoundsGEP1_64(i8_, info, offset));
    }

    [[nodiscard]] MemoryElement memory_element(const std::uint32_t binding_index,
        const Pair index, const std::uint32_t index_width, const bool linear)
    {
        auto* table = builder_.CreateLoad(ptr_, frame_field(64U));
        auto* info = builder_.CreateInBoundsGEP(i8_, table,
            builder_.CreateMul(builder_.CreateZExt(binding(binding_index, 0U), i64_),
                constant(sizeof(runtime::simir::StaticKernelContainerInfo))));
        auto* storage = info_field(info, i32_, 0U);
        auto* count = info_field(info, i64_, 8U);
        auto* value = sign_extend(index.a, index_width);
        auto* known = builder_.CreateNot(nonzero(index.b));
        llvm::Value* in_range { };
        llvm::Value* ordinal { };
        if (linear) {
            in_range = builder_.CreateICmpULT(value, count);
            ordinal = value;
        } else {
            auto* left = info_field(info, i64_, 16U);
            auto* descending = nonzero(builder_.CreateAnd(
                builder_.CreateZExt(info_field(info, i32_, 60U), i64_),
                constant(2U)));
            ordinal = builder_.CreateSelect(descending,
                builder_.CreateSub(left, value), builder_.CreateSub(value, left));
            in_range = builder_.CreateAnd(
                builder_.CreateAnd(
                    builder_.CreateICmpSGE(value, info_field(info, i64_, 24U)),
                    builder_.CreateICmpSLE(value, info_field(info, i64_, 32U))),
                builder_.CreateICmpULT(ordinal, count));
        }
        in_range = builder_.CreateAnd(known, in_range);
        return { info, storage, in_range,
            builder_.CreateSelect(in_range, ordinal, constant(0U)) };
    }

    /// Storage 1: the element slot's arena offset; storage 2: the packed
    /// slot's arena offset. Element bit offset within it (storage 2 only).
    struct MemoryLocation {
        llvm::Value* arena_offset { };
        llvm::Value* words { };
        llvm::Value* bit { };
        llvm::Value* slot { };
    };

    [[nodiscard]] MemoryLocation memory_location(const MemoryElement& element)
    {
        auto* packed = builder_.CreateICmpEQ(element.storage,
            llvm::ConstantInt::get(i32_, 2U));
        // Storage 1 reads the table only for direct memories (others may
        // have no table).
        auto* elements = builder_.CreateLoad(ptr_,
            builder_.CreateConstInBoundsGEP1_64(i8_, element.info, 64U));
        auto* table_index = builder_.CreateMul(element.ordinal, constant(2U));
        auto* has_table = builder_.CreateICmpEQ(element.storage,
            llvm::ConstantInt::get(i32_, 1U));
        auto* safe_index = builder_.CreateSelect(has_table, table_index, constant(0U));
        auto* safe_table = builder_.CreateSelect(has_table, elements,
            builder_.CreateConstInBoundsGEP1_64(i8_, element.info, 72U));
        auto* element_offset = builder_.CreateZExt(builder_.CreateLoad(i32_,
            builder_.CreateInBoundsGEP(i32_, safe_table, safe_index)), i64_);
        auto* element_slot = builder_.CreateLoad(i32_,
            builder_.CreateInBoundsGEP(i32_, safe_table,
                builder_.CreateAdd(safe_index, constant(1U))));
        auto* width = builder_.CreateZExt(info_field(element.info, i32_, 56U), i64_);
        auto* packed_width = builder_.CreateZExt(
            info_field(element.info, i32_, 80U), i64_);
        auto* bit = builder_.CreateSub(packed_width,
            builder_.CreateMul(builder_.CreateAdd(element.ordinal, constant(1U)),
                width));
        auto* packed_offset = builder_.CreateZExt(
            info_field(element.info, i32_, 72U), i64_);
        return {
            builder_.CreateSelect(packed, packed_offset, element_offset),
            builder_.CreateSelect(packed,
                builder_.CreateZExt(info_field(element.info, i32_, 76U), i64_),
                constant(1U)),
            builder_.CreateSelect(packed, bit, constant(0U)),
            builder_.CreateSelect(packed, info_field(element.info, i32_, 4U),
                element_slot),
        };
    }

    [[nodiscard]] Pair emit_mem_read(const KInst& inst, const std::uint32_t at)
    {
        const auto index = load_register(inst.y);
        const auto element = memory_element(inst.x, index, inst.width,
            (inst.flags & detail::flag_linear) != 0U);
        const auto location = memory_location(element);
        auto* m = builder_.CreateSub(builder_.CreateShl(constant(1U),
                                         builder_.CreateZExt(info_field(element.info,
                                                                 i32_, 56U),
                                             i64_)),
            constant(1U));
        auto* width = builder_.CreateZExt(info_field(element.info, i32_, 56U), i64_);
        m = builder_.CreateSelect(builder_.CreateICmpEQ(width, constant(64U)),
            constant(~std::uint64_t { 0 }), m);
        auto* word = builder_.CreateLShr(location.bit, constant(6U));
        auto* shift = builder_.CreateAnd(location.bit, constant(63U));
        auto* last = builder_.CreateSub(location.words, constant(1U));
        auto* next = builder_.CreateSelect(builder_.CreateICmpULT(word, last),
            builder_.CreateAdd(word, constant(1U)), word);
        const auto plane = [&](llvm::Value* first) {
            auto* low = builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_,
                arena_, builder_.CreateAdd(first, word)));
            auto* high = builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_,
                arena_, builder_.CreateAdd(first, next)));
            auto* spill = builder_.CreateSelect(
                builder_.CreateICmpEQ(shift, constant(0U)), constant(0U),
                builder_.CreateShl(high,
                    builder_.CreateSub(constant(64U), shift)));
            return builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateLShr(low, shift), spill), m);
        };
        const Pair loaded { plane(location.arena_offset),
            plane(builder_.CreateAdd(location.arena_offset, location.words)) };
        const auto fast = select(element.in_range, loaded,
            Pair { info_field(element.info, i64_, 40U),
                info_field(element.info, i64_, 48U) });
        return with_fallback(builder_.CreateICmpNE(element.storage,
                                 llvm::ConstantInt::get(i32_, 0U)),
            fast, at, zero_pair(), index);
    }

    void emit_mem_write(const KInst& inst, const std::uint32_t at)
    {
        const auto value = load_register(inst.x);
        const auto index = load_register(inst.y);
        if (inst.aux != 0U || (inst.flags & detail::flag_nba) == 0U) {
            call_effect(at, value, index,
                inst.aux != 0U ? load_register(inst.z) : zero_pair());
            return;
        }
        const auto element = memory_element(inst.d, index, inst.offset,
            (inst.flags & detail::flag_linear) != 0U);
        auto* two_state = nonzero(builder_.CreateAnd(
            builder_.CreateZExt(info_field(element.info, i32_, 60U), i64_),
            constant(1U)));
        auto* ok = builder_.CreateAnd(
            builder_.CreateICmpNE(element.storage, llvm::ConstantInt::get(i32_, 0U)),
            builder_.CreateAnd(element.in_range,
                builder_.CreateICmpEQ(info_field(element.info, i32_, 56U),
                    llvm::ConstantInt::get(i32_, inst.width))));
        ok = builder_.CreateAnd(ok, builder_.CreateNot(
            builder_.CreateAnd(two_state, nonzero(value.b))));
        auto* queue = builder_.CreateLoad(ptr_, frame_field(56U));
        auto* count = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        auto* capacity = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 12U));
        ok = builder_.CreateAnd(ok, builder_.CreateICmpULT(count, capacity));
        auto* fast = llvm::BasicBlock::Create(context_, "mem_append", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "mem_write_slow", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "mem_written", &function_);
        builder_.CreateCondBr(ok, fast, slow);
        builder_.SetInsertPoint(fast);
        // The nonblocking queue's word write to the element's slot, as the
        // reference's element write would make it.
        const auto location = memory_location(element);
        auto* data = builder_.CreateLoad(ptr_, queue);
        auto* entry = builder_.CreateInBoundsGEP(i8_, data,
            builder_.CreateMul(builder_.CreateZExt(count, i64_), constant(48U)));
        const auto entry_field = [&](const std::uint64_t offset) {
            return builder_.CreateConstInBoundsGEP1_64(i8_, entry, offset);
        };
        builder_.CreateStore(location.slot, entry_field(0U));
        builder_.CreateStore(builder_.CreateTrunc(location.bit, i32_),
            entry_field(4U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, inst.width),
            entry_field(8U));
        builder_.CreateStore(builder_.CreateLoad(i32_, frame_field(32U)),
            entry_field(12U));
        builder_.CreateStore(value.a, entry_field(16U));
        builder_.CreateStore(value.b, entry_field(24U));
        builder_.CreateStore(constant(0U), entry_field(32U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, 0U), entry_field(40U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, at), entry_field(44U));
        builder_.CreateStore(
            builder_.CreateAdd(count, llvm::ConstantInt::get(i32_, 1U)),
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        builder_.CreateBr(done);
        builder_.SetInsertPoint(slow);
        call_effect(at, value, index, zero_pair());
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
    }

    /// dynamic_part_select (sub 1) from a wide slot with z planes, when result
    /// bit i reads source bit first + i (the declared right bound is the
    /// low bound of a descending range or the high bound of an ascending
    /// one). Strong 0/1 Logic9 elements read inline; others take the helper.
    [[nodiscard]] Pair emit_slot_part_select(const KInst& inst,
        const std::uint32_t at)
    {
        const auto base_register = load_register(inst.y);
        const auto& part = body_.parts[inst.aux];
        const bool increasing = (inst.flags & detail::flag_increasing) != 0U;
        const bool descending = (inst.flags & detail::flag_descending) != 0U;
        const bool two_state = (inst.flags & detail::flag_two_state) != 0U;
        const auto lower = std::min(part.left, part.right);
        const auto upper = std::max(part.left, part.right);
        if (!((descending && part.right == lower)
                || (!descending && part.right == upper))) {
            return call_evaluate(at, zero_pair(), base_register, zero_pair());
        }
        const auto width = inst.width;
        const auto edge = static_cast<std::int64_t>(width) - 1;
        const auto source_width = static_cast<std::int64_t>(inst.offset);
        const auto words = (inst.offset + 63U) / 64U;
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base_register.b, constant(mask_of(32U))),
            constant(0U));
        auto* base = builder_.CreateSExt(
            builder_.CreateTrunc(base_register.a, i32_), i64_);
        const auto signed_constant = [&](const std::int64_t value) {
            return constant(static_cast<std::uint64_t>(value));
        };
        auto* selected_right = increasing
            ? builder_.CreateAdd(base, signed_constant(descending ? 0 : edge))
            : builder_.CreateSub(base, signed_constant(descending ? edge : 0));
        // first = base_offset + (descending ? sr - right : right - sr)
        auto* first = builder_.CreateAdd(
            signed_constant(static_cast<std::int64_t>(part.base_offset)),
            descending ? builder_.CreateSub(selected_right, signed_constant(part.right))
                       : builder_.CreateSub(signed_constant(part.right), selected_right));
        const auto smax = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSGT(left, right), left,
                right);
        };
        const auto smin = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSLT(left, right), left,
                right);
        };
        auto* low = descending
            ? builder_.CreateSub(signed_constant(lower), selected_right)
            : builder_.CreateSub(selected_right, signed_constant(upper));
        auto* high = descending
            ? builder_.CreateSub(signed_constant(upper), selected_right)
            : builder_.CreateSub(selected_right, signed_constant(lower));
        low = smax(smax(low, constant(0U)),
            builder_.CreateSub(constant(0U), first));
        high = smin(smin(high, signed_constant(edge)),
            builder_.CreateSub(signed_constant(source_width - 1), first));
        auto* any = builder_.CreateICmpSLE(low, high);
        // Safe values when nothing is selected.
        auto* count = builder_.CreateSelect(any,
            builder_.CreateAdd(builder_.CreateSub(high, low), constant(1U)),
            constant(1U));
        auto* shift_out = builder_.CreateSelect(any, low, constant(0U));
        auto* offset = builder_.CreateSelect(any, builder_.CreateAdd(first, low),
            constant(0U));
        auto* count_mask = builder_.CreateSelect(
            builder_.CreateICmpEQ(count, constant(64U)),
            constant(~std::uint64_t { 0 }),
            builder_.CreateSub(builder_.CreateShl(constant(1U), count), constant(1U)));
        auto* slot = builder_.CreateZExt(binding(inst.x, 0U), i64_);
        auto* word = builder_.CreateLShr(offset, constant(6U));
        auto* bit = builder_.CreateAnd(offset, constant(63U));
        auto* next = builder_.CreateSelect(
            builder_.CreateICmpULT(word, constant(words - 1U)),
            builder_.CreateAdd(word, constant(1U)), word);
        const auto plane = [&](const std::uint64_t index) {
            auto* first_word = builder_.CreateAdd(slot, constant(index * words));
            auto* lo = builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_,
                arena_, builder_.CreateAdd(first_word, word)));
            auto* hi = builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_,
                arena_, builder_.CreateAdd(first_word, next)));
            auto* spill = builder_.CreateSelect(
                builder_.CreateICmpEQ(bit, constant(0U)), constant(0U),
                builder_.CreateShl(hi, builder_.CreateSub(constant(64U), bit)));
            return builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateLShr(lo, bit), spill), count_mask);
        };
        llvm::Value* field_a { };
        llvm::Value* field_b { };
        llvm::Value* ok = known;
        if (inst.z == 4U) {
            auto* p0 = plane(0U);
            auto* p1 = plane(1U);
            auto* upper_planes = builder_.CreateOr(plane(2U), plane(3U));
            ok = builder_.CreateAnd(ok, builder_.CreateAnd(
                builder_.CreateICmpEQ(upper_planes, constant(0U)),
                builder_.CreateICmpEQ(p1, count_mask)));
            field_a = p0;
            field_b = constant(0U);
        } else {
            field_a = plane(0U);
            field_b = plane(1U);
        }
        auto* m = constant(mask_of(width));
        auto* placed = builder_.CreateShl(count_mask, shift_out);
        auto* fill = two_state ? constant(0U) : m;
        auto* result_a = builder_.CreateSelect(any,
            builder_.CreateOr(builder_.CreateAnd(fill, builder_.CreateNot(placed)),
                builder_.CreateShl(field_a, shift_out)),
            fill);
        auto* result_b = builder_.CreateSelect(any,
            builder_.CreateOr(builder_.CreateAnd(fill, builder_.CreateNot(placed)),
                builder_.CreateShl(field_b, shift_out)),
            fill);
        // Nothing selected needs no strong check.
        ok = builder_.CreateAnd(known, builder_.CreateOr(builder_.CreateNot(any), ok));
        return with_fallback(ok, Pair { result_a, result_b }, at, zero_pair(),
            base_register);
    }

    /// Appends a deferred VHDL write at bit `offset` (an i32; the static
    /// offset when null).
    void emit_deferred_store(const KInst& inst, const std::uint32_t at,
        llvm::Value* offset = nullptr)
    {
        const auto value = load_register(inst.x);
        auto* unknown = builder_.CreateAnd(unknown_of(inst.x),
            constant(mask_of(inst.width)));
        auto* queue = builder_.CreateLoad(ptr_, frame_field(56U));
        auto* count = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        auto* capacity = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 12U));
        auto* fast = llvm::BasicBlock::Create(context_, "append", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "append_full", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "appended", &function_);
        builder_.CreateCondBr(builder_.CreateICmpULT(count, capacity), fast, slow);
        builder_.SetInsertPoint(fast);
        auto* data = builder_.CreateLoad(ptr_, queue);
        auto* entry = builder_.CreateInBoundsGEP(i8_, data,
            builder_.CreateMul(builder_.CreateZExt(count, i64_), constant(48U)));
        const auto field = [&](const std::uint64_t offset) {
            return builder_.CreateConstInBoundsGEP1_64(i8_, entry, offset);
        };
        builder_.CreateStore(builder_.CreateAnd(binding(inst.d, 1U),
                                 llvm::ConstantInt::get(i32_, ~silent_bit)),
            field(0U));
        builder_.CreateStore(offset != nullptr ? offset
                                               : llvm::ConstantInt::get(i32_, inst.offset),
            field(4U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, inst.width), field(8U));
        builder_.CreateStore(builder_.CreateLoad(i32_, frame_field(32U)),
            field(12U));
        builder_.CreateStore(value.a, field(16U));
        builder_.CreateStore(value.b, field(24U));
        builder_.CreateStore(unknown, field(32U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, 0U), field(40U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, at), field(44U));
        builder_.CreateStore(builder_.CreateAdd(count, llvm::ConstantInt::get(i32_, 1U)),
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        builder_.CreateBr(done);
        builder_.SetInsertPoint(slow);
        call_effect(at, value,
            inst.sub == 1U ? load_register(inst.y) : zero_pair(),
            Pair { unknown, constant(0U) });
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
    }

    /// Deferred store at a dynamic index (kernel_word::dynamic_index): a
    /// known, in-range index whose element fits the target appends directly;
    /// anything else takes the reference path, which reports the error.
    void emit_indexed_deferred_store(const KInst& inst, const std::uint32_t at)
    {
        const auto& selection = body_.indices[inst.aux];
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        const auto index = load_register(inst.y);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(value, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(value, right),
            builder_.CreateSub(value, right), builder_.CreateSub(right, value));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        // offset + width <= slot width (both small; no overflow in 64 bits).
        auto* fits = builder_.CreateICmpULE(
            builder_.CreateAdd(offset, constant(inst.width)), constant(inst.imm_a));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
        auto* direct = llvm::BasicBlock::Create(context_, "indexed_store", &function_);
        auto* reference = llvm::BasicBlock::Create(context_, "indexed_store_ref", &function_);
        auto* joined = llvm::BasicBlock::Create(context_, "indexed_stored", &function_);
        builder_.CreateCondBr(ok, direct, reference);
        builder_.SetInsertPoint(direct);
        emit_deferred_store(inst, at, builder_.CreateTrunc(offset, i32_));
        builder_.CreateBr(joined);
        builder_.SetInsertPoint(reference);
        call_effect(at, load_register(inst.x), index,
            Pair { unknown_of(inst.x), constant(0U) });
        builder_.CreateBr(joined);
        builder_.SetInsertPoint(joined);
    }

    // 'U' tracking (see CompiledBody::shadow_base).
    [[nodiscard]] bool tracked(const std::uint32_t reg) const
    {
        return body_.shadow_base != 0U && reg < body_.tracked.size()
            && body_.tracked[reg] != 0U;
    }

    [[nodiscard]] llvm::Value* unknown_of(const std::uint32_t reg)
    {
        return tracked(reg) ? load_register(body_.shadow_base + reg).a
                            : constant(0U);
    }

    void set_unknown(const std::uint32_t reg, llvm::Value* value)
    {
        if (tracked(reg)) {
            store_register(body_.shadow_base + reg, Pair { value, constant(0U) });
        }
    }

    [[nodiscard]] std::uint8_t mode_of(const std::uint32_t at) const
    {
        return body_.shadow_base != 0U ? body_.u_mode[at] : std::uint8_t { 0 };
    }

    /// Deoptimizes when an operand that compiled code reads by kind holds
    /// 'U'.
    void emit_unknown_checks(const std::uint32_t at)
    {
        if ((mode_of(at) & detail::u_check) == 0U
            || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        llvm::Value* any = constant(0U);
        for (auto index = body_.u_operand_begin[at];
             index < body_.u_operand_begin[at + 1U]; ++index) {
            any = builder_.CreateOr(any, unknown_of(body_.u_operands[index]));
        }
        auto* deopt = llvm::BasicBlock::Create(context_, "deopt", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "known", &function_);
        builder_.CreateCondBr(nonzero(any), deopt, next);
        builder_.SetInsertPoint(deopt);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i32_ }, false);
        auto* callee = builder_.CreateIntToPtr(
            constant(reinterpret_cast<std::uintptr_t>(helpers_.fail)), ptr_);
        builder_.CreateCall(type, callee,
            { frame_, llvm::ConstantInt::get(i32_, at),
                llvm::ConstantInt::get(i32_, 2U) });
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(next);
    }

    /// The result's 'U' mask computed from the operands (before the
    /// instruction may overwrite one of them); null when not applicable.
    [[nodiscard]] llvm::Value* unknown_before(const std::uint32_t at)
    {
        const auto& inst = body_.code[at];
        if ((mode_of(at) & detail::u_aware) == 0U
            || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return nullptr;
        }
        switch (inst.op) {
        case KOp::constant:
            return constant(detail::constant_unknown(inst.offset, inst.aux));
        case KOp::copy:
            return unknown_of(inst.x);
        case KOp::extract:
            if (inst.sub == 2U) {
                return nullptr;
            }
            return builder_.CreateAnd(
                builder_.CreateLShr(unknown_of(inst.x), constant(inst.offset)),
                constant(mask_of(inst.width)));
        case KOp::insert: {
            const auto field = mask_of(inst.width) << inst.offset;
            return builder_.CreateOr(
                builder_.CreateAnd(unknown_of(inst.x), constant(~field)),
                builder_.CreateAnd(
                    builder_.CreateShl(unknown_of(inst.y), constant(inst.offset)),
                    constant(field)));
        }
        case KOp::concat: {
            llvm::Value* result = constant(0U);
            std::uint32_t offset = 0U;
            for (std::uint32_t index = inst.x; index-- > 0U;) {
                const auto& operand = body_.concat[inst.aux + index];
                result = builder_.CreateOr(result,
                    builder_.CreateShl(
                        builder_.CreateAnd(unknown_of(operand.reg),
                            constant(mask_of(operand.width))),
                        constant(offset)));
                offset += operand.width;
            }
            return result;
        }
        case KOp::binary: {
            auto* m = constant(mask_of(inst.width));
            auto* left = unknown_of(inst.x);
            auto* right = unknown_of(inst.y);
            auto* either = builder_.CreateOr(left, right);
            const auto l = load_register(inst.x);
            const auto r = load_register(inst.y);
            switch (static_cast<BinaryOperator>(inst.sub)) {
            case BinaryOperator::bit_and: {
                // 'U' unless either side is a strong 0.
                auto* zero_l = builder_.CreateAnd(bit_not(l.a), bit_not(l.b));
                auto* zero_r = builder_.CreateAnd(bit_not(r.a), bit_not(r.b));
                return builder_.CreateAnd(builder_.CreateAnd(either, m),
                    bit_not(builder_.CreateOr(zero_l, zero_r)));
            }
            case BinaryOperator::bit_or: {
                auto* one_l = builder_.CreateAnd(l.a, bit_not(l.b));
                auto* one_r = builder_.CreateAnd(r.a, bit_not(r.b));
                return builder_.CreateAnd(builder_.CreateAnd(either, m),
                    bit_not(builder_.CreateOr(one_l, one_r)));
            }
            default:
                return builder_.CreateAnd(either, m);
            }
        }
        case KOp::unary_not:
            return unknown_of(inst.x);
        case KOp::conditional: {
            const auto condition = load_register(inst.x);
            auto* a0 = builder_.CreateAnd(condition.a, constant(1U));
            auto* b0 = builder_.CreateAnd(condition.b, constant(1U));
            auto* selected = builder_.CreateSelect(nonzero(a0), unknown_of(inst.y),
                unknown_of(inst.z));
            return builder_.CreateSelect(nonzero(b0), constant(0U), selected);
        }
        default:
            return nullptr;
        }
    }

    void unknown_after(const std::uint32_t at, llvm::Value* result_unknown)
    {
        const auto& inst = body_.code[at];
        const auto mode = mode_of(at);
        if (mode == 0U || builder_.GetInsertBlock() == nullptr
            || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        if ((mode & detail::u_clear) != 0U) {
            set_unknown(inst.d, constant(0U));
            return;
        }
        if ((mode & detail::u_aware) == 0U) {
            return;
        }
        switch (inst.op) {
        case KOp::load_slot9:
        case KOp::load_field9:
        case KOp::load_host:
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
            set_unknown(inst.d, last_unknown_ != nullptr ? last_unknown_
                                                         : constant(0U));
            break;
        case KOp::dynamic_insert:
        case KOp::dynamic_part_insert:
            set_unknown(inst.d, insert_unknown_ != nullptr ? insert_unknown_
                                                           : constant(0U));
            break;
        case KOp::extract:
            set_unknown(inst.d, inst.sub == 2U
                    ? (last_unknown_ != nullptr ? last_unknown_ : constant(0U))
                    : result_unknown);
            break;
        case KOp::store_vhdl:
        case KOp::generic:
            break;
        default:
            if (result_unknown != nullptr) {
                set_unknown(inst.d, result_unknown);
            }
            break;
        }
        last_unknown_ = nullptr;
    }

    /// Inline dynamic_part_insert: known base, selection in range with
    /// target offsets increasing with the source bit, field inside target.
    [[nodiscard]] Pair emit_dynamic_part_insert(const KInst& inst,
        const std::uint32_t at, const Pair target, const Pair source,
        const Pair base)
    {
        const auto& part = body_.parts[inst.aux];
        const auto width = part.width;
        if (width == 0U || width > 64U || inst.width > 64U) {
            deopt_on_unknown(inst, at);
            return call_evaluate(at, target, source, base);
        }
        const bool increasing = part.increasing;
        const bool descending = part.source_descending;
        const auto edge = static_cast<std::int64_t>(width - 1U);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(base);
        auto* selected_right = builder_.CreateAdd(value,
            constant(static_cast<std::uint64_t>(increasing ? (descending ? 0 : edge)
                                                           : (descending ? -edge : 0))));
        auto* selected_last = builder_.CreateAdd(selected_right,
            constant(static_cast<std::uint64_t>(descending ? edge : -edge)));
        auto* right = constant(static_cast<std::uint64_t>(part.right));
        auto* ordered = builder_.CreateICmpSLT(selected_right, selected_last);
        auto* low = builder_.CreateSelect(ordered, selected_right, selected_last);
        auto* high = builder_.CreateSelect(ordered, selected_last, selected_right);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(low,
                constant(static_cast<std::uint64_t>(std::min(part.left, part.right)))),
            builder_.CreateICmpSLE(high,
                constant(static_cast<std::uint64_t>(std::max(part.left, part.right)))));
        auto* increasing_offsets = descending
            ? builder_.CreateICmpSGE(low, right)
            : builder_.CreateICmpSLE(high, right);
        auto* first = builder_.CreateSelect(builder_.CreateICmpSGE(selected_right, right),
            builder_.CreateSub(selected_right, right),
            builder_.CreateSub(right, selected_right));
        auto* offset = builder_.CreateAdd(first, constant(part.base_offset));
        auto* fits = builder_.CreateICmpULE(builder_.CreateAdd(offset, constant(width)),
            constant(inst.width));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range),
            builder_.CreateAnd(increasing_offsets, fits));
        auto* safe = builder_.CreateSelect(ok, offset, constant(0U));
        auto* m = constant(mask_of(width));
        auto* field = builder_.CreateShl(m, safe);
        auto* keep = bit_not(field);
        const Pair fast {
            builder_.CreateOr(builder_.CreateAnd(target.a, keep),
                builder_.CreateShl(builder_.CreateAnd(source.a, m), safe)),
            builder_.CreateOr(builder_.CreateAnd(target.b, keep),
                builder_.CreateShl(builder_.CreateAnd(source.b, m), safe)) };
        insert_unknowns(inst, at, ok, keep, m, safe);
        return with_fallback3(ok, fast, at, target, source, base);
    }

    /// Inline dynamic_insert: known index in range, field inside target.
    [[nodiscard]] Pair emit_dynamic_insert(const KInst& inst,
        const std::uint32_t at, const Pair target, const Pair source,
        const Pair index)
    {
        const auto& selection = body_.indices[inst.aux];
        if (inst.width > 64U || inst.offset == 0U || inst.offset > 64U) {
            deopt_on_unknown(inst, at);
            return call_evaluate(at, target, source, index);
        }
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(value, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(value, right),
            builder_.CreateSub(value, right), builder_.CreateSub(right, value));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        auto* fits = builder_.CreateICmpULE(
            builder_.CreateAdd(offset, constant(inst.offset)), constant(inst.width));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
        auto* safe = builder_.CreateSelect(ok, offset, constant(0U));
        auto* m = constant(mask_of(inst.offset));
        auto* keep = bit_not(builder_.CreateShl(m, safe));
        const Pair fast {
            builder_.CreateOr(builder_.CreateAnd(target.a, keep),
                builder_.CreateShl(builder_.CreateAnd(source.a, m), safe)),
            builder_.CreateOr(builder_.CreateAnd(target.b, keep),
                builder_.CreateShl(builder_.CreateAnd(source.b, m), safe)) };
        insert_unknowns(inst, at, ok, keep, m, safe);
        return with_fallback3(ok, fast, at, target, source, index);
    }

    /// A U-aware insert without an inline path deoptimizes on 'U' operands.
    void deopt_on_unknown(const KInst& inst, const std::uint32_t at)
    {
        if ((mode_of(at) & detail::u_aware) == 0U) {
            return;
        }
        auto* any = builder_.CreateOr(unknown_of(inst.x), unknown_of(inst.y));
        auto* deopt = llvm::BasicBlock::Create(context_, "insert_deopt", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "insert_known", &function_);
        builder_.CreateCondBr(nonzero(any), deopt, next);
        builder_.SetInsertPoint(deopt);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i32_ }, false);
        auto* callee = builder_.CreateIntToPtr(
            constant(reinterpret_cast<std::uintptr_t>(helpers_.fail)), ptr_);
        builder_.CreateCall(type, callee,
            { frame_, llvm::ConstantInt::get(i32_, at),
                llvm::ConstantInt::get(i32_, 2U) });
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(next);
        insert_unknown_ = constant(0U);
    }

    /// 'U' masks of a U-aware dynamic insert: the fast path moves them with
    /// the field; operands with 'U' never take the slow path.
    void insert_unknowns(const KInst& inst, const std::uint32_t at,
        llvm::Value* ok, llvm::Value* keep, llvm::Value* m, llvm::Value* safe)
    {
        if ((mode_of(at) & detail::u_aware) == 0U) {
            return;
        }
        auto* target = unknown_of(inst.x);
        auto* source = unknown_of(inst.y);
        auto* any = builder_.CreateOr(target, source);
        auto* deopt = llvm::BasicBlock::Create(context_, "insert_deopt", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "insert_known", &function_);
        builder_.CreateCondBr(
            builder_.CreateAnd(builder_.CreateNot(ok), nonzero(any)), deopt, next);
        builder_.SetInsertPoint(deopt);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i32_ }, false);
        auto* callee = builder_.CreateIntToPtr(
            constant(reinterpret_cast<std::uintptr_t>(helpers_.fail)), ptr_);
        builder_.CreateCall(type, callee,
            { frame_, llvm::ConstantInt::get(i32_, at),
                llvm::ConstantInt::get(i32_, 2U) });
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(next);
        insert_unknown_ = builder_.CreateSelect(ok,
            builder_.CreateOr(builder_.CreateAnd(target, keep),
                builder_.CreateShl(builder_.CreateAnd(source, m), safe)),
            constant(0U));
    }

    /// with_fallback for three operands.
    [[nodiscard]] Pair with_fallback3(llvm::Value* fast_ok, const Pair fast,
        const std::uint32_t at, const Pair x, const Pair y, const Pair z)
    {
        auto* fast_block = builder_.GetInsertBlock();
        auto* slow = llvm::BasicBlock::Create(context_, "slow", &function_);
        auto* merge = llvm::BasicBlock::Create(context_, "merge", &function_);
        builder_.CreateCondBr(fast_ok, merge, slow);
        builder_.SetInsertPoint(slow);
        const auto slow_value = call_evaluate(at, x, y, z);
        auto* slow_end = builder_.GetInsertBlock();
        builder_.CreateBr(merge);
        builder_.SetInsertPoint(merge);
        auto* a = builder_.CreatePHI(i64_, 2U);
        a->addIncoming(fast.a, fast_block);
        a->addIncoming(slow_value.a, slow_end);
        auto* b = builder_.CreatePHI(i64_, 2U);
        b->addIncoming(fast.b, fast_block);
        b->addIncoming(slow_value.b, slow_end);
        // The fast path never produces 'U'.
        auto* unknown = builder_.CreatePHI(i64_, 2U);
        unknown->addIncoming(constant(0U), fast_block);
        unknown->addIncoming(last_unknown_, slow_end);
        last_unknown_ = unknown;
        return { a, b };
    }

    void emit_instruction(const std::uint32_t at)
    {
        const auto& inst = body_.code[at];
        current_ = at;
        switch (inst.op) {
        case KOp::nop:
        case KOp::mem_bind:
            break;
        case KOp::suspend:
            // Report the suspension operation to the thread runner.
            builder_.CreateStore(llvm::ConstantInt::get(i32_, inst.x),
                frame_field(44U));
            builder_.CreateRet(llvm::ConstantInt::get(i32_, 3U));
            break;
        case KOp::generic: {
            auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { ptr_, ptr_, i32_ }, false);
            auto* callee = builder_.CreateIntToPtr(
                constant(reinterpret_cast<std::uintptr_t>(helpers_.generic)), ptr_);
            builder_.CreateCall(type, callee,
                { frame_, registers_, llvm::ConstantInt::get(i32_, at) });
            // The helper reads and writes registers through the pointer.
            cache_.clear();
            check_status();
            break;
        }
        case KOp::mark:
            builder_.CreateStore(binding(inst.x, 0U), frame_field(32U));
            builder_.CreateStore(llvm::ConstantInt::get(i32_, inst.d),
                frame_field(36U));
            break;
        case KOp::constant:
            if (inst.sub == 1U) {
                // A per-instance constant: each plane is two 32-bit bindings.
                const auto word = [&](const std::uint32_t index) {
                    auto* low = builder_.CreateZExt(binding(index, 0U), i64_);
                    auto* high = builder_.CreateZExt(binding(index, 1U), i64_);
                    return builder_.CreateOr(low,
                        builder_.CreateShl(high, constant(32U)));
                };
                store_register(inst.d, { word(inst.x), word(inst.y) });
                break;
            }
            store_register(inst.d, { constant(inst.imm_a), constant(inst.imm_b) });
            break;
        case KOp::copy:
            store_register(inst.d, load_register(inst.x));
            break;
        case KOp::load_slot: {
            auto* offset = builder_.CreateZExt(binding(inst.x, 0U), i64_);
            auto* low = builder_.CreateInBoundsGEP(i64_, arena_, offset);
            auto* high = builder_.CreateInBoundsGEP(i64_, arena_,
                builder_.CreateAdd(offset, constant(1U)));
            store_register(inst.d,
                { builder_.CreateLoad(i64_, low), builder_.CreateLoad(i64_, high) });
            break;
        }
        case KOp::binary:
            store_register(inst.d,
                emit_binary(inst, load_register(inst.x), load_register(inst.y)));
            break;
        case KOp::reduce:
            store_register(inst.d, emit_reduce(inst, load_register(inst.x)));
            break;
        case KOp::unary_not: {
            const auto value = load_register(inst.x);
            auto* m = constant(mask_of(inst.width));
            store_register(inst.d,
                { builder_.CreateAnd(builder_.CreateOr(bit_not(value.a), value.b), m),
                    builder_.CreateAnd(value.b, m) });
            break;
        }
        case KOp::logical_not: {
            const auto [one, unknown] = truth(load_register(inst.x));
            // logic_not: one -> 0, zero -> 1, unknown -> x.
            auto* zero = builder_.CreateNot(builder_.CreateOr(one, unknown));
            store_register(inst.d, from_truth(zero, unknown));
            break;
        }
        case KOp::logical_binary: {
            const auto [left_one, left_unknown] = truth(load_register(inst.x));
            const auto [right_one, right_unknown] = truth(load_register(inst.y));
            auto* left_zero = builder_.CreateNot(builder_.CreateOr(left_one, left_unknown));
            auto* right_zero
                = builder_.CreateNot(builder_.CreateOr(right_one, right_unknown));
            llvm::Value* one { };
            llvm::Value* zero { };
            if (static_cast<LogicalBinaryOperator>(inst.sub)
                == LogicalBinaryOperator::logical_and) {
                zero = builder_.CreateOr(left_zero, right_zero);
                one = builder_.CreateAnd(left_one, right_one);
            } else {
                one = builder_.CreateOr(left_one, right_one);
                zero = builder_.CreateAnd(left_zero, right_zero);
            }
            auto* unknown = builder_.CreateNot(builder_.CreateOr(one, zero));
            store_register(inst.d, from_truth(one, unknown));
            break;
        }
        case KOp::extract: {
            if (inst.sub == 2U) {
                store_register(inst.d,
                    call_evaluate(at, zero_pair(), zero_pair(), zero_pair()));
                break;
            }
            const auto value = load_register(inst.x);
            auto* m = constant(mask_of(inst.width));
            auto* shift = constant(inst.offset);
            store_register(inst.d,
                { builder_.CreateAnd(builder_.CreateLShr(value.a, shift), m),
                    builder_.CreateAnd(builder_.CreateLShr(value.b, shift), m) });
            break;
        }
        case KOp::insert: {
            const auto target = load_register(inst.x);
            const auto source = load_register(inst.y);
            store_register(inst.d, insert(target, source, inst.offset, inst.width));
            break;
        }
        case KOp::concat: {
            Pair result = zero_pair();
            std::uint32_t offset = 0U;
            for (std::uint32_t index = inst.x; index-- > 0U;) {
                const auto& operand = body_.concat[inst.aux + index];
                result = insert(result, load_register(operand.reg), offset,
                    operand.width);
                offset += operand.width;
            }
            store_register(inst.d, result);
            break;
        }
        case KOp::conditional: {
            const auto condition = load_register(inst.x);
            const auto when_true = load_register(inst.y);
            const auto when_false = load_register(inst.z);
            auto* a0 = builder_.CreateTrunc(condition.a, llvm::Type::getInt1Ty(context_));
            auto* b0 = builder_.CreateTrunc(condition.b, llvm::Type::getInt1Ty(context_));
            auto* is_one = builder_.CreateAnd(a0, builder_.CreateNot(b0));
            auto* is_zero = builder_.CreateAnd(builder_.CreateNot(a0),
                builder_.CreateNot(b0));
            auto* m = constant(mask_of(inst.width));
            auto* same = builder_.CreateAnd(
                bit_not(builder_.CreateOr(builder_.CreateXor(when_true.a, when_false.a),
                    builder_.CreateXor(when_true.b, when_false.b))),
                m);
            auto* differ = builder_.CreateAnd(bit_not(same), m);
            const Pair merged { builder_.CreateOr(builder_.CreateAnd(when_true.a, same),
                                    differ),
                builder_.CreateOr(builder_.CreateAnd(when_true.b, same), differ) };
            store_register(inst.d,
                select(is_one, when_true, select(is_zero, when_false, merged)));
            break;
        }
        case KOp::two_state: {
            const auto value = load_register(inst.x);
            store_register(inst.d,
                { builder_.CreateAnd(value.a, bit_not(value.b)), constant(0U) });
            break;
        }
        case KOp::load_host:
            store_register(inst.d,
                call_evaluate(at, zero_pair(), zero_pair(), zero_pair()));
            break;
        case KOp::shift:
            store_register(inst.d,
                emit_shift(inst, at, load_register(inst.x), load_register(inst.y)));
            break;
        case KOp::dynamic_extract:
            if (inst.sub != 0U) {
                store_register(inst.d,
                    call_evaluate(at, zero_pair(), load_register(inst.y), zero_pair()));
            } else {
                store_register(inst.d, emit_dynamic_extract(inst, at,
                                           load_register(inst.x), load_register(inst.y)));
            }
            break;
        case KOp::dynamic_part_select:
            if (inst.sub == 1U && (inst.z == 2U || inst.z == 4U)) {
                store_register(inst.d, emit_slot_part_select(inst, at));
            } else if (inst.sub != 0U) {
                store_register(inst.d,
                    call_evaluate(at, zero_pair(), load_register(inst.y), zero_pair()));
            } else {
                store_register(inst.d, emit_dynamic_part_select(inst, at,
                                           load_register(inst.x), load_register(inst.y)));
            }
            break;
        case KOp::load_field: {
            auto* base = builder_.CreateZExt(binding(inst.x, 0U), i64_);
            if (inst.sub == 1U) {
                // The bit offset is a per-instance binding: the high word is
                // read (from an in-plane address) only when the field
                // crosses a word boundary inside the plane.
                auto* offset = builder_.CreateZExt(binding(inst.y, 0U), i64_);
                auto* word = builder_.CreateLShr(offset, constant(6U));
                auto* shift = builder_.CreateAnd(offset, constant(63U));
                auto* crosses = builder_.CreateAnd(
                    builder_.CreateICmpNE(shift, constant(0U)),
                    builder_.CreateAnd(
                        builder_.CreateICmpUGT(
                            builder_.CreateAdd(shift, constant(inst.width)),
                            constant(64U)),
                        builder_.CreateICmpULT(
                            builder_.CreateAdd(word, constant(1U)),
                            constant(inst.imm_a))));
                auto* next = builder_.CreateSelect(crosses,
                    builder_.CreateAdd(word, constant(1U)), word);
                auto* spill_shift = builder_.CreateAnd(
                    builder_.CreateSub(constant(64U), shift), constant(63U));
                const auto plane = [&](const std::uint64_t first) {
                    auto* start = builder_.CreateAdd(base, constant(first));
                    auto* low = builder_.CreateLoad(i64_,
                        builder_.CreateInBoundsGEP(i64_, arena_,
                            builder_.CreateAdd(start, word)));
                    auto* high = builder_.CreateLoad(i64_,
                        builder_.CreateInBoundsGEP(i64_, arena_,
                            builder_.CreateAdd(start, next)));
                    auto* value = builder_.CreateOr(
                        builder_.CreateLShr(low, shift),
                        builder_.CreateSelect(crosses,
                            builder_.CreateShl(high, spill_shift), constant(0U)));
                    return builder_.CreateAnd(value, constant(mask_of(inst.width)));
                };
                store_register(inst.d, { plane(0U), plane(inst.imm_a) });
                break;
            }
            const auto word = inst.offset / 64U;
            const auto shift = inst.offset % 64U;
            const auto plane = [&](const std::uint64_t first) {
                auto* low = builder_.CreateLoad(i64_,
                    builder_.CreateInBoundsGEP(i64_, arena_,
                        builder_.CreateAdd(base, constant(first + word))));
                llvm::Value* value = builder_.CreateLShr(low, constant(shift));
                if (shift != 0U && shift + inst.width > 64U
                    && word + 1U < inst.imm_a) {
                    auto* high = builder_.CreateLoad(i64_,
                        builder_.CreateInBoundsGEP(i64_, arena_,
                            builder_.CreateAdd(base, constant(first + word + 1U))));
                    value = builder_.CreateOr(value,
                        builder_.CreateShl(high, constant(64U - shift)));
                }
                return builder_.CreateAnd(value, constant(mask_of(inst.width)));
            };
            store_register(inst.d, { plane(0U), plane(inst.imm_a) });
            break;
        }
        case KOp::dynamic_insert:
            store_register(inst.d,
                emit_dynamic_insert(inst, at, load_register(inst.x),
                    load_register(inst.y), load_register(inst.z)));
            break;
        case KOp::dynamic_part_insert:
            store_register(inst.d,
                emit_dynamic_part_insert(inst, at, load_register(inst.x),
                    load_register(inst.y), load_register(inst.z)));
            break;
        case KOp::mem_read:
            store_register(inst.d, emit_mem_read(inst, at));
            break;
        case KOp::jump:
            builder_.CreateBr(blocks_[std::min<std::size_t>(inst.d, body_.code.size())]);
            break;
        case KOp::branch: {
            const auto condition = inst.sub == 2U
                ? call_evaluate(at, zero_pair(), zero_pair(), zero_pair())
                : load_register(inst.x);
            auto* a0 = builder_.CreateTrunc(condition.a, llvm::Type::getInt1Ty(context_));
            auto* b0 = builder_.CreateTrunc(condition.b, llvm::Type::getInt1Ty(context_));
            auto* when_true = blocks_[std::min<std::size_t>(inst.y, body_.code.size())];
            auto* when_false = blocks_[std::min<std::size_t>(inst.z, body_.code.size())];
            auto* known = llvm::BasicBlock::Create(context_, "known", &function_);
            auto* unknown = llvm::BasicBlock::Create(context_, "unknown", &function_);
            builder_.CreateCondBr(b0, unknown, known);
            builder_.SetInsertPoint(known);
            builder_.CreateCondBr(a0, when_true, when_false);
            builder_.SetInsertPoint(unknown);
            if ((inst.flags & detail::flag_linear) != 0U) {
                builder_.CreateBr(when_false);
            } else {
                auto* type = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(context_), { ptr_, i32_, i32_ }, false);
                auto* callee = builder_.CreateIntToPtr(
                    constant(reinterpret_cast<std::uintptr_t>(helpers_.fail)), ptr_);
                builder_.CreateCall(type, callee,
                    { frame_, llvm::ConstantInt::get(i32_, at),
                        llvm::ConstantInt::get(i32_, 1U) });
                builder_.CreateBr(failure_block_);
            }
            break;
        }
        case KOp::store_slot:
            if (inst.sub == 1U) {
                emit_store_wide_slot(inst, load_register(inst.x));
            } else {
                emit_store_slot(inst, load_register(inst.x));
            }
            break;
        case KOp::store_slot_nba:
            // Appended to the kernel's ordered nonblocking queue.
            emit_deferred_store(inst, at);
            break;
        case KOp::store_host:
            call_effect(at, load_register(inst.x), zero_pair(), zero_pair());
            break;
        case KOp::store_slot_dynamic:
        case KOp::store_slot_part:
            call_effect(at, load_register(inst.x), load_register(inst.y),
                zero_pair());
            break;
        case KOp::mem_write:
            emit_mem_write(inst, at);
            break;
        case KOp::wide_move:
            call_effect(at, zero_pair(),
                inst.sub == 1U ? load_register(inst.y) : zero_pair(), zero_pair());
            break;
        case KOp::load_slot9: {
            // Strong 0/1 elements: p1 set, p2 and p3 clear, p0 the value.
            auto* offset = builder_.CreateZExt(binding(inst.x, 0U), i64_);
            const auto plane = [&](const std::uint64_t index) {
                return builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_,
                    arena_, builder_.CreateAdd(offset, constant(index))));
            };
            auto* m = constant(mask_of(inst.width));
            auto* p0 = plane(0U);
            auto* p1 = plane(1U);
            auto* upper = builder_.CreateOr(plane(2U), plane(3U));
            auto* ok = builder_.CreateAnd(
                builder_.CreateICmpEQ(builder_.CreateAnd(upper, m), constant(0U)),
                builder_.CreateICmpEQ(builder_.CreateAnd(p1, m), m));
            store_register(inst.d,
                with_fallback(ok, Pair { builder_.CreateAnd(p0, m), constant(0U) },
                    at, zero_pair(), zero_pair()));
            break;
        }
        case KOp::load_field9: {
            // Strong 0/1 fields read inline; others take the helper.
            auto* base = builder_.CreateZExt(binding(inst.x, 0U), i64_);
            const auto word = inst.offset / 64U;
            const auto shift = inst.offset % 64U;
            const auto plane = [&](const std::uint64_t index) {
                const auto first = index * inst.imm_a + word;
                auto* low = builder_.CreateLoad(i64_,
                    builder_.CreateInBoundsGEP(i64_, arena_,
                        builder_.CreateAdd(base, constant(first))));
                llvm::Value* value = builder_.CreateLShr(low, constant(shift));
                if (shift != 0U && shift + inst.width > 64U
                    && word + 1U < inst.imm_a) {
                    auto* high = builder_.CreateLoad(i64_,
                        builder_.CreateInBoundsGEP(i64_, arena_,
                            builder_.CreateAdd(base, constant(first + 1U))));
                    value = builder_.CreateOr(value,
                        builder_.CreateShl(high, constant(64U - shift)));
                }
                return builder_.CreateAnd(value, constant(mask_of(inst.width)));
            };
            auto* m = constant(mask_of(inst.width));
            auto* p0 = plane(0U);
            auto* p1 = plane(1U);
            auto* p2 = plane(2U);
            auto* p3 = plane(3U);
            const auto inverted = [&](llvm::Value* value) {
                return builder_.CreateAnd(builder_.CreateNot(value), m);
            };
            if (inst.sub == 1U) {
                // Coerced to Logic4 (kernel_word::logic9_coerced): total.
                auto* not_p3 = inverted(p3);
                auto* zero = builder_.CreateAnd(builder_.CreateAnd(not_p3, p1), inverted(p0));
                auto* one = builder_.CreateAnd(builder_.CreateAnd(not_p3, p1), p0);
                auto* high_z = builder_.CreateAnd(builder_.CreateAnd(not_p3, p2),
                    builder_.CreateAnd(inverted(p1), inverted(p0)));
                auto* unknown = inverted(builder_.CreateOr(builder_.CreateOr(zero, one), high_z));
                store_register(inst.d, Pair { builder_.CreateOr(one, unknown),
                                           builder_.CreateOr(high_z, unknown) });
                last_unknown_ = constant(0U);
                break;
            }
            // Exact (kernel_word::logic9_uword): 0, 1, X, Z, and 'U' into a
            // tracked register; W, L, H and '-' take the reference path.
            auto* u = inverted(builder_.CreateOr(builder_.CreateOr(p0, p1),
                builder_.CreateOr(p2, p3)));
            auto* q0 = builder_.CreateOr(p0, u);
            auto* known = builder_.CreateAnd(p1, inverted(p2));
            auto* x = builder_.CreateAnd(builder_.CreateAnd(inverted(p2), inverted(p1)), q0);
            auto* high_z = builder_.CreateAnd(builder_.CreateAnd(p2, inverted(p1)),
                inverted(q0));
            llvm::Value* ok = builder_.CreateAnd(builder_.CreateICmpEQ(p3, constant(0U)),
                builder_.CreateICmpEQ(builder_.CreateOr(builder_.CreateOr(known, x), high_z),
                    m));
            if (!tracked(inst.d)) {
                ok = builder_.CreateAnd(ok, builder_.CreateICmpEQ(u, constant(0U)));
            }
            store_register(inst.d,
                with_fallback(ok, Pair { q0, builder_.CreateOr(x, high_z) }, at,
                    zero_pair(), zero_pair(), u));
            break;
        }
        case KOp::store_vhdl:
            if (inst.sub == 0U && (inst.flags & detail::flag_blocking) == 0U) {
                emit_deferred_store(inst, at);
                break;
            }
            if (inst.sub == 1U && (inst.flags & detail::flag_blocking) == 0U
                && inst.imm_a != 0U) {
                emit_indexed_deferred_store(inst, at);
                break;
            }
            // z carries the value's 'U' mask.
            call_effect(at, load_register(inst.x),
                inst.sub == 1U ? load_register(inst.y) : zero_pair(),
                Pair { unknown_of(inst.x), constant(0U) });
            break;
        case KOp::integer_binary:
            store_register(inst.d, emit_integer_binary(inst, at,
                                       load_register(inst.x), load_register(inst.y)));
            break;
        case KOp::integer_unary:
            store_register(inst.d,
                call_evaluate(at, load_register(inst.x), zero_pair(), zero_pair()));
            break;
        case KOp::integer_check:
            emit_integer_check(inst, at, load_register(inst.x));
            break;
        case KOp::assert_check: {
            const auto condition = load_register(inst.x);
            auto* one = builder_.CreateAnd(
                builder_.CreateTrunc(condition.a, llvm::Type::getInt1Ty(context_)),
                builder_.CreateNot(builder_.CreateTrunc(condition.b,
                    llvm::Type::getInt1Ty(context_))));
            emit_checked(one, at, condition);
            break;
        }
        case KOp::call:
            emit_call(inst, at);
            break;
        case KOp::ret:
            emit_return(inst, at);
            break;
        case KOp::deopt:
            call_effect(at, zero_pair(), zero_pair(), zero_pair());
            break;
        }
    }

    /// Continues when `ok`; otherwise the effect helper checks (and reports)
    /// the instruction on the reference path first.
    void emit_checked(llvm::Value* ok, const std::uint32_t at, const Pair x)
    {
        auto* slow = llvm::BasicBlock::Create(context_, "check", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "checked", &function_);
        builder_.CreateCondBr(ok, next, slow);
        builder_.SetInsertPoint(slow);
        call_effect(at, x, zero_pair(), zero_pair());
        builder_.CreateBr(next);
        builder_.SetInsertPoint(next);
    }

    [[nodiscard]] Pair emit_integer_binary(const KInst& inst,
        const std::uint32_t at, const Pair x, const Pair y)
    {
        using runtime::simir::IntegerBinaryOperator;
        const auto operation = static_cast<IntegerBinaryOperator>(inst.sub);
        if (inst.width != 32U || operation == IntegerBinaryOperator::power) {
            return call_evaluate(at, x, y, zero_pair());
        }
        auto* m = constant(mask_of(32U));
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(builder_.CreateOr(x.b, y.b), m), constant(0U));
        const auto widen = [&](llvm::Value* value) {
            return builder_.CreateSExt(builder_.CreateTrunc(value, i32_), i64_);
        };
        auto* left = widen(x.a);
        auto* right = widen(y.a);
        llvm::Value* result = nullptr;
        llvm::Value* valid = llvm::ConstantInt::getTrue(context_);
        switch (operation) {
        case IntegerBinaryOperator::add:
            result = builder_.CreateAdd(left, right);
            break;
        case IntegerBinaryOperator::subtract:
            result = builder_.CreateSub(left, right);
            break;
        case IntegerBinaryOperator::multiply:
            result = builder_.CreateMul(left, right);
            break;
        default: {
            // divide, remainder, modulo: division by zero and minimum / -1
            // take the reference path (errors). The divisor is made safe
            // because the fast value is computed before the check.
            auto* minimum = constant(static_cast<std::uint64_t>(
                std::numeric_limits<std::int32_t>::min()));
            auto* minus_one = constant(~std::uint64_t { 0 });
            valid = builder_.CreateNot(builder_.CreateOr(
                builder_.CreateICmpEQ(right, constant(0U)),
                builder_.CreateAnd(builder_.CreateICmpEQ(left, minimum),
                    builder_.CreateICmpEQ(right, minus_one))));
            auto* divisor = builder_.CreateSelect(valid, right, constant(1U));
            if (operation == IntegerBinaryOperator::divide) {
                result = builder_.CreateSDiv(left, divisor);
            } else {
                result = builder_.CreateSRem(left, divisor);
                if (operation == IntegerBinaryOperator::modulo) {
                    // The result takes the divisor's sign.
                    auto* adjust = builder_.CreateAnd(
                        builder_.CreateICmpNE(result, constant(0U)),
                        builder_.CreateICmpNE(
                            builder_.CreateICmpSLT(result, constant(0U)),
                            builder_.CreateICmpSLT(divisor, constant(0U))));
                    result = builder_.CreateSelect(adjust,
                        builder_.CreateAdd(result, divisor), result);
                }
            }
            break;
        }
        }
        // In range when the result survives a round trip through 32 bits.
        auto* fits = builder_.CreateAnd(valid,
            builder_.CreateICmpEQ(result, widen(result)));
        return with_fallback(builder_.CreateAnd(known, fits),
            Pair { builder_.CreateAnd(result, m), constant(0U) }, at, x, y);
    }

    void emit_integer_check(const KInst& inst, const std::uint32_t at,
        const Pair x)
    {
        const auto width = inst.width;
        const auto lower = static_cast<std::int64_t>(inst.imm_a);
        auto* m = constant(mask_of(width));
        auto* known = builder_.CreateICmpEQ(builder_.CreateAnd(x.b, m),
            constant(0U));
        auto* bits = builder_.CreateAnd(x.a, m);
        llvm::Value* value = bits;
        if (width == 32U || width == 64U || lower < 0) {
            if (width < 64U) {
                auto* shift = constant(64U - width);
                value = builder_.CreateAShr(builder_.CreateShl(bits, shift), shift);
            }
        }
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(inst.imm_a)),
            builder_.CreateICmpSLE(value, constant(inst.imm_b)));
        emit_checked(builder_.CreateAnd(known, in_range), at, x);
    }

    [[nodiscard]] llvm::Value* register_word(llvm::Value* reg,
        const std::uint32_t plane)
    {
        auto* index = builder_.CreateAdd(builder_.CreateShl(reg, constant(1U)),
            constant(plane));
        return builder_.CreateInBoundsGEP(i64_, registers_, index);
    }

    void forget_registers(const std::uint32_t first, const std::uint32_t count)
    {
        for (std::uint32_t reg = first; reg < first + count; ++reg) {
            cache_.erase(reg);
        }
    }

    void emit_call(const KInst& inst, const std::uint32_t at)
    {
        if (inst.z == 0U) {
            // The helper pushes onto the member's runtime call stack.
            call_effect(at, zero_pair(), zero_pair(), zero_pair());
            builder_.CreateBr(
                blocks_[std::min<std::size_t>(inst.d, body_.code.size())]);
            return;
        }
        const auto pointer = load_register(inst.x);
        auto* m = constant(mask_of(32U));
        auto* depth = builder_.CreateAnd(pointer.a, m);
        auto* ok = builder_.CreateAnd(
            builder_.CreateICmpEQ(builder_.CreateAnd(pointer.b, m), constant(0U)),
            builder_.CreateICmpULT(depth, constant(inst.z)));
        auto* fast = llvm::BasicBlock::Create(context_, "call", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "call_fail", &function_);
        builder_.CreateCondBr(ok, fast, slow);
        builder_.SetInsertPoint(slow);
        call_effect(at, pointer, zero_pair(), zero_pair());
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(fast);
        auto* entry = builder_.CreateAdd(depth, constant(inst.y));
        builder_.CreateStore(constant(inst.imm_a), register_word(entry, 0U));
        builder_.CreateStore(constant(0U), register_word(entry, 1U));
        forget_registers(inst.y, inst.z);
        store_register(inst.x,
            Pair { builder_.CreateAdd(depth, constant(1U)), constant(0U) });
        builder_.CreateBr(blocks_[std::min<std::size_t>(inst.d, body_.code.size())]);
    }

    void emit_return(const KInst& inst, const std::uint32_t at)
    {
        if (inst.z == 0U) {
            // The helper pops the member's runtime call stack.
            const auto target = call_evaluate(at, zero_pair(), zero_pair(),
                zero_pair());
            auto* invalid = llvm::BasicBlock::Create(context_, "ret_invalid",
                &function_);
            auto* targets = builder_.CreateSwitch(target.a, invalid,
                static_cast<unsigned>(body_.return_targets.size()));
            for (const auto value : body_.return_targets) {
                targets->addCase(llvm::ConstantInt::get(
                                     llvm::cast<llvm::IntegerType>(i64_), value),
                    blocks_[std::min<std::size_t>(value, body_.code.size())]);
            }
            builder_.SetInsertPoint(invalid);
            call_effect(at, Pair { constant(1U), constant(0U) }, zero_pair(),
                zero_pair());
            builder_.CreateBr(failure_block_);
            return;
        }
        const auto pointer = load_register(inst.x);
        auto* m = constant(mask_of(32U));
        auto* depth = builder_.CreateAnd(pointer.a, m);
        auto* ok = builder_.CreateAnd(
            builder_.CreateICmpEQ(builder_.CreateAnd(pointer.b, m), constant(0U)),
            builder_.CreateAnd(builder_.CreateICmpUGE(depth, constant(1U)),
                builder_.CreateICmpULE(depth, constant(inst.z))));
        auto* fast = llvm::BasicBlock::Create(context_, "ret", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "ret_fail", &function_);
        builder_.CreateCondBr(ok, fast, slow);
        builder_.SetInsertPoint(fast);
        auto* entry = builder_.CreateAdd(
            builder_.CreateSub(depth, constant(1U)), constant(inst.y));
        auto* target = builder_.CreateLoad(i64_, register_word(entry, 0U));
        auto* target_b = builder_.CreateLoad(i64_, register_word(entry, 1U));
        auto* dispatch = llvm::BasicBlock::Create(context_, "ret_dispatch",
            &function_);
        builder_.CreateCondBr(builder_.CreateICmpEQ(target_b, constant(0U)),
            dispatch, slow);
        builder_.SetInsertPoint(dispatch);
        store_register(inst.x,
            Pair { builder_.CreateSub(depth, constant(1U)), constant(0U) });
        auto* targets = builder_.CreateSwitch(target, slow,
            static_cast<unsigned>(body_.return_targets.size()));
        for (const auto value : body_.return_targets) {
            targets->addCase(llvm::ConstantInt::get(
                                llvm::cast<llvm::IntegerType>(i64_), value),
                blocks_[std::min<std::size_t>(value, body_.code.size())]);
        }
        builder_.SetInsertPoint(slow);
        // The helper reports the reference failure for this return.
        call_effect(at, pointer, zero_pair(), zero_pair());
        builder_.CreateBr(failure_block_);
    }

    [[nodiscard]] Pair insert(const Pair target, const Pair source,
        const std::uint32_t offset, const std::uint32_t width)
    {
        const auto field = mask_of(width) << offset;
        auto* keep = constant(~field);
        auto* place = constant(field);
        auto* shift = constant(offset);
        return { builder_.CreateOr(builder_.CreateAnd(target.a, keep),
                     builder_.CreateAnd(builder_.CreateShl(source.a, shift), place)),
            builder_.CreateOr(builder_.CreateAnd(target.b, keep),
                builder_.CreateAnd(builder_.CreateShl(source.b, shift), place)) };
    }

    /// A narrow field [offset, offset + width) of a wide slot with imm_a
    /// words per plane, as StaticKernel::write_slot_word stores it.
    void emit_store_wide_slot(const KInst& inst, const Pair value)
    {
        auto* offset = builder_.CreateZExt(binding(inst.d, 0U), i64_);
        auto* info = binding(inst.d, 1U);
        const auto words = inst.imm_a;
        const auto word = inst.offset / 64U;
        const auto shift = inst.offset % 64U;
        const bool spans = shift != 0U && shift + inst.width > 64U
            && word + 1U < words;
        auto* m = constant(mask_of(inst.width));
        const auto address = [&](const std::uint64_t plane,
                                 const std::uint64_t index) {
            return builder_.CreateInBoundsGEP(i64_, arena_,
                builder_.CreateAdd(offset, constant(plane * words + index)));
        };
        struct Plane {
            llvm::Value* low { };
            llvm::Value* high { };
            llvm::Value* field { };
        };
        const auto read = [&](const std::uint64_t plane) {
            Plane result;
            result.low = builder_.CreateLoad(i64_, address(plane, word));
            llvm::Value* field = builder_.CreateLShr(result.low, constant(shift));
            if (spans) {
                result.high = builder_.CreateLoad(i64_, address(plane, word + 1U));
                field = builder_.CreateOr(field,
                    builder_.CreateShl(result.high, constant(64U - shift)));
            }
            result.field = builder_.CreateAnd(field, m);
            return result;
        };
        const auto old_a = read(0U);
        const auto old_b = read(1U);
        auto* next_a = builder_.CreateAnd(value.a, m);
        auto* next_b = builder_.CreateAnd(value.b, m);
        auto* silent = builder_.CreateICmpNE(
            builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
            llvm::ConstantInt::get(i32_, 0U));
        auto* changed = builder_.CreateOr(builder_.CreateXor(old_a.field, next_a),
            builder_.CreateXor(old_b.field, next_b));
        auto* write = llvm::BasicBlock::Create(context_, "write_wide", &function_);
        auto* notify = llvm::BasicBlock::Create(context_, "notify_wide", &function_);
        auto* after = llvm::BasicBlock::Create(context_, "stored_wide", &function_);
        builder_.CreateCondBr(nonzero(changed), write, after);
        builder_.SetInsertPoint(write);
        const auto store = [&](const std::uint64_t plane, const Plane& old,
                               llvm::Value* next) {
            auto* low_mask = constant(~(mask_of(inst.width) << shift));
            builder_.CreateStore(builder_.CreateOr(builder_.CreateAnd(old.low, low_mask),
                                     builder_.CreateShl(next, constant(shift))),
                address(plane, word));
            if (spans) {
                const auto spill = 64U - shift;
                auto* high_mask = constant(~(mask_of(inst.width) >> spill));
                builder_.CreateStore(
                    builder_.CreateOr(builder_.CreateAnd(old.high, high_mask),
                        builder_.CreateLShr(next, constant(spill))),
                    address(plane, word + 1U));
            }
        };
        store(0U, old_a, next_a);
        store(1U, old_b, next_b);
        builder_.CreateCondBr(silent, after, notify);
        builder_.SetInsertPoint(notify);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i64_, i32_ }, false);
        auto* callee = builder_.CreateIntToPtr(
            constant(reinterpret_cast<std::uintptr_t>(helpers_.notify_field)), ptr_);
        builder_.CreateCall(type, callee,
            { frame_, builder_.CreateAnd(info,
                          llvm::ConstantInt::get(i32_, ~silent_bit)),
                changed, llvm::ConstantInt::get(i32_, inst.offset) });
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
            failure_block_, after);
        builder_.SetInsertPoint(after);
    }

    void emit_store_slot(const KInst& inst, const Pair value)
    {
        auto* offset = builder_.CreateZExt(binding(inst.d, 0U), i64_);
        auto* info = binding(inst.d, 1U);
        auto* low = builder_.CreateInBoundsGEP(i64_, arena_, offset);
        auto* high = builder_.CreateInBoundsGEP(i64_, arena_,
            builder_.CreateAdd(offset, constant(1U)));
        const Pair old { builder_.CreateLoad(i64_, low),
            builder_.CreateLoad(i64_, high) };
        const auto next = (inst.flags & detail::flag_linear) != 0U
            ? insert(old, value, inst.offset, inst.width)
            : value;
        auto* silent = builder_.CreateICmpNE(
            builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
            llvm::ConstantInt::get(i32_, 0U));
        auto* changed = builder_.CreateOr(builder_.CreateXor(old.a, next.a),
            builder_.CreateXor(old.b, next.b));
        auto* write = llvm::BasicBlock::Create(context_, "write", &function_);
        auto* notify = llvm::BasicBlock::Create(context_, "notify", &function_);
        auto* after = llvm::BasicBlock::Create(context_, "stored", &function_);
        builder_.CreateCondBr(builder_.CreateOr(silent, nonzero(changed)), write,
            after);
        builder_.SetInsertPoint(write);
        builder_.CreateStore(next.a, low);
        builder_.CreateStore(next.b, high);
        builder_.CreateCondBr(silent, after, notify);
        builder_.SetInsertPoint(notify);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i64_ }, false);
        auto* callee = builder_.CreateIntToPtr(
            constant(reinterpret_cast<std::uintptr_t>(helpers_.notify)), ptr_);
        builder_.CreateCall(type, callee,
            { frame_, builder_.CreateAnd(info,
                          llvm::ConstantInt::get(i32_, ~silent_bit)),
                changed });
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
            failure_block_, after);
        builder_.SetInsertPoint(after);
    }

    llvm::LLVMContext& context_;
    llvm::Function& function_;
    const detail::CompiledBody& body_;
    const StaticKernelNativeHelpers& helpers_;
    llvm::IRBuilder<> builder_;
    llvm::Type* i32_;
    llvm::Type* i64_;
    llvm::Type* i8_;
    llvm::PointerType* ptr_;
    llvm::Value* frame_ { };
    llvm::Value* registers_ { };
    bool local_registers_ { };
    /// Per register: its local slot, or null when it lives in the frame.
    std::vector<llvm::Value*> local_;
    llvm::Value* arena_ { };
    llvm::Value* out_ { };
    /// 'U' mask returned by the last evaluate helper call (or its merge).
    llvm::Value* last_unknown_ { };
    /// 'U' mask of a U-aware dynamic insert's result.
    llvm::Value* insert_unknown_ { };
    llvm::Value* bindings_ { };
    llvm::BasicBlock* return_block_ { };
    llvm::BasicBlock* failure_block_ { };
    std::vector<llvm::BasicBlock*> blocks_;
    std::map<std::uint32_t, Pair> cache_;
    std::uint32_t current_ { };
};

class LlvmStaticKernelCodegen final : public StaticKernelCodegen {
public:
    LlvmStaticKernelCodegen()
    {
        initialize_native_target();
        const char* level = std::getenv("FSIM_STATIC_KERNEL_CODEGEN_LEVEL");
        const auto codegen_level = level == nullptr ? 0 : std::atoi(level);
        jit_ = make_jit(codegen_level,
            std::getenv("FSIM_STATIC_KERNEL_NO_FAST_ISEL") == nullptr);
        // Hot templates: the optimizing backend. Register allocation and the
        // machine passes carry most of its benefit; fast instruction
        // selection halves its compile time for a small run-time cost
        // (FSIM_STATIC_KERNEL_HOT_FAST_ISEL=0 selects the full selector).
        const char* hot_level = std::getenv("FSIM_STATIC_KERNEL_HOT_LEVEL");
        const char* hot_fast_isel = std::getenv("FSIM_STATIC_KERNEL_HOT_FAST_ISEL");
        hot_jit_ = make_jit(hot_level == nullptr ? 2 : std::atoi(hot_level),
            hot_fast_isel == nullptr || std::string_view { hot_fast_isel } != "0");
        // Warm templates: full instruction selection without the optimizing
        // backend's cost.
        warm_jit_ = make_jit(0, false);
    }

    std::vector<StaticKernelNativeEntry> compile(
        std::span<const StaticKernelTemplate> templates,
        const StaticKernelNativeHelpers& helpers) override
    {
        const auto started = std::chrono::steady_clock::now();
        std::vector<StaticKernelNativeEntry> entries(templates.size(), nullptr);
        std::size_t instructions = 0U;
        std::array<double, 3> tier_ms { };
        for (const std::uint8_t tier : { std::uint8_t { 0U }, std::uint8_t { 1U }, std::uint8_t { 2U } }) {
            std::vector<std::size_t> group;
            for (std::size_t index = 0U; index < templates.size(); ++index) {
                if (templates[index].tier == tier) {
                    group.push_back(index);
                    instructions += templates[index].body->code.size();
                }
            }
            if (!group.empty()) {
                const auto group_started = std::chrono::steady_clock::now();
                compile_group(tier == 2U ? *hot_jit_ : tier == 1U ? *warm_jit_ : *jit_,
                    templates, group, helpers, entries, tier);
                tier_ms[tier] = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - group_started).count();
            }
        }
        ++generation_;
        const auto finished = std::chrono::steady_clock::now();
        if (std::getenv("FSIM_STATIC_KERNEL_PERF_MAP") != nullptr) {
            // Diagnostic: a perf map for the templates. Sizes are distances to
            // the next template (capped), so perf can attribute samples.
            std::vector<std::pair<std::uintptr_t, std::size_t>> ordered;
            for (std::size_t index = 0U; index < entries.size(); ++index) {
                ordered.emplace_back(
                    reinterpret_cast<std::uintptr_t>(entries[index]), index);
            }
            std::ranges::sort(ordered);
            auto map = support::native_fs::open_ofstream(
                std::filesystem::path { "/tmp/perf-" + std::to_string(::getpid())
                    + ".map" },
                std::ios::app);
            for (std::size_t index = 0U; index < ordered.size(); ++index) {
                const auto size = index + 1U < ordered.size()
                    ? std::min<std::uintptr_t>(
                          ordered[index + 1U].first - ordered[index].first, 65536U)
                    : 4096U;
                map << std::hex << ordered[index].first << ' ' << size << std::dec
                    << " sk_template_" << ordered[index].second << '\n';
            }
        }
        if (std::getenv("FSIM_PROFILE_PHASES") != nullptr) {
            const auto ms = [](auto from, auto to) {
                return std::chrono::duration<double, std::milli>(to - from).count();
            };
            std::cerr << "fsim-profile: static-kernel-codegen templates="
                      << templates.size() << " hot="
                      << std::ranges::count_if(templates,
                             [](const StaticKernelTemplate& entry) {
                                 return entry.tier == 2U;
                             })
                      << " warm="
                      << std::ranges::count_if(templates,
                             [](const StaticKernelTemplate& entry) {
                                 return entry.tier == 1U;
                             })
                      << " instructions=" << instructions
                      << " cold_ms=" << tier_ms[0] << " warm_ms=" << tier_ms[1]
                      << " hot_ms=" << tier_ms[2]
                      << " total_ms=" << ms(started, finished) << '\n';
        }
        return entries;
    }

private:
    [[nodiscard]] static std::unique_ptr<llvm::orc::LLJIT> make_jit(
        const int codegen_level, const bool fast_isel)
    {
        auto target = llvm::orc::JITTargetMachineBuilder::detectHost();
        if (!target) {
            throw std::runtime_error(error_text(target.takeError()));
        }
        target->setCodeGenOptLevel(codegen_level <= 0 ? llvm::CodeGenOptLevel::None
                : codegen_level == 1                   ? llvm::CodeGenOptLevel::Less
                : codegen_level == 2 ? llvm::CodeGenOptLevel::Default
                                     : llvm::CodeGenOptLevel::Aggressive);
        target->getOptions().EnableFastISel = fast_isel;
        auto jit = llvm::orc::LLJITBuilder()
                       .setJITTargetMachineBuilder(std::move(*target))
                       .setNumCompileThreads(0U)
                       .create();
        if (!jit) {
            throw std::runtime_error(error_text(jit.takeError()));
        }
        return std::move(*jit);
    }

    void compile_group(llvm::orc::LLJIT& jit,
        std::span<const StaticKernelTemplate> templates,
        const std::vector<std::size_t>& group,
        const StaticKernelNativeHelpers& helpers,
        std::vector<StaticKernelNativeEntry>& entries, const std::uint8_t tier)
    {
        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = std::make_unique<llvm::Module>("fsim_static_kernel", *context);
        module->setDataLayout(jit.getDataLayout());
        auto* i32 = llvm::Type::getInt32Ty(*context);
        auto* ptr = llvm::PointerType::getUnqual(*context);
        auto* type = llvm::FunctionType::get(i32, { ptr, ptr }, false);
        std::vector<std::string> names;
        names.reserve(group.size());
        for (const auto index : group) {
            names.push_back("fsim_sk_" + std::to_string(generation_) + "_"
                + std::to_string(index));
            auto* function = llvm::Function::Create(type,
                llvm::Function::ExternalLinkage, names.back(), *module);
            function->addParamAttr(1, llvm::Attribute::NoAlias);
            function->addParamAttr(0, llvm::Attribute::NoAlias);
            function->addFnAttr(llvm::Attribute::NoUnwind);
            FunctionEmitter emitter(*module, *function, *templates[index].body,
                helpers, !templates[index].vhdl);
            emitter.emit();
        }
        if (std::getenv("FSIM_STATIC_KERNEL_VERIFY") != nullptr
            && llvm::verifyModule(*module, &llvm::errs())) {
            throw std::runtime_error("static kernel module is invalid");
        }
        {
            llvm::LoopAnalysisManager loop_analyses;
            llvm::FunctionAnalysisManager function_analyses;
            llvm::CGSCCAnalysisManager cgscc_analyses;
            llvm::ModuleAnalysisManager module_analyses;
            llvm::PassBuilder builder;
            builder.registerModuleAnalyses(module_analyses);
            builder.registerCGSCCAnalyses(cgscc_analyses);
            builder.registerFunctionAnalyses(function_analyses);
            builder.registerLoopAnalyses(loop_analyses);
            builder.crossRegisterProxies(loop_analyses, function_analyses,
                cgscc_analyses, module_analyses);
            llvm::ModulePassManager pipeline;
            // Cold templates only need registers promoted for the fast
            // instruction selector; further IR passes cost more compile
            // time than they save at run time.
            const char* custom = std::getenv(tier == 2U
                    && std::getenv("FSIM_STATIC_KERNEL_HOT_PIPELINE") != nullptr
                ? "FSIM_STATIC_KERNEL_HOT_PIPELINE" : "FSIM_STATIC_KERNEL_PIPELINE");
            if (auto error = builder.parsePassPipeline(pipeline,
                    custom != nullptr ? custom
                        : tier != 0U  ? "function(sroa,early-cse,simplifycfg)"
                                      : "function(sroa)")) {
                throw std::runtime_error(error_text(std::move(error)));
            }
            pipeline.run(*module, module_analyses);
        }
        if (const char* dump = std::getenv("FSIM_STATIC_KERNEL_DUMP_IR")) {
            // Diagnostic: the IR handed to instruction selection, one file per
            // group; function fsim_sk_<generation>_<index> is template <index>.
            auto file = support::native_fs::open_ofstream(
                std::filesystem::path { dump }
                / ("static-kernel-" + std::to_string(generation_)
                    + (tier == 2U ? "-hot.ll" : tier == 1U ? "-warm.ll" : "-cold.ll")));
            std::string text;
            llvm::raw_string_ostream stream(text);
            module->print(stream, nullptr);
            file << text;
        }
        if (auto error = jit.addIRModule(llvm::orc::ThreadSafeModule(
                std::move(module), std::move(context)))) {
            throw std::runtime_error(error_text(std::move(error)));
        }
        for (std::size_t slot = 0U; slot < group.size(); ++slot) {
            auto symbol = jit.lookup(names[slot]);
            if (!symbol) {
                throw std::runtime_error(error_text(symbol.takeError()));
            }
            entries[group[slot]] = symbol->toPtr<StaticKernelNativeEntry>();
        }
    }

    std::unique_ptr<llvm::orc::LLJIT> jit_;
    std::unique_ptr<llvm::orc::LLJIT> hot_jit_;
    std::unique_ptr<llvm::orc::LLJIT> warm_jit_;
    std::size_t generation_ { };
};

} // namespace

std::shared_ptr<runtime::simir::StaticKernelCodegen> make_static_kernel_codegen()
{
    return std::make_shared<LlvmStaticKernelCodegen>();
}

} // namespace fsim::compiler

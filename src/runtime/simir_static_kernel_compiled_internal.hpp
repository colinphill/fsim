// SPDX-License-Identifier: Apache-2.0
//
// Helpers shared by the compiled-tier translation units of the engine v4
// static kernel (simir_static_kernel_compiled*.cpp).
#pragma once

#include "simir_static_kernel_state.hpp"
#include "simir_container_helpers.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace fsim::runtime::simir::static_kernel_compiled_detail {

using namespace static_kernel_detail;
namespace kw = kernel_word;


[[nodiscard]] inline bool comparison(const BinaryOperator operation) noexcept
{
    switch (operation) {
    case BinaryOperator::equal:
    case BinaryOperator::case_equal:
    case BinaryOperator::casez_equal:
    case BinaryOperator::casex_equal:
    case BinaryOperator::wildcard_equal:
    case BinaryOperator::not_equal:
    case BinaryOperator::less_unsigned:
    case BinaryOperator::less_equal_unsigned:
    case BinaryOperator::greater_unsigned:
    case BinaryOperator::greater_equal_unsigned:
    case BinaryOperator::less_signed:
    case BinaryOperator::less_equal_signed:
    case BinaryOperator::greater_signed:
    case BinaryOperator::greater_equal_signed:
    case BinaryOperator::vhdl_match_equal:
        return true;
    default:
        return false;
    }
}

struct CompileFailure {
    const char* reason;
};

/// Register operands of the operations the kernel supports.
inline void operation_registers(const Operation& operation,
    std::vector<RegisterId>& reads, std::optional<RegisterId>& write)
{
    reads.clear();
    write.reset();
    visit_operation([&](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, LoadConstant>
            || std::is_same_v<T, ReadSignal>) {
            write = op.destination;
        } else if constexpr (std::is_same_v<T, CopyRegister>
            || std::is_same_v<T, UnaryNot>
            || std::is_same_v<T, ConvertToTwoState>
            || std::is_same_v<T, LogicalNot>
            || std::is_same_v<T, Reduction>
            || std::is_same_v<T, Extract>) {
            reads.push_back(op.source);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Binary>
            || std::is_same_v<T, LogicalBinary>) {
            reads.push_back(op.lhs);
            reads.push_back(op.rhs);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Shift>) {
            reads.push_back(op.value);
            reads.push_back(op.amount);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, DynamicExtract>) {
            reads.push_back(op.source);
            reads.push_back(op.selection.index);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, DynamicPartSelect>) {
            reads.push_back(op.source);
            reads.push_back(op.base);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Insert>) {
            reads.push_back(op.target);
            reads.push_back(op.source);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, DynamicInsert>) {
            reads.push_back(op.target);
            reads.push_back(op.source);
            reads.push_back(op.selection.index);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, DynamicPartInsert>) {
            reads.push_back(op.target);
            reads.push_back(op.source);
            reads.push_back(op.selection.base);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Concatenate>) {
            reads.insert(reads.end(), op.operands.begin(), op.operands.end());
            write = op.destination;
        } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
            reads.push_back(op.condition);
            reads.push_back(op.when_true);
            reads.push_back(op.when_false);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Branch>) {
            reads.push_back(op.condition);
        } else if constexpr (std::is_same_v<T, Assert>) {
            reads.push_back(op.condition);
        } else if constexpr (std::is_same_v<T, IntegerBinary>) {
            reads.push_back(op.lhs);
            reads.push_back(op.rhs);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, IntegerUnary>) {
            reads.push_back(op.source);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, IntegerCheck>) {
            reads.push_back(op.source);
        } else if constexpr (std::is_same_v<T, WriteBlocking>
            || std::is_same_v<T, WriteBlockingSlice>
            || std::is_same_v<T, WriteUpdate>
            || std::is_same_v<T, WriteUpdateSlice>
            || std::is_same_v<T, WriteProjected>
            || std::is_same_v<T, WriteProjectedSlice>) {
            reads.push_back(op.source);
        } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
            || std::is_same_v<T, WriteUpdateDynamicSlice>
            || std::is_same_v<T, WriteProjectedDynamicSlice>) {
            reads.push_back(op.source);
            reads.push_back(op.selection.index);
        } else if constexpr (std::is_same_v<T, WriteBlockingDynamicPartSlice>
            || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
            reads.push_back(op.source);
            reads.push_back(op.selection.base);
        } else if constexpr (std::is_same_v<T, ContainerRead>) {
            reads.push_back(op.index);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
            reads.push_back(op.index);
            reads.push_back(op.source);
            if (op.dynamic_part) {
                reads.push_back(op.dynamic_part->base);
            }
        }
    }, operation);
}

/// Bits [offset, offset + width) of a Logic9 value as a Logic4 word.
[[nodiscard]] inline std::optional<kw::Word> packed_field9(const PackedLogic4& value,
    const std::uint32_t offset, const std::uint32_t width)
{
    std::uint64_t planes[4] { };
    for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
        const auto words = value.logic9_plane_words(plane);
        const auto word = offset / 64U;
        const auto shift = offset % 64U;
        if (word >= words.size()) {
            return kw::all_x(width);
        }
        planes[plane] = words[word] >> shift;
        if (shift != 0U && shift + width > 64U && word + 1U < words.size()) {
            planes[plane] |= words[word + 1U] << (64U - shift);
        }
    }
    return kw::logic9_word(planes[0], planes[1], planes[2], planes[3], width);
}

[[nodiscard]] inline kw::Word packed_field(const PackedLogic4& value,
    const std::uint32_t offset, const std::uint32_t width)
{
    if (value.is_logic9()) {
        // A Logic9 register; codes outside {0, 1, X, Z} leave compiled
        // execution at the instruction reading them.
        if (const auto word = packed_field9(value, offset, width)) {
            return *word;
        }
        throw static_kernel_detail::KernelDeopt { 0xffffffffU, false };
    }
    const auto a = value.aval_words();
    const auto b = value.bval_words();
    const auto word = offset / 64U;
    const auto shift = offset % 64U;
    if (word >= a.size()) {
        return kw::all_x(width);
    }
    auto value_a = a[word] >> shift;
    auto value_b = b[word] >> shift;
    if (shift != 0U && shift + width > 64U && word + 1U < a.size()) {
        value_a |= a[word + 1U] << (64U - shift);
        value_b |= b[word + 1U] << (64U - shift);
    }
    const auto m = kw::mask(width);
    return { value_a & m, value_b & m };
}

/// Registers a compiled instruction reads and writes, and its successors.
inline void kinst_flow(const KInst& inst, const std::uint32_t at,
    const CompiledBody& body, const std::vector<Operation>& operations,
    const std::uint32_t stop, std::vector<std::uint32_t>& use,
    std::optional<std::uint32_t>& def, std::vector<std::uint32_t>& next)
{
    use.clear();
    def.reset();
    next.clear();
    const auto size = static_cast<std::uint32_t>(body.code.size());
    std::vector<RegisterId> reads;
    std::optional<RegisterId> write;
    const auto fall = [&] { next.push_back(at + 1U); };
    switch (inst.op) {
    case KOp::nop:
    case KOp::mark:
    case KOp::mem_bind:
        fall();
        break;
    case KOp::constant:
    case KOp::load_slot:
    case KOp::load_slot9:
    case KOp::load_field:
    case KOp::load_field9:
    case KOp::load_host:
    case KOp::deopt:
        def = inst.d;
        fall();
        break;
    case KOp::copy:
    case KOp::reduce:
    case KOp::unary_not:
    case KOp::logical_not:
    case KOp::two_state:
    case KOp::integer_unary:
    case KOp::extract:
        use.push_back(inst.x);
        def = inst.d;
        fall();
        break;
    case KOp::binary:
    case KOp::logical_binary:
    case KOp::insert:
    case KOp::shift:
    case KOp::integer_binary:
        use.push_back(inst.x);
        use.push_back(inst.y);
        def = inst.d;
        fall();
        break;
    case KOp::concat:
        for (std::uint32_t index = 0U; index < inst.x; ++index) {
            use.push_back(body.concat[inst.aux + index].reg);
        }
        def = inst.d;
        fall();
        break;
    case KOp::conditional:
    case KOp::dynamic_insert:
    case KOp::dynamic_part_insert:
        use.push_back(inst.x);
        use.push_back(inst.y);
        use.push_back(inst.z);
        def = inst.d;
        fall();
        break;
    case KOp::dynamic_extract:
    case KOp::dynamic_part_select:
        if (inst.sub != 1U) {
            use.push_back(inst.x);
        }
        use.push_back(inst.y);
        def = inst.d;
        fall();
        break;
    case KOp::mem_read:
        use.push_back(inst.y);
        def = inst.d;
        fall();
        break;
    case KOp::mem_write:
        use.push_back(inst.x);
        use.push_back(inst.y);
        if (inst.aux != 0U) {
            use.push_back(inst.z);
        }
        fall();
        break;
    case KOp::wide_move:
        if (inst.sub == 1U) {
            use.push_back(inst.y);
        }
        fall();
        break;
    case KOp::store_slot:
    case KOp::store_slot_nba:
    case KOp::store_host:
    case KOp::integer_check:
    case KOp::assert_check:
        use.push_back(inst.x);
        fall();
        break;
    case KOp::store_slot_dynamic:
    case KOp::store_slot_part:
        use.push_back(inst.x);
        use.push_back(inst.y);
        fall();
        break;
    case KOp::store_vhdl:
        use.push_back(inst.x);
        if (inst.sub == 1U) {
            use.push_back(inst.y);
        }
        fall();
        break;
    case KOp::generic:
        operation_registers(operations[inst.x], reads, write);
        for (const auto reg : reads) {
            use.push_back(inst.y + reg);
        }
        if (write) {
            def = inst.y + *write;
        }
        fall();
        break;
    case KOp::jump:
        next.push_back(std::min(inst.d, size));
        break;
    case KOp::branch:
        use.push_back(inst.x);
        next.push_back(std::min(inst.y, size));
        next.push_back(std::min(inst.z, size));
        break;
    case KOp::call:
        if (inst.z != 0U) {
            use.push_back(inst.x);
        }
        next.push_back(std::min(inst.d, size));
        break;
    case KOp::ret:
        if (inst.z != 0U) {
            use.push_back(inst.x);
            for (std::uint32_t slot = 0U; slot < inst.z; ++slot) {
                use.push_back(inst.y + slot);
            }
        }
        for (const auto target : body.return_targets) {
            next.push_back(target == stop ? size
                                                     : std::min(target, size));
        }
        break;
    }
}

} // namespace fsim::runtime::simir::static_kernel_compiled_detail

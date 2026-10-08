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
#include <string_view>
#include <type_traits>
#include <vector>

namespace fsim::runtime::simir::static_kernel_compiled_detail {

using namespace static_kernel_detail;
namespace kw = kernel_word;


/// Behavioral operations at which a thread suspends: other threads may run
/// (and change any state) before it resumes.
template <typename T>
inline constexpr bool behavioral_suspension = std::is_same_v<T, WaitFor>
    || std::is_same_v<T, WaitOn> || std::is_same_v<T, WaitForever>
    || std::is_same_v<T, Fork> || std::is_same_v<T, ForkEnd>
    || std::is_same_v<T, Stop>;

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
        } else if constexpr (std::is_same_v<T, FormatDisplay>) {
            reads.push_back(op.source);
        } else if constexpr (std::is_same_v<T, PlusArgSelect>) {
            write = op.destination;
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
    case KOp::suspend:
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

/// Compiled member bodies as bytes: every integer as a LEB128 varint, field
/// by field (most are small or zero). They are read back only by the same
/// build (the cache key includes static_kernel_compiler_identity).
class BodyWriter {
public:
    explicit BodyWriter(std::string& out) : out_ { out } { }

    void put(std::uint64_t value)
    {
        while (value >= 0x80U) {
            out_.push_back(static_cast<char>((value & 0x7fU) | 0x80U));
            value >>= 7U;
        }
        out_.push_back(static_cast<char>(value));
    }
    void put_signed(const std::int64_t value)
    {
        // Zigzag: small negative values stay short.
        put((static_cast<std::uint64_t>(value) << 1U)
            ^ static_cast<std::uint64_t>(value >> 63));
    }
    template <typename T, typename F>
    void list(const std::vector<T>& values, F&& each)
    {
        put(values.size());
        for (const auto& value : values) {
            each(value);
        }
    }
    template <typename T>
    void integers(const std::vector<T>& values)
    {
        list(values, [&](const T value) { put(static_cast<std::uint64_t>(value)); });
    }
    void body(const static_kernel_detail::CompiledBody& body)
    {
        list(body.code, [&](const KInst& inst) {
            put(static_cast<std::uint64_t>(inst.op));
            put(inst.sub);
            put(inst.flags);
            put(inst.width);
            put(inst.d);
            put(inst.x);
            put(inst.y);
            put(inst.z);
            put(inst.offset);
            put(inst.aux);
            put(inst.imm_a);
            put(inst.imm_b);
        });
        const bool zero = std::ranges::all_of(body.registers,
            [](const kernel_word::Word& word) { return word.a == 0U && word.b == 0U; });
        put(body.registers.size());
        put(zero ? 1U : 0U);
        if (!zero) {
            for (const auto& word : body.registers) {
                put(word.a);
                put(word.b);
            }
        }
        integers(body.register_widths);
        list(body.indices, [&](const DynamicIndex& index) {
            put(index.index);
            put_signed(index.left);
            put_signed(index.right);
            put(index.base_offset);
            put(index.strict ? 1U : 0U);
        });
        list(body.parts, [&](const DynamicPartIndex& part) {
            put_signed(part.left);
            put_signed(part.right);
            put(part.base);
            put(part.base_offset);
            put(part.width);
            put(part.increasing ? 1U : 0U);
            put(part.source_descending ? 1U : 0U);
        });
        list(body.concat, [&](const ConcatOperand& operand) {
            put(operand.reg);
            put(operand.width);
        });
        integers(body.containers);
        put(body.entry);
        list(body.wide_registers, [&](const PackedLogic4& value) {
            put(value.is_logic9() ? 1U : 0U);
            const auto spelling = value.to_msb_string();
            put(spelling.size());
            out_.append(spelling);
        });
        integers(body.return_targets);
        integers(body.live_at_entry);
        put(body.shadow_base);
        integers(body.tracked);
        integers(body.u_mode);
        integers(body.u_operand_begin);
        integers(body.u_operands);
        put(body.call_stack_base);
        integers(body.resume_entries);
    }

private:
    std::string& out_;
};

class BodyReader {
public:
    explicit BodyReader(std::string_view bytes) : bytes_ { bytes } { }

    bool get(std::uint64_t& value)
    {
        const auto* data = reinterpret_cast<const unsigned char*>(bytes_.data());
        // Most values are one byte.
        if (at_ < bytes_.size() && data[at_] < 0x80U) {
            value = data[at_++];
            return true;
        }
        value = 0U;
        // A varint takes at most ten bytes; with that many left, no bounds
        // check per byte.
        const bool room = bytes_.size() - at_ >= 10U;
        for (unsigned shift = 0U; shift < 64U; shift += 7U) {
            if (!room && at_ == bytes_.size()) {
                return false;
            }
            const auto byte = data[at_++];
            value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
            if ((byte & 0x80U) == 0U) {
                return true;
            }
        }
        return false;
    }
    template <typename T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    bool get(T& value)
    {
        std::uint64_t raw { };
        if (!get(raw)) {
            return false;
        }
        if constexpr (std::is_same_v<T, bool>) {
            if (raw > 1U) {
                return false;
            }
            value = raw != 0U;
        } else if constexpr (std::is_enum_v<T>) {
            using U = std::underlying_type_t<T>;
            if (raw > std::numeric_limits<U>::max()) {
                return false;
            }
            value = static_cast<T>(static_cast<U>(raw));
        } else {
            if (raw > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                return false;
            }
            value = static_cast<T>(raw);
        }
        return true;
    }
    bool get_signed(std::int64_t& value)
    {
        std::uint64_t raw { };
        if (!get(raw)) {
            return false;
        }
        value = static_cast<std::int64_t>(raw >> 1U) ^ -static_cast<std::int64_t>(raw & 1U);
        return true;
    }
    /// A count beyond the remaining bytes is corrupt (each item takes one).
    template <typename T, typename F>
    bool list(std::vector<T>& values, F&& each)
    {
        std::uint64_t count { };
        if (!get(count) || count > bytes_.size() - at_) {
            return false;
        }
        values.resize(static_cast<std::size_t>(count));
        for (auto& value : values) {
            if (!each(value)) {
                return false;
            }
        }
        return true;
    }
    template <typename T>
    bool integers(std::vector<T>& values)
    {
        return list(values, [&](T& value) { return get(value); });
    }
    bool body(static_kernel_detail::CompiledBody& body)
    {
        std::uint64_t registers { };
        bool zero { };
        if (!list(body.code, [&](KInst& inst) {
                return get(inst.op) && get(inst.sub) && get(inst.flags)
                    && get(inst.width) && get(inst.d) && get(inst.x) && get(inst.y)
                    && get(inst.z) && get(inst.offset) && get(inst.aux)
                    && get(inst.imm_a) && get(inst.imm_b);
            })
            || !get(registers) || !get(zero)
            || registers > (std::uint64_t { 1 } << 32U)) {
            return false;
        }
        body.registers.assign(static_cast<std::size_t>(registers), { });
        if (!zero) {
            for (auto& word : body.registers) {
                if (!get(word.a) || !get(word.b)) {
                    return false;
                }
            }
        }
        return integers(body.register_widths)
            && list(body.indices, [&](DynamicIndex& index) {
                   return get(index.index) && get_signed(index.left)
                       && get_signed(index.right) && get(index.base_offset)
                       && get(index.strict);
               })
            && list(body.parts, [&](DynamicPartIndex& part) {
                   return get_signed(part.left) && get_signed(part.right)
                       && get(part.base) && get(part.base_offset) && get(part.width)
                       && get(part.increasing) && get(part.source_descending);
               })
            && list(body.concat, [&](ConcatOperand& operand) {
                   return get(operand.reg) && get(operand.width);
               })
            && integers(body.containers) && get(body.entry)
            && list(body.wide_registers, [&](PackedLogic4& value) {
                   bool logic9 { };
                   std::uint64_t size { };
                   if (!get(logic9) || !get(size) || size > bytes_.size() - at_) {
                       return false;
                   }
                   const auto spelling = bytes_.substr(at_, static_cast<std::size_t>(size));
                   at_ += static_cast<std::size_t>(size);
                   try {
                       value = logic9 ? PackedLogic4::from_logic9_msb_string(spelling)
                                      : PackedLogic4::from_msb_string(spelling);
                   } catch (const std::exception&) {
                       return false;
                   }
                   return true;
               })
            && integers(body.return_targets) && integers(body.live_at_entry)
            && get(body.shadow_base) && integers(body.tracked)
            && integers(body.u_mode) && integers(body.u_operand_begin)
            && integers(body.u_operands) && get(body.call_stack_base)
            && integers(body.resume_entries);
    }
    [[nodiscard]] bool done() const noexcept { return at_ == bytes_.size(); }

private:
    std::string_view bytes_;
    std::size_t at_ { };
};

} // namespace fsim::runtime::simir::static_kernel_compiled_detail

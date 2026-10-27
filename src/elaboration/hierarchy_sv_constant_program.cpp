// SPDX-License-Identifier: Apache-2.0
//
// The interpreter of compiled SystemVerilog constant functions
// (hierarchy_sv_constant_program.hpp). Every register holds a known value
// masked to its width; a slot (a formal or local) may also be unknown, and
// reading it abandons the run.
#include "hierarchy_sv_constant_program.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace fsim::elaboration::sv_constant_program {

namespace {

[[nodiscard]] std::int64_t sign_extended(
    const std::uint64_t value, const std::uint32_t width) noexcept
{
    if (width >= 64U) {
        return static_cast<std::int64_t>(value);
    }
    const auto sign = std::uint64_t { 1 } << (width - 1U);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

/// The index operand as the evaluator's integer_value() reads it; nothing
/// when it does not fit a signed 64-bit integer.
[[nodiscard]] std::optional<std::int64_t> index_value(
    const Select& select, const std::uint64_t value) noexcept
{
    if (select.constant_index) {
        return 0;
    }
    if (select.index_signed) {
        return sign_extended(value, select.index_width);
    }
    if (value > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}

/// The offset in the base of the field's bit 0; wide enough for any index.
[[nodiscard]] __int128 field_offset(
    const Select& select, const std::int64_t index) noexcept
{
    return select.index_negated
        ? static_cast<__int128>(select.start) - index
        : static_cast<__int128>(select.start) + index;
}

} // namespace

std::optional<Result> run(
    const Program& program, const std::span<const EntryValue> entry)
{
    const auto count = std::max<std::size_t>(program.registers, 1U);
    std::vector<std::uint64_t> registers(count);
    std::vector<std::uint8_t> known(count);
    std::vector<std::uint32_t> shapes(count);
    for (std::size_t index { }; index < program.entry.size(); ++index) {
        const auto& slot = program.entry[index];
        registers[slot.slot] = entry[index].bits;
        known[slot.slot] = entry[index].known ? 1U : 0U;
        shapes[slot.slot] = slot.shape;
    }
    const auto& ops = program.ops;
    std::size_t pc { };
    while (pc < ops.size()) {
        const auto& op = ops[pc++];
        auto& out = registers[op.a];
        const auto b = registers[op.b];
        const auto c = registers[op.c];
        const auto mask = width_mask(op.width);
        switch (op.code) {
        case OpCode::constant:
            out = op.immediate;
            break;
        case OpCode::copy:
            out = b;
            break;
        case OpCode::store:
            out = b;
            known[op.a] = 1U;
            shapes[op.a] = static_cast<std::uint32_t>(op.immediate);
            break;
        case OpCode::set_unknown:
            known[op.a] = 0U;
            shapes[op.a] = static_cast<std::uint32_t>(op.immediate);
            break;
        case OpCode::require_known:
            if (known[op.a] == 0U) {
                return std::nullopt;
            }
            break;
        case OpCode::require_shape:
            if (shapes[op.a] != op.immediate) {
                return std::nullopt;
            }
            break;
        case OpCode::fill:
            out = (b & 1U) != 0U ? mask : 0U;
            break;
        case OpCode::extend: {
            auto value = b;
            const auto from = op.aux;
            if (op.width > from && op.flag && from != 0U
                && ((value >> (from - 1U)) & 1U) != 0U) {
                value |= mask & ~width_mask(from);
            }
            out = value & mask;
            break;
        }
        case OpCode::negate:
            out = (std::uint64_t { 0 } - b) & mask;
            break;
        case OpCode::invert:
            out = ~b & mask;
            break;
        case OpCode::logical_not:
            out = b == 0U ? 1U : 0U;
            break;
        case OpCode::reduce_and:
            out = b == mask ? 1U : 0U;
            break;
        case OpCode::reduce_or:
            out = b != 0U ? 1U : 0U;
            break;
        case OpCode::reduce_xor:
            out = static_cast<std::uint64_t>(std::popcount(b) & 1);
            break;
        case OpCode::add:
            out = (b + c) & mask;
            break;
        case OpCode::subtract:
            out = (b - c) & mask;
            break;
        case OpCode::multiply:
            out = (b * c) & mask;
            break;
        case OpCode::divide_unsigned:
        case OpCode::modulo_unsigned:
            // Division by zero gives x (IEEE 1800-2017 11.4.2).
            if (c == 0U) {
                return std::nullopt;
            }
            out = (op.code == OpCode::divide_unsigned ? b / c : b % c)
                & mask;
            break;
        case OpCode::divide_signed:
        case OpCode::modulo_signed: {
            const auto dividend = sign_extended(b, op.width);
            const auto divisor = sign_extended(c, op.width);
            const auto minimum = op.width >= 64U
                ? std::numeric_limits<std::int64_t>::min()
                : -(std::int64_t { 1 } << (op.width - 1U));
            if (divisor == 0 || (dividend == minimum && divisor == -1)) {
                return std::nullopt;
            }
            out = static_cast<std::uint64_t>(
                      op.code == OpCode::divide_signed ? dividend / divisor
                                                       : dividend % divisor)
                & mask;
            break;
        }
        case OpCode::bit_and:
            out = b & c;
            break;
        case OpCode::bit_or:
            out = b | c;
            break;
        case OpCode::bit_xor:
            out = b ^ c;
            break;
        case OpCode::bit_xnor:
            out = ~(b ^ c) & mask;
            break;
        case OpCode::equal:
            out = b == c ? 1U : 0U;
            break;
        case OpCode::not_equal:
            out = b != c ? 1U : 0U;
            break;
        case OpCode::less_unsigned:
            out = b < c ? 1U : 0U;
            break;
        case OpCode::less_equal_unsigned:
            out = b <= c ? 1U : 0U;
            break;
        case OpCode::greater_unsigned:
            out = b > c ? 1U : 0U;
            break;
        case OpCode::greater_equal_unsigned:
            out = b >= c ? 1U : 0U;
            break;
        case OpCode::less_signed:
            out = sign_extended(b, op.width) < sign_extended(c, op.width)
                ? 1U : 0U;
            break;
        case OpCode::less_equal_signed:
            out = sign_extended(b, op.width) <= sign_extended(c, op.width)
                ? 1U : 0U;
            break;
        case OpCode::greater_signed:
            out = sign_extended(b, op.width) > sign_extended(c, op.width)
                ? 1U : 0U;
            break;
        case OpCode::greater_equal_signed:
            out = sign_extended(b, op.width) >= sign_extended(c, op.width)
                ? 1U : 0U;
            break;
        case OpCode::shift_left:
        case OpCode::shift_right:
        case OpCode::shift_right_arithmetic: {
            // A count at or beyond the width shifts every bit out.
            const auto amount = c >= op.width ? op.width
                                              : static_cast<std::uint32_t>(c);
            if (op.code == OpCode::shift_left) {
                out = amount >= 64U ? 0U : (b << amount) & mask;
                break;
            }
            auto value = amount >= 64U ? 0U : b >> amount;
            if (op.code == OpCode::shift_right_arithmetic && amount != 0U
                && ((b >> (op.width - 1U)) & 1U) != 0U) {
                value |= mask & ~width_mask(op.width - amount);
            }
            out = value;
            break;
        }
        case OpCode::shift_or:
            out = ((op.aux >= 64U ? 0U : b << op.aux) | c) & mask;
            break;
        case OpCode::select: {
            const auto& select = program.selects[op.immediate];
            const auto index = index_value(select, c);
            if (!index) {
                // A bit-select whose index is no integer reads the default
                // value; a part-select fails.
                if (select.part_select || !select.two_state) {
                    return std::nullopt;
                }
                out = 0U;
                break;
            }
            const auto first = field_offset(select, *index);
            std::uint64_t value { };
            for (std::uint32_t bit { }; bit < select.count; ++bit) {
                const auto offset = first + bit;
                if (offset < 0 || offset >= select.width) {
                    // An x bit has no register value.
                    if (!select.two_state) {
                        return std::nullopt;
                    }
                    continue;
                }
                value |= ((b >> static_cast<std::uint32_t>(offset)) & 1U)
                    << bit;
            }
            out = value;
            break;
        }
        case OpCode::write_select: {
            const auto& select = program.selects[op.immediate];
            const auto index = index_value(select, c);
            if (!index) {
                // A bit write with no integer index is skipped; a
                // part-select write fails.
                if (select.part_select) {
                    return std::nullopt;
                }
                break;
            }
            const auto first = field_offset(select, *index);
            for (std::uint32_t bit { }; bit < select.count; ++bit) {
                const auto offset = first + bit;
                if (offset < 0 || offset >= select.width) {
                    continue;
                }
                const auto position = static_cast<std::uint32_t>(offset);
                out = (out & ~(std::uint64_t { 1 } << position))
                    | (((b >> bit) & 1U) << position);
            }
            break;
        }
        case OpCode::jump:
            pc = static_cast<std::size_t>(op.immediate);
            break;
        case OpCode::jump_if_zero:
            if (out == 0U) {
                pc = static_cast<std::size_t>(op.immediate);
            }
            break;
        case OpCode::jump_if_nonzero:
            if (out != 0U) {
                pc = static_cast<std::size_t>(op.immediate);
            }
            break;
        case OpCode::loop_start:
            out = 0U;
            break;
        case OpCode::loop_step:
            if (++out >= maximum_loop_iterations) {
                return std::nullopt;
            }
            break;
        case OpCode::return_value:
            return Result { out, static_cast<std::uint32_t>(op.immediate) };
        case OpCode::finish:
            if (known[op.a] == 0U) {
                return std::nullopt;
            }
            return Result { out, shapes[op.a] };
        case OpCode::abandon:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace fsim::elaboration::sv_constant_program

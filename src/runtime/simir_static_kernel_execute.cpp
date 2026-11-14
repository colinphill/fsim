// SPDX-License-Identifier: Apache-2.0
//
// KIR execution of the engine v4 static kernel: the interpreter tier,
// deoptimization, the generic bridge and the slow instruction semantics.
#include "simir_static_kernel_compiled_internal.hpp"
#include "fsim/runtime/simir_interpreter.hpp"
#include "fsim/semantic/sva_plan.hpp"
#include "fsim/support/index_distance.hpp"
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_container_value.hpp"
#include "fsim/runtime/simir_ids.hpp"
#include "fsim/runtime/sva_storage.hpp"
#include "simir_container_helpers.hpp"
#include "simir_internal.hpp"
#include "simir_kernel_word_ops.hpp"
#include "simir_operation_kinds.hpp"
#include "simir_static_kernel.hpp"
#include "simir_static_kernel_ir.hpp"
#include "simir_static_kernel_native.hpp"
#include "simir_static_kernel_state.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cxxabi.h>
#include <limits>
#include <map>
#include <typeinfo>
#include <utility>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;
using namespace static_kernel_compiled_detail;
namespace kw = kernel_word;

namespace {

/// Bits offset .. offset + width - 1 (width 1 to 64) of the wide register
/// at storage index `base` with `chunks` chunks, and their 'U' and weak
/// masks when it has masks (packed_field: X past its chunks).
[[nodiscard]] kw::UWord wide_field(const kw::Word* registers,
    const std::uint32_t base, const std::uint32_t chunks, const bool unknown,
    const std::uint32_t offset, const std::uint32_t width) noexcept
{
    const auto word = offset / 64U;
    const auto shift = offset % 64U;
    if (word >= chunks) {
        return { kw::all_x(width), 0U, 0U };
    }
    const auto low = registers[base + word];
    kw::Word value { low.a >> shift, low.b >> shift };
    std::uint64_t mask = unknown ? registers[base + chunks + word].a >> shift : 0U;
    std::uint64_t weak = unknown ? registers[base + chunks + word].b >> shift : 0U;
    if (shift != 0U && shift + width > 64U && word + 1U < chunks) {
        const auto high = registers[base + word + 1U];
        value.a |= high.a << (64U - shift);
        value.b |= high.b << (64U - shift);
        if (unknown) {
            mask |= registers[base + chunks + word + 1U].a << (64U - shift);
            weak |= registers[base + chunks + word + 1U].b << (64U - shift);
        }
    }
    const auto m = kw::mask(width);
    return { { value.a & m, value.b & m }, mask & m, weak & m };
}

} // namespace

void Interpreter::Impl::StaticKernel::execute(
    CompiledBody& body, std::uint32_t member_index)
{
    ++interpreted_bytecode_runs_;
    const InterpretingScope scope { *this, InterpretedCause::bytecode };
    if (members_[member_index].vhdl) {
        try {
            execute_body(body, member_index);
        } catch (const KernelDeopt& deopt) {
            handle_deopt(member_index, body, deopt);
        }
        return;
    }
    execute_body(body, member_index);
}

void Interpreter::Impl::StaticKernel::execute_body(
    CompiledBody& body, std::uint32_t member_index)
{
    const Member* current = &members_[member_index];
    auto* registers = body.registers.data();
    const auto& code = body.code;
    const auto size = static_cast<std::uint32_t>(code.size());
    // A behavioral body starts at the thread's resume point and runs until it
    // reaches a suspension, however long the thread computes before that.
    const bool behavioral = !body.resume_entries.empty();
    std::uint32_t pc = behavioral ? behavioral_entry_ : body.entry;
    std::size_t steps = 0U;
    struct StepRecord {
        std::size_t& steps;
        std::size_t& into;
        ~StepRecord() { into = steps; }
    } step_record { steps, executed_steps_ };
    // SimIR operation index of instruction 0.
    const auto origin = current->vhdl ? 0U : current->body_begin;
    const auto fail_at = [&](const std::uint32_t at, const std::string& message) {
        fail(member_index, origin + at, message);
    };
    std::uint32_t executing = pc;
    // 'U' shadows (VHDL).
    const auto shadow_base = body.shadow_base;
    const auto unknown = [&](const std::uint32_t reg) -> std::uint64_t {
        return shadow_base != 0U && reg < shadow_base && body.tracked[reg] != 0U
            ? registers[shadow_base + reg].a : 0U;
    };
    // The weak mask: the shadow word's `b`, only in a design with weak
    // elements (CompiledBody::weak).
    const bool weak_body = body.weak != 0U;
    const auto weak = [&](const std::uint32_t reg) -> std::uint64_t {
        return weak_body && shadow_base != 0U && reg < shadow_base
                && body.tracked[reg] != 0U
            ? registers[shadow_base + reg].b : 0U;
    };
    const auto set_unknown = [&](const std::uint32_t reg, const std::uint64_t value,
                                 const std::uint64_t weak_value = 0U) {
        if (shadow_base != 0U && reg < shadow_base && body.tracked[reg] != 0U) {
            registers[shadow_base + reg] = { value, weak_value };
        }
    };
    // The instruction before this one, when it stored a VHDL variable a
    // hook names (note_variable_store).
    std::uint32_t stored_variable = no_variable_store;
    try {
    while (pc < size) {
        if (stored_variable != no_variable_store) [[unlikely]] {
            (void)note_variable_store(
                static_cast<std::uint32_t>(current - members_.data()),
                std::exchange(stored_variable, no_variable_store), registers);
        }
        if (++steps > run_step_limit && !behavioral) {
            fail_at(pc, "static kernel member did not terminate");
        }
        const auto& inst = code[pc];
        const auto at = pc;
        executing = at;
        if (variable_hooks_active_ && at < body.variable_stores.size()) [[unlikely]] {
            stored_variable = body.variable_stores[at];
        }
        ++pc;
        const auto mode = body.shadow_base != 0U ? body.u_mode[at]
                                                 : std::uint8_t { 0 };
        if ((mode & u_check) != 0U) {
            for (auto index = body.u_operand_begin[at];
                 index < body.u_operand_begin[at + 1U]; ++index) {
                if (unknown(body.u_operands[index]) != 0U) {
                    throw KernelDeopt { at, false };
                }
            }
        }
        // 'U' mask of the result, from the operands before the instruction
        // overwrites one of them.
        std::uint64_t result_unknown = 0U;
        // The weak mask of the result: the copy-like operations carry it,
        // the logic operations produce none (their tables give 0, 1, X
        // and U only).
        std::uint64_t result_weak = 0U;
        if ((mode & u_aware) != 0U) {
            switch (inst.op) {
            case KOp::constant:
                result_unknown = constant_unknown(inst.offset, inst.aux);
                result_weak = constant_weak(inst.x, inst.y);
                break;
            case KOp::copy:
                result_unknown = unknown(inst.x);
                result_weak = weak(inst.x);
                break;
            case KOp::extract:
                result_unknown = inst.sub >= 2U ? 0U
                    : (unknown(inst.x) >> inst.offset) & kw::mask(inst.width);
                result_weak = inst.sub >= 2U ? 0U
                    : (weak(inst.x) >> inst.offset) & kw::mask(inst.width);
                break;
            case KOp::insert: {
                const auto field = kw::mask(inst.width) << inst.offset;
                result_unknown = (unknown(inst.x) & ~field)
                    | ((unknown(inst.y) << inst.offset) & field);
                result_weak = (weak(inst.x) & ~field)
                    | ((weak(inst.y) << inst.offset) & field);
                break;
            }
            case KOp::concat: {
                std::uint32_t offset = 0U;
                for (std::uint32_t index = inst.x; index-- > 0U;) {
                    const auto& operand = body.concat[inst.aux + index];
                    result_unknown |= (unknown(operand.reg)
                                          & kw::mask(operand.width))
                        << offset;
                    result_weak |= (weak(operand.reg) & kw::mask(operand.width))
                        << offset;
                    offset += operand.width;
                }
                break;
            }
            case KOp::binary: {
                const auto m = kw::mask(inst.width);
                const auto left = unknown(inst.x);
                const auto right = unknown(inst.y);
                switch (static_cast<BinaryOperator>(inst.sub)) {
                case BinaryOperator::bit_and:
                    result_unknown = kw::unknown_and(registers[inst.x], left,
                                         registers[inst.y], right) & m;
                    break;
                case BinaryOperator::bit_or:
                    result_unknown = kw::unknown_or(registers[inst.x], left,
                                         registers[inst.y], right) & m;
                    break;
                case BinaryOperator::vhdl_match_equal:
                    result_unknown = kw::vhdl_match_equal9(registers[inst.x], left,
                        weak(inst.x), registers[inst.y], right, weak(inst.y),
                        inst.width).unknown;
                    break;
                default:
                    result_unknown = (left | right) & m;
                    break;
                }
                break;
            }
            case KOp::unary_not:
                result_unknown = unknown(inst.x);
                break;
            case KOp::shift: {
                // The weak elements move with their words; the mask shifts
                // as a word of its own (an unknown amount gives all X: no
                // weak element).
                const auto moved = kw::shift(static_cast<ShiftOperator>(inst.sub),
                    { weak(inst.x), 0U }, inst.width, registers[inst.y],
                    inst.offset, (inst.flags & flag_signed) != 0U);
                result_weak = moved.a & ~moved.b & kw::mask(inst.width);
                const auto moved_unknown = kw::shift(
                    static_cast<ShiftOperator>(inst.sub), { unknown(inst.x), 0U },
                    inst.width, registers[inst.y], inst.offset,
                    (inst.flags & flag_signed) != 0U);
                result_unknown = moved_unknown.a & ~moved_unknown.b
                    & kw::mask(inst.width);
                break;
            }
            case KOp::conditional: {
                const auto condition = registers[inst.x];
                if ((condition.b & 1U) == 0U) {
                    result_unknown = (condition.a & 1U) != 0U ? unknown(inst.y)
                                                              : unknown(inst.z);
                    result_weak = (condition.a & 1U) != 0U ? weak(inst.y)
                                                           : weak(inst.z);
                }
                break;
            }
            case KOp::dynamic_insert: {
                const auto target = unknown(inst.x);
                const auto source = unknown(inst.y);
                const auto target_weak = weak(inst.x);
                const auto source_weak = weak(inst.y);
                if ((target | source | target_weak | source_weak) == 0U) {
                    break;
                }
                const auto offset = kw::dynamic_index(registers[inst.z],
                    body.indices[inst.aux]);
                if (offset && *offset <= inst.width
                    && inst.offset <= inst.width - *offset) {
                    const auto field = kw::mask(inst.offset) << *offset;
                    result_unknown = (target & ~field)
                        | ((source & kw::mask(inst.offset)) << *offset);
                    result_weak = (target_weak & ~field)
                        | ((source_weak & kw::mask(inst.offset)) << *offset);
                } else if ((inst.flags & flag_strict) != 0U) {
                    throw KernelDeopt { at, false };
                } else {
                    result_unknown = target;
                    result_weak = target_weak;
                }
                break;
            }
            case KOp::dynamic_part_insert: {
                const auto target = unknown(inst.x);
                const auto source = unknown(inst.y);
                const auto target_weak = weak(inst.x);
                const auto source_weak = weak(inst.y);
                if ((target | source | target_weak | source_weak) == 0U) {
                    break;
                }
                const auto& part = body.parts[inst.aux];
                const auto write = kw::dynamic_part_write({ source, source_weak },
                    registers[inst.z], part);
                if (!write.valid) {
                    throw KernelDeopt { at, false };
                }
                if (!write.write) {
                    result_unknown = target;
                    result_weak = target_weak;
                    break;
                }
                if (write.write->offset > inst.width
                    || write.write->width > inst.width - write.write->offset) {
                    throw KernelDeopt { at, false };
                }
                const auto field = kw::mask(write.write->width)
                    << write.write->offset;
                result_unknown = (target & ~field)
                    | ((write.write->value.a & kw::mask(write.write->width))
                        << write.write->offset);
                result_weak = (target_weak & ~field)
                    | ((write.write->value.b & kw::mask(write.write->width))
                        << write.write->offset);
                break;
            }
            default:
                break;
            }
        }
        if (profile_) {
            profile_operations_ += 1U;
        }
        switch (inst.op) {
        case KOp::nop:
        case KOp::mem_bind:
            break;
        case KOp::mark:
            // A gated member of a partition runs only when due
            // (dissolve_member); aux is the next member's mark.
            if (inst.sub != 0U
                && !dissolved_member_due(scratch().running_partition, inst.d)) {
                pc = inst.aux;
                break;
            }
            scratch().running_position = inst.d;
            member_index = inst.x;
            current = &members_[member_index];
            break;
        case KOp::generic:
            // aux 2: an attempt-start report (Display::inert) that does
            // nothing until a library binds cbAssertionStart.
            if (inst.aux == 2U && !impl_.assertion_starts_bound) {
                break;
            }
            execute_generic(body, registers, inst, member_index);
            break;
        case KOp::net_delay:
            execute_net_delay(body, registers, inst, member_index);
            break;
        case KOp::sva_tick:
            execute_sva_tick(body, registers, inst, member_index);
            break;
        case KOp::sva_outcome:
            execute_sva_outcome(registers, inst, member_index);
            break;
        case KOp::sva_sampled_load:
            execute_sva_sampled_load(body, registers, inst, member_index);
            break;
        case KOp::constant:
            registers[inst.d] = { inst.imm_a, inst.imm_b };
            break;
        case KOp::copy:
            registers[inst.d] = registers[inst.x];
            if (weak_body && (mode & u_aware) == 0U) {
                // A Logic4 destination reads '-' (the word Z) as X.
                registers[inst.d] = kw::coerce_weak(registers[inst.x], weak(inst.x));
            }
            break;
        case KOp::load_slot:
            registers[inst.d] = { arena_[inst.x], arena_[inst.x + 1U] };
            break;
        case KOp::load_field:
            registers[inst.d] = read_field(inst.x, inst.offset, inst.width);
            break;
        case KOp::load_host:
            registers[inst.d] = evaluate_slow(body, inst, at, member_index,
                origin, { }, { }, { }, inst.x, &body.wide_registers);
            break;
        case KOp::binary:
            registers[inst.d] = kw::binary(static_cast<BinaryOperator>(inst.sub),
                registers[inst.x], registers[inst.y], inst.width);
            if (static_cast<BinaryOperator>(inst.sub) == BinaryOperator::vhdl_match_equal) {
                // 'U' elements and the weak '-' take part (kernel_word).
                registers[inst.d] = kw::vhdl_match_equal9(registers[inst.x],
                    unknown(inst.x), weak(inst.x), registers[inst.y], unknown(inst.y),
                    weak(inst.y), inst.width).value;
            }
            // 'U' and X differ only in the 'U' mask.
            // A weak element differs from the strong one its word holds
            // (the enumeration compare of std_ulogic values).
            if (static_cast<BinaryOperator>(inst.sub) == BinaryOperator::case_equal
                && (unknown(inst.x) != unknown(inst.y)
                    || weak(inst.x) != weak(inst.y))) {
                registers[inst.d] = { };
            }
            break;
        case KOp::reduce:
            registers[inst.d] = kw::reduce(
                static_cast<ReductionOperator>(inst.sub), registers[inst.x],
                inst.width);
            break;
        case KOp::unary_not:
            registers[inst.d] = kw::unary_not(registers[inst.x], inst.width);
            break;
        case KOp::logical_not:
            registers[inst.d] = kw::logical_not(registers[inst.x]);
            break;
        case KOp::logical_binary:
            registers[inst.d] = kw::logical_binary(
                static_cast<LogicalBinaryOperator>(inst.sub), registers[inst.x],
                registers[inst.y]);
            break;
        case KOp::shift:
            registers[inst.d] = evaluate_slow(body, inst, at, member_index,
                origin, registers[inst.x], registers[inst.y], { },
                0U, &body.wide_registers);
            scratch().last_unknown = result_unknown;
            scratch().last_weak = result_weak;
            break;
        case KOp::extract:
            if (inst.sub == 3U) {
                // A wide register's chunks.
                const auto field = wide_field(registers, inst.x,
                    static_cast<std::uint32_t>(inst.imm_b), inst.imm_a != 0U,
                    inst.offset, inst.width);
                scratch().last_unknown = field.unknown;
                scratch().last_weak = field.weak;
                registers[inst.d] = field.value;
                break;
            }
            registers[inst.d] = inst.sub == 2U
                ? evaluate_slow(body, inst, at, member_index, origin, { }, { }, { },
                      0U, &body.wide_registers)
                : kw::extract(registers[inst.x], inst.offset, inst.width);
            break;
        case KOp::insert:
            registers[inst.d] = kw::insert(registers[inst.x], registers[inst.y],
                inst.offset, inst.width);
            break;
        case KOp::concat: {
            Word result { };
            std::uint32_t offset = 0U;
            for (std::uint32_t index = inst.x; index-- > 0U;) {
                const auto& operand = body.concat[inst.aux + index];
                result = kw::insert(result, registers[operand.reg], offset,
                    operand.width);
                offset += operand.width;
            }
            registers[inst.d] = result;
            break;
        }
        case KOp::conditional:
            registers[inst.d] = kw::conditional(registers[inst.x],
                registers[inst.y], registers[inst.z], inst.width);
            break;
        case KOp::two_state:
            registers[inst.d] = kw::to_two_state(registers[inst.x]);
            break;
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
            scratch().wide_file = registers;
            registers[inst.d] = inst.sub == 1U
                ? evaluate_slow(body, inst, at, member_index,
                      origin, { }, registers[inst.y], { }, inst.x,
                      &body.wide_registers)
                : evaluate_slow(body, inst, at, member_index,
                      origin,
                      inst.sub >= 2U ? Word { } : registers[inst.x],
                      registers[inst.y],
                      inst.sub >= 2U ? Word { }
                                     : Word { unknown(inst.x), weak(inst.x) },
                      0U, &body.wide_registers);
            break;
        case KOp::wide_load:
        case KOp::wide_copy:
        case KOp::wide_const:
        case KOp::wide_concat:
        case KOp::wide_store:
            // A host signal's dynamic write (wide_store sub 3) takes its
            // index or part base from register y.
            execute_wide(body, registers, inst, at, member_index, origin,
                inst.op == KOp::wide_store && inst.sub == 3U && inst.imm_a != 0U
                    ? registers[inst.y] : Word { },
                Word { }, { }, inst.op == KOp::wide_load ? inst.x : inst.d);
            break;
        case KOp::wide_part_insert:
            execute_wide(body, registers, inst, at, member_index, origin,
                registers[inst.y], Word { unknown(inst.y), weak(inst.y) },
                registers[inst.z], 0U);
            break;
        case KOp::wide_alu:
            if (const char* message = execute_wide_alu(body, registers, inst)) {
                fail_at(at, message);
            }
            break;
        case KOp::dynamic_insert:
        case KOp::dynamic_part_insert:
            registers[inst.d] = evaluate_slow(body, inst, at, member_index,
                origin, registers[inst.x], registers[inst.y],
                registers[inst.z], 0U, &body.wide_registers);
            break;
        case KOp::jump:
            pc = inst.d;
            break;
        case KOp::branch: {
            const auto value = kw::bit(inst.sub == 2U
                    ? evaluate_slow(body, inst, at, member_index, origin, { }, { },
                          { }, 0U, &body.wide_registers)
                    : registers[inst.x],
                0U);
            if (value == Logic4::one) {
                pc = inst.y;
            } else if (value == Logic4::zero
                || (inst.flags & flag_linear) != 0U) {
                pc = inst.z;
            } else {
                fail_at(at, "branch condition is unknown or high impedance");
            }
            break;
        }
        case KOp::store_slot:
            write_slot_word(inst.d, registers[inst.x], inst.offset, inst.width,
                member_index);
            break;
        case KOp::store_slot_nba:
        case KOp::store_slot_dynamic:
        case KOp::store_slot_part:
        case KOp::store_host:
            effect_slow(body, inst, at, member_index, origin,
                registers[inst.x],
                inst.op == KOp::store_slot_nba
                        || (inst.op == KOp::store_host && inst.aux == 0U)
                    ? Word { } : registers[inst.y],
                inst.op == KOp::store_host || inst.op == KOp::store_slot_part
                        || inst.op == KOp::store_slot_dynamic
                    ? Word { unknown(inst.x), weak(inst.x) } : Word { },
                inst.d);
            break;
        case KOp::mem_read:
            if (inst.sub == 1U) {
                // An element of more than 64 bits (execute_wide).
                execute_wide(body, registers, inst, at, member_index, origin,
                    registers[inst.y], Word { }, { }, inst.x);
                break;
            }
            registers[inst.d] = evaluate_slow(body, inst, at, member_index,
                origin, { }, registers[inst.y], { }, inst.x,
                &body.wide_registers);
            break;
        case KOp::mem_write:
            if (inst.sub == 1U) {
                execute_wide(body, registers, inst, at, member_index, origin,
                    registers[inst.y], Word { },
                    inst.aux != 0U ? registers[inst.z] : Word { }, inst.d);
                break;
            }
            effect_slow(body, inst, at, member_index, origin,
                registers[inst.x], registers[inst.y],
                inst.aux != 0U ? registers[inst.z] : Word { }, inst.d);
            break;
        case KOp::mem_copy:
            effect_slow(body, inst, at, member_index, origin, { inst.x, 0U },
                { }, { }, inst.d);
            break;
        case KOp::wide_move:
            effect_slow(body, inst, at, member_index, origin, { inst.x, 0U },
                inst.sub == 1U ? registers[inst.y] : Word { }, { }, inst.d);
            break;
        case KOp::load_field9:
            registers[inst.d] = evaluate_slow(body, inst, at, member_index,
                origin, { }, { }, { }, inst.x, &body.wide_registers);
            break;
        case KOp::load_slot9:
            // x is the slot (passed as the resolved binding), not a register.
            registers[inst.d] = evaluate_slow(body, inst, at, member_index,
                origin, { }, { }, { }, inst.x, &body.wide_registers);
            break;
        case KOp::integer_binary:
        case KOp::integer_unary:
            registers[inst.d] = evaluate_slow(body, inst, at, member_index,
                origin, registers[inst.x],
                inst.op == KOp::integer_binary ? registers[inst.y] : Word { },
                { }, inst.x, &body.wide_registers);
            break;
        case KOp::store_vhdl:
            effect_slow(body, inst, at, member_index, origin, registers[inst.x],
                inst.sub == 1U || inst.sub == 3U ? registers[inst.y] : Word { },
                { unknown(inst.x), weak(inst.x) }, inst.d);
            break;
        case KOp::integer_check:
        case KOp::assert_check:
            effect_slow(body, inst, at, member_index, origin, registers[inst.x],
                { }, { }, 0U);
            break;
        case KOp::call: {
            if (inst.z == 0U) {
                effect_slow(body, inst, at, member_index, origin, { }, { }, { },
                    0U);
                pc = inst.d;
                break;
            }
            const auto pointer = registers[inst.x];
            if ((pointer.b & kw::mask(32U)) != 0U) {
                fail_at(at, "call-stack pointer is unknown");
            }
            const auto depth = pointer.a & kw::mask(32U);
            if ((inst.flags & flag_linear) != 0U) {
                // The body's growable stack.
                grow_call_stack(body.call_entries, depth, member_index, origin + at);
                body.call_entries.entries[depth] = inst.imm_a;
                registers[inst.x] = { depth + 1U, 0U };
                pc = inst.d;
                break;
            }
            if (depth >= inst.z) {
                fail_at(at, "call-stack capacity is exhausted");
            }
            registers[inst.y + depth] = { inst.imm_a, 0U };
            registers[inst.x] = { depth + 1U, 0U };
            pc = inst.d;
            break;
        }
        case KOp::ret: {
            if (inst.z == 0U) {
                const auto target = evaluate_slow(body, inst, at, member_index,
                    origin, { }, { }, { }, 0U, &body.wide_registers);
                pc = target.a == current->body_end
                    ? size : static_cast<std::uint32_t>(target.a);
                break;
            }
            const auto pointer = registers[inst.x];
            if ((pointer.b & kw::mask(32U)) != 0U) {
                fail_at(at, "call-stack pointer is unknown");
            }
            const auto depth = pointer.a & kw::mask(32U);
            if ((inst.flags & flag_linear) != 0U) {
                if (depth == 0U) {
                    fail_at(at, "call-stack underflow");
                }
                const auto linear_target = body.call_entries.entries[depth - 1U];
                if (linear_target >= current->operations().size()) {
                    fail_at(at, "call-stack return target is invalid");
                }
                registers[inst.x] = { depth - 1U, 0U };
                pc = linear_target == current->body_end
                    ? size : static_cast<std::uint32_t>(linear_target);
                break;
            }
            if (depth == 0U || depth > inst.z) {
                fail_at(at, "call-stack underflow");
            }
            const auto target = registers[inst.y + depth - 1U];
            if (target.b != 0U || target.a >= current->operations().size()) {
                fail_at(at, "call-stack return target is invalid");
            }
            registers[inst.x] = { depth - 1U, 0U };
            // VHDL bodies map SimIR operation n to instruction n; the wait
            // is the exit.
            pc = target.a == current->body_end ? size
                                               : static_cast<std::uint32_t>(target.a);
            break;
        }
        case KOp::frame_push: {
            const auto& save = body.frame_saves[inst.aux];
            const auto depth = registers[inst.x].a & kw::mask(32U);
            grow_frame_stack(body.frame_entries, depth, save.pairs, member_index,
                origin + at);
            auto* base = body.frame_entries.entries + 2U * depth;
            base[0] = save.identity;
            base[1] = 0U;
            for (auto item = save.item_begin; item < save.item_end; ++item) {
                const auto& entry = body.frame_items[item];
                for (std::uint32_t pair = 0U; pair < entry.count; ++pair) {
                    base[2U * (1U + entry.offset + pair)]
                        = registers[entry.index + pair].a;
                    base[2U * (1U + entry.offset + pair) + 1U]
                        = registers[entry.index + pair].b;
                }
            }
            registers[inst.x] = { depth + save.pairs, 0U };
            if (inst.sub != 0U) {
                // The frame's strings, containers and private arrays.
                frame_storage(member_index, inst.y, true);
            }
            break;
        }
        case KOp::frame_pop: {
            const auto& save = body.frame_saves[inst.aux];
            const auto depth = registers[inst.x].a & kw::mask(32U);
            if (depth < save.pairs
                || body.frame_entries.entries[2U * (depth - save.pairs)]
                    != save.identity) {
                fail_at(at, "automatic callable frame stack mismatch");
            }
            const auto top = depth - save.pairs;
            const auto* base = body.frame_entries.entries + 2U * top;
            for (auto item = save.item_begin; item < save.item_end; ++item) {
                const auto& entry = body.frame_items[item];
                for (std::uint32_t pair = 0U; pair < entry.count; ++pair) {
                    registers[entry.index + pair]
                        = { base[2U * (1U + entry.offset + pair)],
                              base[2U * (1U + entry.offset + pair) + 1U] };
                }
            }
            registers[inst.x] = { top, 0U };
            if (inst.sub != 0U) {
                frame_storage(member_index, inst.y, false);
            }
            break;
        }
        case KOp::suspend:
            behavioral_suspended_ = inst.x;
            return;
        case KOp::step:
            // Operation-tier bodies run only as native code.
            throw std::logic_error { "static kernel bytecode met an operation-tier step" };
        }
        if (mode == 0U) {
            continue;
        }
        if ((mode & u_clear) != 0U) {
            set_unknown(inst.d, 0U);
        } else if ((mode & u_aware) != 0U) {
            switch (inst.op) {
            case KOp::load_slot9:
            case KOp::load_field9:
            case KOp::load_host:
            case KOp::dynamic_extract:
            case KOp::dynamic_part_select:
            case KOp::shift:
                set_unknown(inst.d, scratch().last_unknown, scratch().last_weak);
                break;
            case KOp::extract:
                if (inst.sub >= 2U) {
                    set_unknown(inst.d, scratch().last_unknown, scratch().last_weak);
                } else {
                    set_unknown(inst.d, result_unknown, result_weak);
                }
                break;
            case KOp::store_vhdl:
            case KOp::generic:
            case KOp::net_delay:
            // Wide registers hold their own 'U' masks.
            case KOp::wide_load:
            case KOp::wide_copy:
            case KOp::wide_const:
            case KOp::wide_concat:
            case KOp::wide_store:
            case KOp::wide_part_insert:
            case KOp::wide_alu:
                break;
            default:
                set_unknown(inst.d, result_unknown, result_weak);
                break;
            }
        }
    }
    } catch (KernelDeopt& deopt) {
        if (deopt.resume == 0xffffffffU) {
            deopt.resume = executing;
        }
        throw;
    }
}

void Interpreter::Impl::StaticKernel::handle_deopt(const std::uint32_t member_index,
    CompiledBody& body, const KernelDeopt& deopt)
{
    ++interpreted_deopts_;
    auto& member = members_[member_index];
    if (profile_) {
        ++profile_deopts_;
        profile_member_deopts_.resize(members_.size(), 0U);
        ++profile_member_deopts_[member_index];
        if (member_index < profile_names_.size()
            && deopt.resume < member.operations().size()) {
            std::string key = profile_names_[member_index];
            std::erase_if(key, [](const char c) {
                return std::isdigit(static_cast<unsigned char>(c)) != 0;
            });
            visit_operation([&](const auto& op) {
                int status = 0;
                char* name = abi::__cxa_demangle(typeid(op).name(), nullptr,
                    nullptr, &status);
                std::string text = name != nullptr ? name : "?";
                std::free(name);
                key += " @" + std::to_string(deopt.resume)
                    + (deopt.executed ? "x " : " ")
                    + text.substr(text.rfind(':') + 1U);
            }, member.operations()[deopt.resume]);
            ++profile_generic_ops_["deopt " + key];
        }
    }
    for (std::size_t reg = 0U; reg < member.registers.size(); ++reg) {
        const auto width = body.register_widths[reg];
        if (reg == deopt.keep || width == 0U) {
            continue;
        }
        const auto kind = reg < member.register_kinds.size()
            ? member.register_kinds[reg] : ValueKind::logic4;
        if (width > 64U) {
            if (reg < body.wide_base.size() && body.wide_base[reg] != 0U) {
                member.registers[reg] = wide_value(body, body.registers.data(),
                    static_cast<std::uint32_t>(reg), kind);
            } else if (reg < body.wide_registers.size()) {
                member.registers[reg] = body.wide_registers[reg];
            }
            continue;
        }
        const bool shadowed = body.shadow_base != 0U && body.tracked[reg] != 0U;
        const auto unknown = shadowed ? body.registers[body.shadow_base + reg].a : 0U;
        const auto weak = shadowed && body.weak != 0U
            ? body.registers[body.shadow_base + reg].b : 0U;
        member.registers[reg] = uword_value(body.registers[reg], unknown,
            width, kind, weak);
    }
    if (body.call_stack_base != 0U) {
        // The compiled stack's live entries continue on the runtime stack.
        const auto depth = body.registers[body.call_stack_base].a;
        member.call_stack.clear();
        for (std::uint64_t entry = 0U; entry < depth; ++entry) {
            member.call_stack.push_back(
                static_cast<InstructionIndex>(body.call_entries.entries[entry]));
        }
        if (body.frame_stack_base != 0U) {
            // The compiled frames continue as the reference's.
            const auto pairs = body.registers[body.frame_stack_base].a;
            export_compiled_frames(member_index, body, body.registers,
                body.frame_entries.entries, pairs, member.frames);
            member.frame_bytes = 0U;
            for (const auto& frame : member.frames) {
                member.frame_bytes += frame.bytes;
            }
            body.registers[body.frame_stack_base] = { };
        }
        body.registers[body.call_stack_base] = { };
    }
    const InterpretingScope scope { *this, InterpretedCause::deopt };
    run_generic_from(member_index, deopt.executed ? deopt.resume + 1U
                                                  : deopt.resume);
    sync_compiled_registers(member_index);
}

bool Interpreter::Impl::StaticKernel::wide_part_insert(CompiledBody& body,
    const Word* registers, const DynamicPartInsert& insert,
    const std::uint32_t base, const bool logic9_source)
{
    // A narrow, known field written into a wide register in place (the
    // reference path copies the whole value twice). A field without
    // unknown ('U'-tracked) bits holds its Logic4 value, which insert_word
    // promotes into a Logic9 target as the reference operand would be.
    // Anything else - unknown bits, mixed kinds, a write outside the
    // target - takes that path.
    const auto destination = base + insert.destination;
    const auto target = base + insert.target;
    const auto source = base + insert.source;
    const auto index = base + insert.selection.base;
    const auto& widths = body.register_widths;
    // A field with 'U' or weak elements takes the reference path (the
    // wide target's Logic9 planes are the reference's).
    const auto unknown = [&](const std::uint32_t reg) -> std::uint64_t {
        return body.shadow_base != 0U && reg < body.tracked.size()
                && body.tracked[reg] != 0U
            ? registers[body.shadow_base + reg].a
                | (body.weak != 0U ? registers[body.shadow_base + reg].b : 0U)
            : 0U;
    };
    const auto chunked = [&](const std::uint32_t reg) {
        return reg < body.wide_base.size() && body.wide_base[reg] != 0U;
    };
    if (destination >= widths.size() || target >= widths.size()
        || source >= widths.size() || index >= widths.size()
        || chunked(destination) || chunked(target)
        || widths[destination] <= 64U || widths[target] != widths[destination]
        || widths[source] > 64U || widths[source] != insert.selection.width
        || widths[index] != 32U || unknown(source) != 0U
        || unknown(index) != 0U
        || body.wide_registers[target].is_logic9() != logic9_source) {
        return false;
    }
    const auto write = kw::dynamic_part_write(registers[source],
        registers[index], insert.selection);
    // The bound is the target value's own width: a register without a fixed
    // width (polymorphic) has none in register_widths.
    const auto target_width = body.wide_registers[target].width();
    if (!write.valid
        || (write.write
            && (write.write->offset > target_width
                || write.write->width > target_width - write.write->offset))) {
        return false;
    }
    if (destination != target) {
        body.wide_registers[destination] = body.wide_registers[target];
    }
    if (write.write) {
        body.wide_registers[destination].insert_word(
            Logic4Word { write.write->width, write.write->value.a,
                write.write->value.b },
            write.write->offset);
    }
    return true;
}

PackedLogic4 Interpreter::Impl::StaticKernel::register_operand(
    const CompiledBody& body, const Word* registers,
    const std::uint32_t member_index, const std::uint32_t base,
    const RegisterId reg) const
{
    const auto& member = members_[member_index];
    const auto index = base + reg;
    const auto width = body.register_widths[index];
    if (width <= 64U) {
        const auto kind = reg < member.register_kinds.size()
            ? member.register_kinds[reg] : ValueKind::logic4;
        const bool shadowed = body.shadow_base != 0U
            && index < body.tracked.size() && body.tracked[index] != 0U;
        const auto unknown = shadowed ? registers[body.shadow_base + index].a : 0U;
        const auto weak = shadowed && body.weak != 0U
            ? registers[body.shadow_base + index].b : 0U;
        return uword_value(registers[index], unknown, width, kind, weak);
    }
    if (index < body.wide_base.size() && body.wide_base[index] != 0U) {
        const auto kind = reg < member.register_kinds.size()
            ? member.register_kinds[reg] : ValueKind::logic4;
        return wide_value(body, registers, index, kind);
    }
    return body.wide_registers[index];
}

void Interpreter::Impl::StaticKernel::generic_load_operands(CompiledBody& body,
    const Word* registers, const KInst& inst, const std::uint32_t member_index,
    const std::vector<RegisterId>& reads)
{
    auto& member = members_[member_index];
    for (const auto reg : reads) {
        member.registers[reg]
            = register_operand(body, registers, member_index, inst.y, reg);
    }
}

void Interpreter::Impl::StaticKernel::execute_projected_write(
    const CompiledBody& body, const Word* registers, const KInst& inst,
    const std::uint32_t member_index)
{
    // KOp::net_delay, sub 1: y the operation. The operation's registers
    // (sources, run-time delays, a dynamic index) are in the register file.
    projected_operation(member_index, inst.y,
        [&](const RegisterId reg) {
            return register_operand(body, registers, member_index, 0U, reg);
        });
}

void Interpreter::Impl::StaticKernel::generic_load_written(CompiledBody& body,
    const Word* registers, const KInst& inst, const std::uint32_t member_index,
    const std::vector<RegisterId>& reads, const std::vector<WrittenRegister>& writes)
{
    // An operation with several results (a file scan's targets and success,
    // a line read's target) may leave some of them as they are: they take the
    // compiled register's value first, so that storing every result back
    // changes nothing it did not write.
    if (writes.size() < 2U) {
        return;
    }
    auto& member = members_[member_index];
    for (const auto& written : writes) {
        if (std::ranges::find(reads, written.reg) != reads.end()) {
            continue;
        }
        member.registers[written.reg]
            = register_operand(body, registers, member_index, inst.y, written.reg);
    }
}

void Interpreter::Impl::StaticKernel::execute_net_delay(
    const CompiledBody& body, const Word* registers, const KInst& inst,
    const std::uint32_t member_index)
{
    // KOp::net_delay: x the source register, y the operation. The kernel's
    // delayed write holds its pending changes as timers.
    if (inst.sub == 1U) {
        // A VHDL projected assignment (IEEE 1076-2019 10.5.2.2).
        execute_projected_write(body, registers, inst, member_index);
        return;
    }
    auto value = register_operand(body, registers, member_index, 0U, inst.x);
    if (auto* const lane = worker_lane_; lane != nullptr) {
        // A worker's write waits for the leader's replay (emit_lane_output),
        // which schedules it at the member's place in the serial order.
        LaneOutput output;
        output.kind = LaneOutput::Kind::net_delay;
        output.member = member_index;
        output.pc = inst.y;
        output.value = std::move(value);
        lane->outputs.push_back(std::move(output));
        return;
    }
    net_delay_write(member_index, inst.y,
        operation_get<WriteNetDelay>(members_[member_index].operations()[inst.y]),
        value);
}


bool Interpreter::Impl::StaticKernel::generic_store_result(CompiledBody& body,
    Word* registers, const KInst& inst, const std::uint32_t member_index,
    const std::optional<RegisterId> write, const bool two_state_code)
{
    if (!write) {
        return false;
    }
    auto& member = members_[member_index];
    const auto index = inst.y + *write;
    const auto width = body.register_widths[index];
    auto& value = member.registers[*write];
    if (width > 64U) {
        if (index < body.wide_base.size() && body.wide_base[index] != 0U) {
            set_wide_value(body, registers, index, value);
        } else {
            body.wide_registers[index] = value;
        }
        return false;
    }
    if (value.width() != width) {
        fail(member_index, inst.x,
            "static kernel generic result width is inconsistent");
    }
    const auto word = exact_uword(value);
    const bool tracked = body.shadow_base != 0U
        && index < body.tracked.size() && body.tracked[index] != 0U;
    // A generic result is a source of the tracking (track_unknowns), so
    // every Logic9 result has its mask words; a Logic4 destination keeps
    // none (its value holds no 'U' or weak element).
    if (word && word->weak != 0U && body.weak == 0U) {
        // design_may_hold_weak is conservative: no source of a weak element
        // is missing from a design without weak support.
        throw std::logic_error { "static kernel met a weak std_logic value in a body without weak support" };
    }
    if (!word || ((word->unknown | word->weak) != 0U && !tracked)) {
        throw KernelDeopt { inst.x, true, *write };
    }
    registers[index] = word->value;
    if (tracked) {
        registers[body.shadow_base + index] = { word->unknown,
            body.weak != 0U ? word->weak : 0U };
    }
    // Two-state code cannot continue with a result with X or 'U' bits.
    return two_state_code && (word->value.b != 0U || word->unknown != 0U);
}

bool Interpreter::Impl::StaticKernel::generic_store_results(CompiledBody& body,
    Word* registers, const KInst& inst, const std::uint32_t member_index,
    const std::vector<WrittenRegister>& writes, const bool two_state_code)
{
    // Every register the operation wrote (a file operation's count, status
    // and targets) goes back to the compiled register file.
    bool stop = false;
    for (const auto& written : writes) {
        stop = generic_store_result(body, registers, inst, member_index,
                   std::optional<RegisterId> { written.reg }, two_state_code)
            || stop;
    }
    return stop;
}

void Interpreter::Impl::StaticKernel::note_generic_profile(
    const CompiledBody& body, const KInst& inst, const std::uint32_t member_index,
    const char* mangled, const std::vector<RegisterId>& regs,
    const std::optional<RegisterId> out)
{
    int status = 0;
    char* name = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    std::string key = name != nullptr ? name : "?";
    std::free(name);
    key = key.substr(key.rfind(':') + 1U);
    key += " w=";
    for (const auto reg : regs) {
        key += std::to_string(body.register_widths[inst.y + reg]) + ",";
    }
    if (out) {
        key += "->" + std::to_string(body.register_widths[inst.y + *out]);
    }
    if (member_index < profile_names_.size()) {
        // The member, instance indices dropped (as for evaluate).
        auto who = " " + profile_names_[member_index];
        std::erase_if(who, [](const char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
        key += who;
    }
    ++profile_generic_ops_[key];
}

bool Interpreter::Impl::StaticKernel::execute_generic(CompiledBody& body,
    Word* registers, const KInst& inst, const std::uint32_t member_index,
    const bool two_state_code)
{
    auto& member = members_[member_index];
    const auto& operation = member.operations()[inst.x];
    auto& reads = scratch().generic_reads;
    std::optional<RegisterId> write;
    operation_registers(operation, reads, write);
    auto& writes = scratch().generic_writes;
    operation_written_registers(operation, writes);
    if (profile_) {
        visit_operation([&](const auto& op) {
            note_generic_profile(body, inst, member_index, typeid(op).name(),
                reads, write);
        }, operation);
    }
    if (const auto* insert = operation_get_if<DynamicPartInsert>(&operation);
        insert != nullptr
        && wide_part_insert(body, registers, *insert, inst.y,
            insert->source < member.register_kinds.size()
                && member.register_kinds[insert->source] == ValueKind::logic9)) {
        return false;
    }
    generic_load_operands(body, registers, inst, member_index, reads);
    generic_load_written(body, registers, inst, member_index, reads, writes);
    if (shadow_ != nullptr && !shadow_->reference) {
        shadow_note_reads(member_index, reads);
    }
    // Behavioral runtime-library operations (output, strings, plusargs)
    // run on the thread's reference state.
    if (member.kind != StaticKernelMemberKind::behavioral
        || !behavioral_step(member_index, inst.x)) {
        (void)step_generic(member_index, inst.x);
    }
    if (shadow_ != nullptr && !shadow_->reference) {
        shadow_record_generic(member_index, inst.x, writes);
    }
    return generic_store_results(body, registers, inst, member_index, writes,
        two_state_code);
}

template <std::size_t K>
bool Interpreter::Impl::StaticKernel::execute_generic_kind(CompiledBody& body,
    Word* registers, const KInst& inst, const std::uint32_t member_index,
    const bool two_state_code)
{
    using T = OperationKindAt<K>;
    auto& member = members_[member_index];
    const auto& op = operation_get<T>(member.operations()[inst.x]);
    auto& reads = scratch().generic_reads;
    std::optional<RegisterId> write;
    operation_registers_of(op, reads, write);
    auto& writes = scratch().generic_writes;
    operation_written_registers_of(op, writes);
    if (profile_) {
        note_generic_profile(body, inst, member_index, typeid(T).name(), reads,
            write);
    }
    if constexpr (std::is_same_v<T, DynamicPartInsert>) {
        if (wide_part_insert(body, registers, op, inst.y,
                op.source < member.register_kinds.size()
                    && member.register_kinds[op.source] == ValueKind::logic9)) {
            return false;
        }
    }
    generic_load_operands(body, registers, inst, member_index, reads);
    generic_load_written(body, registers, inst, member_index, reads, writes);
    if (shadow_ != nullptr && !shadow_->reference) {
        shadow_note_reads(member_index, reads);
    }
    if (member.kind != StaticKernelMemberKind::behavioral
        || !(this->*behavioral_operation_steps()[K])(member_index, inst.x)) {
        (void)(this->*operation_steps()[K])(member_index, inst.x);
    }
    if (shadow_ != nullptr && !shadow_->reference) {
        shadow_record_generic(member_index, inst.x, writes);
    }
    return generic_store_results(body, registers, inst, member_index, writes,
        two_state_code);
}

template <std::size_t K>
void Interpreter::Impl::StaticKernel::native_generic_kind(
    StaticKernelNativeFrame* frame, Word* registers, const std::uint32_t at)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    if (kernel.profile_) {
        ++kernel.profile_native_calls_[3];
    }
    try {
        const auto& program = *static_cast<const CompiledBody*>(frame->program);
        kernel.scratch().running_position = frame->position;
        if (kernel.execute_generic_kind<K>(
                *static_cast<CompiledBody*>(frame->instance), registers,
                program.code[at], frame->member, kernel.scratch().two_state_running)) {
            // The template's full code continues after the instruction.
            frame->reserved = at + 1U;
            frame->status = 4U;
        }
    } catch (const KernelDeopt& deopt) {
        kernel.scratch().pending_deopt = deopt;
        frame->status = 2U;
    } catch (...) {
        kernel.scratch().native_exception = std::current_exception();
        frame->status = 1U;
    }
}

void Interpreter::Impl::StaticKernel::plan_sva_table()
{
    // One entry per (member, directive site), members in order: the
    // instruction's aux is the directive's ordinal in its member
    // (sva_site_ordinal at lowering), so member_base + aux names the entry
    // whatever instance of a shared body runs.
    sva_member_base_.assign(members_.size(), 0U);
    std::uint32_t total = 0U;
    std::vector<std::uint32_t> sites;
    Operation scratch;
    // An assertion control read (SvaKind::control) has no block: its word
    // is the process's control site (Impl::assertion_controls).
    const auto note = [&](const Operation& operation) {
        if (const auto* sva = operation_get_if<SvaOperation>(&operation);
            sva != nullptr && sva->kind != SvaKind::control
            && std::ranges::find(sites, sva->site) == sites.end()) {
            sites.push_back(sva->site);
        }
    };
    // The host member's sites are those of its original program, which
    // the scheduler's stub replaced (host_program_).
    impl_.plan_assertion_controls(host_process_,
        host_program_ ? &*host_program_ : nullptr);
    sva_control_base_.assign(members_.size(), 0U);
    for (std::size_t index = 0U; index < members_.size(); ++index) {
        const auto process = members_[index].process;
        if (process < impl_.assertion_control_base.size()
            && impl_.assertion_control_base[process]
                != Impl::no_assertion_control) {
            sva_control_base_[index] = impl_.assertion_control_base[process];
        }
    }
    for (std::size_t index = 0U; index < members_.size(); ++index) {
        sva_member_base_[index] = total;
        sites.clear();
        const auto& member = members_[index];
        if (member.operations_source != nullptr) {
            // A member restored from the image keeps reading its program's
            // operations: the scan does not copy them (operations()).
            const auto& operations = member.operations_source->processes
                .program_view(member.process).operations();
            for (std::size_t op = 0U; op < operations.size(); ++op) {
                note(operations.expanded_ref(op, scratch));
            }
        } else {
            for (const auto& operation : member.operations()) {
                note(operation);
            }
        }
        total += static_cast<std::uint32_t>(sites.size());
    }
    sva_table_used_ = false;
    sva_table_ = StaticKernelSvaTable { };
    sva_table_.pop_serial = &impl_.sva_pop_serial;
    sva_table_.rings = impl_.sampled_ring_words.data();
    sva_table_.sampled = impl_.sampled_value_words.data();
    sva_table_.now = &sva_now_;
    sva_table_.control_base = sva_control_base_.data();
    sva_table_.controls = impl_.assertion_controls.data();
    sva_table_.history = history_words_.data();
    if (total == 0U) {
        // No directive; an inline sampled read in procedural code still
        // reads the sampled words and rings through the table.
        sva_blocks_.reset();
        return;
    }
    sva_blocks_ = std::make_shared<std::vector<std::int64_t*>>(total, nullptr);
    sva_table_.blocks = sva_blocks_->data();
    sva_table_.member_base = sva_member_base_.data();
}

void Interpreter::Impl::StaticKernel::reset_sva_blocks() noexcept
{
    if (sva_blocks_) {
        std::ranges::fill(*sva_blocks_, nullptr);
    }
}

void Interpreter::Impl::StaticKernel::execute_sva_outcome(Word* registers,
    const KInst& inst, const std::uint32_t member_index)
{
    // The ring pop of sva::AttemptStorage::pop_outcome, inline on the block
    // (the same words generated code reads): the outcome code, with the
    // attempt's start, the fail position and the pop serial left in the
    // header for the host's callback (39.4.2), and outcome_action_off added
    // when the action controls skip the outcome's action block (20.11).
    using namespace sva::layout;
    auto* const block = sva_block(inst, member_index);
    const auto record = [&] {
        if (shadow_ != nullptr && !shadow_->reference) {
            shadow_->sva_outcomes.emplace_back(inst.x,
                PackedLogic4::from_aval_bval(32,
                    static_cast<std::uint32_t>(registers[inst.d].a), 0U));
        }
    };
    if (block == nullptr || block[h_outcomes_count] == 0) {
        registers[inst.d] = { };
        record();
        return;
    }
    const auto head = static_cast<std::uint64_t>(block[h_outcomes_head]);
    const auto capacity = static_cast<std::uint64_t>(block[h_outcomes_cap]);
    const auto row = static_cast<std::uint64_t>(block[h_outcomes_off])
        + head * outcome_stride;
    registers[inst.d] = { static_cast<std::uint64_t>(
                              outcome_code(block[row], block[h_flags])),
        0U };
    block[h_last_start] = block[row + 1U];
    block[h_last_fail_line] = block[row + 2U];
    block[h_last_fail_column] = block[row + 3U];
    block[h_outcomes_head] = static_cast<std::int64_t>((head + 1U) & (capacity - 1U));
    block[h_outcomes_count] -= 1;
    block[h_pop_serial] = ++impl_.sva_pop_serial;
    record();
}

void Interpreter::Impl::StaticKernel::note_sva_code(
    const std::size_t index) noexcept
{
    const auto& compiled = members_[index].compiled;
    if (compiled
        && std::ranges::any_of(compiled->code, [](const KInst& inst) {
               return inst.op == KOp::sva_tick || inst.op == KOp::sva_outcome
                   || inst.op == KOp::sva_sampled_load;
           })) {
        sva_table_used_ = true;
    }
}

namespace {

/// The planes of a history ring entry (Impl::SampledRing) or of a signal's
/// default (an empty history) as a reader of words: plane `plane`, chunk
/// `chunk`. A ring entry holds two planes per 64 bit chunk for a Logic4
/// signal and four for a Logic9 one, plane after plane; the default is the
/// same words in the body's wide constants, two to a pair.
struct SampledEntryReader {
    // The words of Impl::SampledRing the entries are addressed by.
    static constexpr std::size_t capacity_word { 0U };
    static constexpr std::size_t first_word { 1U };
    static constexpr std::size_t value_words_word { 3U };
    static constexpr std::size_t header_words { 12U };

    const std::uint64_t* ring;
    const kernel_word::Word* defaults;
    std::uint64_t chunks;
    std::uint64_t planes;

    [[nodiscard]] std::uint64_t entry(const std::uint64_t index,
        const std::uint64_t plane, const std::uint64_t chunk) const
    {
        const auto slot = (ring[first_word] + index) % ring[capacity_word];
        return ring[header_words + slot * ring[value_words_word]
            + plane * chunks + chunk];
    }
    [[nodiscard]] std::uint64_t fallback(const std::uint64_t plane,
        const std::uint64_t chunk) const
    {
        const auto flat = plane * chunks + chunk;
        return (flat & 1U) != 0U ? defaults[flat / 2U].b : defaults[flat / 2U].a;
    }
    [[nodiscard]] std::uint64_t word(const bool have, const std::uint64_t index,
        const std::uint64_t plane, const std::uint64_t chunk) const
    {
        return have ? entry(index, plane, chunk) : fallback(plane, chunk);
    }
    /// The value of a chunk of `bits` elements as Logic4 words: a Logic9
    /// entry reads through the Logic9 to Logic4 mapping (kernel_word::
    /// logic9_coerced).
    [[nodiscard]] kernel_word::Word coerced(const bool have,
        const std::uint64_t index, const std::uint64_t chunk,
        const std::uint32_t bits) const
    {
        if (planes == 4U) {
            return kw::logic9_coerced(word(have, index, 0U, chunk),
                word(have, index, 1U, chunk), word(have, index, 2U, chunk),
                word(have, index, 3U, chunk), bits);
        }
        const auto m = kw::mask(bits);
        return { word(have, index, 0U, chunk) & m, word(have, index, 1U, chunk) & m };
    }
};

} // namespace

void Interpreter::Impl::StaticKernel::execute_sva_sampled_load(
    const CompiledBody& body, Word* registers, const KInst& inst,
    const std::uint32_t member_index)
{
    // The reads of run_sampled_read over the host's sampled words and rings.
    // The Logic4 signals of at most 64 bits read their one word pair; a wide
    // or Logic9 signal's ring (aux bit 1) reads its planes by chunk.
    const auto m = kw::mask(inst.width);
    if (inst.aux == 8U) {
        // An assertion control read (IEEE 1800-2023 20.11): the word of
        // site x of the member's process (Impl::assertion_controls).
        const auto index = std::size_t { member_index < sva_control_base_.size()
                                   ? sva_control_base_[member_index] : 0U }
            + inst.x;
        const auto word = index < impl_.assertion_controls.size()
            ? impl_.assertion_controls[index]
            : semantic::sva::control_default;
        registers[inst.d] = { static_cast<std::uint64_t>(word) & m, 0U };
        return;
    }
    if (inst.aux == 16U) {
        // A history attribute of a kernel signal (IEEE 1076-2019 16.2.4):
        // sub 0 'EVENT, 1 'ACTIVE, 2 'LAST_EVENT, 3 'LAST_ACTIVE.
        const bool transaction = (inst.sub & 1U) != 0U;
        const auto* const record = history_words_.data() + inst.x;
        const auto now = static_cast<std::uint64_t>(kernel_now());
        const bool has = (record[0] & (transaction ? 2U : 1U)) != 0U;
        const auto stamp_time = record[transaction ? 3U : 1U];
        std::uint64_t value { };
        if (inst.sub < 2U) {
            value = has && stamp_time == now
                    && record[transaction ? 4U : 2U] == history_words_[0]
                ? 1U : 0U;
        } else {
            value = has ? now - stamp_time
                        : static_cast<std::uint64_t>(
                              std::numeric_limits<std::int64_t>::max());
        }
        registers[inst.d] = { value & m, 0U };
        return;
    }
    if (inst.aux == 4U) {
        // $time (sub 0) or $stime (sub 1): the time step in the unit's ticks
        // (imm_a each), rounded half up (systemverilog_time_function).
        const auto now = static_cast<std::uint64_t>(kernel_now());
        auto units = now / inst.imm_a;
        if (now % inst.imm_a >= inst.imm_a / 2U + inst.imm_a % 2U) {
            ++units;
        }
        registers[inst.d] = { units & m, 0U };
        return;
    }
    if ((inst.aux & 1U) != 0U) {
        // $sampled: the Preponed value (16.5.1).
        const auto* const words = impl_.sampled_value_words.data() + inst.x;
        registers[inst.d] = { words[0] & m, words[1] & m };
        return;
    }
    using Ring = Impl::SampledRing;
    const auto* const ring = impl_.sampled_ring_words.data() + inst.x;
    // Whether the clock's sample of this time step is in the ring (the
    // clock's publication appended it): this time step is a tick (16.9.3).
    const auto sampled_now_in = [this](const std::uint64_t* const words) {
        return words[Ring::slot_valid] != 0U
            && words[Ring::slot_time] == static_cast<std::uint64_t>(kernel_now())
            && words[Ring::slot_process_domain]
                == static_cast<std::uint64_t>(ProcessSchedulingDomain::systemverilog);
    };
    static_assert(SampledEntryReader::capacity_word == Ring::capacity
        && SampledEntryReader::first_word == Ring::first
        && SampledEntryReader::value_words_word == Ring::value_words
        && SampledEntryReader::header_words == Ring::header_words);
    if ((inst.aux & 2U) != 0U) {
        const auto chunks = inst.imm_a & 0xffff'ffffU;
        const SampledEntryReader reader { ring, body.wide_constants.data() + inst.imm_b,
            chunks, (inst.imm_a >> 32U) != 0U ? 4U : 2U };
        const auto count_now = ring[Ring::count];
        const auto kind_now = static_cast<SignalReadKind>(inst.sub);
        const auto chunk_bits = [&](const std::uint64_t chunk) {
            return static_cast<std::uint32_t>(
                std::min<std::uint64_t>(64U, inst.offset - 64U * chunk));
        };
        if (kind_now == SignalReadKind::sampled) {
            registers[inst.d] = reader.coerced(count_now >= 1U, count_now - 1U, 0U,
                chunk_bits(0U));
            registers[inst.d].a &= m;
            registers[inst.d].b &= m;
            return;
        }
        if (kind_now == SignalReadKind::past) {
            const auto prior = count_now - (sampled_now_in(ring) ? 1U : 0U);
            registers[inst.d] = reader.coerced(prior >= inst.z, prior - inst.z, 0U,
                chunk_bits(0U));
            registers[inst.d].a &= m;
            registers[inst.d].b &= m;
            return;
        }
        // In a time step that is not a tick of the clock the current value
        // is the Preponed one (y - 1 in the sampled words, Logic4) and the
        // previous one the ring's last sample (16.9.3). A SystemVerilog
        // reader compares the values it reads: a Logic9 entry through the
        // Logic4 mapping (21.2.1.3; strength is not part of the value).
        const bool between_ticks = inst.y != 0U && !sampled_now_in(ring);
        const auto* const preponed = between_ticks
            ? impl_.sampled_value_words.data() + (inst.y - 1U) : nullptr;
        const auto previous_back = between_ticks ? 1U : 2U;
        const bool have_current = count_now >= 1U;
        const bool have_previous = count_now >= previous_back;
        const auto current_index = count_now - 1U;
        const auto previous_index = count_now - previous_back;
        const auto current_value = [&](const std::uint64_t chunk) {
            if (preponed == nullptr) {
                return reader.coerced(have_current, current_index, chunk,
                    chunk_bits(chunk));
            }
            const auto bits_mask = kw::mask(chunk_bits(chunk));
            return Word { preponed[chunk] & bits_mask,
                preponed[chunks + chunk] & bits_mask };
        };
        const auto previous_value = [&](const std::uint64_t chunk) {
            return reader.coerced(have_previous, previous_index, chunk,
                chunk_bits(chunk));
        };
        if (kind_now == SignalReadKind::rose || kind_now == SignalReadKind::fell) {
            const auto current = current_value(0U);
            const auto previous = previous_value(0U);
            const bool rose = kind_now == SignalReadKind::rose;
            const auto level = [&](const Word word) {
                return rose ? (word.a & 1U) != 0U && (word.b & 1U) == 0U
                            : (word.a & 1U) == 0U && (word.b & 1U) == 0U;
            };
            registers[inst.d] = { level(current) && !level(previous) ? 1U : 0U, 0U };
            return;
        }
        // $stable and $changed compare every chunk of the values read.
        bool equal = true;
        for (std::uint64_t chunk = 0U; chunk < chunks && equal; ++chunk) {
            const auto current = current_value(chunk);
            const auto previous = previous_value(chunk);
            equal = current.a == previous.a && current.b == previous.b;
        }
        const bool result = kind_now == SignalReadKind::stable ? equal : !equal;
        registers[inst.d] = { result ? 1U : 0U, 0U };
        return;
    }
    const auto capacity = ring[Ring::capacity];
    const auto first = ring[Ring::first];
    const auto count = ring[Ring::count];
    const auto value_words = ring[Ring::value_words];
    const Word fallback { inst.imm_a, inst.imm_b };
    const auto value = [&](const std::uint64_t index) {
        const auto* const at = ring + Ring::header_words
            + ((first + index) % capacity) * value_words;
        return Word { at[0], at[1] };
    };
    const auto current = count >= 1U ? value(count - 1U) : fallback;
    const auto kind = static_cast<SignalReadKind>(inst.sub);
    if (kind == SignalReadKind::sampled) {
        registers[inst.d] = { current.a & m, current.b & m };
        return;
    }
    if (kind == SignalReadKind::past) {
        // The sample of this time step (the clock's publication appended
        // it) is not a past one (16.9.3).
        const auto prior = count - (sampled_now_in(ring) ? 1U : 0U);
        const auto past = prior >= inst.z ? value(prior - inst.z) : fallback;
        registers[inst.d] = { past.a & m, past.b & m };
        return;
    }
    // A value change function compares the sampled value of this time step
    // with the sample of the most recent strictly prior tick (16.9.3): at a
    // tick the ring's last two samples, in any other time step the Preponed
    // value (y - 1 in the sampled words) and the ring's last sample. Every
    // bit of the signal (offset bits) is compared.
    const bool between_ticks = inst.y != 0U && !sampled_now_in(ring);
    const auto now_value = between_ticks
        ? Word { impl_.sampled_value_words[inst.y - 1U],
              impl_.sampled_value_words[inst.y] }
        : current;
    const auto previous = between_ticks ? current
        : count >= 2U ? value(count - 2U) : fallback;
    const auto one = [](const Word word) {
        return (word.a & 1U) != 0U && (word.b & 1U) == 0U;
    };
    const auto zero = [](const Word word) {
        return (word.a & 1U) == 0U && (word.b & 1U) == 0U;
    };
    const auto signal_mask = kw::mask(inst.offset);
    const bool equal = (now_value.a & signal_mask) == (previous.a & signal_mask)
        && (now_value.b & signal_mask) == (previous.b & signal_mask);
    const bool result = kind == SignalReadKind::rose
        ? one(now_value) && !one(previous)
        : kind == SignalReadKind::fell
        ? zero(now_value) && !zero(previous)
        : kind == SignalReadKind::stable ? equal : !equal;
    registers[inst.d] = { result ? 1U : 0U, 0U };
}

void Interpreter::Impl::StaticKernel::native_net_delay(
    StaticKernelNativeFrame* frame, Word* registers, const std::uint32_t at)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    if (kernel.profile_) {
        ++kernel.profile_native_calls_[3];
    }
    try {
        const auto& program = *static_cast<const CompiledBody*>(frame->program);
        kernel.scratch().running_position = frame->position;
        kernel.execute_net_delay(program, registers, program.code[at],
            frame->member);
    } catch (const KernelDeopt& deopt) {
        kernel.scratch().pending_deopt = deopt;
        frame->status = 2U;
    } catch (...) {
        kernel.scratch().native_exception = std::current_exception();
        frame->status = 1U;
    }
}

template <std::size_t K>
std::uint32_t Interpreter::Impl::StaticKernel::native_step_kind(
    StaticKernelNativeFrame* frame, const std::uint32_t pc)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    try {
        if (++kernel.operation_tier_steps_ > run_step_limit) {
            kernel.fail(frame->member, pc, "static kernel member did not terminate");
        }
        return (kernel.*operation_steps()[K])(frame->member, pc);
    } catch (...) {
        kernel.scratch().native_exception = std::current_exception();
        frame->status = 1U;
        return operation_tier_stop;
    }
}

template <std::size_t K>
std::uint32_t Interpreter::Impl::StaticKernel::native_thread_step_kind(
    StaticKernelNativeFrame* frame, const std::uint32_t pc)
{
    // As the thread runner's reference loop: the operation's boundary
    // (wait, fork, halt, debug point), else its behavioral or kernel step.
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    try {
        const auto outcome
            = (kernel.*boundary_steps()[K])(kernel.operation_tier_thread_, pc);
        if (outcome == Boundary::none) {
            if ((kernel.*behavioral_operation_steps()[K])(frame->member, pc)) {
                return pc + 1U;
            }
            return (kernel.*operation_steps()[K])(frame->member, pc);
        }
        if (outcome == Boundary::advance) {
            return pc + 1U;
        }
        kernel.operation_tier_boundary_ = outcome;
        kernel.operation_tier_pc_ = pc;
        return operation_tier_stop;
    } catch (...) {
        kernel.scratch().native_exception = std::current_exception();
        frame->status = 1U;
        return operation_tier_stop;
    }
}

template <std::size_t K>
std::uint32_t Interpreter::Impl::StaticKernel::native_boundary_kind(void* kernel,
    const std::uint32_t thread, const std::uint32_t pc)
{
    // Called by the thread runner (not by native code) once the compiled
    // registers are back in the member's state.
    return static_cast<std::uint32_t>(
        (static_cast<StaticKernel*>(kernel)->*boundary_steps()[K])(thread, pc));
}

template <std::size_t K>
std::uint32_t Interpreter::Impl::StaticKernel::native_hosted_step_kind(
    StaticKernelNativeFrame* frame, const std::uint32_t pc)
{
    // The evaluator's semantics for this operation kind on the process, then
    // its boundary handling (Impl::hosted_step).
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    try {
        auto& process = *static_cast<ProcessState*>(frame->instance);
        return (kernel.impl_.*Impl::hosted_steps()[K])(process, pc);
    } catch (...) {
        kernel.scratch().native_exception = std::current_exception();
        frame->status = 1U;
        return operation_tier_stop;
    }
}

template <KOp Op>
Interpreter::Impl::StaticKernel::Word
Interpreter::Impl::StaticKernel::evaluate_slow_op(const CompiledBody& body,
    const KInst& inst, const std::uint32_t at, const std::uint32_t member_index,
    const std::uint32_t body_begin, const Word x, const Word y, const Word z,
    const std::uint32_t resolved, const std::vector<PackedLogic4>* wide)
{
    // Source bit reader: wide slot (sub 1), wide register (sub 2), or x.
    const auto fail_at = [&](const std::string& message) {
        fail(member_index, body_begin + at, message);
    };
    // Source bits from a wide slot (sub 1) or wide register (sub 2); 'U'
    // elements of a Logic9 register need a tracked destination.
    const auto source_field = [&](const std::uint32_t offset,
                                  const std::uint32_t width) -> kw::UWord {
        if (inst.op == KOp::load_field9 || inst.sub == 1U) {
            const auto& slot = slots_[resolved];
            if (slot.planes != 4U) {
                return { read_field(resolved, offset, width), 0U, 0U };
            }
            // A Logic9 slot read in place; imm_b 1 coerces to Logic4.
            std::uint64_t planes[4] { };
            const auto word = offset / 64U;
            const auto shift = offset % 64U;
            for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
                const auto* base = arena_.data() + slot.offset + plane * slot.words;
                planes[plane] = base[word] >> shift;
                if (shift != 0U && shift + width > 64U && word + 1U < slot.words) {
                    planes[plane] |= base[word + 1U] << (64U - shift);
                }
            }
            if (inst.op == KOp::load_field9 ? inst.sub == 1U : inst.imm_b != 0U) {
                return { kw::logic9_coerced(planes[0], planes[1], planes[2],
                             planes[3], width),
                    0U, 0U };
            }
            // All nine codes: 'U' and the weak elements go to the masks of
            // the (tracked) destination.
            const auto field = kw::logic9_uword(planes[0], planes[1], planes[2],
                planes[3], width);
            if (field.weak != 0U && body.weak == 0U) {
                throw std::logic_error { "static kernel met a weak std_logic value in a body without weak support" };
            }
            return field;
        }
        if (inst.sub == 3U) {
            // A wide register's chunks (imm_b of them; imm_a: 'U' masks).
            const auto field = wide_field(scratch().wide_file, inst.x,
                static_cast<std::uint32_t>(inst.imm_b), inst.imm_a != 0U, offset,
                width);
            return field;
        }
        const auto& source = (*wide)[inst.x];
        if (!source.is_logic9()) {
            return { packed_field(source, offset, width), 0U, 0U };
        }
        std::uint64_t planes[4] { };
        const auto word = offset / 64U;
        const auto shift = offset % 64U;
        for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
            const auto words = source.logic9_plane_words(plane);
            if (word >= words.size()) {
                return { kw::all_x(width), 0U, 0U };
            }
            planes[plane] = words[word] >> shift;
            if (shift != 0U && shift + width > 64U && word + 1U < words.size()) {
                planes[plane] |= words[word + 1U] << (64U - shift);
            }
        }
        const auto field = kw::logic9_uword(planes[0], planes[1], planes[2],
            planes[3], width);
        if (field.weak != 0U && body.weak == 0U) {
            throw std::logic_error { "static kernel met a weak std_logic value in a body without weak support" };
        }
        return field;
    };
    // Runs SimIR operation `origin + at` on the reference evaluator with the
    // given operand words and returns its destination as a word.
    // The operation's kernel step (its kind's, chosen here).
    const auto reference = [&](const auto& operation,
                               const std::initializer_list<
                                   std::pair<RegisterId, Word>> operands,
                               const RegisterId destination) -> Word {
        using T = std::decay_t<decltype(operation)>;
        auto& member = members_[member_index];
        const auto& widths = body.register_widths;
        for (const auto& [reg, value] : operands) {
            const auto kind = reg < member.register_kinds.size()
                ? member.register_kinds[reg] : ValueKind::logic4;
            member.registers[reg] = Impl::coerce_value_kind(
                PackedLogic4::from_aval_bval(widths[reg], value.a, value.b), kind);
        }
        (void)(this->*operation_steps()[operation_kind_of<T>])(member_index,
            body_begin + at);
        // An integer result holds 0, 1, X and Z only ('U' and weak
        // elements read as the words of their masks).
        if (const auto word = exact_uword(member.registers[destination])) {
            return word->value;
        }
        fail_at("static kernel integer result has no register representation");
        return { };
    };
    switch (Op) {
    case KOp::load_field9: {
        const auto field = source_field(inst.offset, inst.width);
        scratch().last_unknown = field.unknown;
        scratch().last_weak = field.weak;
        return field.value;
    }
    case KOp::branch: {
        // A wide or polymorphic condition register (sub 2).
        const auto& condition = (*wide)[inst.x];
        if (condition.width() != 1U) {
            fail_at("branch condition must be scalar");
        }
        switch (condition.get(0)) {
        case Logic4::zero:
            return { 0U, 0U };
        case Logic4::one:
            return { 1U, 0U };
        case Logic4::z:
            return { 0U, 1U };
        case Logic4::x:
            break;
        }
        return { 1U, 1U };
    }
    case KOp::ret: {
        // Runtime-owned call stack (z == 0).
        auto& member = members_[member_index];
        if (member.call_stack.empty()) {
            fail_at("call-stack underflow");
        }
        const auto target = member.call_stack.back();
        member.call_stack.pop_back();
        if (target >= member.operations().size()) {
            fail_at("call-stack return target is invalid");
        }
        return { target, 0U };
    }
    case KOp::load_slot9: {
        const auto& slot = slots_[resolved];
        const auto* planes = arena_.data() + slot.offset;
        scratch().last_unknown = 0U;
        scratch().last_weak = 0U;
        if (inst.sub == 1U) {
            return kw::logic9_coerced(planes[0], planes[1], planes[2], planes[3],
                inst.width);
        }
        const auto word = kw::logic9_uword(planes[0], planes[1], planes[2],
            planes[3], inst.width);
        if (word.weak != 0U && body.weak == 0U) {
            // design_may_hold_weak finds every source of a weak element.
            throw std::logic_error { "static kernel met a weak std_logic value in a body without weak support" };
        }
        scratch().last_unknown = word.unknown;
        scratch().last_weak = word.weak;
        return word.value;
    }
    case KOp::integer_binary: {
        const auto& op = *operation_get_if<IntegerBinary>(
            &members_[member_index].operations()[body_begin + at]);
        if (inst.width == 32U && ((x.b | y.b) & kw::mask(32U)) == 0U) {
            const auto left = static_cast<std::int64_t>(
                static_cast<std::int32_t>(static_cast<std::uint32_t>(x.a)));
            const auto right = static_cast<std::int64_t>(
                static_cast<std::int32_t>(static_cast<std::uint32_t>(y.a)));
            std::optional<std::int64_t> result;
            switch (op.operation) {
            case IntegerBinaryOperator::add:
                result = left + right;
                break;
            case IntegerBinaryOperator::subtract:
                result = left - right;
                break;
            case IntegerBinaryOperator::multiply:
                result = left * right;
                break;
            default:
                break;
            }
            if (result && *result >= std::numeric_limits<std::int32_t>::min()
                && *result <= std::numeric_limits<std::int32_t>::max()) {
                return { static_cast<std::uint64_t>(*result) & kw::mask(32U), 0U };
            }
        }
        return reference(op, { { op.lhs, x }, { op.rhs, y } }, op.destination);
    }
    case KOp::integer_unary: {
        const auto& op = *operation_get_if<IntegerUnary>(
            &members_[member_index].operations()[body_begin + at]);
        return reference(op, { { op.source, x } }, op.destination);
    }
    case KOp::load_host: {
        const auto& current = impl_.logical_signal_value(resolved);
        scratch().last_unknown = 0U;
        scratch().last_weak = 0U;
        if (inst.sub == 1U && current.is_logic9()) {
            if (const auto word = exact_uword(current)) {
                if (word->weak != 0U && body.weak == 0U) {
                    throw std::logic_error { "static kernel met a weak std_logic value in a body without weak support" };
                }
                scratch().last_unknown = word->unknown;
                scratch().last_weak = word->weak;
                return word->value;
            }
        }
        const auto value = Impl::coerce_value_kind(current, ValueKind::logic4);
        const auto aval = value.aval_words();
        const auto bval = value.bval_words();
        return { aval.empty() ? 0U : aval[0], bval.empty() ? 0U : bval[0] };
    }
    case KOp::shift:
        return kw::shift(static_cast<ShiftOperator>(inst.sub), x, inst.width, y,
            inst.offset, (inst.flags & flag_signed) != 0U);
    case KOp::binary:
        return kw::binary(static_cast<BinaryOperator>(inst.sub), x, y,
            inst.width);
    case KOp::extract: {
        // A selection from a wide register (sub 2).
        const auto field = source_field(inst.offset, inst.width);
        scratch().last_unknown = field.unknown;
        scratch().last_weak = field.weak;
        return field.value;
    }
    case KOp::dynamic_extract: {
        scratch().last_unknown = 0U;
        scratch().last_weak = 0U;
        const auto offset = kw::dynamic_index(y, body.indices[inst.aux]);
        if (inst.sub != 0U && offset && *offset < inst.width) {
            const auto field = source_field(*offset, 1U);
            scratch().last_unknown = field.unknown;
            scratch().last_weak = field.weak;
            return field.value;
        }
        if (!offset || *offset >= inst.width) {
            if ((inst.flags & flag_strict) != 0U) {
                fail_at(offset ? "extract range is outside the source value"
                               : "dynamic packed index contains an unknown or "
                                 "high-impedance value or is outside the "
                                 "declared range");
            }
            return kw::all_x(1U);
        }
        // z holds the source register's 'U' and weak masks.
        scratch().last_unknown = (z.a >> *offset) & 1U;
        scratch().last_weak = (z.b >> *offset) & 1U;
        return kw::extract(x, *offset, 1U);
    }
    case KOp::dynamic_part_select: {
        const auto& part = body.parts[inst.aux];
        if (inst.sub != 0U) {
            // Source bits come from the wide slot; mirror
            // dynamic_part_select_value bit by bit.
            Word result = (inst.flags & flag_two_state) != 0U
                ? Word { } : kw::all_x(inst.width);
            if ((y.b & kw::mask(32U)) != 0U) {
                return result;
            }
            const auto signed_base = static_cast<std::int64_t>(
                static_cast<std::int32_t>(static_cast<std::uint32_t>(y.a)));
            const auto lower = std::min(part.left, part.right);
            const auto upper = std::max(part.left, part.right);
            const auto edge = static_cast<std::int64_t>(inst.width - 1U);
            const bool increasing = (inst.flags & flag_increasing) != 0U;
            const bool descending = (inst.flags & flag_descending) != 0U;
            const auto selected_right = increasing
                ? signed_base + (descending ? 0 : edge)
                : signed_base - (descending ? edge : 0);
            scratch().last_unknown = 0U;
            scratch().last_weak = 0U;
            if ((descending && part.right == lower)
                || (!descending && part.right == upper)) {
                // Result bit i is source bit first + i: one field read.
                const auto first = static_cast<std::int64_t>(part.base_offset)
                    + (descending ? selected_right - part.right
                                  : part.right - selected_right);
                auto low = std::max<std::int64_t>(
                    descending ? lower - selected_right : selected_right - upper,
                    0);
                auto high = std::min<std::int64_t>(
                    descending ? upper - selected_right : selected_right - lower,
                    edge);
                high = std::min<std::int64_t>(high,
                    static_cast<std::int64_t>(inst.offset) - 1 - first);
                low = std::max<std::int64_t>(low, -first);
                if (low <= high) {
                    const auto count = static_cast<std::uint32_t>(high - low + 1);
                    const auto source = source_field(
                        static_cast<std::uint32_t>(first + low), count);
                    const auto shift = static_cast<std::uint32_t>(low);
                    const auto m = kw::mask(count) << shift;
                    result.a = (result.a & ~m) | (source.value.a << shift);
                    result.b = (result.b & ~m) | (source.value.b << shift);
                    scratch().last_unknown = source.unknown << shift;
                    scratch().last_weak = source.weak << shift;
                }
                return result;
            }
            for (std::uint32_t bit = 0U; bit < inst.width; ++bit) {
                const auto selected = descending
                    ? selected_right + static_cast<std::int64_t>(bit)
                    : selected_right - static_cast<std::int64_t>(bit);
                if (selected < lower || selected > upper) {
                    continue;
                }
                const auto offset
                    = support::index_distance(selected, part.right);
                if (offset > std::numeric_limits<std::uint32_t>::max()
                        - part.base_offset
                    || part.base_offset + offset >= inst.offset) {
                    continue;
                }
                const auto source = source_field(
                    static_cast<std::uint32_t>(part.base_offset + offset), 1U);
                const auto one = std::uint64_t { 1 } << bit;
                result.a = (result.a & ~one) | (source.value.a << bit);
                result.b = (result.b & ~one) | (source.value.b << bit);
                scratch().last_unknown |= source.unknown << bit;
                scratch().last_weak |= source.weak << bit;
            }
            return result;
        }
        // z holds the source register's 'U' and weak masks: the same
        // selection of each, with bits outside the source neither.
        scratch().last_unknown = kw::dynamic_part_select({ z.a, 0U }, inst.offset, y,
            part.left, part.right, part.base_offset, inst.width,
            (inst.flags & flag_increasing) != 0U,
            (inst.flags & flag_descending) != 0U, true).a;
        scratch().last_weak = kw::dynamic_part_select({ z.b, 0U }, inst.offset, y,
            part.left, part.right, part.base_offset, inst.width,
            (inst.flags & flag_increasing) != 0U,
            (inst.flags & flag_descending) != 0U, true).a;
        return kw::dynamic_part_select(x, inst.offset, y, part.left, part.right,
            part.base_offset, inst.width, (inst.flags & flag_increasing) != 0U,
            (inst.flags & flag_descending) != 0U,
            (inst.flags & flag_two_state) != 0U);
    }
    case KOp::dynamic_insert: {
        const auto offset = kw::dynamic_index(z, body.indices[inst.aux]);
        if (!offset || *offset > inst.width || inst.offset > inst.width - *offset) {
            if ((inst.flags & flag_strict) != 0U) {
                if (!offset) {
                    // An index outside the declared range or with unknown
                    // bits reports the reference's message (simir_logic).
                    try {
                        (void)dynamic_index_offset(
                            PackedLogic4::from_aval_bval(32U, z.a, z.b),
                            body.indices[inst.aux]);
                    } catch (const std::invalid_argument& error) {
                        fail_at(error.what());
                    }
                }
                fail_at("dynamic packed index is invalid");
            }
            return x;
        }
        return kw::insert(x, y, *offset, inst.offset);
    }
    case KOp::dynamic_part_insert: {
        const auto write = kw::dynamic_part_write(y, z, body.parts[inst.aux]);
        if (!write.valid) {
            fail_at("invalid dynamic part-select write");
        }
        if (!write.write) {
            return x;
        }
        if (write.write->offset > inst.width
            || write.write->width > inst.width - write.write->offset) {
            fail_at("insert range is outside the target value");
        }
        return kw::insert(x, write.write->value, write.write->offset,
            write.write->width);
    }
    case KOp::mem_read: {
        const auto& container = containers_[resolved];
        const auto& type = container.type;
        const auto element_width = static_cast<std::uint32_t>(type.element_width);
        const Word unknown = type.two_state ? Word { } : kw::all_x(element_width);
        if (!kw::known(y)) {
            return unknown;
        }
        const auto value = kw::sign_extend(y.a, inst.width);
        if ((inst.flags & flag_linear) != 0U) {
            return value < 0 || static_cast<std::uint64_t>(value) >= container.count
                ? unknown
                : container_element_word(container, static_cast<std::size_t>(value));
        }
        const auto low = std::min(type.index_left, type.index_right);
        const auto high = std::max(type.index_left, type.index_right);
        return value < low || value > high
            ? unknown
            : container_element_word(container,
                  fixed_offset(type, static_cast<std::int32_t>(value)));
    }
    default:
        break;
    }
    fail_at("static kernel instruction has no slow evaluation");
    return { };
}

void Interpreter::Impl::StaticKernel::grow_frame_stack(NativeCallStack& stack,
    const std::uint64_t depth, const std::uint64_t pairs,
    const std::uint32_t member, const std::uint32_t instruction)
{
    // Capacity growth only: room for `pairs` more register-file pairs (two
    // entries each) at `depth`, within the owning-storage budget.
    const auto needed = 2U * (depth + pairs);
    if (needed <= stack.capacity) {
        return;
    }
    if (needed > maximum_container_storage_bytes / sizeof(std::uint64_t)) {
        fail(member, instruction,
            "automatic callable frames exceed their owning-storage budget");
    }
    stack.reserve(needed);
}

void Interpreter::Impl::StaticKernel::grow_call_stack(NativeCallStack& stack,
    const std::uint64_t depth, const std::uint32_t member,
    const std::uint32_t instruction)
{
    // Capacity growth only: room for one more entry. The reference's
    // owning-storage budget applies to the depth.
    if (depth < stack.capacity) {
        return;
    }
    if (depth >= maximum_container_storage_bytes / sizeof(InstructionIndex)) {
        fail(member, instruction,
            "dynamic call stack exceeds its owning-storage budget");
    }
    stack.reserve(depth + 1U);
}

template <KOp Op>
void Interpreter::Impl::StaticKernel::effect_slow_op(const CompiledBody& body,
    const KInst& inst, const std::uint32_t at, const std::uint32_t member_index,
    const std::uint32_t body_begin, const Word x, const Word y, const Word z,
    const std::uint32_t resolved)
{
    const auto fail_at = [&](const std::string& message) {
        fail(member_index, body_begin + at, message);
    };
    const auto to_packed = [](const Word value, const std::uint32_t width) {
        return PackedLogic4::from_aval_bval(width, value.a, value.b);
    };
    // A later piece of one wide nonblocking write continues the update of the
    // piece before it (write_continues).
    const std::uint32_t recorded_at = (body_begin + at)
        | ((Op == KOp::store_slot_part || Op == KOp::store_slot_dynamic)
                    && (inst.flags & flag_continues) == flag_continues
                ? write_continues : 0U);
    const auto pending_slice = [&](const std::uint32_t slot,
                                   const std::uint32_t offset, const Word value,
                                   const std::uint32_t width, const bool slice) {
        (void)slice;
        StaticKernelWrite write;
        write.slot = slot;
        write.offset = offset;
        write.width = width;
        write.member = member_index;
        write.a = value.a;
        write.b = value.b;
        write.instruction = recorded_at;
        push_write(write);
    };
    // Runs SimIR operation `body_begin + at` on the reference evaluator with
    // its operand register set to `value`.
    const auto reference = [&](const auto& operation, const RegisterId reg,
                               const Word value) {
        using T = std::decay_t<decltype(operation)>;
        auto& member = members_[member_index];
        const auto kind = reg < member.register_kinds.size()
            ? member.register_kinds[reg] : ValueKind::logic4;
        member.registers[reg] = Impl::coerce_value_kind(
            PackedLogic4::from_aval_bval(body.register_widths[reg], value.a,
                value.b),
            kind);
        (void)(this->*operation_steps()[operation_kind_of<T>])(member_index,
            body_begin + at);
    };
    switch (Op) {
    case KOp::store_vhdl: {
        auto offset = inst.offset;
        if (inst.sub == 1U || inst.sub == 3U) {
            const auto& selection = body.indices[inst.aux];
            const auto selected = kw::dynamic_index(y, selection);
            if (!selected) {
                try {
                    (void)dynamic_index_offset(to_packed(y, 32U), selection);
                } catch (const std::invalid_argument& error) {
                    fail_at(error.what());
                }
                fail_at("dynamic packed index is invalid");
            }
            offset = *selected;
        }
        auto& slot = slots_[resolved];
        if (offset > slot.width || inst.width > slot.width - offset) {
            fail_at("partial update range is outside its target signal");
        }
        if (inst.sub >= 2U) {
            // A nine-value constant into a resolved slot: its codes, which
            // no register holds, from the body's constants (kind 3), one
            // write per 64-bit chunk.
            // Sub 3 carries the source count above the constants' index.
            const auto constant
                = (inst.sub == 2U ? inst.imm_a : inst.imm_b) & 0xffffffffU;
            for (std::uint32_t done = 0U; done < inst.width; done += 64U) {
                StaticKernelWrite write;
                write.slot = resolved;
                write.offset = offset + done;
                write.width = std::min(64U, inst.width - done);
                write.member = member_index;
                write.a = constant + done / 32U;
                write.kind = 3U;
                write.instruction = body_begin + at;
                // Resolved into the slot's next block now; the queued write
                // marks the slot for the update phase.
                set_net_bits(resolved, member_process_[member_index], write.offset,
                    write.width, net_constant_codes(write), true);
                push_write(write);
            }
            return;
        }
        if ((inst.flags & flag_blocking) != 0U) {
            // Shared variable: immediate.
            last_writer_[resolved] = member_process_[member_index];
            const WordSnapshot before(
                arena_.data() + slot.offset, slot.planes * slot.words);
            store_slot_word(resolved, x, offset, inst.width, z.a, z.b);
            if (!std::equal(before.begin(), before.end(),
                    arena_.begin() + slot.offset)) {
                slot_changed(resolved, 0U, before.data());
            }
            return;
        }
        StaticKernelWrite write;
        write.slot = resolved;
        write.offset = offset;
        write.width = inst.width;
        write.member = member_index;
        write.a = x.a;
        write.b = x.b;
        write.unknown = z.a & kw::mask(inst.width);
        write.weak = z.b & kw::mask(inst.width);
        write.instruction = body_begin + at;
        if (slot.resolved) {
            // A VHDL assignment to a resolved slot resolves into its next
            // block now (kind 4); the queued write marks the slot for the
            // update phase.
            write.kind = 4U;
            set_net_bits(resolved, member_process_[member_index], offset,
                inst.width,
                kw::logic9_planes(x, inst.width, write.unknown, write.weak), true);
        }
        push_write(write);
        return;
    }
    case KOp::integer_check: {
        const auto lower = static_cast<std::int64_t>(inst.imm_a);
        const auto upper = static_cast<std::int64_t>(inst.imm_b);
        if ((x.b & kw::mask(inst.width)) == 0U) {
            const auto bits = x.a & kw::mask(inst.width);
            std::optional<std::int64_t> value;
            if (inst.width == 32U || inst.width == 64U || lower < 0) {
                value = kw::sign_extend(bits, inst.width);
            } else if (bits <= static_cast<std::uint64_t>(
                           std::numeric_limits<std::int64_t>::max())) {
                value = static_cast<std::int64_t>(bits);
            }
            if (value && *value >= lower && *value <= upper) {
                return;
            }
        }
        const auto& op = *operation_get_if<IntegerCheck>(
            &members_[member_index].operations()[body_begin + at]);
        reference(op, op.source, x);
        return;
    }
    case KOp::frame_push:
    case KOp::frame_pop:
        // Native code reports a frame stack without the frame here.
        fail_at("automatic callable frame stack mismatch");
        return;
    case KOp::call:
    case KOp::ret: {
        if (inst.op == KOp::call && inst.z == 0U) {
            auto& member = members_[member_index];
            if (member.call_stack.size() >= maximum_container_storage_bytes
                    / sizeof(InstructionIndex)) {
                fail_at("dynamic call stack exceeds its owning-storage budget");
            }
            member.call_stack.push_back(
                static_cast<InstructionIndex>(inst.imm_a));
            return;
        }
        // Native code reports a failed call or return here.
        if ((x.b & kw::mask(32U)) != 0U) {
            fail_at("call-stack pointer is unknown");
        }
        const auto depth = x.a & kw::mask(32U);
        if (inst.op == KOp::call) {
            fail_at("call-stack capacity is exhausted");
        }
        if (depth == 0U
            || ((inst.flags & flag_linear) == 0U && depth > inst.z)) {
            fail_at("call-stack underflow");
        }
        fail_at("call-stack return target is invalid");
        return;
    }
    case KOp::assert_check: {
        if ((x.a & 1U) != 0U && (x.b & 1U) == 0U) {
            return;
        }
        const auto& op = *operation_get_if<Assert>(
            &members_[member_index].operations()[body_begin + at]);
        reference(op, op.condition, x);
        return;
    }
    case KOp::store_slot:
        write_slot_word(resolved, x, inst.offset, inst.width, member_index);
        return;
    case KOp::store_slot_nba:
        pending_slice(resolved, inst.offset, x, inst.width,
            (inst.flags & flag_linear) != 0U);
        return;
    case KOp::store_slot_dynamic: {
        const auto offset = kw::dynamic_index(y, body.indices[inst.aux]);
        if (!offset) {
            if ((inst.flags & flag_strict) != 0U) {
                fail_at("dynamic packed index contains an unknown or "
                        "high-impedance value or is outside the declared "
                        "range");
            }
            // IEEE 1800-2023 11.5.1: the write has no effect.
            return;
        }
        const auto& slot = slots_[resolved];
        if (*offset > slot.width || inst.width > slot.width - *offset) {
            fail_at("static kernel write is outside its signal");
        }
        if ((inst.flags & flag_nba) != 0U && slot.planes == 4U && !slot.resolved) {
            // A nine-value slot: the source's 'U' and weak masks (z) go with
            // the field, as store_slot_part queues them.
            StaticKernelWrite queued;
            queued.slot = resolved;
            queued.offset = *offset;
            queued.width = inst.width;
            queued.member = member_index;
            queued.a = x.a;
            queued.b = x.b;
            queued.unknown = z.a & kw::mask(inst.width);
            queued.weak = z.b & kw::mask(inst.width);
            queued.instruction = recorded_at;
            push_write(queued);
        } else if ((inst.flags & flag_nba) != 0U) {
            pending_slice(resolved, *offset, x, inst.width, true);
        } else if (inst.sub == 3U) {
            // A nine-value slot written at once: the masks of z with the
            // field, one change report.
            last_writer_[resolved] = member_process_[member_index];
            const WordSnapshot before(
                arena_.data() + slot.offset, slot.planes * slot.words);
            store_slot_word(resolved, x, *offset, inst.width,
                z.a & kw::mask(inst.width), z.b & kw::mask(inst.width));
            if (!std::equal(before.begin(), before.end(),
                    arena_.begin() + slot.offset)) {
                slot_changed(resolved, 0U, before.data());
            }
        } else {
            write_slot_word(resolved, x, *offset, inst.width, member_index);
        }
        return;
    }
    case KOp::store_slot_part: {
        const auto write = kw::dynamic_part_write(x, y, body.parts[inst.aux]);
        if (!write.valid) {
            fail_at("invalid dynamic part-select write");
        }
        if (!write.write) {
            return;
        }
        const auto& slot = slots_[resolved];
        if (write.write->offset > slot.width
            || write.write->width > slot.width - write.write->offset) {
            fail_at("static kernel write is outside its signal");
        }
        if ((inst.flags & flag_nba) != 0U) {
            // The value's 'U' and weak masks (z) go with the bits written, as
            // a VHDL assignment to a nine-value slot queues them
            // (IEEE 1076-2019 10.5.2.1): the masks select the same bits as
            // the value, so the same selection places them.
            const auto masks = kw::dynamic_part_write(z, y, body.parts[inst.aux]);
            StaticKernelWrite queued;
            queued.slot = resolved;
            queued.offset = write.write->offset;
            queued.width = write.write->width;
            queued.member = member_index;
            queued.a = write.write->value.a;
            queued.b = write.write->value.b;
            if (masks.write) {
                queued.unknown = masks.write->value.a & kw::mask(queued.width);
                queued.weak = masks.write->value.b & kw::mask(queued.width);
            }
            queued.instruction = recorded_at;
            if (slot.resolved) {
                // Resolved into the slot's next block now (kind 4); the
                // queued write marks the slot for the update phase.
                queued.kind = 4U;
                set_net_bits(resolved, member_process_[member_index], queued.offset,
                    queued.width,
                    kw::logic9_planes(write.write->value, queued.width,
                        queued.unknown, queued.weak),
                    true);
            }
            push_write(queued);
        } else if (inst.sub == 3U) {
            // A shared variable of nine values (IEEE 1076-2019 6.4.2.4):
            // immediate, the masks of z selected like the value's bits, one
            // change report.
            const auto masks = kw::dynamic_part_write(z, y, body.parts[inst.aux]);
            last_writer_[resolved] = member_process_[member_index];
            const WordSnapshot before(
                arena_.data() + slot.offset, slot.planes * slot.words);
            store_slot_word(resolved, write.write->value, write.write->offset,
                write.write->width,
                masks.write ? masks.write->value.a & kw::mask(write.write->width) : 0U,
                masks.write ? masks.write->value.b & kw::mask(write.write->width) : 0U);
            if (!std::equal(before.begin(), before.end(),
                    arena_.begin() + slot.offset)) {
                slot_changed(resolved, 0U, before.data());
            }
        } else {
            write_slot_word(resolved, write.write->value, write.write->offset,
                write.write->width, member_index);
        }
        return;
    }
    case KOp::store_host: {
        // A tracked value with weak elements ('U' is excluded by the
        // operand check) is a nine-value one: the word and its mask.
        auto value = z.b != 0U ? uword_value(x, 0U, inst.width, ValueKind::logic9, z.b)
                               : to_packed(x, inst.width);
        std::optional<std::uint32_t> offset;
        if ((inst.flags & flag_linear) != 0U) {
            offset = inst.offset;
        } else if (inst.aux != 0U) {
            // A dynamic index (offset 0) or part select (offset 1; IEEE
            // 1800-2023 11.5.1): register y holds the index or base.
            if (inst.offset == 0U) {
                const auto selected = kw::dynamic_index(y, body.indices[inst.aux - 1U]);
                if (!selected) {
                    if ((inst.flags & flag_strict) != 0U) {
                        fail_at("dynamic packed index contains an unknown or "
                                "high-impedance value or is outside the "
                                "declared range");
                    }
                    // The write has no effect.
                    return;
                }
                offset = *selected;
            } else {
                const auto part = kw::dynamic_part_write(x, y, body.parts[inst.aux - 1U]);
                if (!part.valid) {
                    fail_at("invalid dynamic part-select write");
                }
                if (!part.write) {
                    return;
                }
                value = to_packed(part.write->value, part.write->width);
                offset = static_cast<std::uint32_t>(part.write->offset);
            }
        }
        write_host(member_index, resolved, std::move(value), offset,
            (inst.flags & flag_blocking) != 0U,
            static_cast<SignalUpdateDomain>(inst.sub));
        return;
    }
    case KOp::mem_copy:
        // x carries the source container; resolved is the destination.
        copy_container(resolved, static_cast<std::uint32_t>(x.a), member_index,
            body_begin + at);
        return;
    case KOp::wide_move: {
        move_wide(body, inst, at, member_index, static_cast<std::uint32_t>(x.a),
            y, resolved);
        return;
    }
    case KOp::mem_write: {
        NbaEntry entry;
        entry.kind = inst.aux != 0U ? NbaKind::element_part : NbaKind::element;
        entry.member = member_index;
        entry.instruction = body_begin + at;
        entry.target = resolved;
        entry.value = x;
        entry.width = inst.width;
        entry.index = y;
        entry.index_width = inst.offset;
        entry.linear_index = (inst.flags & flag_linear) != 0U;
        if (inst.aux != 0U) {
            entry.base = z;
            entry.part = &body.parts[inst.aux - 1U];
        }
        if ((inst.flags & flag_nba) != 0U) {
            push_element_write(entry);
        } else {
            write_element_entry(entry);
        }
        return;
    }
    default:
        break;
    }
    fail_at("static kernel instruction has no effect evaluation");
}

namespace {

/// Instruction kinds with slow evaluate and effect semantics
/// (evaluate_slow_op, effect_slow_op).
constexpr std::array evaluate_kinds { KOp::load_field9, KOp::branch, KOp::ret,
    KOp::load_slot9, KOp::integer_binary, KOp::integer_unary, KOp::load_host,
    KOp::shift, KOp::binary, KOp::extract, KOp::dynamic_extract,
    KOp::dynamic_part_select, KOp::dynamic_insert, KOp::dynamic_part_insert,
    KOp::mem_read };
constexpr std::array effect_kinds { KOp::store_vhdl, KOp::integer_check,
    KOp::call, KOp::ret, KOp::frame_push, KOp::frame_pop, KOp::assert_check, KOp::store_slot,
    KOp::store_slot_nba, KOp::store_slot_dynamic, KOp::store_slot_part,
    KOp::store_host, KOp::mem_copy, KOp::wide_move, KOp::mem_write };

} // namespace

Interpreter::Impl::StaticKernel::Word
Interpreter::Impl::StaticKernel::evaluate_slow(const CompiledBody& body,
    const KInst& inst, const std::uint32_t at, const std::uint32_t member_index,
    const std::uint32_t body_begin, const Word x, const Word y, const Word z,
    const std::uint32_t resolved, const std::vector<PackedLogic4>* wide)
{
    using Evaluate = Word (StaticKernel::*)(const CompiledBody&, const KInst&,
        std::uint32_t, std::uint32_t, std::uint32_t, Word, Word, Word,
        std::uint32_t, const std::vector<PackedLogic4>*);
    static constexpr auto table = []<std::size_t... I>(
                                      std::index_sequence<I...>) {
        std::array<Evaluate, kernel_op_count> entries { };
        ((entries[static_cast<std::size_t>(evaluate_kinds[I])]
             = &StaticKernel::evaluate_slow_op<evaluate_kinds[I]>),
            ...);
        return entries;
    }(std::make_index_sequence<evaluate_kinds.size()>());
    const auto entry = table[static_cast<std::size_t>(inst.op)];
    if (entry == nullptr) {
        fail(member_index, body_begin + at,
            "static kernel instruction has no slow evaluation");
    }
    return (this->*entry)(body, inst, at, member_index, body_begin, x, y, z,
        resolved, wide);
}

void Interpreter::Impl::StaticKernel::effect_slow(const CompiledBody& body,
    const KInst& inst, const std::uint32_t at, const std::uint32_t member_index,
    const std::uint32_t body_begin, const Word x, const Word y, const Word z,
    const std::uint32_t resolved)
{
    using Effect = void (StaticKernel::*)(const CompiledBody&, const KInst&,
        std::uint32_t, std::uint32_t, std::uint32_t, Word, Word, Word,
        std::uint32_t);
    static constexpr auto table = []<std::size_t... I>(
                                      std::index_sequence<I...>) {
        std::array<Effect, kernel_op_count> entries { };
        ((entries[static_cast<std::size_t>(effect_kinds[I])]
             = &StaticKernel::effect_slow_op<effect_kinds[I]>),
            ...);
        return entries;
    }(std::make_index_sequence<effect_kinds.size()>());
    const auto entry = table[static_cast<std::size_t>(inst.op)];
    if (entry == nullptr) {
        fail(member_index, body_begin + at,
            "static kernel instruction has no effect evaluation");
    }
    (this->*entry)(body, inst, at, member_index, body_begin, x, y, z, resolved);
}

template <KOp Op>
void Interpreter::Impl::StaticKernel::native_evaluate(
    StaticKernelNativeFrame* frame, const std::uint32_t at,
    const std::uint64_t xa, const std::uint64_t xb, const std::uint64_t ya,
    const std::uint64_t yb, const std::uint64_t za, const std::uint64_t zb,
    std::uint64_t* out)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    if (kernel.profile_) {
        ++kernel.profile_native_calls_[0];
    }
    try {
        const auto& body = *static_cast<const CompiledBody*>(frame->program);
        const auto inst = with_bound_offset(body.code[at], frame->bindings);
        if (kernel.profile_) {
            static const bool who = std::getenv("FSIM_PROFILE_KERNEL_WHO") != nullptr;
            std::string member;
            if (who && frame->member < kernel.profile_names_.size()) {
                member = " " + kernel.profile_names_[frame->member];
                std::erase_if(member, [](const char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) != 0;
                });
            }
            ++kernel.profile_generic_ops_["evaluate op="
                + std::to_string(static_cast<int>(inst.op)) + " sub="
                + std::to_string(inst.sub) + " w=" + std::to_string(inst.width) + member];
        }
        std::uint32_t resolved = 0U;
        if (inst.op == KOp::load_host || inst.op == KOp::mem_read) {
            resolved = frame->bindings[2U * inst.x];
        } else if (((inst.op == KOp::dynamic_extract
                        || inst.op == KOp::dynamic_part_select)
                       && inst.sub == 1U)
            || inst.op == KOp::load_slot9 || inst.op == KOp::load_field9) {
            resolved = frame->bindings[2U * inst.x + 1U] & ~partition_tag;
        }
        const auto result = kernel.evaluate_slow_op<Op>(body, inst, at, frame->member,
            kernel.members_[frame->member].vhdl ? 0U : kernel.members_[frame->member].body_begin,
            { xa, xb }, { ya, yb },
            { za, zb }, resolved,
            &static_cast<CompiledBody*>(frame->instance)->wide_registers);
        out[0] = result.a;
        out[1] = result.b;
        out[2] = kernel.scratch().last_unknown;
        out[3] = kernel.scratch().last_weak;
    } catch (const KernelDeopt& deopt) {
        kernel.scratch().pending_deopt = deopt;
        if (kernel.scratch().pending_deopt->resume == 0xffffffffU) {
            kernel.scratch().pending_deopt->resume = at;
        }
        frame->status = 2U;
        out[0] = 0U;
        out[1] = 0U;
    } catch (...) {
        kernel.scratch().native_exception = std::current_exception();
        frame->status = 1U;
        out[0] = 0U;
        out[1] = 0U;
    }
}

template <KOp Op>
void Interpreter::Impl::StaticKernel::native_effect(
    StaticKernelNativeFrame* frame, const std::uint32_t at,
    const std::uint64_t xa, const std::uint64_t xb, const std::uint64_t ya,
    const std::uint64_t yb, const std::uint64_t za, const std::uint64_t zb,
    std::uint64_t* out)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    if (kernel.profile_) {
        ++kernel.profile_native_calls_[1];
    }
    out[0] = 0U;
    out[1] = 0U;
    try {
        const auto& body = *static_cast<const CompiledBody*>(frame->program);
        const auto inst = with_bound_offset(body.code[at], frame->bindings);
        if constexpr (Op == KOp::frame_push || Op == KOp::frame_pop) {
            if (Op == KOp::frame_push && za == 1U) {
                // Capacity growth of the body's frame stack: xa is the
                // depth, ya the pairs the frame needs.
                auto& instance = *static_cast<CompiledBody*>(frame->instance);
                kernel.grow_frame_stack(instance.frame_entries,
                    xa & kw::mask(32U), ya, frame->member, at);
                return;
            }
            if (za == 2U) {
                // The runtime's part of the frame (sub 1): its strings,
                // containers and private arrays.
                kernel.frame_storage(frame->member, inst.y,
                    Op == KOp::frame_push);
                return;
            }
        }
        if constexpr (Op == KOp::call) {
            if ((inst.flags & flag_linear) != 0U && za != 0U) {
                // Capacity growth of the body's call stack: xa is the depth.
                auto& instance = *static_cast<CompiledBody*>(frame->instance);
                kernel.grow_call_stack(instance.call_entries,
                    xa & kw::mask(32U), frame->member, at);
                return;
            }
        }
        if (kernel.profile_) {
            static const bool who = std::getenv("FSIM_PROFILE_KERNEL_WHO") != nullptr;
            std::string member;
            if (who && frame->member < kernel.profile_names_.size()) {
                member = " " + kernel.profile_names_[frame->member];
                std::erase_if(member, [](const char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) != 0;
                });
            }
            ++kernel.profile_generic_ops_["effect op="
                + std::to_string(static_cast<int>(inst.op)) + " sub="
                + std::to_string(inst.sub) + " w=" + std::to_string(inst.width) + member];
        }
        const auto binding = inst.d;
        std::uint32_t resolved = 0U;
        auto source = Word { xa, xb };
        if (inst.op == KOp::store_host || inst.op == KOp::mem_write
            || inst.op == KOp::mem_copy) {
            resolved = frame->bindings[2U * binding];
        } else if (inst.op == KOp::store_slot || inst.op == KOp::store_slot_nba
            || inst.op == KOp::store_slot_dynamic
            || inst.op == KOp::store_slot_part || inst.op == KOp::store_vhdl
            || inst.op == KOp::wide_move) {
            resolved = frame->bindings[2U * binding + 1U] & ~partition_tag;
        }
        if (inst.op == KOp::mem_copy) {
            // x carries the source container.
            source = { frame->bindings[2U * inst.x], 0U };
        }
        if (inst.op == KOp::wide_move) {
            // x carries the source slot.
            source = { frame->bindings[2U * inst.x + 1U] & ~partition_tag, 0U };
        }
        kernel.scratch().running_position = frame->position;
        kernel.effect_slow_op<Op>(body, inst, at, frame->member,
            kernel.members_[frame->member].vhdl ? 0U : kernel.members_[frame->member].body_begin,
            source, { ya, yb }, { za, zb }, resolved);
    } catch (const KernelDeopt& deopt) {
        kernel.scratch().pending_deopt = deopt;
        if (kernel.scratch().pending_deopt->resume == 0xffffffffU) {
            kernel.scratch().pending_deopt->resume = at;
        }
        frame->status = 2U;
    } catch (...) {
        kernel.scratch().native_exception = std::current_exception();
        frame->status = 1U;
    }
}


void Interpreter::Impl::StaticKernel::set_instruction_helpers(
    StaticKernelNativeHelpers& helpers)
{
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((helpers.evaluate_ops[static_cast<std::size_t>(evaluate_kinds[I])]
             = &StaticKernel::native_evaluate<evaluate_kinds[I]>),
            ...);
    }(std::make_index_sequence<evaluate_kinds.size()>());
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((helpers.effect_ops[static_cast<std::size_t>(effect_kinds[I])]
             = &StaticKernel::native_effect<effect_kinds[I]>),
            ...);
    }(std::make_index_sequence<effect_kinds.size()>());
    static constexpr std::array wide_kinds { KOp::wide_load, KOp::wide_copy,
        KOp::wide_const, KOp::wide_concat, KOp::wide_store, KOp::wide_part_insert,
        KOp::dynamic_extract, KOp::dynamic_part_select, KOp::mem_read,
        KOp::mem_write };
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((helpers.wide_ops[static_cast<std::size_t>(wide_kinds[I])]
             = &StaticKernel::native_wide<wide_kinds[I]>),
            ...);
    }(std::make_index_sequence<wide_kinds.size()>());
    helpers.net_delay = &StaticKernel::native_net_delay;
    helpers.sva_tick = &StaticKernel::native_sva_tick;
    helpers.sva_ensure = &StaticKernel::native_sva_ensure;
    helpers.sva_grow = &StaticKernel::native_sva_grow;
    helpers.sva_post = &StaticKernel::native_sva_post;
    helpers.sva_filter = &StaticKernel::native_sva_filter;
    helpers.sva_check = &StaticKernel::native_sva_check;
    helpers.sva_verify = &StaticKernel::native_sva_verify;
    helpers.sva_bind = &StaticKernel::native_sva_bind;
    helpers.sva_verify_outcome = &StaticKernel::native_sva_verify_outcome;
    [&]<std::size_t... K>(std::index_sequence<K...>) {
        ((helpers.generic_ops[K] = &StaticKernel::native_generic_kind<K>), ...);
        ((helpers.step_ops[K] = &StaticKernel::native_step_kind<K>), ...);
        ((helpers.thread_step_ops[K] = &StaticKernel::native_thread_step_kind<K>),
            ...);
        ((helpers.hosted_step_ops[K] = &StaticKernel::native_hosted_step_kind<K>),
            ...);
        ((helpers.boundary_ops[K] = &StaticKernel::native_boundary_kind<K>), ...);
    }(std::make_index_sequence<operation_kind_count>());
}

void Interpreter::Impl::StaticKernel::move_wide(const CompiledBody& body,
    const KInst& inst, const std::uint32_t at, const std::uint32_t member_index,
    const std::uint32_t source_slot, const Word base,
    const std::uint32_t target_slot)
{
    // Bit ranges of word planes; widths are at most 64.
    const auto read_bits = [](const std::uint64_t* plane, const std::uint32_t words,
                               const std::uint64_t bit, const std::uint32_t count) {
        const auto word = bit / 64U;
        const auto shift = bit % 64U;
        auto value = plane[word] >> shift;
        if (shift != 0U && shift + count > 64U && word + 1U < words) {
            value |= plane[word + 1U] << (64U - shift);
        }
        return value & kw::mask(count);
    };
    const auto write_bits = [](std::uint64_t* plane, const std::uint64_t bit,
                                const std::uint32_t count, const std::uint64_t value) {
        const auto word = bit / 64U;
        const auto shift = bit % 64U;
        const auto m = kw::mask(count);
        plane[word] = (plane[word] & ~(m << shift)) | ((value & m) << shift);
        if (shift != 0U && shift + count > 64U) {
            const auto spill = 64U - shift;
            plane[word + 1U] = (plane[word + 1U] & ~(m >> spill))
                | ((value & m) >> spill);
        }
    };
    const auto& source = slots_[source_slot];
    const auto* source_a = arena_.data() + source.offset;
    const auto* source_b = source_a + source.words;
    const auto width = inst.width;
    const auto words = (width + 63U) / 64U;
    auto& value = scratch().wide_move;
    value.assign(2U * words, 0U);
    auto* value_a = value.data();
    auto* value_b = value_a + words;
    const auto copy = [&](const std::uint64_t from, const std::uint64_t to,
                          const std::uint64_t count) {
        for (std::uint64_t done = 0U; done < count; done += 64U) {
            const auto chunk = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(64U, count - done));
            write_bits(value_a, to + done, chunk,
                read_bits(source_a, source.words, from + done, chunk));
            write_bits(value_b, to + done, chunk,
                read_bits(source_b, source.words, from + done, chunk));
        }
    };
    if (inst.sub == 0U) {
        copy(0U, 0U, width);
    } else {
        // dynamic_part_select_value: bits outside the source stay X (0 for
        // two-state); an unknown base selects nothing.
        if ((inst.flags & flag_two_state) == 0U) {
            for (std::uint32_t word = 0U; word < words; ++word) {
                const auto m = kw::mask(std::min(64U, width - 64U * word));
                value_a[word] = m;
                value_b[word] = m;
            }
        }
        const auto& part = body.parts[inst.aux];
        if ((base.b & kw::mask(32U)) == 0U) {
            const auto signed_base = static_cast<std::int64_t>(
                static_cast<std::int32_t>(static_cast<std::uint32_t>(base.a)));
            const auto lower = std::min(part.left, part.right);
            const auto upper = std::max(part.left, part.right);
            const auto edge = static_cast<std::int64_t>(width - 1U);
            const bool increasing = (inst.flags & flag_increasing) != 0U;
            const bool descending = (inst.flags & flag_descending) != 0U;
            const auto selected_right = increasing
                ? signed_base + (descending ? 0 : edge)
                : signed_base - (descending ? edge : 0);
            const auto source_width = static_cast<std::int64_t>(source.width);
            if ((descending && part.right == lower)
                || (!descending && part.right == upper)) {
                // Destination bit i reads source bit first + i.
                const auto first = static_cast<std::int64_t>(part.base_offset)
                    + (descending ? selected_right - part.right
                                  : part.right - selected_right);
                auto low = descending ? lower - selected_right
                                      : selected_right - upper;
                auto high = descending ? upper - selected_right
                                       : selected_right - lower;
                low = std::max<std::int64_t>(low, 0);
                high = std::min<std::int64_t>(high, edge);
                high = std::min<std::int64_t>(high, source_width - 1 - first);
                low = std::max<std::int64_t>(low, -first);
                if (low <= high) {
                    copy(static_cast<std::uint64_t>(first + low),
                        static_cast<std::uint64_t>(low),
                        static_cast<std::uint64_t>(high - low + 1));
                }
            } else {
                for (std::uint32_t bit = 0U; bit < width; ++bit) {
                    const auto selected = descending
                        ? selected_right + static_cast<std::int64_t>(bit)
                        : selected_right - static_cast<std::int64_t>(bit);
                    if (selected < lower || selected > upper) {
                        continue;
                    }
                    const auto offset
                        = support::index_distance(selected, part.right);
                    if (offset > std::numeric_limits<std::uint32_t>::max()
                                - part.base_offset
                        || part.base_offset + offset >= source.width) {
                        continue;
                    }
                    copy(part.base_offset + offset, bit, 1U);
                }
            }
        }
    }
    // The write: one word write per 64-bit chunk, as the reference's wide
    // write notifies once per changed range.
    for (std::uint32_t word = 0U; word < words; ++word) {
        const auto chunk = std::min(64U, width - 64U * word);
        const Word next { value_a[word], value_b[word] };
        const auto offset = inst.offset + 64U * word;
        if ((inst.flags & flag_nba) != 0U) {
            StaticKernelWrite write;
            write.slot = target_slot;
            write.offset = offset;
            write.width = chunk;
            write.member = member_index;
            write.a = next.a;
            write.b = next.b;
            write.instruction = word != 0U ? at | write_continues : at;
            push_write(write);
        } else {
            write_slot_word(target_slot, next, offset, chunk, member_index);
        }
    }
}

PackedLogic4 Interpreter::Impl::StaticKernel::wide_value(
    const CompiledBody& body, const Word* registers, const std::uint32_t reg,
    const ValueKind kind)
{
    return wide_value_at(registers, body.wide_base[reg],
        body.register_widths[reg], kind);
}

PackedLogic4 Interpreter::Impl::StaticKernel::wide_value_at(
    const Word* registers, const std::uint32_t entry, const std::uint32_t width,
    const ValueKind kind)
{
    const auto base = entry & ~wide_unknown_flag;
    const auto chunks = wide_chunks(width);
    // As uword_value: a value with 'U' or weak bits is Logic9 whatever the
    // kind.
    bool unknown = false;
    for (std::uint32_t chunk = 0U;
         chunk < chunks && (entry & wide_unknown_flag) != 0U; ++chunk) {
        unknown = unknown || registers[base + chunks + chunk].a != 0U
            || registers[base + chunks + chunk].b != 0U;
    }
    if (!unknown) {
        // One buffer for both planes; short values stay off the heap.
        std::array<std::uint64_t, 16> inline_planes;
        std::vector<std::uint64_t> heap_planes;
        auto* planes = inline_planes.data();
        if (2U * chunks > inline_planes.size()) {
            heap_planes.resize(2U * chunks);
            planes = heap_planes.data();
        }
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            planes[chunk] = registers[base + chunk].a;
            planes[chunks + chunk] = registers[base + chunk].b;
        }
        auto result = PackedLogic4::from_word_planes(width,
            { planes, chunks }, { planes + chunks, chunks });
        return kind == ValueKind::logic9 ? result.promoted_to_logic9() : result;
    }
    std::array<std::vector<std::uint64_t>, 4> planes;
    for (auto& plane : planes) {
        plane.resize(chunks);
    }
    for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
        const auto codes = kw::logic9_planes(registers[base + chunk],
            std::min(64U, width - 64U * chunk), registers[base + chunks + chunk].a,
            registers[base + chunks + chunk].b);
        planes[0][chunk] = codes.p0;
        planes[1][chunk] = codes.p1;
        planes[2][chunk] = codes.p2;
        planes[3][chunk] = codes.p3;
    }
    return PackedLogic4::from_logic9_word_planes(width, planes[0], planes[1],
        planes[2], planes[3]);
}

void Interpreter::Impl::StaticKernel::set_wide_value(const CompiledBody& body,
    Word* registers, const std::uint32_t reg, const PackedLogic4& value)
{
    set_wide_value_at(body, registers, body.wide_base[reg],
        body.register_widths[reg], value);
}

void Interpreter::Impl::StaticKernel::set_wide_value_at(const CompiledBody& body,
    Word* registers, const std::uint32_t entry, const std::uint32_t width,
    const PackedLogic4& value)
{
    const auto base = entry & ~wide_unknown_flag;
    const auto chunks = wide_chunks(width);
    if ((entry & wide_unknown_flag) == 0U || !value.is_logic9()) {
        const auto logic4 = Impl::coerce_value_kind(value, ValueKind::logic4);
        const auto a = logic4.aval_words();
        const auto b = logic4.bval_words();
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            const auto m = kw::mask(std::min(64U, width - 64U * chunk));
            registers[base + chunk] = { (chunk < a.size() ? a[chunk] : 0U) & m,
                (chunk < b.size() ? b[chunk] : 0U) & m };
            if ((entry & wide_unknown_flag) != 0U) {
                registers[base + chunks + chunk] = { };
            }
        }
        return;
    }
    // Every Logic9 code has a representation: the word and its masks. A
    // design without weak sources (CompiledBody::weak) has no weak mask.
    const auto plane = [&](const std::uint32_t index, const std::uint32_t chunk) {
        const auto words = value.logic9_plane_words(index);
        return chunk < words.size() ? words[chunk] : 0U;
    };
    for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
        const auto field = kw::logic9_uword(plane(0U, chunk), plane(1U, chunk),
            plane(2U, chunk), plane(3U, chunk), std::min(64U, width - 64U * chunk));
        registers[base + chunk] = field.value;
        registers[base + chunks + chunk]
            = { field.unknown, body.weak != 0U ? field.weak : 0U };
    }
}

const char* Interpreter::Impl::StaticKernel::execute_wide_alu(const CompiledBody& body,
    Word* registers, const KInst& inst)
{
    const auto family = static_cast<WideFamily>(inst.offset);
    const auto has = [&](const std::uint32_t group, const std::uint32_t slot) {
        return (inst.aux & (group << slot)) != 0U;
    };
    const auto kind = [&](const std::uint32_t slot) {
        return has(wide_alu_logic9, slot) ? ValueKind::logic9 : ValueKind::logic4;
    };
    const auto entry = [&](const std::uint32_t slot, const std::uint32_t reg) {
        return reg | (has(wide_alu_masks, slot) ? wide_unknown_flag : 0U);
    };
    // Operand `slot` named `reg`, of `width` bits: wide chunks, or a narrow
    // register with its shadow masks (as register_operand reads one).
    const auto operand = [&](const std::uint32_t slot, const std::uint32_t reg,
                             const std::uint32_t width) {
        if (has(wide_alu_wide, slot)) {
            return wide_value_at(registers, entry(slot, reg), width, kind(slot));
        }
        const bool shadowed = body.shadow_base != 0U && reg < body.tracked.size()
            && body.tracked[reg] != 0U;
        const auto unknown = shadowed ? registers[body.shadow_base + reg].a : 0U;
        const auto weak = shadowed && body.weak != 0U
            ? registers[body.shadow_base + reg].b : 0U;
        return uword_value(registers[reg], unknown, width, kind(slot), weak);
    };
    const auto x_width = static_cast<std::uint32_t>(inst.imm_b);
    const auto y_width = static_cast<std::uint32_t>(inst.imm_a);
    PackedLogic4 result;
    switch (family) {
    case WideFamily::binary:
        result = binary_value(static_cast<BinaryOperator>(inst.sub),
            operand(1U, inst.x, inst.width), operand(2U, inst.y, inst.width));
        break;
    case WideFamily::shift:
        result = shift_value(static_cast<ShiftOperator>(inst.sub),
            operand(1U, inst.x, inst.width), operand(2U, inst.y, y_width),
            (inst.flags & flag_signed) != 0U);
        break;
    case WideFamily::unary_not:
        result = unary_not(operand(1U, inst.x, inst.width));
        break;
    case WideFamily::reduce:
        result = reduce_value(static_cast<ReductionOperator>(inst.sub),
            operand(1U, inst.x, inst.width));
        break;
    case WideFamily::conditional:
        result = conditional_value(operand(1U, inst.x, 1U),
            operand(2U, inst.y, inst.width), operand(3U, inst.z, inst.width));
        break;
    case WideFamily::logical_not:
        result = logical_not(operand(1U, inst.x, inst.width));
        break;
    case WideFamily::logical_binary:
        result = logical_binary(static_cast<LogicalBinaryOperator>(inst.sub),
            operand(1U, inst.x, x_width), operand(2U, inst.y, y_width));
        break;
    case WideFamily::two_state: {
        // ConvertToTwoState: the elements that read as 1.
        const auto source = operand(1U, inst.x, inst.width);
        result = PackedLogic4(source.width(), Logic4::zero);
        for (std::size_t bit = 0U; bit < source.width(); ++bit) {
            if (to_logic4(source.get_logic9(bit)) == Logic4::one) {
                result.set(bit, Logic4::one);
            }
        }
        break;
    }
    case WideFamily::extract:
        // imm_a the offset, imm_b the source's width.
        result = operand(1U, inst.x, x_width).extract_bits(
            static_cast<std::size_t>(inst.imm_a), inst.width);
        break;
    case WideFamily::insert:
        // imm_a the source's width, imm_b the offset.
        result = operand(1U, inst.x, inst.width);
        result.insert_bits(operand(2U, inst.y, y_width),
            static_cast<std::size_t>(inst.imm_b));
        break;
    case WideFamily::dynamic_part_select: {
        // imm_a the source's width, imm_b the part, y the 32 bit base.
        const auto& part = body.parts[inst.imm_b];
        result = dynamic_part_select_value(
            operand(1U, inst.x, static_cast<std::uint32_t>(inst.imm_a)),
            operand(2U, inst.y, 32U), part.left, part.right, part.base_offset,
            inst.width, (inst.flags & flag_increasing) != 0U,
            (inst.flags & flag_descending) != 0U, (inst.flags & flag_two_state) != 0U);
        break;
    }
    case WideFamily::dynamic_insert: {
        // IEEE 1800-2023 11.5.1: an unknown or out-of-range index, or a
        // field past the target, writes nothing (strict: an error).
        const auto offset
            = kw::dynamic_index(registers[inst.z], body.indices[inst.imm_b]);
        result = operand(1U, inst.x, inst.width);
        if (!offset || *offset > inst.width || y_width > inst.width - *offset) {
            if ((inst.flags & flag_strict) != 0U) {
                if (!offset) {
                    // The reference's message for an index outside the
                    // declared range or with unknown bits.
                    static thread_local std::string reference_message;
                    try {
                        (void)dynamic_index_offset(
                            PackedLogic4::from_aval_bval(32U, registers[inst.z].a,
                                registers[inst.z].b),
                            body.indices[inst.imm_b]);
                    } catch (const std::invalid_argument& error) {
                        reference_message = error.what();
                        return reference_message.c_str();
                    }
                }
                return "dynamic packed index is invalid";
            }
        } else {
            result.insert_bits(operand(2U, inst.y, y_width), *offset);
        }
        break;
    }
    }
    if (has(wide_alu_wide, 0U)) {
        set_wide_value_at(body, registers, entry(0U, inst.d), inst.width, result);
        return nullptr;
    }
    const auto front = [](const std::span<const std::uint64_t> words) {
        return words.empty() ? std::uint64_t { 0 } : words.front();
    };
    const bool shadowed = body.shadow_base != 0U && inst.d < body.tracked.size()
        && body.tracked[inst.d] != 0U;
    if (result.is_logic9()) {
        // The matching equality of std_ulogic values gives 'U' where an
        // element pair of 'U' decides it (binary_value): X in the word, the
        // result's 'U' mask set (the narrow KOp::binary does the same).
        const auto code = result.get_logic9(0U);
        const auto word = kw::from_logic4(to_logic4(code));
        registers[inst.d] = { word.a & 1U, word.b & 1U };
        if (shadowed) {
            registers[body.shadow_base + inst.d] = { code == Logic9::u ? 1U : 0U, 0U };
        }
        return nullptr;
    }
    registers[inst.d] = { front(result.aval_words()) & 1U,
        front(result.bval_words()) & 1U };
    if (shadowed) {
        registers[body.shadow_base + inst.d] = { };
    }
    return nullptr;
}

void Interpreter::Impl::StaticKernel::execute_wide(const CompiledBody& body,
    Word* registers, const KInst& inst, const std::uint32_t at,
    const std::uint32_t member_index, const std::uint32_t body_begin,
    const Word y, const Word y_masks, const Word z,
    const std::uint32_t resolved)
{
    const auto chunks = wide_chunks(inst.width);
    const auto chunk_width = [&](const std::uint32_t chunk) {
        return std::min(64U, inst.width - 64U * chunk);
    };
    switch (inst.op) {
    case KOp::wide_load: {
        if (inst.sub >= 4U) {
            // A wide $sampled or $past (IEEE 1800-2023 16.5.1, 16.9.3): the
            // chunks of the Preponed value (sub 4) or of a ring entry (sub
            // 5), read as the narrow KOp::sva_sampled_load reads them. A
            // register with masks gets them cleared (Logic4 values).
            if (inst.sub == 4U) {
                const auto* const words = impl_.sampled_value_words.data() + inst.x;
                for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                    const auto m = kw::mask(chunk_width(chunk));
                    registers[inst.d + chunk] = { words[chunk] & m,
                        words[chunks + chunk] & m };
                }
            } else {
                using Ring = Impl::SampledRing;
                const auto* const ring = impl_.sampled_ring_words.data() + inst.x;
                const SampledEntryReader reader { ring,
                    body.wide_constants.data() + inst.imm_b, chunks,
                    (inst.imm_a >> 32U) != 0U ? 4U : 2U };
                const auto count_now = ring[Ring::count];
                std::uint64_t index = count_now - 1U;
                bool have = count_now >= 1U;
                if (inst.offset == 1U) {
                    const bool sampled_now = ring[Ring::slot_valid] != 0U
                        && ring[Ring::slot_time]
                            == static_cast<std::uint64_t>(kernel_now())
                        && ring[Ring::slot_process_domain]
                            == static_cast<std::uint64_t>(
                                ProcessSchedulingDomain::systemverilog);
                    const auto prior = count_now - (sampled_now ? 1U : 0U);
                    index = prior - inst.z;
                    have = prior >= inst.z;
                }
                for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                    registers[inst.d + chunk]
                        = reader.coerced(have, index, chunk, chunk_width(chunk));
                }
            }
            if (inst.aux != 0U) {
                for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                    registers[inst.d + chunks + chunk] = { };
                }
            }
            return;
        }
        if (inst.sub == 3U) {
            // A host signal that has no mirror (resolved: the signal): its
            // chunks, with the masks of the nine values in a register that
            // has them (as load_host sub 1), else coerced to four states.
            set_wide_value_at(body, registers,
                inst.d | (inst.aux != 0U ? wide_unknown_flag : 0U), inst.width,
                impl_.logical_signal_value(resolved));
            return;
        }
        const auto& slot = slots_[resolved];
        const auto* planes = arena_.data() + slot.offset;
        const auto words = static_cast<std::uint32_t>(inst.imm_a);
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            const auto m = kw::mask(chunk_width(chunk));
            switch (inst.sub) {
            case 0U:
                registers[inst.d + chunk] = { planes[chunk] & m,
                    planes[words + chunk] & m };
                if (inst.aux != 0U) {
                    registers[inst.d + chunks + chunk] = { };
                }
                break;
            case 1U: {
                const auto field = kw::logic9_uword(planes[chunk],
                    planes[words + chunk], planes[2U * words + chunk],
                    planes[3U * words + chunk], chunk_width(chunk));
                registers[inst.d + chunk] = field.value;
                registers[inst.d + chunks + chunk] = { field.unknown, field.weak };
                break;
            }
            default:
                registers[inst.d + chunk] = kw::logic9_coerced(planes[chunk],
                    planes[words + chunk], planes[2U * words + chunk],
                    planes[3U * words + chunk], chunk_width(chunk));
                if (inst.aux != 0U) {
                    registers[inst.d + chunks + chunk] = { };
                }
                break;
            }
        }
        return;
    }
    case KOp::wide_copy:
        for (std::uint32_t index = 0U; index < inst.aux; ++index) {
            registers[inst.d + index] = registers[inst.x + index];
        }
        if (inst.sub == 1U) {
            // A Logic4 destination reads the source's weak elements as
            // coerce_weak does ('W' and '-' as X); inst.aux is the chunk
            // count, the source's masks follow its chunks.
            for (std::uint32_t index = 0U; index < inst.aux; ++index) {
                registers[inst.d + index] = kw::coerce_weak(registers[inst.x + index],
                    registers[inst.x + inst.aux + index].b);
            }
        }
        for (std::uint64_t index = 0U; index < inst.imm_a; ++index) {
            registers[inst.d + inst.aux + index] = { };
        }
        return;
    case KOp::wide_const:
        for (std::uint32_t index = 0U; index < inst.aux; ++index) {
            registers[inst.d + index] = body.wide_constants[inst.imm_a + index];
        }
        return;
    case KOp::wide_concat: {
        // Into scratch first: an operand may be the destination.
        auto& value = scratch().wide_concat;
        value.assign(4U * chunks, 0U);
        const auto place = [&](const Word raw, const Word masks,
                               const std::uint32_t width,
                               const std::uint32_t offset) {
            const auto m = kw::mask(width);
            const auto word = offset / 64U;
            const auto shift = offset % 64U;
            // A Logic4 result reads a weak element through coerce_weak ('W'
            // and '-' as X; 'H' and 'L' keep their 1 and 0): the word of '-'
            // is Z, so its a bit is set.
            const Word bits = inst.sub != 1U
                ? Word { raw.a | (masks.b & raw.b), raw.b } : raw;
            const std::uint64_t parts[4] { bits.a & m, bits.b & m, masks.a & m,
                masks.b & m };
            for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
                value[plane * chunks + word] |= parts[plane] << shift;
                if (shift != 0U && shift + width > 64U) {
                    value[plane * chunks + word + 1U] |= parts[plane] >> (64U - shift);
                }
            }
        };
        std::uint32_t offset = 0U;
        for (std::uint32_t index = inst.x; index-- > 0U;) {
            const auto& operand = body.concat[inst.aux + index];
            if (operand.width <= 64U) {
                const bool shadowed = body.shadow_base != 0U
                    && operand.reg < body.tracked.size()
                    && body.tracked[operand.reg] != 0U;
                place(registers[operand.reg],
                    shadowed ? Word { registers[body.shadow_base + operand.reg].a,
                                   body.weak != 0U
                                       ? registers[body.shadow_base + operand.reg].b
                                       : 0U }
                             : Word { },
                    operand.width, offset);
            } else {
                const auto count = wide_chunks(operand.width);
                for (std::uint32_t chunk = 0U; chunk < count; ++chunk) {
                    place(registers[operand.reg + chunk],
                        operand.unknown != 0U
                            ? registers[operand.reg + count + chunk] : Word { },
                        std::min(64U, operand.width - 64U * chunk),
                        offset + 64U * chunk);
                }
            }
            offset += operand.width;
        }
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            registers[inst.d + chunk] = { value[chunk], value[chunks + chunk] };
            if (inst.sub == 1U) {
                registers[inst.d + chunks + chunk]
                    = { value[2U * chunks + chunk], value[3U * chunks + chunk] };
            }
        }
        return;
    }
    case KOp::wide_store: {
        if (inst.sub == 3U) {
            // A host signal's write (resolved: the signal): the chunks, with
            // the 'U' and weak elements of a register that has masks, become
            // the host's value, whole or at a static offset (flag_linear),
            // a dynamic index (imm_a the position in indices + 1; IEEE
            // 1800-2023 11.5.1: an unknown or out-of-range one writes
            // nothing, a strict one is an error) or a dynamic part (imm_a
            // from bit 32; y the base).
            auto value = wide_value_at(registers,
                inst.x | (inst.aux != 0U ? wide_unknown_flag : 0U), inst.width,
                ValueKind::logic4);
            std::optional<std::uint32_t> offset;
            if ((inst.flags & flag_linear) != 0U) {
                offset = inst.offset;
            } else if (inst.imm_a != 0U) {
                const auto position = static_cast<std::uint32_t>(inst.imm_a & 0xffffffffU) - 1U;
                try {
                    if ((inst.imm_a >> 32U) == 0U) {
                        const auto selected = kw::dynamic_index(y, body.indices[position]);
                        if (!selected) {
                            if ((inst.flags & flag_strict) != 0U) {
                                fail(member_index, body_begin + at,
                                    "dynamic packed index contains an unknown or "
                                    "high-impedance value or is outside the declared "
                                    "range");
                            }
                            return;
                        }
                        offset = *selected;
                    } else {
                        auto write = dynamic_part_write_value(value,
                            PackedLogic4::from_aval_bval(32U, y.a, y.b),
                            body.parts[position]);
                        if (!write) {
                            return;
                        }
                        value = std::move(write->value);
                        offset = write->offset;
                    }
                } catch (const std::invalid_argument& error) {
                    fail(member_index, body_begin + at, error.what());
                }
            }
            write_host(member_index, resolved, std::move(value), offset,
                (inst.flags & flag_blocking) != 0U,
                static_cast<SignalUpdateDomain>(inst.imm_b));
            return;
        }
        auto& slot = slots_[resolved];
        if (inst.offset > slot.width || inst.width > slot.width - inst.offset) {
            fail(member_index, body_begin + at,
                "partial update range is outside its target signal");
        }
        const auto unknown = [&](const std::uint32_t chunk) {
            return inst.aux != 0U ? registers[inst.x + chunks + chunk].a
                    & kw::mask(chunk_width(chunk))
                                  : 0U;
        };
        const auto weak = [&](const std::uint32_t chunk) {
            return inst.aux != 0U && body.weak != 0U
                ? registers[inst.x + chunks + chunk].b & kw::mask(chunk_width(chunk))
                : 0U;
        };
        if (inst.sub == 2U && (inst.flags & flag_blocking) != 0U) {
            // A shared variable: immediate, one change report.
            last_writer_[resolved] = member_process_[member_index];
            const WordSnapshot before(
                arena_.data() + slot.offset, slot.planes * slot.words);
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                store_slot_word(resolved, registers[inst.x + chunk],
                    inst.offset + 64U * chunk, chunk_width(chunk), unknown(chunk),
                    weak(chunk));
            }
            if (!std::equal(before.begin(), before.end(),
                    arena_.begin() + slot.offset)) {
                slot_changed(resolved, 0U, before.data());
            }
            return;
        }
        // One write per 64-bit chunk (as KOp::wide_move).
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            const auto m = kw::mask(chunk_width(chunk));
            const auto value = registers[inst.x + chunk];
            const auto offset = inst.offset + 64U * chunk;
            if (inst.sub == 0U) {
                // A Logic4 slot reads 'W' and '-' as X.
                write_slot_word(resolved,
                    kw::coerce_weak({ value.a & m, value.b & m }, weak(chunk)), offset,
                    chunk_width(chunk), member_index);
                continue;
            }
            StaticKernelWrite write;
            write.slot = resolved;
            write.offset = offset;
            write.width = chunk_width(chunk);
            write.member = member_index;
            write.a = value.a & m;
            write.b = value.b & m;
            write.unknown = unknown(chunk);
            write.weak = weak(chunk);
            write.instruction = chunk != 0U ? (body_begin + at) | write_continues
                                            : body_begin + at;
            if (slot.resolved) {
                // A VHDL assignment to a resolved slot resolves into its next
                // block now (kind 4); the queued write marks the slot for the
                // update phase.
                write.kind = 4U;
                set_net_bits(resolved, member_process_[member_index], offset,
                    write.width,
                    kw::logic9_planes({ write.a, write.b }, write.width, write.unknown,
                        write.weak),
                    true);
            }
            push_write(write);
        }
        return;
    }
    case KOp::mem_read: {
        // An element of more than 64 bits (sub 1; offset its width, aux
        // whether the destination has masks, width the index's): the
        // element's planes, or X for a two-state-less array (zero for a
        // two-state one) where the index is unknown or outside the array
        // (IEEE 1800-2023 7.4.6, 7.9), as the narrow read.
        const auto& container = containers_[resolved];
        const auto& type = container.type;
        const auto element_width = inst.offset;
        auto value = type.two_state ? PackedLogic4(element_width, Logic4::zero)
                                    : PackedLogic4(element_width, Logic4::x);
        if (kw::known(y)) {
            const auto index = kw::sign_extend(y.a, inst.width);
            if ((inst.flags & flag_linear) != 0U) {
                if (index >= 0 && static_cast<std::uint64_t>(index) < container.count) {
                    value = container_element(container, static_cast<std::size_t>(index));
                }
            } else {
                const auto low = std::min(type.index_left, type.index_right);
                const auto high = std::max(type.index_left, type.index_right);
                if (index >= low && index <= high) {
                    value = container_element(container,
                        fixed_offset(type, static_cast<std::int32_t>(index)));
                }
            }
        }
        set_wide_value_at(body, registers,
            inst.d | (inst.aux != 0U ? wide_unknown_flag : 0U), element_width, value);
        return;
    }
    case KOp::mem_write: {
        // A write of an element of more than 64 bits (sub 1; width the
        // value's, offset the index's, aux 1 + the part when it is a
        // dynamic part-select write with base register z, imm_b bit 0 masks
        // in the source): the reference's element write, now or queued in
        // the update phase (flag_nba).
        Pending pending;
        pending.kind = inst.aux != 0U ? PendingKind::element_part : PendingKind::element;
        pending.member = member_index;
        pending.instruction = body_begin + at;
        pending.target = resolved;
        pending.value = wide_value_at(registers,
            inst.x | ((inst.imm_b & 1U) != 0U ? wide_unknown_flag : 0U), inst.width,
            ValueKind::logic4);
        pending.index = PackedLogic4::from_aval_bval(inst.offset, y.a, y.b);
        pending.signed_index = (inst.flags & flag_signed) != 0U;
        pending.linear_index = (inst.flags & flag_linear) != 0U;
        if (inst.aux != 0U) {
            pending.base = PackedLogic4::from_aval_bval(32U, z.a, z.b);
            pending.part = body.parts[inst.aux - 1U];
        }
        if ((inst.flags & flag_nba) != 0U) {
            push_generic_pending(std::move(pending));
        } else {
            write_element(pending);
        }
        return;
    }
    case KOp::wide_part_insert: {
        const auto& part = body.parts[inst.aux];
        const auto write = kw::dynamic_part_write(y, z, part);
        // An invalid write, or one outside the target, is the reference's
        // to report (nothing written yet).
        if (!write.valid
            || (write.write
                && (write.write->offset > inst.width
                    || write.write->width > inst.width - write.write->offset))) {
            throw KernelDeopt { at, false };
        }
        const bool masks = inst.imm_a != 0U;
        if (inst.d != inst.x) {
            const auto count = masks ? 2U * chunks : chunks;
            for (std::uint32_t index = 0U; index < count; ++index) {
                registers[inst.d + index] = registers[inst.x + index];
            }
        }
        if (!write.write) {
            return;
        }
        const auto field_masks = (y_masks.a | y_masks.b) != 0U
            ? kw::dynamic_part_write(y_masks, z, part).write->value
            : Word { };
        const auto width = write.write->width;
        const auto m = kw::mask(width);
        const auto word = write.write->offset / 64U;
        const auto shift = write.write->offset % 64U;
        const auto insert = [&](const std::uint32_t chunk, const bool high) {
            const auto field_mask = high ? m >> (64U - shift) : m << shift;
            auto& target = registers[inst.d + chunk];
            const auto place = [&](const std::uint64_t bits) {
                return high ? (bits & m) >> (64U - shift) : (bits & m) << shift;
            };
            target.a = (target.a & ~field_mask) | place(write.write->value.a);
            target.b = (target.b & ~field_mask) | place(write.write->value.b);
            if (masks) {
                auto& mask = registers[inst.d + chunks + chunk];
                mask.a = (mask.a & ~field_mask) | place(field_masks.a);
                mask.b = (mask.b & ~field_mask) | place(field_masks.b);
            }
        };
        insert(word, false);
        if (shift != 0U && shift + width > 64U) {
            insert(word + 1U, true);
        }
        return;
    }
    default:
        break;
    }
    fail(member_index, body_begin + at, "static kernel wide instruction is invalid");
}

template <KOp Op>
void Interpreter::Impl::StaticKernel::native_wide(StaticKernelNativeFrame* frame,
    Word* registers, const std::uint32_t at, const std::uint64_t xa,
    const std::uint64_t xb, const std::uint64_t ya, const std::uint64_t yb,
    const std::uint64_t za, const std::uint64_t zb, std::uint64_t* out)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    if (kernel.profile_) {
        ++kernel.profile_native_calls_[1];
    }
    out[0] = 0U;
    out[1] = 0U;
    out[2] = 0U;
    out[3] = 0U;
    try {
        const auto& body = *static_cast<const CompiledBody*>(frame->program);
        const auto& inst = body.code[at];
        const auto& member = kernel.members_[frame->member];
        const auto body_begin = member.vhdl ? 0U : member.body_begin;
        kernel.scratch().running_position = frame->position;
        if constexpr (Op == KOp::dynamic_extract || Op == KOp::dynamic_part_select) {
            kernel.scratch().wide_file = registers;
            const auto result = kernel.evaluate_slow_op<Op>(body, inst, at,
                frame->member, body_begin, { }, { ya, yb }, { }, 0U,
                &static_cast<CompiledBody*>(frame->instance)->wide_registers);
            out[0] = result.a;
            out[1] = result.b;
            out[2] = kernel.scratch().last_unknown;
            out[3] = kernel.scratch().last_weak;
        } else {
            std::uint32_t resolved = 0U;
            if constexpr (Op == KOp::wide_load) {
                // Sub 3 reads a host signal: its binding is the signal.
                // Sub 4 and 5 read the sampled words and rings: no binding.
                resolved = inst.sub >= 4U ? 0U
                    : inst.sub == 3U
                    ? frame->bindings[2U * inst.x]
                    : frame->bindings[2U * inst.x + 1U] & ~partition_tag;
            } else if constexpr (Op == KOp::wide_store) {
                resolved = inst.sub == 3U
                    ? frame->bindings[2U * inst.d]
                    : frame->bindings[2U * inst.d + 1U] & ~partition_tag;
            } else if constexpr (Op == KOp::mem_read) {
                // A container's binding (kind 3).
                resolved = frame->bindings[2U * inst.x];
            } else if constexpr (Op == KOp::mem_write) {
                resolved = frame->bindings[2U * inst.d];
            }
            // x carries the 'U' (xa) and weak (xb) masks of y.
            kernel.execute_wide(body, registers, inst, at, frame->member, body_begin,
                { ya, yb }, { xa, xb }, { za, zb }, resolved);
        }
    } catch (const KernelDeopt& deopt) {
        kernel.scratch().pending_deopt = deopt;
        if (kernel.scratch().pending_deopt->resume == 0xffffffffU) {
            kernel.scratch().pending_deopt->resume = at;
        }
        frame->status = 2U;
    } catch (...) {
        kernel.scratch().native_exception = std::current_exception();
        frame->status = 1U;
    }
}

} // namespace fsim::runtime::simir

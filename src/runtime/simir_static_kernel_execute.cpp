// SPDX-License-Identifier: Apache-2.0
//
// KIR execution of the engine v4 static kernel: the interpreter tier,
// deoptimization, the generic bridge and the slow instruction semantics.
#include "simir_static_kernel_compiled_internal.hpp"

#include <cxxabi.h>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <typeinfo>
#include <unordered_map>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;
using namespace static_kernel_compiled_detail;
namespace kw = kernel_word;

void Interpreter::Impl::StaticKernel::execute(
    CompiledBody& body, std::uint32_t member_index)
{
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
    const auto set_unknown = [&](const std::uint32_t reg, const std::uint64_t value) {
        if (shadow_base != 0U && reg < shadow_base && body.tracked[reg] != 0U) {
            registers[shadow_base + reg] = { value, 0U };
        }
    };
    try {
    while (pc < size) {
        if (++steps > run_step_limit && !behavioral) {
            fail_at(pc, "static kernel member did not terminate");
        }
        const auto& inst = code[pc];
        const auto at = pc;
        executing = at;
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
        if ((mode & u_aware) != 0U) {
            switch (inst.op) {
            case KOp::constant:
                result_unknown = constant_unknown(inst.offset, inst.aux);
                break;
            case KOp::copy:
                result_unknown = unknown(inst.x);
                break;
            case KOp::extract:
                result_unknown = inst.sub == 2U ? 0U
                    : (unknown(inst.x) >> inst.offset) & kw::mask(inst.width);
                break;
            case KOp::insert: {
                const auto field = kw::mask(inst.width) << inst.offset;
                result_unknown = (unknown(inst.x) & ~field)
                    | ((unknown(inst.y) << inst.offset) & field);
                break;
            }
            case KOp::concat: {
                std::uint32_t offset = 0U;
                for (std::uint32_t index = inst.x; index-- > 0U;) {
                    const auto& operand = body.concat[inst.aux + index];
                    result_unknown |= (unknown(operand.reg)
                                          & kw::mask(operand.width))
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
                default:
                    result_unknown = (left | right) & m;
                    break;
                }
                break;
            }
            case KOp::unary_not:
                result_unknown = unknown(inst.x);
                break;
            case KOp::conditional: {
                const auto condition = registers[inst.x];
                if ((condition.b & 1U) == 0U) {
                    result_unknown = (condition.a & 1U) != 0U ? unknown(inst.y)
                                                              : unknown(inst.z);
                }
                break;
            }
            case KOp::dynamic_insert: {
                const auto target = unknown(inst.x);
                const auto source = unknown(inst.y);
                if ((target | source) == 0U) {
                    break;
                }
                const auto offset = kw::dynamic_index(registers[inst.z],
                    body.indices[inst.aux]);
                if (offset && *offset <= inst.width
                    && inst.offset <= inst.width - *offset) {
                    const auto field = kw::mask(inst.offset) << *offset;
                    result_unknown = (target & ~field)
                        | ((source & kw::mask(inst.offset)) << *offset);
                } else if ((inst.flags & flag_strict) != 0U) {
                    throw KernelDeopt { at, false };
                } else {
                    result_unknown = target;
                }
                break;
            }
            case KOp::dynamic_part_insert: {
                const auto target = unknown(inst.x);
                const auto source = unknown(inst.y);
                if ((target | source) == 0U) {
                    break;
                }
                const auto& part = body.parts[inst.aux];
                const auto write = kw::dynamic_part_write({ source, 0U },
                    registers[inst.z], part);
                if (!write.valid) {
                    throw KernelDeopt { at, false };
                }
                if (!write.write) {
                    result_unknown = target;
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
                break;
            }
            default:
                break;
            }
        }
        profile_operations_ += profile_ ? 1U : 0U;
        switch (inst.op) {
        case KOp::nop:
        case KOp::mem_bind:
            break;
        case KOp::mark:
            running_position_ = inst.d;
            member_index = inst.x;
            current = &members_[member_index];
            break;
        case KOp::generic:
            execute_generic(body, registers, inst, member_index);
            break;
        case KOp::constant:
            registers[inst.d] = { inst.imm_a, inst.imm_b };
            break;
        case KOp::copy:
            registers[inst.d] = registers[inst.x];
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
            break;
        case KOp::extract:
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
            registers[inst.d] = inst.sub == 1U
                ? evaluate_slow(body, inst, at, member_index,
                      origin, { }, registers[inst.y], { }, inst.x,
                      &body.wide_registers)
                : evaluate_slow(body, inst, at, member_index,
                      origin,
                      inst.sub == 2U ? Word { } : registers[inst.x],
                      registers[inst.y], { }, 0U, &body.wide_registers);
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
                inst.op == KOp::store_slot_nba || inst.op == KOp::store_host
                    ? Word { } : registers[inst.y],
                { }, inst.d);
            break;
        case KOp::mem_read:
            registers[inst.d] = evaluate_slow(body, inst, at, member_index,
                origin, { }, registers[inst.y], { }, inst.x,
                &body.wide_registers);
            break;
        case KOp::mem_write:
            effect_slow(body, inst, at, member_index, origin,
                registers[inst.x], registers[inst.y],
                inst.aux != 0U ? registers[inst.z] : Word { }, inst.d);
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
        case KOp::integer_binary:
        case KOp::integer_unary:
            registers[inst.d] = evaluate_slow(body, inst, at, member_index,
                origin, registers[inst.x],
                inst.op == KOp::integer_binary ? registers[inst.y] : Word { },
                { }, inst.x, &body.wide_registers);
            break;
        case KOp::store_vhdl:
            effect_slow(body, inst, at, member_index, origin, registers[inst.x],
                inst.sub == 1U ? registers[inst.y] : Word { },
                { unknown(inst.x), 0U }, inst.d);
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
            if (depth >= inst.z) {
                if ((inst.flags & flag_linear) != 0U) {
                    throw KernelDeopt { at, false };
                }
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
            if (depth == 0U || depth > inst.z) {
                fail_at(at, "call-stack underflow");
            }
            const auto target = registers[inst.y + depth - 1U];
            if (target.b != 0U || target.a >= current->operations.size()) {
                fail_at(at, "call-stack return target is invalid");
            }
            registers[inst.x] = { depth - 1U, 0U };
            // VHDL bodies map SimIR operation n to instruction n; the wait
            // is the exit.
            pc = target.a == current->body_end ? size
                                               : static_cast<std::uint32_t>(target.a);
            break;
        }
        case KOp::deopt:
            throw KernelDeopt { at, false };
        case KOp::suspend:
            behavioral_suspended_ = inst.x;
            return;
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
                set_unknown(inst.d, last_unknown_);
                break;
            case KOp::extract:
                set_unknown(inst.d, inst.sub == 2U ? last_unknown_ : result_unknown);
                break;
            case KOp::store_vhdl:
            case KOp::generic:
                break;
            default:
                set_unknown(inst.d, result_unknown);
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
    auto& member = members_[member_index];
    if (profile_) {
        ++profile_deopts_;
        profile_member_deopts_.resize(members_.size(), 0U);
        ++profile_member_deopts_[member_index];
        if (member_index < profile_names_.size()
            && deopt.resume < member.operations.size()) {
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
            }, member.operations[deopt.resume]);
            ++profile_generic_ops_["deopt " + key];
        }
    }
    for (std::size_t reg = 0U; reg < member.registers.size(); ++reg) {
        const auto width = body.register_widths[reg];
        if (reg == deopt.keep || width == 0U) {
            continue;
        }
        if (width > 64U) {
            if (reg < body.wide_registers.size()) {
                member.registers[reg] = body.wide_registers[reg];
            }
            continue;
        }
        const auto kind = reg < member.register_kinds.size()
            ? member.register_kinds[reg] : ValueKind::logic4;
        const auto unknown = body.shadow_base != 0U && body.tracked[reg] != 0U
            ? body.registers[body.shadow_base + reg].a : 0U;
        member.registers[reg] = uword_value(body.registers[reg], unknown,
            width, kind);
    }
    if (body.call_stack_base != 0U) {
        // The synthetic stack's live entries continue on the runtime stack.
        const auto depth = body.registers[body.call_stack_base].a;
        member.call_stack.clear();
        for (std::uint64_t entry = 0U; entry < depth; ++entry) {
            member.call_stack.push_back(static_cast<InstructionIndex>(
                body.registers[body.call_stack_base + 1U + entry].a));
        }
        body.registers[body.call_stack_base] = { };
    }
    run_generic_from(member_index, deopt.executed ? deopt.resume + 1U
                                                  : deopt.resume);
    sync_compiled_registers(member_index);
}

bool Interpreter::Impl::StaticKernel::execute_generic(CompiledBody& body,
    Word* registers, const KInst& inst, const std::uint32_t member_index,
    const bool two_state_code)
{
    auto& member = members_[member_index];
    const auto& operation = member.operations[inst.x];
    if (profile_) {
        visit_operation([&](const auto& op) {
            int status = 0;
            char* name = abi::__cxa_demangle(typeid(op).name(), nullptr, nullptr,
                &status);
            std::string key = name != nullptr ? name : "?";
            std::free(name);
            key = key.substr(key.rfind(':') + 1U);
            std::vector<RegisterId> regs;
            std::optional<RegisterId> out;
            operation_registers(operation, regs, out);
            key += " w=";
            for (const auto reg : regs) {
                key += std::to_string(body.register_widths[inst.y + reg]) + ",";
            }
            if (out) {
                key += "->" + std::to_string(body.register_widths[inst.y + *out]);
            }
            ++profile_generic_ops_[key];
        }, operation);
    }
    std::vector<RegisterId> reads;
    std::optional<RegisterId> write;
    operation_registers(operation, reads, write);
    const auto base = inst.y;
    for (const auto reg : reads) {
        const auto index = base + reg;
        const auto width = body.register_widths[index];
        if (width <= 64U) {
            const auto kind = reg < member.register_kinds.size()
                ? member.register_kinds[reg] : ValueKind::logic4;
            const auto unknown = body.shadow_base != 0U
                    && index < body.tracked.size() && body.tracked[index] != 0U
                ? registers[body.shadow_base + index].a : 0U;
            member.registers[reg] = uword_value(registers[index], unknown, width,
                kind);
        } else {
            member.registers[reg] = body.wide_registers[index];
        }
    }
    // Behavioral runtime-library operations (output, strings, plusargs)
    // run on the thread's reference state.
    if (member.kind != StaticKernelMemberKind::behavioral
        || !behavioral_step(member_index, inst.x)) {
        (void)step_generic(member_index, inst.x);
    }
    if (write) {
        const auto index = base + *write;
        const auto width = body.register_widths[index];
        auto& value = member.registers[*write];
        if (width <= 64U) {
            if (value.width() != width) {
                fail(member_index, inst.x,
                    "static kernel generic result width is inconsistent");
            }
            const auto word = exact_uword(value);
            const bool tracked = body.shadow_base != 0U
                && index < body.tracked.size() && body.tracked[index] != 0U;
            if (!word || (word->unknown != 0U && !tracked)) {
                throw KernelDeopt { inst.x, true, *write };
            }
            registers[index] = word->value;
            if (tracked) {
                registers[body.shadow_base + index] = { word->unknown, 0U };
            }
            if (two_state_code
                && (word->value.b != 0U || word->unknown != 0U)) {
                // Two-state code cannot continue with this value.
                return true;
            }
        } else {
            body.wide_registers[index] = value;
        }
    }
    return false;
}

void Interpreter::Impl::StaticKernel::native_generic(
    StaticKernelNativeFrame* frame, Word* registers, const std::uint32_t at)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    kernel.profile_native_calls_[3] += kernel.profile_ ? 1U : 0U;
    try {
        const auto& program = *static_cast<const CompiledBody*>(frame->program);
        kernel.running_position_ = frame->position;
        if (kernel.execute_generic(*static_cast<CompiledBody*>(frame->instance),
                registers, program.code[at], frame->member,
                kernel.two_state_running_)) {
            // The template's full code continues after the instruction.
            frame->reserved = at + 1U;
            frame->status = 4U;
        }
    } catch (const KernelDeopt& deopt) {
        kernel.pending_deopt_ = deopt;
        frame->status = 2U;
    } catch (...) {
        kernel.native_exception_ = std::current_exception();
        frame->status = 1U;
    }
}

Interpreter::Impl::StaticKernel::Word
Interpreter::Impl::StaticKernel::evaluate_slow(const CompiledBody& body,
    const KInst& inst, const std::uint32_t at, const std::uint32_t member_index,
    const std::uint32_t body_begin, const Word x, const Word y, const Word z,
    const std::uint32_t resolved, const std::vector<PackedLogic4>* wide)
{
    // Source bit reader: wide slot (sub 1), wide register (sub 2), or x.
    const auto fail_at = [&](const std::string& message) {
        fail(member_index, body_begin + at, message);
    };
    const auto tracks = [&](const std::uint32_t reg) {
        return body.shadow_base != 0U && reg < body.tracked.size()
            && body.tracked[reg] != 0U;
    };
    // Source bits from a wide slot (sub 1) or wide register (sub 2); 'U'
    // elements of a Logic9 register need a tracked destination.
    const auto source_field = [&](const std::uint32_t offset,
                                  const std::uint32_t width) -> kw::UWord {
        if (inst.op == KOp::load_field9 || inst.sub == 1U) {
            const auto& slot = slots_[resolved];
            if (slot.planes != 4U) {
                return { read_field(resolved, offset, width), 0U };
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
                    0U };
            }
            const auto field = kw::logic9_uword(planes[0], planes[1], planes[2],
                planes[3], width);
            if (!field || (field->unknown != 0U && !tracks(inst.d))) {
                throw KernelDeopt { at, false };
            }
            return *field;
        }
        const auto& source = (*wide)[inst.x];
        if (!source.is_logic9()) {
            return { packed_field(source, offset, width), 0U };
        }
        std::uint64_t planes[4] { };
        const auto word = offset / 64U;
        const auto shift = offset % 64U;
        for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
            const auto words = source.logic9_plane_words(plane);
            if (word >= words.size()) {
                return { kw::all_x(width), 0U };
            }
            planes[plane] = words[word] >> shift;
            if (shift != 0U && shift + width > 64U && word + 1U < words.size()) {
                planes[plane] |= words[word + 1U] << (64U - shift);
            }
        }
        const auto field = kw::logic9_uword(planes[0], planes[1], planes[2],
            planes[3], width);
        if (!field || (field->unknown != 0U && !tracks(inst.d))) {
            throw KernelDeopt { at, false };
        }
        return *field;
    };
    // Runs SimIR operation `origin + at` on the reference evaluator with the
    // given operand words and returns its destination as a word.
    const auto reference = [&](const std::initializer_list<
                                   std::pair<RegisterId, Word>> operands,
                               const RegisterId destination) -> Word {
        auto& member = members_[member_index];
        const auto& widths = body.register_widths;
        for (const auto& [reg, value] : operands) {
            const auto kind = reg < member.register_kinds.size()
                ? member.register_kinds[reg] : ValueKind::logic4;
            member.registers[reg] = Impl::coerce_value_kind(
                PackedLogic4::from_aval_bval(widths[reg], value.a, value.b), kind);
        }
        (void)step_generic(member_index, body_begin + at);
        if (const auto word = exact_word(member.registers[destination])) {
            return *word;
        }
        throw KernelDeopt { at, true, destination };
    };
    switch (inst.op) {
    case KOp::load_field9: {
        const auto field = source_field(inst.offset, inst.width);
        last_unknown_ = field.unknown;
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
        if (target >= member.operations.size()) {
            fail_at("call-stack return target is invalid");
        }
        return { target, 0U };
    }
    case KOp::load_slot9: {
        const auto& slot = slots_[resolved];
        const auto* planes = arena_.data() + slot.offset;
        last_unknown_ = 0U;
        if (inst.sub == 1U) {
            return kw::logic9_coerced(planes[0], planes[1], planes[2], planes[3],
                inst.width);
        }
        if (const auto word = kw::logic9_uword(planes[0], planes[1], planes[2],
                planes[3], inst.width);
            word && (word->unknown == 0U || tracks(inst.d))) {
            last_unknown_ = word->unknown;
            return word->value;
        }
        throw KernelDeopt { at, false };
    }
    case KOp::integer_binary: {
        const auto& op = *operation_get_if<IntegerBinary>(
            &members_[member_index].operations[body_begin + at]);
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
        return reference({ { op.lhs, x }, { op.rhs, y } }, op.destination);
    }
    case KOp::integer_unary: {
        const auto& op = *operation_get_if<IntegerUnary>(
            &members_[member_index].operations[body_begin + at]);
        return reference({ { op.source, x } }, op.destination);
    }
    case KOp::load_host: {
        const auto& current = impl_.logical_signal_value(resolved);
        last_unknown_ = 0U;
        if (inst.sub == 1U && current.is_logic9()) {
            if (const auto word = exact_uword(current);
                word && (word->unknown == 0U || tracks(inst.d))) {
                last_unknown_ = word->unknown;
                return word->value;
            }
            throw KernelDeopt { at, false };
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
        last_unknown_ = field.unknown;
        return field.value;
    }
    case KOp::dynamic_extract: {
        last_unknown_ = 0U;
        const auto offset = kw::dynamic_index(y, body.indices[inst.aux]);
        if (inst.sub != 0U && offset && *offset < inst.width) {
            const auto field = source_field(*offset, 1U);
            last_unknown_ = field.unknown;
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
            last_unknown_ = 0U;
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
                    last_unknown_ = source.unknown << shift;
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
                const auto offset = selected >= part.right
                    ? static_cast<std::uint64_t>(selected - part.right)
                    : static_cast<std::uint64_t>(part.right - selected);
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
                last_unknown_ |= source.unknown << bit;
            }
            return result;
        }
        return kw::dynamic_part_select(x, inst.offset, y, part.left, part.right,
            part.base_offset, inst.width, (inst.flags & flag_increasing) != 0U,
            (inst.flags & flag_descending) != 0U,
            (inst.flags & flag_two_state) != 0U);
    }
    case KOp::dynamic_insert: {
        const auto offset = kw::dynamic_index(z, body.indices[inst.aux]);
        if (!offset || *offset > inst.width || inst.offset > inst.width - *offset) {
            if ((inst.flags & flag_strict) != 0U) {
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

void Interpreter::Impl::StaticKernel::effect_slow(const CompiledBody& body,
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
        write.instruction = body_begin + at;
        push_write(write);
    };
    // Runs SimIR operation `body_begin + at` on the reference evaluator with
    // its operand register set to `value`.
    const auto reference = [&](const RegisterId reg, const Word value) {
        auto& member = members_[member_index];
        const auto kind = reg < member.register_kinds.size()
            ? member.register_kinds[reg] : ValueKind::logic4;
        member.registers[reg] = Impl::coerce_value_kind(
            PackedLogic4::from_aval_bval(body.register_widths[reg], value.a,
                value.b),
            kind);
        (void)step_generic(member_index, body_begin + at);
    };
    switch (inst.op) {
    case KOp::store_vhdl: {
        auto offset = inst.offset;
        if (inst.sub == 1U) {
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
        if ((inst.flags & flag_blocking) != 0U) {
            // Shared variable: immediate.
            slot.last_writer = member_process_[member_index];
            std::vector<std::uint64_t> before(arena_.begin() + slot.offset,
                arena_.begin() + slot.offset + slot.planes * slot.words);
            store_slot_word(resolved, x, offset, inst.width, z.a);
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
        write.instruction = body_begin + at;
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
            &members_[member_index].operations[body_begin + at]);
        reference(op.source, x);
        return;
    }
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
        // Native code reports a failed call or return here. The synthetic
        // stack's overflow continues on the runtime-owned stack instead.
        if (inst.op == KOp::call && (inst.flags & flag_linear) != 0U) {
            throw KernelDeopt { at, false };
        }
        if ((x.b & kw::mask(32U)) != 0U) {
            fail_at("call-stack pointer is unknown");
        }
        const auto depth = x.a & kw::mask(32U);
        if (inst.op == KOp::call) {
            fail_at("call-stack capacity is exhausted");
        }
        if (depth == 0U || depth > inst.z) {
            fail_at("call-stack underflow");
        }
        fail_at("call-stack return target is invalid");
        return;
    }
    case KOp::deopt:
        throw KernelDeopt { at, false };
    case KOp::assert_check: {
        if ((x.a & 1U) != 0U && (x.b & 1U) == 0U) {
            return;
        }
        const auto& op = *operation_get_if<Assert>(
            &members_[member_index].operations[body_begin + at]);
        reference(op.condition, x);
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
            fail_at("dynamic packed index contains an unknown or "
                    "high-impedance value or is outside the declared range");
        }
        const auto& slot = slots_[resolved];
        if (*offset > slot.width || inst.width > slot.width - *offset) {
            fail_at("static kernel write is outside its signal");
        }
        if ((inst.flags & flag_nba) != 0U) {
            pending_slice(resolved, *offset, x, inst.width, true);
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
            pending_slice(resolved, write.write->offset, write.write->value,
                write.write->width, true);
        } else {
            write_slot_word(resolved, write.write->value, write.write->offset,
                write.write->width, member_index);
        }
        return;
    }
    case KOp::store_host:
        write_host(member_index, resolved, to_packed(x, inst.width),
            (inst.flags & flag_linear) != 0U
                ? std::optional<std::uint32_t> { inst.offset }
                : std::nullopt,
            (inst.flags & flag_blocking) != 0U,
            static_cast<SignalUpdateDomain>(inst.sub));
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
    auto& value = wide_move_scratch_;
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
                    const auto offset = selected >= part.right
                        ? static_cast<std::uint64_t>(selected - part.right)
                        : static_cast<std::uint64_t>(part.right - selected);
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
            write.instruction = at;
            push_write(write);
        } else {
            write_slot_word(target_slot, next, offset, chunk, member_index);
        }
    }
}

} // namespace fsim::runtime::simir

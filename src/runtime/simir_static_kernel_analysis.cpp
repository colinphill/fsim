// SPDX-License-Identifier: Apache-2.0
//
// KIR analyses of the engine v4 static kernel: reachability and liveness
// pruning, 'U' tracking and dead-constant elimination.
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

void Interpreter::Impl::StaticKernel::prune_operations(
    const std::uint32_t member_index, std::vector<std::uint8_t>& skip,
    std::vector<std::uint64_t>& live_at_entry) const
{
    const auto& member = members_[member_index];
    const auto& operations = member.operations;
    const auto size = static_cast<std::uint32_t>(operations.size());
    const auto entry = member.body_begin;
    const auto stop = member.body_end;
    std::vector<std::uint32_t> return_targets;
    for (const auto& operation : operations) {
        if (const auto* call = operation_get_if<Call>(&operation)) {
            return_targets.push_back(call->return_target);
        }
    }
    // Successors of each operation in the compiled view: the wait (or halt)
    // ends the activation and the next one resumes at the entry.
    const auto successors = [&](const std::uint32_t pc,
                                std::vector<std::uint32_t>& next) {
        next.clear();
        if (pc == stop) {
            if (stop < size && operation_holds<WaitSensitivity>(operations[pc])) {
                next.push_back(entry);
            }
            return;
        }
        visit_operation([&](const auto& op) {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, Jump>) {
                next.push_back(op.target);
            } else if constexpr (std::is_same_v<T, Branch>) {
                next.push_back(op.when_true);
                next.push_back(op.when_false);
            } else if constexpr (std::is_same_v<T, Call>) {
                next.push_back(op.target);
            } else if constexpr (std::is_same_v<T, Return>) {
                next.insert(next.end(), return_targets.begin(),
                    return_targets.end());
            } else if constexpr (std::is_same_v<T, Halt>
                || std::is_same_v<T, WaitSensitivity>) {
            } else {
                next.push_back(pc + 1U);
            }
        }, operations[pc]);
        std::erase_if(next, [&](const std::uint32_t target) {
            return target >= size;
        });
    };
    std::vector<std::uint8_t> reachable(size, 0U);
    std::vector<std::uint32_t> work { entry };
    std::vector<std::uint32_t> next;
    while (!work.empty()) {
        const auto pc = work.back();
        work.pop_back();
        if (pc >= size || reachable[pc] != 0U) {
            continue;
        }
        reachable[pc] = 1U;
        successors(pc, next);
        work.insert(work.end(), next.begin(), next.end());
    }
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        if (reachable[pc] == 0U && pc != stop) {
            skip[pc] = 1U;
        }
    }
    // Backward liveness over the reachable operations.
    const auto registers = member.registers.size();
    const auto words = (registers + 63U) / 64U;
    std::vector<std::uint64_t> live_in(static_cast<std::size_t>(size) * words, 0U);
    std::vector<std::vector<std::uint32_t>> uses(size);
    std::vector<std::optional<std::uint32_t>> defs(size);
    std::vector<std::vector<std::uint32_t>> succ(size);
    std::vector<RegisterId> reads;
    std::optional<RegisterId> write;
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        if (reachable[pc] == 0U) {
            continue;
        }
        successors(pc, succ[pc]);
        const auto& operation = operations[pc];
        if (const auto* call = operation_get_if<Call>(&operation)) {
            if (call->stack.capacity != 0U) {
                uses[pc].push_back(call->stack.pointer);
            }
            continue;
        }
        if (const auto* ret = operation_get_if<Return>(&operation)) {
            if (ret->stack.capacity != 0U) {
                uses[pc].push_back(ret->stack.pointer);
                for (std::uint32_t slot = 0U; slot < ret->stack.capacity; ++slot) {
                    uses[pc].push_back(ret->stack.entries + slot);
                }
            }
            continue;
        }
        operation_registers(operation, reads, write);
        uses[pc].assign(reads.begin(), reads.end());
        if (write) {
            defs[pc] = *write;
        }
    }
    std::vector<std::uint64_t> live(words);
    for (bool changed = true; changed;) {
        changed = false;
        for (std::uint32_t pc = size; pc-- > 0U;) {
            if (reachable[pc] == 0U) {
                continue;
            }
            std::ranges::fill(live, 0U);
            for (const auto successor : succ[pc]) {
                for (std::size_t word = 0U; word < words; ++word) {
                    live[word] |= live_in[successor * words + word];
                }
            }
            if (defs[pc] && *defs[pc] < registers) {
                live[*defs[pc] / 64U] &= ~(std::uint64_t { 1 } << (*defs[pc] % 64U));
            }
            for (const auto reg : uses[pc]) {
                if (reg < registers) {
                    live[reg / 64U] |= std::uint64_t { 1 } << (reg % 64U);
                }
            }
            for (std::size_t word = 0U; word < words; ++word) {
                if (live_in[pc * words + word] != live[word]) {
                    live_in[pc * words + word] = live[word];
                    changed = true;
                }
            }
        }
    }
    if (const char* debug = std::getenv("FSIM_KERNEL_PRUNE_DEBUG")) {
        const auto name = impl_.processes.program_view(member.process).name();
        if (name.find(debug) != std::string::npos) {
            std::cerr << "fsim-kernel-prune: " << name << " entry=" << entry
                      << " stop=" << stop << " targets=";
            for (const auto target : return_targets) {
                std::cerr << target << ',';
            }
            std::cerr << " live_after:";
            for (std::uint32_t pc = 0U; pc < size && pc < 24U; ++pc) {
                std::cerr << ' ' << pc << (reachable[pc] != 0U ? "" : "u") << ':';
                for (const auto successor : succ[pc]) {
                    std::cerr << (live_in[successor * words] & 1U);
                }
            }
            std::cerr << '\n';
        }
    }
    if (entry < size) {
        const auto first = static_cast<std::ptrdiff_t>(entry * words);
        live_at_entry.assign(live_in.begin() + first,
            live_in.begin() + first + static_cast<std::ptrdiff_t>(words));
    }
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        const auto* load = operation_get_if<LoadConstant>(&operations[pc]);
        if (reachable[pc] == 0U || load == nullptr
            || load->destination >= registers) {
            continue;
        }
        bool live_out = false;
        for (const auto successor : succ[pc]) {
            live_out = live_out
                || (live_in[successor * words + load->destination / 64U]
                       >> (load->destination % 64U) & 1U) != 0U;
        }
        if (!live_out) {
            skip[pc] = 1U;
        }
    }
}

void Interpreter::Impl::StaticKernel::track_unknowns(CompiledBody& body,
    const std::uint32_t member_index) const
{
    const auto& member = members_[member_index];
    const auto count = static_cast<std::uint32_t>(body.registers.size());
    const auto size = static_cast<std::uint32_t>(body.code.size());
    std::vector<std::uint8_t> tracked(count, 0U);
    std::vector<std::uint32_t> use;
    std::optional<std::uint32_t> def;
    std::vector<std::uint32_t> next;
    const auto narrow = [&](const std::uint32_t reg) {
        return reg < count && body.register_widths[reg] != 0U
            && body.register_widths[reg] <= 64U;
    };
    // CopyRegister coerces to its destination's kind: a Logic4 register
    // reads 'U' as X.
    const auto logic9_register = [&](const std::uint32_t reg) {
        return reg < member.register_kinds.size()
            && member.register_kinds[reg] == ValueKind::logic9;
    };
    const auto bitwise = [](const KInst& inst) {
        if (inst.op == KOp::unary_not) {
            return true;
        }
        if (inst.op != KOp::binary) {
            return false;
        }
        const auto operation = static_cast<BinaryOperator>(inst.sub);
        return operation == BinaryOperator::bit_and
            || operation == BinaryOperator::bit_or
            || operation == BinaryOperator::bit_xor;
    };
    // Registers that may hold 'U': exact Logic9 loads, 'U' constants,
    // reference-evaluated results, and moves or logic of tracked values.
    for (bool changed = true; changed;) {
        changed = false;
        for (std::uint32_t at = 0U; at < size; ++at) {
            const auto& inst = body.code[at];
            kinst_flow(inst, at, body, member.operations, member.body_end, use,
                def, next);
            if (!def || !narrow(*def) || tracked[*def] != 0U) {
                continue;
            }
            bool source = false;
            switch (inst.op) {
            case KOp::load_slot9:
            case KOp::load_field9:
                source = inst.sub == 0U;
                break;
            case KOp::load_host:
                source = inst.sub == 1U;
                break;
            case KOp::constant:
                source = constant_unknown(inst.offset, inst.aux) != 0U;
                break;
            case KOp::generic:
                source = true;
                break;
            case KOp::extract:
                source = inst.sub == 2U
                    || (inst.x < count && tracked[inst.x] != 0U);
                break;
            case KOp::copy:
                source = logic9_register(*def) && inst.x < count
                    && tracked[inst.x] != 0U;
                break;
            case KOp::insert:
            case KOp::concat:
                source = std::ranges::any_of(use, [&](const std::uint32_t reg) {
                    return reg < count && tracked[reg] != 0U;
                });
                break;
            case KOp::dynamic_extract:
            case KOp::dynamic_part_select:
                source = inst.sub == 2U
                    || (inst.sub == 1U && slots_[inst.x].planes == 4U
                        && inst.imm_b == 0U);
                break;
            case KOp::dynamic_insert:
            case KOp::dynamic_part_insert:
                source = (inst.x < count && tracked[inst.x] != 0U)
                    || (inst.y < count && tracked[inst.y] != 0U);
                break;
            case KOp::conditional:
                source = (inst.y < count && tracked[inst.y] != 0U)
                    || (inst.z < count && tracked[inst.z] != 0U);
                break;
            default:
                source = bitwise(inst)
                    && std::ranges::any_of(use, [&](const std::uint32_t reg) {
                           return reg < count && tracked[reg] != 0U;
                       });
                break;
            }
            if (source) {
                tracked[*def] = 1U;
                changed = true;
            }
        }
    }
    if (std::ranges::none_of(tracked, [](const auto value) { return value != 0U; })) {
        return;
    }
    body.u_mode.assign(size, 0U);
    for (std::uint32_t at = 0U; at < size; ++at) {
        const auto& inst = body.code[at];
        kinst_flow(inst, at, body, member.operations, member.body_end, use, def,
            next);
        const bool reads_tracked = std::ranges::any_of(use,
            [&](const std::uint32_t reg) { return reg < count && tracked[reg] != 0U; });
        const bool writes_tracked = def && *def < count && tracked[*def] != 0U;
        std::uint8_t mode = 0U;
        switch (inst.op) {
        case KOp::load_slot9:
        case KOp::load_host:
        case KOp::constant:
        case KOp::insert:
        case KOp::concat:
        case KOp::store_vhdl:
        case KOp::generic:
            mode = u_aware;
            break;
        case KOp::copy:
            mode = def && logic9_register(*def) ? u_aware : u_clear;
            break;
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
            mode = inst.sub == 2U
                    || (inst.sub == 1U && slots_[inst.x].planes == 4U)
                ? u_aware : static_cast<std::uint8_t>(u_check | u_clear);
            break;
        case KOp::load_field9:
            mode = u_aware;
            break;
        case KOp::dynamic_insert:
        case KOp::dynamic_part_insert:
        // A known condition selects a value with its 'U' elements; an
        // unknown one merges as Logic4 (no 'U').
        case KOp::conditional:
            mode = u_aware;
            break;
        case KOp::extract:
            mode = inst.sub == 2U || reads_tracked ? u_aware : 0U;
            break;
        // The result is the same for 'U' as for X.
        case KOp::reduce:
        case KOp::logical_not:
        case KOp::logical_binary:
        case KOp::two_state:
        case KOp::branch:
        case KOp::integer_binary:
        case KOp::integer_unary:
        case KOp::assert_check:
        case KOp::load_slot:
        case KOp::load_field:
        case KOp::nop:
        case KOp::mark:
        case KOp::mem_bind:
        case KOp::jump:
        case KOp::call:
        case KOp::ret:
            mode = u_clear;
            break;
        case KOp::binary: {
            const auto operation = static_cast<BinaryOperator>(inst.sub);
            mode = bitwise(inst) ? u_aware
                : operation == BinaryOperator::case_equal ? u_check | u_clear
                                                          : u_clear;
            break;
        }
        case KOp::unary_not:
            mode = u_aware;
            break;
        default:
            mode = u_check | u_clear;
            break;
        }
        if ((mode & u_check) != 0U && !reads_tracked) {
            mode &= static_cast<std::uint8_t>(~u_check);
        }
        if ((mode & u_clear) != 0U && !writes_tracked) {
            mode &= static_cast<std::uint8_t>(~u_clear);
        }
        body.u_mode[at] = mode;
        body.u_operand_begin.push_back(
            static_cast<std::uint32_t>(body.u_operands.size()));
        if ((mode & u_check) != 0U) {
            for (const auto reg : use) {
                if (reg < count && tracked[reg] != 0U) {
                    body.u_operands.push_back(reg);
                }
            }
        }
    }
    body.u_operand_begin.push_back(static_cast<std::uint32_t>(body.u_operands.size()));
    body.tracked = std::move(tracked);
    body.shadow_base = count;
    body.registers.resize(2U * count, Word { });
    body.register_widths.resize(2U * count, 0U);
}

void Interpreter::Impl::StaticKernel::eliminate_dead_constants(
    CompiledBody& body, const std::uint32_t member_index)
{
    // Unrepresentable constants (all-'U' variable initializers, typically)
    // are usually overwritten before any read. Backward liveness over the
    // compiled body finds them. Registers persist across activations, so the
    // exit flows to the entry of the next activation; nothing else reads a
    // member's registers.
    const auto size = static_cast<std::uint32_t>(body.code.size());
    if (std::ranges::none_of(body.code,
            [](const KInst& inst) { return inst.op == KOp::deopt; })) {
        return;
    }
    const auto registers = body.registers.size();
    const auto words = (registers + 63U) / 64U;
    std::vector<std::uint64_t> live_in((size + 1U) * words, 0U);
    const auto entry = std::min(member_index < members_.size()
            ? members_[member_index].body_begin : 0U, size);
    std::vector<std::vector<std::uint32_t>> uses(size);
    std::vector<std::optional<std::uint32_t>> defs(size);
    std::vector<std::vector<std::uint32_t>> successors(size);
    const auto& member = members_[member_index];
    for (std::uint32_t at = 0U; at < size; ++at) {
        kinst_flow(body.code[at], at, body, member.operations, member.body_end,
            uses[at], defs[at], successors[at]);
    }
    std::vector<std::uint64_t> live(words);
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t word = 0U; word < words; ++word) {
            live_in[size * words + word] = live_in[entry * words + word];
        }
        for (std::uint32_t at = size; at-- > 0U;) {
            std::ranges::fill(live, 0U);
            for (const auto successor : successors[at]) {
                for (std::size_t word = 0U; word < words; ++word) {
                    live[word] |= live_in[successor * words + word];
                }
            }
            if (defs[at] && *defs[at] < registers) {
                live[*defs[at] / 64U] &= ~(std::uint64_t { 1 } << (*defs[at] % 64U));
            }
            for (const auto reg : uses[at]) {
                if (reg < registers) {
                    live[reg / 64U] |= std::uint64_t { 1 } << (reg % 64U);
                }
            }
            for (std::size_t word = 0U; word < words; ++word) {
                if (live_in[at * words + word] != live[word]) {
                    live_in[at * words + word] = live[word];
                    changed = true;
                }
            }
        }
    }
    for (std::uint32_t at = 0U; at < size; ++at) {
        auto& inst = body.code[at];
        if (inst.op != KOp::deopt) {
            continue;
        }
        bool live_out = false;
        for (const auto successor : successors[at]) {
            live_out = live_out
                || (live_in[successor * words + inst.d / 64U]
                       >> (inst.d % 64U) & 1U) != 0U;
        }
        if (!live_out) {
            inst.op = KOp::nop;
        }
    }
}

} // namespace fsim::runtime::simir

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

bool Interpreter::Impl::StaticKernel::debug_register_defined(
    const Member& member, const RegisterId reg) const
{
    // The reference evaluator shows a local as <uninitialized> until some
    // operation has written its register. The kernel does not track writes,
    // so the answer is the may-be-written set at the member's last debug
    // point: registers written on some path from the entry to it. A member
    // with a fork also counts the writes of earlier operations (fork
    // branches run in order until they wait, and share the registers).
    // Under the debugger plan every statement is a debug point, so a write
    // whose statement's next point was never reached (an untaken branch)
    // did not run; one followed by a control transfer first still counts.
    const auto size = static_cast<std::uint32_t>(member.operations.size());
    const auto registers = member.registers.size();
    if (member.debug_pc == no_slot && debug_points_) {
        // Under the debugger plan every statement is a debug point: a member
        // that has reached none has not run, so it has written nothing.
        return false;
    }
    if (member.debug_pc == no_slot || member.debug_pc >= size
        || reg >= registers) {
        return true;
    }
    const auto words = (registers + 63U) / 64U;
    const auto executed = [&](const std::uint32_t pc) {
        if (member.debug_hit.size() != size) {
            return true;
        }
        for (auto next = pc + 1U; next < size; ++next) {
            const auto& operation = member.operations[next];
            if (const auto* point = operation_get_if<DebugPoint>(&operation)) {
                if (debug_point_executable(point->kind)) {
                    return member.debug_hit[next] != 0U;
                }
                continue;
            }
            if (operation_holds<Jump>(operation)
                || operation_holds<Branch>(operation)
                || operation_holds<Call>(operation)
                || operation_holds<Return>(operation)
                || operation_holds<Halt>(operation)
                || operation_holds<Fork>(operation)
                || operation_holds<ForkEnd>(operation)) {
                return true;
            }
        }
        return true;
    };
    if (member.debug_defined_pc != member.debug_pc) {
        std::vector<std::uint64_t> in(static_cast<std::size_t>(size) * words, 0U);
        std::vector<std::uint8_t> seen(size, 0U);
        std::vector<std::uint32_t> return_targets;
        bool has_fork = false;
        for (const auto& operation : member.operations) {
            if (const auto* call = operation_get_if<Call>(&operation)) {
                return_targets.push_back(call->return_target);
            } else if (operation_holds<Fork>(operation)) {
                has_fork = true;
            }
        }
        std::vector<RegisterId> reads;
        std::optional<RegisterId> write;
        std::vector<std::uint32_t> next;
        std::vector<std::uint32_t> work { 0U };
        seen[0] = 1U;
        std::vector<std::uint64_t> out(words);
        while (!work.empty()) {
            const auto pc = work.back();
            work.pop_back();
            std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(pc * words),
                words, out.begin());
            operation_registers(member.operations[pc], reads, write);
            if (write && *write < registers && executed(pc)) {
                out[*write / 64U] |= std::uint64_t { 1 } << (*write % 64U);
            }
            next.clear();
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
                } else if constexpr (std::is_same_v<T, Fork>) {
                    next.insert(next.end(), op.branches.begin(),
                        op.branches.end());
                    next.push_back(pc + 1U);
                } else if constexpr (std::is_same_v<T, Halt>
                    || std::is_same_v<T, ForkEnd>) {
                } else {
                    next.push_back(pc + 1U);
                }
            }, member.operations[pc]);
            for (const auto target : next) {
                if (target >= size) {
                    continue;
                }
                bool changed = seen[target] == 0U;
                seen[target] = 1U;
                auto* destination = in.data()
                    + static_cast<std::size_t>(target) * words;
                for (std::size_t word = 0U; word < words; ++word) {
                    const auto merged = destination[word] | out[word];
                    changed = changed || merged != destination[word];
                    destination[word] = merged;
                }
                if (changed) {
                    work.push_back(target);
                }
            }
        }
        member.debug_defined.assign(
            in.begin() + static_cast<std::ptrdiff_t>(member.debug_pc * words),
            in.begin() + static_cast<std::ptrdiff_t>((member.debug_pc + 1U) * words));
        if (has_fork) {
            for (std::uint32_t pc = 0U; pc < member.debug_pc; ++pc) {
                operation_registers(member.operations[pc], reads, write);
                if (write && *write < registers && executed(pc)) {
                    member.debug_defined[*write / 64U]
                        |= std::uint64_t { 1 } << (*write % 64U);
                }
            }
        }
        member.debug_defined_pc = member.debug_pc;
    }
    return (member.debug_defined[reg / 64U] >> (reg % 64U) & 1U) != 0U;
}

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
    // A VHDL first run enters at operation 0 and leaves the prologue for the
    // entry (native_prologue).
    const bool prologue = native_prologue(member);
    const auto prologue_exit = prologue && member.prologue_end != entry
        ? member.prologue_end : no_slot;
    // Successors of each operation in the compiled view: the wait (or halt)
    // ends the activation and the next one resumes at the entry.
    const auto successors = [&](const std::uint32_t pc,
                                std::vector<std::uint32_t>& next) {
        next.clear();
        if (pc == prologue_exit) {
            next.push_back(entry);
            return;
        }
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
    if (prologue) {
        work.push_back(0U);
    }
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
        if (pc == prologue_exit) {
            continue;
        }
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
    static const char* const prune_debug = std::getenv("FSIM_KERNEL_PRUNE_DEBUG");
    if (const char* debug = prune_debug) {
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
        if (prologue) {
            for (std::size_t word = 0U; word < words; ++word) {
                live_at_entry[word] |= live_in[word];
            }
        }
    }
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        const auto* load = operation_get_if<LoadConstant>(&operations[pc]);
        if (reachable[pc] == 0U || load == nullptr || pc == prologue_exit
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
                        && inst.imm_b == 0U)
                    || (inst.sub == 0U && inst.x < count && tracked[inst.x] != 0U);
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
        // A selection from a register carries its source's 'U' elements
        // (the index is an integer, never 'U').
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
            mode = inst.sub != 1U || slots_[inst.x].planes == 4U
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
        // Case equality (VHDL "=" on std_ulogic) compares the 'U' masks too;
        // the other relations give the same result for 'U' as for X.
        case KOp::binary:
            mode = bitwise(inst) ? u_aware : u_clear;
            break;
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

const char* Interpreter::Impl::StaticKernel::collapse_impurity(
    const Member& member, KOp& rejected_op) const
{
    // A collapsed member may run in a different delta, and never on the
    // intermediate input combinations the delta schedule passes through,
    // so its compiled code must compute a function of the slots it reads
    // and do nothing else: no state carried between runs (a register read
    // before this run writes it, unless no run writes it), no check or
    // assertion that could fail, no loop (the step limit), no generic
    // operation, and only deferred static-range assignments. A selection
    // whose index is a constant is checked here once.
    const auto& body = *member.compiled;
    const auto& code = body.code;
    const auto size = static_cast<std::uint32_t>(code.size());
    const auto registers = static_cast<std::uint32_t>(body.registers.size());
    if (size == 0U || body.entry >= size || !body.resume_entries.empty()
        || body.call_stack_base != 0U) {
        return "code_shape";
    }
    std::vector<std::uint8_t> reached(size, 0U);
    std::vector<std::uint32_t> definitions(registers, 0U);
    std::vector<const KInst*> definition(registers, nullptr);
    // Successors of each reached instruction: successor_begin[at] ..
    // successor_begin[at] + successor_count[at] in successors.
    std::vector<std::uint32_t> successor_begin(size, 0U);
    std::vector<std::uint8_t> successor_count(size, 0U);
    // Instructions that may end the run.
    std::vector<std::uint8_t> exits(size, 0U);
    std::vector<std::uint32_t> successors;
    std::vector<std::uint32_t> use;
    std::optional<std::uint32_t> def;
    std::vector<std::uint32_t> next;
    std::vector<std::uint32_t> work { body.entry };
    reached[body.entry] = 1U;
    while (!work.empty()) {
        const auto at = work.back();
        work.pop_back();
        const auto& inst = code[at];
        bool allowed = false;
        switch (inst.op) {
        case KOp::nop:
        case KOp::constant:
        case KOp::copy:
        case KOp::load_slot:
        case KOp::load_slot9:
        case KOp::load_field:
        case KOp::load_field9:
        // A host signal the planner found unwritten (a constant input).
        case KOp::load_host:
        case KOp::binary:
        case KOp::reduce:
        case KOp::unary_not:
        case KOp::logical_not:
        case KOp::logical_binary:
        case KOp::shift:
        case KOp::insert:
        case KOp::concat:
        case KOp::conditional:
        case KOp::two_state:
        case KOp::jump:
        case KOp::dynamic_insert:
        case KOp::dynamic_part_insert:
        case KOp::assert_check:
            allowed = true;
            break;
        // Sub 2 reads a wide register, which only generic code writes.
        case KOp::extract:
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
        case KOp::branch:
            allowed = inst.sub != 2U;
            break;
        case KOp::store_vhdl:
            allowed = inst.sub == 0U && (inst.flags & flag_blocking) == 0U;
            break;
        default:
            break;
        }
        if (!allowed) {
            rejected_op = inst.op;
            return "operation";
        }
        kinst_flow(inst, at, body, member.operations, member.body_end, use, def,
            next);
        for (const auto reg : use) {
            if (reg >= registers) {
                return "register";
            }
        }
        if (def) {
            if (*def >= registers) {
                return "register";
            }
            ++definitions[*def];
            definition[*def] = &inst;
        }
        successor_begin[at] = static_cast<std::uint32_t>(successors.size());
        exits[at] = next.empty() ? 1U : 0U;
        for (const auto target : next) {
            if (target >= size) {
                exits[at] = 1U;
                continue;
            }
            successors.push_back(target);
            ++successor_count[at];
            if (reached[target] == 0U) {
                reached[target] = 1U;
                work.push_back(target);
            }
        }
    }
    // No loop: depth-first postorder, rejecting a back edge.
    std::vector<std::uint32_t> postorder;
    {
        std::vector<std::uint8_t> color(size, 0U);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> stack {
            { body.entry, 0U } };
        color[body.entry] = 1U;
        while (!stack.empty()) {
            auto& [at, position] = stack.back();
            if (position < successor_count[at]) {
                const auto target = successors[successor_begin[at] + position++];
                if (color[target] == 1U) {
                    return "loop";
                }
                if (color[target] == 0U) {
                    color[target] = 1U;
                    stack.push_back({ target, 0U });
                }
                continue;
            }
            color[at] = 2U;
            postorder.push_back(at);
            stack.pop_back();
        }
    }
    // Upward-exposed uses of registers some run writes: backward liveness
    // in postorder (successors first), on the written registers only.
    std::vector<std::uint32_t> compact(registers, no_slot);
    std::uint32_t written = 0U;
    for (std::uint32_t reg = 0U; reg < registers; ++reg) {
        if (definitions[reg] != 0U) {
            compact[reg] = written++;
        }
    }
    const std::size_t words = (written + 63U) / 64U;
    if (words != 0U) {
        if (static_cast<std::uint64_t>(size) * words > (std::uint64_t { 1 } << 21U)) {
            return "size";
        }
        std::vector<std::uint64_t> live(static_cast<std::size_t>(size) * words, 0U);
        for (const auto at : postorder) {
            kinst_flow(code[at], at, body, member.operations, member.body_end, use,
                def, next);
            auto* in = live.data() + static_cast<std::size_t>(at) * words;
            for (std::uint32_t position = 0U; position < successor_count[at];
                 ++position) {
                const auto target = successors[successor_begin[at] + position];
                const auto* out = live.data() + static_cast<std::size_t>(target) * words;
                for (std::size_t word = 0U; word < words; ++word) {
                    in[word] |= out[word];
                }
            }
            if (def) {
                const auto bit = compact[*def];
                in[bit / 64U] &= ~(std::uint64_t { 1 } << (bit % 64U));
            }
            for (const auto reg : use) {
                if (const auto bit = compact[reg]; bit != no_slot) {
                    in[bit / 64U] |= std::uint64_t { 1 } << (bit % 64U);
                }
            }
        }
        const auto* entry = live.data() + static_cast<std::size_t>(body.entry) * words;
        for (std::size_t word = 0U; word < words; ++word) {
            if (entry[word] != 0U) {
                return "carried_state";
            }
        }
    }
    // Every run assigns every bit of what it drives: an assignment on some
    // paths only keeps the previous value on the others (a latch), which
    // depends on the inputs it saw in earlier deltas. Forward must-analysis
    // in reverse postorder over the driven bits.
    {
        std::vector<std::uint32_t> base(slots_.size(), no_slot);
        std::uint32_t bits = 0U;
        for (const auto at : postorder) {
            if (const auto& inst = code[at];
                inst.op == KOp::store_vhdl && base[inst.d] == no_slot) {
                base[inst.d] = bits;
                bits += slots_[inst.d].width;
            }
        }
        const std::size_t span = (bits + 63U) / 64U;
        if (span != 0U) {
            if (static_cast<std::uint64_t>(size) * span > (std::uint64_t { 1 } << 21U)) {
                return "size";
            }
            std::vector<std::uint64_t> must(static_cast<std::size_t>(size) * span, 0U);
            std::vector<std::uint8_t> seen(size, 0U);
            std::vector<std::uint64_t> out(span, 0U);
            for (auto position = postorder.size(); position-- > 0U;) {
                const auto at = postorder[position];
                const auto* in = must.data() + static_cast<std::size_t>(at) * span;
                std::copy(in, in + span, out.begin());
                if (const auto& inst = code[at]; inst.op == KOp::store_vhdl) {
                    for (auto bit = base[inst.d] + inst.offset;
                         bit < base[inst.d] + inst.offset + inst.width; ++bit) {
                        out[bit / 64U] |= std::uint64_t { 1 } << (bit % 64U);
                    }
                }
                if (exits[at] != 0U) {
                    for (std::uint32_t bit = 0U; bit < bits; ++bit) {
                        if (((out[bit / 64U] >> (bit % 64U)) & 1U) == 0U) {
                            return "partial_assignment";
                        }
                    }
                }
                for (std::uint32_t index = 0U; index < successor_count[at]; ++index) {
                    const auto target = successors[successor_begin[at] + index];
                    auto* target_in = must.data() + static_cast<std::size_t>(target) * span;
                    for (std::size_t word = 0U; word < span; ++word) {
                        target_in[word] = seen[target] != 0U
                            ? target_in[word] & out[word] : out[word];
                    }
                    seen[target] = 1U;
                }
            }
        }
    }
    // Every use now follows a write in the same run: a register written
    // once, by a constant, holds that constant wherever it is read.
    const auto constant = [&](const std::uint32_t reg) -> std::optional<kw::Word> {
        if (reg < registers && definitions[reg] == 1U
            && definition[reg]->op == KOp::constant) {
            return kw::Word { definition[reg]->imm_a, definition[reg]->imm_b };
        }
        return std::nullopt;
    };
    for (const auto at : postorder) {
        const auto& inst = code[at];
        switch (inst.op) {
        case KOp::dynamic_extract: {
            if ((inst.flags & flag_strict) == 0U) {
                break;
            }
            const auto index = constant(inst.y);
            const auto offset = index
                ? kw::dynamic_index(*index, body.indices[inst.aux]) : std::nullopt;
            if (!offset || *offset >= inst.width) {
                return "strict_extract";
            }
            break;
        }
        case KOp::dynamic_insert: {
            if ((inst.flags & flag_strict) == 0U) {
                break;
            }
            const auto index = constant(inst.z);
            const auto offset = index
                ? kw::dynamic_index(*index, body.indices[inst.aux]) : std::nullopt;
            if (!offset || *offset > inst.width || inst.offset > inst.width - *offset) {
                return "strict_insert";
            }
            break;
        }
        case KOp::dynamic_part_insert: {
            const auto base = constant(inst.z);
            if (!base) {
                return "dynamic_part_insert";
            }
            const auto write = kw::dynamic_part_write({ }, *base, body.parts[inst.aux]);
            if (!write.valid
                || (write.write
                    && (write.write->offset > inst.width
                        || write.write->width > inst.width - write.write->offset))) {
                return "part_insert_range";
            }
            break;
        }
        case KOp::assert_check: {
            const auto condition = constant(inst.x);
            if (!condition || (condition->a & 1U) == 0U || (condition->b & 1U) != 0U) {
                return "assertion";
            }
            break;
        }
        default:
            break;
        }
    }
    return nullptr;
}

void Interpreter::Impl::StaticKernel::plan_delta_collapse()
{
    // The planner admitted members whose readers are collapsed members or
    // run only in a time step's first round, after signals that only
    // delay-only threads write, which the kernel runs before that round.
    // That needs every delta to be the kernel's: a host process at the
    // boundary or a hosted process could run between its rounds. Members
    // whose compiled code is not pure leave the set, and so do the
    // collapsed writers of what they read (they become ordinary readers).
    const auto count = static_cast<std::uint32_t>(members_.size());
    const bool allowed = vhdl_ && !host_boundary_ && !has_hosted_;
    std::vector<std::uint8_t> keep(count, 0U);
    std::vector<std::uint32_t> dropped;
    std::uint32_t planned = 0U;
    std::map<std::string, std::size_t> impurities;
    for (std::uint32_t index = 0U; index < count; ++index) {
        const auto& member = members_[index];
        if (!member.collapse) {
            continue;
        }
        ++planned;
        const auto plain = [](const std::vector<std::uint32_t>& slots) {
            return std::ranges::find(slots, no_slot) == slots.end();
        };
        const char* shape = !allowed ? "shape:boundary"
            : !member.vhdl || !member.run_at_start
                || member.kind != StaticKernelMemberKind::combinational
            ? "shape:kind"
            : member.lazy ? "shape:lazy"
            : member.partition != no_slot ? "shape:partition"
            : !member.compiled ? "shape:compiled"
            : !plain(member.collapse_inputs) ? "shape:inputs"
            : member.collapse_outputs.empty() || !plain(member.collapse_outputs)
            ? "shape:outputs"
            : nullptr;
        KOp op { };
        const auto* impurity
            = shape != nullptr ? shape : collapse_impurity(member, op);
        if (impurity != nullptr) {
            ++impurities[std::string { impurity }
                + (std::string_view { impurity } == "operation"
                        ? ":" + std::to_string(static_cast<int>(op)) : "")];
            dropped.push_back(index);
            continue;
        }
        keep[index] = 1U;
    }
    std::vector<std::uint32_t> writer(slots_.size(), no_slot);
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (keep[index] != 0U) {
            for (const auto slot : members_[index].collapse_outputs) {
                writer[slot] = index;
            }
        }
    }
    while (!dropped.empty()) {
        const auto index = dropped.back();
        dropped.pop_back();
        for (const auto slot : members_[index].collapse_inputs) {
            if (slot == no_slot) {
                continue;
            }
            if (const auto source = writer[slot];
                source != no_slot && keep[source] != 0U) {
                keep[source] = 0U;
                dropped.push_back(source);
            }
        }
    }
    // Levels: a member after the collapsed writers of its inputs (the
    // planner left no cycle).
    std::vector<std::vector<std::uint32_t>> successors(count);
    std::vector<std::uint32_t> indegree(count, 0U);
    std::vector<std::uint32_t> ready;
    std::uint32_t kept = 0U;
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (keep[index] == 0U) {
            continue;
        }
        ++kept;
        auto inputs = members_[index].collapse_inputs;
        std::ranges::sort(inputs);
        inputs.erase(std::ranges::unique(inputs).begin(), inputs.end());
        for (const auto slot : inputs) {
            if (const auto source = writer[slot];
                source != no_slot && keep[source] != 0U) {
                successors[source].push_back(index);
                ++indegree[index];
            }
        }
        if (indegree[index] == 0U) {
            ready.push_back(index);
        }
    }
    std::vector<std::uint32_t> level(count, 0U);
    std::uint32_t levels = 0U;
    std::uint32_t ordered = 0U;
    while (!ready.empty()) {
        const auto index = ready.back();
        ready.pop_back();
        ++ordered;
        levels = std::max(levels, level[index] + 1U);
        for (const auto next : successors[index]) {
            level[next] = std::max(level[next], level[index] + 1U);
            if (--indegree[next] == 0U) {
                ready.push_back(next);
            }
        }
    }
    collapse_enabled_ = kept != 0U && ordered == kept;
    for (std::uint32_t index = 0U; index < count; ++index) {
        auto& state = member_schedule_[index];
        state.collapse = collapse_enabled_ && keep[index] != 0U;
        state.collapse_level = state.collapse ? level[index] : 0U;
    }
    collapse_buckets_.assign(collapse_enabled_ ? levels : 0U, { });
    collapse_low_ = static_cast<std::uint32_t>(collapse_buckets_.size());
    if (std::getenv("FSIM_PROFILE_PHASES") != nullptr && planned != 0U) {
        std::cerr << "fsim-profile: static-kernel-collapse planned=" << planned
                  << " kept=" << (collapse_enabled_ ? kept : 0U)
                  << " levels=" << (collapse_enabled_ ? levels : 0U)
                  << (allowed ? "" : " host-boundary");
        for (const auto& [reason, members] : impurities) {
            std::cerr << ' ' << reason << '=' << members;
        }
        std::cerr << '\n';
    }
}

} // namespace fsim::runtime::simir

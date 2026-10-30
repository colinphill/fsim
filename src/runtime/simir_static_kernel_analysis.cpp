// SPDX-License-Identifier: Apache-2.0
//
// KIR analyses of the engine v4 static kernel: reachability and liveness
// pruning, 'U' tracking and dead-constant elimination.
#include "simir_static_kernel_compiled_internal.hpp"

#include <array>
#include <cxxabi.h>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <string>
#include <tuple>
#include <typeinfo>
#include <unordered_map>

namespace fsim::runtime::simir {

std::optional<StaticKernelInlinedProgram> static_kernel_inline_calls(
    const std::span<const Operation> operations, const std::uint32_t wait,
    const std::uint32_t jump)
{
    const auto size = static_cast<std::uint32_t>(operations.size());
    if (wait >= size || jump >= size || (jump != wait + 1U && wait != 0U)) {
        return std::nullopt;
    }
    // Code positions are labels (context, operation): context 0 is the
    // program from operation 0 to the loop jump, each expanded call a new
    // context. Targets hold label numbers until every position is known.
    constexpr std::size_t size_limit = std::size_t { 1 } << 18U;
    struct Context {
        std::uint32_t return_label { std::numeric_limits<std::uint32_t>::max() };
    };
    std::vector<Context> contexts(1U);
    std::unordered_map<std::uint64_t, std::uint32_t> label_of;
    std::vector<std::uint64_t> labels;
    std::unordered_map<std::uint64_t, std::uint32_t> position;
    const auto key = [](const std::uint32_t context, const std::uint32_t pc) {
        return (std::uint64_t { context } << 32U) | pc;
    };
    const auto label = [&](const std::uint32_t context, const std::uint32_t pc) {
        const auto [found, inserted] = label_of.try_emplace(key(context, pc),
            static_cast<std::uint32_t>(labels.size()));
        if (inserted) {
            labels.push_back(key(context, pc));
        }
        return found->second;
    };
    StaticKernelInlinedProgram result;
    auto& out = result.operations;
    std::vector<std::uint32_t> active;
    bool called = false;
    // A callee's code: what its entry reaches without entering further
    // calls, up to its returns.
    const auto callee_region = [&](const std::uint32_t entry)
        -> std::optional<std::vector<std::uint32_t>> {
        std::vector<std::uint8_t> seen(size, 0U);
        std::vector<std::uint32_t> work { entry };
        std::vector<std::uint32_t> region;
        while (!work.empty()) {
            const auto pc = work.back();
            work.pop_back();
            if (pc >= size) {
                return std::nullopt;
            }
            if (seen[pc] != 0U) {
                continue;
            }
            seen[pc] = 1U;
            region.push_back(pc);
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    work.push_back(op.target);
                } else if constexpr (std::is_same_v<T, Branch>) {
                    work.push_back(op.when_true);
                    work.push_back(op.when_false);
                } else if constexpr (std::is_same_v<T, Call>) {
                    work.push_back(op.return_target);
                } else if constexpr (std::is_same_v<T, Return>
                    || std::is_same_v<T, Halt>) {
                } else {
                    work.push_back(pc + 1U);
                }
            }, operations[pc]);
        }
        std::ranges::sort(region);
        return region;
    };
    const auto emit = [&](const auto& self, const std::uint32_t context,
                          const std::vector<std::uint32_t>& region) -> bool {
        for (std::size_t at = 0U; at < region.size(); ++at) {
            const auto pc = region[at];
            position.emplace(key(context, pc), static_cast<std::uint32_t>(out.size()));
            if (out.size() > size_limit) {
                return false;
            }
            bool falls_through = true;
            bool ok = true;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, CallableFramePush>) {
                    ok = op.native_isolated;
                } else if constexpr (std::is_same_v<T, CallableFramePop>) {
                } else if constexpr (std::is_same_v<T, Call>) {
                    called = true;
                    falls_through = false;
                    if (std::ranges::find(active, op.target) != active.end()) {
                        ok = false;
                        return;
                    }
                    const auto region = callee_region(op.target);
                    if (!region) {
                        ok = false;
                        return;
                    }
                    const auto callee = static_cast<std::uint32_t>(contexts.size());
                    contexts.push_back({ label(context, op.return_target) });
                    active.push_back(op.target);
                    ok = self(self, callee, *region);
                    active.pop_back();
                } else if constexpr (std::is_same_v<T, Return>) {
                    falls_through = false;
                    ok = contexts[context].return_label
                        != std::numeric_limits<std::uint32_t>::max();
                    out.emplace_back(Jump { contexts[context].return_label });
                } else if constexpr (std::is_same_v<T, Jump>) {
                    falls_through = false;
                    out.emplace_back(Jump { label(context, op.target) });
                } else if constexpr (std::is_same_v<T, Branch>) {
                    falls_through = false;
                    auto branch = op;
                    branch.when_true = label(context, op.when_true);
                    branch.when_false = label(context, op.when_false);
                    out.emplace_back(branch);
                } else if constexpr (std::is_same_v<T, Halt>) {
                    falls_through = false;
                    out.emplace_back(op);
                } else {
                    out.emplace_back(op);
                }
            }, operations[pc]);
            if (!ok) {
                return false;
            }
            if (falls_through
                && (at + 1U == region.size() || region[at + 1U] != pc + 1U)) {
                out.emplace_back(Jump { label(context, pc + 1U) });
            }
        }
        return true;
    };
    std::vector<std::uint32_t> program(jump + 1U);
    for (std::uint32_t pc = 0U; pc <= jump; ++pc) {
        program[pc] = pc;
    }
    if (!emit(emit, 0U, program) || !called) {
        return std::nullopt;
    }
    // The loop jump ends the program.
    if (out.empty() || !operation_holds<Jump>(out.back())) {
        return std::nullopt;
    }
    for (auto& operation : out) {
        const auto resolve = [&](InstructionIndex& target) {
            if (target >= labels.size()) {
                return false;
            }
            const auto found = position.find(labels[target]);
            if (found == position.end()) {
                return false;
            }
            target = found->second;
            return true;
        };
        bool ok = true;
        if (auto* value = operation_get_if<Jump>(&operation)) {
            ok = resolve(value->target);
        } else if (auto* branch = operation_get_if<Branch>(&operation)) {
            ok = resolve(branch->when_true) && resolve(branch->when_false);
        }
        if (!ok) {
            return std::nullopt;
        }
    }
    const auto new_wait = position.at(key(0U, wait));
    const auto new_jump = position.at(key(0U, jump));
    if (new_jump + 1U != out.size()) {
        return std::nullopt;
    }
    if (wait == 0U) {
        result.body_begin = 1U;
        result.body_end = new_jump;
        result.exit_alt = 0U;
    } else {
        result.body_begin = 0U;
        result.body_end = new_wait;
        result.exit_alt = new_wait + 1U;
        if (new_jump != new_wait + 1U) {
            return std::nullopt;
        }
    }
    return result;
}

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
    const auto size = static_cast<std::uint32_t>(member.operations().size());
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
            const auto& operation = member.operations()[next];
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
                || operation_holds<ForkEnd>(operation)
                || operation_holds<DisableBlock>(operation)) {
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
        for (const auto& operation : member.operations()) {
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
            operation_registers(member.operations()[pc], reads, write);
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
                } else if constexpr (std::is_same_v<T, DisableBlock>) {
                    // The thread continues after the disable or at the end
                    // of its own block.
                    next.push_back(pc + 1U);
                    next.push_back(op.end);
                } else if constexpr (std::is_same_v<T, Halt>
                    || std::is_same_v<T, ForkEnd>) {
                } else {
                    next.push_back(pc + 1U);
                }
            }, member.operations()[pc]);
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
                operation_registers(member.operations()[pc], reads, write);
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
    const auto& operations = member.operations();
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
            kinst_flow(inst, at, body, member.operations(), member.body_end, use,
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
                source = inst.sub >= 2U
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
                source = inst.sub >= 2U
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
        kinst_flow(inst, at, body, member.operations(), member.body_end, use, def,
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
        // Wide registers hold their own 'U' masks.
        case KOp::wide_load:
        case KOp::wide_copy:
        case KOp::wide_const:
        case KOp::wide_concat:
        case KOp::wide_store:
        case KOp::wide_part_insert:
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
            mode = inst.sub >= 2U || reads_tracked ? u_aware : 0U;
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
        kinst_flow(body.code[at], at, body, member.operations(), member.body_end,
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
    // before this run writes it, unless no run writes it), no generic
    // operation, only deferred static-range assignments, and no check,
    // assertion or loop whose outcome could differ between runs. Collapsing
    // is off at time zero, where every member runs once (run_at_start), so
    // a check that is static (it reads no value derived from a slot and is
    // not control dependent on a branch that does) passes in every run as
    // it did then, and a loop whose exits are static ends as it did then.
    // Range checks fail fatally; an assertion may only report, so it needs
    // a condition that is a constant true.
    const auto& body = *member.compiled;
    const auto& code = body.code;
    const auto size = static_cast<std::uint32_t>(code.size());
    const auto registers = static_cast<std::uint32_t>(body.registers.size());
    // A VHDL signal assignment of a narrow or wide register, deferred to
    // the update phase.
    const auto deferred_store = [](const KInst& inst) {
        return ((inst.op == KOp::store_vhdl && inst.sub == 0U)
                   || (inst.op == KOp::wide_store && inst.sub == 2U))
            && (inst.flags & flag_blocking) == 0U;
    };
    if (size == 0U || body.entry >= size || !body.resume_entries.empty()) {
        return "code_shape";
    }
    // Each reached instruction once: allowed, and its register uses and
    // definition (call and return touch only the synthetic call stack).
    std::vector<std::uint8_t> checked(size, 0U);
    std::vector<std::uint32_t> use_begin(size + 1U, 0U);
    std::vector<std::uint32_t> uses;
    std::vector<std::uint32_t> defined(size, no_slot);
    std::vector<std::uint32_t> definitions(registers, 0U);
    std::vector<const KInst*> definition(registers, nullptr);
    std::vector<std::uint32_t> use;
    std::optional<std::uint32_t> def;
    std::vector<std::uint32_t> next;
    std::vector<std::uint32_t> first_use(size, no_slot);
    std::vector<std::uint32_t> use_count(size, 0U);
    const auto check_instruction = [&](const std::uint32_t at) -> const char* {
        if (checked[at] != 0U) {
            return nullptr;
        }
        checked[at] = 1U;
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
        case KOp::integer_binary:
        case KOp::integer_unary:
        case KOp::integer_check:
        // Wide registers (sub 2 and 3 of the selections read one), which
        // only these and generic instructions write.
        case KOp::extract:
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
        case KOp::branch:
        case KOp::wide_load:
        case KOp::wide_copy:
        case KOp::wide_const:
        case KOp::wide_concat:
            allowed = true;
            break;
        case KOp::store_vhdl:
        case KOp::wide_store:
            allowed = deferred_store(inst);
            break;
        // Fixed-register calls; the dynamic call stack (z 0) is the host's.
        case KOp::call:
        case KOp::ret:
            allowed = inst.z != 0U;
            break;
        default:
            break;
        }
        if (!allowed) {
            rejected_op = inst.op;
            return "operation";
        }
        if (inst.op == KOp::call || inst.op == KOp::ret) {
            return nullptr;
        }
        kinst_flow(inst, at, body, member.operations(), member.body_end, use, def,
            next);
        first_use[at] = static_cast<std::uint32_t>(uses.size());
        for (const auto reg : use) {
            if (reg >= registers) {
                return "register";
            }
            uses.push_back(reg);
        }
        use_count[at] = static_cast<std::uint32_t>(use.size());
        if (def) {
            if (*def >= registers) {
                return "register";
            }
            defined[at] = *def;
            ++definitions[*def];
            definition[*def] = &inst;
        }
        return nullptr;
    };
    // The flow graph with calls expanded: a node is (context, instruction),
    // context 0 the body and each call a new context that returns to its
    // caller's return target. A node's successors are nodes or `leave`
    // (the exit).
    constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
    constexpr std::uint32_t leave = none - 1U;
    constexpr std::size_t node_limit = std::size_t { 1 } << 16U;
    struct Context {
        std::uint32_t caller { };
        std::uint32_t return_at { };
        std::uint32_t callee { };
    };
    std::vector<Context> contexts { { none, none, none } };
    // Context 0's nodes by instruction; the expanded calls' in a map.
    std::vector<std::uint32_t> body_node(size, none);
    std::unordered_map<std::uint64_t, std::uint32_t> node_of;
    std::vector<std::uint32_t> node_context;
    std::vector<std::uint32_t> node_at;
    std::vector<std::array<std::uint32_t, 2>> successor;
    node_context.reserve(size);
    node_at.reserve(size);
    successor.reserve(size);
    const auto node = [&](const std::uint32_t context, const std::uint32_t at) {
        const auto created = static_cast<std::uint32_t>(node_at.size());
        auto& slot = context == 0U ? body_node[at]
                                   : node_of.try_emplace(
                                         (std::uint64_t { context } << 32U) | at, none)
                                         .first->second;
        if (slot == none) {
            slot = created;
            node_context.push_back(context);
            node_at.push_back(at);
        }
        return slot;
    };
    node(0U, body.entry);
    for (std::uint32_t current = 0U; current < node_at.size(); ++current) {
        if (node_at.size() > node_limit) {
            return "size";
        }
        const auto at = node_at[current];
        const auto context = node_context[current];
        if (const auto* reason = check_instruction(at)) {
            return reason;
        }
        const auto& inst = code[at];
        std::array<std::uint32_t, 2> out { none, none };
        if (inst.op == KOp::call) {
            if (inst.d >= size) {
                return "code_shape";
            }
            for (auto caller = context; caller != 0U; caller = contexts[caller].caller) {
                if (contexts[caller].callee == inst.d) {
                    return "recursion";
                }
            }
            contexts.push_back({ context, static_cast<std::uint32_t>(inst.imm_a), inst.d });
            out[0] = node(static_cast<std::uint32_t>(contexts.size() - 1U), inst.d);
        } else if (inst.op == KOp::ret) {
            if (context == 0U) {
                return "code_shape";
            }
            // VHDL bodies map SimIR operation n to instruction n; the wait
            // is the exit.
            const auto target = contexts[context].return_at;
            out[0] = target == member.body_end || target >= size
                ? leave : node(contexts[context].caller, target);
        } else {
            kinst_flow(inst, at, body, member.operations(), member.body_end, use,
                def, next);
            if (next.empty()) {
                out[0] = leave;
            }
            for (std::size_t index = 0U; index < next.size() && index < 2U; ++index) {
                out[index] = next[index] >= size ? leave : node(context, next[index]);
            }
            if (next.size() > 2U) {
                return "code_shape";
            }
            if (out[1] == out[0]) {
                out[1] = none;
            }
        }
        successor.push_back(out);
    }
    const auto nodes = static_cast<std::uint32_t>(node_at.size());
    const auto each_successor = [&](const std::uint32_t at, const auto& visit) {
        for (const auto target : successor[at]) {
            if (target != none) {
                visit(target);
            }
        }
    };
    // Depth-first postorder from the entry.
    std::vector<std::uint32_t> postorder;
    postorder.reserve(nodes);
    {
        std::vector<std::uint8_t> seen(nodes, 0U);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> stack { { 0U, 0U } };
        seen[0] = 1U;
        while (!stack.empty()) {
            auto& [at, position] = stack.back();
            if (position < 2U) {
                const auto target = successor[at][position++];
                if (target < nodes && seen[target] == 0U) {
                    seen[target] = 1U;
                    stack.push_back({ target, 0U });
                }
                continue;
            }
            postorder.push_back(at);
            stack.pop_back();
        }
    }
    // Upward-exposed uses of registers some run writes: backward liveness
    // to a fixed point, on the written registers only.
    std::vector<std::uint32_t> compact(registers, no_slot);
    std::uint32_t written = 0U;
    for (std::uint32_t reg = 0U; reg < registers; ++reg) {
        if (definitions[reg] != 0U) {
            compact[reg] = written++;
        }
    }
    const std::size_t words = (written + 63U) / 64U;
    if (words != 0U) {
        if (static_cast<std::uint64_t>(nodes) * words > (std::uint64_t { 1 } << 21U)) {
            return "size";
        }
        std::vector<std::uint64_t> live(static_cast<std::size_t>(nodes) * words, 0U);
        std::vector<std::uint64_t> in(words, 0U);
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto at : postorder) {
                std::ranges::fill(in, 0U);
                each_successor(at, [&](const std::uint32_t target) {
                    if (target < nodes) {
                        const auto* out = live.data() + static_cast<std::size_t>(target) * words;
                        for (std::size_t word = 0U; word < words; ++word) {
                            in[word] |= out[word];
                        }
                    }
                });
                const auto instruction = node_at[at];
                if (defined[instruction] != no_slot) {
                    const auto bit = compact[defined[instruction]];
                    in[bit / 64U] &= ~(std::uint64_t { 1 } << (bit % 64U));
                }
                for (std::uint32_t index = 0U; index < use_count[instruction]; ++index) {
                    if (const auto bit = compact[uses[first_use[instruction] + index]];
                        bit != no_slot) {
                        in[bit / 64U] |= std::uint64_t { 1 } << (bit % 64U);
                    }
                }
                auto* stored = live.data() + static_cast<std::size_t>(at) * words;
                if (!std::equal(in.begin(), in.end(), stored)) {
                    std::ranges::copy(in, stored);
                    changed = true;
                }
            }
        }
        for (std::size_t word = 0U; word < words; ++word) {
            if (live[word] != 0U) {
                return "carried_state";
            }
        }
    }
    // Every run assigns every bit it assigns on any path: an assignment on
    // some paths only keeps the previous value on the others (a latch),
    // which depends on the inputs it saw in earlier deltas. Bits no path
    // assigns (another member's slice of the signal) are not its own.
    // Forward must-analysis to a fixed point over the assigned bits.
    {
        std::vector<std::uint32_t> base(slots_.size(), no_slot);
        std::uint32_t bits = 0U;
        for (std::uint32_t at = 0U; at < size; ++at) {
            if (const auto& inst = code[at];
                checked[at] != 0U && deferred_store(inst) && base[inst.d] == no_slot) {
                base[inst.d] = bits;
                bits += slots_[inst.d].width;
            }
        }
        const std::size_t span = (bits + 63U) / 64U;
        if (span != 0U) {
            if (static_cast<std::uint64_t>(nodes) * span > (std::uint64_t { 1 } << 21U)) {
                return "size";
            }
            const auto add_stored = [&](const KInst& inst, std::vector<std::uint64_t>& set) {
                for (auto bit = base[inst.d] + inst.offset;
                     bit < base[inst.d] + inst.offset + inst.width; ++bit) {
                    set[bit / 64U] |= std::uint64_t { 1 } << (bit % 64U);
                }
            };
            std::vector<std::uint64_t> assigned(span, 0U);
            for (std::uint32_t at = 0U; at < size; ++at) {
                if (checked[at] != 0U && deferred_store(code[at])) {
                    add_stored(code[at], assigned);
                }
            }
            // Must-assigned bits on entry to each node, from all-assigned
            // down (the entry starts with none).
            std::vector<std::uint64_t> must(static_cast<std::size_t>(nodes) * span,
                ~std::uint64_t { 0 });
            std::fill_n(must.begin(), span, 0U);
            std::vector<std::uint64_t> out(span, 0U);
            for (bool changed = true; changed;) {
                changed = false;
                for (auto position = postorder.size(); position-- > 0U;) {
                    const auto at = postorder[position];
                    const auto* in = must.data() + static_cast<std::size_t>(at) * span;
                    std::copy(in, in + span, out.begin());
                    if (const auto& inst = code[node_at[at]]; deferred_store(inst)) {
                        add_stored(inst, out);
                    }
                    each_successor(at, [&](const std::uint32_t target) {
                        if (target >= nodes) {
                            return;
                        }
                        auto* target_in = must.data() + static_cast<std::size_t>(target) * span;
                        for (std::size_t word = 0U; word < span; ++word) {
                            if ((target_in[word] & out[word]) != target_in[word]) {
                                target_in[word] &= out[word];
                                changed = true;
                            }
                        }
                    });
                }
            }
            for (const auto at : postorder) {
                if (successor[at][0] != leave && successor[at][1] != leave) {
                    continue;
                }
                std::copy_n(must.begin() + static_cast<std::ptrdiff_t>(at * span), span,
                    out.begin());
                if (const auto& inst = code[node_at[at]]; deferred_store(inst)) {
                    add_stored(inst, out);
                }
                for (std::size_t word = 0U; word < span; ++word) {
                    if ((assigned[word] & ~out[word]) != 0U) {
                        return "partial_assignment";
                    }
                }
            }
        }
    }
    // Immediate postdominators (Cooper, Harvey and Kennedy) on the reverse
    // graph, `nodes` standing for the exit; a node that cannot reach the
    // exit never ends.
    std::vector<std::uint32_t> predecessor_begin(nodes + 2U, 0U);
    std::vector<std::uint32_t> predecessors;
    {
        const auto target_of = [&](const std::uint32_t target) {
            return target == leave ? nodes : target;
        };
        for (std::uint32_t at = 0U; at < nodes; ++at) {
            each_successor(at, [&](const std::uint32_t target) {
                ++predecessor_begin[target_of(target) + 1U];
            });
        }
        for (std::uint32_t at = 0U; at <= nodes; ++at) {
            predecessor_begin[at + 1U] += predecessor_begin[at];
        }
        predecessors.resize(predecessor_begin[nodes + 1U]);
        auto fill = predecessor_begin;
        for (std::uint32_t at = 0U; at < nodes; ++at) {
            each_successor(at, [&](const std::uint32_t target) {
                predecessors[fill[target_of(target)]++] = at;
            });
        }
    }
    std::vector<std::uint32_t> reverse_post(nodes + 1U, none);
    std::vector<std::uint32_t> reverse_order;
    {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> stack {
            { nodes, predecessor_begin[nodes] } };
        std::vector<std::uint8_t> seen(nodes + 1U, 0U);
        seen[nodes] = 1U;
        while (!stack.empty()) {
            auto& [at, position] = stack.back();
            if (position < predecessor_begin[at + 1U]) {
                const auto source = predecessors[position++];
                if (seen[source] == 0U) {
                    seen[source] = 1U;
                    stack.push_back({ source, predecessor_begin[source] });
                }
                continue;
            }
            reverse_post[at] = static_cast<std::uint32_t>(reverse_order.size());
            reverse_order.push_back(at);
            stack.pop_back();
        }
        if (reverse_order.size() != nodes + 1U) {
            return "no_exit";
        }
    }
    std::vector<std::uint32_t> ipdom(nodes + 1U, none);
    ipdom[nodes] = nodes;
    {
        const auto intersect = [&](std::uint32_t left, std::uint32_t right) {
            while (left != right) {
                while (reverse_post[left] < reverse_post[right]) {
                    left = ipdom[left];
                }
                while (reverse_post[right] < reverse_post[left]) {
                    right = ipdom[right];
                }
            }
            return left;
        };
        for (bool changed = true; changed;) {
            changed = false;
            for (auto position = reverse_order.size() - 1U; position-- > 0U;) {
                const auto at = reverse_order[position];
                std::uint32_t chosen = none;
                each_successor(at, [&](const std::uint32_t target) {
                    const auto other = target == leave ? nodes : target;
                    if (ipdom[other] == none) {
                        return;
                    }
                    chosen = chosen == none ? other : intersect(other, chosen);
                });
                if (chosen != ipdom[at]) {
                    ipdom[at] = chosen;
                    changed = true;
                }
            }
        }
    }
    // Control dependence (Ferrante, Ottenstein and Warren): each node on
    // the postdominator path from a branch's successor up to the branch's
    // immediate postdominator depends on that branch.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> dependence;
    for (std::uint32_t at = 0U; at < nodes; ++at) {
        if (successor[at][1] == none) {
            continue;
        }
        for (const auto target : successor[at]) {
            for (auto runner = target == leave ? nodes : target;
                 runner != ipdom[at] && runner != nodes; runner = ipdom[runner]) {
                dependence.emplace_back(runner, at);
            }
        }
    }
    std::ranges::sort(dependence);
    // Values derived from slots (data), and nodes that run or not by them
    // (controlled), to a fixed point. Registers are taken whole: one data
    // definition makes every use data.
    std::vector<std::uint8_t> data(registers, 0U);
    std::vector<std::uint8_t> controlled(nodes, 0U);
    // Only instructions that read nothing but registers (and constant
    // inputs) can define a register that is not data; every other one,
    // including selections that read a slot in place (sub 1), is a source.
    const auto data_source = [](const KInst& inst) {
        switch (inst.op) {
        case KOp::constant:
        case KOp::load_host:
        case KOp::copy:
        case KOp::binary:
        case KOp::reduce:
        case KOp::unary_not:
        case KOp::logical_not:
        case KOp::logical_binary:
        case KOp::shift:
        case KOp::extract:
        case KOp::insert:
        case KOp::concat:
        case KOp::conditional:
        case KOp::two_state:
        case KOp::dynamic_insert:
        case KOp::dynamic_part_insert:
        case KOp::integer_binary:
        case KOp::integer_unary:
        case KOp::wide_copy:
        case KOp::wide_const:
        case KOp::wide_concat:
            return false;
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
            return inst.sub == 1U;
        default:
            return true;
        }
    };
    const auto data_use = [&](const std::uint32_t instruction) {
        for (std::uint32_t index = 0U; index < use_count[instruction]; ++index) {
            if (data[uses[first_use[instruction] + index]] != 0U) {
                return true;
            }
        }
        return false;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (std::uint32_t at = 0U; at < nodes; ++at) {
            const auto instruction = node_at[at];
            if (controlled[at] == 0U) {
                for (const auto& [dependent, branch] : std::ranges::equal_range(
                         dependence, at, {}, &std::pair<std::uint32_t, std::uint32_t>::first)) {
                    if (controlled[branch] != 0U || data_use(node_at[branch])) {
                        controlled[at] = 1U;
                        changed = true;
                        break;
                    }
                }
            }
            if (const auto reg = defined[instruction]; reg != no_slot && data[reg] == 0U
                && (controlled[at] != 0U || data_source(code[instruction])
                    || data_use(instruction))) {
                data[reg] = 1U;
                changed = true;
            }
        }
    }
    const auto is_static = [&](const std::uint32_t at,
                               std::initializer_list<std::uint32_t> operands) {
        return controlled[at] == 0U
            && std::ranges::none_of(operands, [&](const std::uint32_t reg) {
                   return reg < registers && data[reg] != 0U;
               });
    };
    // Loops end as they did at time zero: every branch that leaves a loop,
    // at every nesting depth, is static. Loops are strongly connected
    // components (Tarjan); inside one, the edges into its headers (the
    // nodes entered from outside it) are removed and the rest is searched
    // again for inner loops.
    {
        std::vector<std::uint32_t> in_set(nodes, none);
        std::vector<std::uint8_t> header(nodes, 0U);
        std::vector<std::uint32_t> component(nodes, none);
        std::vector<std::uint32_t> order(nodes, none);
        std::vector<std::uint32_t> low(nodes, 0U);
        std::vector<std::uint8_t> on_stack(nodes, 0U);
        std::vector<std::uint32_t> stack;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> calls;
        std::vector<std::vector<std::uint32_t>> found;
        std::vector<std::vector<std::uint32_t>> pending(1U);
        pending.front().resize(nodes);
        std::iota(pending.front().begin(), pending.front().end(), 0U);
        std::uint32_t set = 0U;
        std::uint32_t next_component = 0U;
        // The successor of `at` at `position` inside the searched set, or
        // none.
        const auto edge = [&](const std::uint32_t at, const std::uint32_t position) {
            const auto target = successor[at][position];
            return target < nodes && in_set[target] == in_set[at] && header[target] == 0U
                ? target : none;
        };
        while (!pending.empty()) {
            const auto subset = std::move(pending.back());
            pending.pop_back();
            ++set;
            for (const auto at : subset) {
                in_set[at] = set;
                order[at] = none;
            }
            found.clear();
            std::uint32_t counter = 0U;
            for (const auto root : subset) {
                if (order[root] != none) {
                    continue;
                }
                order[root] = low[root] = counter++;
                stack.push_back(root);
                on_stack[root] = 1U;
                calls.assign(1U, { root, 0U });
                while (!calls.empty()) {
                    auto& [at, position] = calls.back();
                    if (position < 2U) {
                        const auto target = edge(at, position++);
                        if (target == none) {
                            continue;
                        }
                        if (order[target] == none) {
                            order[target] = low[target] = counter++;
                            stack.push_back(target);
                            on_stack[target] = 1U;
                            calls.push_back({ target, 0U });
                        } else if (on_stack[target] != 0U) {
                            low[at] = std::min(low[at], order[target]);
                        }
                        continue;
                    }
                    const auto finished = at;
                    calls.pop_back();
                    if (!calls.empty()) {
                        low[calls.back().first]
                            = std::min(low[calls.back().first], low[finished]);
                    }
                    if (low[finished] != order[finished]) {
                        continue;
                    }
                    const auto id = next_component++;
                    auto& members = found.emplace_back();
                    for (std::uint32_t popped = none; popped != finished;) {
                        popped = stack.back();
                        stack.pop_back();
                        on_stack[popped] = 0U;
                        component[popped] = id;
                        members.push_back(popped);
                    }
                    if (members.size() == 1U && edge(finished, 0U) != finished
                        && edge(finished, 1U) != finished) {
                        found.pop_back();
                    }
                }
            }
            for (auto& members : found) {
                const auto id = component[members.front()];
                for (const auto at : members) {
                    if (successor[at][1] == none) {
                        continue;
                    }
                    const bool leaves = std::ranges::any_of(successor[at],
                        [&](const std::uint32_t target) {
                            return target == leave
                                || (target < nodes && component[target] != id);
                        });
                    if (leaves && !is_static(at, { code[node_at[at]].x })) {
                        return "loop";
                    }
                }
                bool entered = false;
                for (const auto at : members) {
                    for (auto position = predecessor_begin[at];
                         position < predecessor_begin[at + 1U]; ++position) {
                        if (component[predecessors[position]] != id) {
                            header[at] = 1U;
                            entered = true;
                        }
                    }
                    if (at == 0U) {
                        header[at] = 1U;
                        entered = true;
                    }
                }
                if (!entered) {
                    return "loop";
                }
                pending.push_back(std::move(members));
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
    for (std::uint32_t at = 0U; at < nodes; ++at) {
        const auto& inst = code[node_at[at]];
        switch (inst.op) {
        case KOp::dynamic_extract: {
            if ((inst.flags & flag_strict) == 0U || is_static(at, { inst.y })) {
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
            if ((inst.flags & flag_strict) == 0U || is_static(at, { inst.z })) {
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
            if (is_static(at, { inst.z })) {
                break;
            }
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
        case KOp::integer_check:
        case KOp::integer_unary:
            if (!is_static(at, { inst.x })) {
                return "integer_data";
            }
            break;
        case KOp::integer_binary:
            if (!is_static(at, { inst.x, inst.y })) {
                return "integer_data";
            }
            break;
        default:
            break;
        }
    }
    return nullptr;
}

void Interpreter::Impl::StaticKernel::plan_delta_collapse()
{
    // A restored kernel image holds the plan its recording made.
    auto plan = restored_collapse_ ? std::move(*restored_collapse_)
                                   : decide_delta_collapse();
    restored_collapse_.reset();
    if (recording_image_) {
        record_collapse_plan(plan);
        recording_image_.reset();
    }
    apply_delta_collapse(plan);
}

Interpreter::Impl::StaticKernel::CollapsePlan
Interpreter::Impl::StaticKernel::decide_delta_collapse()
{
    // The planner admitted members whose readers are collapsed members or
    // run only in a time step's first round, after signals that only
    // delay-only threads write, which the kernel runs before that round.
    // That needs every delta to be the kernel's: a host process at the
    // boundary or a hosted process could run between its rounds, and a
    // host signal's attributes count the host's deltas (vhdl_host_cycles_).
    // Members whose compiled code is not pure leave the set, and so do the
    // collapsed writers of what they read (they become ordinary readers).
    const auto count = static_cast<std::uint32_t>(members_.size());
    CollapsePlan plan;
    plan.allowed = vhdl_ && !host_boundary_ && !has_hosted_ && !vhdl_host_cycles_;
    const bool allowed = plan.allowed;
    std::vector<std::uint8_t> keep(count, 0U);
    std::vector<std::uint32_t> dropped;
    auto& planned = plan.planned;
    auto& impurities = plan.impurities;
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
    // The collapsed writers whose changes reach a slot in the update phase
    // that makes them: its own (slice drivers share a slot) and those of
    // the slots it follows through a wire or a conversion member, which
    // commit_writes runs in that phase.
    using SlotPair = std::pair<std::uint32_t, std::uint32_t>;
    std::vector<SlotPair> slot_writers;
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (keep[index] != 0U) {
            for (const auto slot : members_[index].collapse_outputs) {
                slot_writers.emplace_back(slot, index);
            }
        }
    }
    std::vector<SlotPair> slot_sources;
    for (const auto& wire : wires_) {
        slot_sources.emplace_back(wire.target, wire.source);
    }
    for (std::uint32_t source = 0U; source < slot_conversions_.size(); ++source) {
        for (const auto member : slot_conversions_[source]) {
            const auto& write = operation_get<WriteUpdate>(
                members_[member].operations()[members_[member].body_end - 1U]);
            slot_sources.emplace_back(slot_of_signal_[write.signal], source);
        }
    }
    std::ranges::sort(slot_writers);
    std::ranges::sort(slot_sources);
    std::vector<std::uint32_t> visited(slot_writers.empty() ? 0U : slots_.size(), 0U);
    std::uint32_t epoch = 0U;
    std::vector<std::uint32_t> pending_slots;
    const auto for_each_writer = [&](const std::uint32_t slot, const auto& visit) {
        ++epoch;
        visited[slot] = epoch;
        pending_slots.assign(1U, slot);
        while (!pending_slots.empty()) {
            const auto at = pending_slots.back();
            pending_slots.pop_back();
            for (const auto& [written, member] : std::ranges::equal_range(
                     slot_writers, at, {}, &SlotPair::first)) {
                visit(member);
            }
            for (const auto& [target, source] : std::ranges::equal_range(
                     slot_sources, at, {}, &SlotPair::first)) {
                if (visited[source] != epoch) {
                    visited[source] = epoch;
                    pending_slots.push_back(source);
                }
            }
        }
    };
    while (!dropped.empty() && !slot_writers.empty()) {
        const auto index = dropped.back();
        dropped.pop_back();
        for (const auto slot : members_[index].collapse_inputs) {
            if (slot == no_slot) {
                continue;
            }
            for_each_writer(slot, [&](const std::uint32_t source) {
                if (keep[source] != 0U) {
                    keep[source] = 0U;
                    dropped.push_back(source);
                }
            });
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
            for_each_writer(slot, [&](const std::uint32_t source) {
                if (keep[source] != 0U) {
                    successors[source].push_back(index);
                    ++indegree[index];
                }
            });
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
    if (kept != 0U && ordered == kept) {
        plan.kept = kept;
        plan.levels = levels;
        plan.member_levels.assign(count, 0U);
        for (std::uint32_t index = 0U; index < count; ++index) {
            if (keep[index] != 0U) {
                plan.member_levels[index] = level[index] + 1U;
            }
        }
    }
    return plan;
}

void Interpreter::Impl::StaticKernel::apply_delta_collapse(const CollapsePlan& plan)
{
    const auto count = static_cast<std::uint32_t>(members_.size());
    collapse_enabled_ = plan.kept != 0U && plan.member_levels.size() == count;
    for (std::uint32_t index = 0U; index < count; ++index) {
        auto& state = member_schedule_[index];
        state.collapse = collapse_enabled_ && plan.member_levels[index] != 0U;
        state.collapse_level = state.collapse ? plan.member_levels[index] - 1U : 0U;
    }
    collapse_buckets_.assign(collapse_enabled_ ? plan.levels : 0U, { });
    collapse_low_ = static_cast<std::uint32_t>(collapse_buckets_.size());
    if (std::getenv("FSIM_PROFILE_PHASES") != nullptr && plan.planned != 0U) {
        std::cerr << "fsim-profile: static-kernel-collapse planned=" << plan.planned
                  << " kept=" << (collapse_enabled_ ? plan.kept : 0U)
                  << " levels=" << (collapse_enabled_ ? plan.levels : 0U)
                  << (plan.allowed ? "" : " host-boundary");
        for (const auto& [reason, members] : plan.impurities) {
            std::cerr << ' ' << reason << '=' << members;
        }
        std::cerr << '\n';
    }
}

void Interpreter::Impl::StaticKernel::report_mt_plan() const
{
    // Units are what one native call runs: a partition, a loose
    // combinational member or a sequential member (multithread proposal
    // §2.2). A unit is eligible for a parallel batch when its compiled code
    // stays on the native tier and owns every slot it writes; domains are
    // the connected components of the settle graph over combinational units
    // (§2.4). Cost is observed runs times code size.
    const auto member_count = static_cast<std::uint32_t>(members_.size());
    const auto unit_total = member_count + static_cast<std::uint32_t>(partitions_.size());
    const auto unit_of = [&](const std::uint32_t member) {
        return members_[member].partition != no_slot
            ? member_count + members_[member].partition : member;
    };
    std::vector<const char*> reason(unit_total, nullptr);
    std::vector<std::uint8_t> kind(unit_total, 0U); // 0 none, 1 comb, 2 seq
    std::vector<std::uint64_t> cost(unit_total, 0U);
    // A generic severity message (an assertion's action block) runs only
    // when the assertion fails; like assert_check it is a logged effect
    // (proposal §4.3), not a reason to stay serial.
    const auto scan = [&](const CompiledBody& body, std::uint32_t member) -> const char* {
        for (const auto& inst : body.code) {
            switch (inst.op) {
            case KOp::mark:
                member = inst.x;
                break;
            case KOp::store_host:
                return "store_host";
            case KOp::generic:
                if (member < members_.size()
                    && inst.x < members_[member].operations().size()
                    && operation_holds<Report>(members_[member].operations()[inst.x])) {
                    break;
                }
                return "generic";
            case KOp::step:
                return "step";
            case KOp::deopt:
                return "deopt";
            case KOp::suspend:
                return "suspend";
            case KOp::mem_copy:
                return "mem_copy";
            default:
                break;
            }
        }
        return nullptr;
    };
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        const auto& member = members_[index];
        if (member.partition != no_slot || member.lazy
            || (member.kind != StaticKernelMemberKind::combinational
                && member.kind != StaticKernelMemberKind::sequential)) {
            continue;
        }
        kind[index] = member.kind == StaticKernelMemberKind::combinational ? 1U : 2U;
        cost[index] = (index < profile_member_runs_.size() ? profile_member_runs_[index] : 0U)
            * (member.compiled ? member.compiled->code.size()
                               : member.body_end - member.body_begin);
        reason[index] = !member.compiled ? "uncompiled"
            : member.generic_mode        ? "generic_mode"
            : member.rewrites_signal     ? "rewrites_signal"
                                         : scan(*member.compiled, index);
    }
    for (std::uint32_t partition = 0U; partition < partitions_.size(); ++partition) {
        const auto unit = member_count + partition;
        kind[unit] = 1U;
        cost[unit] = partition < profile_partition_operations_.size()
            ? profile_partition_operations_[partition] : 0U;
        reason[unit] = scan(partitions_[partition].program, no_slot);
    }
    std::unordered_map<ProcessId, std::uint32_t> member_of_process;
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        member_of_process.emplace(members_[index].process, index);
    }
    // Ownership: the combinational writers of one slot share a domain (one
    // worker runs them, in order); a slot written by two sequential units
    // makes them ineligible (two triggered-batch writers could race).
    std::vector<std::uint32_t> parent(unit_total);
    for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
        parent[unit] = unit;
    }
    const auto find = [&](std::uint32_t at) {
        while (parent[at] != at) {
            parent[at] = parent[parent[at]];
            at = parent[at];
        }
        return at;
    };
    // A slot's reader units: its whole-slot targets (notify_targets_ after
    // build_schedule_targets) and its ranged readers.
    const auto reader_units = [&](const std::uint32_t slot, std::vector<std::uint32_t>& out) {
        out.clear();
        if (slot < slot_notify_.size()) {
            const auto notify = slot_notify_[slot];
            for (auto index = notify.begin; index < notify.end; ++index) {
                const auto id = notify_targets_[index].id;
                out.push_back((id & partition_tag) != 0U
                        ? member_count + (id & ~partition_tag)
                        : unit_of(id));
            }
        }
        for (const auto& reader : slots_[slot].readers) {
            out.push_back(unit_of(reader.member));
        }
        std::ranges::sort(out);
        out.erase(std::ranges::unique(out).begin(), out.end());
    };
    std::vector<std::uint32_t> writers;
    std::vector<std::uint32_t> readers;
    std::vector<std::uint32_t> slot_domain_writer(slots_.size(), no_slot);
    for (std::uint32_t slot_index = 0U; slot_index < slots_.size(); ++slot_index) {
        const auto& slot = slots_[slot_index];
        writers.clear();
        for (const auto& region : slot.writers) {
            if (const auto found = member_of_process.find(region.process);
                found != member_of_process.end()) {
                writers.push_back(unit_of(found->second));
            }
        }
        std::ranges::sort(writers);
        writers.erase(std::ranges::unique(writers).begin(), writers.end());
        if (std::ranges::count_if(writers, [&](const std::uint32_t unit) {
                return kind[unit] == 2U;
            }) > 1) {
            for (const auto unit : writers) {
                if (kind[unit] == 2U && reason[unit] == nullptr) {
                    reason[unit] = "shared_writer";
                }
            }
        }
        std::uint32_t first_comb = no_slot;
        for (const auto unit : writers) {
            if (kind[unit] != 1U) {
                continue;
            }
            if (first_comb == no_slot) {
                first_comb = unit;
                slot_domain_writer[slot_index] = unit;
                continue;
            }
            const auto left = find(first_comb);
            const auto right = find(unit);
            if (left != right) {
                parent[std::max(left, right)] = std::min(left, right);
            }
        }
        if (slot.deferred_write) {
            continue;
        }
        reader_units(slot_index, readers);
        std::erase_if(readers, [&](const std::uint32_t unit) { return kind[unit] != 1U; });
        for (const auto writer : writers) {
            if (kind[writer] != 1U) {
                continue;
            }
            for (const auto reader : readers) {
                const auto left = find(writer);
                const auto right = find(reader);
                if (left != right) {
                    parent[std::max(left, right)] = std::min(left, right);
                }
            }
        }
    }
    struct Totals {
        std::uint64_t units { };
        std::uint64_t eligible { };
        std::uint64_t cost { };
        std::uint64_t eligible_cost { };
    };
    Totals totals[3] { };
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> reasons;
    for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
        if (kind[unit] == 0U) {
            continue;
        }
        auto& total = totals[kind[unit]];
        ++total.units;
        total.cost += cost[unit];
        if (reason[unit] == nullptr) {
            ++total.eligible;
            total.eligible_cost += cost[unit];
        } else {
            auto& entry = reasons[std::string { kind[unit] == 1U ? "comb " : "seq " }
                + reason[unit]];
            ++entry.first;
            entry.second += cost[unit];
        }
    }
    const auto percent = [](const std::uint64_t part, const std::uint64_t whole) {
        return whole == 0U ? 0.0 : 100.0 * static_cast<double>(part)
                / static_cast<double>(whole);
    };
    for (const std::uint32_t phase : { 1U, 2U }) {
        const auto& total = totals[phase];
        std::cerr << "fsim-kernel: mt " << (phase == 1U ? "comb" : "seq")
                  << " units=" << total.units << " eligible=" << total.eligible
                  << " cost=" << total.cost << " eligible_cost%="
                  << percent(total.eligible_cost, total.cost) << '\n';
    }
    for (const auto& [name, entry] : reasons) {
        std::cerr << "fsim-kernel: mt ineligible " << name << " units=" << entry.first
                  << " cost=" << entry.second << '\n';
    }
    // Domains over combinational units, by cost.
    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint32_t>> domains;
    for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
        if (kind[unit] == 1U) {
            auto& domain = domains[find(unit)];
            domain.first += cost[unit];
            ++domain.second;
        }
    }
    // Runs per domain, and slot changes by what a domain-private settle
    // would do with them (proposal §2.4): every target in the writer's
    // domain (private), some target elsewhere or a general effect (logged
    // for the leader), or a change from a sequential unit, the host or a
    // thread (the leader's commit).
    const auto runs_of = [&](const std::uint32_t unit) -> std::uint64_t {
        if (unit < member_count) {
            return unit < profile_member_runs_.size() ? profile_member_runs_[unit] : 0U;
        }
        const auto partition = unit - member_count;
        return partition < profile_partition_counts_.size()
            ? profile_partition_counts_[partition] : 0U;
    };
    std::unordered_map<std::uint32_t, std::uint64_t> domain_runs;
    std::unordered_map<std::uint32_t, std::uint64_t> domain_changes;
    std::uint64_t comb_runs = 0U;
    std::uint64_t seq_runs = 0U;
    for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
        if (kind[unit] == 1U) {
            domain_runs[find(unit)] += runs_of(unit);
            comb_runs += runs_of(unit);
        } else if (kind[unit] == 2U) {
            seq_runs += runs_of(unit);
        }
    }
    std::uint64_t private_changes = 0U;
    std::uint64_t crossing_changes = 0U;
    std::uint64_t commit_changes = 0U;
    std::uint64_t private_targets = 0U;
    std::uint64_t external_targets = 0U;
    const bool slot_counts = profile_slot_change_counts_.size() == slots_.size();
    for (std::uint32_t slot_index = 0U; slot_counts && slot_index < slots_.size(); ++slot_index) {
        const auto changes = profile_slot_change_counts_[slot_index];
        if (changes == 0U) {
            continue;
        }
        const auto& slot = slots_[slot_index];
        reader_units(slot_index, readers);
        const auto writer = slot_domain_writer[slot_index];
        if (writer == no_slot) {
            commit_changes += changes;
            external_targets += changes * readers.size();
            continue;
        }
        const auto domain = find(writer);
        domain_changes[domain] += changes;
        std::uint64_t inside = 0U;
        for (const auto unit : readers) {
            inside += kind[unit] == 1U && find(unit) == domain ? 1U : 0U;
        }
        const auto outside = readers.size() - inside;
        private_targets += changes * inside;
        external_targets += changes * outside;
        // Outputs, edges and thread waiters are the leader's.
        const bool general = slot.output || !slot.edges.empty()
            || (slot_index < slot_waitable_.size() && slot_waitable_[slot_index] != 0U);
        (outside == 0U && !general ? private_changes : crossing_changes) += changes;
    }
    std::cerr << "fsim-kernel: mt runs comb=" << comb_runs << " seq=" << seq_runs << '\n';
    if (slot_counts) {
        const auto all = private_changes + crossing_changes + commit_changes;
        std::cerr << "fsim-kernel: mt changes=" << all
                  << " private%=" << percent(private_changes, all)
                  << " crossing%=" << percent(crossing_changes, all)
                  << " commit%=" << percent(commit_changes, all)
                  << " targets private%="
                  << percent(private_targets, private_targets + external_targets) << '\n';
    }
    std::vector<std::tuple<std::uint64_t, std::uint32_t, std::uint32_t>> ranked;
    for (const auto& [root, domain] : domains) {
        ranked.emplace_back(domain.first, domain.second, root);
    }
    std::ranges::sort(ranked, std::greater<> { });
    const auto comb_cost = totals[1].cost;
    std::cerr << "fsim-kernel: mt domains=" << ranked.size() << " comb_cost=" << comb_cost;
    for (const std::uint32_t threads : { 2U, 4U, 8U, 16U }) {
        // Domain-parallel settle bound: the largest domain or an even share.
        const auto largest = ranked.empty() ? 0U : std::get<0>(ranked.front());
        const auto share = static_cast<double>(comb_cost) / threads;
        const auto bound = std::max(static_cast<double>(largest), share);
        std::cerr << " t" << threads << "=x"
                  << (bound == 0.0 ? 1.0 : static_cast<double>(comb_cost) / bound);
    }
    std::cerr << '\n';
    for (std::size_t rank = 0U; rank < ranked.size() && rank < 8U; ++rank) {
        const auto [domain_cost, units, root] = ranked[rank];
        // The domain's heaviest unit names it.
        std::uint32_t heaviest = root;
        for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
            if (kind[unit] == 1U && find(unit) == root && cost[unit] > cost[heaviest]) {
                heaviest = unit;
            }
        }
        const auto named = heaviest < member_count ? heaviest
                                                   : partitions_[heaviest - member_count].members.front();
        const auto found_runs = domain_runs.find(root);
        const auto found_changes = domain_changes.find(root);
        std::cerr << "fsim-kernel: mt domain cost%=" << percent(domain_cost, comb_cost)
                  << " runs%=" << percent(found_runs == domain_runs.end() ? 0U
                                                  : found_runs->second, comb_runs)
                  << " changes%=" << percent(found_changes == domain_changes.end() ? 0U
                                                     : found_changes->second,
                                         private_changes + crossing_changes)
                  << " units=" << units << " heaviest="
                  << (named < profile_names_.size() ? profile_names_[named] : std::string { "?" })
                  << (heaviest < member_count ? "" : " (partition)") << '\n';
    }
}

} // namespace fsim::runtime::simir

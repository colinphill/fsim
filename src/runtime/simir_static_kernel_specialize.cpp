// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 static kernel: specialization of VHDL member bodies on their
// constant control flow. A subprogram such as a bit-serial GF multiply runs
// loops whose bounds, indices and range checks depend only on constants; the
// specializer evaluates that part once, at kernel construction, with the
// reference evaluator (step_generic), so calls inline, constant loops unroll,
// range checks and index arithmetic fold, and dynamic selects with a known
// index become static ones. What remains depends on signal values and is
// emitted unchanged.
//
// The result is ordinary SimIR appended to the member's operations, and the
// member's activations enter it instead of the original body. The reference
// evaluator, deoptimization (which resumes at a SimIR operation) and 'U'
// tracking therefore treat it like any other body; Member::origin maps its
// operations back to the original ones for diagnostics. The first activation
// (operation 0, through the process prologue) still runs the original body.
#include "simir_static_kernel_compiled_internal.hpp"

#include <cstdlib>
#include <string>
#include <iostream>
#include <unordered_map>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;
using namespace static_kernel_compiled_detail;

namespace {

/// A register whose value the specializer knows at a program point;
/// `materialized` records that the register itself holds it.
struct KnownRegister {
    RegisterId reg { };
    bool materialized { };
    PackedLogic4 value;
    std::uint64_t hash { };
};

/// Register renames applied to one operation's reads.
using Renames = std::vector<std::pair<RegisterId, RegisterId>>;

struct SpecializeState {
    /// Sorted by register.
    std::vector<KnownRegister> known;
    /// Return targets of the inlined calls.
    std::vector<InstructionIndex> stack;
    /// Registers holding a signal's current value from an earlier read in
    /// this activation (sorted by register). A VHDL signal does not change
    /// during an activation unless the member writes it, so reading it
    /// again into the same register is redundant.
    std::vector<std::pair<RegisterId, SignalId>> holds;
    /// Registers a copy assigned from another register whose value they
    /// still equal (sorted by copy). The copy is emitted only when needed:
    /// before the source changes, or at the exit if the copy is live.
    /// Reads of the copy read the source instead.
    std::vector<std::pair<RegisterId, RegisterId>> aliases;
};

[[nodiscard]] std::uint64_t mix(std::uint64_t hash, const std::uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

[[nodiscard]] std::span<const std::uint64_t> value_words(
    const PackedLogic4& value, const std::size_t plane)
{
    if (value.is_logic9()) {
        return value.logic9_plane_words(plane);
    }
    return plane == 0U ? value.aval_words()
        : plane == 1U  ? value.bval_words()
                       : std::span<const std::uint64_t> { };
}

[[nodiscard]] bool same_value(const PackedLogic4& left, const PackedLogic4& right)
{
    if (left.width() != right.width() || left.is_logic9() != right.is_logic9()) {
        return false;
    }
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        const auto a = value_words(left, plane);
        const auto b = value_words(right, plane);
        if (!std::ranges::equal(a, b)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint64_t state_hash(const InstructionIndex pc,
    const SpecializeState& state)
{
    auto hash = mix(0U, pc);
    for (const auto target : state.stack) {
        hash = mix(hash, target);
    }
    for (const auto& known : state.known) {
        hash = mix(hash, known.reg);
        hash = mix(hash, known.hash);
    }
    return hash;
}

[[nodiscard]] std::uint64_t value_hash(const PackedLogic4& value)
{
    auto hash = mix(0U, value.width() | (value.is_logic9() ? 1ULL << 63U : 0U));
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        for (const auto word : value_words(value, plane)) {
            hash = mix(hash, word);
        }
    }
    return hash;
}

/// A block generated for state `block` can be entered from `arriving`: the
/// same call stack and known values (materialization aside), no aliases,
/// and every signal-holding fact the block relies on.
[[nodiscard]] bool same_values(const SpecializeState& block,
    const SpecializeState& arriving)
{
    const auto& left = block;
    const auto& right = arriving;
    if (left.stack != right.stack || !left.aliases.empty() || !right.aliases.empty()
        || left.known.size() != right.known.size()
        || !std::ranges::includes(right.holds, left.holds)) {
        return false;
    }
    for (std::size_t index = 0U; index < left.known.size(); ++index) {
        const auto& a = left.known[index];
        const auto& b = right.known[index];
        if (a.reg != b.reg || a.hash != b.hash || !same_value(a.value, b.value)) {
            return false;
        }
    }
    return true;
}

/// Operations whose result depends only on their register operands and
/// fields, without effects: the specializer evaluates them when every
/// operand is known.
template <typename T>
inline constexpr bool specialize_pure = std::is_same_v<T, LoadConstant>
    || std::is_same_v<T, CopyRegister> || std::is_same_v<T, ConvertToTwoState>
    || std::is_same_v<T, UnaryNot> || std::is_same_v<T, LogicalNot>
    || std::is_same_v<T, LogicalBinary> || std::is_same_v<T, Reduction>
    || std::is_same_v<T, Shift> || std::is_same_v<T, Extract>
    || std::is_same_v<T, DynamicExtract> || std::is_same_v<T, DynamicPartSelect>
    || std::is_same_v<T, Insert> || std::is_same_v<T, DynamicInsert>
    || std::is_same_v<T, DynamicPartInsert> || std::is_same_v<T, Concatenate>
    || std::is_same_v<T, Binary> || std::is_same_v<T, ConditionalSelect>
    || std::is_same_v<T, IntegerBinary> || std::is_same_v<T, IntegerUnary>;

/// Operations emitted unchanged; operation_registers lists every register
/// they read and write.
template <typename T>
inline constexpr bool specialize_effect = std::is_same_v<T, ReadSignal>
    || std::is_same_v<T, WriteBlocking> || std::is_same_v<T, WriteBlockingSlice>
    || std::is_same_v<T, WriteBlockingDynamicSlice>
    || std::is_same_v<T, WriteBlockingDynamicPartSlice>
    || std::is_same_v<T, WriteUpdate> || std::is_same_v<T, WriteUpdateSlice>
    || std::is_same_v<T, WriteUpdateDynamicSlice>
    || std::is_same_v<T, WriteUpdateDynamicPartSlice>
    || std::is_same_v<T, WriteProjected> || std::is_same_v<T, WriteProjectedSlice>
    || std::is_same_v<T, WriteProjectedDynamicSlice> || std::is_same_v<T, Assert>
    || std::is_same_v<T, ContainerRead>
    || std::is_same_v<T, WriteContainerObjectElement>;

/// The specialized body of one operation list. Operations flagged verbatim
/// are copies of original operation origin[i] (signals, report texts and
/// other instance fields come from each member's own expanded operation);
/// the others (constants, jumps, rewritten selects, branches) are shared.
struct SpecializeRecipe {
    bool accepted { };
    std::uint32_t size { };
    std::uint32_t entry { };
    std::uint32_t stop { };
    std::vector<ValueKind> kinds;
    std::vector<Operation> out;
    std::vector<InstructionIndex> origin;
    std::vector<std::uint8_t> verbatim;
    /// Register renames of verbatim operations (reads of copies).
    std::vector<Renames> renames;
    /// The signal of every signal operation the activation reaches: which
    /// operations name the same signal decides which reads are redundant.
    std::vector<std::pair<InstructionIndex, SignalId>> signals;
};

/// Applies f to every register operand operation_registers reports as read.
template <typename F>
void map_reads(Operation& operation, F&& f)
{
    visit_operation([&](auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, CopyRegister> || std::is_same_v<T, UnaryNot>
            || std::is_same_v<T, ConvertToTwoState> || std::is_same_v<T, LogicalNot>
            || std::is_same_v<T, Reduction> || std::is_same_v<T, Extract>
            || std::is_same_v<T, IntegerUnary> || std::is_same_v<T, IntegerCheck>
            || std::is_same_v<T, FormatDisplay>) {
            op.source = f(op.source);
        } else if constexpr (std::is_same_v<T, Binary> || std::is_same_v<T, LogicalBinary>
            || std::is_same_v<T, IntegerBinary>) {
            op.lhs = f(op.lhs);
            op.rhs = f(op.rhs);
        } else if constexpr (std::is_same_v<T, Shift>) {
            op.value = f(op.value);
            op.amount = f(op.amount);
        } else if constexpr (std::is_same_v<T, DynamicExtract>) {
            op.source = f(op.source);
            op.selection.index = f(op.selection.index);
        } else if constexpr (std::is_same_v<T, DynamicPartSelect>) {
            op.source = f(op.source);
            op.base = f(op.base);
        } else if constexpr (std::is_same_v<T, Insert>) {
            op.target = f(op.target);
            op.source = f(op.source);
        } else if constexpr (std::is_same_v<T, DynamicInsert>) {
            op.target = f(op.target);
            op.source = f(op.source);
            op.selection.index = f(op.selection.index);
        } else if constexpr (std::is_same_v<T, DynamicPartInsert>) {
            op.target = f(op.target);
            op.source = f(op.source);
            op.selection.base = f(op.selection.base);
        } else if constexpr (std::is_same_v<T, Concatenate>) {
            for (auto& operand : op.operands) {
                operand = f(operand);
            }
        } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
            op.condition = f(op.condition);
            op.when_true = f(op.when_true);
            op.when_false = f(op.when_false);
        } else if constexpr (std::is_same_v<T, Branch> || std::is_same_v<T, Assert>) {
            op.condition = f(op.condition);
        } else if constexpr (std::is_same_v<T, WriteBlocking>
            || std::is_same_v<T, WriteBlockingSlice> || std::is_same_v<T, WriteUpdate>
            || std::is_same_v<T, WriteUpdateSlice> || std::is_same_v<T, WriteProjected>
            || std::is_same_v<T, WriteProjectedSlice>) {
            op.source = f(op.source);
        } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
            || std::is_same_v<T, WriteUpdateDynamicSlice>
            || std::is_same_v<T, WriteProjectedDynamicSlice>) {
            op.source = f(op.source);
            op.selection.index = f(op.selection.index);
        } else if constexpr (std::is_same_v<T, WriteBlockingDynamicPartSlice>
            || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
            op.source = f(op.source);
            op.selection.base = f(op.selection.base);
        } else if constexpr (std::is_same_v<T, ContainerRead>) {
            op.index = f(op.index);
        } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
            op.index = f(op.index);
            op.source = f(op.source);
            if (op.dynamic_part) {
                op.dynamic_part->base = f(op.dynamic_part->base);
            }
        }
    }, operation);
}

void apply_renames(Operation& operation, const Renames& renames)
{
    map_reads(operation, [&](const RegisterId reg) {
        for (const auto& [from, to] : renames) {
            if (from == reg) {
                return to;
            }
        }
        return reg;
    });
}

[[nodiscard]] std::optional<SignalId> operation_signal(const Operation& operation)
{
    std::optional<SignalId> signal;
    visit_operation([&](const auto& op) {
        if constexpr (requires { op.signal; }) {
            if constexpr (std::is_same_v<std::decay_t<decltype(op.signal)>, SignalId>) {
                signal = op.signal;
            }
        }
    }, operation);
    return signal;
}

/// Recipes by shared canonical operation storage. Members whose lists
/// share it and have no operation overrides differ only in instance fields,
/// which specialization never reads.
using SpecializeCache = std::unordered_map<const void*,
    std::vector<std::shared_ptr<const SpecializeRecipe>>>;

/// Activations of one member must not spend more than this many evaluation
/// steps, or emit more than this many operations, in the specializer.
constexpr std::size_t specialize_step_budget = 400'000U;
constexpr std::size_t specialize_size_budget = 16'384U;
constexpr std::size_t specialize_call_depth = 32U;

} // namespace

bool Interpreter::Impl::StaticKernel::specialize_member(
    const std::uint32_t member_index)
{
    auto& member = members_[member_index];
    const auto size = static_cast<std::uint32_t>(member.operations.size());
    const auto entry = member.body_begin;
    const auto stop = member.body_end;
    if (!member.vhdl || member.kind == StaticKernelMemberKind::behavioral
        || entry >= size || stop >= size
        || member.exit_alt != std::numeric_limits<std::uint32_t>::max()
        || !operation_holds<WaitSensitivity>(member.operations[stop])) {
        return false;
    }

    // A recipe from another member fits when each verbatim operation has the
    // same kind and registers here.
    const auto fits = [&](const SpecializeRecipe& recipe) {
        std::vector<RegisterId> reads_here;
        std::vector<RegisterId> reads_there;
        std::optional<RegisterId> write_here;
        std::optional<RegisterId> write_there;
        for (std::size_t index = 0U; index < recipe.out.size(); ++index) {
            if (recipe.verbatim[index] == 0U) {
                continue;
            }
            const auto& here = member.operations[recipe.origin[index]];
            const auto& there = recipe.out[index];
            bool same_kind = false;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                same_kind = operation_holds<T>(here);
            }, there);
            operation_registers(here, reads_here, write_here);
            operation_registers(there, reads_there, write_there);
            if (!same_kind || reads_here != reads_there || write_here != write_there) {
                return false;
            }
        }
        std::unordered_map<SignalId, SignalId> there_to_here;
        std::unordered_map<SignalId, SignalId> here_to_there;
        for (const auto& [pc, there] : recipe.signals) {
            const auto here = operation_signal(member.operations[pc]);
            if (!here || there_to_here.try_emplace(there, *here).first->second != *here
                || here_to_there.try_emplace(*here, there).first->second != there) {
                return false;
            }
        }
        return true;
    };
    const auto apply = [&](const SpecializeRecipe& recipe) {
        if (!recipe.accepted) {
            return false;
        }
        member.origin.resize(size);
        for (std::uint32_t index = 0U; index < size; ++index) {
            member.origin[index] = index;
        }
        member.origin.insert(member.origin.end(), recipe.origin.begin(),
            recipe.origin.end());
        member.operations.reserve(size + recipe.out.size());
        for (std::size_t index = 0U; index < recipe.out.size(); ++index) {
            if (recipe.verbatim[index] == 0U) {
                member.operations.push_back(recipe.out[index]);
                continue;
            }
            member.operations.push_back(member.operations[recipe.origin[index]]);
            if (!recipe.renames[index].empty()) {
                apply_renames(member.operations.back(), recipe.renames[index]);
            }
        }
        member.body_begin = size;
        return true;
    };
    const void* identity = nullptr;
    {
        const auto program = impl_.processes.program_view(member.process);
        const auto& list = program.operations();
        // Instance overrides (signals, report texts, debug points) are
        // admitted only on operations the specializer copies verbatim or
        // drops.
        if (list.size() == size) {
            identity = list.data();
            for (std::uint32_t index = 0U; index < size && identity != nullptr;
                 ++index) {
                if (&list[index] == list.data() + index) {
                    continue;
                }
                visit_operation([&](const auto& op) {
                    using T = std::decay_t<decltype(op)>;
                    if constexpr (!specialize_effect<T>
                        && !std::is_same_v<T, WaitSensitivity>
                        && !std::is_same_v<T, DebugPoint>) {
                        identity = nullptr;
                    }
                }, list[index]);
            }
        }
    }
    if (!specialize_cache_) {
        specialize_cache_ = std::make_shared<SpecializeCache>();
    }
    auto& cache = *static_cast<SpecializeCache*>(specialize_cache_.get());
    if (identity != nullptr) {
        for (const auto& recipe : cache[identity]) {
            if (recipe->size == size && recipe->entry == entry
                && recipe->stop == stop && recipe->kinds == member.register_kinds
                && fits(*recipe)) {
                ++specialize_cache_hits_;
                return apply(*recipe);
            }
        }
    }
    auto recipe = std::make_shared<SpecializeRecipe>();
    recipe->size = size;
    recipe->entry = entry;
    recipe->stop = stop;
    recipe->kinds = member.register_kinds;
    const auto finish = [&](const bool accepted) {
        recipe->accepted = accepted;
        if (identity != nullptr) {
            cache[identity].push_back(recipe);
        }
        return apply(*recipe);
    };

    // Admission: every operation the activation reaches is understood, and
    // there is a loop to gain from (a backward edge). Frames are
    // admitted only when isolated (as the compiled tier requires): inlined
    // calls then need no register snapshots.
    // Unrolling must not repeat reads of wide signals: a register defined
    // once by such a read is what lets the compiled tier load just the
    // selected field. Nor may the body grow much (codegen time).
    const auto wide_read = [&](const Operation& operation) {
        const auto* read = operation_get_if<ReadSignal>(&operation);
        return read != nullptr && read->signal < impl_.signals.size()
            && impl_.get_signal(read->signal).initial_value.width() > 64U;
    };
    std::vector<std::uint8_t> leader(size, 0U);
    std::size_t reachable = 0U;
    std::size_t reads_before = 0U;
    {
        std::vector<std::uint8_t> seen(size, 0U);
        std::vector<std::uint32_t> work { entry };
        bool gain = false;
        bool admitted = true;
        leader[entry] = 1U;
        const auto edge = [&](const std::uint32_t from, const std::uint32_t to) {
            if (to >= size) {
                admitted = false;
                return;
            }
            leader[to] = 1U;
            gain = gain || to <= from;
            work.push_back(to);
        };
        while (!work.empty() && admitted) {
            const auto pc = work.back();
            work.pop_back();
            if (pc >= size) {
                admitted = false;
                break;
            }
            if (seen[pc] != 0U || pc == stop) {
                continue;
            }
            seen[pc] = 1U;
            ++reachable;
            reads_before += wide_read(member.operations[pc]) ? 1U : 0U;
            if (const auto signal = operation_signal(member.operations[pc])) {
                recipe->signals.emplace_back(pc, *signal);
            }
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    edge(pc, op.target);
                } else if constexpr (std::is_same_v<T, Branch>) {
                    edge(pc, op.when_true);
                    edge(pc, op.when_false);
                } else if constexpr (std::is_same_v<T, Call>) {
                    edge(pc, op.target);
                    edge(pc, op.return_target);
                } else if constexpr (std::is_same_v<T, Return>
                    || std::is_same_v<T, Halt>) {
                } else if constexpr (std::is_same_v<T, CallableFramePush>) {
                    admitted = admitted && op.native_isolated;
                    work.push_back(pc + 1U);
                } else if constexpr (specialize_pure<T> || specialize_effect<T>
                    || std::is_same_v<T, IntegerCheck>
                    || std::is_same_v<T, DebugPoint>
                    || std::is_same_v<T, CallableFramePop>) {
                    work.push_back(pc + 1U);
                } else {
                    admitted = false;
                }
            }, member.operations[pc]);
        }
        if (!admitted || !gain) {
            return finish(false);
        }
    }

    // Registers live across activations: only these must hold their values
    // when the activation ends.
    std::vector<std::uint64_t> live_at_entry;
    {
        std::vector<std::uint8_t> unreached(size, 0U);
        prune_operations(member_index, unreached, live_at_entry);
    }
    const auto live = [&](const RegisterId reg) {
        return live_at_entry.empty()
            || (reg / 64U < live_at_entry.size()
                && ((live_at_entry[reg / 64U] >> (reg % 64U)) & 1U) != 0U);
    };
    const auto kind_of = [&](const RegisterId reg) {
        return reg < member.register_kinds.size() ? member.register_kinds[reg]
                                                  : ValueKind::logic4;
    };

    // Folding runs the reference evaluator on the member's own registers;
    // they are restored afterwards.
    const auto saved_registers = member.registers;
    const auto saved_stack = member.call_stack;
    auto& out = recipe->out;
    auto& origin = recipe->origin;
    auto& verbatim = recipe->verbatim;
    auto& renames = recipe->renames;
    struct Block {
        InstructionIndex pc { };
        std::uint32_t start { };
        SpecializeState state;
    };
    std::vector<Block> blocks;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> block_index;
    struct Pending {
        std::uint32_t branch { };
        bool when_true { };
        InstructionIndex pc { };
        SpecializeState state;
    };
    std::vector<Pending> pending;
    std::size_t steps = 0U;
    std::vector<RegisterId> reads;
    std::optional<RegisterId> write;
    bool abandoned = false;

    const auto find = [](SpecializeState& state, const RegisterId reg)
        -> KnownRegister* {
        const auto at = std::ranges::lower_bound(state.known, reg, { },
            &KnownRegister::reg);
        return at != state.known.end() && at->reg == reg ? &*at : nullptr;
    };
    const auto release = [](SpecializeState& state, const RegisterId reg) {
        const auto at = std::ranges::lower_bound(state.holds, reg, { },
            &std::pair<RegisterId, SignalId>::first);
        if (at != state.holds.end() && at->first == reg) {
            state.holds.erase(at);
        }
    };
    const auto alias_root = [](const SpecializeState& state, const RegisterId reg) {
        const auto at = std::ranges::lower_bound(state.aliases, reg, { },
            &std::pair<RegisterId, RegisterId>::first);
        return at != state.aliases.end() && at->first == reg ? at->second : reg;
    };
    const auto drop_alias = [](SpecializeState& state, const RegisterId reg) {
        const auto at = std::ranges::lower_bound(state.aliases, reg, { },
            &std::pair<RegisterId, RegisterId>::first);
        if (at != state.aliases.end() && at->first == reg) {
            state.aliases.erase(at);
        }
    };
    const auto holds_signal = [](const SpecializeState& state, const RegisterId reg)
        -> std::optional<SignalId> {
        const auto at = std::ranges::lower_bound(state.holds, reg, { },
            &std::pair<RegisterId, SignalId>::first);
        return at != state.holds.end() && at->first == reg
            ? std::optional<SignalId> { at->second } : std::nullopt;
    };
    const auto forget = [&](SpecializeState& state, const RegisterId reg) {
        const auto at = std::ranges::lower_bound(state.known, reg, { },
            &KnownRegister::reg);
        if (at != state.known.end() && at->reg == reg) {
            state.known.erase(at);
        }
        release(state, reg);
    };
    std::size_t reads_after = 0U;
    const auto emit = [&](Operation operation, const InstructionIndex from,
                          const bool copy = false, Renames applied = { }) {
        reads_after += wide_read(operation) ? 1U : 0U;
        out.push_back(std::move(operation));
        origin.push_back(from);
        verbatim.push_back(copy ? 1U : 0U);
        renames.push_back(std::move(applied));
    };
    // Copies of reg are emitted before reg changes.
    const auto spill_aliases_of = [&](SpecializeState& state, const RegisterId reg,
                                      const InstructionIndex from) {
        for (auto at = state.aliases.begin(); at != state.aliases.end();) {
            if (at->second == reg) {
                emit(CopyRegister { at->first, reg }, from);
                at = state.aliases.erase(at);
            } else {
                ++at;
            }
        }
    };
    const auto learn = [&](SpecializeState& state, const RegisterId reg,
                           PackedLogic4 value, const InstructionIndex from) {
        spill_aliases_of(state, reg, from);
        drop_alias(state, reg);
        release(state, reg);
        const auto at = std::ranges::lower_bound(state.known, reg, { },
            &KnownRegister::reg);
        const auto hash = value_hash(value);
        if (at != state.known.end() && at->reg == reg) {
            at->materialized = false;
            at->value = std::move(value);
            at->hash = hash;
        } else {
            state.known.insert(at,
                KnownRegister { reg, false, std::move(value), hash });
        }
    };
    const auto materialize = [&](SpecializeState& state, const RegisterId reg,
                                 const InstructionIndex from) {
        if (auto* known = find(state, reg); known != nullptr && !known->materialized) {
            emit(LoadConstant { reg, known->value }, from);
            known->materialized = true;
        }
    };
    // Before a halt every register; at the activation's end the live ones.
    const auto materialize_all = [&](SpecializeState& state,
                                     const InstructionIndex from, const bool live_only) {
        for (auto& known : state.known) {
            if (!known.materialized && (!live_only || live(known.reg))) {
                emit(LoadConstant { known.reg, known.value }, from);
                known.materialized = true;
            }
        }
        for (const auto& [copy, source] : state.aliases) {
            if (!live_only || live(copy)) {
                emit(CopyRegister { copy, source }, from);
            }
        }
        state.aliases.clear();
    };
    // Emits an operation for pc after materializing its operands; copy marks
    // operation pc itself.
    const auto emit_operation = [&](SpecializeState& state, Operation operation,
                                    const InstructionIndex pc, const bool copy) {
        std::vector<RegisterId> operands;
        std::optional<RegisterId> target;
        operation_registers(operation, operands, target);
        Renames applied;
        for (const auto reg : operands) {
            materialize(state, reg, pc);
            if (const auto root = alias_root(state, reg); root != reg
                && std::ranges::find(applied, reg, &std::pair<RegisterId, RegisterId>::first)
                    == applied.end()) {
                applied.emplace_back(reg, root);
            }
        }
        auto renamed = operation;
        if (!applied.empty()) {
            apply_renames(renamed, applied);
            // Every renamed operand must be one operation_registers reports.
            std::vector<RegisterId> check;
            std::optional<RegisterId> check_target;
            operation_registers(renamed, check, check_target);
            for (std::size_t index = 0U; index < operands.size(); ++index) {
                if (index >= check.size() || check[index] != alias_root(state, operands[index])) {
                    abandoned = true;
                }
            }
        }
        if (target) {
            spill_aliases_of(state, *target, pc);
            drop_alias(state, *target);
            forget(state, *target);
        }
        if (!copy) {
            operation = std::move(renamed);
            applied.clear();
        }
        if (!operation_holds<ReadSignal>(operation)) {
            if (const auto signal = operation_signal(operation)) {
                std::erase_if(state.holds, [&](const auto& held) {
                    return held.second == *signal;
                });
            }
        }
        emit(std::move(operation), pc, copy, std::move(applied));
    };
    // Evaluates operation pc on known operands; false when it fails (the
    // emitted operation then reports the failure at run time).
    const auto evaluate = [&](SpecializeState& state, const InstructionIndex pc)
        -> std::optional<InstructionIndex> {
        for (const auto reg : reads) {
            if (reg >= member.registers.size()) {
                return std::nullopt;
            }
            member.registers[reg] = find(state, reg)->value;
        }
        try {
            return step_generic(member_index, pc);
        } catch (...) {
            return std::nullopt;
        }
    };

    static const char* const factor_text
        = std::getenv("FSIM_STATIC_KERNEL_SPECIALIZE_FACTOR");
    const std::size_t size_factor = factor_text != nullptr
        ? static_cast<std::size_t>(std::strtoull(factor_text, nullptr, 10)) : 2U;
    const auto size_limit = std::min(specialize_size_budget, size_factor * reachable + 64U);
    bool split = false;
    const auto run_path = [&](InstructionIndex pc, SpecializeState state) {
        while (true) {
            if (++steps > specialize_step_budget || out.size() > size_limit
                || reads_after > reads_before || pc >= size) {
                abandoned = true;
                return;
            }
            if (pc == stop) {
                materialize_all(state, pc, true);
                emit(Jump { stop }, pc);
                return;
            }
            // Paths can meet only after an unknown branch has split them;
            // before that the single path never revisits a state (that
            // would be a loop that does not terminate).
            if (leader[pc] != 0U && split) {
                // Copies are made real where paths may meet, so that paths
                // differing only in deferred copies share the block.
                for (const auto& [copy, source] : state.aliases) {
                    emit(CopyRegister { copy, source }, pc);
                }
                state.aliases.clear();
                const auto hash = state_hash(pc, state);
                auto& candidates = block_index[hash];
                for (const auto block : candidates) {
                    if (blocks[block].pc == pc && same_values(blocks[block].state, state)) {
                        // The block relies on the registers it found
                        // materialized; the others it materializes itself.
                        const auto& expected = blocks[block].state.known;
                        for (std::size_t index = 0U; index < expected.size(); ++index) {
                            if (expected[index].materialized) {
                                materialize(state, expected[index].reg, pc);
                            }
                        }
                        state.holds = blocks[block].state.holds;
                        emit(Jump { size + blocks[block].start }, pc);
                        return;
                    }
                }
                candidates.push_back(static_cast<std::uint32_t>(blocks.size()));
                blocks.push_back(Block { pc, static_cast<std::uint32_t>(out.size()), state });
            }
            const auto& operation = member.operations[pc];
            bool ended = false;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, DebugPoint>
                    || std::is_same_v<T, CallableFramePush>
                    || std::is_same_v<T, CallableFramePop>) {
                    ++pc;
                } else if constexpr (std::is_same_v<T, Jump>) {
                    pc = op.target;
                } else if constexpr (std::is_same_v<T, Branch>) {
                    if (find(state, op.condition) != nullptr) {
                        reads.assign(1U, op.condition);
                        if (const auto next = evaluate(state, pc)) {
                            pc = *next;
                            return;
                        }
                    }
                    materialize(state, op.condition, pc);
                    auto renamed = op;
                    renamed.condition = alias_root(state, op.condition);
                    const auto branch = static_cast<std::uint32_t>(out.size());
                    emit(renamed, pc);
                    pending.push_back(Pending { branch, false, op.when_false, state });
                    pending.push_back(Pending { branch, true, op.when_true, state });
                    split = true;
                    ended = true;
                } else if constexpr (std::is_same_v<T, Call>) {
                    if (state.stack.size() >= specialize_call_depth
                        || std::ranges::find(state.stack, op.return_target)
                            != state.stack.end()) {
                        abandoned = true;
                        ended = true;
                        return;
                    }
                    state.stack.push_back(op.return_target);
                    pc = op.target;
                } else if constexpr (std::is_same_v<T, Return>) {
                    if (state.stack.empty()) {
                        abandoned = true;
                        ended = true;
                        return;
                    }
                    pc = state.stack.back();
                    state.stack.pop_back();
                } else if constexpr (std::is_same_v<T, Halt>) {
                    materialize_all(state, pc, false);
                    emit(op, pc, true);
                    ended = true;
                } else if constexpr (specialize_pure<T>
                    || std::is_same_v<T, IntegerCheck>) {
                    operation_registers(operation, reads, write);
                    const bool known = std::ranges::all_of(reads,
                        [&](const RegisterId reg) { return find(state, reg) != nullptr; });
                    if (known && evaluate(state, pc)) {
                        if (write) {
                            learn(state, *write, member.registers[*write], pc);
                        }
                        ++pc;
                        return;
                    }
                    // A copy of an unknown register of the same kind becomes
                    // an alias; it is emitted only if it must be.
                    if constexpr (std::is_same_v<T, CopyRegister>) {
                        const auto root = alias_root(state, op.source);
                        if (!known && op.destination < member.registers.size()
                            && root < member.registers.size()
                            && kind_of(op.destination) == kind_of(root)) {
                            if (op.destination != root) {
                                spill_aliases_of(state, op.destination, pc);
                                drop_alias(state, op.destination);
                                forget(state, op.destination);
                                const auto at = std::ranges::lower_bound(state.aliases,
                                    op.destination, { },
                                    &std::pair<RegisterId, RegisterId>::first);
                                state.aliases.insert(at, { op.destination, root });
                            }
                            ++pc;
                            return;
                        }
                    }
                    // A part select with a known base, in range of a source
                    // holding a signal's value, is the static extract the
                    // reference evaluator itself takes in that case.
                    if constexpr (std::is_same_v<T, DynamicPartSelect>) {
                        const auto* base = find(state, op.base);
                        const auto root = alias_root(state, op.source);
                        const auto held = holds_signal(state, root);
                        if (base != nullptr && find(state, op.source) == nullptr && held
                            && base->value.width() == 32U && op.width != 0U) {
                            if (const auto value = base->value.known_signed_value()) {
                                const auto signed_base = static_cast<std::int64_t>(
                                    static_cast<std::int32_t>(*value));
                                const auto edge = static_cast<std::int64_t>(op.width - 1U);
                                const auto selected_right = op.increasing
                                    ? signed_base + (op.source_descending ? 0 : edge)
                                    : signed_base - (op.source_descending ? edge : 0);
                                const auto selected_last = selected_right
                                    + (op.source_descending ? edge : -edge);
                                const auto low = std::min(selected_right, selected_last);
                                const auto high = std::max(selected_right, selected_last);
                                const bool in_range = low >= std::min(op.left, op.right)
                                    && high <= std::max(op.left, op.right);
                                const bool increasing = op.source_descending
                                    ? low >= op.right : high <= op.right;
                                const auto first = selected_right >= op.right
                                    ? selected_right - op.right
                                    : op.right - selected_right;
                                const auto offset = static_cast<std::uint64_t>(first)
                                    + op.base_offset;
                                if (in_range && increasing
                                    && offset + op.width <= impl_.get_signal(*held)
                                           .initial_value.width()) {
                                    emit_operation(state,
                                        Extract { op.destination, op.source,
                                            static_cast<std::uint32_t>(offset), op.width },
                                        pc, false);
                                    ++pc;
                                    return;
                                }
                            }
                        }
                    }
                    // A strict (VHDL) select with a known in-range index is
                    // the static select at that offset.
                    if constexpr (std::is_same_v<T, DynamicExtract>
                        || std::is_same_v<T, DynamicInsert>) {
                        if (const auto* index = find(state, op.selection.index);
                            index != nullptr && op.selection.strict) {
                            std::optional<std::uint32_t> offset;
                            try {
                                offset = dynamic_index_offset(index->value, op.selection);
                            } catch (const std::invalid_argument&) {
                            }
                            if (offset) {
                                if constexpr (std::is_same_v<T, DynamicExtract>) {
                                    emit_operation(state,
                                        Extract { op.destination, op.source, *offset, 1U }, pc,
                                        false);
                                } else {
                                    emit_operation(state,
                                        Insert { op.destination, op.target, op.source, *offset },
                                        pc, false);
                                }
                                ++pc;
                                return;
                            }
                        }
                    }
                    emit_operation(state, operation, pc, true);
                    ++pc;
                } else if constexpr (std::is_same_v<T, ReadSignal>) {
                    const bool plain = op.kind == SignalReadKind::current
                        && op.ticks == 1U && !op.clock && !op.gate;
                    const auto held = std::ranges::lower_bound(state.holds,
                        op.destination, { }, &std::pair<RegisterId, SignalId>::first);
                    if (plain && held != state.holds.end()
                        && held->first == op.destination && held->second == op.signal) {
                        ++pc;
                        return;
                    }
                    emit_operation(state, operation, pc, true);
                    if (plain) {
                        const auto at = std::ranges::lower_bound(state.holds,
                            op.destination, { }, &std::pair<RegisterId, SignalId>::first);
                        state.holds.insert(at, { op.destination, op.signal });
                    }
                    ++pc;
                } else if constexpr (specialize_effect<T>) {
                    emit_operation(state, operation, pc, true);
                    ++pc;
                } else {
                    abandoned = true;
                    ended = true;
                }
            }, operation);
            if (ended || abandoned) {
                return;
            }
        }
    };

    run_path(entry, SpecializeState { });
    while (!pending.empty() && !abandoned) {
        auto next = std::move(pending.back());
        pending.pop_back();
        auto& branch = operation_get<Branch>(out[next.branch]);
        (next.when_true ? branch.when_true : branch.when_false)
            = size + static_cast<std::uint32_t>(out.size());
        run_path(next.pc, std::move(next.state));
    }
    member.registers = saved_registers;
    member.call_stack = saved_stack;
    const bool accepted = !abandoned && reads_after <= reads_before
        && out.size() <= size_limit;
    static const bool debug = std::getenv("FSIM_STATIC_KERNEL_SPECIALIZE_DEBUG") != nullptr;
    if (debug) {
        std::cerr << "fsim-kernel-specialize: "
                  << impl_.processes.program_view(member.process).name()
                  << " reachable=" << reachable << " out=" << out.size()
                  << " reads=" << reads_before << "->" << reads_after
                  << " steps=" << steps << " blocks=" << blocks.size()
                  << (identity != nullptr ? " shared" : "")
                  << (abandoned ? " abandoned" : accepted ? " accepted" : " rejected")
                  << '\n';
    }
    if (!accepted) {
        out.clear();
        origin.clear();
        verbatim.clear();
        renames.clear();
    }
    return finish(accepted);
}

std::string static_kernel_specializer_identity()
{
    // The specializer, the compiler that consumes its output, and the knobs
    // that change what it produces.
    std::string identity = "fsim-static-kernel-specializations-v1;" __DATE__ " " __TIME__;
    for (const char* name : { "FSIM_STATIC_KERNEL_SPECIALIZE",
             "FSIM_STATIC_KERNEL_SPECIALIZE_FACTOR" }) {
        const char* value = std::getenv(name);
        identity += ";";
        identity += name;
        identity += "=";
        identity += value != nullptr ? value : "-";
    }
    return identity;
}

} // namespace fsim::runtime::simir

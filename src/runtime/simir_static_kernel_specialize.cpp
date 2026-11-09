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

#include <algorithm>
#include <bit>
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
    || std::is_same_v<T, Report> || std::is_same_v<T, ContainerRead>
    || std::is_same_v<T, WriteContainerObjectElement> || std::is_same_v<T, Display>
    || std::is_same_v<T, FormatDisplay> || std::is_same_v<T, TimeDisplay>
    || std::is_same_v<T, SystemVerilogMath>
    || std::is_same_v<T, SystemVerilogScalarBinary>;

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
            for (auto& level : op.levels) {
                level.index = f(level.index);
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

} // namespace

namespace {

/// Applies f to the register operand operation_registers reports as written.
template <typename F>
void map_write(Operation& operation, F&& f)
{
    visit_operation([&](auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, LoadConstant> || std::is_same_v<T, ReadSignal>
            || std::is_same_v<T, CopyRegister> || std::is_same_v<T, UnaryNot>
            || std::is_same_v<T, ConvertToTwoState> || std::is_same_v<T, LogicalNot>
            || std::is_same_v<T, Reduction> || std::is_same_v<T, CountOnes>
            || std::is_same_v<T, CountBits> || std::is_same_v<T, Extract>
            || std::is_same_v<T, Binary> || std::is_same_v<T, LogicalBinary>
            || std::is_same_v<T, Shift> || std::is_same_v<T, DynamicExtract>
            || std::is_same_v<T, DynamicPartSelect> || std::is_same_v<T, Insert>
            || std::is_same_v<T, DynamicInsert> || std::is_same_v<T, DynamicPartInsert>
            || std::is_same_v<T, Concatenate> || std::is_same_v<T, ConditionalSelect>
            || std::is_same_v<T, PlusArgSelect> || std::is_same_v<T, SystemVerilogMath>
            || std::is_same_v<T, SystemVerilogScalarBinary>
            || std::is_same_v<T, RandomValue> || std::is_same_v<T, IntegerBinary>
            || std::is_same_v<T, IntegerUnary> || std::is_same_v<T, ContainerRead>) {
            op.destination = f(op.destination);
        }
    }, operation);
}

/// Union-find over the definitions of one register.
struct Webs {
    std::vector<std::uint32_t> parent;
    explicit Webs(const std::size_t count) : parent(count)
    {
        for (std::size_t index = 0U; index < count; ++index) {
            parent[index] = static_cast<std::uint32_t>(index);
        }
    }
    [[nodiscard]] std::uint32_t find(std::uint32_t node)
    {
        while (parent[node] != node) {
            parent[node] = parent[parent[node]];
            node = parent[node];
        }
        return node;
    }
    void join(const std::uint32_t left, const std::uint32_t right)
    {
        parent[find(left)] = find(right);
    }
};

} // namespace

// Width specialization of VHDL callees. A register that several call sites
// give values of different widths (the formals and result of a subprogram
// called with differently constrained arguments) has no static width, which
// the compiled tier needs. Each site gets its own copy of the callee's code
// (a clone appended to the member's operations; the calls inside it are
// cloned in turn, a recursive call stays as it is and keeps the member
// uncompiled), then every register with several clones touching it is split
// into its webs: the maximal sets of definitions and uses connected through
// reaching definitions. The copy at one site is reached by the setup
// operations of that site only, so its webs take that site's widths; the
// result reads after the return join the same web as the clone's definition.
// The process state that lives across activations is one web with the
// registers' initial values: the exit of the body flows to its entry, and the
// entry holds a definition for every register.
bool Interpreter::Impl::StaticKernel::split_polymorphic_calls(
    const std::uint32_t member_index)
{
    auto& member = members_[member_index];
    if (!member.vhdl || !member.dpi_site_at.empty()
        || member.kind == StaticKernelMemberKind::behavioral) {
        return false;
    }
    const auto old_size = static_cast<std::uint32_t>(member.operations().size());
    const auto entry = member.body_begin;
    const auto exit_pc = member.body_end;
    if (entry >= old_size || exit_pc >= old_size
        || !operation_holds<WaitSensitivity>(member.operations()[exit_pc])) {
        return false;
    }
    constexpr std::size_t growth_limit = 20'000U;
    constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
    std::vector<Operation> ops = member.operations();
    // Per operation: the clone it belongs to (0: the member's own), and the
    // operation of the original code it was copied from.
    std::vector<std::uint32_t> clone_of(old_size, 0U);
    std::vector<std::uint32_t> source_of(old_size);
    for (std::uint32_t pc = 0U; pc < old_size; ++pc) {
        source_of[pc] = pc;
    }
    // Per clone (index - 1): the operation its Return continues at.
    std::vector<std::uint32_t> return_to;
    std::vector<std::uint32_t> active;
    bool refused = false;
    // What an entry reaches without entering calls, up to its returns.
    const auto region_of = [&](const std::uint32_t from)
        -> std::optional<std::vector<std::uint32_t>> {
        std::vector<std::uint8_t> seen(old_size, 0U);
        std::vector<std::uint32_t> work { from };
        std::vector<std::uint32_t> region;
        while (!work.empty()) {
            const auto pc = work.back();
            work.pop_back();
            if (pc >= old_size || pc == exit_pc) {
                return std::nullopt;
            }
            if (seen[pc] != 0U) {
                continue;
            }
            seen[pc] = 1U;
            region.push_back(pc);
            bool bad = false;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    work.push_back(op.target);
                } else if constexpr (std::is_same_v<T, Branch>) {
                    work.push_back(op.when_true);
                    work.push_back(op.when_false);
                } else if constexpr (std::is_same_v<T, Call>) {
                    work.push_back(op.return_target);
                } else if constexpr (std::is_same_v<T, Return>) {
                } else if constexpr (std::is_same_v<T, Halt>
                    || std::is_same_v<T, WaitSensitivity>
                    || std::is_same_v<T, CallableFramePush>
                    || std::is_same_v<T, CallableFramePop>
                    || behavioral_suspension<T>) {
                    bad = true;
                } else {
                    work.push_back(pc + 1U);
                }
            }, ops[pc]);
            if (bad) {
                return std::nullopt;
            }
        }
        std::ranges::sort(region);
        return region;
    };
    // Appends a copy of the code of `from`, whose Return continues at
    // `continue_at`; the copy's own calls are cloned too.
    const auto clone = [&](const auto& self, const std::uint32_t from,
                           const std::uint32_t continue_at) -> std::uint32_t {
        const auto region = region_of(from);
        if (!region || std::ranges::find(active, from) != active.end()
            || ops.size() + region->size() > old_size + growth_limit) {
            refused = true;
            return from;
        }
        const auto id = static_cast<std::uint32_t>(return_to.size() + 1U);
        return_to.push_back(continue_at);
        const auto base = static_cast<std::uint32_t>(ops.size());
        std::unordered_map<std::uint32_t, std::uint32_t> position;
        for (std::size_t at = 0U; at < region->size(); ++at) {
            position.emplace((*region)[at], base + static_cast<std::uint32_t>(at));
        }
        for (const auto pc : *region) {
            ops.push_back(ops[pc]);
            clone_of.push_back(id);
            source_of.push_back(source_of[pc]);
        }
        const auto mapped = [&](const std::uint32_t target) {
            const auto found = position.find(target);
            if (found == position.end()) {
                refused = true;
                return base;
            }
            return found->second;
        };
        active.push_back(from);
        for (std::size_t at = 0U; at < region->size() && !refused; ++at) {
            const auto index = base + static_cast<std::uint32_t>(at);
            if (auto* jump = operation_get_if<Jump>(&ops[index])) {
                jump->target = mapped(jump->target);
            } else if (auto* branch = operation_get_if<Branch>(&ops[index])) {
                branch->when_true = mapped(branch->when_true);
                branch->when_false = mapped(branch->when_false);
            } else if (const auto* call = operation_get_if<Call>(&ops[index])) {
                const auto callee = call->target;
                const auto resume = mapped(call->return_target);
                const auto entered = self(self, callee, resume);
                auto* patched = operation_get_if<Call>(&ops[index]);
                patched->target = entered;
                patched->return_target = resume;
            }
        }
        active.pop_back();
        return position.at(from);
    };
    // The member's own code from its entry: patch each call to a clone.
    {
        std::vector<std::uint8_t> seen(old_size, 0U);
        std::vector<std::uint32_t> work { entry };
        while (!work.empty() && !refused) {
            const auto pc = work.back();
            work.pop_back();
            if (pc == exit_pc) {
                continue;
            }
            if (pc >= old_size) {
                return false;
            }
            if (seen[pc] != 0U) {
                continue;
            }
            seen[pc] = 1U;
            bool bad = false;
            if (const auto* call = operation_get_if<Call>(&ops[pc])) {
                const auto callee = call->target;
                const auto resume = call->return_target;
                work.push_back(resume);
                const auto entered = clone(clone, callee, resume);
                if (auto* patched = operation_get_if<Call>(&ops[pc])) {
                    patched->target = entered;
                }
                continue;
            }
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    work.push_back(op.target);
                } else if constexpr (std::is_same_v<T, Branch>) {
                    work.push_back(op.when_true);
                    work.push_back(op.when_false);
                } else if constexpr (std::is_same_v<T, Return>
                    || std::is_same_v<T, Halt>) {
                } else if constexpr (std::is_same_v<T, WaitSensitivity>
                    || std::is_same_v<T, CallableFramePush>
                    || std::is_same_v<T, CallableFramePop>
                    || behavioral_suspension<T>) {
                    bad = true;
                } else {
                    work.push_back(pc + 1U);
                }
            }, ops[pc]);
            if (bad) {
                return false;
            }
        }
    }
    if (refused || return_to.empty()) {
        return false;
    }
    // Control flow of the new code, from the entry. An edge to the exit
    // continues at the entry (the next activation).
    const auto size = static_cast<std::uint32_t>(ops.size());
    std::vector<std::vector<std::uint32_t>> succ(size);
    std::vector<std::uint8_t> reached(size, 0U);
    {
        const auto edge = [&](const std::uint32_t target) {
            return target == exit_pc ? entry : target;
        };
        std::vector<std::uint32_t> work { entry };
        while (!work.empty()) {
            const auto pc = work.back();
            work.pop_back();
            if (pc >= size) {
                return false;
            }
            if (reached[pc] != 0U) {
                continue;
            }
            reached[pc] = 1U;
            auto& next = succ[pc];
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    next.push_back(edge(op.target));
                } else if constexpr (std::is_same_v<T, Branch>) {
                    next.push_back(edge(op.when_true));
                    next.push_back(edge(op.when_false));
                } else if constexpr (std::is_same_v<T, Call>) {
                    next.push_back(op.target);
                } else if constexpr (std::is_same_v<T, Return>) {
                    if (clone_of[pc] != 0U) {
                        next.push_back(return_to[clone_of[pc] - 1U]);
                    }
                } else if constexpr (std::is_same_v<T, Halt>) {
                } else if constexpr (std::is_same_v<T, WaitSensitivity>) {
                    next.push_back(entry);
                } else {
                    next.push_back(edge(pc + 1U));
                }
            }, ops[pc]);
            for (const auto target : next) {
                work.push_back(target);
            }
        }
    }
    // Registers touched by several clones are the split candidates.
    const auto register_count = static_cast<std::uint32_t>(member.registers.size());
    std::vector<std::uint32_t> first_clone(register_count, none);
    std::vector<std::uint8_t> candidate(register_count, 0U);
    std::vector<RegisterId> reads;
    std::optional<RegisterId> write;
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        if (reached[pc] == 0U || clone_of[pc] == 0U) {
            continue;
        }
        operation_registers(ops[pc], reads, write);
        if (write) {
            reads.push_back(*write);
        }
        for (const auto reg : reads) {
            if (reg >= register_count) {
                return false;
            }
            if (first_clone[reg] == none) {
                first_clone[reg] = clone_of[pc];
            } else if (first_clone[reg] != clone_of[pc]) {
                candidate[reg] = 1U;
            }
        }
    }
    std::vector<std::vector<std::uint32_t>> preds(size);
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        for (const auto target : succ[pc]) {
            preds[target].push_back(pc);
        }
    }
    std::vector<std::uint32_t> order;
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        if (reached[pc] != 0U) {
            order.push_back(pc);
        }
    }
    std::uint32_t next_register = register_count;
    std::vector<ValueKind> new_kinds;
    bool any = false;
    for (RegisterId target = 0U; target < register_count; ++target) {
        if (candidate[target] == 0U) {
            continue;
        }
        std::vector<std::uint32_t> defs;
        std::vector<std::uint32_t> def_index(size, none);
        std::vector<std::uint32_t> users;
        for (const auto pc : order) {
            operation_registers(ops[pc], reads, write);
            if (write && *write == target) {
                def_index[pc] = static_cast<std::uint32_t>(defs.size());
                defs.push_back(pc);
            }
            if (std::ranges::find(reads, target) != reads.end()) {
                users.push_back(pc);
            }
        }
        // One bit per definition, then one for the entry state.
        const auto bits = defs.size() + 1U;
        const auto words = (bits + 63U) / 64U;
        if (defs.size() < 2U || words * size > 40'000'000U) {
            continue;
        }
        std::vector<std::uint64_t> in(static_cast<std::size_t>(size) * words, 0U);
        const auto at = [&](const std::uint32_t pc) {
            return in.data() + static_cast<std::size_t>(pc) * words;
        };
        at(entry)[defs.size() / 64U] |= std::uint64_t { 1 } << (defs.size() % 64U);
        std::vector<std::uint64_t> scratch(words);
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto pc : order) {
                std::ranges::fill(scratch, 0U);
                if (pc == entry) {
                    scratch[defs.size() / 64U]
                        |= std::uint64_t { 1 } << (defs.size() % 64U);
                }
                for (const auto before : preds[pc]) {
                    if (def_index[before] != none) {
                        const auto bit = def_index[before];
                        scratch[bit / 64U] |= std::uint64_t { 1 } << (bit % 64U);
                    } else {
                        const auto* previous = at(before);
                        for (std::size_t word = 0U; word < words; ++word) {
                            scratch[word] |= previous[word];
                        }
                    }
                }
                auto* current = at(pc);
                for (std::size_t word = 0U; word < words; ++word) {
                    if ((current[word] | scratch[word]) != current[word]) {
                        current[word] |= scratch[word];
                        changed = true;
                    }
                }
            }
        }
        Webs webs(bits);
        const auto first_of = [&](const std::uint32_t pc) -> std::uint32_t {
            const auto* row = at(pc);
            for (std::size_t word = 0U; word < words; ++word) {
                if (row[word] != 0U) {
                    return static_cast<std::uint32_t>(word * 64U
                        + static_cast<std::size_t>(std::countr_zero(row[word])));
                }
            }
            return none;
        };
        for (const auto pc : users) {
            const auto first = first_of(pc);
            if (first == none) {
                continue;
            }
            const auto* row = at(pc);
            for (std::size_t word = 0U; word < words; ++word) {
                for (auto bits_left = row[word]; bits_left != 0U;
                     bits_left &= bits_left - 1U) {
                    webs.join(first, static_cast<std::uint32_t>(word * 64U
                        + static_cast<std::size_t>(std::countr_zero(bits_left))));
                }
            }
        }
        // The web of the entry state keeps the register; others get new ones.
        std::unordered_map<std::uint32_t, RegisterId> named;
        const auto home = webs.find(static_cast<std::uint32_t>(defs.size()));
        named.emplace(home, target);
        const auto name_of = [&](const std::uint32_t node) {
            const auto root = webs.find(node);
            const auto [found, inserted] = named.try_emplace(root, next_register);
            if (inserted) {
                new_kinds.push_back(target < member.register_kinds.size()
                        ? member.register_kinds[target] : ValueKind::logic4);
                ++next_register;
            }
            return found->second;
        };
        const auto rename = [&](const std::uint32_t pc, const RegisterId read_as,
                                const std::optional<RegisterId> write_as) {
            std::vector<RegisterId> before_reads;
            std::optional<RegisterId> before_write;
            operation_registers(ops[pc], before_reads, before_write);
            map_reads(ops[pc], [&](const RegisterId reg) {
                return reg == target ? read_as : reg;
            });
            if (write_as) {
                map_write(ops[pc], [&](const RegisterId reg) {
                    return reg == target ? *write_as : reg;
                });
            }
            // Every operand must be one operation_registers reports.
            std::vector<RegisterId> after_reads;
            std::optional<RegisterId> after_write;
            operation_registers(ops[pc], after_reads, after_write);
            bool same = after_reads.size() == before_reads.size()
                && after_write.has_value() == before_write.has_value();
            for (std::size_t index = 0U; same && index < after_reads.size();
                 ++index) {
                same = after_reads[index]
                    == (before_reads[index] == target ? read_as : before_reads[index]);
            }
            if (same && before_write) {
                same = *after_write
                    == (*before_write == target && write_as ? *write_as : *before_write);
            }
            return same;
        };
        // Reads first (an operation can read and write the register).
        for (const auto pc : users) {
            const auto first = first_of(pc);
            if (first == none) {
                continue;
            }
            const auto name = name_of(first);
            if (name != target && !rename(pc, name, std::nullopt)) {
                return false;
            }
        }
        for (std::uint32_t index = 0U; index < defs.size(); ++index) {
            const auto name = name_of(index);
            if (name != target) {
                if (!rename(defs[index], target, name)) {
                    return false;
                }
                any = true;
            }
        }
        any = any || next_register != register_count;
    }
    if (!any) {
        return false;
    }
    // Commit.
    if (!member.origin.empty() && member.origin.size() == old_size) {
        const auto original = member.origin;
        member.origin.resize(size);
        for (std::uint32_t pc = old_size; pc < size; ++pc) {
            member.origin[pc] = original[source_of[pc]];
        }
    }
    if (!member.stripped_origin.empty() && member.stripped_origin.size() == old_size) {
        const auto original = member.stripped_origin;
        member.stripped_origin.resize(size);
        for (std::uint32_t pc = old_size; pc < size; ++pc) {
            member.stripped_origin[pc] = original[source_of[pc]];
        }
    }
    member.operations() = std::move(ops);
    member.registers.resize(next_register, PackedLogic4 { });
    if (!member.register_kinds.empty()) {
        member.register_kinds.resize(register_count, ValueKind::logic4);
        member.register_kinds.insert(member.register_kinds.end(), new_kinds.begin(),
            new_kinds.end());
    }
    member.body_identity = nullptr;
    return true;
}

// Accesses through a family proxy (a net that several kernel-owned leaf
// nets make up, Family) as accesses of its leaves: a whole read of the proxy
// reads each leaf and concatenates the values, a write of the proxy or of a
// slice of it extracts the part of the value that lies in each leaf and
// writes it to the leaf (a whole leaf as a whole write, a part as a slice).
// The proxy has no storage of its own, so this is what an access means
// (IEEE 1800-2023 6.5, 10.10: a net made of nets is the nets). Each access
// is replaced by a jump to the expansion appended after the code, which
// jumps back, so no position of the member changes. family_widths_ are the
// register widths of the last compilation, and a read it serves in place
// (family_in_place_: only narrow selections use it) stays.
bool Interpreter::Impl::StaticKernel::expand_family_accesses(
    const std::uint32_t member_index)
{
    const auto& widths = family_widths_;
    auto& member = members_[member_index];
    if (!member.dpi_site_at.empty()) {
        return false;
    }
    // A VHDL or behavioral member compiles its whole stream, so the appended
    // operations need no exit between them and the body.
    const bool whole = member.vhdl
        || member.kind == StaticKernelMemberKind::behavioral;
    const auto old_size = static_cast<std::uint32_t>(member.operations().size());
    const auto register_count = static_cast<std::uint32_t>(member.registers.size());
    constexpr std::size_t growth_limit = 20'000U;
    // The leaves of a proxy from the most significant, when they tile it.
    const auto tiling = [&](const SignalId signal)
        -> std::optional<std::vector<FamilyLeaf>> {
        if (signal >= family_of_signal_.size()
            || family_of_signal_[signal] == no_slot) {
            return std::nullopt;
        }
        const auto& family = families_[family_of_signal_[signal]];
        auto leaves = family.leaves;
        std::ranges::sort(leaves, [](const FamilyLeaf& left, const FamilyLeaf& right) {
            return left.offset > right.offset;
        });
        std::uint32_t end = family.width;
        for (const auto& leaf : leaves) {
            if (leaf.width == 0U || leaf.offset + leaf.width != end
                || leaf.slot >= slots_.size()) {
                return std::nullopt;
            }
            end = leaf.offset;
        }
        if (end != 0U || leaves.empty() || leaves.size() > 512U) {
            return std::nullopt;
        }
        return leaves;
    };
    std::vector<Operation> ops = member.operations();
    std::vector<std::uint32_t> source_of(old_size);
    for (std::uint32_t pc = 0U; pc < old_size; ++pc) {
        source_of[pc] = pc;
    }
    // A member that is not compiled whole (a SystemVerilog one) compiles the
    // operations up to its exit (body_end) and then those appended after
    // old_size (compile_once, family_expansion_begin); the exit operation
    // between them ends the body, so one exists even where the member's
    // stream stops at body_end.
    if (!whole && member.family_expansion_begin == 0U
        && member.body_end >= old_size) {
        ops.emplace_back(Halt { });
        source_of.push_back(old_size - 1U);
    }
    std::vector<ValueKind> new_kinds;
    std::uint32_t next_register = register_count;
    const auto fresh = [&] {
        new_kinds.push_back(ValueKind::logic4);
        return next_register++;
    };
    bool any = false;
    std::vector<std::uint32_t> new_continuations;
    for (std::uint32_t pc = 0U; pc < old_size; ++pc) {
        if (ops.size() > old_size + growth_limit) {
            break;
        }
        std::vector<Operation> block;
        // Positions in `block` of the stores that continue a wide write.
        std::vector<std::uint32_t> block_continues;
        const auto read_wide = [&](const std::uint32_t source) {
            return source < widths.size() ? widths[source] : 0U;
        };
        const auto write_pieces = [&](const SignalId signal,
                                      const RegisterId source,
                                      const std::uint32_t offset,
                                      const std::uint32_t count,
                                      const auto& emit_whole, const auto& emit_part) {
            const auto leaves = tiling(signal);
            if (!leaves || count == 0U) {
                return false;
            }
            const auto& family = families_[family_of_signal_[signal]];
            if (offset > family.width || count > family.width - offset) {
                return false;
            }
            for (const auto& leaf : *leaves) {
                const auto low = std::max(leaf.offset, offset);
                const auto high = std::min(leaf.offset + leaf.width, offset + count);
                if (low >= high) {
                    continue;
                }
                const auto piece = fresh();
                block.emplace_back(Extract { piece, source, low - offset, high - low });
                const auto leaf_signal = slots_[leaf.slot].signal;
                if (low == leaf.offset && high - low == leaf.width) {
                    block.push_back(emit_whole(leaf_signal, piece));
                } else {
                    block.push_back(emit_part(leaf_signal, piece, low - leaf.offset));
                }
            }
            return !block.empty();
        };
        // Writes through a dynamic index or part select (IEEE 1800-2023
        // 11.5.1) whose value is wider than 64 bits, or whose target is a
        // family proxy. A wide value is written in pieces of 64 bits, each a
        // narrow write whose offset is that of the piece (base_offset + 64 j;
        // a part select's base register adds the same distance). A proxy's
        // write goes to the leaves, each with the selection restricted to
        // the indices whose bits lie in the leaf and counted from the leaf's
        // first bit (index(q) = right + s (q - q_r): the offset of an index
        // is base_offset + |index - right|, linear in the index over the
        // declared range, so a leaf takes the sub-range whose offsets are in
        // [0, width - element] with its nearest end as `right`).
        const auto index_of = [](const DynamicIndex& selection, const std::int64_t q,
                                 const std::int64_t q_right) {
            const std::int64_t direction = selection.left >= selection.right ? 1 : -1;
            return selection.right + direction * (q - q_right);
        };
        // Calls emit(leaf signal, selection) for each leaf the write reaches;
        // false when an element could straddle two leaves (or there are too
        // many positions to check), which no per-leaf selection expresses.
        const auto leaf_pieces = [&](const std::vector<FamilyLeaf>& leaves,
                                     const DynamicIndex& selection,
                                     const std::int64_t element, const bool check_straddle,
                                     const auto& emit) {
            const auto lower = std::min(selection.left, selection.right);
            const auto upper = std::max(selection.left, selection.right);
            const std::int64_t span = upper - lower;
            if (span > 65'536) {
                return false;
            }
            if (check_straddle) {
                for (std::int64_t k = 0; k <= span; ++k) {
                    const std::int64_t position = selection.base_offset + k;
                    const bool inside = std::ranges::any_of(leaves, [&](const FamilyLeaf& leaf) {
                        return position >= leaf.offset
                            && position + element <= static_cast<std::int64_t>(leaf.offset)
                                + leaf.width;
                    });
                    if (!inside) {
                        return false;
                    }
                }
            }
            for (const auto& leaf : leaves) {
                const std::int64_t q_right = static_cast<std::int64_t>(selection.base_offset)
                    - leaf.offset;
                const std::int64_t q_left = q_right + span;
                const auto q_a = std::max<std::int64_t>(0, q_right);
                const auto q_b = std::min<std::int64_t>(
                    static_cast<std::int64_t>(leaf.width) - element, q_left);
                if (q_a > q_b) {
                    continue;
                }
                auto leaf_selection = selection;
                leaf_selection.right = index_of(selection, q_a, q_right);
                leaf_selection.left = index_of(selection, q_b, q_right);
                leaf_selection.base_offset = static_cast<std::uint32_t>(q_a);
                leaf_selection.strict = false;
                emit(slots_[leaf.slot].signal, leaf_selection);
            }
            return true;
        };
        // A host signal takes a wide write whole: the host applies it as one
        // update event (IEEE 1800-2023 4.4), where pieces would be one event
        // and one reported change each.
        const auto kernel_owned = [&](const SignalId signal) {
            return signal < slot_of_signal_.size() && slot_of_signal_[signal] != no_slot;
        };
        const auto index_write = [&](const SignalId signal, const RegisterId source,
                                     const DynamicIndex& selection, const auto& make) {
            const auto bits = read_wide(source);
            const bool proxy = signal < family_of_signal_.size()
                && family_of_signal_[signal] != no_slot;
            if (bits == 0U || (!proxy && (bits <= 64U || !kernel_owned(signal)))) {
                return false;
            }
            std::optional<std::vector<FamilyLeaf>> leaves;
            if (proxy) {
                leaves = tiling(signal);
                if (!leaves || selection.strict) {
                    return false;
                }
            }
            std::vector<Operation> pieces;
            std::vector<std::uint32_t> continues;
            const auto kinds_before = new_kinds;
            const auto registers_before = next_register;
            for (std::uint32_t done = 0U; done < bits; done += 64U) {
                const auto count = std::min(64U, bits - done);
                auto piece_selection = selection;
                piece_selection.base_offset = selection.base_offset + done;
                RegisterId piece = source;
                if (bits > 64U) {
                    piece = fresh();
                    pieces.emplace_back(Extract { piece, source, done, count });
                }
                if (!proxy) {
                    if (done != 0U) {
                        continues.push_back(static_cast<std::uint32_t>(pieces.size()));
                    }
                    pieces.push_back(make(signal, piece, piece_selection));
                    continue;
                }
                if (!leaf_pieces(*leaves, piece_selection, count, true,
                        [&](const SignalId leaf_signal, const DynamicIndex& leaf_selection) {
                            pieces.push_back(make(leaf_signal, piece, leaf_selection));
                        })) {
                    new_kinds = kinds_before;
                    next_register = registers_before;
                    return false;
                }
            }
            block = std::move(pieces);
            block_continues = std::move(continues);
            return true;
        };
        // A part select's first bit lies at base_offset + |... - right| from
        // selected_right = base + off(width - 1) (kernel_word::
        // dynamic_part_write), so the piece of width w at bit 64 j has the
        // base register `base + off(W - 1) - off(w - 1) +- 64 j`.
        const auto part_offset = [](const DynamicPartIndex& selection, const std::int64_t edge) {
            return selection.increasing
                ? (selection.source_descending ? std::int64_t { 0 } : edge)
                : (selection.source_descending ? -edge : std::int64_t { 0 });
        };
        const auto part_write = [&](const SignalId signal, const RegisterId source,
                                    const DynamicPartIndex& selection, const auto& make) {
            const auto bits = read_wide(source);
            const bool proxy = signal < family_of_signal_.size()
                && family_of_signal_[signal] != no_slot;
            if (bits == 0U || bits != selection.width
                || (!proxy && (bits <= 64U || !kernel_owned(signal)))) {
                return false;
            }
            std::optional<std::vector<FamilyLeaf>> leaves;
            if (proxy) {
                leaves = tiling(signal);
                if (!leaves) {
                    return false;
                }
            }
            std::vector<Operation> pieces;
            std::vector<std::uint32_t> continues;
            const auto kinds_before = new_kinds;
            const auto registers_before = next_register;
            for (std::uint32_t done = 0U; done < bits; done += 64U) {
                const auto count = std::min(64U, bits - done);
                auto piece_selection = selection;
                piece_selection.width = count;
                RegisterId piece = source;
                if (bits > 64U) {
                    piece = fresh();
                    pieces.emplace_back(Extract { piece, source, done, count });
                    // The distance from the whole part's first bit to the piece's.
                    const std::int64_t delta = part_offset(selection, bits - 1U)
                        - part_offset(selection, count - 1U)
                        + (selection.source_descending ? static_cast<std::int64_t>(done)
                                                       : -static_cast<std::int64_t>(done));
                    const auto distance = fresh();
                    pieces.emplace_back(LoadConstant { distance,
                        PackedLogic4::from_aval_bval(32U,
                            static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)), 0U) });
                    piece_selection.base = fresh();
                    if (member.vhdl) {
                        pieces.emplace_back(IntegerBinary { IntegerBinaryOperator::add,
                            piece_selection.base, selection.base, distance });
                    } else {
                        pieces.emplace_back(Binary { BinaryOperator::add_unsigned,
                            piece_selection.base, selection.base, distance });
                    }
                }
                if (!proxy) {
                    if (done != 0U) {
                        continues.push_back(static_cast<std::uint32_t>(pieces.size()));
                    }
                    pieces.push_back(make(signal, piece, piece_selection));
                    continue;
                }
                // Each selected bit lies in one leaf: a leaf takes the indices
                // whose offsets are in [0, width) (element width 1).
                DynamicIndex indices;
                indices.left = piece_selection.left;
                indices.right = piece_selection.right;
                indices.base_offset = piece_selection.base_offset;
                if (!leaf_pieces(*leaves, indices, 1, false,
                        [&](const SignalId leaf_signal, const DynamicIndex& leaf_indices) {
                            auto leaf_selection = piece_selection;
                            leaf_selection.left = leaf_indices.left;
                            leaf_selection.right = leaf_indices.right;
                            leaf_selection.base_offset = leaf_indices.base_offset;
                            pieces.push_back(make(leaf_signal, piece, leaf_selection));
                        })) {
                    new_kinds = kinds_before;
                    next_register = registers_before;
                    return false;
                }
            }
            block = std::move(pieces);
            block_continues = std::move(continues);
            return true;
        };
        bool expanded = false;
        if (const auto* read = operation_get_if<ReadSignal>(&ops[pc])) {
            if (read->kind == SignalReadKind::current && read->ticks == 1U
                && !read->clock && !read->gate
                && !(read->destination < family_in_place_.size()
                    && family_in_place_[read->destination] != 0U)) {
                if (const auto leaves = tiling(read->signal)) {
                    ReadSignal leaf_read;
                    Concatenate join;
                    join.destination = read->destination;
                    join.width = families_[family_of_signal_[read->signal]].width;
                    for (const auto& leaf : *leaves) {
                        leaf_read.destination = fresh();
                        leaf_read.signal = slots_[leaf.slot].signal;
                        block.emplace_back(leaf_read);
                        join.operands.push_back(leaf_read.destination);
                    }
                    block.emplace_back(std::move(join));
                    expanded = true;
                }
            }
        } else if (const auto* w1 = operation_get_if<WriteBlocking>(&ops[pc])) {
            const auto count = read_wide(w1->source);
            expanded = family_of_signal_[w1->signal] != no_slot
                && write_pieces(w1->signal, w1->source, 0U, count,
                    [](const SignalId signal, const RegisterId piece) {
                        return Operation { WriteBlocking { signal, piece } };
                    },
                    [](const SignalId signal, const RegisterId piece,
                        const std::uint32_t at) {
                        return Operation { WriteBlockingSlice { signal, piece, at } };
                    });
        } else if (const auto* w2 = operation_get_if<WriteBlockingSlice>(&ops[pc])) {
            const auto count = read_wide(w2->source);
            expanded = family_of_signal_[w2->signal] != no_slot
                && write_pieces(w2->signal, w2->source, w2->offset, count,
                    [](const SignalId signal, const RegisterId piece) {
                        return Operation { WriteBlocking { signal, piece } };
                    },
                    [](const SignalId signal, const RegisterId piece,
                        const std::uint32_t at) {
                        return Operation { WriteBlockingSlice { signal, piece, at } };
                    });
        } else if (const auto* w3 = operation_get_if<WriteUpdate>(&ops[pc])) {
            const auto domain = w3->domain;
            const auto count = read_wide(w3->source);
            expanded = family_of_signal_[w3->signal] != no_slot
                && write_pieces(w3->signal, w3->source, 0U, count,
                    [domain](const SignalId signal, const RegisterId piece) {
                        return Operation { WriteUpdate { signal, piece, domain } };
                    },
                    [domain](const SignalId signal, const RegisterId piece,
                        const std::uint32_t at) {
                        return Operation { WriteUpdateSlice { signal, piece, at, domain } };
                    });
        } else if (const auto* w4 = operation_get_if<WriteUpdateSlice>(&ops[pc])) {
            const auto domain = w4->domain;
            const auto count = read_wide(w4->source);
            expanded = family_of_signal_[w4->signal] != no_slot
                && write_pieces(w4->signal, w4->source, w4->offset, count,
                    [domain](const SignalId signal, const RegisterId piece) {
                        return Operation { WriteUpdate { signal, piece, domain } };
                    },
                    [domain](const SignalId signal, const RegisterId piece,
                        const std::uint32_t at) {
                        return Operation { WriteUpdateSlice { signal, piece, at, domain } };
                    });
        } else if (const auto* d1 = operation_get_if<WriteBlockingDynamicSlice>(&ops[pc])) {
            expanded = index_write(d1->signal, d1->source, d1->selection,
                [](const SignalId signal, const RegisterId piece, const DynamicIndex& selection) {
                    return Operation { WriteBlockingDynamicSlice { signal, piece, selection } };
                });
        } else if (const auto* d2 = operation_get_if<WriteUpdateDynamicSlice>(&ops[pc])) {
            const auto domain = d2->domain;
            expanded = index_write(d2->signal, d2->source, d2->selection,
                [domain](const SignalId signal, const RegisterId piece,
                    const DynamicIndex& selection) {
                    return Operation { WriteUpdateDynamicSlice { signal, piece, selection,
                        domain } };
                });
        } else if (const auto* d3 = operation_get_if<WriteBlockingDynamicPartSlice>(&ops[pc])) {
            expanded = part_write(d3->signal, d3->source, d3->selection,
                [](const SignalId signal, const RegisterId piece,
                    const DynamicPartIndex& selection) {
                    return Operation { WriteBlockingDynamicPartSlice { signal, piece,
                        selection } };
                });
        } else if (const auto* d4 = operation_get_if<WriteUpdateDynamicPartSlice>(&ops[pc])) {
            const auto domain = d4->domain;
            expanded = part_write(d4->signal, d4->source, d4->selection,
                [domain](const SignalId signal, const RegisterId piece,
                    const DynamicPartIndex& selection) {
                    return Operation { WriteUpdateDynamicPartSlice { signal, piece,
                        selection, domain } };
                });
        } else if (const auto* d5 = operation_get_if<WriteProjectedDynamicSlice>(&ops[pc])) {
            const auto delay = d5->delay;
            const auto rejection = d5->rejection;
            const auto mode = d5->mode;
            expanded = index_write(d5->signal, d5->source, d5->selection,
                [=](const SignalId signal, const RegisterId piece,
                    const DynamicIndex& selection) {
                    return Operation { WriteProjectedDynamicSlice { signal, piece, selection,
                        delay, rejection, mode } };
                });
        }
        if (!expanded) {
            continue;
        }
        const auto base = static_cast<std::uint32_t>(ops.size());
        for (const auto at : block_continues) {
            new_continuations.push_back(base + at);
        }
        block.emplace_back(Jump { pc + 1U });
        ops[pc] = Jump { base };
        for (auto& added : block) {
            ops.push_back(std::move(added));
            source_of.push_back(pc);
        }
        any = true;
    }
    if (!any) {
        return false;
    }
    const auto size = static_cast<std::uint32_t>(ops.size());
    if (!member.origin.empty() && member.origin.size() == old_size) {
        const auto original = member.origin;
        member.origin.resize(size);
        for (std::uint32_t pc = old_size; pc < size; ++pc) {
            member.origin[pc] = original[source_of[pc]];
        }
    }
    if (!member.stripped_origin.empty() && member.stripped_origin.size() == old_size) {
        const auto original = member.stripped_origin;
        member.stripped_origin.resize(size);
        for (std::uint32_t pc = old_size; pc < size; ++pc) {
            member.stripped_origin[pc] = original[source_of[pc]];
        }
    }
    member.operations() = std::move(ops);
    member.piece_continuations.insert(member.piece_continuations.end(),
        new_continuations.begin(), new_continuations.end());
    std::ranges::sort(member.piece_continuations);
    member.registers.resize(next_register, PackedLogic4 { });
    if (!member.register_kinds.empty()) {
        member.register_kinds.resize(register_count, ValueKind::logic4);
        member.register_kinds.insert(member.register_kinds.end(), new_kinds.begin(),
            new_kinds.end());
    }
    member.body_identity = nullptr;
    if (!whole && member.family_expansion_begin == 0U) {
        member.family_expansion_begin = std::max(old_size, member.body_end + 1U);
    }
    return true;
}

bool Interpreter::Impl::StaticKernel::specialize_member(
    const std::uint32_t member_index)
{
    auto& member = members_[member_index];
    const auto size = static_cast<std::uint32_t>(member.operations().size());
    const auto entry = member.body_begin;
    const auto stop = member.body_end;
    if (!member.vhdl || member.kind == StaticKernelMemberKind::behavioral
        || entry >= size || stop >= size
        || member.exit_alt != std::numeric_limits<std::uint32_t>::max()
        || !operation_holds<WaitSensitivity>(member.operations()[stop])) {
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
            const auto& here = member.operations()[recipe.origin[index]];
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
            const auto here = operation_signal(member.operations()[pc]);
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
        member.operations().reserve(size + recipe.out.size());
        for (std::size_t index = 0U; index < recipe.out.size(); ++index) {
            if (recipe.verbatim[index] == 0U) {
                member.operations().push_back(recipe.out[index]);
                continue;
            }
            member.operations().push_back(member.operations()[recipe.origin[index]]);
            if (!recipe.renames[index].empty()) {
                apply_renames(member.operations().back(), recipe.renames[index]);
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
            reads_before += wide_read(member.operations()[pc]) ? 1U : 0U;
            if (const auto signal = operation_signal(member.operations()[pc])) {
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
            }, member.operations()[pc]);
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
            const InterpretingScope scope { *this,
                InterpretedCause::specialize };
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
            const auto& operation = member.operations()[pc];
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
                    if (std::ranges::find(state.stack, op.return_target)
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

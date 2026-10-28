// SPDX-License-Identifier: Apache-2.0
//
// Native units of the engine v4 static kernel: generated-code entry,
// runtime helpers, template binding and partitions.
#include "simir_static_kernel_compiled_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cxxabi.h>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;
using namespace static_kernel_compiled_detail;
namespace kw = kernel_word;

namespace {
constexpr std::string_view native_magic { "fsim-static-kernel-templates-v1" };
} // namespace

std::string static_kernel_canonical_identity()
{
    return std::string { native_magic } + ";" __DATE__ " " __TIME__ ";"
        + static_kernel_compiler_identity();
}

void Interpreter::Impl::StaticKernel::run_compiled(const std::uint32_t member_index,
    const std::uint32_t start)
{
    auto& member = members_[member_index];
    if (member.native.entry == nullptr
        && member.native.lazy_template != no_slot) {
        note_lazy_run(member.native.lazy_template);
    }
    // Kernel bytecode enters at the activation entry: a prologue runs on the
    // reference evaluator first.
    const auto bytecode = [&] {
        if (start == static_kernel_prologue_entry) {
            if (const auto stopped = run_operation_tier(member_index, 0U, false)) {
                // The whole first run (prologue, then the activation) in
                // operation-tier code.
                if (*stopped != member.body_end) {
                    run_generic_from(member_index, *stopped);
                }
                sync_compiled_registers(member_index);
                return;
            }
            std::uint32_t pc = 0U;
            {
                const InterpretingScope scope { *this, InterpretedCause::prologue };
                pc = run_generic_from(member_index, 0U, member.prologue_end);
            }
            sync_compiled_registers(member_index);
            if (pc == member.body_end) {
                return;
            }
            if (member.generic_mode) {
                const InterpretingScope scope { *this,
                    InterpretedCause::generic_mode };
                run_generic_from(member_index, pc);
                sync_compiled_registers(member_index);
                return;
            }
        }
        execute(*member.compiled, member_index);
    };
    if (member.native.entry != nullptr
        && (!member.native.two_state
            || (member.registers_known && !member.two_state_off))) {
        run_native(member.native, *member.compiled, member_index, false, start);
        return;
    }
    if (member.native.entry == nullptr || !member.native.two_state) {
        bytecode();
        return;
    }
    // A two-state member with X or 'U' values runs its full-semantics code
    // (built ahead of time, or compiled now).
    if (member.native.full_entry == nullptr) {
        compile_full_template(member.native.full_template);
    }
    if (member.native.full_entry != nullptr) {
        run_native(member.native, *member.compiled, member_index, true, start);
    } else {
        bytecode();
    }
    if (!member.two_state_off) {
        member.registers_known = registers_known(*member.compiled);
    } else if (++member.off_runs >= member.off_retry) {
        member.off_runs = 0U;
        member.off_retry = member.off_retry < (1U << 20U)
            ? member.off_retry * 2U : member.off_retry;
        member.registers_known = registers_known(*member.compiled);
        if (member.registers_known) {
            // Known values again: two-state code gets a fresh start.
            member.two_state_off = false;
            member.two_state_deopts = 0U;
            fast_runs_[member_index].two_state_runs = 0U;
            refresh_fast_run(member_index);
        }
    }
}

void Interpreter::Impl::StaticKernel::compile_full_template(
    const std::uint32_t full_template)
{
    if (full_template == no_slot) {
        return;
    }
    auto& full = full_templates_[full_template];
    if (full.runs != 0U) {
        return;
    }
    full.runs = 1U;
    const std::array batch { full.code };
    const auto entries = codegen_->compile(batch, native_helpers_);
    if (entries.empty() || entries.front() == nullptr) {
        return;
    }
    for (auto* unit : full.units) {
        unit->full_entry = entries.front();
    }
}

bool Interpreter::Impl::StaticKernel::registers_known(
    CompiledBody& body) noexcept
{
    // A register an activation writes before reading holds nothing for the
    // next one: clear its X plane and 'U' mask instead of checking them.
    const auto live = [&](const std::size_t reg) {
        return body.live_at_entry.empty()
            || (reg / 64U < body.live_at_entry.size()
                && (body.live_at_entry[reg / 64U] >> (reg % 64U) & 1U) != 0U);
    };
    auto registers = body.shadow_base != 0U
        ? std::min<std::size_t>(body.shadow_base, body.registers.size())
        : body.registers.size();
    // Wide registers' chunks (at the end of the file) are read with their X
    // planes, so two-state code may start with X in them.
    for (const auto entry : body.wide_base) {
        if (entry != 0U) {
            registers = std::min<std::size_t>(registers, entry & ~wide_unknown_flag);
        }
    }
    bool known = true;
    for (std::size_t reg = 0U; reg < registers; ++reg) {
        auto& word = body.registers[reg];
        const bool tracked = body.shadow_base != 0U
            && reg < body.tracked.size() && body.tracked[reg] != 0U;
        auto* unknown = tracked ? &body.registers[body.shadow_base + reg].a
                                : nullptr;
        if (word.b == 0U && (unknown == nullptr || *unknown == 0U)) {
            continue;
        }
        if (live(reg)) {
            known = false;
            continue;
        }
        word.b = 0U;
        if (unknown != nullptr) {
            *unknown = 0U;
        }
    }
    for (std::size_t reg = registers; reg < body.registers.size(); ++reg) {
        known = known && body.registers[reg].b == 0U;
    }
    return known;
}

void Interpreter::Impl::StaticKernel::note_lazy_run(
    const std::uint32_t lazy_template)
{
    // A template reaching the threshold is compiled before this run (by
    // default its first: compiled code never waits on the interpreter).
    auto& lazy = lazy_templates_[lazy_template];
    if (++lazy.runs == lazy_threshold_) {
        lazy_pending_.push_back(lazy_template);
        compile_lazy_templates();
    }
}

void Interpreter::Impl::StaticKernel::note_lazy_work(
    const std::uint32_t lazy_template, const std::size_t steps)
{
    auto& lazy = lazy_templates_[lazy_template];
    const std::uint64_t size = lazy.code.body->code.size();
    const auto threshold = size * (200U + size / 10U);
    if (lazy.work < threshold && lazy.work + steps >= threshold) {
        lazy_pending_.push_back(lazy_template);
        compile_lazy_templates();
    }
    lazy.work += steps;
}

void Interpreter::Impl::StaticKernel::compile_lazy_templates()
{
    std::vector<StaticKernelTemplate> batch;
    batch.reserve(lazy_pending_.size());
    for (const auto index : lazy_pending_) {
        batch.push_back(lazy_templates_[index].code);
    }
    const auto entries = codegen_->compile(batch, native_helpers_);
    for (std::size_t at = 0U; at < entries.size(); ++at) {
        if (entries[at] == nullptr) {
            continue;
        }
        for (auto* unit : lazy_templates_[lazy_pending_[at]].units) {
            unit->entry = entries[at];
            ++native_units_;
        }
    }
    lazy_pending_.clear();
}

void Interpreter::Impl::StaticKernel::run_native(NativeUnit& unit,
    CompiledBody& body, const std::uint32_t member, const bool full,
    const std::uint32_t start)
{
    if (profile_) {
        profile_native_calls_[4] += 1U;
    }
    if (profile_) {
        ++profile_template_runs_[unit.program];
    }
    StaticKernelNativeFrame frame;
    frame.arena = arena_.data();
    frame.bindings = unit.bindings.data();
    frame.kernel = this;
    frame.program = unit.program;
    frame.instance = &body;
    frame.writes = &write_queue();
    frame.containers = container_info_.data();
    frame.member = member;
    frame.position = 0U;
    frame.status = 0U;
    const bool behavioral = !body.resume_entries.empty();
    // Behavioral bodies start at a resume point and return 3 when they
    // reach a suspension, whose operation index comes back in `reserved`.
    frame.reserved = behavioral ? behavioral_entry_ : start;
    if (profile_) {
        profile_operations_ += body.code.size();
    }
    const bool two_state = unit.two_state && !full;
    scratch().two_state_running = two_state;
    const auto status = (full ? unit.full_entry : unit.entry)(
        &frame, body.registers.data());
    scratch().two_state_running = false;
    if (two_state) {
        ++fast_runs_[member].two_state_runs;
    }
    if (status != 0U || frame.status != 0U) {
        finish_native(unit, body, member, frame, status, full);
    }
}

namespace {

/// Step code for an operation stream: one step instruction per operation
/// (the helper family `aux`), then native control flow to its static
/// successors (pc + 1, jump and branch targets, call and return targets).
/// `stops(pc)` positions return to the runner. Any operation may be where a
/// run starts (a deoptimization or a resumed process continues anywhere).
template <typename Operations, typename Stops>
void build_step_code(CompiledBody& body, const Operations& operations,
    const std::uint32_t aux, const Stops& stops)
{
    const auto size = static_cast<std::uint32_t>(operations.size());
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        if (const auto* call = operation_get_if<Call>(&operations[pc])) {
            body.return_targets.push_back(call->return_target);
        }
    }
    std::ranges::sort(body.return_targets);
    body.return_targets.erase(std::unique(body.return_targets.begin(),
                                  body.return_targets.end()),
        body.return_targets.end());
    body.code.reserve(size);
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        KInst inst;
        inst.x = pc;
        if (stops(pc)) {
            inst.op = KOp::suspend;
            body.code.push_back(inst);
            continue;
        }
        const auto& operation = operations[pc];
        inst.op = KOp::step;
        inst.sub = static_cast<std::uint8_t>(operation_kind_ordinal(operation));
        inst.aux = aux;
        if (const auto* jump = operation_get_if<Jump>(&operation)) {
            inst.d = jump->target;
            inst.z = 1U;
        } else if (const auto* branch = operation_get_if<Branch>(&operation)) {
            inst.d = branch->when_true;
            inst.y = branch->when_false;
            inst.z = 2U;
        } else if (const auto* call = operation_get_if<Call>(&operation)) {
            inst.d = call->target;
            inst.z = 1U;
        } else if (operation_holds<Return>(operation)) {
            inst.offset = 1U;
        } else {
            inst.d = pc + 1U;
            inst.z = 1U;
        }
        body.code.push_back(inst);
    }
    body.resume_entries.resize(size);
    for (std::uint32_t pc = 0U; pc < size; ++pc) {
        body.resume_entries[pc] = pc;
    }
}

/// Content hash of step code (op, kind, successors and return targets).
std::uint64_t step_code_hash(const CompiledBody& body)
{
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&](const std::uint64_t value) {
        hash = (hash ^ value) * 1099511628211ULL;
    };
    for (const auto& inst : body.code) {
        mix(static_cast<std::uint64_t>(inst.op) | std::uint64_t { inst.sub } << 8U
            | std::uint64_t { inst.z } << 16U | std::uint64_t { inst.offset } << 24U
            | std::uint64_t { inst.aux } << 32U);
        mix(inst.d | std::uint64_t { inst.y } << 32U);
    }
    for (const auto target : body.return_targets) {
        mix(target);
    }
    return hash;
}

bool same_step_code(const CompiledBody& left, const CompiledBody& right)
{
    return left.return_targets == right.return_targets
        && std::ranges::equal(left.code, right.code, [](const KInst& a, const KInst& b) {
               return a.op == b.op && a.sub == b.sub && a.aux == b.aux && a.x == b.x
                   && a.d == b.d && a.y == b.y && a.z == b.z && a.offset == b.offset;
           });
}

/// Instructions one run of `body` executes, estimated: its size, plus each
/// loop body (a backward jump or branch) again per estimated iteration. A
/// loop's exit test comparing against a constant gives the trip count
/// (capped); otherwise eight iterations are assumed.
std::size_t estimated_run_size(const CompiledBody& body)
{
    constexpr std::uint64_t default_trips = 8U;
    constexpr std::uint64_t maximum_trips = 1024U;
    const auto size = body.code.size();
    std::uint64_t total = size;
    const auto constant_of = [&](const std::uint32_t reg, const std::size_t before)
        -> std::optional<std::uint64_t> {
        for (auto at = before; at-- > 0U;) {
            const auto& inst = body.code[at];
            if (inst.d == reg && inst.op == KOp::constant && inst.sub == 0U
                && inst.imm_b == 0U) {
                return inst.imm_a;
            }
            if (before - at > 8U) {
                break;
            }
        }
        return std::nullopt;
    };
    for (std::size_t at = 0U; at < size; ++at) {
        const auto& inst = body.code[at];
        std::uint32_t target = std::numeric_limits<std::uint32_t>::max();
        if (inst.op == KOp::jump && inst.d <= at) {
            target = inst.d;
        } else if (inst.op == KOp::branch) {
            target = std::min(inst.y <= at ? inst.y : target,
                inst.z <= at ? inst.z : target);
        }
        if (target == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        // The header's first branch tests a comparison with a constant.
        std::uint64_t trips = default_trips;
        for (auto scan = static_cast<std::size_t>(target);
             scan < std::min<std::size_t>(at, target + 16U); ++scan) {
            const auto& test = body.code[scan];
            if (test.op != KOp::branch) {
                continue;
            }
            for (auto define = scan; define-- > target;) {
                const auto& compare = body.code[define];
                if (compare.d != test.x || compare.op != KOp::binary) {
                    continue;
                }
                const auto bound = constant_of(compare.y, define);
                if (bound && *bound >= 2U) {
                    trips = std::min(*bound, maximum_trips);
                }
                break;
            }
            break;
        }
        total += (at - target + 1U) * (trips - 1U);
    }
    return static_cast<std::size_t>(std::min<std::uint64_t>(total,
        std::numeric_limits<std::uint32_t>::max()));
}

} // namespace

std::unique_ptr<Interpreter::Impl::StaticKernel::OperationTier>
Interpreter::Impl::StaticKernel::build_operation_tier(
    const std::uint32_t member_index, const bool thread) const
{
    // The member variant stops where the reference loop stops
    // (run_generic_from at the VHDL wait, run_generic outside the body); the
    // thread variant stops at the boundaries its helpers report.
    const auto& member = members_[member_index];
    const auto size = static_cast<std::uint32_t>(member.operations.size());
    auto tier = std::make_unique<OperationTier>();
    auto& body = tier->body;
    build_step_code(body, member.operations, thread ? 1U : 0U,
        [&](const std::uint32_t pc) {
            return !thread
                && (member.vhdl ? pc == member.body_end
                                : pc < member.body_begin || pc >= member.body_end);
        });
    if (!thread && native_prologue(member) && member.prologue_end != member.body_begin
        && member.prologue_end < size) {
        // A specialized member's first run leaves the prologue for the
        // specialized body (as its compiled code does).
        KInst jump;
        jump.op = KOp::jump;
        jump.d = member.body_begin;
        body.code[member.prologue_end] = jump;
    }
    return tier;
}

StaticKernelTemplate Interpreter::Impl::StaticKernel::operation_template(
    const OperationTier& tier) const
{
    StaticKernelTemplate code;
    code.body = &tier.body;
    return code;
}

Interpreter::Impl::StaticKernel::OperationTier*
Interpreter::Impl::StaticKernel::operation_tier(const std::uint32_t member_index,
    const bool thread)
{
    // Helpers are bound once build_native has run.
    if (codegen_ == nullptr || verify_ || native_helpers_.step_ops[0] == nullptr
        || members_[member_index].operations.empty()) {
        return nullptr;
    }
    const bool built = operation_tiers_.size() == members_.size()
        && operation_tiers_[member_index][thread ? 1U : 0U] != nullptr;
    auto& tier = operation_tier_slot(member_index, thread);
    if (!built && tier.entry == nullptr) {
        const std::array batch { operation_template(tier) };
        const auto entries = codegen_->compile(batch, native_helpers_);
        if (!entries.empty()) {
            tier.entry = entries.front();
        }
    }
    return tier.entry != nullptr ? &tier : nullptr;
}

Interpreter::Impl::StaticKernel::OperationTier&
Interpreter::Impl::StaticKernel::operation_tier_slot(const std::uint32_t member_index,
    const bool thread)
{
    if (operation_tiers_.size() != members_.size()) {
        operation_tiers_.resize(members_.size());
    }
    auto& slot = operation_tiers_[member_index][thread ? 1U : 0U];
    if (slot != nullptr) {
        return *slot;
    }
    const auto& member = members_[member_index];
    const auto key = std::tuple { member.body_identity, member.body_begin,
        member.body_end, member.prologue_end, thread };
    if (member.body_identity != nullptr) {
        if (const auto found = shared_operation_tiers_.find(key);
            found != shared_operation_tiers_.end()) {
            slot = found->second;
            return *slot;
        }
    }
    auto tier = build_operation_tier(member_index, thread);
    if (member.body_identity != nullptr) {
        slot = operation_tier_store_.emplace_back(std::move(tier)).get();
        shared_operation_tiers_.emplace(key, slot);
        return *slot;
    }
    // A specialized member: equal step code shares one tier.
    auto& candidates = operation_tiers_by_content_[step_code_hash(tier->body)];
    for (auto* candidate : candidates) {
        if (same_step_code(candidate->body, tier->body)) {
            slot = candidate;
            return *slot;
        }
    }
    slot = operation_tier_store_.emplace_back(std::move(tier)).get();
    candidates.push_back(slot);
    return *slot;
}

std::optional<std::uint32_t> Interpreter::Impl::StaticKernel::run_operation_tier(
    const std::uint32_t member_index, const std::uint32_t pc, const bool thread)
{
    auto* tier = operation_tier(member_index, thread);
    if (tier == nullptr) {
        return std::nullopt;
    }
    StaticKernelNativeFrame frame;
    frame.arena = arena_.data();
    frame.kernel = this;
    frame.program = &tier->body;
    frame.instance = &tier->body;
    frame.writes = &write_queue();
    frame.containers = container_info_.data();
    frame.member = member_index;
    frame.reserved = pc;
    // A step may run another member's tier (nested runs keep their counts).
    const auto steps = std::exchange(operation_tier_steps_, 0U);
    const auto status = tier->entry(&frame, nullptr);
    operation_tier_steps_ = steps;
    if (frame.status != 0U || status != 3U) {
        auto error = std::exchange(scratch().native_exception, nullptr);
        if (error) {
            std::rethrow_exception(error);
        }
        fail(member_index, pc, "static kernel operation tier failed");
    }
    return frame.reserved;
}

Interpreter::Impl::StaticKernel::OperationTier&
Interpreter::Impl::StaticKernel::hosted_tier_slot(const OperationList& operations)
{
    auto tier = std::make_unique<OperationTier>();
    build_step_code(tier->body, operations, 2U, [](const std::uint32_t) { return false; });
    auto& candidates = hosted_tiers_by_content_[step_code_hash(tier->body)];
    for (auto* candidate : candidates) {
        if (same_step_code(candidate->body, tier->body)) {
            return *candidate;
        }
    }
    auto* slot = hosted_tier_store_.emplace_back(std::move(tier)).get();
    candidates.push_back(slot);
    return *slot;
}

Interpreter::Impl::StaticKernel::OperationTier*
Interpreter::Impl::StaticKernel::hosted_tier(ProcessState& process,
    const OperationList& operations)
{
    // Helpers are bound once build_native has run.
    if (codegen_ == nullptr || verify_ || native_helpers_.hosted_step_ops[0] == nullptr
        || operations.size() == 0U) {
        return nullptr;
    }
    auto& cold = process.cold();
    if (cold.hosted_tier != nullptr && cold.hosted_tier_list == &operations
        && cold.hosted_tier_body == operations.body_identity()
        && cold.hosted_tier_size == operations.size()) {
        return static_cast<OperationTier*>(const_cast<void*>(cold.hosted_tier));
    }
    auto& tier = hosted_tier_slot(operations);
    if (tier.entry == nullptr) {
        const std::array batch { operation_template(tier) };
        const auto entries = codegen_->compile(batch, native_helpers_);
        if (!entries.empty()) {
            tier.entry = entries.front();
        }
    }
    if (tier.entry == nullptr) {
        return nullptr;
    }
    cold.hosted_tier = &tier;
    cold.hosted_tier_list = &operations;
    cold.hosted_tier_body = operations.body_identity();
    cold.hosted_tier_size = operations.size();
    return &tier;
}

void Interpreter::Impl::StaticKernel::build_hosted_tiers()
{
    // An ahead-of-time build compiles the hosted processes' tiers with the
    // rest of the image; the simulation finds them there.
    if (codegen_ == nullptr || !codegen_->ahead_of_time() || verify_
        || native_helpers_.hosted_step_ops[0] == nullptr) {
        return;
    }
    std::vector<StaticKernelTemplate> code;
    std::vector<OperationTier*> tier_of;
    std::unordered_set<OperationTier*> queued;
    for (ProcessId id = 0U; id < impl_.static_kernel_hosted_process.size(); ++id) {
        if (impl_.static_kernel_hosted_process[id] == 0U) {
            continue;
        }
        const auto* state = std::as_const(impl_.processes).full_state_if_present(id);
        if (state == nullptr) {
            continue;
        }
        const auto& operations = state->program().operations();
        if (operations.size() == 0U) {
            continue;
        }
        auto& tier = hosted_tier_slot(operations);
        if (tier.entry == nullptr && queued.insert(&tier).second) {
            code.push_back(operation_template(tier));
            tier_of.push_back(&tier);
        }
    }
    if (!code.empty()) {
        const auto compiled = codegen_->compile(code, native_helpers_);
        for (std::size_t at = 0U; at < compiled.size(); ++at) {
            tier_of[at]->entry = compiled[at];
        }
    }
}

bool Interpreter::Impl::StaticKernel::run_hosted_tier(ProcessState& process)
{
    const auto& operations = process.program().operations();
    if (process.pc >= operations.size()) {
        return false;
    }
    auto* tier = hosted_tier(process, operations);
    if (tier == nullptr) {
        return false;
    }
    StaticKernelNativeFrame frame;
    frame.arena = arena_.data();
    frame.kernel = this;
    frame.program = &tier->body;
    frame.instance = &process;
    frame.writes = &write_queue();
    frame.containers = container_info_.data();
    frame.member = process.id;
    // The process runs to its next suspension. A step that leaves the
    // operation's static successors (a disable, a fork redirect) re-enters
    // at the process's new position.
    while (!process.halted) {
        frame.status = 0U;
        frame.reserved = process.pc;
        const auto status = tier->entry(&frame, nullptr);
        if (frame.status != 0U || status != 3U) {
            auto error = std::exchange(scratch().native_exception, nullptr);
            if (error) {
                std::rethrow_exception(error);
            }
            impl_.fail(process, "static kernel hosted tier failed");
        }
        if (frame.reserved == operation_tier_stop) {
            return true;
        }
        if (frame.reserved >= operations.size()) {
            // The evaluator reports the position (Impl::execute).
            return false;
        }
        process.pc = frame.reserved;
    }
    return true;
}

void Interpreter::Impl::StaticKernel::refresh_fast_run(
    const std::uint32_t member_index)
{
    auto& fast = fast_runs_[member_index];
    const auto& member = members_[member_index];
    const auto& unit = member.native;
    const bool eligible = !trace_ && !profile_ && !verify_ && member.compiled
        && !member.fresh && !member.generic_mode
        && member.kind != StaticKernelMemberKind::behavioral
        && member.partition == no_slot && unit.entry != nullptr
        && member.compiled->resume_entries.empty()
        && (!unit.two_state
            || (member.registers_known && !member.two_state_off));
    fast.entry = eligible ? unit.entry : nullptr;
    fast.bindings = unit.bindings.data();
    fast.program = unit.program;
    fast.body = eligible ? const_cast<CompiledBody*>(&*member.compiled) : nullptr;
    fast.registers = fast.body != nullptr ? fast.body->registers.data() : nullptr;
    fast.two_state = unit.two_state;
}

void Interpreter::Impl::StaticKernel::finish_native(NativeUnit& unit,
    CompiledBody& body, const std::uint32_t member,
    StaticKernelNativeFrame& frame, std::uint32_t status, const bool full)
{
    const bool behavioral = !body.resume_entries.empty();
    const bool two_state = unit.two_state && !full;
    bool continued = false;
    if (two_state) {
        auto& state = members_[member];
        if (frame.status == 4U) {
            // An X or 'U' value: the full code continues at `reserved`.
            if (unit.full_entry == nullptr) {
                compile_full_template(unit.full_template);
            }
            if (unit.full_entry == nullptr) {
                fail(member, members_[member].body_begin,
                    "static kernel full code could not be compiled");
            }
            if (profile_) {
                // Where two-state code hands over (the instruction itself,
                // or the one after a guarded store).
                const auto* program = static_cast<const CompiledBody*>(unit.program);
                const auto at = frame.reserved;
                std::string key = "two-state hand-over at op=";
                key += at < program->code.size()
                    ? std::to_string(static_cast<int>(program->code[at].op)) : "end";
                if (at > 0U && at - 1U < program->code.size()) {
                    key += " after op="
                        + std::to_string(static_cast<int>(program->code[at - 1U].op));
                }
                ++profile_generic_ops_[key];
            }
            // Mostly X or 'U' inputs: two-state code only adds detours.
            if (++state.two_state_deopts >= 16U
                && 8U * state.two_state_deopts
                    >= fast_runs_[member].two_state_runs) {
                state.two_state_off = true;
            }
            frame.status = 0U;
            status = unit.full_entry(&frame, body.registers.data());
            continued = true;
        }
    }
    if (frame.status == 2U && scratch().pending_deopt) {
        if (behavioral) {
            return;
        }
        const auto deopt = *scratch().pending_deopt;
        scratch().pending_deopt.reset();
        handle_deopt(frame.member, body, deopt);
        if (unit.two_state) {
            members_[member].registers_known = registers_known(body);
        }
        return;
    }
    if (continued) {
        members_[member].registers_known = registers_known(body);
    }
    if (behavioral && status == 3U && frame.status == 0U) {
        behavioral_suspended_ = frame.reserved;
        behavioral_boundary_helper_ = frame.boundary;
        return;
    }
    if (status != 0U || frame.status != 0U) {
        auto error = std::exchange(scratch().native_exception, nullptr);
        if (error) {
            std::rethrow_exception(error);
        }
        fail(frame.member, members_[frame.member].body_begin,
            "static kernel native code failed");
    }
}

void Interpreter::Impl::StaticKernel::native_fail(
    StaticKernelNativeFrame* frame, const std::uint32_t at,
    const std::uint32_t reason)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    if (reason == 2U) {
        // An operand holds 'U' where compiled code needs a known kind.
        kernel.scratch().pending_deopt = KernelDeopt { at, false };
        frame->status = 2U;
        return;
    }
    try {
        kernel.fail(frame->member,
            (kernel.members_[frame->member].vhdl ? 0U : kernel.members_[frame->member].body_begin) + at,
            "branch condition is unknown or high impedance");
    } catch (...) {
        kernel.scratch().native_exception = std::current_exception();
        frame->status = 1U;
    }
}

void Interpreter::Impl::StaticKernel::build_native(StaticKernelCodegen& codegen)
{
    std::unordered_map<std::uint32_t, std::uint32_t> slot_of_offset;
    for (std::uint32_t slot = 0U; slot < slots_.size(); ++slot) {
        slot_of_offset.emplace(slots_[slot].offset, slot);
    }
    struct Unit {
        NativeUnit* native;
        const CompiledBody* body;
        /// VHDL bodies keep their constants (Logic9 and 'U' metadata).
        bool vhdl { };
    };
    std::vector<Unit> units;
    for (auto& partition_state : partitions_) {
        units.push_back({ &partition_state.native, &partition_state.program, false });
    }
    for (auto& member : members_) {
        if (member.compiled && member.partition == no_slot) {
            units.push_back({ &member.native, &*member.compiled, member.vhdl });
        }
    }
    // Canonical templates and per-unit bindings recorded ahead of time
    // (StaticKernelRuntimeSpec::native_image) replace steps 1 to 3.
    std::vector<std::uint32_t> template_of_unit(units.size(), no_slot);
    bool restored = false;
    if (restored_native_ && restored_native_->starts_with(native_magic)) {
        BodyReader reader { std::string_view { *restored_native_ }.substr(
            native_magic.size()) };
        std::uint64_t template_count { };
        std::uint64_t unit_count { };
        bool valid = reader.get(template_count) && template_count <= units.size();
        std::vector<std::unique_ptr<CompiledBody>> templates;
        if (valid) {
            templates.resize(static_cast<std::size_t>(template_count));
            for (auto& code : templates) {
                code = std::make_unique<CompiledBody>();
                if (!reader.body(*code)) {
                    valid = false;
                    break;
                }
            }
        }
        std::vector<std::vector<std::uint32_t>> bindings(units.size());
        valid = valid && reader.get(unit_count) && unit_count == units.size();
        for (std::size_t unit = 0U; valid && unit < units.size(); ++unit) {
            std::uint32_t index { };
            valid = reader.get(index) && index < templates.size()
                && templates[index]->code.size() == units[unit].body->code.size()
                && reader.integers(bindings[unit]);
            template_of_unit[unit] = index;
        }
        if (valid && reader.done()) {
            templates_ = std::move(templates);
            for (std::size_t unit = 0U; unit < units.size(); ++unit) {
                units[unit].native->bindings = std::move(bindings[unit]);
                units[unit].native->program = templates_[template_of_unit[unit]].get();
            }
            restored = true;
        } else {
            std::ranges::fill(template_of_unit, no_slot);
        }
    }
    if (!restored) {
        // 1. Canonical bodies: slots, host signals, memories and marks become
        // per-unit bindings.
        struct Canonical {
            std::unique_ptr<CompiledBody> body;
            std::vector<std::uint32_t> bindings;
            std::unordered_map<std::uint64_t, std::uint32_t> binding_of;
        };
        std::vector<Canonical> canonicals(units.size());
        const auto bind_in = [](Canonical& unit_state, const std::uint64_t kind,
                                 const std::uint32_t value, const std::uint32_t first,
                                 const std::uint32_t second) {
            const auto key = (kind << 40U) | value;
            const auto [it, inserted] = unit_state.binding_of.emplace(
                key, static_cast<std::uint32_t>(unit_state.bindings.size() / 2U));
            if (inserted) {
                unit_state.bindings.push_back(first);
                unit_state.bindings.push_back(second);
            }
            return it->second;
        };
        for (std::size_t unit = 0U; unit < units.size(); ++unit) {
            const auto& instance = *units[unit].body;
            auto& state = canonicals[unit];
            state.body = std::make_unique<CompiledBody>(instance);
            auto& canonical = state.body;
            const auto bind = [&](const std::uint64_t kind, const std::uint32_t value,
                                  const std::uint32_t first, const std::uint32_t second) {
                return bind_in(state, kind, value, first, second);
            };
            const auto bind_slot = [&](const std::uint32_t slot) {
                return bind(1U, slot, slots_[slot].offset,
                    slot | (slots_[slot].silent ? partition_tag : 0U));
            };
            for (auto& inst : canonical->code) {
                switch (inst.op) {
                case KOp::load_slot:
                    inst.x = bind_slot(slot_of_offset.at(inst.x));
                    break;
                case KOp::load_field:
                    inst.x = bind_slot(inst.x);
                    break;
                case KOp::dynamic_extract:
                case KOp::dynamic_part_select:
                    if (inst.sub == 1U) {
                        inst.x = bind_slot(inst.x);
                    }
                    break;
                case KOp::store_slot:
                    // A silent slot's store is plain in the template (aux 1):
                    // part of its shape, so units whose slot is not silent
                    // get their own template.
                    if (inst.sub == 0U && slots_[inst.d].silent) {
                        inst.aux = 1U;
                    }
                    inst.d = bind_slot(inst.d);
                    break;
                case KOp::store_slot_nba:
                case KOp::store_slot_dynamic:
                case KOp::store_slot_part:
                case KOp::store_vhdl:
                    inst.d = bind_slot(inst.d);
                    break;
                case KOp::wide_move:
                    inst.d = bind_slot(inst.d);
                    inst.x = bind_slot(inst.x);
                    break;
                case KOp::load_slot9:
                case KOp::load_field9:
                case KOp::wide_load:
                    inst.x = bind_slot(inst.x);
                    break;
                case KOp::wide_store:
                    inst.d = bind_slot(inst.d);
                    break;
                case KOp::load_host:
                    inst.x = bind(2U, inst.x, inst.x, 0U);
                    break;
                case KOp::store_host:
                    inst.d = bind(2U, inst.d, inst.d, 0U);
                    break;
                case KOp::mem_read:
                    inst.x = bind(3U, inst.x, inst.x, 0U);
                    break;
                case KOp::mem_write:
                    inst.d = bind(3U, inst.d, inst.d, 0U);
                    break;
                case KOp::mem_copy:
                    inst.d = bind(3U, inst.d, inst.d, 0U);
                    inst.x = bind(3U, inst.x, inst.x, 0U);
                    break;
                case KOp::mark:
                    inst.x = bind(4U, inst.x, inst.x, 0U);
                    break;
                default:
                    break;
                }
            }
        }
        // Units are grouped by shape (constants and field offsets may differ)
        // and then by their exact canonical body: a 64-bit hash of the fields
        // picks the bucket and an exact comparison of the same fields decides.
        const auto same_inst = [](const KInst& left, const KInst& right,
                                   const bool shape) {
            const bool field = shape && binding_offset_field(left);
            const bool open = shape && left.op == KOp::constant;
            return left.op == right.op && left.sub == right.sub
                && left.flags == right.flags && left.width == right.width
                && left.d == right.d && left.x == right.x && left.y == right.y
                && left.z == right.z && (field || left.offset == right.offset)
                && left.aux == right.aux
                && (open || (left.imm_a == right.imm_a && left.imm_b == right.imm_b));
        };
        const auto same_body = [&](const CompiledBody& left, const CompiledBody& right,
                                   const bool shape) {
            if (left.code.size() != right.code.size()
                || left.registers.size() != right.registers.size()
                || left.entry != right.entry || left.shadow_base != right.shadow_base
                || left.tracked != right.tracked || left.u_mode != right.u_mode
                || left.u_operands != right.u_operands
                || left.return_targets != right.return_targets
                || left.indices.size() != right.indices.size()
                || left.parts.size() != right.parts.size()
                || left.concat.size() != right.concat.size()
                || left.wide_base != right.wide_base
                || left.wide_constants.size() != right.wide_constants.size()) {
                return false;
            }
            for (std::size_t at = 0U; at < left.wide_constants.size(); ++at) {
                if (left.wide_constants[at].a != right.wide_constants[at].a
                    || left.wide_constants[at].b != right.wide_constants[at].b) {
                    return false;
                }
            }
            for (std::size_t at = 0U; at < left.code.size(); ++at) {
                if (!same_inst(left.code[at], right.code[at], shape)) {
                    return false;
                }
            }
            for (std::size_t at = 0U; at < left.indices.size(); ++at) {
                const auto& a = left.indices[at];
                const auto& b = right.indices[at];
                if (a.index != b.index || a.left != b.left || a.right != b.right
                    || a.base_offset != b.base_offset || a.strict != b.strict) {
                    return false;
                }
            }
            for (std::size_t at = 0U; at < left.parts.size(); ++at) {
                const auto& a = left.parts[at];
                const auto& b = right.parts[at];
                if (a.left != b.left || a.right != b.right || a.base != b.base
                    || a.base_offset != b.base_offset || a.width != b.width
                    || a.increasing != b.increasing
                    || a.source_descending != b.source_descending) {
                    return false;
                }
            }
            for (std::size_t at = 0U; at < left.concat.size(); ++at) {
                if (left.concat[at].reg != right.concat[at].reg
                    || left.concat[at].width != right.concat[at].width
                    || left.concat[at].unknown != right.concat[at].unknown) {
                    return false;
                }
            }
            return true;
        };
        const auto hash_body = [](const CompiledBody& body, const bool shape) {
            std::uint64_t hash = 0xcbf29ce484222325ULL;
            const auto mix = [&](const std::uint64_t value) {
                hash = (hash ^ value) * 0x100000001b3ULL;
                hash ^= hash >> 29U;
            };
            for (const auto& inst : body.code) {
                mix(static_cast<std::uint64_t>(inst.op)
                    | static_cast<std::uint64_t>(inst.sub) << 8U
                    | static_cast<std::uint64_t>(inst.flags) << 16U
                    | static_cast<std::uint64_t>(inst.width) << 32U);
                mix(inst.d | static_cast<std::uint64_t>(inst.x) << 32U);
                mix(inst.y | static_cast<std::uint64_t>(inst.z) << 32U);
                mix((shape && binding_offset_field(inst) ? 0U : inst.offset)
                    | static_cast<std::uint64_t>(inst.aux) << 32U);
                if (!(shape && inst.op == KOp::constant)) {
                    mix(inst.imm_a);
                    mix(inst.imm_b);
                }
            }
            mix(body.registers.size());
            mix(body.entry | static_cast<std::uint64_t>(body.shadow_base) << 32U);
            mix(body.u_operands.size() | body.indices.size() << 20U
                | body.parts.size() << 40U);
            mix(body.wide_base.size() | body.wide_constants.size() << 32U);
            return hash;
        };
        // 2. Units with the same shape share one template: the constants that
        // differ between them are loaded from their bindings (sub 1, x and y the
        // bindings of the aval and bval words as two 32-bit halves).
        {
            std::vector<std::vector<std::uint32_t>> shapes;
            std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> shapes_by_hash;
            for (std::size_t unit = 0U; unit < units.size(); ++unit) {
                const auto& body = *canonicals[unit].body;
                auto& bucket = shapes_by_hash[hash_body(body, true)];
                const auto found = std::ranges::find_if(bucket, [&](const std::uint32_t shape) {
                    return same_body(*canonicals[shapes[shape].front()].body, body, true);
                });
                if (found != bucket.end()) {
                    shapes[*found].push_back(static_cast<std::uint32_t>(unit));
                } else {
                    bucket.push_back(static_cast<std::uint32_t>(shapes.size()));
                    shapes.push_back({ static_cast<std::uint32_t>(unit) });
                }
            }
            for (const auto& group : shapes) {
                if (group.size() < 2U) {
                    continue;
                }
                const auto& first = canonicals[group.front()].body->code;
                for (std::uint32_t at = 0U; at < first.size(); ++at) {
                    if (binding_offset_field(first[at])) {
                        // Field reads (and VHDL signal assignments) at different
                        // bit offsets of the same-shaped slot take the offset
                        // from a binding: load_field sub 1 and y; load_field9
                        // and store_vhdl z, the binding plus one.
                        const bool offsets_differ = std::ranges::any_of(group,
                            [&](const std::uint32_t unit) {
                                return canonicals[unit].body->code[at].offset
                                    != first[at].offset;
                            });
                        if (offsets_differ) {
                            for (const auto unit : group) {
                                auto& state = canonicals[unit];
                                auto& inst = state.body->code[at];
                                const auto offset = bind_in(state, 7U, at, inst.offset, 0U);
                                if (inst.op == KOp::load_field) {
                                    inst.sub = 1U;
                                    inst.y = offset;
                                } else {
                                    inst.z = offset + 1U;
                                }
                                inst.offset = 0U;
                            }
                        }
                        continue;
                    }
                    if (first[at].op != KOp::constant) {
                        continue;
                    }
                    const bool differs = std::ranges::any_of(group,
                        [&](const std::uint32_t unit) {
                            const auto& inst = canonicals[unit].body->code[at];
                            return inst.imm_a != first[at].imm_a
                                || inst.imm_b != first[at].imm_b;
                        });
                    // A VHDL template runs two-state code only without X or 'U'
                    // literals (unknown_constant), so a literal that has X bits
                    // in any unit stays an immediate there.
                    const bool unknown = std::ranges::any_of(group,
                        [&](const std::uint32_t unit) {
                            return units[unit].vhdl
                                && canonicals[unit].body->code[at].imm_b != 0U;
                        });
                    if (!differs || unknown) {
                        continue;
                    }
                    for (const auto unit : group) {
                        auto& state = canonicals[unit];
                        auto& inst = state.body->code[at];
                        inst.sub = 1U;
                        inst.x = bind_in(state, 5U, at,
                            static_cast<std::uint32_t>(inst.imm_a),
                            static_cast<std::uint32_t>(inst.imm_a >> 32U));
                        inst.y = bind_in(state, 6U, at,
                            static_cast<std::uint32_t>(inst.imm_b),
                            static_cast<std::uint32_t>(inst.imm_b >> 32U));
                        inst.imm_a = 0U;
                        inst.imm_b = 0U;
                    }
                }
            }
        }
        // 3. Templates.
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> templates_by_hash;
        for (std::size_t unit = 0U; unit < units.size(); ++unit) {
            auto& state = canonicals[unit];
            auto& bucket = templates_by_hash[hash_body(*state.body, false)];
            const auto found = std::ranges::find_if(bucket, [&](const std::uint32_t index) {
                return same_body(*templates_[index], *state.body, false);
            });
            std::uint32_t index { };
            if (found != bucket.end()) {
                index = *found;
            } else {
                index = static_cast<std::uint32_t>(templates_.size());
                bucket.push_back(index);
                templates_.push_back(std::move(state.body));
            }
            template_of_unit[unit] = index;
            units[unit].native->bindings = std::move(state.bindings);
            units[unit].native->program = templates_[index].get();
        }
    }
    if (recorded_native_ && !restored) {
        recorded_native_->assign(native_magic);
        BodyWriter writer { *recorded_native_ };
        writer.put(templates_.size());
        for (const auto& code : templates_) {
            writer.body(*code);
        }
        writer.put(units.size());
        for (std::size_t unit = 0U; unit < units.size(); ++unit) {
            writer.put(template_of_unit[unit]);
            writer.integers(units[unit].native->bindings);
        }
    }
    std::vector<std::size_t> template_units(templates_.size(), 0U);
    // Partitions rerun whole whenever any of their inputs changes, so their
    // code runs far more often than a member's (which runs only when its
    // own inputs change); only heavy partition templates repay the
    // optimizing backend's compile time, heavy member templates get the
    // cheaper warm tier.
    std::vector<std::uint8_t> template_partition(templates_.size(), 0U);
    std::vector<std::uint8_t> template_vhdl(templates_.size(), 0U);
    for (std::size_t unit = 0U; unit < template_of_unit.size(); ++unit) {
        ++template_units[template_of_unit[unit]];
        template_vhdl[template_of_unit[unit]] |= units[unit].vhdl ? 1U : 0U;
        if (unit < partitions_.size()) {
            template_partition[template_of_unit[unit]] = 1U;
        }
    }
    std::vector<StaticKernelTemplate> templates;
    templates.reserve(templates_.size());
    const char* threshold_text = std::getenv("FSIM_STATIC_KERNEL_HOT_THRESHOLD");
    const std::size_t hot_threshold = threshold_text != nullptr
        ? static_cast<std::size_t>(std::strtoull(threshold_text, nullptr, 10))
        : 32768U;
    // The optimizing backend's cost grows faster than a template's size: a
    // loop template larger than this stays with the fast one
    // (FSIM_STATIC_KERNEL_HOT_LOOP_MAX_SIZE).
    const char* loop_max_text = std::getenv("FSIM_STATIC_KERNEL_HOT_LOOP_MAX_SIZE");
    const std::size_t hot_loop_max_size = loop_max_text != nullptr
        ? static_cast<std::size_t>(std::strtoull(loop_max_text, nullptr, 10))
        : 4096U;
    // VHDL templates run two-state code while their registers hold known
    // values (FSIM_STATIC_KERNEL_TWO_STATE=0 keeps the full code). A literal
    // with X or 'U' bits would leave it on every run.
    const auto unknown_constant = [](const CompiledBody& body) {
        return std::ranges::any_of(body.code, [](const KInst& inst) {
            return inst.op == KOp::constant && inst.sub == 0U
                && (inst.imm_b != 0U
                    || constant_unknown(inst.offset, inst.aux) != 0U);
        });
    };
    static const bool two_state = [] {
        const char* text = std::getenv("FSIM_STATIC_KERNEL_TWO_STATE");
        return text == nullptr || std::string_view { text } != "0";
    }();
    // FSIM_STATIC_KERNEL_TWO_STATE=2 also gives SystemVerilog templates
    // two-state code. It is off by default: SystemVerilog members commonly
    // hold X until reset, so most templates then also need their full code
    // (the throughput case compiled twice as much and ran slower).
    static const bool two_state_sv = [] {
        const char* text = std::getenv("FSIM_STATIC_KERNEL_TWO_STATE");
        return text != nullptr && std::string_view { text } == "2";
    }();
    for (std::size_t index = 0U; index < templates_.size(); ++index) {
        // Instances times size estimates how much run time a template
        // carries; only the heaviest get the slower optimizing backend.
        // Loops (their bodies counted per iteration) need it as partitions
        // do: the fast register allocator spills every live value around
        // each loop block.
        const auto size = templates_[index]->code.size();
        const auto run_size = estimated_run_size(*templates_[index]);
        const bool hot_loop = run_size >= 2U * size && size <= hot_loop_max_size
            && template_units[index] * run_size >= hot_threshold;
        templates.push_back({ templates_[index].get(),
            static_cast<std::uint8_t>(size < 64U ? 0U
                    : hot_loop || (template_partition[index] != 0U
                                      && template_units[index] * size >= hot_threshold)
                    ? 2U
                    : template_units[index] * size >= hot_threshold ? 1U
                                                                    : 0U),
            template_vhdl[index] != 0U,
            two_state
                && ((template_vhdl[index] != 0U
                        && templates_[index]->shadow_base != 0U)
                    || (two_state_sv && template_vhdl[index] == 0U))
                && templates_[index]->resume_entries.empty()
                && !unknown_constant(*templates_[index]) });
    }
    continuations_.assign(templates.size(), { });
    for (std::size_t index = 0U; index < templates.size(); ++index) {
        if (templates[index].two_state) {
            templates[index].resume_points = &continuations_[index];
        }
    }
    StaticKernelNativeHelpers helpers;
    set_instruction_helpers(helpers);
    helpers.notify = &StaticKernel::native_notify;
    helpers.notify_field = &StaticKernel::native_notify_field;
    helpers.binary = [](const std::uint64_t operation_width, const std::uint64_t la,
                         const std::uint64_t lb, const std::uint64_t ra,
                         const std::uint64_t rb, std::uint64_t* out) {
        const auto result = kw::binary(
            static_cast<BinaryOperator>(operation_width & 0xffU), { la, lb },
            { ra, rb }, static_cast<std::uint32_t>(operation_width >> 8U));
        out[0] = result.a;
        out[1] = result.b;
    };
    helpers.fail = &StaticKernel::native_fail;
    if (std::getenv("FSIM_STATIC_KERNEL_PERF_MAP") != nullptr) {
        std::vector<std::size_t> units_of(templates_.size(), 0U);
        std::vector<std::string> example(templates_.size());
        for (std::size_t unit = 0U; unit < units.size(); ++unit) {
            const auto index = template_of_unit[unit];
            if (units_of[index]++ == 0U) {
                for (std::uint32_t member = 0U; member < members_.size(); ++member) {
                    if (&members_[member].native == units[unit].native) {
                        example[index]
                            = impl_.processes.program_view(members_[member].process).name();
                    }
                }
            }
        }
        for (std::size_t index = 0U; index < templates_.size(); ++index) {
            std::cerr << "fsim-kernel-template: " << index << " units="
                      << units_of[index] << " insts=" << templates_[index]->code.size()
                      << " run=" << estimated_run_size(*templates_[index])
                      << " tier=" << static_cast<unsigned>(templates[index].tier)
                      << " example=" << example[index] << '\n';
        }
    }
    // Diagnostic: FSIM_KERNEL_DIFF_TEMPLATES=A,B lists the instructions in
    // which two templates differ (what kept them from sharing a shape).
    if (const char* pair = std::getenv("FSIM_KERNEL_DIFF_TEMPLATES")) {
        char* rest = nullptr;
        const auto left_index = std::strtoull(pair, &rest, 10);
        const auto right_index = rest != nullptr && *rest == ','
            ? std::strtoull(rest + 1, nullptr, 10) : left_index;
        if (left_index < templates_.size() && right_index < templates_.size()) {
            const auto& left = *templates_[left_index];
            const auto& right = *templates_[right_index];
            std::cerr << "fsim-kernel-diff: sizes " << left.code.size() << ' '
                      << right.code.size() << " registers " << left.registers.size()
                      << ' ' << right.registers.size() << " indices "
                      << left.indices.size() << ' ' << right.indices.size()
                      << " parts " << left.parts.size() << ' ' << right.parts.size()
                      << " wide " << left.wide_constants.size() << ' '
                      << right.wide_constants.size() << '\n';
            // The side tables a template's key also covers.
            const auto table = [&](const char* name, const auto& a, const auto& b) {
                if (a != b) {
                    std::cerr << "fsim-kernel-diff: " << name << " differ (sizes "
                              << a.size() << ' ' << b.size() << ")\n";
                }
            };
            table("register_widths", left.register_widths, right.register_widths);
            table("return_targets", left.return_targets, right.return_targets);
            table("tracked", left.tracked, right.tracked);
            table("u_mode", left.u_mode, right.u_mode);
            table("u_operands", left.u_operands, right.u_operands);
            table("wide_base", left.wide_base, right.wide_base);
            table("resume_entries", left.resume_entries, right.resume_entries);
            if (left.entry != right.entry || left.shadow_base != right.shadow_base
                || left.call_stack_base != right.call_stack_base) {
                std::cerr << "fsim-kernel-diff: entry/shadow/call-stack differ\n";
            }
            std::size_t shown = 0U;
            for (std::size_t at = 0U; at < std::min(left.code.size(), right.code.size())
                 && shown < 40U;
                 ++at) {
                const auto& x = left.code[at];
                const auto& y = right.code[at];
                if (x.op == y.op && x.sub == y.sub && x.flags == y.flags
                    && x.width == y.width && x.d == y.d && x.x == y.x && x.y == y.y
                    && x.z == y.z && x.offset == y.offset && x.aux == y.aux
                    && x.imm_a == y.imm_a && x.imm_b == y.imm_b) {
                    continue;
                }
                ++shown;
                std::cerr << "fsim-kernel-diff: at " << at << " op "
                          << static_cast<int>(x.op) << '/' << static_cast<int>(y.op)
                          << " sub " << int(x.sub) << '/' << int(y.sub) << " d " << x.d
                          << '/' << y.d << " x " << x.x << '/' << y.x << " y " << x.y
                          << '/' << y.y << " z " << x.z << '/' << y.z << " off "
                          << x.offset << '/' << y.offset << " w " << x.width << '/'
                          << y.width << " aux " << x.aux << '/' << y.aux << " imm "
                          << x.imm_a << '/' << y.imm_a << ' ' << x.imm_b << '/'
                          << y.imm_b << " flags " << int(x.flags) << '/'
                          << int(y.flags) << '\n';
            }
        }
    }
    // Cold member templates are compiled when they first run, so members
    // that never run cost nothing (FSIM_STATIC_KERNEL_LAZY_RUNS: the run
    // that compiles them; 0 compiles everything now).
    static const std::uint32_t lazy_threshold = [] {
        const char* text = std::getenv("FSIM_STATIC_KERNEL_LAZY_RUNS");
        return text != nullptr
            ? static_cast<std::uint32_t>(std::strtoul(text, nullptr, 10))
            : 1U;
    }();
    // An ahead-of-time build compiles every template now; code built ahead
    // of time is used from the start.
    const bool ahead_of_time = codegen.ahead_of_time();
    lazy_threshold_ = ahead_of_time ? 0U : lazy_threshold;
    native_helpers_ = helpers;
    const auto built = ahead_of_time
        ? std::vector<StaticKernelNativeEntry>(templates.size(), nullptr)
        : codegen.available(templates, helpers);
    std::vector<StaticKernelTemplate> eager;
    std::vector<std::size_t> eager_of;
    std::vector<std::uint32_t> lazy_of(templates.size(), no_slot);
    for (std::size_t index = 0U; index < templates.size(); ++index) {
        if (built[index] != nullptr) {
            continue;
        }
        // Behavioral templates (with resume entries) count interpreted
        // work instead of runs (note_lazy_work);
        // FSIM_STATIC_KERNEL_LAZY_BEHAVIORAL=0 compiles them now.
        static const bool lazy_behavioral = [] {
            const char* text = std::getenv("FSIM_STATIC_KERNEL_LAZY_BEHAVIORAL");
            return text == nullptr || std::string_view { text } != "0";
        }();
        if (lazy_threshold_ != 0U && templates[index].tier == 0U
            && template_partition[index] == 0U
            && (lazy_behavioral || templates_[index]->resume_entries.empty())) {
            lazy_of[index] = static_cast<std::uint32_t>(lazy_templates_.size());
            lazy_templates_.push_back({ templates[index], { }, 0U });
            continue;
        }
        eager_of.push_back(index);
        eager.push_back(templates[index]);
    }
    std::vector<StaticKernelNativeEntry> entries = built;
    if (!eager.empty()) {
        const auto compiled = codegen.compile(eager, helpers);
        for (std::size_t at = 0U; at < compiled.size(); ++at) {
            entries[eager_of[at]] = compiled[at];
        }
    }
    std::vector<std::uint32_t> full_of(templates.size(), no_slot);
    std::vector<StaticKernelTemplate> full_code;
    for (std::size_t index = 0U; index < templates.size(); ++index) {
        if (templates[index].two_state) {
            full_of[index] = static_cast<std::uint32_t>(full_templates_.size());
            auto code = templates[index];
            code.two_state = false;
            full_templates_.push_back({ code, { }, 0U });
            full_code.push_back(code);
        }
    }
    if (ahead_of_time && !full_code.empty()) {
        // Full code continues two-state code that leaves at one of its
        // resume points, known now that the two-state code is compiled.
        static_cast<void>(codegen.compile(full_code, helpers));
    }
    if (ahead_of_time) {
        // Members without compiled bodies run their operation tier; later
        // runs find it with the rest of the code built now. Instances of one
        // design unit share it. A compiled VHDL member continues in its tier
        // only after a deoptimization (Logic9 values its full code does not
        // represent), which is rare, so that tier is compiled when first
        // needed (operation_tier).
        std::vector<StaticKernelTemplate> tier_code;
        std::vector<OperationTier*> tier_of;
        std::unordered_set<OperationTier*> queued;
        for (std::uint32_t index = 0U; index < members_.size(); ++index) {
            const auto& member = members_[index];
            if (member.compiled || member.partition != no_slot
                || member.operations.empty()) {
                continue;
            }
            const bool thread = member.kind == StaticKernelMemberKind::behavioral;
            auto& tier = operation_tier_slot(index, thread);
            if (queued.insert(&tier).second) {
                tier_code.push_back(operation_template(tier));
                tier_of.push_back(&tier);
            }
        }
        if (!tier_code.empty()) {
            const auto compiled = codegen.compile(tier_code, helpers);
            for (std::size_t at = 0U; at < compiled.size(); ++at) {
                tier_of[at]->entry = compiled[at];
            }
        }
    }

    for (std::size_t unit = 0U; unit < units.size(); ++unit) {
        const auto index = template_of_unit[unit];
        units[unit].native->two_state = templates[index].two_state;
        if (full_of[index] != no_slot) {
            units[unit].native->full_template = full_of[index];
            full_templates_[full_of[index]].units.push_back(units[unit].native);
        }
        if (entries[index] != nullptr) {
            units[unit].native->entry = entries[index];
            ++native_units_;
        } else if (lazy_of[index] != no_slot) {
            units[unit].native->lazy_template = lazy_of[index];
            lazy_templates_[lazy_of[index]].units.push_back(units[unit].native);
        }
    }
}

void Interpreter::Impl::StaticKernel::run_partition(const std::uint32_t partition)
{
    auto& state = partitions_[partition];
    const auto operations_before = profile_operations_;
    if (profile_) {
        ++profile_partition_runs_;
        if (profile_partition_counts_.empty()) {
            profile_partition_counts_.assign(partitions_.size(), 0U);
            profile_partition_operations_.assign(partitions_.size(), 0U);
        }
        ++profile_partition_counts_[partition];
    }
    scratch().running_partition = partition;
    scratch().running_position = 0U;
    try {
        if (state.native.entry != nullptr) {
            run_native(state.native, state.program, state.members.front());
        } else {
            execute(state.program, state.members.front());
        }
    } catch (...) {
        scratch().running_partition = no_slot;
        throw;
    }
    scratch().running_partition = no_slot;
    if (profile_) {
        profile_partition_operations_[partition]
            += profile_operations_ - operations_before;
    }
}

void Interpreter::Impl::StaticKernel::forward_partition_stores(
    CompiledBody& program,
    const std::unordered_map<std::uint32_t, std::uint32_t>& slot_of_offset)
{
    // A partition runs its members in order, every pass from the start. A
    // load of a whole narrow slot that a branch-free member stored earlier
    // in the pass, with no other write to the slot or its register since,
    // reads the stored register instead of the arena (the store itself
    // stays). The optimizing backend then keeps the value in a machine
    // register; it cannot see through arena offsets, which are bindings.
    std::unordered_map<std::uint32_t, std::uint32_t> register_of_slot;
    std::unordered_map<std::uint32_t, std::uint32_t> slot_of_register;
    const auto forget_slot = [&](const std::uint32_t slot) {
        if (const auto found = register_of_slot.find(slot);
            found != register_of_slot.end()) {
            slot_of_register.erase(found->second);
            register_of_slot.erase(found);
        }
    };
    const auto forget_register = [&](const std::uint32_t reg) {
        if (const auto found = slot_of_register.find(reg);
            found != slot_of_register.end()) {
            register_of_slot.erase(found->second);
            slot_of_register.erase(found);
        }
    };
    const auto whole_narrow = [&](const std::uint32_t slot, const std::uint32_t width) {
        return slot < slots_.size() && slots_[slot].words == 1U
            && slots_[slot].planes == 2U && slots_[slot].width == width;
    };
    const std::vector<Operation> no_operations;
    std::vector<std::uint32_t> use;
    std::optional<std::uint32_t> def;
    std::vector<std::uint32_t> next;
    bool branching = false;
    const auto size = program.code.size();
    for (std::size_t at = 0U; at < size; ++at) {
        auto& inst = program.code[at];
        switch (inst.op) {
        case KOp::mark: {
            // A member whose stores may be skipped: its own stores forward
            // nothing.
            branching = false;
            for (auto scan = at + 1U; scan < size && program.code[scan].op != KOp::mark;
                 ++scan) {
                const auto op = program.code[scan].op;
                branching = branching || op == KOp::branch || op == KOp::jump;
            }
            continue;
        }
        case KOp::load_slot: {
            // In a member with branches a loop could reach the load again
            // after a later store to the slot.
            const auto slot = slot_of_offset.find(inst.x);
            // load_slot reads the whole slot (its width is not set).
            if (!branching && slot != slot_of_offset.end()
                && whole_narrow(slot->second, slots_[slot->second].width)) {
                if (const auto found = register_of_slot.find(slot->second);
                    found != register_of_slot.end() && found->second != inst.d) {
                    inst.op = KOp::copy;
                    inst.x = found->second;
                    ++forwarded_loads_;
                }
            }
            break;
        }
        case KOp::store_slot: {
            forget_slot(inst.d);
            if (!branching && (inst.flags & flag_linear) == 0U
                && whole_narrow(inst.d, inst.width)) {
                forget_register(inst.x);
                register_of_slot.emplace(inst.d, inst.x);
                slot_of_register.emplace(inst.x, inst.d);
            }
            continue;
        }
        case KOp::store_slot_nba:
            // Deferred: the slot keeps its value for the rest of the pass.
            continue;
        case KOp::store_slot_dynamic:
        case KOp::store_slot_part:
        case KOp::wide_move:
        case KOp::wide_store:
            forget_slot(inst.d);
            continue;
        case KOp::generic:
        case KOp::mem_write:
        case KOp::mem_copy:
        case KOp::mem_read:
            // May write any slot (or a memory a slot holds).
            register_of_slot.clear();
            slot_of_register.clear();
            continue;
        default:
            break;
        }
        kinst_flow(inst, static_cast<std::uint32_t>(at), program, no_operations,
            0U, use, def, next);
        if (def) {
            forget_register(*def);
        }
    }
}

NodeLevels static_kernel_detail::condensed_levels(
    const std::vector<std::vector<std::uint32_t>>& successors)
{
    // Tarjan's algorithm emits each component after every component it
    // reaches, so the emission order reversed is a topological order.
    constexpr auto unvisited = std::numeric_limits<std::uint32_t>::max();
    const auto node_total = static_cast<std::uint32_t>(successors.size());
    std::vector<std::uint32_t> order(node_total, unvisited);
    std::vector<std::uint32_t> low(node_total, 0U);
    std::vector<std::uint8_t> stacked(node_total, 0U);
    std::vector<std::uint32_t> stack;
    std::vector<std::uint32_t> component_of(node_total, 0U);
    std::vector<std::vector<std::uint32_t>> components;
    std::uint32_t counter = 0U;
    struct Frame {
        std::uint32_t node;
        std::size_t next;
    };
    std::vector<Frame> frames;
    for (std::uint32_t root = 0U; root < node_total; ++root) {
        if (order[root] != unvisited) {
            continue;
        }
        frames.push_back({ root, 0U });
        order[root] = low[root] = counter++;
        stack.push_back(root);
        stacked[root] = 1U;
        while (!frames.empty()) {
            auto& frame = frames.back();
            const auto node = frame.node;
            if (frame.next < successors[node].size()) {
                const auto successor = successors[node][frame.next++];
                if (order[successor] == unvisited) {
                    order[successor] = low[successor] = counter++;
                    stack.push_back(successor);
                    stacked[successor] = 1U;
                    frames.push_back({ successor, 0U });
                } else if (stacked[successor] != 0U) {
                    low[node] = std::min(low[node], order[successor]);
                }
                continue;
            }
            if (low[node] == order[node]) {
                const auto component = static_cast<std::uint32_t>(components.size());
                auto& members = components.emplace_back();
                for (;;) {
                    const auto top = stack.back();
                    stack.pop_back();
                    stacked[top] = 0U;
                    component_of[top] = component;
                    members.push_back(top);
                    if (top == node) {
                        break;
                    }
                }
                std::ranges::sort(members);
            }
            frames.pop_back();
            if (!frames.empty()) {
                low[frames.back().node] = std::min(low[frames.back().node], low[node]);
            }
        }
    }
    NodeLevels result;
    result.level.assign(node_total, 0U);
    std::vector<std::uint32_t> base(components.size(), 0U);
    for (auto component = components.size(); component-- > 0U;) {
        const auto& members = components[component];
        const auto first = base[component];
        const auto span = static_cast<std::uint32_t>(members.size());
        if (span > 1U) {
            result.cyclic += span;
        }
        for (std::uint32_t position = 0U; position < span; ++position) {
            result.level[members[position]] = first + position;
        }
        result.count = std::max(result.count, first + span);
        for (const auto member : members) {
            for (const auto successor : successors[member]) {
                if (const auto target = component_of[successor];
                    target != component) {
                    base[target] = std::max(base[target], first + span);
                }
            }
        }
    }
    return result;
}

void Interpreter::Impl::StaticKernel::build_partitions(
    const std::vector<std::vector<std::uint32_t>>& successors)
{
    // A combinational feedback cycle (a strongly connected component of
    // more than one member) with a member that assigns one signal more than
    // once per run is not partitioned. A partition runs its members in one
    // fixed order per pass and reruns when a member writes a signal an
    // earlier member reads; such a transient write (a_r = 0; ...; a_r = c in
    // one block) invalidates its own cycle on every pass and the partition
    // never settles. As separate levelized members they wake only on a net
    // value change and converge like any event-driven evaluation (IEEE
    // 1800-2023 4.7; 9.2.2.2.2 gives always_comb the same re-evaluation
    // rule). A cycle only at signal granularity (bit-sliced continuous
    // assignments to one vector) writes each signal once and stays
    // partitioned.
    std::vector<std::uint8_t> on_cycle(members_.size(), 0U);
    {
        constexpr auto unvisited = std::numeric_limits<std::uint32_t>::max();
        const auto node_total = static_cast<std::uint32_t>(members_.size());
        std::vector<std::uint32_t> order(node_total, unvisited);
        std::vector<std::uint32_t> low(node_total, 0U);
        std::vector<std::uint8_t> stacked(node_total, 0U);
        std::vector<std::uint32_t> stack;
        std::uint32_t counter = 0U;
        struct Frame {
            std::uint32_t node;
            std::size_t next;
        };
        for (std::uint32_t root = 0U; root < node_total; ++root) {
            if (order[root] != unvisited || successors[root].empty()) {
                continue;
            }
            std::vector<Frame> frames { { root, 0U } };
            order[root] = low[root] = counter++;
            stack.push_back(root);
            stacked[root] = 1U;
            while (!frames.empty()) {
                auto& frame = frames.back();
                const auto node = frame.node;
                if (frame.next < successors[node].size()) {
                    const auto successor = successors[node][frame.next++];
                    if (order[successor] == unvisited) {
                        order[successor] = low[successor] = counter++;
                        stack.push_back(successor);
                        stacked[successor] = 1U;
                        frames.push_back({ successor, 0U });
                    } else if (stacked[successor] != 0U) {
                        low[node] = std::min(low[node], order[successor]);
                    }
                    continue;
                }
                if (low[node] == order[node]) {
                    std::vector<std::uint32_t> component;
                    for (;;) {
                        const auto top = stack.back();
                        stack.pop_back();
                        stacked[top] = 0U;
                        component.push_back(top);
                        if (top == node) {
                            break;
                        }
                    }
                    if (component.size() > 1U
                        && std::ranges::any_of(component,
                            [&](const std::uint32_t member) {
                                return members_[member].rewrites_signal;
                            })) {
                        for (const auto member : component) {
                            on_cycle[member] = 1U;
                        }
                    }
                }
                frames.pop_back();
                if (!frames.empty()) {
                    low[frames.back().node]
                        = std::min(low[frames.back().node], low[node]);
                }
            }
        }
    }
    // A member with a loop in its body (a procedural for or while) stays
    // out: a partition pass runs every member of its component, and such a
    // member usually spans many otherwise separate cones (one per array
    // element it iterates over), joining them into one component.
    const auto loops = [](const Member& member) {
        for (auto pc = member.body_begin; pc < member.body_end; ++pc) {
            bool backward = false;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    backward = op.target <= pc;
                } else if constexpr (std::is_same_v<T, Branch>) {
                    backward = op.when_true <= pc || op.when_false <= pc;
                }
            }, member.operations[pc]);
            if (backward) {
                return true;
            }
        }
        return false;
    };
    // Group compiled combinational members by key.
    std::map<std::uint32_t, std::vector<std::uint32_t>> groups;
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        const auto& member = members_[index];
        if (member.kind == StaticKernelMemberKind::combinational && !member.vhdl
            && member.compiled && member.partition_key != no_slot
            && on_cycle[index] == 0U && !loops(member)) {
            groups[member.partition_key].push_back(index);
        }
    }
    // Split each group into its weakly connected components: a change then
    // runs only the cone it reaches. FSIM_STATIC_KERNEL_WHOLE_PARTITIONS
    // keeps one partition per key.
    std::vector<std::vector<std::uint32_t>> components;
    const bool split = std::getenv("FSIM_STATIC_KERNEL_WHOLE_PARTITIONS") == nullptr;
    // Position of a member within the group being processed (no_slot
    // outside it); reset after each group.
    std::vector<std::uint32_t> local(members_.size(), no_slot);
    const auto local_of = [&](const std::uint32_t member) {
        return member < local.size() ? local[member] : no_slot;
    };
    for (auto& [key, group] : groups) {
        (void)key;
        if (!split || group.size() < 2U) {
            components.push_back(std::move(group));
            continue;
        }
        for (std::uint32_t at = 0U; at < group.size(); ++at) {
            local[group[at]] = at;
        }
        std::vector<std::uint32_t> parent(group.size());
        for (std::uint32_t at = 0U; at < group.size(); ++at) {
            parent[at] = at;
        }
        const auto find = [&](std::uint32_t at) {
            while (parent[at] != at) {
                parent[at] = parent[parent[at]];
                at = parent[at];
            }
            return at;
        };
        for (std::uint32_t at = 0U; at < group.size(); ++at) {
            for (const auto successor : successors[group[at]]) {
                if (const auto found = local_of(successor); found != no_slot) {
                    const auto left = find(at);
                    const auto right = find(found);
                    if (left != right) {
                        parent[std::max(left, right)] = std::min(left, right);
                    }
                }
            }
        }
        for (const auto member : group) {
            local[member] = no_slot;
        }
        std::map<std::uint32_t, std::vector<std::uint32_t>> by_root;
        for (std::uint32_t at = 0U; at < group.size(); ++at) {
            by_root[find(at)].push_back(group[at]);
        }
        for (auto& [root, component] : by_root) {
            (void)root;
            components.push_back(std::move(component));
        }
    }
    for (auto& members : components) {
        if (members.size() < 2U) {
            continue;
        }
        // Partition-local topological order, ties in declaration order, so
        // instances of one module share one canonical program.
        {
            for (std::uint32_t at = 0U; at < members.size(); ++at) {
                local[members[at]] = at;
            }
            std::vector<std::uint32_t> indegree(members.size(), 0U);
            for (const auto member : members) {
                for (const auto successor : successors[member]) {
                    if (const auto found = local_of(successor);
                        found != no_slot && successor != member) {
                        ++indegree[found];
                    }
                }
            }
            std::priority_queue<std::uint32_t, std::vector<std::uint32_t>,
                std::greater<>> ready;
            for (std::uint32_t at = 0U; at < members.size(); ++at) {
                if (indegree[at] == 0U) {
                    ready.push(members[at]);
                }
            }
            std::vector<std::uint32_t> ordered;
            std::vector<std::uint8_t> done(members.size(), 0U);
            while (ordered.size() < members.size()) {
                if (ready.empty()) {
                    // A combinational loop: continue with the first pending
                    // member in declaration order.
                    for (std::uint32_t at = 0U; at < members.size(); ++at) {
                        if (done[at] == 0U) {
                            indegree[at] = 0U;
                            ready.push(members[at]);
                            break;
                        }
                    }
                }
                const auto member = ready.top();
                ready.pop();
                const auto at = local[member];
                if (done[at] != 0U) {
                    continue;
                }
                done[at] = 1U;
                ordered.push_back(member);
                for (const auto successor : successors[member]) {
                    if (const auto found = local_of(successor);
                        found != no_slot && done[found] == 0U
                        && successor != member && indegree[found] != 0U
                        && --indegree[found] == 0U) {
                        ready.push(successor);
                    }
                }
            }
            for (const auto member : members) {
                local[member] = no_slot;
            }
            members = std::move(ordered);
        }
        const auto partition = static_cast<std::uint32_t>(partitions_.size());
        Partition state;
        state.members = members;
        for (std::uint32_t position = 0U; position < members.size(); ++position) {
            members_[members[position]].partition = partition;
            members_[members[position]].position = position;
        }
        partitions_.push_back(std::move(state));
    }

    // Node levels over partitions and the remaining combinational members.
    const auto count = static_cast<std::uint32_t>(members_.size());
    const auto node_count = count + static_cast<std::uint32_t>(partitions_.size());
    const auto node_of = [&](const std::uint32_t member) {
        return members_[member].partition != no_slot
            ? count + members_[member].partition : member;
    };
    std::vector<std::vector<std::uint32_t>> node_successors(node_count);
    for (std::uint32_t member = 0U; member < count; ++member) {
        for (const auto successor : successors[member]) {
            const auto from = node_of(member);
            const auto to = node_of(successor);
            if (from != to) {
                node_successors[from].push_back(to);
            }
        }
    }
    for (auto& list : node_successors) {
        std::ranges::sort(list);
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    const auto node_level = static_kernel_detail::condensed_levels(
        node_successors).level;
    for (std::uint32_t member = 0U; member < count; ++member) {
        if (members_[member].partition == no_slot) {
            members_[member].level = node_level[member];
        }
    }

    // Concatenate member programs.
    // Slots by arena offset (load_slot names its slot by offset).
    std::unordered_map<std::uint32_t, std::uint32_t> slot_of_offset;
    for (std::uint32_t slot = 0U; slot < slots_.size(); ++slot) {
        slot_of_offset.emplace(slots_[slot].offset, slot);
    }
    for (std::uint32_t partition = 0U; partition < partitions_.size();
         ++partition) {
        auto& state = partitions_[partition];
        state.level = node_level[count + partition];
        auto& program = state.program;
        for (std::uint32_t position = 0U; position < state.members.size();
             ++position) {
            const auto index = state.members[position];
            const auto& body = *members_[index].compiled;
            const auto register_base
                = static_cast<std::uint32_t>(program.registers.size());
            const auto code_base = static_cast<std::uint32_t>(program.code.size())
                + 1U;
            const auto exit = code_base
                + static_cast<std::uint32_t>(body.code.size());
            const auto index_base
                = static_cast<std::uint32_t>(program.indices.size());
            const auto part_base = static_cast<std::uint32_t>(program.parts.size());
            const auto concat_base
                = static_cast<std::uint32_t>(program.concat.size());
            const auto constant_base
                = static_cast<std::uint32_t>(program.wide_constants.size());
            if (!body.wide_base.empty() || !program.wide_base.empty()) {
                program.wide_base.resize(register_base, 0U);
                program.wide_base.resize(register_base + body.registers.size(), 0U);
                for (std::size_t reg = 0U; reg < body.wide_base.size(); ++reg) {
                    if (const auto entry = body.wide_base[reg]; entry != 0U) {
                        program.wide_base[register_base + reg] = entry + register_base;
                    }
                }
            }
            program.wide_constants.insert(program.wide_constants.end(),
                body.wide_constants.begin(), body.wide_constants.end());
            program.registers.insert(program.registers.end(),
                body.registers.begin(), body.registers.end());
            if (!body.wide_registers.empty() || !program.wide_registers.empty()) {
                program.wide_registers.resize(register_base, PackedLogic4 { });
                if (body.wide_registers.empty()) {
                    program.wide_registers.resize(
                        register_base + body.registers.size(), PackedLogic4 { });
                } else {
                    program.wide_registers.insert(program.wide_registers.end(),
                        body.wide_registers.begin(), body.wide_registers.end());
                }
            }
            program.register_widths.insert(program.register_widths.end(),
                body.register_widths.begin(), body.register_widths.end());
            program.indices.insert(program.indices.end(), body.indices.begin(),
                body.indices.end());
            program.parts.insert(program.parts.end(), body.parts.begin(),
                body.parts.end());
            for (auto operand : body.concat) {
                operand.reg += register_base;
                program.concat.push_back(operand);
            }
            KInst mark;
            mark.op = KOp::mark;
            mark.d = position;
            mark.x = index;
            program.code.push_back(mark);
            for (auto inst : body.code) {
                const auto target = [&](std::uint32_t& value) {
                    value = value >= body.code.size() ? exit : code_base + value;
                };
                const auto reg = [&](std::uint32_t& value) {
                    value += register_base;
                };
                switch (inst.op) {
                case KOp::nop:
                case KOp::mem_bind:
                case KOp::mark:
                case KOp::suspend:
                case KOp::step:
                    break;
                case KOp::generic:
                    inst.y += register_base;
                    break;
                case KOp::constant:
                case KOp::load_slot:
                case KOp::load_host:
                    reg(inst.d);
                    break;
                case KOp::copy:
                case KOp::reduce:
                case KOp::unary_not:
                case KOp::logical_not:
                case KOp::two_state:
                case KOp::extract:
                    reg(inst.d);
                    reg(inst.x);
                    break;
                case KOp::binary:
                case KOp::logical_binary:
                case KOp::shift:
                case KOp::insert:
                    reg(inst.d);
                    reg(inst.x);
                    reg(inst.y);
                    break;
                case KOp::concat:
                    reg(inst.d);
                    inst.aux += concat_base;
                    break;
                case KOp::conditional:
                case KOp::dynamic_insert:
                    reg(inst.d);
                    reg(inst.x);
                    reg(inst.y);
                    reg(inst.z);
                    if (inst.op == KOp::dynamic_insert) {
                        inst.aux += index_base;
                    }
                    break;
                case KOp::dynamic_part_insert:
                    reg(inst.d);
                    reg(inst.x);
                    reg(inst.y);
                    reg(inst.z);
                    inst.aux += part_base;
                    break;
                case KOp::dynamic_extract:
                    reg(inst.d);
                    if (inst.sub != 1U) {
                        reg(inst.x);
                    }
                    reg(inst.y);
                    inst.aux += index_base;
                    break;
                case KOp::dynamic_part_select:
                    reg(inst.d);
                    if (inst.sub != 1U) {
                        reg(inst.x);
                    }
                    reg(inst.y);
                    inst.aux += part_base;
                    break;
                case KOp::load_field:
                    reg(inst.d);
                    break;
                case KOp::jump:
                    target(inst.d);
                    break;
                case KOp::branch:
                    reg(inst.x);
                    target(inst.y);
                    target(inst.z);
                    break;
                case KOp::store_slot:
                case KOp::store_slot_nba:
                case KOp::store_host:
                    reg(inst.x);
                    break;
                case KOp::store_slot_dynamic:
                    reg(inst.x);
                    reg(inst.y);
                    inst.aux += index_base;
                    break;
                case KOp::store_slot_part:
                    reg(inst.x);
                    reg(inst.y);
                    inst.aux += part_base;
                    break;
                case KOp::mem_read:
                    reg(inst.d);
                    reg(inst.y);
                    break;
                case KOp::mem_write:
                    reg(inst.x);
                    reg(inst.y);
                    if (inst.aux != 0U) {
                        reg(inst.z);
                        inst.aux += part_base;
                    }
                    break;
                case KOp::wide_move:
                    if (inst.sub == 1U) {
                        reg(inst.y);
                        inst.aux += part_base;
                    }
                    break;
                case KOp::mem_copy:
                    break;
                case KOp::wide_load:
                    reg(inst.d);
                    break;
                case KOp::wide_copy:
                    reg(inst.d);
                    reg(inst.x);
                    break;
                case KOp::wide_const:
                    reg(inst.d);
                    inst.imm_a += constant_base;
                    break;
                case KOp::wide_concat:
                    reg(inst.d);
                    inst.aux += concat_base;
                    break;
                case KOp::wide_store:
                    reg(inst.x);
                    break;
                case KOp::wide_part_insert:
                    reg(inst.d);
                    reg(inst.x);
                    reg(inst.y);
                    reg(inst.z);
                    inst.aux += part_base;
                    break;
                case KOp::load_slot9:
                case KOp::store_vhdl:
                case KOp::integer_binary:
                case KOp::integer_unary:
                case KOp::integer_check:
                case KOp::call:
                case KOp::ret:
                case KOp::assert_check:
                case KOp::deopt:
                case KOp::load_field9:
                    throw std::logic_error(
                        "VHDL kernel instructions are not partitioned");
                }
                program.code.push_back(inst);
            }
            // Member state now lives in the partition program.
            members_[index].compiled.reset();
            if (index < not_compiled_reason_.size()) {
                not_compiled_reason_[index] = "partitioned";
            }
        }
        forward_partition_stores(program, slot_of_offset);
    }

    if (profile_) {
        // Diagnostic: distinct programs modulo instance bindings.
        const auto canonical = [&](const CompiledBody& body) {
            std::string key;
            std::unordered_map<std::uint64_t, std::uint32_t> bindings;
            const auto bind = [&](const std::uint64_t value) {
                const auto [it, inserted] = bindings.emplace(
                    value, static_cast<std::uint32_t>(bindings.size()));
                return it->second;
            };
            for (auto inst : body.code) {
                switch (inst.op) {
                case KOp::load_slot:
                    inst.x = bind(inst.x);
                    break;
                case KOp::load_host:
                    inst.x = bind((std::uint64_t { 1 } << 40U) | inst.x);
                    break;
                case KOp::wide_load:
                    inst.x = bind((std::uint64_t { 5 } << 40U) | inst.x);
                    break;
                case KOp::store_slot:
                case KOp::store_slot_nba:
                case KOp::store_slot_dynamic:
                case KOp::store_slot_part:
                case KOp::wide_store:
                    inst.d = bind((std::uint64_t { 2 } << 40U) | inst.d);
                    break;
                case KOp::store_host:
                    inst.d = bind((std::uint64_t { 3 } << 40U) | inst.d);
                    break;
                case KOp::mem_read:
                    inst.x = bind((std::uint64_t { 4 } << 40U) | inst.x);
                    break;
                case KOp::mem_write:
                    inst.d = bind((std::uint64_t { 4 } << 40U) | inst.d);
                    break;
                case KOp::mem_copy:
                    inst.d = bind((std::uint64_t { 4 } << 40U) | inst.d);
                    inst.x = bind((std::uint64_t { 4 } << 40U) | inst.x);
                    break;
                case KOp::mark:
                    inst.x = 0U;
                    break;
                default:
                    break;
                }
                KInst clean;
                std::memset(static_cast<void*>(&clean), 0, sizeof(clean));
                clean.op = inst.op;
                clean.sub = inst.sub;
                clean.flags = inst.flags;
                clean.width = inst.width;
                clean.d = inst.d;
                clean.x = inst.x;
                clean.y = inst.y;
                clean.z = inst.z;
                clean.offset = inst.offset;
                clean.aux = inst.aux;
                clean.imm_a = inst.imm_a;
                clean.imm_b = inst.imm_b;
                key.append(reinterpret_cast<const char*>(&clean), sizeof(clean));
            }
            return key;
        };
        std::unordered_map<std::string, std::size_t> partition_templates;
        std::size_t partition_ops = 0U;
        for (const auto& partition_state : partitions_) {
            ++partition_templates[canonical(partition_state.program)];
            partition_ops += partition_state.program.code.size();
        }
        std::size_t template_ops = 0U;
        for (const auto& [key, uses] : partition_templates) {
            template_ops += key.size() / sizeof(KInst);
        }
        std::unordered_map<std::string, std::size_t> member_templates;
        std::size_t member_ops = 0U;
        std::size_t member_template_ops = 0U;
        for (const auto& member : members_) {
            if (member.compiled && member.partition == no_slot) {
                const auto key = canonical(*member.compiled);
                if (member_templates[key]++ == 0U) {
                    member_template_ops += member.compiled->code.size();
                }
                member_ops += member.compiled->code.size();
            }
        }
        if (const char* left_name = std::getenv("FSIM_KERNEL_DIFF_A")) {
            const char* right_name = std::getenv("FSIM_KERNEL_DIFF_B");
            const CompiledBody* left = nullptr;
            const CompiledBody* right = nullptr;
            for (const auto& partition_state : partitions_) {
                const auto& name = profile_names_[partition_state.members.front()];
                if (left == nullptr && name.find(left_name) != std::string::npos) {
                    left = &partition_state.program;
                }
                if (right == nullptr && right_name != nullptr
                    && name.find(right_name) != std::string::npos) {
                    right = &partition_state.program;
                }
            }
            if (left != nullptr && right != nullptr) {
                const auto a = canonical(*left);
                const auto b = canonical(*right);
                std::cerr << "fsim-kernel-diff: sizes " << left->code.size() << ' '
                          << right->code.size() << '\n';
                for (std::size_t at = 0U; at * sizeof(KInst) < a.size()
                     && at * sizeof(KInst) < b.size(); ++at) {
                    if (a.compare(at * sizeof(KInst), sizeof(KInst), b,
                            at * sizeof(KInst), sizeof(KInst)) != 0) {
                        KInst x;
                        KInst y;
                        std::memcpy(&x, a.data() + at * sizeof(KInst), sizeof(KInst));
                        std::memcpy(&y, b.data() + at * sizeof(KInst), sizeof(KInst));
                        std::cerr << "fsim-kernel-diff: at " << at << " op "
                                  << static_cast<int>(x.op) << '/' << static_cast<int>(y.op)
                                  << " sub " << int(x.sub) << '/' << int(y.sub)
                                  << " d " << x.d << '/' << y.d << " x " << x.x << '/' << y.x
                                  << " y " << x.y << '/' << y.y << " z " << x.z << '/' << y.z
                                  << " off " << x.offset << '/' << y.offset << " w " << x.width
                                  << '/' << y.width << " aux " << x.aux << '/' << y.aux
                                  << " imm " << x.imm_a << '/' << y.imm_a << " flags "
                                  << int(x.flags) << '/' << int(y.flags) << '\n';
                        break;
                    }
                }
            }
        }
        std::cerr << "fsim-kernel: forwarded_loads=" << forwarded_loads_ << '\n';
        std::cerr << "fsim-kernel: partitions=" << partitions_.size()
                  << " partition_templates=" << partition_templates.size()
                  << " partition_ops=" << partition_ops
                  << " template_ops=" << template_ops
                  << " member_templates=" << member_templates.size()
                  << " member_ops=" << member_ops
                  << " member_template_ops=" << member_template_ops << '\n';
    }

    // Silent slots: no reader, or every writer and reader is in one
    // partition and every reader runs after every writer in its pass.
    std::unordered_map<ProcessId, std::uint32_t> member_of_process;
    for (std::uint32_t index = 0U; index < count; ++index) {
        member_of_process.emplace(members_[index].process, index);
    }
    for (std::uint32_t index = 0U; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        if (slot.output || !slot.edges.empty()
            || (index < slot_waitable_.size() && slot_waitable_[index] != 0U)) {
            continue;
        }
        if (slot.readers.empty()) {
            // Nothing observes changes: materialize reads the value itself.
            slot.silent = true;
            continue;
        }
        if (slot.writers.empty() || slot.deferred_write) {
            continue;
        }
        std::uint32_t partition = no_slot;
        std::uint32_t last_writer = 0U;
        bool silent = true;
        for (const auto& writer : slot.writers) {
            const auto found = member_of_process.find(writer.process);
            if (found == member_of_process.end()
                || members_[found->second].partition == no_slot
                || (partition != no_slot
                    && members_[found->second].partition != partition)) {
                silent = false;
                break;
            }
            partition = members_[found->second].partition;
            last_writer = std::max(last_writer, members_[found->second].position);
        }
        for (const auto& reader : slot.readers) {
            if (!silent) {
                break;
            }
            silent = members_[reader.member].partition == partition
                && members_[reader.member].position > last_writer;
        }
        slot.silent = silent;
    }
}

} // namespace fsim::runtime::simir

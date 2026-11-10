// SPDX-License-Identifier: Apache-2.0
//
// Compiled narrow tier of the engine v4 static kernel. A member whose SimIR
// registers have statically inferable widths of at most 64 bits, and whose
// signal and memory accesses are narrow, is translated one-to-one into a
// compact register-machine program over aval/bval word pairs. Operation
// semantics are the exact word functions in simir_kernel_word_ops.hpp, fuzzed
// against the reference value functions. Everything else stays on the
// generic evaluator.
//
// This unit translates members to KIR (compile_members, compile); the
// analyses, native units and execution live in the sibling
// simir_static_kernel_*.cpp units.
#include "simir_static_kernel_compiled_internal.hpp"

#include "fsim/runtime/sva_storage.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cxxabi.h>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <typeinfo>
#include <cstring>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;
using namespace static_kernel_compiled_detail;
namespace kw = kernel_word;

namespace {

constexpr std::string_view bodies_magic { "fsim-static-kernel-bodies-v11" };

} // namespace

std::string static_kernel_compiler_identity()
{
    return std::string { bodies_magic } + ";" __DATE__ " " __TIME__ ";"
        + static_kernel_specializer_identity();
}

void Interpreter::Impl::StaticKernel::compile_members()
{
    std::map<std::string, std::size_t> failures;
    not_compiled_reason_.assign(members_.size(), nullptr);
    // The block table of the concurrent assertion directives (KOp::sva_tick,
    // sva_outcome): laid out before any member compiles.
    plan_sva_table();
    // Decided before any member compiles (and before the commit reads it).
    (void)design_may_hold_weak();
    const char* dump = std::getenv("FSIM_KERNEL_DUMP");
    std::size_t dumped = 0U;
    // Diagnostics: compile only members whose name contains
    // FSIM_KERNEL_COMPILE_ONLY, or skip those containing
    // FSIM_KERNEL_COMPILE_SKIP.
    const char* only = std::getenv("FSIM_KERNEL_COMPILE_ONLY");
    const char* skip = std::getenv("FSIM_KERNEL_COMPILE_SKIP");
    const char* specialize_text = std::getenv("FSIM_STATIC_KERNEL_SPECIALIZE");
    const bool specialize = specialize_text == nullptr
        || std::string_view { specialize_text } != "0";
    // Members that run once (constant drivers, mostly) are compiled like
    // the others: a compiled simulation runs no operation on the reference
    // evaluator. FSIM_STATIC_KERNEL_COMPILE_ONCE=0 runs them there instead.
    const char* compile_once_text = std::getenv("FSIM_STATIC_KERNEL_COMPILE_ONCE");
    const bool compile_once = compile_once_text == nullptr
        || std::string_view { compile_once_text } != "0";
    // Restored bodies, by member: (process, operation count, body).
    struct RestoredBody {
        bool present { };
        ProcessId process { };
        std::uint64_t operations { };
        std::optional<static_kernel_detail::CompiledBody> body;
    };
    std::vector<RestoredBody> restored_bodies;
    if (restored_bodies_ && restored_bodies_->starts_with(bodies_magic)) {
        BodyReader reader { restored_bodies_->substr(
            bodies_magic.size()) };
        std::vector<RestoredBody> entries(members_.size());
        std::uint64_t count { };
        bool valid = reader.get(count) && count <= members_.size();
        for (std::uint64_t entry = 0U; valid && entry < count; ++entry) {
            std::uint32_t index { };
            bool has_body { };
            RestoredBody scratch;
            valid = reader.get(index) && index < members_.size()
                && !entries[index].present && reader.get(scratch.process)
                && reader.get(scratch.operations) && reader.get(has_body);
            if (!valid) {
                break;
            }
            // Decoded in place; a corrupt stream discards every entry.
            auto& restored = entries[index];
            restored.process = scratch.process;
            restored.operations = scratch.operations;
            restored.present = true;
            if (has_body) {
                restored.body.emplace();
                valid = reader.body(*restored.body);
            }
        }
        if (valid && reader.done()) {
            restored_bodies = std::move(entries);
        }
    }
    std::string recorded_bodies;
    std::uint64_t recorded_count = 0U;
    std::unordered_map<ProcessId, StaticKernelSpecialization*> restored_by_process;
    if (restored_specializations_) {
        for (auto& entry : restored_specializations_->members) {
            restored_by_process.emplace(entry.process, &entry);
        }
    }
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        if (!compile_once && members_[index].kind == StaticKernelMemberKind::once) {
            not_compiled_reason_[index] = "once";
            continue;
        }
        // VHDL behavioral threads (IEEE 1076-2019 11.3) compile like
        // SystemVerilog ones; compile_once carries the design note that maps
        // each VHDL construct to the kernel's machinery. They are not
        // specialized (specialize_member leaves a behavioral member alone).
        // SystemVerilog members with nine-valued registers (a member reading
        // a VHDL-driven net) compile like VHDL ones: the words hold the
        // four-state value of each element and 'U' and weak masks follow
        // (track_unknowns below).
        // Per-thread callable contexts need the automatic frames the
        // compiled bodies elide: such a member runs on the operation tier.
        if (members_[index].thread_contexts) {
            not_compiled_reason_[index] = "thread_contexts";
            continue;
        }
        // So do per-thread register frames (Thread::isolated).
        if (members_[index].isolated_forks) {
            not_compiled_reason_[index] = "isolated_forks";
            continue;
        }
        // Reference-format container registers run the reference's
        // container operations on the operation tier.
        if (!members_[index].dynamic_containers.empty()) {
            not_compiled_reason_[index] = "dynamic_containers";
            continue;
        }
        // Assertion evaluators and region members compile like any other
        // behavioral member (IEEE 1800-2023 4.4.2, 16.14): a sampled value
        // read and a monitor step are effect calls into the assertion
        // services (KOp::generic of their kind; the monitor's state machine
        // and the sampled history are the host's), a move to a later region
        // is a suspension, and the surrounding body is native code.
        // A program's end (complete_program_member) runs on the operation
        // tier.
        if (members_[index].program_owner) {
            not_compiled_reason_[index] = "program";
            continue;
        }
        if (only != nullptr || skip != nullptr) {
            const auto name
                = impl_.processes.program_view(members_[index].process).name();
            if ((only != nullptr && name.find(only) == std::string::npos)
                || (skip != nullptr && name.find(skip) != std::string::npos)) {
                not_compiled_reason_[index] = "filtered";
                continue;
            }
        }
        if (specialize && members_[index].vhdl) {
            const auto specialize_start = std::chrono::steady_clock::now();
            auto& member = members_[index];
            const auto original_size = static_cast<std::uint32_t>(member.operations().size());
            auto* restored = [&]() -> StaticKernelSpecialization* {
                const auto found = restored_by_process.find(member.process);
                return found != restored_by_process.end()
                        && found->second->original_size == original_size
                    ? found->second : nullptr;
            }();
            bool specialized = false;
            if (restored != nullptr) {
                // As specialize_member applies a recipe.
                specialized = restored->specialized;
                if (specialized) {
                    member.origin.resize(original_size);
                    for (std::uint32_t op = 0U; op < original_size; ++op) {
                        member.origin[op] = op;
                    }
                    // Each restored entry serves one member: move it.
                    member.origin.insert(member.origin.end(),
                        restored->origin.begin(), restored->origin.end());
                    member.operations().insert(member.operations().end(),
                        std::make_move_iterator(restored->operations.begin()),
                        std::make_move_iterator(restored->operations.end()));
                    restored->operations.clear();
                    member.body_begin = original_size;
                }
            } else {
                specialized = specialize_member(index);
            }
            if (specialized) {
                ++specialized_members_;
                member.body_identity = nullptr;
            }
            if (recorded_specializations_) {
                auto& entry = recorded_specializations_->members.emplace_back();
                entry.process = member.process;
                entry.original_size = original_size;
                entry.specialized = specialized;
                if (specialized) {
                    entry.operations.assign(
                        member.operations().begin() + original_size,
                        member.operations().end());
                    entry.origin.assign(member.origin.begin() + original_size,
                        member.origin.end());
                }
            }
            specialize_seconds_ += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - specialize_start).count();
        }
        try {
            if (index < restored_bodies.size() && restored_bodies[index].present
                && restored_bodies[index].process == members_[index].process
                && restored_bodies[index].operations
                    == members_[index].operations().size()) {
                members_[index].compiled = std::move(restored_bodies[index].body);
                note_sva_code(index);
                static const bool verify_bodies
                    = std::getenv("FSIM_KERNEL_VERIFY_BODIES") != nullptr;
                if (verify_bodies) {
                    // Diagnostic: the restored body must be what compiling gives.
                    std::optional<CompiledBody> fresh;
                    try {
                        fresh = compile(index);
                    } catch (const CompileFailure&) {
                    }
                    const auto* a = fresh ? &*fresh : nullptr;
                    const auto* b = members_[index].compiled
                        ? &*members_[index].compiled : nullptr;
                    std::string differs;
                    if ((a == nullptr) != (b == nullptr)) {
                        differs = "presence";
                    } else if (a != nullptr) {
                        const auto same_inst = [](const KInst& x, const KInst& y) {
                            return x.op == y.op && x.sub == y.sub && x.flags == y.flags
                                && x.width == y.width && x.d == y.d && x.x == y.x
                                && x.y == y.y && x.z == y.z && x.offset == y.offset
                                && x.aux == y.aux && x.imm_a == y.imm_a
                                && x.imm_b == y.imm_b;
                        };
                        const auto same_words = [](const auto& x, const auto& y) {
                            return x.size() == y.size()
                                && std::equal(x.begin(), x.end(), y.begin(),
                                    [](const auto& l, const auto& r) {
                                        return l.a == r.a && l.b == r.b;
                                    });
                        };
                        if (!std::equal(a->code.begin(), a->code.end(), b->code.begin(),
                                b->code.end(), same_inst)) {
                            differs = "code";
                        } else if (!same_words(a->registers, b->registers)) {
                            differs = "registers";
                        } else if (a->register_widths != b->register_widths) {
                            differs = "register_widths";
                        } else if (a->indices != b->indices) {
                            differs = "indices";
                        } else if (a->parts != b->parts) {
                            differs = "parts";
                        } else if (a->containers != b->containers) {
                            differs = "containers";
                        } else if (a->entry != b->entry) {
                            differs = "entry";
                        } else if (a->wide_registers != b->wide_registers) {
                            differs = "wide_registers";
                        } else if (a->wide_base != b->wide_base) {
                            differs = "wide_base";
                        } else if (!same_words(a->wide_constants, b->wide_constants)) {
                            differs = "wide_constants";
                        } else if (a->return_targets != b->return_targets) {
                            differs = "return_targets";
                        } else if (a->live_at_entry != b->live_at_entry) {
                            differs = "live_at_entry";
                        } else if (a->shadow_base != b->shadow_base) {
                            differs = "shadow_base";
                        } else if (a->tracked != b->tracked) {
                            differs = "tracked";
                        } else if (a->u_mode != b->u_mode) {
                            differs = "u_mode";
                        } else if (a->u_operand_begin != b->u_operand_begin
                            || a->u_operands != b->u_operands) {
                            differs = "u_operands";
                        } else if (a->call_stack_base != b->call_stack_base) {
                            differs = "call_stack_base";
                        } else if (a->resume_entries != b->resume_entries) {
                            differs = "resume_entries";
                        }
                    }
                    if (!differs.empty()) {
                        std::cerr << "fsim-kernel: restored body differs member="
                                  << index << " field=" << differs << '\n';
                    }
                }
            } else {
                members_[index].compiled = compile(index);
            }
            if (recorded_bodies_) {
                BodyWriter writer { recorded_bodies };
                writer.put(index);
                writer.put(members_[index].process);
                writer.put(static_cast<std::uint64_t>(members_[index].operations().size()));
                writer.put(members_[index].compiled.has_value());
                if (members_[index].compiled) {
                    writer.body(*members_[index].compiled);
                }
                ++recorded_count;
            }
            if (members_[index].compiled) {
                // compile() admits frames only when they are no-ops.
                members_[index].frames_elided = (members_[index].vhdl
                    || members_[index].kind == StaticKernelMemberKind::behavioral)
                    && members_[index].compiled->frame_stack_base == 0U;
                // Generic instructions read memories through the static
                // container-register bindings.
                for (std::size_t reg = 0U;
                     reg < members_[index].compiled->containers.size()
                     && reg < members_[index].container_registers.size(); ++reg) {
                    members_[index].container_registers[reg]
                        = members_[index].compiled->containers[reg];
                }
                ++compiled_members_;
                if (dump != nullptr && dumped < 6U
                    && index < profile_names_.size()
                    && profile_names_[index].find(dump) != std::string::npos) {
                    ++dumped;
                    std::cerr << "fsim-kernel-dump: " << profile_names_[index]
                              << " kind=" << static_cast<int>(members_[index].kind)
                              << '\n';
                    for (const auto& inst : members_[index].compiled->code) {
                        std::cerr << "  op=" << static_cast<int>(inst.op)
                                  << " sub=" << static_cast<int>(inst.sub)
                                  << " d=" << inst.d << " x=" << inst.x
                                  << " y=" << inst.y << " z=" << inst.z
                                  << " off=" << inst.offset << " w=" << inst.width
                                  << " imm=" << inst.imm_a << '/' << inst.imm_b
                                  << '\n';
                    }
                }
            }
        } catch (const CompileFailure& failure) {
            not_compiled_reason_[index] = failure.reason;
            if (failures[failure.reason]++ == 0U && profile_
                && index < profile_names_.size()) {
                std::size_t widest = 0U;
                for (const auto& operation : members_[index].operations()) {
                    visit_operation([&](const auto& op) {
                        using T = std::decay_t<decltype(op)>;
                        if constexpr (std::is_same_v<T, LoadConstant>) {
                            widest = std::max(widest, op.value.width());
                        } else if constexpr (std::is_same_v<T, Concatenate>
                            || std::is_same_v<T, Extract>) {
                            widest = std::max<std::size_t>(widest, op.width);
                        }
                    }, operation);
                }
                std::cerr << "fsim-kernel: not compiled example reason="
                          << failure.reason << " member="
                          << profile_names_[index] << " widest=" << widest
                          << " ops=" << members_[index].operations().size()
                          << ' ' << compile_failure_detail_ << '\n';
                for (std::size_t pc = 0U; pc < members_[index].operations().size(); ++pc) {
                    std::cerr << "    " << pc << ": variant="
                              << members_[index].operations()[pc].storage.index();
                    visit_operation([&](const auto& op) {
                        if constexpr (requires { op.destination; }) {
                            if constexpr (std::is_integral_v<
                                              std::decay_t<decltype(op.destination)>>) {
                                std::cerr << " dst=" << op.destination;
                            }
                        }
                        int status = 0;
                        char* name = abi::__cxa_demangle(
                            typeid(op).name(), nullptr, nullptr, &status);
                        std::cerr << ' ' << (name != nullptr ? name : "?");
                        std::free(name);
                        if constexpr (requires { op.source; }) {
                            if constexpr (std::is_integral_v<
                                              std::decay_t<decltype(op.source)>>) {
                                std::cerr << " src=" << op.source;
                            }
                        }
                    }, members_[index].operations()[pc]);
                    std::cerr << '\n';
                }
            }
        }
    }
    if (recorded_bodies_) {
        recorded_bodies_->assign(bodies_magic);
        BodyWriter writer { *recorded_bodies_ };
        writer.put(recorded_count);
        recorded_bodies_->append(recorded_bodies);
    }
    if (profile_) {
        std::cerr << "fsim-kernel: wide members=" << wide_members_
                  << " wide_ops=" << wide_member_ops_
                  << " total_ops=" << wide_member_total_ops_ << '\n';
        for (const auto& [reason, count] : failures) {
            std::cerr << "fsim-kernel: not compiled reason=" << reason
                      << " count=" << count << '\n';
        }
    }
}

void Interpreter::Impl::StaticKernel::report_interpreted_sites() const
{
    // Rows by reason: compiled members without native code (kernel
    // bytecode). Listed apart: instructions lowered to an operation's helper
    // (generic), which native code calls by kind, and members or first runs
    // that run operation-tier code (tier:), which calls one helper per
    // operation.
    std::map<std::string, std::pair<std::size_t, std::uint32_t>> sites;
    std::map<std::string, std::pair<std::size_t, std::uint32_t>> helpers;
    const auto note = [&](std::string reason, const std::uint32_t member) {
        auto& rows = reason.starts_with("generic") || reason.starts_with("tier:")
            ? helpers : sites;
        rows.try_emplace(std::move(reason), 0U, member).first->second.first++;
    };
    const auto note_generic = [&](const CompiledBody& code,
                                  const std::uint32_t member) {
        for (const auto& inst : code.code) {
            // aux 2: an attempt-start report, which calls its helper only
            // once a library has bound cbAssertionStart.
            if (inst.op != KOp::generic || inst.aux == 2U
                || inst.x >= members_[member].operations().size()) {
                continue;
            }
            visit_operation([&](const auto& op) {
                int status = 0;
                char* name = abi::__cxa_demangle(typeid(op).name(), nullptr,
                    nullptr, &status);
                std::string key = name != nullptr ? name : "?";
                std::free(name);
                note("generic:" + key.substr(key.rfind(':') + 1U), member);
            }, members_[member].operations()[inst.x]);
        }
    };
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        const auto& member = members_[index];
        const char* reason = index < not_compiled_reason_.size()
            ? not_compiled_reason_[index] : nullptr;
        if (!member.compiled) {
            // Partition members run in their partition's program (below).
            if (reason == nullptr || std::string_view { reason } != "partitioned") {
                note(std::string { "tier:not_compiled:" }
                        + (reason != nullptr ? reason : "none"),
                    index);
            }
            continue;
        }
        if (member.native.entry == nullptr
            && member.native.lazy_template == static_kernel_detail::no_slot) {
            note("bytecode", index);
        }
        if (member.vhdl && member.kind != StaticKernelMemberKind::behavioral
            && !first_run_is_activation(member)
            && (member.native.entry == nullptr
                || !has_prologue_entry(*member.compiled, member.vhdl))) {
            note("tier:vhdl_prologue", index);
        }
        note_generic(*member.compiled, index);
    }
    for (const auto& partition : partitions_) {
        if (partition.members.empty()) {
            continue;
        }
        if (partition.native.entry == nullptr
            && partition.native.lazy_template == static_kernel_detail::no_slot) {
            note("bytecode_partition", partition.members.front());
        }
        // Generic instructions name their operation by the first member's
        // stream only when the partition has one member; count them by
        // instruction kind otherwise.
        for (const auto& inst : partition.program.code) {
            if (inst.op == KOp::generic) {
                note("generic_partition", partition.members.front());
            }
        }
    }
    const auto print = [&](const char* row, const auto& rows) {
        for (const auto& [reason, site] : rows) {
            auto example = impl_.processes.program_view(members_[site.second].process)
                               .name();
            std::ranges::replace(example, ' ', '_');
            std::cerr << "fsim-profile: static-kernel-" << row << " reason=" << reason
                      << " count=" << site.first << " example=" << example << '\n';
        }
    };
    print("interpreted", sites);
    print("helper", helpers);
}

std::optional<Interpreter::Impl::StaticKernel::CompiledBody>
Interpreter::Impl::StaticKernel::compile(const std::uint32_t member_index)
{
    family_generic_ = false;
    auto first = compile_family(member_index);
    if (!family_generic_ || !first) {
        return first;
    }
    // A proxy access ran generically: compile its leaves' accesses instead,
    // and keep the first body if that does not compile.
    auto& member = members_[member_index];
    auto operations = member.operations();
    auto origin = member.origin;
    auto stripped_origin = member.stripped_origin;
    auto kinds = member.register_kinds;
    const auto registers = member.registers.size();
    const auto identity = member.body_identity;
    const auto expansion_begin = member.family_expansion_begin;
    auto continuations = member.piece_continuations;
    if (!expand_family_accesses(member_index)) {
        return first;
    }
    try {
        family_generic_ = false;
        auto second = compile_family(member_index);
        if (second) {
            return second;
        }
    } catch (const CompileFailure& failure) {
        if (profile_) {
            std::cerr << "fsim-kernel: expanded accesses not compiled reason="
                      << failure.reason << " member=" << member_index << '\n';
        }
    }
    member.operations() = std::move(operations);
    member.origin = std::move(origin);
    member.stripped_origin = std::move(stripped_origin);
    member.register_kinds = std::move(kinds);
    member.registers.resize(registers);
    member.body_identity = identity;
    member.family_expansion_begin = expansion_begin;
    member.piece_continuations = std::move(continuations);
    return first;
}

std::optional<Interpreter::Impl::StaticKernel::CompiledBody>
Interpreter::Impl::StaticKernel::compile_family(const std::uint32_t member_index)
{
    try {
        return compile_once(member_index);
    } catch (const CompileFailure& failure) {
        // A register several call sites give values of different widths has
        // no static width: each site gets its own copy of the callee and the
        // copies' registers are split (split_polymorphic_calls), then the
        // member compiles again; a member the split cannot help keeps its
        // reason.
        auto& member = members_[member_index];
        if (!member.vhdl || std::string_view { failure.reason } != "register_width") {
            throw;
        }
        auto operations = member.operations();
        auto origin = member.origin;
        auto stripped_origin = member.stripped_origin;
        auto kinds = member.register_kinds;
        const auto registers = member.registers.size();
        const auto identity = member.body_identity;
        if (!split_polymorphic_calls(member_index)) {
            throw;
        }
        try {
            return compile_once(member_index);
        } catch (...) {
            member.operations() = std::move(operations);
            member.origin = std::move(origin);
            member.stripped_origin = std::move(stripped_origin);
            member.register_kinds = std::move(kinds);
            member.registers.resize(registers);
            member.body_identity = identity;
            throw;
        }
    }
}

namespace {

/// The plan constants generated code specializes a directive's attempt
/// automaton on (KOp::sva_tick, task #186), or nullopt with the profile
/// reason the tick stays the host monitor's (Monitor::tick on the same attempt
/// block). Supported: a single clocked plan with literal bounds whose root
/// property is
///   - a sequence (`assert`, `cover`, `cover sequence`),
///   - `not` of a sequence, or
///   - `|->`, `|=>`, `#-#` or `#=#` with a sequence antecedent and a sequence
///     (or vacuous) consequent,
/// and whose sequences use only the delay, fork, jump, repetition counter and
/// match programs (no throughout, first_match, and, intersect or clock
/// switch: their threads carry joins and groups). Everything else keeps the
/// host call: `reason` names the first unsupported feature. (Since wave AF
/// the property trees, joins, throughout, first_match and multiclocked plans
/// of any root, 16.13, are covered too; only a malformed plan is refused.)
/// The local-variable constants of `plan` (IEEE 1800-2023 16.10;
/// sva::Monitor::build lays out the same): false when the plan's references
/// to its locals, expressions and local sets are out of range.
bool sva_native_locals(const semantic::sva::Plan& plan, SvaNativePlan& native)
{
    namespace sv = semantic::sva;
    for (const auto& local : plan.locals) {
        native.local_offset.push_back(native.locals_words);
        native.local_size.push_back(sv::local_words(local.width));
        native.local_width.push_back(local.width);
        native.locals_words += native.local_size.back();
    }
    std::uint32_t result_words = 0U;
    for (const auto& expression : plan.expressions) {
        if (expression.target > plan.locals.size()) {
            return false;
        }
        native.expression_target.push_back(expression.target);
        result_words = std::max(result_words,
            expression.target == 0U ? 1U : native.local_size[expression.target - 1U]);
    }
    for (const auto& set : plan.local_sets) {
        if (std::ranges::any_of(set,
                [&](const std::uint32_t local) { return local >= plan.locals.size(); })) {
            return false;
        }
    }
    native.local_sets = plan.local_sets;
    if (!plan.expressions.empty()) {
        native.memo_stride = static_cast<std::uint32_t>(runtime::sva::layout::memo_e_locals)
            + native.locals_words + result_words;
    }
    return true;
}

/// The local-variable constants of the plan of the tick of `site` among
/// `operations`, for the request operations of that site (KOp::sva_tick sub
/// 1 to 4): nullopt when there is no such tick or its plan is malformed.
std::optional<SvaNativePlan> sva_native_requests(
    const std::vector<Operation>& operations, const std::uint32_t site)
{
    for (const auto& item : operations) {
        const auto* op = operation_get_if<SvaOperation>(&item);
        if (op == nullptr || op->kind != SvaKind::tick || op->site != site) {
            continue;
        }
        const auto decoded = semantic::sva::decode(op->plan);
        SvaNativePlan native;
        if (!decoded || !sva_native_locals(*decoded, native)) {
            return std::nullopt;
        }
        return native;
    }
    return std::nullopt;
}

std::optional<SvaNativePlan> sva_native_plan(const SvaOperation& operation,
    std::string& reason)
{
    namespace sv = semantic::sva;
    const auto decoded = sv::decode(operation.plan);
    if (!decoded) {
        reason = "plan";
        return std::nullopt;
    }
    const auto& plan = *decoded;
    if (plan.root >= plan.properties.size()) {
        reason = "plan";
        return std::nullopt;
    }
    const auto kind_name = [](const sv::PropertyKind kind) {
        switch (kind) {
        case sv::PropertyKind::sequence:
            return "sequence";
        case sv::PropertyKind::negation:
            return "negation";
        case sv::PropertyKind::conjunction:
            return "conjunction";
        case sv::PropertyKind::disjunction:
            return "disjunction";
        case sv::PropertyKind::equivalence:
            return "equivalence";
        case sv::PropertyKind::implies:
            return "implies";
        case sv::PropertyKind::implication:
            return "implication";
        case sv::PropertyKind::followed_by:
            return "followed_by";
        case sv::PropertyKind::conditional:
            return "conditional";
        case sv::PropertyKind::nexttime:
            return "nexttime";
        case sv::PropertyKind::always:
            return "always";
        case sv::PropertyKind::eventually:
            return "eventually";
        case sv::PropertyKind::until:
            return "until";
        case sv::PropertyKind::accept:
            return "accept_on";
        case sv::PropertyKind::reject:
            return "reject_on";
        case sv::PropertyKind::vacuous:
            return "vacuous";
        }
        return "property";
    };
    const auto& root = plan.properties[plan.root];
    const auto child_kind = [&](const std::size_t index) -> std::optional<sv::PropertyKind> {
        if (index >= root.children.size()
            || root.children[index] >= plan.properties.size()) {
            return std::nullopt;
        }
        return plan.properties[root.children[index]].kind;
    };
    std::vector<std::uint32_t> used;
    // A property tree of fixed operands: sequences and vacuous leaves under
    // not, and, or, implies, iff, if/else, nexttime, accept_on and reject_on.
    // Each node is one instance in an attempt; the lists of started
    // instances (always, eventually, until, implication consequents that are
    // not sequences) are not part of it.
    const auto tree_ok = [&](const auto& self, const std::uint32_t node) -> bool {
        if (node >= plan.properties.size()) {
            reason = "plan";
            return false;
        }
        const auto& property = plan.properties[node];
        std::size_t children = 0U;
        switch (property.kind) {
        case sv::PropertyKind::sequence:
            used.push_back(property.sequence);
            return true;
        case sv::PropertyKind::vacuous:
            return true;
        case sv::PropertyKind::negation:
        case sv::PropertyKind::nexttime:
        case sv::PropertyKind::accept:
        case sv::PropertyKind::reject:
        case sv::PropertyKind::always:
        case sv::PropertyKind::eventually:
            children = 1U;
            break;
        case sv::PropertyKind::implication:
        case sv::PropertyKind::followed_by:
            if (property.sequence >= plan.sequences.size()) {
                reason = "plan";
                return false;
            }
            used.push_back(property.sequence);
            children = 1U;
            break;
        case sv::PropertyKind::conjunction:
        case sv::PropertyKind::disjunction:
        case sv::PropertyKind::equivalence:
        case sv::PropertyKind::implies:
        case sv::PropertyKind::until:
            children = 2U;
            break;
        case sv::PropertyKind::conditional:
            children = property.children.size() == 1U ? 1U : 2U;
            break;
        default:
            reason = std::string { "property_" } + kind_name(property.kind);
            return false;
        }
        if (property.children.size() != children) {
            reason = "plan";
            return false;
        }
        if ((property.kind == sv::PropertyKind::conditional
                || property.kind == sv::PropertyKind::accept
                || property.kind == sv::PropertyKind::reject)
            && property.guard >= (property.guard_expression ? plan.expressions.size()
                                                            : std::size_t { plan.guards })) {
            reason = "plan";
            return false;
        }
        for (const auto child : property.children) {
            if (!self(self, child)) {
                return false;
            }
        }
        return true;
    };
    switch (root.kind) {
    case sv::PropertyKind::sequence:
        used.push_back(root.sequence);
        break;
    case sv::PropertyKind::negation:
        if (child_kind(0U) != sv::PropertyKind::sequence) {
            if (!tree_ok(tree_ok, plan.root)) {
                return std::nullopt;
            }
            break;
        }
        used.push_back(plan.properties[root.children[0]].sequence);
        break;
    case sv::PropertyKind::implication:
    case sv::PropertyKind::followed_by: {
        const auto consequent = child_kind(0U);
        if (!consequent) {
            reason = "plan";
            return std::nullopt;
        }
        if (*consequent != sv::PropertyKind::sequence
            && *consequent != sv::PropertyKind::vacuous) {
            if (!tree_ok(tree_ok, plan.root)) {
                return std::nullopt;
            }
            break;
        }
        used.push_back(root.sequence);
        if (*consequent == sv::PropertyKind::sequence) {
            used.push_back(plan.properties[root.children[0]].sequence);
        }
        break;
    }
    default:
        if (!tree_ok(tree_ok, plan.root)) {
            return std::nullopt;
        }
        break;
    }
    const auto resolved = [&](const sv::Bound& bound, std::int64_t& out,
                              std::int32_t& slot) {
        switch (bound.mode) {
        case sv::Bound::Mode::literal:
            out = bound.value + bound.offset;
            return true;
        case sv::Bound::Mode::unbounded:
            out = std::numeric_limits<std::int64_t>::max();
            return true;
        case sv::Bound::Mode::slot:
            // Read from the block's bounds region (per-instance constants).
            out = bound.offset;
            slot = static_cast<std::int32_t>(bound.value);
            return bound.value >= 0
                && static_cast<std::size_t>(bound.value) < operation.bounds.size();
        }
        return false;
    };
    SvaNativePlan native;
    if (!sva_native_locals(plan, native)) {
        reason = "plan";
        return std::nullopt;
    }
    const auto expressions = plan.expressions.size();
    std::uint32_t base = 0U;
    for (std::size_t index = 0U; index < plan.sequences.size(); ++index) {
        const auto& sequence = plan.sequences[index];
        SvaNativeSequence compiled;
        compiled.state_base = base;
        base += static_cast<std::uint32_t>(sequence.code.size());
        if (std::ranges::find(used, static_cast<std::uint32_t>(index)) == used.end()) {
            native.sequences.push_back(std::move(compiled));
            continue;
        }
        // A thread's local variables follow its repetition counters.
        compiled.counters = sequence.counters + native.locals_words;
        compiled.locals_base = sequence.counters;
        for (const auto& instruction : sequence.code) {
            switch (instruction.op) {
            case sv::Op::check:
            case sv::Op::delay:
            case sv::Op::fork:
            case sv::Op::jump:
            case sv::Op::counter_set:
            case sv::Op::counter_branch:
            case sv::Op::counter_increment:
            case sv::Op::match:
                break;
            case sv::Op::throughout_begin:
            case sv::Op::throughout_end:
                break;
            case sv::Op::first_match_begin:
            case sv::Op::first_match_end:
                break;
            case sv::Op::join_begin:
            case sv::Op::join_end:
                break;
            case sv::Op::check_expression:
                if (instruction.a >= expressions) {
                    reason = "plan";
                    return std::nullopt;
                }
                break;
            case sv::Op::assign:
                if (instruction.a >= plan.locals.size() || instruction.b >= expressions
                    || native.expression_target[instruction.b] != instruction.a + 1U) {
                    reason = "plan";
                    return std::nullopt;
                }
                break;
            case sv::Op::call:
                if (instruction.a >= plan.calls) {
                    reason = "plan";
                    return std::nullopt;
                }
                break;
            case sv::Op::clock:
                if (plan.clock_guards.empty() || instruction.a >= plan.clock_guards.size()) {
                    reason = "plan";
                    return std::nullopt;
                }
                break;
            }
            SvaNativeStep step { instruction.op, instruction.a, instruction.b,
                instruction.c, 0, 0, instruction.line, instruction.column };
            if (!resolved(instruction.lo, step.lo, step.lo_slot)
                || !resolved(instruction.hi, step.hi, step.hi_slot)) {
                reason = "plan";
                return std::nullopt;
            }
            if (instruction.op == sv::Op::join_end
                && (instruction.b > plan.local_sets.size()
                    || instruction.c > plan.local_sets.size())) {
                reason = "plan";
                return std::nullopt;
            }
            if (instruction.op == sv::Op::join_begin
                && (instruction.b > sequence.code.size() || instruction.c > sequence.code.size()
                    || instruction.a > 1U)) {
                reason = "plan";
                return std::nullopt;
            }
            if (instruction.op == sv::Op::throughout_begin
                && instruction.a >= (instruction.c == 1U ? expressions : plan.guards)) {
                reason = "plan";
                return std::nullopt;
            }
            if ((instruction.op == sv::Op::check && instruction.a >= plan.guards)
                || ((instruction.op == sv::Op::fork || instruction.op == sv::Op::jump)
                    && instruction.a > sequence.code.size())
                || (instruction.op == sv::Op::counter_branch
                    && (instruction.b > sequence.code.size()
                        || instruction.a >= sequence.counters))
                || ((instruction.op == sv::Op::counter_set
                        || instruction.op == sv::Op::counter_increment)
                    && instruction.a >= sequence.counters)) {
                reason = "plan";
                return std::nullopt;
            }
            compiled.code.push_back(step);
        }
        native.sequences.push_back(std::move(compiled));
    }
    for (const auto& property : plan.properties) {
        SvaNativeProperty compiled;
        compiled.kind = property.kind;
        compiled.overlapped = property.overlapped;
        compiled.synchronous = property.synchronous;
        // A multiclocked property's clock (16.13.3); 0 for a singly clocked
        // plan.
        if (property.clock != 0U && property.clock >= plan.clock_guards.size()) {
            reason = "plan";
            return std::nullopt;
        }
        compiled.clock = property.clock;
        compiled.sequence = property.sequence;
        compiled.child = property.children.empty()
            ? -1 : static_cast<std::int32_t>(property.children[0]);
        for (const auto child : property.children) {
            compiled.children.push_back(static_cast<std::int32_t>(child));
        }
        compiled.guard = property.guard;
        if ((property.kind == sv::PropertyKind::nexttime
                || property.kind == sv::PropertyKind::always
                || property.kind == sv::PropertyKind::eventually)
            && !resolved(property.lo, compiled.lo, compiled.lo_slot)) {
            reason = "plan";
            return std::nullopt;
        }
        if ((property.kind == sv::PropertyKind::always
                || property.kind == sv::PropertyKind::eventually)
            && !resolved(property.hi, compiled.hi, compiled.hi_slot)) {
            reason = "plan";
            return std::nullopt;
        }
        compiled.line = property.line;
        compiled.column = property.column;
        compiled.guard_expression = property.guard_expression;
        compiled.initializers = property.initializers;
        if (std::ranges::any_of(property.initializers, [&](const std::uint32_t expression) {
                return expression >= expressions || native.expression_target[expression] == 0U;
            })) {
            reason = "plan";
            return std::nullopt;
        }
        native.properties.push_back(compiled);
    }
    native.root = plan.root;
    native.every_match = plan.every_match && root.kind == sv::PropertyKind::sequence;
    if (!plan.clock_guards.empty()) {
        native.clock_guards = plan.clock_guards;
    }
    for (const auto& span : plan.guard_spans) {
        native.guard_positions.emplace_back(span.line, span.column);
    }
    return native;
}

} // namespace

std::optional<Interpreter::Impl::StaticKernel::CompiledBody>
Interpreter::Impl::StaticKernel::compile_once(const std::uint32_t member_index)
{
    const auto& member = members_[member_index];
    // A VHDL member compiles its whole operation stream (subprogram bodies
    // may follow the loop): it enters after its wait and leaves when it
    // reaches the wait (or the halt of a process without sensitivity). A
    // behavioral member also compiles its whole stream and enters at any
    // resume point (body.resume_entries).
    const bool behavioral = member.kind == StaticKernelMemberKind::behavioral;
    // A VHDL member (delta semantics); a mixed kernel holds both languages.
    const bool vhdl = member.vhdl;
    const bool whole = vhdl || behavioral;
    // Design note: VHDL behavioral processes (IEEE 1076-2019 11.3) on the
    // kernel's thread machinery. A process is one thread of a behavioral
    // member whose compiled body holds all of its operations; every wait is
    // a KOp::suspend that returns to the thread runner (run_thread), which
    // registers it in behavioral_boundary_op and resumes the body at the next
    // operation (body.resume_entries).
    //  - wait on S (10.2): WaitOn registers the signals' any-change
    //    sensitivity (wait_on); the wake lands in the next delta cycle
    //    (14.7.5.3) through vhdl_ready_threads_.
    //  - wait for T (10.2): WaitFor puts a deadline in vhdl_timers_, which
    //    expires in the update phase of its time (vhdl_timer_due); T = 0
    //    resumes in the next delta cycle.
    //  - wait until C / combined forms (10.2): WaitOn with a timeout
    //    (thread.deadline; timeout_origin keeps the first wait's deadline on
    //    a re-wait) and the loop back to the condition are ordinary branches
    //    in compiled code; a timeout's result register is written by
    //    run_thread when the thread resumes (set_thread_register).
    //  - zero-delay signal assignment (10.5.2.2): the store (KOp::store_vhdl,
    //    store_host) queues the transaction for the round's update phase
    //    (14.7.5.2, commit_round); a variable assignment is a register move.
    //  - delayed, transport, reject ... inertial and multi-element
    //    assignments (10.5.2.2, edit rules of the projected output waveform
    //    in 14.7.2): KOp::net_delay with sub 1 (execute_projected_write):
    //    the driver's waveform is kernel state (projected_sites_, a list of
    //    transactions per bit), edited by projected_edit; a transaction
    //    matures through a timer in vhdl_timers_ in the update phase of its
    //    time and writes the slot before the processes of that cycle run
    //    (collect_timers). A driver that has any such assignment sends all
    //    of its assignments of the signal through the waveform (a later one
    //    preempts the pending transactions); a host signal's assignment
    //    goes to the host's driver (write_host_projected).
    // Registers survive every suspension (one register file per member), so
    // no liveness is computed (live_at_entry stays empty), there is no
    // prologue entry and no specialization, and the first-run (fresh) path of
    // activation-style members does not apply.
    // A member that is not compiled whole runs [body_begin, body_end) and
    // the operations a family expansion appended (expand_family_accesses),
    // which the body reaches by jumps; the operations between are left out,
    // the exit operation at body_end ends the body.
    const bool expanded = !whole && member.family_expansion_begin != 0U
        && member.family_expansion_begin <= member.operations().size();
    const auto begin = whole ? 0U : member.body_begin;
    const auto end = whole || expanded
        ? static_cast<std::uint32_t>(member.operations().size())
        : member.body_end;
    const auto stop = member.body_end;
    const auto register_count = member.registers.size();
    const auto reject = [](const char* reason) -> CompileFailure {
        return CompileFailure { reason };
    };
    const auto kind_of = [&](const RegisterId reg) {
        return reg < member.register_kinds.size() ? member.register_kinds[reg]
                                                  : ValueKind::logic4;
    };
    // A recursive VHDL callable keeps real frames: its packed registers go
    // on the body's growable frame stack at each call (KOp::frame_push,
    // frame_pop); the lists are the pushes' (a pop names only what it keeps).
    // The callables whose frames also list strings, containers or private
    // arrays (storage_frames) save and restore those through the runtime,
    // which owns them (frame_storage).
    bool recursive_frames = false;
    std::map<std::uint32_t, std::vector<RegisterId>> frame_lists;
    std::set<std::uint32_t> storage_frames;
    if (whole) {
        // Automatic frames compile to nothing when each callable owns its
        // registers (native_isolated) and no call chain recurses.
        std::vector<std::vector<std::uint32_t>> callees(end);
        std::vector<std::uint8_t> is_function(end, 0U);
        bool frames = false;
        for (std::uint32_t pc = 0U; pc < end; ++pc) {
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, CallableFramePush>) {
                    frames = true;
                    if (!op.native_isolated) {
                        throw reject("frame_not_isolated");
                    }
                    frame_lists.try_emplace(op.identity,
                        op.packed.begin(), op.packed.end());
                } else if constexpr (std::is_same_v<T, Call>) {
                    if (op.target >= end || op.return_target >= end) {
                        throw reject("call_target");
                    }
                    is_function[op.target] = 1U;
                }
            }, member.operations()[pc]);
        }
        if (frames) {
            // Callees reachable from each function entry, following control
            // flow up to its returns.
            for (std::uint32_t entry = 0U; entry < end; ++entry) {
                if (is_function[entry] == 0U) {
                    continue;
                }
                std::vector<std::uint8_t> seen(end, 0U);
                std::vector<std::uint32_t> work { entry };
                while (!work.empty()) {
                    const auto pc = work.back();
                    work.pop_back();
                    if (pc >= end || seen[pc] != 0U) {
                        continue;
                    }
                    seen[pc] = 1U;
                    visit_operation([&](const auto& op) {
                        using T = std::decay_t<decltype(op)>;
                        if constexpr (std::is_same_v<T, Jump>) {
                            work.push_back(op.target);
                        } else if constexpr (std::is_same_v<T, Branch>) {
                            work.push_back(op.when_true);
                            work.push_back(op.when_false);
                        } else if constexpr (std::is_same_v<T, Call>) {
                            callees[entry].push_back(op.target);
                            work.push_back(op.return_target);
                        } else if constexpr (std::is_same_v<T, Fork>) {
                            for (const auto branch : op.branches) {
                                work.push_back(branch);
                            }
                            work.push_back(pc + 1U);
                        } else if constexpr (std::is_same_v<T, DisableBlock>) {
                            work.push_back(pc + 1U);
                            if (op.scopes.empty()) {
                                work.push_back(op.end);
                            }
                        } else if constexpr (std::is_same_v<T, Return>
                            || std::is_same_v<T, WaitSensitivity>
                            || std::is_same_v<T, Halt>
                            || std::is_same_v<T, ForkEnd>
                            || std::is_same_v<T, Stop>) {
                        } else {
                            work.push_back(pc + 1U);
                        }
                    }, member.operations()[pc]);
                }
            }
            std::vector<std::uint8_t> state(end, 0U);
            const auto cyclic = [&](const auto& self, const std::uint32_t node)
                -> bool {
                if (state[node] == 1U) {
                    return true;
                }
                if (state[node] == 2U) {
                    return false;
                }
                state[node] = 1U;
                for (const auto callee : callees[node]) {
                    if (self(self, callee)) {
                        return true;
                    }
                }
                state[node] = 2U;
                return false;
            };
            for (std::uint32_t entry = 0U; entry < end; ++entry) {
                if (is_function[entry] != 0U && cyclic(cyclic, entry)) {
                    recursive_frames = true;
                    break;
                }
            }
            if (recursive_frames) {
                // Any other language has no compiled frame yet. Strings,
                // containers and private arrays in a frame are a behavioral
                // member's (the static tiers take no string or container
                // registers): each activation gets its own (IEEE 1076-2019
                // 14.6 b), 4.3) through the push and pop (KOp::frame_push
                // and frame_pop sub 1).
                if (!vhdl) {
                    throw reject("recursive_call");
                }
                for (std::uint32_t pc = 0U; pc < end; ++pc) {
                    if (const auto* push = operation_get_if<CallableFramePush>(
                            &member.operations()[pc]);
                        push != nullptr
                        && (!push->strings.empty() || !push->containers.empty()
                            || member.private_frame(push->identity))) {
                        if (!behavioral) {
                            throw reject("recursive_frame_storage");
                        }
                        storage_frames.insert(push->identity);
                    }
                }
            }
        }
    }
    // 0. VHDL: operations the compiled entry never reaches (the process
    // prologue runs only in the first, reference-evaluated run) and constant
    // loads no later read can see are left out; they would otherwise give
    // registers conflicting widths.
    std::vector<std::uint8_t> skip(end, 0U);
    if (expanded) {
        for (auto pc = member.body_end; pc < member.family_expansion_begin; ++pc) {
            skip[pc] = 1U;
        }
    }
    std::vector<std::uint64_t> live_at_entry;
    if (vhdl && !behavioral) {
        if (member.body_identity != nullptr) {
            const std::tuple key { member.body_identity, member.body_begin,
                member.body_end };
            if (const auto found = prune_cache_.find(key);
                found != prune_cache_.end()) {
                skip = found->second.first;
                live_at_entry = found->second.second;
            } else {
                prune_operations(member_index, skip, live_at_entry);
                prune_cache_.emplace(key, std::pair { skip, live_at_entry });
            }
        } else {
            prune_operations(member_index, skip, live_at_entry);
        }
    }

    // 1. Flow-insensitive register widths.
    constexpr auto polymorphic = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> width(register_count, 0U);
    const auto signal_width = [&](const SignalId signal) -> std::uint32_t {
        if (const auto slot = slot_of_signal_[signal]; slot != no_slot) {
            return slots_[slot].width;
        }
        if (const auto family = family_of_signal_[signal]; family != no_slot) {
            return families_[family].width;
        }
        return static_cast<std::uint32_t>(
            impl_.get_signal(signal).initial_value.width());
    };
    // The kernel ticks in one time unit of a $time or $stime query that is
    // computed inline (KOp::sva_sampled_load, aux 4; IEEE 1800-2023 20.3):
    // the unit and precision of the design unit, each the project resolution
    // when the unit has no timescale, as systemverilog_time_function takes
    // them. Empty for $realtime (a rounding-mode dependent real) and for a
    // context the library rejects, which keep the library call.
    const auto time_query_ticks = [&](const SystemVerilogMath& op)
        -> std::optional<std::uint64_t> {
        if (vhdl
            || (op.function != SystemVerilogMathFunction::Time
                && op.function != SystemVerilogMathFunction::Stime)) {
            return std::nullopt;
        }
        const auto resolution = time_resolution_ != 0U
            ? time_resolution_ : impl_.time_format.resolution_femtoseconds;
        const auto unit = op.time_unit_femtoseconds != 0U
            ? op.time_unit_femtoseconds : resolution;
        const auto precision = op.time_precision_femtoseconds != 0U
            ? op.time_precision_femtoseconds : resolution;
        if (resolution == 0U || precision == 0U || unit % precision != 0U
            || precision % resolution != 0U) {
            return std::nullopt;
        }
        return unit / resolution;
    };
    // The ordinal of a directive among the member's SvaOperation sites: its
    // entry in the kernel's block table (sva_member_base_[member] + ordinal).
    const auto sva_site_ordinal = [&](const std::uint32_t site) {
        std::vector<std::uint32_t> sites;
        for (const auto& operation : member.operations()) {
            if (const auto* sva = operation_get_if<SvaOperation>(&operation);
                sva != nullptr && sva->kind != SvaKind::control
                && std::ranges::find(sites, sva->site) == sites.end()) {
                sites.push_back(sva->site);
            }
        }
        const auto found = std::ranges::find(sites, site);
        if (found == sites.end()) {
            throw reject("sva_site");
        }
        return static_cast<std::uint32_t>(found - sites.begin());
    };
    // Private arrays are bound from the start; design memories bind at
    // ReadContainerObject.
    std::vector<std::uint32_t> container_of_register(
        member.container_registers.begin(), member.container_registers.end());
    // The width of local variable `local` of the plan of the tick at `site`
    // (the lowering filled it in; 16.10).
    const auto sva_local_width = [&](const std::uint32_t site,
                                     const std::uint32_t local) -> std::uint32_t {
        for (const auto& operation : member.operations()) {
            const auto* sva = operation_get_if<SvaOperation>(&operation);
            if (sva == nullptr || sva->kind != SvaKind::tick || sva->site != site) {
                continue;
            }
            const auto plan = semantic::sva::decode(sva->plan);
            if (plan && local < plan->locals.size()) {
                return plan->locals[local].width;
            }
        }
        throw reject("operation");
    };
    bool changed = true;
    std::uint32_t defining = 0U;
    // A register's width only moves from unknown to a width to polymorphic,
    // so the passes end.
    while (changed) {
        changed = false;
        const auto define = [&](const RegisterId reg, const std::uint32_t value) {
            if (reg >= register_count) {
                throw reject("register_range");
            }
            if (value == 0U || width[reg] == polymorphic) {
                return;
            }
            if (width[reg] == 0U) {
                width[reg] = value;
                changed = true;
            } else if (width[reg] != value) {
                // A register holding values of several widths (a shared
                // subprogram result, for example) is kept as a reference
                // value; every instruction touching it runs generically.
                if (!vhdl) {
                    throw reject("width_conflict");
                }
                if (profile_ && value != polymorphic) {
                    std::string name = impl_.processes.program_view(member.process).name();
                    std::erase_if(name, [](const char c) {
                        return std::isdigit(static_cast<unsigned char>(c)) != 0;
                    });
                    ++profile_generic_ops_["polymorphic " + name + " r"
                        + std::to_string(reg) + " " + std::to_string(width[reg])
                        + "/" + std::to_string(value) + " at "
                        + std::to_string(defining)];
                }
                width[reg] = polymorphic;
                changed = true;
            }
        };
        const auto of = [&](const RegisterId reg) -> std::uint32_t {
            if (reg >= register_count) {
                throw reject("register_range");
            }
            return width[reg];
        };
        for (std::uint32_t pc = begin; pc < end; ++pc) {
            if (skip[pc] != 0U) {
                continue;
            }
            defining = pc;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, LoadConstant>) {
                    define(op.destination,
                        static_cast<std::uint32_t>(op.value.width()));
                } else if constexpr (std::is_same_v<T, CopyRegister>
                    || std::is_same_v<T, UnaryNot>
                    || std::is_same_v<T, ConvertToTwoState>) {
                    define(op.destination, of(op.source));
                } else if constexpr (std::is_same_v<T, ReadSignal>) {
                    define(op.destination, sampled_read_width(op, signal_width(op.signal)));
                } else if constexpr (std::is_same_v<T, SignalEvent>
                    || std::is_same_v<T, SignalActive>) {
                    define(op.destination, 1U);
                } else if constexpr (std::is_same_v<T, SignalLastEvent>
                    || std::is_same_v<T, SignalLastActive>) {
                    // Elapsed ticks (IEEE 1076-2019 16.2.4).
                    define(op.destination, 64U);
                } else if constexpr (std::is_same_v<T, SignalLastValue>) {
                    define(op.destination, op.selection
                        ? op.selection->width : signal_width(op.signal));
                } else if constexpr (std::is_same_v<T, SvaOperation>) {
                    // The outcome count or outcome code (run_sva); a local
                    // variable's read has the local's width, which the
                    // tick's plan of the site holds (IEEE 1800-2023 16.10).
                    define(op.result, op.kind == SvaKind::local
                            ? sva_local_width(op.site, op.guard_count)
                            : 32U);
                } else if constexpr (std::is_same_v<T, WaitOn>) {
                    // A VHDL wait with a timeout (IEEE 1076-2019 10.2): the
                    // thread runner writes the one-bit timed-out flag when
                    // the thread resumes (run_thread).
                    if (op.timeout_result) {
                        define(*op.timeout_result, 1U);
                    }
                } else if constexpr (std::is_same_v<T, Binary>) {
                    define(op.destination,
                        comparison(op.operation) ? 1U : of(op.lhs));
                } else if constexpr (std::is_same_v<T, Reduction>
                    || std::is_same_v<T, LogicalNot>
                    || std::is_same_v<T, LogicalBinary>
                    || std::is_same_v<T, DynamicExtract>) {
                    define(op.destination, 1U);
                } else if constexpr (std::is_same_v<T, Shift>) {
                    define(op.destination, of(op.value));
                } else if constexpr (std::is_same_v<T, Extract>
                    || std::is_same_v<T, DynamicPartSelect>
                    || std::is_same_v<T, Concatenate>) {
                    define(op.destination, op.width);
                } else if constexpr (std::is_same_v<T, Insert>
                    || std::is_same_v<T, DynamicInsert>
                    || std::is_same_v<T, DynamicPartInsert>) {
                    define(op.destination, of(op.target));
                } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
                    define(op.destination, of(op.when_true));
                    define(op.destination, of(op.when_false));
                } else if constexpr (std::is_same_v<T, IntegerBinary>) {
                    define(op.destination, of(op.lhs));
                } else if constexpr (std::is_same_v<T, IntegerUnary>) {
                    define(op.destination, of(op.source));
                } else if constexpr (std::is_same_v<T, PlusArgSelect>
                    || std::is_same_v<T, RandomValue>
                    || std::is_same_v<T, CountOnes>
                    || std::is_same_v<T, CountBits>) {
                    define(op.destination, 32U);
                } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
                    // The time queries (IEEE 1800-2023 20.3): $stime is 32
                    // bits, $time 64 and $realtime a 64-bit real payload.
                    // The others (20.8, 20.9): $rtoi an int, the
                    // shortreal results 32 bits, every other result a
                    // 64-bit real payload (systemverilog_math_payload).
                    define(op.destination,
                        op.function == SystemVerilogMathFunction::Stime
                                || op.function == SystemVerilogMathFunction::Rtoi
                                || op.function
                                    == SystemVerilogMathFunction::BitsToShortReal
                                || op.function
                                    == SystemVerilogMathFunction::ShortRealToBits
                            ? 32U
                            : 64U);
                } else if constexpr (std::is_same_v<T, SystemVerilogScalarBinary>) {
                    // systemverilog_scalar_binary_payload: a comparison is
                    // one bit, a conversion to an integral a 64-bit value;
                    // other results are the payload of their kind, which an
                    // arithmetic result without a declared kind only has at
                    // run time (the member then runs uncompiled).
                    const auto payload = [](const SystemVerilogScalarKind kind) {
                        return kind == SystemVerilogScalarKind::ShortReal ? 32U
                            : kind == SystemVerilogScalarKind::Real
                                || kind == SystemVerilogScalarKind::Realtime
                                || kind == SystemVerilogScalarKind::Time
                                || kind == SystemVerilogScalarKind::Chandle
                            ? 64U
                            : 0U;
                    };
                    if (op.operation >= SystemVerilogScalarBinaryOperator::Equal
                        && op.operation != SystemVerilogScalarBinaryOperator::Convert) {
                        define(op.destination, 1U);
                    } else if (op.operation == SystemVerilogScalarBinaryOperator::Convert
                        && op.result_kind == SystemVerilogScalarKind::None) {
                        define(op.destination, 64U);
                    } else {
                        define(op.destination, payload(op.result_kind));
                    }
                } else if constexpr (std::is_same_v<T, StringMethod>) {
                    // The integer results of the methods (execute_string_
                    // method): 32 bits, or the 64-bit payload of a real or a
                    // VHDL T'VALUE.
                    using M = StringMethodOperator;
                    const auto method = op.operation;
                    if (method == M::getc || method == M::compare
                        || method == M::icompare || method == M::atoi
                        || method == M::atohex || method == M::atooct
                        || method == M::atobin) {
                        define(op.destination, 32U);
                    } else if (method == M::atoreal
                        || method == M::vhdl_enumeration_value
                        || method == M::vhdl_physical_value
                        || method == M::vhdl_integer_value) {
                        define(op.destination, 64U);
                    }
                } else if constexpr (std::is_same_v<T, StringLength>
                    || std::is_same_v<T, StringIndex>) {
                    define(op.destination, 32U);
                } else if constexpr (std::is_same_v<T, CompareStrings>) {
                    define(op.destination, 1U);
                } else if constexpr (std::is_same_v<T, FileOpen>
                    || std::is_same_v<T, FileClose>
                    || std::is_same_v<T, FileEndOfFile>
                    || std::is_same_v<T, FileReadLine>
                    || std::is_same_v<T, FileErrorStatus>
                    || std::is_same_v<T, FileScan>
                    || std::is_same_v<T, FilePosition>
                    || std::is_same_v<T, FileBinaryRead>) {
                    // The count, status and target registers the reference's
                    // run_file stores, with the widths of their values.
                    std::vector<WrittenRegister> written;
                    operation_written_registers_of(op, written);
                    for (const auto& result : written) {
                        if (result.width != 0U) {
                            define(result.reg, result.width);
                        }
                    }
                } else if constexpr (std::is_same_v<T, Call>) {
                    if (op.stack.capacity != 0U) {
                        define(op.stack.pointer, 32U);
                        for (std::uint32_t entry = 0U; entry < op.stack.capacity;
                             ++entry) {
                            define(op.stack.entries + entry, 32U);
                        }
                    }
                } else if constexpr (std::is_same_v<T, ReadContainerObject>) {
                    if (op.destination >= container_of_register.size()
                        || op.object >= container_of_object_.size()
                        || container_of_object_[op.object] == no_container) {
                        throw reject("container_binding");
                    }
                    auto& bound = container_of_register[op.destination];
                    const auto container = container_of_object_[op.object];
                    if (bound != no_container && bound != container) {
                        throw reject("container_rebinding");
                    }
                    bound = container;
                } else if constexpr (std::is_same_v<T, ContainerRead>) {
                    if (op.source >= container_of_register.size()
                        || container_of_register[op.source] == no_container) {
                        return;
                    }
                    define(op.destination, static_cast<std::uint32_t>(
                        containers_[container_of_register[op.source]]
                            .type.element_width));
                }
            }, member.operations()[pc]);
        }
    }

    static const char* const widths_dump = std::getenv("FSIM_KERNEL_WIDTHS");
    if (const char* dump = widths_dump) {
        const auto name = impl_.processes.program_view(member.process).name();
        if (name.find(dump) != std::string::npos) {
            std::cerr << "fsim-kernel-widths: " << name;
            for (std::size_t reg = 0U; reg < register_count; ++reg) {
                std::cerr << ' ' << reg << ':'
                          << (width[reg] == polymorphic ? std::string { "P" }
                                                       : std::to_string(width[reg]));
            }
            std::cerr << '\n';
        }
    }
    // Null-range registers (IEEE 1076-2019 5.3.2.2: an array with a null range
    // has no elements): registers without a width whose every definition
    // produces no bits (a constant, a signal read, a field or a
    // concatenation of width 0, or a copy of such a register). A register
    // without a width that anything else defines is an undefined one (the
    // member then runs uncompiled).
    std::vector<std::uint8_t> null_register(register_count, 0U);
    {
        std::vector<std::uint8_t> zero_defined(register_count, 0U);
        std::vector<RegisterId> reads;
        std::optional<RegisterId> written;
        for (RegisterId reg = 0U; reg < register_count; ++reg) {
            null_register[reg] = width[reg] == 0U ? 1U : 0U;
        }
        for (bool grew = true; grew;) {
            grew = false;
            std::ranges::fill(zero_defined, 0U);
            for (std::uint32_t pc = begin; pc < end; ++pc) {
                if (skip[pc] != 0U) {
                    continue;
                }
                operation_registers(member.operations()[pc], reads, written);
                if (!written || *written >= register_count) {
                    continue;
                }
                const auto target = *written;
                bool zero = false;
                visit_operation([&](const auto& op) {
                    using T = std::decay_t<decltype(op)>;
                    if constexpr (std::is_same_v<T, LoadConstant>) {
                        zero = op.value.width() == 0U;
                    } else if constexpr (std::is_same_v<T, ReadSignal>) {
                        zero = op.kind == SignalReadKind::current && !op.clock
                            && !op.gate && signal_width(op.signal) == 0U;
                    } else if constexpr (std::is_same_v<T, Extract>
                        || std::is_same_v<T, Concatenate>) {
                        zero = op.width == 0U;
                    } else if constexpr (std::is_same_v<T, CopyRegister>) {
                        zero = op.source < register_count
                            && null_register[op.source] != 0U;
                    }
                }, member.operations()[pc]);
                if (zero) {
                    zero_defined[target] = 1U;
                } else if (null_register[target] != 0U) {
                    null_register[target] = 0U;
                    grew = true;
                }
            }
        }
        for (RegisterId reg = 0U; reg < register_count; ++reg) {
            null_register[reg] = null_register[reg] != 0U && zero_defined[reg] != 0U
                ? 1U : 0U;
        }
    }
    if (profile_) {
        std::size_t wide_ops = 0U;
        std::size_t total_ops = 0U;
        for (std::uint32_t pc = begin; pc < end; ++pc) {
            bool wide = false;
            visit_operation([&](const auto& op) {
                if constexpr (requires { op.destination; }) {
                    if constexpr (std::is_integral_v<
                                      std::decay_t<decltype(op.destination)>>) {
                        wide = op.destination < register_count
                            && width[op.destination] > 64U;
                    }
                }
            }, member.operations()[pc]);
            wide_ops += wide ? 1U : 0U;
            ++total_ops;
        }
        if (wide_ops != 0U) {
            ++wide_members_;
            wide_member_ops_ += wide_ops;
            wide_member_total_ops_ += total_ops;
        }
    }
    // 2. Translate.
    CompiledBody body;
    body.weak = design_may_hold_weak() ? 1U : 0U;
    // VHDL calls on the runtime-owned stack use a growable stack in the
    // body (NativeCallStack); the register file holds its depth word.
    if (whole && std::ranges::any_of(member.operations(), [](const Operation& op) {
            const auto* call = operation_get_if<Call>(&op);
            return call != nullptr && call->stack.capacity == 0U;
        })) {
        body.call_stack_base = static_cast<std::uint32_t>(width.size());
        width.resize(width.size() + 1U, 32U);
    }
    if (recursive_frames) {
        body.frame_stack_base = static_cast<std::uint32_t>(width.size());
        width.resize(width.size() + 1U, 32U);
    }
    body.registers.assign(width.size(), { });
    body.register_widths = width;
    body.containers = container_of_register;
    std::uint32_t translating = 0U;
    const auto narrow = [&](const RegisterId reg) -> std::uint32_t {
        if (reg >= register_count || width[reg] == 0U || width[reg] > 64U) {
            if (profile_) {
                compile_failure_detail_ = "reg=" + std::to_string(reg) + " width="
                    + std::to_string(reg < register_count ? width[reg] : 0U)
                    + " op_index=" + std::to_string(
                        member.operations()[translating].storage.index())
                    + " pc=" + std::to_string(translating);
            }
            throw reject("register_width");
        }
        return reg;
    };
    const auto w = [&](const RegisterId reg) { return width[narrow(reg)]; };
    // A frame operation of a recursive callable (body.frame_stack_base):
    // the registers its identity's pushes list, those a pop keeps marked.
    // The items (register-file indices) follow the wide storage's
    // assignment, at the end of the translation.
    // A frame whose callable lists strings, containers or private arrays
    // (storage_frames) has sub 1 and y its operation: the runtime saves or
    // restores those after the registers (frame_storage).
    const auto lower_frame = [&](KInst& inst, const std::uint32_t identity,
                                 const bool push,
                                 const std::vector<RegisterId>& preserve,
                                 const std::uint32_t pc) {
        const auto found = frame_lists.find(identity);
        if (found == frame_lists.end()) {
            throw reject("frame_identity");
        }
        CompiledBody::FrameSave save;
        save.identity = identity;
        save.register_begin = static_cast<std::uint32_t>(body.frame_registers.size());
        for (const auto id : found->second) {
            if (id >= register_count) {
                throw reject("register_width");
            }
            const bool keep = !push
                && std::ranges::find(preserve, id) != preserve.end();
            body.frame_registers.push_back(id | (keep ? 0x80000000U : 0U));
        }
        save.register_end = static_cast<std::uint32_t>(body.frame_registers.size());
        inst.op = push ? KOp::frame_push : KOp::frame_pop;
        inst.x = body.frame_stack_base;
        inst.aux = static_cast<std::uint32_t>(body.frame_saves.size());
        if (storage_frames.contains(identity)) {
            inst.sub = 1U;
            inst.y = pc;
        }
        body.frame_saves.push_back(save);
    };
    // A jump to an activation's exit operation (body_end) ends the body. A
    // behavioral thread has no such exit: its family accesses are expanded
    // from body_end on (expand_family_accesses), so a jump there enters them.
    const auto map_target = [&](const InstructionIndex target) -> std::uint32_t {
        if ((vhdl || expanded) && !behavioral && target == stop) {
            return end - begin;
        }
        return target >= begin && target < end ? target - begin : end - begin;
    };
    const auto owned_narrow_slot = [&](const SignalId signal)
        -> std::optional<std::uint32_t> {
        if (signal >= slot_of_signal_.size()) {
            throw reject("signal_range");
        }
        if (family_of_signal_[signal] != no_slot) {
            throw reject("proxy_access");
        }
        auto slot = slot_of_signal_[signal];
        if (slot == no_slot && mirror_of_signal_[signal] != no_slot
            && slots_[mirror_of_signal_[signal]].planes == 2U) {
            // A mirrored host signal reads like a slot.
            slot = mirror_of_signal_[signal];
        }
        if (slot == no_slot) {
            return std::nullopt;
        }
        if (slots_[slot].words != 1U) {
            throw reject("wide_slot");
        }
        return slot;
    };
    const auto owned_slot = [&](const SignalId signal)
        -> std::optional<std::uint32_t> {
        if (signal >= slot_of_signal_.size()) {
            throw reject("signal_range");
        }
        if (family_of_signal_[signal] != no_slot) {
            throw reject("proxy_access");
        }
        const auto slot = slot_of_signal_[signal];
        if (slot == no_slot) {
            return std::nullopt;
        }
        return slot;
    };
    body.code.reserve(end - begin);
    std::vector<RegisterId> operand_reads;
    std::optional<RegisterId> operand_write;
    bool has_wide = false;
    // A wide owned slot read whose register only feeds narrow selections,
    // in a member that never writes the slot, is read in place.
    std::vector<std::uint32_t> field_slot(register_count, no_slot);
    // Likewise a family proxy read whose narrow selections each lie in one
    // leaf, in a member that never writes the family: each selection loads
    // its leaf (family_leaf).
    std::vector<std::uint32_t> field_family(register_count, no_slot);
    // Wide registers of fixed width (threads included: their wide values are
    // read and written through thread_register/set_thread_register, which
    // know the chunks) live in chunks of the register file (CompiledBody::
    // wide_base); instructions name them by register until storage is
    // assigned after translation.
    // FSIM_KERNEL_WIDE_MASK (diagnostic): the wide lowerings to use (bit 0
    // ReadSignal, 1 CopyRegister, 2 LoadConstant, 3 Concatenate, 4
    // DynamicPartInsert, 5 writes, 6 arithmetic and logic); 0 keeps every
    // register in wide_registers.
    static const std::uint32_t wide_mask = [] {
        const char* text = std::getenv("FSIM_KERNEL_WIDE_MASK");
        return text == nullptr ? ~0U
                               : static_cast<std::uint32_t>(std::strtoul(text, nullptr, 0));
    }();
    const auto chunked = [&](const RegisterId reg) {
        return wide_mask != 0U && reg < register_count && width[reg] > 64U
            && width[reg] != polymorphic && field_slot[reg] == no_slot
            && field_family[reg] == no_slot;
    };
    // A VHDL register may hold Logic9 values whatever its declared kind
    // (only ReadSignal, LoadConstant and CopyRegister coerce to it), so
    // every chunked VHDL register has 'U' masks.
    const auto has_unknowns = [&](const RegisterId reg) {
        return vhdl || kind_of(reg) == ValueKind::logic9;
    };
    // A sampled-value read the kernel loads inline (KOp::sva_sampled_load;
    // KOp::wide_load sub 4 and 5 for a wide result): $sampled of the Preponed
    // value (a signal with sampled words), or a clocked history read
    // ($rose, $fell, $stable, $changed, $past, $sampled with a clock) of a
    // signal whose ring the plan lays out, of any width and any value kind
    // (a Logic9 signal reads through the Logic9 to Logic4 mapping of the
    // host read, IEEE 1800-2023 21.2.1.3). The global-clocking forms are
    // inline only for $future_gclk, which is the signal's current value (a
    // plain read, 16.9.4); $rising_gclk, $falling_gclk, $steady_gclk and
    // $changing_gclk compare it with the sampled value and read the host.
    const auto inline_sampled_read = [&](const ReadSignal& op) {
        if (!sampled_read(op) || op.signal >= impl_.signals.size()
            || signal_width(op.signal) == 0U) {
            return false;
        }
        const bool wide_result = signal_width(op.signal) > 64U
            && (op.kind == SignalReadKind::sampled
                || op.kind == SignalReadKind::past);
        // A Logic9 signal reads through the Logic4 mapping (21.2.1.3; 'H' is
        // 1, 'L' is 0, 'U', 'W' and '-' are x) into any register.
        if (wide_result
            && (!chunked(op.destination) || (wide_mask & 1U) == 0U
                || width[op.destination] != signal_width(op.signal))) {
            return false;
        }
        switch (op.kind) {
        case SignalReadKind::sampled:
            if (!op.clock) {
                impl_.ensure_sampled_value_word_layout();
                return op.signal < impl_.sampled_value_word_offsets.size()
                    && impl_.sampled_value_word_offsets[op.signal]
                        != Impl::no_sampled_value_words;
            }
            [[fallthrough]];
        case SignalReadKind::rose:
        case SignalReadKind::fell:
        case SignalReadKind::stable:
        case SignalReadKind::changed:
        case SignalReadKind::past:
            if (!op.clock || op.ticks == 0U
                || !impl_.planned_sampled_ring_offset(Impl::SampledHistoryKey {
                           op.signal, op.clock, op.clock_edge, op.gate,
                           ProcessSchedulingDomain::systemverilog })
                        .has_value()) {
                return false;
            }
            if (op.kind == SignalReadKind::sampled || op.kind == SignalReadKind::past) {
                return true;
            }
            // A value change function also reads the Preponed value, which a
            // time step that is not a tick of its clock compares (16.9.3);
            // the signals of clocked reads are in the sampled-value mask.
            impl_.ensure_sampled_value_word_layout();
            return op.signal < impl_.sampled_value_word_offsets.size()
                && impl_.sampled_value_word_offsets[op.signal]
                    != Impl::no_sampled_value_words;
        default:
            return false;
        }
    };
    // The words of a signal's default (an empty history read, the signal's
    // value at the start of simulation) in the layout of a ring entry: its
    // planes of ceil(width / 64) words each (two for a Logic4 signal, four
    // for a Logic9 one), appended to the body's wide constants two to a
    // word pair. Returns the index of the first pair.
    const auto sampled_default_constants = [&](const SignalId signal) {
        const auto& value = impl_.get_signal(signal).initial_value;
        const auto chunks = wide_chunks(static_cast<std::uint32_t>(value.width()));
        std::vector<std::uint64_t> words;
        const auto plane = [&](const std::span<const std::uint64_t> source) {
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                words.push_back(chunk < source.size() ? source[chunk] : 0U);
            }
        };
        if (value.is_logic9()) {
            for (std::size_t index = 0U; index < 4U; ++index) {
                plane(value.logic9_plane_words(index));
            }
        } else {
            plane(value.aval_words());
            plane(value.bval_words());
        }
        const auto first = body.wide_constants.size();
        for (std::size_t at = 0U; at < words.size(); at += 2U) {
            body.wide_constants.push_back(
                { words[at], at + 1U < words.size() ? words[at + 1U] : 0U });
        }
        return first;
    };
    // A write through a dynamic index (IEEE 1800-2023 11.5.1: an unknown or
    // out-of-range one writes nothing; 1076-2019 8.4: a checked one) or a
    // dynamic part select (11.5.1) to a host signal, the host applying it as
    // it applies a static one: store_host (narrow source: x, index or part
    // base register y, aux the selection's position + 1 in indices, or in
    // parts when offset is 1) and wide_store sub 3 (chunked source: imm_a the
    // position + 1, from bit 32 for a part). The domain is the member's, the
    // flags flag_blocking and flag_strict.
    const auto host_dynamic_write = [&](KInst& inst, const SignalId signal,
                                        const RegisterId source, const RegisterId index,
                                        const SignalUpdateDomain domain, const bool blocking,
                                        const bool strict, const bool part,
                                        const DynamicIndex* selection,
                                        const DynamicPartIndex* part_selection) {
        inst.d = signal;
        inst.y = index;
        inst.width = width[source];
        inst.flags = static_cast<std::uint8_t>(
            (blocking ? flag_blocking : 0U) | (strict ? flag_strict : 0U));
        const auto position = static_cast<std::uint64_t>(
            (part ? body.parts.size() : body.indices.size()) + 1U);
        if (chunked(source)) {
            inst.op = KOp::wide_store;
            inst.sub = 3U;
            inst.x = source;
            inst.aux = has_unknowns(source) ? 1U : 0U;
            inst.imm_b = static_cast<std::uint64_t>(domain);
            inst.imm_a = part ? (std::uint64_t { 1 } << 32U) | position : position;
        } else {
            inst.op = KOp::store_host;
            inst.x = narrow(source);
            inst.sub = static_cast<std::uint8_t>(domain);
            inst.aux = static_cast<std::uint32_t>(position);
            inst.offset = part ? 1U : 0U;
        }
        if (part) {
            body.parts.push_back(*part_selection);
        } else {
            body.indices.push_back(*selection);
        }
    };
    // A VHDL register read only as the value of assignments to four-state
    // slots (a SystemVerilog net, as on a VHDL inout it alone drives): the
    // assignment maps 'H', 'L', 'W' and '-' to four states, so a constant
    // with those codes loads as its four-state value instead of leaving
    // compiled execution.
    // A behavioral member's registers persist across its suspensions and are
    // observable (a process variable read by the embedder or the debugger), so
    // none is a sink: a nine-value constant keeps its codes.
    std::vector<std::uint8_t> four_state_sink(
        vhdl ? register_count : 0U, behavioral ? 0U : 1U);
    // A VHDL register that only carries a constant into assignments to
    // resolved slots (a SystemVerilog net with several sources), each
    // directly after the constant's load: such an assignment takes the
    // constant's nine values from the load (store_vhdl sub 2 and 3), so 'H',
    // 'L', 'W' and '-' enter the net's resolution as they are.
    std::vector<std::uint8_t> net_constant(vhdl ? register_count : 0U, 1U);
    // The register an assignment to a resolved slot at `pc` stores, when the
    // operation before it loads that register with a constant.
    const auto constant_into_net = [&](const std::uint32_t pc)
        -> std::optional<RegisterId> {
        std::optional<RegisterId> source;
        visit_operation([&](const auto& op) {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, WriteBlocking>
                || std::is_same_v<T, WriteBlockingSlice>
                || std::is_same_v<T, WriteUpdate>
                || std::is_same_v<T, WriteUpdateSlice>
                || std::is_same_v<T, WriteProjected>
                || std::is_same_v<T, WriteProjectedSlice>
                || std::is_same_v<T, WriteBlockingDynamicSlice>
                || std::is_same_v<T, WriteUpdateDynamicSlice>
                || std::is_same_v<T, WriteProjectedDynamicSlice>) {
                const auto slot = owned_slot(op.signal);
                if (!slot || !slots_[*slot].resolved || pc == begin
                    || skip[pc - 1U] != 0U) {
                    return;
                }
                const auto* load = operation_get_if<LoadConstant>(
                    &member.operations()[pc - 1U]);
                if (load != nullptr && load->destination == op.source) {
                    source = op.source;
                }
            }
        }, member.operations()[pc]);
        return source;
    };
    if (vhdl) {
        for (std::uint32_t pc = begin; pc < end; ++pc) {
            if (skip[pc] != 0U) {
                continue;
            }
            const auto& operation = member.operations()[pc];
            operation_registers(operation, operand_reads, operand_write);
            std::optional<RegisterId> stored;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteProjected>
                    || std::is_same_v<T, WriteProjectedSlice>) {
                    if (const auto slot = owned_slot(op.signal);
                        slot && slots_[*slot].planes == 2U
                        && !slots_[*slot].resolved) {
                        stored = op.source;
                    }
                }
            }, operation);
            const auto into_net = constant_into_net(pc);
            for (const auto reg : operand_reads) {
                if (reg < register_count && reg != stored) {
                    four_state_sink[reg] = 0U;
                }
                if (reg < register_count && reg != into_net) {
                    net_constant[reg] = 0U;
                }
            }
        }
    }
    // A constant with codes four-state values do not hold ('H', 'L', 'W',
    // '-').
    const auto weak_codes = [](const PackedLogic4& value) {
        return has_weak_codes(value);
    };
    const auto four_state_constant = [&](const LoadConstant& op) {
        return vhdl && op.destination < register_count && op.value.is_logic9()
            && (four_state_sink[op.destination] != 0U
                || (net_constant[op.destination] != 0U && weak_codes(op.value)));
    };
    // The assignment at `pc` stores a nine-value constant into a resolved
    // slot (net_constant): the position of the constant's load.
    const auto net_constant_load = [&](const std::uint32_t pc)
        -> std::optional<std::uint32_t> {
        const auto source = vhdl ? constant_into_net(pc) : std::nullopt;
        if (!source || *source >= register_count || net_constant[*source] == 0U
            || !weak_codes(operation_get<LoadConstant>(
                member.operations()[pc - 1U]).value)) {
            return std::nullopt;
        }
        return pc - 1U;
    };
    // The codes of the constant loaded at `load`, appended to the body's
    // constants as kind 3 writes read them (StaticKernelWrite): their index.
    const auto net_constant_codes = [&](const std::uint32_t load) {
        const auto& value = operation_get<LoadConstant>(member.operations()[load]).value;
        const auto index = body.wide_constants.size();
        const auto value_width = static_cast<std::uint32_t>(value.width());
        for (std::uint32_t done = 0U; done < value_width; done += 64U) {
            const auto chunk = [&](const std::uint32_t plane) {
                const auto words = value.logic9_plane_words(plane);
                return done / 64U < words.size()
                    ? words[done / 64U] & kernel_word::mask(std::min(64U, value_width - done))
                    : 0U;
            };
            body.wide_constants.push_back({ chunk(0U), chunk(1U) });
            body.wide_constants.push_back({ chunk(2U), chunk(3U) });
        }
        return index;
    };
    // A store to a resolved slot (store_slot sub 2 and 3) carries the
    // slot's shape for generated code: imm_b its source count and, from bit
    // 32, the member's source; aux its width.
    // The source count of a resolved slot as generated code takes it, with
    // net_nine_bit for a slot of nine states (detail::NetShape).
    const auto net_source_count = [&](const std::uint32_t slot) {
        return std::uint64_t { slots_[slot].net_processes.size() }
            | (slots_[slot].nine() ? std::uint64_t { net_nine_bit } : 0U);
    };
    const auto net_store = [&](KInst& inst, const std::uint32_t slot) {
        if (slots_[slot].resolved) {
            inst.imm_b = net_source_count(slot)
                | std::uint64_t { net_source_index(slot, member.process) } << 32U;
            inst.aux = slots_[slot].width;
        }
    };
    const auto family_leaf = [&](const std::uint32_t family,
                                 const std::uint32_t offset,
                                 const std::uint32_t count) -> const FamilyLeaf* {
        for (const auto& leaf : families_[family].leaves) {
            if (leaf.offset <= offset && count <= leaf.width
                && offset - leaf.offset <= leaf.width - count) {
                return &leaf;
            }
        }
        return nullptr;
    };
    // The leaf a write of `count` bits at `offset` of a family proxy
    // replaces exactly.
    const auto proxy_leaf = [&](const SignalId signal, const std::uint32_t offset,
                                const std::uint32_t count) -> const FamilyLeaf* {
        if (signal >= family_of_signal_.size()
            || family_of_signal_[signal] == no_slot) {
            return nullptr;
        }
        const auto* leaf = family_leaf(family_of_signal_[signal], offset, count);
        return leaf != nullptr && leaf->offset == offset && leaf->width == count
            ? leaf : nullptr;
    };
    {
        // Families and slots this member writes, as sorted lists: a member
        // writes few of them, and dense flags over every slot cost more to
        // clear than the member takes to compile.
        std::vector<std::uint32_t> written_families;
        std::vector<std::uint32_t> definitions(register_count, 0U);
        std::vector<std::uint8_t> other_use(register_count, 0U);
        // A written slot may still be read in place when no write to it,
        // no backward jump and no call lies between the read and its last
        // selection (straight-line read-before-write, as in RAM models).
        std::vector<std::uint32_t> definition_pc(register_count, 0U);
        std::vector<std::uint32_t> last_use(register_count, 0U);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> slot_writes;
        std::vector<std::uint32_t> barriers;
        for (std::uint32_t pc = begin; pc < end; ++pc) {
            if (skip[pc] != 0U) {
                continue;
            }
            const auto& operation = member.operations()[pc];
            operation_registers(operation, operand_reads, operand_write);
            if (operand_write && *operand_write < register_count) {
                ++definitions[*operand_write];
                definition_pc[*operand_write] = pc;
            }
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    if (op.target <= pc) {
                        barriers.push_back(pc);
                    }
                } else if constexpr (std::is_same_v<T, Branch>) {
                    if (op.when_true <= pc || op.when_false <= pc) {
                        barriers.push_back(pc);
                    }
                } else if constexpr (std::is_same_v<T, Call>
                    || std::is_same_v<T, Return> || std::is_same_v<T, Halt>
                    || std::is_same_v<T, WaitSensitivity>
                    || behavioral_suspension<T>) {
                    barriers.push_back(pc);
                } else if constexpr (std::is_same_v<T, DebugPoint>) {
                    // A stoppable point: the debugger may deposit a value
                    // while the thread is stopped there.
                    if (debug_points_ && debug_point_executable(op.kind)) {
                        barriers.push_back(pc);
                    }
                } else if constexpr (std::is_same_v<T, Extract>
                    || std::is_same_v<T, DynamicPartSelect>
                    || std::is_same_v<T, DynamicExtract>) {
                    if (op.source < register_count) {
                        last_use[op.source] = std::max(last_use[op.source], pc);
                    }
                }
            }, operation);
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, ReadSignal>) {
                    // A sampled value is the host's history, not the slot
                    // an in-place field read would see.
                    if (sampled_read(op)) {
                        for (const auto reg : operand_reads) {
                            other_use[reg] = 1U;
                        }
                        return;
                    }
                    if (op.signal < family_of_signal_.size()
                        && family_of_signal_[op.signal] != no_slot
                        && op.destination < register_count) {
                        field_family[op.destination]
                            = family_of_signal_[op.signal];
                    }
                    auto slot = op.signal < slot_of_signal_.size()
                        ? slot_of_signal_[op.signal] : no_slot;
                    if (slot == no_slot && op.signal < mirror_of_signal_.size()) {
                        slot = mirror_of_signal_[op.signal];
                    }
                    if (slot != no_slot && slots_[slot].words != 1U
                        && op.destination < register_count) {
                        field_slot[op.destination] = slot;
                    }
                } else if constexpr (std::is_same_v<T, Extract>
                    || std::is_same_v<T, DynamicPartSelect>
                    || std::is_same_v<T, DynamicExtract>) {
                    for (const auto reg : operand_reads) {
                        if (reg != op.source && reg < register_count) {
                            other_use[reg] = 1U;
                        }
                    }
                    if constexpr (requires { op.width; }) {
                        if (op.width > 64U && op.source < register_count) {
                            other_use[op.source] = 1U;
                        }
                    }
                    if (op.source < register_count
                        && field_family[op.source] != no_slot) {
                        bool fits = false;
                        if constexpr (std::is_same_v<T, Extract>) {
                            fits = op.width != 0U && op.width <= 64U
                                && family_leaf(field_family[op.source],
                                       op.offset, op.width) != nullptr;
                        }
                        if (!fits) {
                            field_family[op.source] = no_slot;
                        }
                    }
                    return;
                }
                if constexpr (requires { op.signal; }) {
                    // VHDL signal assignments are deferred to the end of the
                    // round, so only immediate (shared-variable) writes can
                    // change a slot under an in-place read.
                    constexpr bool immediate = std::is_same_v<T, WriteBlocking>
                        || std::is_same_v<T, WriteBlockingSlice>
                        || std::is_same_v<T, WriteBlockingDynamicSlice>
                        || std::is_same_v<T, WriteBlockingDynamicPartSlice>;
                    if constexpr (std::is_same_v<std::decay_t<decltype(op.signal)>,
                                      SignalId>
                        && !std::is_same_v<T, ReadSignal>) {
                        if (op.signal < family_of_signal_.size()
                            && family_of_signal_[op.signal] != no_slot) {
                            written_families.push_back(family_of_signal_[op.signal]);
                        }
                        if (op.signal < slot_of_signal_.size()
                            && slot_of_signal_[op.signal] != no_slot
                            && (!vhdl || immediate)) {
                            slot_writes.emplace_back(slot_of_signal_[op.signal], pc);
                        }
                    }
                }
                for (const auto reg : operand_reads) {
                    if (reg < register_count) {
                        other_use[reg] = 1U;
                    }
                }
            }, operation);
        }
        std::ranges::sort(written_families);
        std::vector<std::uint32_t> written_slots;
        written_slots.reserve(slot_writes.size());
        for (const auto& [slot, at] : slot_writes) {
            written_slots.push_back(slot);
        }
        std::ranges::sort(written_slots);
        const auto written_slot = [&](const std::uint32_t slot) {
            return std::ranges::binary_search(written_slots, slot);
        };
        const auto unwritten_between = [&](const std::uint32_t reg) {
            const auto from = definition_pc[reg];
            const auto to = last_use[reg];
            if (to <= from) {
                return false;
            }
            return std::ranges::none_of(slot_writes, [&](const auto& write) {
                       return write.first == field_slot[reg] && write.second > from
                           && write.second < to;
                   })
                && std::ranges::none_of(barriers, [&](const std::uint32_t pc) {
                       return pc >= from && pc <= to;
                   });
        };
        for (std::uint32_t reg = 0U; reg < register_count; ++reg) {
            if (field_slot[reg] != no_slot
                && (definitions[reg] != 1U || other_use[reg] != 0U
                    || (written_slot(field_slot[reg])
                        && !unwritten_between(reg)))) {
                field_slot[reg] = no_slot;
            }
        }
        // A leaf written by this member (directly or through the proxy)
        // or a barrier between the read and its last selection could
        // change what an in-place read sees.
        for (std::uint32_t reg = 0U; reg < register_count; ++reg) {
            if (field_family[reg] == no_slot) {
                continue;
            }
            const auto& family = families_[field_family[reg]];
            const bool leaf_written = std::ranges::any_of(family.leaves,
                [&](const FamilyLeaf& leaf) {
                    return written_slot(leaf.slot);
                });
            const auto from = definition_pc[reg];
            const auto to = last_use[reg];
            if (definitions[reg] != 1U || other_use[reg] != 0U || to <= from
                || std::ranges::binary_search(written_families, field_family[reg])
                || leaf_written
                || std::ranges::any_of(barriers, [&](const std::uint32_t pc) {
                       return pc >= from && pc <= to;
                   })) {
                field_family[reg] = no_slot;
            }
        }
    }
    // Wide moves (SystemVerilog): ReadSignal of a wide slot whose register
    // only feeds an optional DynamicPartSelect and then one write of a wide
    // slot, in straight-line code that does not write the source, run as
    // one word-level copy (KOp::wide_move).
    std::vector<std::uint8_t> fused_away(end - begin, 0U);
    std::vector<std::optional<KInst>> fused_move(end - begin);
    if (!vhdl) {
        std::vector<std::uint32_t> defs(register_count, 0U);
        std::vector<std::uint32_t> uses(register_count, 0U);
        std::vector<std::uint32_t> use_pc(register_count, 0U);
        for (std::uint32_t pc = begin; pc < end; ++pc) {
            if (skip[pc] != 0U) {
                continue;
            }
            operation_registers(member.operations()[pc], operand_reads,
                operand_write);
            if (operand_write && *operand_write < register_count) {
                ++defs[*operand_write];
            }
            for (const auto reg : operand_reads) {
                if (reg < register_count) {
                    ++uses[reg];
                    use_pc[reg] = pc;
                }
            }
        }
        const auto wide_slot = [&](const SignalId signal) -> std::uint32_t {
            if (signal >= slot_of_signal_.size()
                || family_of_signal_[signal] != no_slot
                || slot_of_signal_[signal] == no_slot) {
                return no_slot;
            }
            const auto slot = slot_of_signal_[signal];
            return slots_[slot].words > 1U && slots_[slot].planes == 2U
                ? slot : no_slot;
        };
        const auto single_use = [&](const RegisterId reg) {
            return reg < register_count && defs[reg] == 1U && uses[reg] == 1U
                && width[reg] > 64U && width[reg] != polymorphic;
        };
        // No control flow in (from, to) and no write there to `signal`.
        const auto straight = [&](const std::uint32_t from, const std::uint32_t to,
                                  const SignalId signal) {
            for (auto pc = from + 1U; pc < to; ++pc) {
                bool ok = true;
                visit_operation([&](const auto& op) {
                    using T = std::decay_t<decltype(op)>;
                    if constexpr (std::is_same_v<T, Branch> || std::is_same_v<T, Jump>
                        || std::is_same_v<T, Call> || std::is_same_v<T, Return>
                        || std::is_same_v<T, Halt>
                        || std::is_same_v<T, WaitSensitivity>
                        || behavioral_suspension<T>) {
                        ok = false;
                    } else if constexpr (requires { op.signal; }) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(op.signal)>,
                                          SignalId>
                            && !std::is_same_v<T, ReadSignal>) {
                            ok = op.signal != signal;
                        }
                    }
                }, member.operations()[pc]);
                if (!ok) {
                    return false;
                }
            }
            return true;
        };
        for (std::uint32_t read_pc = begin; read_pc < end; ++read_pc) {
            if (skip[read_pc] != 0U) {
                continue;
            }
            const auto* read = operation_get_if<ReadSignal>(
                &member.operations()[read_pc]);
            if (read == nullptr || read->kind != SignalReadKind::current
                || read->ticks != 1U || read->clock || read->gate
                || !single_use(read->destination)) {
                continue;
            }
            const auto source = wide_slot(read->signal);
            if (source == no_slot
                || slots_[source].width != width[read->destination]) {
                continue;
            }
            KInst move;
            move.op = KOp::wide_move;
            move.x = source;
            auto value = read->destination;
            auto write_pc = use_pc[value];
            std::uint32_t select_pc = no_slot;
            std::optional<DynamicPartIndex> selection;
            if (const auto* select = operation_get_if<DynamicPartSelect>(
                    &member.operations()[write_pc])) {
                if (select->source != value || !single_use(select->destination)
                    || select->width != width[select->destination]
                    || select->base >= register_count
                    || width[select->base] != 32U) {
                    continue;
                }
                select_pc = write_pc;
                move.sub = 1U;
                move.y = select->base;
                move.flags = static_cast<std::uint8_t>(
                    (select->increasing ? flag_increasing : 0U)
                    | (select->source_descending ? flag_descending : 0U)
                    | (select->two_state ? flag_two_state : 0U));
                selection.emplace();
                selection->left = select->left;
                selection->right = select->right;
                selection->base_offset = select->base_offset;
                selection->width = select->width;
                selection->increasing = select->increasing;
                selection->source_descending = select->source_descending;
                value = select->destination;
                write_pc = use_pc[value];
            }
            if (write_pc <= read_pc || write_pc >= end || skip[write_pc] != 0U) {
                continue;
            }
            bool matched = false;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                constexpr bool whole = std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteBlocking>;
                constexpr bool slice = std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteBlockingSlice>;
                if constexpr (whole || slice) {
                    if (op.source != value) {
                        return;
                    }
                    const auto target = wide_slot(op.signal);
                    // A resolved target is written through wide_store, which
                    // resolves each chunk (emit_wide_net_store); the read,
                    // the selection and the write then stay separate
                    // instructions.
                    if (target == no_slot || slots_[target].resolved) {
                        return;
                    }
                    std::uint32_t offset = 0U;
                    if constexpr (slice) {
                        offset = op.offset;
                    }
                    if (offset > slots_[target].width
                        || width[value] > slots_[target].width - offset
                        || (whole && width[value] != slots_[target].width)) {
                        return;
                    }
                    bool deferred = false;
                    if constexpr (std::is_same_v<T, WriteUpdate>
                        || std::is_same_v<T, WriteUpdateSlice>) {
                        if (op.domain == SignalUpdateDomain::generic) {
                            return;
                        }
                        deferred = op.domain == SignalUpdateDomain::systemverilog_nba;
                    }
                    move.d = target;
                    move.offset = offset;
                    move.width = width[value];
                    // Generated code moves to an unresolved slot chunk by
                    // chunk (emit_wide_move): imm_a the source's words per
                    // plane, imm_b the target's and, from bit 32, the
                    // source's width.
                    move.imm_a = slots_[move.x].words;
                    move.imm_b = slots_[target].words
                        | std::uint64_t { slots_[move.x].width } << 32U;
                    move.flags = static_cast<std::uint8_t>(
                        move.flags | (deferred ? flag_nba : 0U));
                    matched = true;
                }
            }, member.operations()[write_pc]);
            if (!matched || !straight(read_pc, write_pc, read->signal)) {
                continue;
            }
            if (selection) {
                // The fused move copies one field of the source: result bit
                // i is source bit first + i, which holds when the declared
                // right bound is the low bound of a descending range or the
                // high bound of an ascending one. Any other selection
                // (a reversed one) stays a wide_alu dynamic_part_select.
                const bool descending = (move.flags & flag_descending) != 0U;
                const auto low = std::min(selection->left, selection->right);
                const auto high = std::max(selection->left, selection->right);
                if (descending ? selection->right != low : selection->right != high) {
                    continue;
                }
            }
            if (select_pc != no_slot) {
                // The base must still hold its value at the write.
                bool base_kept = true;
                for (auto pc = select_pc + 1U; pc < write_pc; ++pc) {
                    operation_registers(member.operations()[pc], operand_reads,
                        operand_write);
                    base_kept = base_kept
                        && (!operand_write || *operand_write != move.y);
                }
                if (!base_kept) {
                    continue;
                }
                fused_away[select_pc - begin] = 1U;
                move.aux = static_cast<std::uint32_t>(body.parts.size());
                body.parts.push_back(*selection);
            }
            fused_away[read_pc - begin] = 1U;
            fused_move[write_pc - begin] = move;
        }
    }
    for (std::uint32_t pc = begin; pc < end; ++pc) {
        translating = pc;
        KInst inst;
        if (expanded && pc == stop) {
            // The body's end: the code before it falls through to here.
            inst.op = KOp::jump;
            inst.d = end - begin;
            body.code.push_back(inst);
            continue;
        }
        if (skip[pc] != 0U || fused_away[pc - begin] != 0U) {
            body.code.push_back(inst);
            continue;
        }
        if (fused_move[pc - begin]) {
            body.code.push_back(*fused_move[pc - begin]);
            continue;
        }
        {
            // Null-range values (IEEE 1076-2019 5.3.2.2: an array with a
            // null range has no elements): a register no operation gives a
            // width holds no bits, so an operation that only moves such
            // values around, or writes them to a signal without elements,
            // has no effect and compiles to nothing.
            const auto null = [&](const RegisterId reg) {
                return reg < register_count && null_register[reg] != 0U;
            };
            bool nothing = false;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, LoadConstant>) {
                    nothing = null(op.destination) && op.value.width() == 0U;
                } else if constexpr (std::is_same_v<T, ReadSignal>) {
                    nothing = null(op.destination)
                        && op.kind == SignalReadKind::current && !op.clock
                        && !op.gate && signal_width(op.signal) == 0U;
                } else if constexpr (std::is_same_v<T, CopyRegister>) {
                    nothing = null(op.destination) && null(op.source);
                } else if constexpr (std::is_same_v<T, Extract>) {
                    nothing = op.width == 0U && null(op.destination);
                } else if constexpr (std::is_same_v<T, Concatenate>) {
                    nothing = op.width == 0U && null(op.destination)
                        && std::ranges::all_of(op.operands, null);
                } else if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteProjected>) {
                    nothing = null(op.source) && op.signal < slot_of_signal_.size()
                        && signal_width(op.signal) == 0U;
                } else if constexpr (std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteProjectedSlice>) {
                    nothing = null(op.source) && op.signal < slot_of_signal_.size();
                }
            }, member.operations()[pc]);
            if (nothing) {
                body.code.push_back(inst);
                continue;
            }
        }
        if (vhdl) {
            // A projected assignment (IEEE 1076-2019 10.5.2.2): the kernel's
            // instruction, which reads its registers (sources, run-time
            // delays, a dynamic index) from the register file.
            std::vector<RegisterId> sources;
            bool projected = false;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, WriteProjected>
                    || std::is_same_v<T, WriteProjectedSlice>
                    || std::is_same_v<T, WriteProjectedDynamicSlice>) {
                    if (op.delay != 0U || op.rejection != 0U
                        || op.mode != ProjectedDelayMode::inertial
                        || member_projects(member_index, op.signal)) {
                        projected = true;
                        sources.push_back(op.source);
                        if constexpr (std::is_same_v<T, WriteProjectedDynamicSlice>) {
                            sources.push_back(op.selection.index);
                        }
                    }
                } else if constexpr (std::is_same_v<T, WriteProjectedWaveform>
                    || std::is_same_v<T, WriteProjectedWaveformSlice>
                    || std::is_same_v<T, WriteProjectedWaveformDynamicSlice>) {
                    projected = true;
                    for (const auto& element : op.elements) {
                        sources.push_back(element.source);
                        if (element.delay_source) {
                            sources.push_back(*element.delay_source);
                        }
                    }
                    if constexpr (std::is_same_v<T, WriteProjectedWaveformDynamicSlice>) {
                        sources.push_back(op.selection.index);
                    }
                }
            }, member.operations()[pc]);
            if (projected) {
                for (const auto source : sources) {
                    if (source >= register_count || width[source] == 0U
                        || width[source] == polymorphic) {
                        throw reject("projected_source");
                    }
                    has_wide = has_wide || width[source] > 64U;
                }
                inst.op = KOp::net_delay;
                inst.sub = 1U;
                inst.x = sources.front();
                inst.y = pc;
                inst.width = width[sources.front()];
                inst.aux = has_unknowns(sources.front()) ? 1U : 0U;
                body.code.push_back(inst);
                continue;
            }
        }
        if (const auto* delayed = operation_get_if<WriteNetDelay>(
                &member.operations()[pc])) {
            // An interconnect delay (IEEE 1364-2005 16.2.4) is the kernel's
            // own instruction in every member kind: it reads its source
            // (any width) from the register file and holds its pending
            // changes as kernel timers (StaticKernel::net_delay_write).
            if (delayed->source >= register_count || width[delayed->source] == 0U
                || width[delayed->source] == polymorphic) {
                throw reject("net_delay_source");
            }
            inst.op = KOp::net_delay;
            inst.x = delayed->source;
            inst.y = pc;
            inst.width = width[delayed->source];
            inst.aux = has_unknowns(delayed->source) ? 1U : 0U;
            has_wide = has_wide || width[delayed->source] > 64U;
            body.code.push_back(inst);
            continue;
        }
        if (behavioral) {
            // Suspensions return to the thread runner; runtime-library
            // operations run through the generic bridge; calls use the
            // synthetic stack; isolated nonrecursive frames compile away.
            bool translated = true;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (behavioral_suspension<T>
                    || std::is_same_v<T, WaitSensitivity>
                    || std::is_same_v<T, Halt>) {
                    inst.op = KOp::suspend;
                    inst.x = pc;
                    inst.sub = static_cast<std::uint8_t>(operation_kind_of<T>);
                    inst.aux = 1U;
                } else if constexpr (std::is_same_v<T, Display>) {
                    // An attempt-start report (Display::inert) runs only
                    // once a library has bound cbAssertionStart: the
                    // instruction tests the kernel's flag (aux 2) and skips
                    // the call while it is clear, at any time in the run.
                    if (op.inert) {
                        inst.op = KOp::generic;
                        inst.x = pc;
                        inst.y = 0U;
                        inst.aux = 2U;
                    } else {
                        inst.op = KOp::generic;
                        inst.x = pc;
                        inst.y = 0U;
                    }
                } else if constexpr (std::is_same_v<T, FormatDisplay>
                    || std::is_same_v<T, StringDisplay>
                    || std::is_same_v<T, TimeDisplay>
                    || std::is_same_v<T, Report>
                    || std::is_same_v<T, RandomValue>
                    || std::is_same_v<T, CountOnes>
                    || std::is_same_v<T, CountBits>
                    || std::is_same_v<T, ConcatenateStrings>
                    || std::is_same_v<T, LoadStringConstant>
                    || std::is_same_v<T, CopyStringRegister>
                    || std::is_same_v<T, PlusArgSelect>
                    || std::is_same_v<T, CodeCoverageHit>
                    || std::is_same_v<T, CodeCoverageToggle>) {
                    inst.op = KOp::generic;
                    inst.x = pc;
                    inst.y = 0U;
                    if constexpr (std::is_same_v<T, CodeCoverageToggle>) {
                        for (const auto sample : { op.previous, op.current }) {
                            if (sample >= register_count || width[sample] == 0U
                                || width[sample] == polymorphic) {
                                throw reject("toggle_sample");
                            }
                            has_wide = has_wide || width[sample] > 64U;
                        }
                    }
                    if constexpr (std::is_same_v<T, FormatDisplay>) {
                        if (op.source >= register_count || width[op.source] == 0U
                            || width[op.source] == polymorphic) {
                            throw reject("display_source");
                        }
                        has_wide = has_wide || width[op.source] > 64U;
                    }
                } else if constexpr (std::is_same_v<T, StringMethod>
                    || std::is_same_v<T, StringLength>
                    || std::is_same_v<T, StringIndex>
                    || std::is_same_v<T, CompareStrings>
                    || std::is_same_v<T, StringReplaceByte>
                    || std::is_same_v<T, StringReport>
                    || std::is_same_v<T, ReadStringObject>
                    || std::is_same_v<T, WriteStringObject>
                    || std::is_same_v<T, FileOpen>
                    || std::is_same_v<T, FileClose>
                    || std::is_same_v<T, FileWriteLiteral>
                    || std::is_same_v<T, FileWriteFormatted>
                    || std::is_same_v<T, FileWriteString>
                    || std::is_same_v<T, FileFlush>
                    || std::is_same_v<T, FileEndOfFile>
                    || std::is_same_v<T, FileReadLine>
                    || std::is_same_v<T, FileErrorStatus>
                    || std::is_same_v<T, FileScan>
                    || std::is_same_v<T, FilePosition>
                    || std::is_same_v<T, FileBinaryRead>) {
                    // String and file operations run through the generic
                    // bridge on the thread's reference state (strings and the
                    // file table are the host's): their numeric registers
                    // must have a known width, those a file operation
                    // writes besides its result included.
                    std::vector<RegisterId> operands;
                    std::optional<RegisterId> result;
                    operation_registers_of(op, operands, result);
                    std::vector<WrittenRegister> written;
                    operation_written_registers_of(op, written);
                    for (const auto& output : written) {
                        operands.push_back(output.reg);
                    }
                    for (const auto operand : operands) {
                        if (operand >= register_count || width[operand] == 0U
                            || width[operand] == polymorphic) {
                            throw reject("generic_operand");
                        }
                        has_wide = has_wide || width[operand] > 64U;
                    }
                    inst.op = KOp::generic;
                    inst.x = pc;
                    inst.y = 0U;
                } else if constexpr (std::is_same_v<T, DebugPoint>) {
                    // Debugger plan: a statement, call, wait, assertion or
                    // entry point returns to the thread runner like a
                    // suspension, which raises the execution-point hook
                    // there and may stop the run. Block markers and every
                    // point of a normal plan compile to nothing.
                    if (debug_points_ && debug_point_executable(op.kind)) {
                        inst.op = KOp::suspend;
                        inst.x = pc;
                        inst.sub = static_cast<std::uint8_t>(operation_kind_of<T>);
                        inst.aux = 1U;
                    } else {
                        inst.op = KOp::nop;
                    }
                } else if constexpr (std::is_same_v<T, CallableFramePush>
                    || std::is_same_v<T, CallableFramePop>) {
                    // Isolated; a recursive callable's frames are real
                    // (frame_push, frame_pop; with the runtime's part when
                    // they list strings, containers or private arrays), and
                    // other frames that save private arrays run through the
                    // generic bridge.
                    if (body.frame_stack_base != 0U) {
                        if constexpr (std::is_same_v<T, CallableFramePush>) {
                            lower_frame(inst, op.identity, true, { }, pc);
                        } else {
                            lower_frame(inst, op.identity, false,
                                std::vector<RegisterId>(op.preserve_packed.begin(),
                                    op.preserve_packed.end()),
                                pc);
                        }
                    } else if (member.private_frame(op.identity)) {
                        inst.op = KOp::generic;
                        inst.x = pc;
                        inst.y = 0U;
                    } else {
                        inst.op = KOp::nop;
                    }
                } else if constexpr (std::is_same_v<T, Call>) {
                    inst.op = KOp::call;
                    inst.d = map_target(op.target);
                    if (op.stack.capacity == 0U) {
                        // The body's growable stack: x is the depth word.
                        inst.x = body.call_stack_base;
                        inst.y = body.call_stack_base;
                        inst.z = 1U;
                        inst.flags = flag_linear;
                    } else {
                        inst.x = narrow(op.stack.pointer);
                        inst.y = op.stack.entries;
                        inst.z = op.stack.capacity;
                    }
                    inst.imm_a = op.return_target;
                    body.return_targets.push_back(op.return_target);
                } else if constexpr (std::is_same_v<T, Return>) {
                    inst.op = KOp::ret;
                    if (op.stack.capacity == 0U) {
                        inst.x = body.call_stack_base;
                        inst.y = body.call_stack_base;
                        inst.z = 1U;
                        inst.flags = flag_linear;
                    } else {
                        inst.x = narrow(op.stack.pointer);
                        inst.y = op.stack.entries;
                        inst.z = op.stack.capacity;
                    }
                } else {
                    translated = false;
                }
            }, member.operations()[pc]);
            if (translated) {
                body.code.push_back(inst);
                continue;
            }
        }
        // Whole-value operations on chunked wide registers (KOp::wide_load
        // .. wide_part_insert, wide_alu); anything else on them runs
        // generically. A thread lowers them as a member does: its registers
        // are the compiled body's (thread_register reads the chunks) and its
        // signal accesses bind slots like a member's.
        {
            bool lowered = false;
            const auto enabled = [&](const std::uint32_t bit) {
                return ((wide_mask >> bit) & 1U) != 0U;
            };
            const auto narrow_operand = [&](const RegisterId reg) {
                return reg < register_count && width[reg] != 0U
                    && width[reg] <= 64U;
            };
            // Operand slot `slot` (0 d, 1 x, 2 y, 3 z) of the wide_alu
            // instruction `alu` names `reg`.
            const auto place_alu = [&](KInst& alu, const std::uint32_t slot,
                                       const RegisterId reg) {
                const bool wide = chunked(reg);
                alu.aux |= (wide ? wide_alu_wide : 0U) << slot;
                if (wide && has_unknowns(reg)) {
                    alu.aux |= wide_alu_masks << slot;
                }
                if (kind_of(reg) == ValueKind::logic9) {
                    alu.aux |= wide_alu_logic9 << slot;
                }
                (slot == 0U ? alu.d : slot == 1U ? alu.x
                        : slot == 2U ? alu.y : alu.z)
                    = reg;
            };
            // An owned plain slot of `count` bits at `offset`.
            // A read may also take the mirror of a host signal (mirror_of_
            // signal_: a slot the kernel refreshes between activations, as
            // the narrow reads of a mirrored signal take); a write never.
            const auto plain_slot = [&](const SignalId signal,
                                        const std::uint32_t offset,
                                        const std::uint32_t count,
                                        const bool whole,
                                        const bool mirrored = false) -> std::uint32_t {
                if (signal >= slot_of_signal_.size()
                    || family_of_signal_[signal] != no_slot) {
                    return no_slot;
                }
                auto slot = slot_of_signal_[signal];
                if (slot == no_slot && mirrored
                    && mirror_of_signal_[signal] != no_slot) {
                    slot = mirror_of_signal_[signal];
                }
                if (slot == no_slot) {
                    return no_slot;
                }
                const auto& state = slots_[slot];
                if (offset > state.width || count > state.width - offset
                    || (whole && count != state.width)) {
                    return no_slot;
                }
                return slot;
            };
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (enabled(0U) && chunked(op.destination)
                        && sampled_read(op) && inline_sampled_read(op)) {
                        // A wide $sampled or $past (IEEE 1800-2023 16.5.1,
                        // 16.9.3): the chunks of the Preponed value (sub 4,
                        // x its words in Impl::sampled_value_words) or of a
                        // history ring entry (sub 5, x the ring's start,
                        // offset the read kind, z the $past depth, imm_a the
                        // chunks and, from bit 32, 1 for a Logic9 ring,
                        // imm_b the default's constants), coerced to Logic4.
                        sva_table_used_ = true;
                        inst.op = KOp::wide_load;
                        inst.d = op.destination;
                        inst.width = width[op.destination];
                        inst.aux = has_unknowns(op.destination) ? 1U : 0U;
                        inst.imm_a = wide_chunks(inst.width);
                        if (op.kind == SignalReadKind::sampled && !op.clock) {
                            inst.sub = 4U;
                            inst.x = impl_.sampled_value_word_offsets[op.signal];
                            lowered = true;
                            return;
                        }
                        const auto ring = impl_.planned_sampled_ring_offset(
                            Impl::SampledHistoryKey { op.signal, op.clock,
                                op.clock_edge, op.gate,
                                ProcessSchedulingDomain::systemverilog });
                        if (!ring) {
                            throw reject("sampled_ring");
                        }
                        impl_.sampled_ring_layout_baked = true;
                        inst.sub = 5U;
                        inst.x = static_cast<std::uint32_t>(*ring);
                        inst.offset = op.kind == SignalReadKind::past ? 1U : 0U;
                        inst.z = op.ticks;
                        if (impl_.get_signal(op.signal).initial_value.is_logic9()) {
                            inst.imm_a |= std::uint64_t { 1 } << 32U;
                        }
                        inst.imm_b = sampled_default_constants(op.signal);
                        lowered = true;
                        return;
                    }
                    if (!enabled(0U) || !chunked(op.destination)
                        || sampled_read(op)) {
                        return;
                    }
                    const auto slot = plain_slot(op.signal, 0U,
                        width[op.destination], true, true);
                    // A host signal without a mirror (one a member writes
                    // blockingly, so the host's value is current at every
                    // read): sub 3 copies its chunks into the register file
                    // (execute_wide), exact codes into a register with masks
                    // (as load_host sub 1).
                    if (slot == no_slot
                        && (op.signal >= slot_of_signal_.size()
                            || family_of_signal_[op.signal] != no_slot
                            || slot_of_signal_[op.signal] != no_slot
                            || signal_width(op.signal) != width[op.destination])) {
                        return;
                    }
                    inst.op = KOp::wide_load;
                    inst.d = op.destination;
                    inst.width = width[op.destination];
                    // The value read is coerced to the register's kind.
                    inst.aux = has_unknowns(op.destination) ? 1U : 0U;
                    if (slot == no_slot) {
                        inst.x = op.signal;
                        inst.sub = 3U;
                        lowered = true;
                        return;
                    }
                    inst.x = slot;
                    inst.imm_a = slots_[slot].words;
                    if (slots_[slot].planes != 2U) {
                        inst.sub = kind_of(op.destination) == ValueKind::logic9
                            ? 1U : 2U;
                    }
                    lowered = true;
                } else if constexpr (std::is_same_v<T, CopyRegister>) {
                    if (!enabled(1U)) {
                        return;
                    }
                    // The width fixpoint gives a copy's destination its
                    // source's width (a register of several widths is
                    // polymorphic and split by split_polymorphic_calls), so a
                    // chunked destination has a chunked source of its width;
                    // anything else would be a compiler error, reported
                    // rather than run on the reference.
                    if (op.destination >= register_count || op.source >= register_count
                        || (width[op.destination] <= 64U && width[op.source] <= 64U)) {
                        return;
                    }
                    if (!chunked(op.destination) || !chunked(op.source)
                        || width[op.destination] != width[op.source]) {
                        if (width[op.destination] == polymorphic
                            || width[op.source] == polymorphic) {
                            return;
                        }
                        throw reject("copy_shape");
                    }
                    // The copy is coerced to the destination's kind: the
                    // masks survive only into a Logic9 register ('U' reads
                    // as X, 'H' and 'L' as 1 and 0 without them).
                    const auto chunks = wide_chunks(width[op.destination]);
                    const bool to = has_unknowns(op.destination);
                    const bool keep = to && has_unknowns(op.source)
                        && kind_of(op.destination) == ValueKind::logic9;
                    inst.op = KOp::wide_copy;
                    inst.d = op.destination;
                    inst.x = op.source;
                    inst.width = width[op.destination];
                    inst.aux = keep ? 2U * chunks : chunks;
                    inst.imm_a = to && !keep ? chunks : 0U;
                    // sub 1: a Logic4 destination reads the source's weak
                    // elements through coerce_weak ('W' and '-' as X).
                    inst.sub = !keep && has_unknowns(op.source) ? 1U : 0U;
                    lowered = true;
                } else if constexpr (std::is_same_v<T, LoadConstant>) {
                    if (!enabled(2U) || !chunked(op.destination)
                        || op.value.width() != width[op.destination]) {
                        return;
                    }
                    const auto chunks = wide_chunks(width[op.destination]);
                    const bool masks = has_unknowns(op.destination);
                    const auto value = Impl::coerce_value_kind(op.value,
                        four_state_constant(op) ? ValueKind::logic4
                                                : kind_of(op.destination));
                    std::vector<Word> words(masks ? 2U * chunks : chunks);
                    for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                        const auto field_width
                            = std::min(64U, width[op.destination] - 64U * chunk);
                        const auto field = value.extract_bits(64U * chunk, field_width);
                        const auto word = exact_uword(field);
                        words[chunk] = word->value;
                        if (masks) {
                            words[chunks + chunk] = { word->unknown, word->weak };
                        }
                    }
                    inst.op = KOp::wide_const;
                    inst.d = op.destination;
                    inst.width = width[op.destination];
                    inst.aux = static_cast<std::uint32_t>(words.size());
                    inst.imm_a = body.wide_constants.size();
                    body.wide_constants.insert(body.wide_constants.end(),
                        words.begin(), words.end());
                    lowered = true;
                } else if constexpr (std::is_same_v<T, Concatenate>) {
                    if (!enabled(3U) || !chunked(op.destination)
                        || op.width != width[op.destination]) {
                        return;
                    }
                    const bool masks = has_unknowns(op.destination);
                    std::uint32_t total = 0U;
                    std::uint32_t kept = 0U;
                    for (const auto reg : op.operands) {
                        // A null-range operand contributes no bits.
                        if (reg < register_count && null_register[reg] != 0U) {
                            continue;
                        }
                        // A Logic9 operand of a Logic4 result is read
                        // through coerce_weak (emit_wide_concat).
                        if (!(narrow_operand(reg) || chunked(reg))) {
                            // An operand read in place is a field of its
                            // slot (field_slot, only Extract and the
                            // dynamic selections use one); a register of
                            // several widths is split before this point.
                            if (reg >= register_count || width[reg] == polymorphic
                                || width[reg] <= 64U) {
                                return;
                            }
                            throw reject("concat_operand");
                        }
                        total += width[reg];
                        ++kept;
                    }
                    if (total != op.width || kept == 0U) {
                        return;
                    }
                    inst.op = KOp::wide_concat;
                    inst.d = op.destination;
                    inst.x = kept;
                    inst.aux = static_cast<std::uint32_t>(body.concat.size());
                    inst.width = op.width;
                    inst.sub = masks ? 1U : 0U;
                    for (const auto reg : op.operands) {
                        if (reg < register_count && null_register[reg] != 0U) {
                            continue;
                        }
                        body.concat.push_back({ reg, width[reg],
                            width[reg] > 64U && has_unknowns(reg) ? 1U : 0U });
                    }
                    lowered = true;
                } else if constexpr (std::is_same_v<T, DynamicPartInsert>) {
                    if (!enabled(4U) || !chunked(op.destination) || !chunked(op.target)
                        || width[op.destination] != width[op.target]
                        || has_unknowns(op.destination) != has_unknowns(op.target)
                        || !narrow_operand(op.source)
                        || width[op.source] != op.selection.width
                        || !narrow_operand(op.selection.base)
                        || width[op.selection.base] != 32U
                        || (!has_unknowns(op.destination) && has_unknowns(op.source))) {
                        return;
                    }
                    inst.op = KOp::wide_part_insert;
                    inst.d = op.destination;
                    inst.x = op.target;
                    inst.y = op.source;
                    inst.z = op.selection.base;
                    inst.width = width[op.destination];
                    inst.imm_a = has_unknowns(op.destination) ? 1U : 0U;
                    inst.aux = static_cast<std::uint32_t>(body.parts.size());
                    body.parts.push_back(op.selection);
                    lowered = true;
                } else if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteProjected>
                    || std::is_same_v<T, WriteProjectedSlice>) {
                    constexpr bool blocking = std::is_same_v<T, WriteBlocking>
                        || std::is_same_v<T, WriteBlockingSlice>;
                    constexpr bool slice = std::is_same_v<T, WriteBlockingSlice>
                        || std::is_same_v<T, WriteUpdateSlice>
                        || std::is_same_v<T, WriteProjectedSlice>;
                    constexpr bool projected = std::is_same_v<T, WriteProjected>
                        || std::is_same_v<T, WriteProjectedSlice>;
                    if (!enabled(5U) || !chunked(op.source)) {
                        return;
                    }
                    std::uint32_t offset = 0U;
                    if constexpr (slice) {
                        offset = op.offset;
                    }
                    const auto slot = plain_slot(op.signal, offset, width[op.source],
                        !slice);
                    if (slot == no_slot) {
                        // A host signal's write: the register file's chunks
                        // become the host's value (wide_store sub 3,
                        // execute_wide), a blocking one at once, the others
                        // staged in the member's domain, as store_host does
                        // for a narrow value.
                        if (op.signal >= slot_of_signal_.size()
                            || slot_of_signal_[op.signal] != no_slot
                            || family_of_signal_[op.signal] != no_slot) {
                            return;
                        }
                        SignalUpdateDomain domain = blocking
                            ? SignalUpdateDomain::systemverilog_active
                            : SignalUpdateDomain::generic;
                        if constexpr (std::is_same_v<T, WriteUpdate>
                            || std::is_same_v<T, WriteUpdateSlice>) {
                            domain = op.domain;
                        }
                        inst.op = KOp::wide_store;
                        inst.sub = 3U;
                        inst.x = op.source;
                        inst.d = op.signal;
                        inst.offset = offset;
                        inst.width = width[op.source];
                        inst.aux = has_unknowns(op.source) ? 1U : 0U;
                        inst.flags = static_cast<std::uint8_t>(
                            (blocking ? flag_blocking : 0U)
                            | (slice ? flag_linear : 0U));
                        inst.imm_b = static_cast<std::uint64_t>(domain);
                        lowered = true;
                        return;
                    }
                    if (vhdl) {
                        inst.sub = 2U;
                        inst.flags = blocking ? flag_blocking : 0U;
                        if (const auto load = net_constant_load(pc)) {
                            // As the narrow assignment of such a constant.
                            inst.op = KOp::store_vhdl;
                            inst.x = op.source;
                            inst.d = slot;
                            inst.offset = offset;
                            inst.width = width[op.source];
                            inst.imm_a = net_constant_codes(*load);
                            net_store(inst, slot);
                            lowered = true;
                            return;
                        }
                    } else {
                        // As KOp::wide_move: Logic4 slots; an update in the
                        // generic domain stays generic.
                        // A register with masks (a nine-value operand read
                        // from a VHDL-driven net) stores its four-state value
                        // with 'W' and '-' read as X ('U' already is, in the
                        // word): the chunk store applies the weak mask
                        // (kernel_word::coerce_weak).
                        // An update in the generic domain stores at once, as
                        // the narrow store_slot of every domain but the NBA
                        // one does. A nine-value slot takes the codes of the
                        // value (sub 2 with the value's masks, as a shared
                        // variable or a queued VHDL assignment stores them:
                        // at once, flag_blocking, or deferred for the NBA
                        // domain); a resolved one is not done here.
                        if (projected) {
                            return;
                        }
                        bool deferred = false;
                        if constexpr (std::is_same_v<T, WriteUpdate>
                            || std::is_same_v<T, WriteUpdateSlice>) {
                            deferred = op.domain
                                == SignalUpdateDomain::systemverilog_nba;
                        }
                        if (slots_[slot].planes != 2U) {
                            if (slots_[slot].resolved) {
                                return;
                            }
                            inst.sub = 2U;
                            inst.flags = deferred ? 0U : flag_blocking;
                        } else {
                            inst.sub = deferred ? 1U : 0U;
                        }
                    }
                    inst.op = KOp::wide_store;
                    inst.x = op.source;
                    inst.d = slot;
                    inst.offset = offset;
                    inst.width = width[op.source];
                    inst.aux = has_unknowns(op.source) ? 1U : 0U;
                    // Generated code stores a SystemVerilog blocking write
                    // chunk by chunk (emit_wide_slot_store): imm_b the
                    // slot's words per plane.
                    inst.imm_b = !vhdl && inst.sub == 0U ? slots_[slot].words : 0U;
                    if ((vhdl || inst.sub == 2U) && (inst.flags & flag_blocking) != 0U) {
                        // A shared variable (emit_wide_shared_store): the
                        // slot's words per plane and, from bit 32, its
                        // planes.
                        inst.imm_b = slots_[slot].words
                            | std::uint64_t { slots_[slot].planes } << 32U;
                    }
                    if (slots_[slot].resolved && (inst.flags & flag_blocking) == 0U
                        && inst.sub != 1U) {
                        // Generated code stores each chunk of a resolved
                        // slot (emit_wide_net_store): imm_a the slot's words
                        // per plane and, from bit 32, its width; imm_b as
                        // for store_slot (net_store).
                        inst.imm_a = slots_[slot].words
                            | std::uint64_t { slots_[slot].width } << 32U;
                        net_store(inst, slot);
                        inst.aux = has_unknowns(op.source) ? 1U : 0U;
                    }
                    lowered = true;
                } else if constexpr (std::is_same_v<T, Extract>
                    || std::is_same_v<T, Insert>
                    || std::is_same_v<T, DynamicInsert>) {
                    // Fields of wide registers (KOp::wide_alu extract, insert
                    // and dynamic_insert): a wide destination; the
                    // narrow-destination Extract is KOp::extract sub 3.
                    if (!enabled(6U) || !chunked(op.destination)) {
                        return;
                    }
                    KInst alu;
                    alu.op = KOp::wide_alu;
                    if constexpr (std::is_same_v<T, Extract>) {
                        if (!chunked(op.source) || op.width != width[op.destination]
                            || op.offset > width[op.source]
                            || op.width > width[op.source] - op.offset) {
                            return;
                        }
                        alu.offset = static_cast<std::uint32_t>(WideFamily::extract);
                        alu.width = op.width;
                        alu.imm_a = op.offset;
                        alu.imm_b = width[op.source];
                        place_alu(alu, 0U, op.destination);
                        place_alu(alu, 1U, op.source);
                    } else {
                        const auto target_width = width[op.destination];
                        if (!chunked(op.target) || width[op.target] != target_width
                            || !(narrow_operand(op.source) || chunked(op.source))
                            || width[op.source] > target_width) {
                            return;
                        }
                        alu.width = target_width;
                        alu.imm_a = width[op.source];
                        place_alu(alu, 0U, op.destination);
                        place_alu(alu, 1U, op.target);
                        place_alu(alu, 2U, op.source);
                        if constexpr (std::is_same_v<T, Insert>) {
                            if (op.offset > target_width
                                || width[op.source] > target_width - op.offset) {
                                return;
                            }
                            alu.offset = static_cast<std::uint32_t>(WideFamily::insert);
                            alu.imm_b = op.offset;
                        } else {
                            if (!narrow_operand(op.selection.index)
                                || width[op.selection.index] != 32U) {
                                return;
                            }
                            alu.offset = static_cast<std::uint32_t>(
                                WideFamily::dynamic_insert);
                            alu.flags = op.selection.strict ? flag_strict : 0U;
                            place_alu(alu, 3U, op.selection.index);
                            alu.imm_b = body.indices.size();
                            body.indices.push_back(op.selection);
                        }
                    }
                    inst = alu;
                    lowered = true;
                } else if constexpr (std::is_same_v<T, ContainerRead>
                    || std::is_same_v<T, ContainerWrite>
                    || std::is_same_v<T, WriteContainerObjectElement>) {
                    // An element of more than 64 bits of a kernel-held array
                    // (IEEE 1800-2023 7.4, 7.9): KOp::mem_read and mem_write
                    // sub 1, whose chunks generated code loops over
                    // (emit_wide_mem_read, emit_wide_mem_write).
                    RegisterId value_register { };
                    if constexpr (std::is_same_v<T, ContainerRead>) {
                        value_register = op.destination;
                    } else {
                        value_register = op.source;
                    }
                    if (!enabled(6U) || !chunked(value_register)) {
                        return;
                    }
                    std::uint32_t container = no_container;
                    if constexpr (std::is_same_v<T, ContainerRead>) {
                        if (op.source >= container_of_register.size()
                            || op.string_index || !narrow_operand(op.index)) {
                            return;
                        }
                        container = container_of_register[op.source];
                    } else if constexpr (std::is_same_v<T, ContainerWrite>) {
                        if (op.target >= container_of_register.size() || op.string_index
                            || !narrow_operand(op.index)) {
                            return;
                        }
                        container = container_of_register[op.target];
                        if (container != no_container
                            && containers_[container].object != no_container_object) {
                            return;
                        }
                    } else {
                        if (op.object >= container_of_object_.size()
                            || op.transaction_signal || !op.levels.empty()
                            || !narrow_operand(op.index)
                            || (op.dynamic_part
                                && !(narrow_operand(op.dynamic_part->base)
                                    && width[op.dynamic_part->base] == 32U))) {
                            return;
                        }
                        container = container_of_object_[op.object];
                    }
                    if (container == no_container) {
                        return;
                    }
                    const auto& type = containers_[container].type;
                    if (type.associative || !type.fixed) {
                        return;
                    }
                    if constexpr (std::is_same_v<T, ContainerRead>) {
                        if (type.element_width != width[op.destination]) {
                            return;
                        }
                        inst.op = KOp::mem_read;
                        inst.sub = 1U;
                        inst.d = op.destination;
                        inst.x = container;
                        inst.y = op.index;
                        inst.width = width[op.index];
                        inst.offset = width[op.destination];
                        inst.aux = has_unknowns(op.destination) ? 1U : 0U;
                        inst.flags = op.linear_index ? flag_linear : 0U;
                    } else {
                        inst.op = KOp::mem_write;
                        inst.sub = 1U;
                        inst.d = container;
                        inst.x = op.source;
                        inst.y = op.index;
                        inst.width = width[op.source];
                        inst.offset = width[op.index];
                        inst.imm_b = has_unknowns(op.source) ? 1U : 0U;
                        inst.flags = static_cast<std::uint8_t>(
                            (op.linear_index ? flag_linear : 0U)
                            | (op.signed_index ? flag_signed : 0U));
                        if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
                            inst.flags = static_cast<std::uint8_t>(inst.flags
                                | (op.nonblocking ? flag_nba : 0U));
                            if (op.dynamic_part) {
                                inst.z = op.dynamic_part->base;
                                inst.aux = static_cast<std::uint32_t>(body.parts.size()) + 1U;
                                body.parts.push_back(*op.dynamic_part);
                            }
                        }
                    }
                    lowered = true;
                } else if constexpr (std::is_same_v<T, DynamicPartSelect>) {
                    // A part select with a wide result of a wide register
                    // (KOp::wide_alu dynamic_part_select; a result of at
                    // most 64 bits is KOp::dynamic_part_select).
                    if (!enabled(6U) || op.width <= 64U || !chunked(op.destination)
                        || !chunked(op.source) || width[op.destination] != op.width
                        || has_unknowns(op.destination) != has_unknowns(op.source)
                        || !narrow_operand(op.base) || width[op.base] != 32U) {
                        return;
                    }
                    KInst alu;
                    alu.op = KOp::wide_alu;
                    alu.offset = static_cast<std::uint32_t>(
                        WideFamily::dynamic_part_select);
                    alu.width = op.width;
                    alu.imm_a = width[op.source];
                    alu.imm_b = body.parts.size();
                    alu.flags = static_cast<std::uint8_t>(
                        (op.increasing ? flag_increasing : 0U)
                        | (op.source_descending ? flag_descending : 0U)
                        | (op.two_state ? flag_two_state : 0U));
                    place_alu(alu, 0U, op.destination);
                    place_alu(alu, 1U, op.source);
                    place_alu(alu, 2U, op.base);
                    DynamicPartIndex part;
                    part.left = op.left;
                    part.right = op.right;
                    part.base_offset = op.base_offset;
                    part.width = op.width;
                    part.increasing = op.increasing;
                    part.source_descending = op.source_descending;
                    body.parts.push_back(part);
                    inst = alu;
                    lowered = true;
                } else if constexpr (std::is_same_v<T, Binary>
                    || std::is_same_v<T, Shift>
                    || std::is_same_v<T, UnaryNot>
                    || std::is_same_v<T, Reduction>
                    || std::is_same_v<T, ConditionalSelect>
                    || std::is_same_v<T, LogicalNot>
                    || std::is_same_v<T, LogicalBinary>
                    || std::is_same_v<T, ConvertToTwoState>) {
                    // Arithmetic and logic on chunked wide registers
                    // (KOp::wide_alu). Operands the lowering cannot take
                    // (an unfixed width, a width mismatch the reference
                    // reports) stay generic.
                    if (!enabled(6U)) {
                        return;
                    }
                    KInst alu;
                    // Operand slot `slot` (0 d, 1 x, 2 y, 3 z) names `reg`.
                    const auto place_operand = [&](const std::uint32_t slot,
                                                   const RegisterId reg) {
                        place_alu(alu, slot, reg);
                    };
                    const auto scalar = [&](const RegisterId reg) {
                        return narrow_operand(reg) && width[reg] == 1U;
                    };
                    alu.op = KOp::wide_alu;
                    if constexpr (std::is_same_v<T, Binary>) {
                        constexpr auto first = BinaryOperator::equal;
                        constexpr auto last = BinaryOperator::greater_equal_signed;
                        // The matching equality (IEEE 1076-2019 9.2.3) is a
                        // comparison too: a scalar, 'U' when an element pair
                        // of 'U' decides it (emit_wide_binary).
                        const bool comparison = (op.operation >= first
                            && op.operation <= last)
                            || op.operation == BinaryOperator::vhdl_match_equal;
                        if (!chunked(op.lhs) || !chunked(op.rhs)
                            || width[op.lhs] != width[op.rhs]
                            || (comparison ? !scalar(op.destination)
                                           : !chunked(op.destination)
                                    || width[op.destination] != width[op.lhs])) {
                            return;
                        }
                        alu.offset = static_cast<std::uint32_t>(WideFamily::binary);
                        alu.sub = static_cast<std::uint8_t>(op.operation);
                        alu.width = width[op.lhs];
                        place_operand(0U, op.destination);
                        place_operand(1U, op.lhs);
                        place_operand(2U, op.rhs);
                    } else if constexpr (std::is_same_v<T, Shift>) {
                        if (!chunked(op.value) || !chunked(op.destination)
                            || width[op.value] != width[op.destination]
                            || !(narrow_operand(op.amount) || chunked(op.amount))) {
                            return;
                        }
                        alu.offset = static_cast<std::uint32_t>(WideFamily::shift);
                        alu.sub = static_cast<std::uint8_t>(op.operation);
                        alu.width = width[op.value];
                        alu.imm_a = width[op.amount];
                        alu.flags = op.signed_amount ? flag_signed : 0U;
                        place_operand(0U, op.destination);
                        place_operand(1U, op.value);
                        place_operand(2U, op.amount);
                    } else if constexpr (std::is_same_v<T, UnaryNot>
                        || std::is_same_v<T, ConvertToTwoState>) {
                        if (!chunked(op.source) || !chunked(op.destination)
                            || width[op.source] != width[op.destination]) {
                            return;
                        }
                        alu.offset = static_cast<std::uint32_t>(
                            std::is_same_v<T, UnaryNot> ? WideFamily::unary_not
                                                        : WideFamily::two_state);
                        alu.width = width[op.source];
                        place_operand(0U, op.destination);
                        place_operand(1U, op.source);
                    } else if constexpr (std::is_same_v<T, Reduction>
                        || std::is_same_v<T, LogicalNot>) {
                        if (!chunked(op.source) || !scalar(op.destination)) {
                            return;
                        }
                        alu.offset = static_cast<std::uint32_t>(
                            std::is_same_v<T, Reduction> ? WideFamily::reduce
                                                         : WideFamily::logical_not);
                        if constexpr (std::is_same_v<T, Reduction>) {
                            alu.sub = static_cast<std::uint8_t>(op.operation);
                        }
                        alu.width = width[op.source];
                        place_operand(0U, op.destination);
                        place_operand(1U, op.source);
                    } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
                        if (!scalar(op.condition) || !chunked(op.when_true)
                            || !chunked(op.when_false) || !chunked(op.destination)
                            || width[op.when_true] != width[op.when_false]
                            || width[op.destination] != width[op.when_true]) {
                            return;
                        }
                        alu.offset = static_cast<std::uint32_t>(
                            WideFamily::conditional);
                        alu.width = width[op.when_true];
                        alu.imm_b = 1U;
                        place_operand(0U, op.destination);
                        place_operand(1U, op.condition);
                        place_operand(2U, op.when_true);
                        place_operand(3U, op.when_false);
                    } else {
                        // LogicalBinary: any widths, one operand at least wide.
                        if (!scalar(op.destination)
                            || !(narrow_operand(op.lhs) || chunked(op.lhs))
                            || !(narrow_operand(op.rhs) || chunked(op.rhs))
                            || !(chunked(op.lhs) || chunked(op.rhs))) {
                            return;
                        }
                        alu.offset = static_cast<std::uint32_t>(
                            WideFamily::logical_binary);
                        alu.sub = static_cast<std::uint8_t>(op.operation);
                        alu.width = width[op.lhs];
                        alu.imm_a = width[op.rhs];
                        place_operand(0U, op.destination);
                        place_operand(1U, op.lhs);
                        place_operand(2U, op.rhs);
                    }
                    if constexpr (std::is_same_v<T, Binary>
                        || std::is_same_v<T, Shift>
                        || std::is_same_v<T, UnaryNot>
                        || std::is_same_v<T, Reduction>
                        || std::is_same_v<T, LogicalNot>
                        || std::is_same_v<T, ConvertToTwoState>) {
                        alu.imm_b = alu.width;
                    }
                    if constexpr (std::is_same_v<T, LogicalBinary>) {
                        alu.imm_b = width[op.lhs];
                    }
                    inst = alu;
                    lowered = true;
                }
            }, member.operations()[pc]);
            if (lowered) {
                body.code.push_back(inst);
                continue;
            }
        }
        // Operations on values wider than 64 bits, wide or proxy slots, and
        // dynamic host writes run through the reference value functions.
        {
            const auto& operation = member.operations()[pc];
            operation_registers(operation, operand_reads, operand_write);
            bool generic = false;
            // Narrow selections may read a wide register directly.
            std::optional<RegisterId> wide_source;
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Extract>
                    || std::is_same_v<T, DynamicPartSelect>
                    || std::is_same_v<T, DynamicExtract>) {
                    if (op.source < register_count && width[op.source] > 64U
                        && width[op.source] != polymorphic
                        && field_slot[op.source] == no_slot
                        && field_family[op.source] == no_slot
                        && op.destination < register_count
                        && width[op.destination] <= 64U) {
                        wide_source = op.source;
                    }
                } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
                    || std::is_same_v<T, WriteUpdateDynamicSlice>
                    || std::is_same_v<T, WriteProjectedDynamicSlice>
                    || std::is_same_v<T, WriteBlockingDynamicPartSlice>
                    || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                    // A host signal takes a chunked wide value whole through
                    // the host (wide_store sub 3, host_dynamic_write).
                    if (op.signal < family_of_signal_.size()
                        && family_of_signal_[op.signal] == no_slot
                        && !owned(op.signal) && chunked(op.source)) {
                        wide_source = op.source;
                    }
                } else if constexpr (std::is_same_v<T, SvaOperation>) {
                    // A tick reads more than 64 guards from the chunks of
                    // one wide register (KOp::sva_tick; IEEE 1800-2023
                    // 16.14).
                    if (op.kind == SvaKind::tick && op.guard_count > 64U
                        && chunked(op.guards)) {
                        wide_source = op.guards;
                    }
                    // A local variable wider than 64 bits (16.10) is read
                    // into, or answered from, the chunks of one register.
                    if (op.kind == SvaKind::local && chunked(op.result)) {
                        wide_source = op.result;
                    }
                    if (op.kind == SvaKind::answer && chunked(op.guards)) {
                        wide_source = op.guards;
                    }
                }
            }, operation);
            // A concatenation's null-range operands contribute no bits
            // (they are dropped where it lowers).
            const bool null_operands = operation_holds<Concatenate>(operation);
            const auto check = [&](const RegisterId reg) {
                if (null_operands && reg < register_count && null_register[reg] != 0U
                    && (!operand_write || *operand_write != reg)) {
                    return;
                }
                if (reg >= register_count || width[reg] == 0U) {
                    if (profile_) {
                        compile_failure_detail_ = "undefined reg="
                            + std::to_string(reg) + " op_index="
                            + std::to_string(operation.storage.index())
                            + " pc=" + std::to_string(pc);
                    }
                    throw reject("register_width");
                }
                generic = generic
                    || (width[reg] > 64U && field_slot[reg] == no_slot
                        && field_family[reg] == no_slot
                        && (!wide_source || reg != *wide_source));
            };
            // Control flow never runs generically (the bridge does not
            // transfer control); a wide condition is read in place.
            const bool control = operation_holds<Branch>(operation);
            for (const auto reg : operand_reads) {
                check(reg);
            }
            if (operand_write) {
                check(*operand_write);
            }
            if (control) {
                generic = false;
            }
            const auto wide_signal = [&](const SignalId signal) {
                if (signal >= slot_of_signal_.size()) {
                    throw reject("signal_range");
                }
                if (family_of_signal_[signal] != no_slot) {
                    return true;
                }
                const auto slot = slot_of_signal_[signal];
                return slot != no_slot ? slots_[slot].words != 1U
                                       : signal_width(signal) > 64U;
            };
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                const auto proxy = [&](const SignalId signal) {
                    return family_of_signal_[signal] != no_slot;
                };
                if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (op.destination < register_count
                        && (field_slot[op.destination] != no_slot
                            || field_family[op.destination] != no_slot)) {
                        return;
                    }
                    family_generic_ = family_generic_ || proxy(op.signal);
                    // A sampled-value read of a narrow signal is an inline
                    // load of the host's sampled words (KOp::sva_sampled_load,
                    // task #186); the global-clocking future forms read the
                    // signal's current value through the host.
                    generic = generic
                        || (sampled_read(op) && !inline_sampled_read(op))
                        || (!sampled_read(op) && wide_signal(op.signal));
                } else if constexpr (std::is_same_v<T, SvaOperation>) {
                    // The monitor's tick and outcome pop (IEEE 1800-2023
                    // 16.14) are kernel operations (KOp::sva_tick,
                    // sva_outcome, task #186) once every operand is a narrow
                    // register (checked above).
                } else if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteProjected>) {
                    if (op.signal >= slot_of_signal_.size()) {
                        throw reject("signal_range");
                    }
                    family_generic_ = family_generic_
                        || (owned(op.signal) && proxy(op.signal));
                    generic = generic || (owned(op.signal) && wide_signal(op.signal));
                } else if constexpr (std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteProjectedSlice>) {
                    // Narrow slices of wide owned slots are field stores; a
                    // SystemVerilog slice that is exactly one family leaf
                    // stores the leaf.
                    if (op.signal >= slot_of_signal_.size()) {
                        throw reject("signal_range");
                    }
                    bool leaf = false;
                    if constexpr (!std::is_same_v<T, WriteProjectedSlice>) {
                        leaf = !vhdl && op.source < register_count
                            && proxy_leaf(op.signal, op.offset, width[op.source])
                                != nullptr;
                    }
                    family_generic_ = family_generic_
                        || (owned(op.signal) && proxy(op.signal) && !leaf);
                    generic = generic
                        || (owned(op.signal) && proxy(op.signal) && !leaf);
                } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
                    || std::is_same_v<T, WriteUpdateDynamicSlice>
                    || std::is_same_v<T, WriteProjectedDynamicSlice>
                    || std::is_same_v<T, WriteBlockingDynamicPartSlice>
                    || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                    if (op.signal >= slot_of_signal_.size()) {
                        throw reject("signal_range");
                    }
                    // A host target takes store_host (host_dynamic_write); a
                    // family proxy's access is expanded into its leaves'
                    // (expand_family_accesses) before it gets here.
                    generic = generic || proxy(op.signal);
                    // A proxy's write, and a wide value's to an owned slot,
                    // are rewritten as narrow writes of the leaves and of
                    // 64-bit pieces (expand_family_accesses) and the member
                    // compiled again. A chunked wide value's to a host signal
                    // is one wide_store (sub 3): the host applies it as the
                    // one update event it is (IEEE 1800-2023 4.4).
                    family_generic_ = family_generic_ || proxy(op.signal)
                        || (op.source < register_count && width[op.source] > 64U
                            && width[op.source] != polymorphic
                            && (owned(op.signal) || !chunked(op.source)));
                    // A VHDL part-select assignment (IEEE 1076-2019 10.5.2.1)
                    // of an unresolved owned slot, deferred to the update
                    // phase, queues its field like a SystemVerilog
                    // nonblocking one (store_slot_part); a resolved slot's
                    // is resolved into its next block with the other
                    // sources' values at the write (emit_deferred_store). A
                    // shared variable's blocking one stores in place
                    // (emit_store_slot_part).
                    if constexpr (std::is_same_v<T, WriteBlockingDynamicPartSlice>
                        || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                        generic = generic || (vhdl && proxy(op.signal));
                    }
                } else if constexpr (std::is_same_v<T, ContainerRead>) {
                    generic = generic || op.string_index;
                } else if constexpr (std::is_same_v<T, Display>) {
                    // An attempt-start report tests the bind flag
                    // (KOp::generic, aux 2); it is not a wide operation.
                    generic = generic || !op.inert;
                } else if constexpr (std::is_same_v<T, Report>
                    || std::is_same_v<T, CodeCoverageHit>
                    || std::is_same_v<T, CodeCoverageToggle>
                    || std::is_same_v<T, TimeDisplay>
                    || std::is_same_v<T, SystemVerilogMath>
                    || std::is_same_v<T, SystemVerilogScalarBinary>) {
                    if constexpr (std::is_same_v<T, SystemVerilogMath>) {
                        // $time and $stime are an inline load of the kernel's
                        // time (below).
                        if (time_query_ticks(op)) {
                            return;
                        }
                    }
                    // A report statement (IEEE 1076-2019 10.9; a SystemVerilog
                    // severity message) and immediate output emit, a time
                    // query reads the kernel's time, a real-number operation
                    // runs the shared scalar library, and a coverage hit
                    // bumps its host counter, through the kernel's operation
                    // step.
                    generic = true;
                } else if constexpr (std::is_same_v<T, SignalEvent>
                    || std::is_same_v<T, SignalActive>
                    || std::is_same_v<T, SignalLastValue>
                    || std::is_same_v<T, SignalLastEvent>
                    || std::is_same_v<T, SignalLastActive>) {
                    // 'EVENT, 'ACTIVE, 'LAST_VALUE, 'LAST_EVENT and
                    // 'LAST_ACTIVE of a host signal (IEEE 1076-2019 16.2.4)
                    // read the host's history through the kernel's operation
                    // step.
                    generic = true;
                } else if constexpr (std::is_same_v<T, FormatDisplay>) {
                    if (op.source >= register_count || width[op.source] == 0U
                        || width[op.source] == polymorphic) {
                        throw reject("display_source");
                    }
                    generic = true;
                }
            }, operation);
            if (generic) {
                inst.op = KOp::generic;
                inst.x = pc;
                inst.y = 0U;
                has_wide = true;
                body.code.push_back(inst);
                continue;
            }
        }
        bool handled = false;
        if (vhdl) {
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                handled = true;
                if constexpr (std::is_same_v<T, WaitSensitivity>
                    || std::is_same_v<T, Halt>) {
                    // A runtime check's halt never runs: its failure report
                    // ends the simulation (the planner checks no branch
                    // reaches it). In 2019 mode std.env may disable failures
                    // (IEEE 1076-2019 16.5.6), so the halt can run.
                    if (pc != stop
                        && !(pc > begin && !member.vhdl_2019
                            && static_kernel_halt_after_failure(
                                member.operations()[pc - 1U],
                                member.operations()[pc]))) {
                        throw reject("vhdl_suspension");
                    }
                    inst.op = KOp::jump;
                    inst.d = end - begin;
                } else if constexpr (std::is_same_v<T, LoadConstant>) {
                    if (kind_of(op.destination) != ValueKind::logic9
                        || !op.value.is_logic9()) {
                        handled = false;
                        return;
                    }
                    if (op.value.width() == 0U || op.value.width() > 64U) {
                        throw reject("wide_constant");
                    }
                    inst.d = narrow(op.destination);
                    // All nine values are representable (word, 'U' mask in
                    // offset and aux, weak mask in x and y).
                    const auto word = exact_uword(four_state_constant(op)
                            ? Impl::coerce_value_kind(op.value, ValueKind::logic4)
                            : op.value);
                    inst.op = KOp::constant;
                    inst.imm_a = word->value.a;
                    inst.imm_b = word->value.b;
                    inst.offset = static_cast<std::uint32_t>(word->unknown);
                    inst.aux = static_cast<std::uint32_t>(word->unknown >> 32U);
                    inst.x = static_cast<std::uint32_t>(word->weak);
                    inst.y = static_cast<std::uint32_t>(word->weak >> 32U);
                } else if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (op.destination < register_count
                        && (field_slot[op.destination] != no_slot
                            || field_family[op.destination] != no_slot)) {
                        // Read in place by its narrow selections.
                        inst.op = KOp::nop;
                        return;
                    }
                    auto slot = owned_slot(op.signal);
                    if (!slot && mirror_of_signal_[op.signal] != no_slot) {
                        slot = mirror_of_signal_[op.signal];
                    }
                    if (slot && slots_[*slot].planes == 4U) {
                        if (slots_[*slot].words != 1U) {
                            throw reject("wide_logic9_slot");
                        }
                        inst.op = KOp::load_slot9;
                        inst.d = narrow(op.destination);
                        inst.x = *slot;
                        inst.width = slots_[*slot].width;
                        // sub 1: a Logic4 destination coerces Logic9 codes.
                        inst.sub = kind_of(op.destination) == ValueKind::logic9
                            ? 0U : 1U;
                    } else if (!slot) {
                        if (signal_width(op.signal) > 64U) {
                            throw reject("wide_host_read");
                        }
                        inst.op = KOp::load_host;
                        inst.d = narrow(op.destination);
                        inst.x = op.signal;
                        // sub 1: a Logic9 destination keeps exact codes.
                        inst.sub = kind_of(op.destination) == ValueKind::logic9
                            ? 1U : 0U;
                    } else {
                        handled = false;
                    }
                } else if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteProjected>
                    || std::is_same_v<T, WriteProjectedSlice>) {
                    constexpr bool blocking = std::is_same_v<T, WriteBlocking>
                        || std::is_same_v<T, WriteBlockingSlice>;
                    constexpr bool slice = std::is_same_v<T, WriteBlockingSlice>
                        || std::is_same_v<T, WriteUpdateSlice>
                        || std::is_same_v<T, WriteProjectedSlice>;
                    std::uint32_t offset = 0U;
                    if constexpr (slice) {
                        offset = op.offset;
                    }
                    const auto value_width = w(op.source);
                    inst.x = op.source;
                    inst.offset = offset;
                    inst.width = value_width;
                    if (const auto slot = owned_slot(op.signal)) {
                        if (offset > slots_[*slot].width
                            || value_width > slots_[*slot].width - offset
                            || (!slice && value_width != slots_[*slot].width)) {
                            throw reject("write_width");
                        }
                        inst.op = KOp::store_vhdl;
                        inst.d = *slot;
                        inst.flags = blocking ? flag_blocking : 0U;
                        // The target's shape, for generated code's check of
                        // an unchanged assignment (words per plane, planes).
                        // A resolved slot holds the resolution, not the
                        // assignment's value: no check.
                        inst.imm_a = slots_[*slot].words;
                        inst.imm_b = slots_[*slot].resolved ? 0U : slots_[*slot].planes;
                        // A resolved slot resolves the assignment into its
                        // next block in generated code (net_shape): imm_b
                        // its source count and the member's source, aux its
                        // width.
                        net_store(inst, *slot);
                        if (const auto load = net_constant_load(pc)) {
                            // Sub 2: the value is a nine-value constant,
                            // whose codes are at imm_a in the body's
                            // constants.
                            inst.sub = 2U;
                            inst.imm_a = net_constant_codes(*load);
                        }
                    } else {
                        SignalUpdateDomain domain = SignalUpdateDomain::generic;
                        if constexpr (std::is_same_v<T, WriteUpdate>
                            || std::is_same_v<T, WriteUpdateSlice>) {
                            domain = op.domain;
                        }
                        inst.op = KOp::store_host;
                        inst.d = op.signal;
                        inst.sub = static_cast<std::uint8_t>(domain);
                        inst.flags = static_cast<std::uint8_t>(
                            (blocking ? flag_blocking : 0U)
                            | (slice ? flag_linear : 0U));
                    }
                } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
                    || std::is_same_v<T, WriteUpdateDynamicSlice>
                    || std::is_same_v<T, WriteProjectedDynamicSlice>) {
                    const auto slot = owned_slot(op.signal);
                    if (w(op.selection.index) != 32U) {
                        throw reject("dynamic_write");
                    }
                    if (!slot) {
                        host_dynamic_write(inst, op.signal, op.source, op.selection.index,
                            SignalUpdateDomain::generic,
                            std::is_same_v<T, WriteBlockingDynamicSlice>,
                            op.selection.strict, false, &op.selection, nullptr);
                        return;
                    }
                    inst.op = KOp::store_vhdl;
                    inst.sub = 1U;
                    inst.d = *slot;
                    inst.x = op.source;
                    inst.y = op.selection.index;
                    inst.width = w(op.source);
                    // The target's width, for the generated range check.
                    inst.imm_a = slots_[*slot].width;
                    // A shared variable's immediate store (emit_vhdl_blocking_
                    // store) also needs the slot's planes, in z (imm_b is the
                    // shape of a resolved slot, net_shape).
                    if (std::is_same_v<T, WriteBlockingDynamicSlice>
                        && !slots_[*slot].resolved) {
                        inst.z = slots_[*slot].planes;
                    }
                    if (const auto load = net_constant_load(pc)) {
                        // Sub 3: sub 1 of a nine-value constant, whose codes
                        // are at imm_b in the body's constants (as sub 2).
                        inst.sub = 3U;
                        inst.imm_b = net_constant_codes(*load);
                    }
                    if (slots_[*slot].resolved) {
                        // The slot's shape (net_shape): sub 1 imm_b its
                        // source count and the member's source; sub 3 the
                        // source count above the codes' index in imm_b and
                        // the member's source above the width in imm_a.
                        const auto sources = net_source_count(*slot);
                        const auto own = std::uint64_t {
                            net_source_index(*slot, member.process) };
                        if (inst.sub == 3U) {
                            inst.imm_b |= sources << 32U;
                            inst.imm_a |= own << 32U;
                        } else {
                            inst.imm_b = sources | own << 32U;
                        }
                    }
                    inst.aux = static_cast<std::uint32_t>(body.indices.size());
                    body.indices.push_back(op.selection);
                    inst.flags = std::is_same_v<T, WriteBlockingDynamicSlice>
                        ? flag_blocking : 0U;
                } else if constexpr (std::is_same_v<T, IntegerBinary>) {
                    if (w(op.lhs) != w(op.rhs)) {
                        throw reject("integer_widths");
                    }
                    inst.op = KOp::integer_binary;
                    inst.sub = static_cast<std::uint8_t>(op.operation);
                    inst.d = narrow(op.destination);
                    inst.x = narrow(op.lhs);
                    inst.y = narrow(op.rhs);
                    inst.width = w(op.lhs);
                } else if constexpr (std::is_same_v<T, IntegerUnary>) {
                    inst.op = KOp::integer_unary;
                    inst.sub = static_cast<std::uint8_t>(op.operation);
                    inst.d = narrow(op.destination);
                    inst.x = narrow(op.source);
                    inst.width = w(op.source);
                } else if constexpr (std::is_same_v<T, IntegerCheck>) {
                    inst.op = KOp::integer_check;
                    inst.x = narrow(op.source);
                    inst.width = w(op.source);
                    inst.imm_a = static_cast<std::uint64_t>(op.lower);
                    inst.imm_b = static_cast<std::uint64_t>(op.upper);
                } else if constexpr (std::is_same_v<T, Call>) {
                    inst.op = KOp::call;
                    inst.d = map_target(op.target);
                    if (op.stack.capacity == 0U) {
                        // The body's growable stack: x is the depth word.
                        inst.x = body.call_stack_base;
                        inst.y = body.call_stack_base;
                        inst.z = 1U;
                        inst.flags = flag_linear;
                    } else {
                        inst.x = narrow(op.stack.pointer);
                        inst.y = op.stack.entries;
                        inst.z = op.stack.capacity;
                    }
                    inst.imm_a = op.return_target;
                    body.return_targets.push_back(op.return_target);
                } else if constexpr (std::is_same_v<T, Return>) {
                    inst.op = KOp::ret;
                    if (op.stack.capacity == 0U) {
                        inst.x = body.call_stack_base;
                        inst.y = body.call_stack_base;
                        inst.z = 1U;
                        inst.flags = flag_linear;
                    } else {
                        inst.x = narrow(op.stack.pointer);
                        inst.y = op.stack.entries;
                        inst.z = op.stack.capacity;
                    }
                } else if constexpr (std::is_same_v<T, CallableFramePush>
                    || std::is_same_v<T, CallableFramePop>) {
                    // Isolated (checked above); a recursive callable's frames
                    // are real.
                    if (body.frame_stack_base != 0U) {
                        if constexpr (std::is_same_v<T, CallableFramePush>) {
                            lower_frame(inst, op.identity, true, { }, pc);
                        } else {
                            lower_frame(inst, op.identity, false,
                                std::vector<RegisterId>(op.preserve_packed.begin(),
                                    op.preserve_packed.end()),
                                pc);
                        }
                    } else {
                        inst.op = KOp::nop;
                    }
                } else if constexpr (std::is_same_v<T, Assert>) {
                    inst.op = KOp::assert_check;
                    inst.x = narrow(op.condition);
                } else {
                    handled = false;
                }
            }, member.operations()[pc]);
        }
        if (handled) {
            body.code.push_back(inst);
            continue;
        }
        visit_operation([&](const auto& op) {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, DebugPoint>) {
                inst.op = KOp::nop;
            } else if constexpr (std::is_same_v<T, LoadConstant>) {
                const auto value
                    = Impl::coerce_value_kind(op.value, ValueKind::logic4);
                if (value.width() > 64U || value.width() == 0U) {
                    throw reject("wide_constant");
                }
                inst.op = KOp::constant;
                inst.d = narrow(op.destination);
                const auto aval = value.aval_words();
                const auto bval = value.bval_words();
                inst.imm_a = aval.empty() ? 0U : aval[0];
                inst.imm_b = bval.empty() ? 0U : bval[0];
            } else if constexpr (std::is_same_v<T, CopyRegister>) {
                inst.op = KOp::copy;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
            } else if constexpr (std::is_same_v<T, ReadSignal>) {
                if (sampled_read(op)) {
                    // A sampled value or sampled-value function (IEEE
                    // 1800-2023 16.5.1, 16.9.3): an inline load of the
                    // host's sampled words (the generic classification
                    // above kept the forms this cannot express).
                    inst.op = KOp::sva_sampled_load;
                    sva_table_used_ = true;
                    inst.d = narrow(op.destination);
                    inst.width = w(op.destination);
                    inst.sub = static_cast<std::uint8_t>(op.kind);
                    inst.offset = signal_width(op.signal);
                    inst.z = op.ticks;
                    if (op.kind == SignalReadKind::sampled && !op.clock) {
                        inst.aux = 1U;
                        inst.x = impl_.sampled_value_word_offsets[op.signal];
                        return;
                    }
                    const auto ring = impl_.planned_sampled_ring_offset(
                        Impl::SampledHistoryKey { op.signal, op.clock,
                            op.clock_edge, op.gate,
                            ProcessSchedulingDomain::systemverilog });
                    if (!ring) {
                        throw reject("sampled_ring");
                    }
                    impl_.sampled_ring_layout_baked = true;
                    inst.x = static_cast<std::uint32_t>(*ring);
                    if (op.kind != SignalReadKind::sampled
                        && op.kind != SignalReadKind::past) {
                        // $rose, $fell, $stable, $changed: between two
                        // ticks the Preponed value is the current one
                        // (16.9.3), at y - 1 in the sampled words.
                        impl_.ensure_sampled_value_word_layout();
                        const auto words = op.signal < impl_.sampled_value_word_offsets.size()
                            ? impl_.sampled_value_word_offsets[op.signal]
                            : Impl::no_sampled_value_words;
                        if (words == Impl::no_sampled_value_words) {
                            throw reject("sampled_words");
                        }
                        inst.y = words + 1U;
                    }
                    if (const auto& initial = impl_.get_signal(op.signal).initial_value;
                        signal_width(op.signal) > 64U || initial.is_logic9()) {
                        // A wide or Logic9 signal's ring entries are several
                        // planes of several words (aux 2): imm_a the chunks
                        // and, from bit 32, 1 for a Logic9 ring, imm_b the
                        // default's constants.
                        inst.aux = 2U;
                        inst.imm_a = wide_chunks(signal_width(op.signal))
                            | (initial.is_logic9() ? std::uint64_t { 1 } << 32U : 0U);
                        inst.imm_b = sampled_default_constants(op.signal);
                        return;
                    }
                    // An empty history reads the signal's default: its
                    // value at the start of simulation (sampled_defaults),
                    // which is the initial value after the startup writes.
                    const auto shape = Impl::coerce_value_kind(
                        impl_.get_signal(op.signal).initial_value, ValueKind::logic4);
                    const auto aval = shape.aval_words();
                    const auto bval = shape.bval_words();
                    inst.imm_a = aval.empty() ? 0U : aval[0];
                    inst.imm_b = bval.empty() ? 0U : bval[0];
                    return;
                }
                if (op.destination < register_count
                    && (field_slot[op.destination] != no_slot
                        || field_family[op.destination] != no_slot)) {
                    inst.op = KOp::nop;
                    return;
                }
                inst.d = narrow(op.destination);
                if (const auto slot = owned_narrow_slot(op.signal);
                    slot && slots_[*slot].planes == 4U) {
                    // A VHDL std_logic slot read by SystemVerilog (a port of
                    // a VHDL parent): its Logic9 codes coerce to Logic4, U
                    // and W reading as x (IEEE 1800-2023 21.2.1.3), as on
                    // the VHDL side (sub 1).
                    inst.op = KOp::load_slot9;
                    inst.x = *slot;
                    inst.width = slots_[*slot].width;
                    inst.sub = kind_of(op.destination) == ValueKind::logic9
                        ? 0U : 1U;
                } else if (slot) {
                    inst.op = KOp::load_slot;
                    inst.x = slots_[*slot].offset;
                } else {
                    if (signal_width(op.signal) > 64U) {
                        throw reject("wide_host_read");
                    }
                    inst.op = KOp::load_host;
                    inst.x = op.signal;
                }
            } else if constexpr (std::is_same_v<T, Binary>) {
                if (w(op.lhs) != w(op.rhs)) {
                    throw reject("binary_widths");
                }
                inst.op = KOp::binary;
                inst.sub = static_cast<std::uint8_t>(op.operation);
                inst.d = narrow(op.destination);
                inst.x = op.lhs;
                inst.y = op.rhs;
                inst.width = w(op.lhs);
            } else if constexpr (std::is_same_v<T, Reduction>) {
                inst.op = KOp::reduce;
                inst.sub = static_cast<std::uint8_t>(op.operation);
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
                inst.width = w(op.source);
            } else if constexpr (std::is_same_v<T, UnaryNot>) {
                inst.op = KOp::unary_not;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
                inst.width = w(op.source);
            } else if constexpr (std::is_same_v<T, LogicalNot>) {
                inst.op = KOp::logical_not;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
            } else if constexpr (std::is_same_v<T, LogicalBinary>) {
                inst.op = KOp::logical_binary;
                inst.sub = static_cast<std::uint8_t>(op.operation);
                inst.d = narrow(op.destination);
                inst.x = narrow(op.lhs);
                inst.y = narrow(op.rhs);
            } else if constexpr (std::is_same_v<T, Shift>) {
                inst.op = KOp::shift;
                inst.sub = static_cast<std::uint8_t>(op.operation);
                inst.d = narrow(op.destination);
                inst.x = narrow(op.value);
                inst.y = narrow(op.amount);
                inst.width = w(op.value);
                inst.offset = w(op.amount);
                inst.flags = op.signed_amount ? flag_signed : 0U;
            } else if constexpr (std::is_same_v<T, Extract>) {
                if (op.source < register_count
                    && field_family[op.source] != no_slot) {
                    const auto* leaf = family_leaf(field_family[op.source],
                        op.offset, op.width);
                    if (leaf == nullptr) {
                        throw reject("extract_range");
                    }
                    const auto slot = leaf->slot;
                    inst.d = narrow(op.destination);
                    inst.x = slot;
                    inst.offset = op.offset - leaf->offset;
                    inst.width = op.width;
                    inst.imm_a = slots_[slot].words;
                    if (slots_[slot].planes == 4U) {
                        inst.op = KOp::load_field9;
                        inst.sub = kind_of(op.source) == ValueKind::logic9 ? 0U : 1U;
                    } else {
                        inst.op = KOp::load_field;
                    }
                    return;
                }
                if (op.source < register_count && field_slot[op.source] != no_slot) {
                    const auto slot = field_slot[op.source];
                    if (op.width == 0U || op.width > 64U
                        || op.offset > slots_[slot].width
                        || op.width > slots_[slot].width - op.offset) {
                        throw reject("extract_range");
                    }
                    if (slots_[slot].planes == 4U) {
                        // The read register's kind decides exact or coerced.
                        inst.op = KOp::load_field9;
                        inst.sub = kind_of(op.source) == ValueKind::logic9 ? 0U : 1U;
                        inst.d = narrow(op.destination);
                        inst.x = slot;
                        inst.offset = op.offset;
                        inst.width = op.width;
                        inst.imm_a = slots_[slot].words;
                        return;
                    }
                    inst.op = KOp::load_field;
                    inst.d = narrow(op.destination);
                    inst.x = slot;
                    inst.offset = op.offset;
                    inst.width = op.width;
                    inst.imm_a = slots_[slot].words;
                    return;
                }
                if (op.source < register_count && width[op.source] > 64U) {
                    if (op.width == 0U || op.width > 64U
                        || op.offset > width[op.source]
                        || op.width > width[op.source] - op.offset) {
                        throw reject("extract_range");
                    }
                    inst.op = KOp::extract;
                    inst.sub = 2U;
                    inst.d = narrow(op.destination);
                    inst.x = op.source;
                    inst.offset = op.offset;
                    inst.width = op.width;
                    if (chunked(op.source)) {
                        inst.sub = 3U;
                        inst.imm_a = has_unknowns(op.source) ? 1U : 0U;
                        inst.imm_b = wide_chunks(width[op.source]);
                    }
                    return;
                }
                if (op.width == 0U || op.offset > w(op.source)
                    || op.width > w(op.source) - op.offset) {
                    throw reject("extract_range");
                }
                inst.op = KOp::extract;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
                inst.offset = op.offset;
                inst.width = op.width;
            } else if constexpr (std::is_same_v<T, Insert>) {
                if (op.offset > w(op.target)
                    || w(op.source) > w(op.target) - op.offset) {
                    throw reject("insert_range");
                }
                inst.op = KOp::insert;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.target);
                inst.y = narrow(op.source);
                inst.offset = op.offset;
                inst.width = w(op.source);
            } else if constexpr (std::is_same_v<T, Concatenate>) {
                std::uint32_t total = 0U;
                std::uint32_t kept = 0U;
                inst.aux = static_cast<std::uint32_t>(body.concat.size());
                for (const auto operand : op.operands) {
                    // A null-range operand contributes no bits.
                    if (operand < register_count && null_register[operand] != 0U) {
                        continue;
                    }
                    total += w(operand);
                    body.concat.push_back({ operand, w(operand) });
                    ++kept;
                }
                if (total != op.width || kept == 0U) {
                    throw reject("concat_width");
                }
                inst.op = KOp::concat;
                inst.d = narrow(op.destination);
                inst.x = kept;
                inst.width = op.width;
            } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
                if (w(op.condition) != 1U || w(op.when_true) != w(op.when_false)) {
                    throw reject("conditional_widths");
                }
                inst.op = KOp::conditional;
                inst.d = narrow(op.destination);
                inst.x = op.condition;
                inst.y = op.when_true;
                inst.z = op.when_false;
                inst.width = w(op.when_true);
            } else if constexpr (std::is_same_v<T, ConvertToTwoState>) {
                inst.op = KOp::two_state;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.source);
            } else if constexpr (std::is_same_v<T, DynamicExtract>) {
                if (w(op.selection.index) != 32U) {
                    throw reject("dynamic_index_width");
                }
                inst.op = KOp::dynamic_extract;
                inst.d = narrow(op.destination);
                inst.y = op.selection.index;
                if (op.source < register_count && field_slot[op.source] != no_slot) {
                    inst.sub = 1U;
                    inst.x = field_slot[op.source];
                    inst.width = slots_[field_slot[op.source]].width;
                    // Logic9 slot read into a Logic4 register: coerce.
                    inst.imm_b = slots_[inst.x].planes == 4U
                            && kind_of(op.source) != ValueKind::logic9
                        ? 1U : 0U;
                    // The slot's plane count, for generated code.
                    inst.z = slots_[inst.x].planes;
                } else if (op.source < register_count && width[op.source] > 64U) {
                    inst.sub = 2U;
                    inst.x = op.source;
                    inst.width = width[op.source];
                    if (chunked(op.source)) {
                        inst.sub = 3U;
                        inst.imm_a = has_unknowns(op.source) ? 1U : 0U;
                        inst.imm_b = wide_chunks(width[op.source]);
                    }
                } else {
                    inst.x = narrow(op.source);
                    inst.width = w(op.source);
                }
                inst.aux = static_cast<std::uint32_t>(body.indices.size());
                inst.flags = op.selection.strict ? flag_strict : 0U;
                body.indices.push_back(op.selection);
            } else if constexpr (std::is_same_v<T, DynamicPartSelect>) {
                if (w(op.base) != 32U || op.width == 0U || op.width > 64U) {
                    throw reject("part_select");
                }
                inst.op = KOp::dynamic_part_select;
                inst.d = narrow(op.destination);
                inst.y = op.base;
                if (op.source < register_count && field_slot[op.source] != no_slot) {
                    inst.sub = 1U;
                    inst.x = field_slot[op.source];
                    inst.offset = slots_[field_slot[op.source]].width;
                    inst.imm_b = slots_[inst.x].planes == 4U
                            && kind_of(op.source) != ValueKind::logic9
                        ? 1U : 0U;
                    // The slot's plane count, for generated code.
                    inst.z = slots_[inst.x].planes;
                } else if (op.source < register_count && width[op.source] > 64U) {
                    inst.sub = 2U;
                    inst.x = op.source;
                    inst.offset = width[op.source];
                    if (chunked(op.source)) {
                        inst.sub = 3U;
                        inst.imm_a = has_unknowns(op.source) ? 1U : 0U;
                        inst.imm_b = wide_chunks(width[op.source]);
                    }
                } else {
                    inst.x = narrow(op.source);
                    inst.offset = w(op.source);
                }
                inst.width = op.width;
                inst.aux = static_cast<std::uint32_t>(body.parts.size());
                inst.flags = static_cast<std::uint8_t>(
                    (op.increasing ? flag_increasing : 0U)
                    | (op.source_descending ? flag_descending : 0U)
                    | (op.two_state ? flag_two_state : 0U));
                // Readers take the base offset from body.parts; sub 3 keeps
                // its mask flag in imm_a.
                if (inst.sub != 3U) {
                    inst.imm_a = op.base_offset;
                }
                DynamicPartIndex part;
                part.left = op.left;
                part.right = op.right;
                part.base_offset = op.base_offset;
                part.width = op.width;
                part.increasing = op.increasing;
                part.source_descending = op.source_descending;
                body.parts.push_back(part);
            } else if constexpr (std::is_same_v<T, DynamicInsert>) {
                if (w(op.selection.index) != 32U) {
                    throw reject("dynamic_index_width");
                }
                inst.op = KOp::dynamic_insert;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.target);
                inst.y = narrow(op.source);
                inst.z = op.selection.index;
                inst.width = w(op.target);
                inst.offset = w(op.source);
                inst.aux = static_cast<std::uint32_t>(body.indices.size());
                inst.flags = op.selection.strict ? flag_strict : 0U;
                body.indices.push_back(op.selection);
            } else if constexpr (std::is_same_v<T, DynamicPartInsert>) {
                if (w(op.selection.base) != 32U || op.selection.width == 0U
                    || w(op.source) != op.selection.width) {
                    throw reject("part_insert");
                }
                inst.op = KOp::dynamic_part_insert;
                inst.d = narrow(op.destination);
                inst.x = narrow(op.target);
                inst.y = narrow(op.source);
                inst.z = op.selection.base;
                inst.width = w(op.target);
                inst.aux = static_cast<std::uint32_t>(body.parts.size());
                body.parts.push_back(op.selection);
            } else if constexpr (std::is_same_v<T, Jump>) {
                inst.op = KOp::jump;
                inst.d = map_target(op.target);
            } else if constexpr (std::is_same_v<T, Branch>) {
                inst.op = KOp::branch;
                if (op.condition < register_count && width[op.condition] > 64U) {
                    // sub 2: the condition is a wide or polymorphic register.
                    inst.sub = 2U;
                } else if (w(op.condition) != 1U) {
                    throw reject("branch_width");
                }
                inst.x = op.condition;
                inst.y = map_target(op.when_true);
                inst.z = map_target(op.when_false);
                inst.flags = op.unknown_policy == UnknownBranchPolicy::when_false
                    ? flag_linear : 0U;
            } else if constexpr (std::is_same_v<T, WriteBlocking>
                || std::is_same_v<T, WriteBlockingSlice>
                || std::is_same_v<T, WriteUpdate>
                || std::is_same_v<T, WriteUpdateSlice>) {
                constexpr bool blocking = std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>;
                constexpr bool slice = std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteUpdateSlice>;
                SignalUpdateDomain domain = SignalUpdateDomain::systemverilog_active;
                if constexpr (!blocking) {
                    domain = op.domain;
                }
                std::uint32_t offset = 0U;
                if constexpr (slice) {
                    offset = op.offset;
                }
                const auto value_width = w(op.source);
                inst.x = op.source;
                inst.offset = offset;
                inst.width = value_width;
                if (const auto* leaf = proxy_leaf(op.signal, offset, value_width)) {
                    // The whole leaf (see the generic decision).
                    inst.d = leaf->slot;
                    inst.offset = 0U;
                    // Sub 2 and 3: sub 0 and 1 of a resolved slot, whose
                    // stores resolve its sources (net_store).
                    inst.sub = static_cast<std::uint8_t>(
                        (slots_[leaf->slot].words != 1U ? 1U : 0U)
                        + (slots_[leaf->slot].resolved ? 2U : 0U));
                    inst.imm_a = slots_[leaf->slot].words;
                    net_store(inst, leaf->slot);
                    inst.op = !blocking
                            && domain == SignalUpdateDomain::systemverilog_nba
                        ? KOp::store_slot_nba : KOp::store_slot;
                    inst.flags = 0U;
                } else if (const auto slot = owned_slot(op.signal)) {
                    // Sub 1: a narrow slice of a wide slot; imm_a is the
                    // slot's word count.
                    inst.sub = static_cast<std::uint8_t>(
                        (slots_[*slot].words != 1U ? 1U : 0U)
                        + (slots_[*slot].resolved ? 2U : 0U));
                    inst.imm_a = slots_[*slot].words;
                    net_store(inst, *slot);
                    if (offset > slots_[*slot].width
                        || value_width > slots_[*slot].width - offset
                        || (!slice && value_width != slots_[*slot].width)) {
                        throw reject("write_width");
                    }
                    inst.d = *slot;
                    inst.op = !blocking
                            && domain == SignalUpdateDomain::systemverilog_nba
                        ? KOp::store_slot_nba : KOp::store_slot;
                    inst.flags = slice ? flag_linear : 0U;
                } else {
                    inst.op = KOp::store_host;
                    inst.d = op.signal;
                    inst.sub = static_cast<std::uint8_t>(domain);
                    inst.flags = static_cast<std::uint8_t>(
                        (blocking ? flag_blocking : 0U)
                        | (slice ? flag_linear : 0U));
                }
            } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
                || std::is_same_v<T, WriteUpdateDynamicSlice>) {
                constexpr bool blocking
                    = std::is_same_v<T, WriteBlockingDynamicSlice>;
                const auto slot = owned_slot(op.signal);
                if (w(op.selection.index) != 32U) {
                    throw reject("dynamic_write");
                }
                if (!slot) {
                    SignalUpdateDomain domain = SignalUpdateDomain::systemverilog_active;
                    if constexpr (!blocking) {
                        domain = op.domain;
                    }
                    host_dynamic_write(inst, op.signal, op.source, op.selection.index,
                        domain, blocking, op.selection.strict, false, &op.selection,
                        nullptr);
                    return;
                }
                inst.op = KOp::store_slot_dynamic;
                inst.d = *slot;
                inst.x = op.source;
                inst.y = op.selection.index;
                inst.width = w(op.source);
                // The slot's words per plane and width, for native stores
                // (Logic4 slots only).
                inst.imm_a = slots_[*slot].words;
                inst.imm_b = slots_[*slot].planes == 2U && !slots_[*slot].resolved
                    ? slots_[*slot].width : 0U;
                if (slots_[*slot].resolved) {
                    // sub 2: a resolved slot, whose member source takes the
                    // field's codes in generated code (net_store: imm_b the
                    // source count and the member's source); offset its width.
                    inst.sub = 2U;
                    inst.offset = slots_[*slot].width;
                    net_store(inst, *slot);
                } else if (slots_[*slot].planes == 4U) {
                    // A nine-value slot: a write that takes effect at once
                    // stores the codes of the four planes (sub 3, with the
                    // source's 'U' and weak masks), a nonblocking one queues
                    // the masks with the field (as store_slot_part).
                    bool immediate = true;
                    if constexpr (!blocking) {
                        immediate = op.domain != SignalUpdateDomain::systemverilog_nba;
                    }
                    inst.imm_b = slots_[*slot].width;
                    inst.sub = immediate ? 3U : 0U;
                }
                inst.aux = static_cast<std::uint32_t>(body.indices.size());
                body.indices.push_back(op.selection);
                // A strict (VHDL) index is checked; a SystemVerilog write
                // through an unknown or out-of-range index does nothing
                // (IEEE 1800-2023 11.5.1), as in the reference evaluator.
                inst.flags = op.selection.strict ? flag_strict : 0U;
                if constexpr (!blocking) {
                    inst.flags = static_cast<std::uint8_t>(inst.flags
                        | (op.domain == SignalUpdateDomain::systemverilog_nba
                            ? flag_nba : 0U));
                    if (op.domain == SignalUpdateDomain::systemverilog_nba
                        && std::ranges::binary_search(member.piece_continuations, pc)) {
                        inst.flags = static_cast<std::uint8_t>(inst.flags | flag_continues);
                    }
                }
            } else if constexpr (std::is_same_v<T, WriteBlockingDynamicPartSlice>
                || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                constexpr bool blocking
                    = std::is_same_v<T, WriteBlockingDynamicPartSlice>;
                const auto slot = owned_slot(op.signal);
                if (w(op.selection.base) != 32U
                    || w(op.source) != op.selection.width) {
                    throw reject("dynamic_part_write");
                }
                if (!slot) {
                    SignalUpdateDomain domain = vhdl
                        ? SignalUpdateDomain::generic
                        : SignalUpdateDomain::systemverilog_active;
                    if constexpr (!blocking) {
                        if (!vhdl) {
                            domain = op.domain;
                        }
                    }
                    host_dynamic_write(inst, op.signal, op.source, op.selection.base,
                        domain, blocking, false, true, nullptr, &op.selection);
                    return;
                }
                inst.op = KOp::store_slot_part;
                inst.d = *slot;
                inst.x = op.source;
                inst.y = op.selection.base;
                inst.width = w(op.source);
                // As for store_slot_dynamic: the slot's words per plane and
                // width, for native stores (Logic4 slots only).
                inst.imm_a = slots_[*slot].words;
                inst.imm_b = slots_[*slot].planes == 2U && !slots_[*slot].resolved
                    ? slots_[*slot].width : 0U;
                if (slots_[*slot].resolved) {
                    // sub 2: a resolved slot, whose member source takes the
                    // field's codes in generated code (net_store: imm_b the
                    // source count and the member's source); offset its width.
                    inst.sub = 2U;
                    inst.offset = slots_[*slot].width;
                    net_store(inst, *slot);
                } else if (slots_[*slot].planes == 4U) {
                    // A nine-value slot. A VHDL assignment is applied in the
                    // update phase, as is a nonblocking SystemVerilog one:
                    // the field's width is all the queue form needs. A
                    // shared variable (IEEE 1076-2019 6.4.2.4) and a
                    // SystemVerilog write that takes effect at once store
                    // the codes of the four planes (sub 3).
                    bool immediate = true;
                    if constexpr (!blocking) {
                        immediate = !vhdl
                            && op.domain != SignalUpdateDomain::systemverilog_nba;
                    }
                    inst.imm_b = slots_[*slot].width;
                    inst.sub = immediate ? 3U : 0U;
                }
                inst.aux = static_cast<std::uint32_t>(body.parts.size());
                body.parts.push_back(op.selection);
                if constexpr (!blocking) {
                    inst.flags = vhdl || op.domain == SignalUpdateDomain::systemverilog_nba
                        ? flag_nba : 0U;
                    if (!vhdl && op.domain == SignalUpdateDomain::systemverilog_nba
                        && std::ranges::binary_search(member.piece_continuations, pc)) {
                        inst.flags = static_cast<std::uint8_t>(inst.flags | flag_continues);
                    }
                }
            } else if constexpr (std::is_same_v<T, ReadContainerObject>) {
                inst.op = KOp::nop;
            } else if constexpr (std::is_same_v<T, ContainerRead>) {
                if (op.source >= container_of_register.size()
                    || container_of_register[op.source] == no_container
                    || op.string_index) {
                    throw reject("container_read");
                }
                const auto container = container_of_register[op.source];
                // Elements of more than 64 bits are the wide lowering's
                // (mem_read sub 1); the destination of this one is narrow.
                if (containers_[container].type.element_width > 64U
                    || containers_[container].type.associative
                    || !containers_[container].type.fixed) {
                    throw reject("container_shape");
                }
                inst.op = KOp::mem_read;
                inst.d = narrow(op.destination);
                inst.x = container;
                inst.y = narrow(op.index);
                inst.width = w(op.index);
                inst.flags = op.linear_index ? flag_linear : 0U;
            } else if constexpr (std::is_same_v<T, ContainerWrite>) {
                // A blocking element write of a private array.
                if (op.target >= container_of_register.size()
                    || container_of_register[op.target] == no_container
                    || containers_[container_of_register[op.target]].object
                        != no_container_object
                    || op.string_index) {
                    throw reject("container_write");
                }
                inst.op = KOp::mem_write;
                inst.d = container_of_register[op.target];
                inst.x = narrow(op.source);
                inst.y = narrow(op.index);
                inst.width = w(op.source);
                inst.offset = w(op.index);
                inst.flags = static_cast<std::uint8_t>(
                    (op.linear_index ? flag_linear : 0U)
                    | (op.signed_index ? flag_signed : 0U));
            } else if constexpr (std::is_same_v<T, CopyContainerRegister>) {
                // Arrays of one shape (either may be a design memory, as the
                // planner admitted); the runtime copies the elements.
                if (op.destination >= container_of_register.size()
                    || op.source >= container_of_register.size()) {
                    throw reject("container_copy");
                }
                const auto destination = container_of_register[op.destination];
                const auto source = container_of_register[op.source];
                if (destination == no_container || source == no_container
                    || !(containers_[destination].type
                        == containers_[source].type)
                    || containers_[destination].count != containers_[source].count) {
                    throw reject("container_copy");
                }
                inst.op = KOp::mem_copy;
                inst.d = destination;
                inst.x = source;
            } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
                if (op.object >= container_of_object_.size()
                    || container_of_object_[op.object] == no_container
                    || op.transaction_signal || !op.levels.empty()) {
                    throw reject("container_write");
                }
                inst.op = KOp::mem_write;
                inst.d = container_of_object_[op.object];
                inst.x = narrow(op.source);
                inst.y = narrow(op.index);
                inst.width = w(op.source);
                inst.offset = w(op.index);
                inst.flags = static_cast<std::uint8_t>(
                    (op.nonblocking ? flag_nba : 0U)
                    | (op.linear_index ? flag_linear : 0U)
                    | (op.signed_index ? flag_signed : 0U));
                if (op.dynamic_part) {
                    inst.z = narrow(op.dynamic_part->base);
                    inst.aux = static_cast<std::uint32_t>(body.parts.size())
                        + 1U;
                    body.parts.push_back(*op.dynamic_part);
                    // The element's width, for generated code's in-place
                    // part write (emit_mem_write_part).
                    inst.imm_a = containers_[inst.d].type.element_width;
                }
            } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
                // $time and $stime (IEEE 1800-2023 20.3; the classification
                // above leaves no other form here): the kernel's time in the
                // unit's ticks, rounded to the nearest unit, from the SVA
                // table's time (aux 4; sub 0 $time, 1 $stime).
                const auto ticks = time_query_ticks(op);
                if (!ticks) {
                    throw reject("operation");
                }
                inst.op = KOp::sva_sampled_load;
                sva_table_used_ = true;
                inst.d = narrow(op.destination);
                inst.aux = 4U;
                inst.sub = op.function == SystemVerilogMathFunction::Stime ? 1U : 0U;
                inst.width = op.function == SystemVerilogMathFunction::Stime ? 32U : 64U;
                inst.imm_a = *ticks;
            } else if constexpr (std::is_same_v<T, SvaOperation>) {
                if (op.kind == SvaKind::control) {
                    // An assertion control read (IEEE 1800-2023 20.11): an
                    // inline load of the control word of site x of the
                    // member's process (StaticKernelSvaTable::controls).
                    inst.op = KOp::sva_sampled_load;
                    sva_table_used_ = true;
                    inst.d = narrow(op.result);
                    inst.width = 32U;
                    inst.aux = 8U;
                    inst.x = op.site;
                    return;
                }
                // A concurrent assertion's clock tick or outcome pop (IEEE
                // 1800-2023 16.14; task #186): x the operation, y the
                // register base (fused bodies add theirs), aux the
                // directive's entry in the member's block table.
                if (op.kind == SvaKind::request || op.kind == SvaKind::local
                    || op.kind == SvaKind::answer || op.kind == SvaKind::call) {
                    // A local-variable request of the directive's last tick
                    // (IEEE 1800-2023 16.10): the tick's entry with sub 1
                    // (request), 2 (local) or 3 (answer) reads and writes
                    // the request's memo entry (StaticKernel::
                    // execute_sva_memo).
                    inst.op = KOp::sva_tick;
                    sva_table_used_ = true;
                    inst.sub = op.kind == SvaKind::request ? 1U
                        : op.kind == SvaKind::local        ? 2U
                        : op.kind == SvaKind::answer       ? 3U
                                                           : 4U;
                    inst.d = op.result;
                    inst.width = op.kind == SvaKind::local ? width[op.result] : 32U;
                    inst.x = pc;
                    inst.y = 0U;
                    inst.aux = sva_site_ordinal(op.site);
                    if (op.kind != SvaKind::call) {
                        (void)narrow(op.start);
                    }
                    if (op.kind == SvaKind::answer && !chunked(op.guards)) {
                        (void)narrow(op.guards);
                    }
                    // The operations are generated code over the block's
                    // memo and calls regions with the plan's local
                    // constants (z the plan's index plus one; imm_a the
                    // request and answer registers, imm_b guard_count).
                    static const bool requests_native = [] {
                        const char* text = std::getenv("FSIM_KERNEL_SVA_NATIVE");
                        return text == nullptr || std::string_view { text } != "0";
                    }();
                    if (requests_native) {
                        if (auto locals = sva_native_requests(member.operations(), op.site)) {
                            const auto found = std::ranges::find(body.sva_plans, *locals);
                            if (found == body.sva_plans.end()) {
                                body.sva_plans.push_back(std::move(*locals));
                                inst.z = static_cast<std::uint32_t>(body.sva_plans.size());
                            } else {
                                inst.z = static_cast<std::uint32_t>(
                                    found - body.sva_plans.begin()) + 1U;
                            }
                            inst.imm_a = static_cast<std::uint64_t>(op.start)
                                | (static_cast<std::uint64_t>(op.guards) << 32U);
                            inst.imm_b = op.guard_count;
                        }
                    }
                    return;
                }
                inst.op = op.kind == SvaKind::tick ? KOp::sva_tick : KOp::sva_outcome;
                sva_table_used_ = true;
                inst.d = narrow(op.result);
                inst.width = 32U;
                inst.x = pc;
                inst.y = 0U;
                inst.aux = sva_site_ordinal(op.site);
                if (op.kind == SvaKind::tick) {
                    (void)narrow(op.start);
                    (void)narrow(op.disabled);
                    if (op.guard_count != 0U && !chunked(op.guards)) {
                        (void)narrow(op.guards);
                    }
                    // A plan of a supported shape gets its attempt automaton
                    // as generated code (z the plan's index plus one; the
                    // operands' registers relative to y); the others call
                    // the host monitor (FSIM_KERNEL_SVA_NATIVE=0: all).
                    static const bool native_enabled = [] {
                        const char* text = std::getenv("FSIM_KERNEL_SVA_NATIVE");
                        return text == nullptr || std::string_view { text } != "0";
                    }();
                    std::string reason = "disabled";
                    std::optional<SvaNativePlan> native;
                    if (native_enabled) {
                        native = sva_native_plan(op, reason);
                    }
                    if (native) {
                        body.sva_plans.push_back(std::move(*native));
                        inst.z = static_cast<std::uint32_t>(body.sva_plans.size());
                        inst.imm_a = static_cast<std::uint64_t>(op.guards)
                            | (static_cast<std::uint64_t>(op.start) << 32U);
                        inst.imm_b = static_cast<std::uint64_t>(op.disabled)
                            | (static_cast<std::uint64_t>(op.guard_count) << 32U);
                    } else if (profile_) {
                        ++profile_generic_ops_["sva_tick_host:" + reason];
                    }
                }
            } else {
                throw reject("operation");
            }
        }, member.operations()[pc]);
        body.code.push_back(inst);
    }
    // Every register an instruction reads must have a known narrow width.
    for (const auto& inst : body.code) {
        const auto check = [&](const std::uint32_t reg) { (void)narrow(reg); };
        switch (inst.op) {
        case KOp::binary:
            check(inst.x);
            check(inst.y);
            break;
        case KOp::conditional:
            check(inst.x);
            check(inst.y);
            check(inst.z);
            break;
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
            check(inst.y);
            break;
        case KOp::dynamic_insert:
        case KOp::dynamic_part_insert:
            check(inst.z);
            break;
        case KOp::wide_part_insert:
            check(inst.y);
            check(inst.z);
            break;
        case KOp::branch:
            if (inst.sub != 2U) {
                check(inst.x);
            }
            break;
        case KOp::store_slot:
        case KOp::store_slot_nba:
        case KOp::store_host:
            check(inst.x);
            break;
        case KOp::store_slot_dynamic:
        case KOp::store_slot_part:
            check(inst.x);
            check(inst.y);
            break;
        case KOp::store_vhdl:
            // A wide nine-value constant stored to a resolved slot (sub 2,
            // wide_constants) names its chunked register, which the store
            // does not read: the codes are at imm_a.
            if (inst.sub != 2U || inst.width <= 64U) {
                check(inst.x);
            }
            if (inst.sub == 1U || inst.sub == 3U) {
                check(inst.y);
            }
            break;
        case KOp::integer_binary:
            check(inst.x);
            check(inst.y);
            break;
        case KOp::integer_unary:
        case KOp::integer_check:
        case KOp::assert_check:
            check(inst.x);
            break;
        case KOp::call:
        case KOp::ret:
            if ((inst.flags & flag_linear) == 0U) {
                check(inst.x);
            }
            break;
        default:
            break;
        }
    }
    if (behavioral) {
        // A thread starts at operation 0, resumes after each suspension, and
        // fork children start at their branches.
        body.entry = 0U;
        body.resume_entries.push_back(0U);
        for (std::uint32_t pc = 0U; pc < end; ++pc) {
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Fork>) {
                    body.resume_entries.insert(body.resume_entries.end(),
                        op.branches.begin(), op.branches.end());
                }
                if constexpr (std::is_same_v<T, DisableBlock>) {
                    // A disabled block's threads resume at its end (a block
                    // named by hierarchical name: its block_exit below).
                    if (op.scopes.empty()) {
                        body.resume_entries.push_back(op.end);
                    }
                }
                if constexpr (behavioral_suspension<T>
                    || std::is_same_v<T, WaitSensitivity>) {
                    body.resume_entries.push_back(pc + 1U);
                }
                if constexpr (std::is_same_v<T, DebugPoint>) {
                    if (debug_points_ && debug_point_executable(op.kind)) {
                        body.resume_entries.push_back(pc + 1U);
                    }
                    // A disable naming this block by hierarchical name, in
                    // any member, resumes its threads at the block's end
                    // (disable_named_scope).
                    if (named_disable_ && op.kind == DebugPointKind::block_exit) {
                        body.resume_entries.push_back(pc);
                    }
                }
            }, member.operations()[pc]);
        }
        std::ranges::sort(body.resume_entries);
        body.resume_entries.erase(std::unique(body.resume_entries.begin(),
                                      body.resume_entries.end()),
            body.resume_entries.end());
        std::erase_if(body.resume_entries,
            [&](const std::uint32_t entry) { return entry >= end; });
        std::ranges::sort(body.return_targets);
        body.return_targets.erase(std::unique(body.return_targets.begin(),
                                      body.return_targets.end()),
            body.return_targets.end());
    }
    if (!vhdl && !member.register_kinds.empty()) {
        track_unknowns(body, member_index);
    }
    if (vhdl && behavioral) {
        // Entry and resume points are the thread's (above); the 'U' and weak
        // tracking is the VHDL member's.
        track_unknowns(body, member_index);
        std::ranges::sort(body.return_targets);
        body.return_targets.erase(std::unique(body.return_targets.begin(),
                                      body.return_targets.end()),
            body.return_targets.end());
    } else if (vhdl) {
        if (native_prologue(member) && member.prologue_end != member.body_begin
            && member.prologue_end < body.code.size()) {
            // The first run leaves the prologue for the specialized body.
            KInst jump;
            jump.op = KOp::jump;
            jump.d = member.body_begin;
            body.code[member.prologue_end] = jump;
        }
        track_unknowns(body, member_index);
        body.entry = member.body_begin;
        body.live_at_entry = std::move(live_at_entry);
        std::ranges::sort(body.return_targets);
        body.return_targets.erase(std::unique(body.return_targets.begin(),
                                      body.return_targets.end()),
            body.return_targets.end());
    }
    // Storage of the chunked wide registers, after every other register
    // and the 'U' shadows: chunks, then a Logic9 register's 'U' masks.
    // Instructions named them by register until now.
    {
        std::vector<std::uint32_t> storage(register_count, 0U);
        bool any = false;
        for (RegisterId reg = 0U; reg < register_count; ++reg) {
            if (!chunked(reg)) {
                continue;
            }
            const auto chunks = wide_chunks(width[reg]);
            const auto base = static_cast<std::uint32_t>(body.registers.size());
            const auto count = has_unknowns(reg) ? 2U * chunks : chunks;
            storage[reg] = base;
            body.registers.resize(base + count, Word { });
            body.register_widths.resize(base + count, 0U);
            any = true;
        }
        if (any) {
            body.wide_base.assign(body.registers.size(), 0U);
            for (RegisterId reg = 0U; reg < register_count; ++reg) {
                if (storage[reg] != 0U) {
                    body.wide_base[reg] = storage[reg]
                        | (has_unknowns(reg) ? wide_unknown_flag : 0U);
                }
            }
            const auto place = [&](std::uint32_t& field) {
                if (field >= register_count || storage[field] == 0U) {
                    throw reject("wide_storage");
                }
                field = storage[field];
            };
            for (auto& inst : body.code) {
                switch (inst.op) {
                case KOp::wide_load:
                case KOp::wide_const:
                case KOp::wide_concat:
                    place(inst.d);
                    break;
                case KOp::wide_copy:
                case KOp::wide_part_insert:
                    place(inst.d);
                    place(inst.x);
                    break;
                case KOp::wide_store:
                    place(inst.x);
                    break;
                case KOp::wide_alu:
                    // Only the wide operands name chunked registers.
                    if ((inst.aux & (wide_alu_wide << 0U)) != 0U) {
                        place(inst.d);
                    }
                    if ((inst.aux & (wide_alu_wide << 1U)) != 0U) {
                        place(inst.x);
                    }
                    if ((inst.aux & (wide_alu_wide << 2U)) != 0U) {
                        place(inst.y);
                    }
                    if ((inst.aux & (wide_alu_wide << 3U)) != 0U) {
                        place(inst.z);
                    }
                    break;
                case KOp::extract:
                case KOp::dynamic_extract:
                case KOp::dynamic_part_select:
                    if (inst.sub == 3U) {
                        place(inst.x);
                    }
                    break;
                case KOp::mem_read:
                    // An element of more than 64 bits is the chunks of d.
                    if (inst.sub == 1U) {
                        place(inst.d);
                    }
                    break;
                case KOp::mem_write:
                    if (inst.sub == 1U) {
                        place(inst.x);
                    }
                    break;
                default:
                    break;
                }
            }
            for (auto& operand : body.concat) {
                if (operand.width > 64U) {
                    place(operand.reg);
                }
            }
        }
    }
    // The register-file items of each frame operation, now that the wide
    // storage and the 'U' shadows have their indices: a narrow register and
    // its shadow word, or a wide register's chunks and masks. A frame holds
    // a header pair (the identity) and the pairs of every listed register;
    // a pop lists the ones its call does not keep.
    for (auto& save : body.frame_saves) {
        save.item_begin = static_cast<std::uint32_t>(body.frame_items.size());
        std::uint32_t offset = 0U;
        for (auto at = save.register_begin; at < save.register_end; ++at) {
            const auto id = body.frame_registers[at] & 0x7fffffffU;
            const bool keep = (body.frame_registers[at] & 0x80000000U) != 0U;
            if (width[id] == 0U) {
                continue;
            }
            const auto add = [&](const std::uint32_t index, const std::uint32_t count) {
                if (!keep) {
                    body.frame_items.push_back({ index, count, offset });
                }
                offset += count;
            };
            if (id < body.wide_base.size() && body.wide_base[id] != 0U) {
                const auto chunks = wide_chunks(width[id]);
                add(body.wide_base[id] & ~wide_unknown_flag,
                    (body.wide_base[id] & wide_unknown_flag) != 0U ? 2U * chunks
                                                                    : chunks);
            } else if (width[id] > 64U) {
                throw reject("register_width");
            } else {
                add(id, 1U);
                if (body.shadow_base != 0U && id < body.tracked.size()
                    && body.tracked[id] != 0U) {
                    add(body.shadow_base + id, 1U);
                }
            }
        }
        save.item_end = static_cast<std::uint32_t>(body.frame_items.size());
        save.pairs = 1U + offset;
    }
    if (has_wide) {
        body.wide_registers.assign(register_count, PackedLogic4 { });
    }
    // Generated code calls each generic instruction's operation helper by
    // the operation's ordinal.
    for (auto& inst : body.code) {
        if (inst.op == KOp::generic && inst.x < member.operations().size()) {
            inst.sub = static_cast<std::uint8_t>(
                operation_kind_ordinal(member.operations()[inst.x]));
        }
    }
    if (family_generic_) {
        family_widths_ = width;
        family_in_place_.assign(register_count, 0U);
        for (RegisterId reg = 0U; reg < register_count; ++reg) {
            family_in_place_[reg] = field_family[reg] != no_slot ? 1U : 0U;
        }
    }
    return body;
}

bool Interpreter::Impl::StaticKernel::design_may_hold_weak()
{
    if (weak_possible_) {
        return *weak_possible_;
    }
    // Every way a weak element ('H', 'L', 'W', '-') can arise in a design
    // (the owner's rule: found at plan time, never at run time; a false
    // positive costs speed, a false negative is a bug):
    //  - a client other than the design writes values: a foreign library
    //    (VPI, VHPI, PLI, DPI), a Tcl session, the debugger (weak_sources_);
    //  - a constant: a LoadConstant of any process of the design (host
    //    processes and compact constant drivers such as `sda <= 'H'`
    //    included), the initial value of any signal or container element,
    //    the value of any driver, a pull or weak driver strength (which a
    //    VHDL reader sees as 'H', 'L' or 'W'), a Logic9 slot of the kernel;
    //  - a value made from data at run time: 'VAL, 'VALUE (and every
    //    conversion of an integer, character or string to a std_ulogic
    //    value) reach SimIR as an Extract of a non-Logic9 register into a
    //    Logic9 one, or as an operation outside those that only derive their
    //    result from Logic9 inputs; any process with such an operation
    //    writing a Logic9 register, and any file read (TEXTIO read, READ of
    //    a file of std_ulogic) counts. 'SUCC, 'PRED, 'LEFTOF and 'RIGHTOF
    //    are IntegerUnary/IntegerBinary on the ordinal followed by that
    //    Extract.
    // Resolution, the logic operators, TO_X01 and TO_UX01 (outputs 0, 1, X,
    // U), copies, selects and shifts only derive weak elements from weak
    // inputs, which are one of these sources.
    bool weak = weak_sources_;
    for (const auto& slot : slots_) {
        const auto rows = slot.planes == 4U ? 4U : 0U;
        for (std::uint32_t word = 0U; word < slot.words && !weak && rows != 0U;
             ++word) {
            const auto* planes = arena_.data() + slot.offset;
            const auto p0 = planes[word];
            const auto p1 = planes[slot.words + word];
            const auto p2 = planes[2U * slot.words + word];
            const auto p3 = planes[3U * slot.words + word];
            weak = (p3 | (p2 & (p1 | p0))) != 0U;
        }
    }
    const auto signal_count = impl_.signals.size();
    for (std::size_t id = 0U; id < signal_count && !weak; ++id) {
        const auto signal = static_cast<SignalId>(id);
        weak = has_weak_codes(impl_.get_signal(signal).initial_value);
        if (!weak && id < impl_.driver_values.size()) {
            impl_.driver_values[id].for_each_in_process_order(
                [&](const DriverRecord& record) {
                    weak = weak || has_weak_codes(record.value)
                        || record.strength.zero != StrengthRank::strong
                        || record.strength.one != StrengthRank::strong;
                });
        }
    }
    const auto container_weak = [](const auto& self, const ContainerValue& value) -> bool {
        return std::ranges::any_of(value.elements,
                   [](const PackedLogic4& element) { return has_weak_codes(element); })
            || std::ranges::any_of(value.nested_elements,
                [&](const ContainerValue& nested) { return self(self, nested); });
    };
    for (std::size_t id = 0U; id < impl_.container_objects.size() && !weak; ++id) {
        weak = container_weak(container_weak, impl_.container_objects[id].initial_value);
    }
    std::vector<RegisterId> reads;
    std::optional<RegisterId> written;
    // One operation of a program whose Logic9 registers are those `logic9`
    // names.
    const auto scan_operation = [&](const Operation& operation, const auto& logic9) {
        visit_operation([&](const auto& o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, LoadConstant>) {
                weak = weak || has_weak_codes(o.value);
            } else if constexpr (std::is_same_v<T, FileReadLine>
                || std::is_same_v<T, FileScan>
                || std::is_same_v<T, FileBinaryRead>) {
                weak = true;
            }
        }, operation);
        if (weak) {
            return;
        }
        written.reset();
        operation_registers(operation, reads, written);
        if (!written || !logic9(*written)) {
            return;
        }
        // A Logic9 result: derived from Logic9 data, or made from other
        // data.
        bool derived = false;
        visit_operation([&](const auto& o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, LoadConstant>
                || std::is_same_v<T, CopyRegister>
                || std::is_same_v<T, UnaryNot>
                || std::is_same_v<T, LogicalNot>
                || std::is_same_v<T, LogicalBinary>
                || std::is_same_v<T, Binary>
                || std::is_same_v<T, Reduction>
                || std::is_same_v<T, Shift>
                || std::is_same_v<T, Insert>
                || std::is_same_v<T, Concatenate>
                || std::is_same_v<T, ConditionalSelect>
                || std::is_same_v<T, ConvertToTwoState>
                || std::is_same_v<T, DynamicInsert>
                || std::is_same_v<T, DynamicPartInsert>
                || std::is_same_v<T, ReadSignal>
                || std::is_same_v<T, ReadContainerObject>
                || std::is_same_v<T, ContainerRead>
                || std::is_same_v<T, ContainerElementRead>
                || std::is_same_v<T, ContainerAggregateRead>
                // Integer and status results of string and file operations
                // (a length, a character code, a comparison, an end-of-file
                // flag, a handle) hold no weak element.
                || std::is_same_v<T, StringMethod>
                || std::is_same_v<T, StringLength>
                || std::is_same_v<T, StringIndex>
                || std::is_same_v<T, CompareStrings>
                || std::is_same_v<T, FileOpen>
                || std::is_same_v<T, FileClose>
                || std::is_same_v<T, FileEndOfFile>
                || std::is_same_v<T, FileErrorStatus>
                || std::is_same_v<T, FilePosition>) {
                derived = true;
            } else if constexpr (std::is_same_v<T, Extract>
                || std::is_same_v<T, DynamicExtract>
                || std::is_same_v<T, DynamicPartSelect>) {
                derived = logic9(o.source);
            }
        }, operation);
        weak = weak || !derived;
    };
    // The processes still in the process table (the host's) ...
    const auto process_count = impl_.processes.size();
    for (std::size_t id = 0U; id < process_count && !weak; ++id) {
        const auto program = impl_.processes.program_view(static_cast<ProcessId>(id));
        const auto& operations = program.operations();
        const auto& kinds = program.register_value_kinds();
        const auto logic9 = [&](const RegisterId reg) {
            return reg < kinds.size() && kinds[reg] == ValueKind::logic9;
        };
        Operation scratch;
        for (std::size_t op = 0U; op < operations.size() && !weak; ++op) {
            scan_operation(operations.expanded_ref(op, scratch), logic9);
        }
    }
    // ... and the kernel's members, whose programs the kernel took over
    // (the table holds a stub for them).
    for (const auto& member : members_) {
        const auto logic9 = [&](const RegisterId reg) {
            return reg < member.register_kinds.size()
                && member.register_kinds[reg] == ValueKind::logic9;
        };
        const auto& operations = member.operations();
        for (std::size_t op = 0U; op < operations.size() && !weak; ++op) {
            scan_operation(operations[op], logic9);
        }
        if (weak) {
            break;
        }
    }
    weak_possible_ = weak;
    return weak;
}

} // namespace fsim::runtime::simir

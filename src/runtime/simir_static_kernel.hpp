// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 static RTL kernel (docs/simulation-engine-v4-plan.md, Phase 1).
// Statically scheduled SystemVerilog processes (continuous assignments,
// combinational and edge-triggered always blocks, constant and one-shot
// initializers) execute inside one kernel hosted by an ordinary process of the
// existing scheduler. Kernel-owned signals and memories live in kernel storage;
// boundary outputs are published to the scheduler under their original driver
// identity, and observation materializes hidden state on demand.
#pragma once

#include "fsim/runtime/constraint_solver.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_graph.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

class StaticKernelCodegen;

enum class StaticKernelMemberKind : std::uint8_t {
    /// Re-evaluated whenever an any-change sensitivity entry changes.
    combinational,
    /// Runs on posedge/negedge of its static sensitivity entries.
    sequential,
    /// Runs once at time zero and halts.
    once,
    /// A testbench process run as resumable threads (timed and event waits,
    /// fork/join, calls and runtime-library output);
    /// FSIM_STATIC_KERNEL_BEHAVIORAL=0 leaves such processes on the host.
    behavioral,
    /// A process the kernel schedules but does not evaluate: its own
    /// ProcessState is executed by the reference evaluator
    /// (Interpreter::Impl::execute) at the point of the kernel's region loop
    /// that the reference scheduler would run it (docs/engine-v4-semantics.md
    /// S25). Hosted processes are listed in StaticKernelRuntimeSpec::hosted,
    /// not in `members`.
    hosted,
};

struct StaticKernelMemberSpec {
    ProcessId process { };
    StaticKernelMemberKind kind { StaticKernelMemberKind::combinational };
    /// First and one-past-last body operation; the wait and loop jump are
    /// structural and excluded.
    std::uint32_t body_begin { };
    std::uint32_t body_end { };
    /// A second operation index that ends an activation (the wait when
    /// subprogram bodies follow the loop jump); the maximum means none.
    std::uint32_t exit_alt { std::numeric_limits<std::uint32_t>::max() };
    /// Combinational bodies placed before their wait run at time zero.
    bool run_at_start { };
    /// The member's original static sensitivity; dormant members are
    /// installed without one.
    std::vector<Sensitivity> sensitivity;
    /// Combinational members with the same key (their module instance)
    /// compile into one partition program; the maximum means none.
    std::uint32_t partition { std::numeric_limits<std::uint32_t>::max() };
    /// A VHDL process (delta semantics); otherwise SystemVerilog.
    bool vhdl { };
    /// A behavioral thread that starts one delta after time zero's other
    /// processes (systemverilog_late_starters).
    bool start_late { };
    /// A behavioral process that is not initialized (an implicit signal's
    /// driver): armed on its static sensitivity at time zero without
    /// running (StaticKernel::prepare_behavioral).
    bool start_waiting { };
    /// Dead logic (plan dead-logic removal): a combinational member whose
    /// outputs no live process reads. It is never scheduled; a read of what
    /// it writes (materialize) evaluates it first from its current inputs.
    bool lazy { };
    /// VHDL delta collapsing (exact subset): a pure combinational member
    /// whose every reader is another such member or wakes only before a
    /// time step's first delta. The kernel may evaluate it in the update
    /// phase that changed its inputs (StaticKernel::plan_delta_collapse).
    bool collapse { };
    /// A VHDL port-association conversion (IEEE 1076-2008 6.5.7.1,
    /// 14.7.3.3): the member is the program of this signal's resolver,
    /// which the kernel runs in the update phase that changes one of its
    /// sources and whose result is the signal's new effective value. The
    /// maximum means none.
    SignalId conversion_target { std::numeric_limits<SignalId>::max() };
};

/// A kernel-owned aggregate proxy: its value is the concatenation of its
/// kernel-owned leaves at their bit offsets.
struct StaticKernelAliasLeaf {
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
};

struct StaticKernelAliasFamily {
    SignalId proxy { };
    std::uint32_t width { };
    std::vector<StaticKernelAliasLeaf> leaves;
};

enum class StaticKernelContainerStorage : std::uint8_t {
    /// The kernel stores the elements itself.
    elements,
    /// Each element is a kernel-owned signal, indexed by ordinal.
    element_signals,
    /// The whole memory is one kernel-owned packed signal.
    packed_signal,
};

struct StaticKernelContainerSpec {
    ContainerObjectId object { };
    StaticKernelContainerStorage storage {
        StaticKernelContainerStorage::elements
    };
    std::vector<SignalId> element_signals;
    SignalId packed_signal { };
};

/// One member's driven bit range of a kernel-owned signal (width 0 is the
/// whole signal).
struct StaticKernelWriterRegion {
    SignalId signal { };
    ProcessId process { };
    std::uint32_t offset { };
    std::uint32_t width { };
};

/// One VHDL member's specialization outcome (simir_static_kernel_specialize):
/// elaborate --aot records them, and a later simulate of the same artifact
/// restores them instead of specializing again.
struct StaticKernelSpecialization {
    ProcessId process { };
    /// The member's operation count before specialization (checked when
    /// restored).
    std::uint32_t original_size { };
    bool specialized { };
    /// The specialized activation appended to the member's operations, and
    /// the original operation each appended one came from.
    std::vector<Operation> operations;
    std::vector<InstructionIndex> origin;
};

struct StaticKernelSpecializations {
    std::vector<StaticKernelSpecialization> members;
};

/// Identifies the specializer, for persisted specializations' cache keys.
[[nodiscard]] std::string static_kernel_specializer_identity();

struct StaticKernelRuntimeSpec {
    ProcessId host { };
    /// The host's original program; the scheduler runs the host stub.
    Process host_original;
    std::vector<StaticKernelMemberSpec> members;
    /// SystemVerilog processes the kernel schedules and the reference
    /// evaluator executes (StaticKernelMemberKind::hosted); only `process`
    /// is meaningful. Empty for VHDL and mixed kernels.
    std::vector<StaticKernelMemberSpec> hosted;
    /// A kernel-owned signal with a pure-routing host resolver
    /// (static_kernel_wiring) over another owned signal is a wired formal:
    /// the kernel drives it in the update phase that changes its source.
    std::vector<SignalId> owned_signals;
    std::vector<SignalId> boundary_outputs;
    std::vector<StaticKernelAliasFamily> families;
    std::vector<StaticKernelContainerSpec> containers;
    /// Optional native code generator for kernel program templates.
    std::shared_ptr<StaticKernelCodegen> codegen;
    /// VHDL delta mode: every member is a VHDL process. Signal assignments
    /// are deferred to the end of the kernel round, so one round is exactly
    /// one VHDL delta cycle (IEEE 1076-2019 14.7.5), and members woken by a
    /// round's events run in the next round.
    bool vhdl { };
    /// Mixed-language kernel: VHDL members follow delta rounds and
    /// SystemVerilog members the Active/NBA regions, interleaved as the
    /// reference scheduler interleaves generic deltas and SystemVerilog
    /// regions (`vhdl` is also set).
    bool mixed { };
    /// Publish-all mode: some process has statically unknown signal accesses
    /// and stays on the host, so it may read or write any signal. Every
    /// owned signal is then a boundary output (the host copy is always
    /// current for it), and a value the host commits to an owned signal is
    /// taken into the kernel (StaticKernel::import_host_write).
    bool import_host_writes { };
    /// Debugger plan (StaticKernelAccessPlan::debug): every member is a
    /// behavioral thread, and its debug points are executable: compiled
    /// bodies stop at each one (KOp::suspend), and the thread runner raises
    /// the execution-point hook there. Without it they compile to nothing.
    bool debug_points { };
    /// Writer ranges of owned signals in VHDL mode, merged per process; they
    /// replace the members' declared driver regions for publication.
    std::vector<StaticKernelWriterRegion> writer_regions;
    /// Host signals members read that no process writes (constant port
    /// actuals, undriven nets). They stay inputs, but host processes cannot
    /// change them between kernel rounds.
    std::vector<SignalId> unwritten_inputs;
    /// Host signals members read whose only driver is one member process.
    /// The host applies that member's Active-region updates unchanged, so the
    /// kernel mirrors them as it stages them.
    std::vector<SignalId> sole_writer_inputs;
    /// Specializations restored from an earlier run (members not listed are
    /// specialized as usual).
    std::shared_ptr<StaticKernelSpecializations> specializations;
    /// When set, the kernel records every VHDL member's specialization here.
    std::shared_ptr<StaticKernelSpecializations> record_specializations;
    /// Compiled member bodies from an earlier run of the same build
    /// (static_kernel_compiler_identity), restored instead of compiling, and
    /// where this run records its own; the bytes are the kernel's own.
    std::shared_ptr<const std::string> compiled_bodies;
    std::shared_ptr<std::string> record_compiled_bodies;
    /// Canonical templates and their units' bindings, likewise
    /// (static_kernel_canonical_identity).
    std::shared_ptr<const std::string> native_image;
    std::shared_ptr<std::string> record_native_image;
};

/// Identifies the template canonicalization, for persisted native images.
[[nodiscard]] std::string static_kernel_canonical_identity();

/// Identifies the member compiler, for persisted compiled bodies.
[[nodiscard]] std::string static_kernel_compiler_identity();

/// Whether a fork child of a program can hold an automatic callable frame
/// across a suspension. An operation that returns a behavioral thread to its
/// runner (a wait, fork, join, halt, $stop or $pause) reachable from a Call
/// target before its Return, or from a frame push before the pop of the same
/// callable, makes that callable suspending. A SystemVerilog function never
/// is (IEEE 1800-2023 13.4: no timing controls in a function); a task may
/// (13.3). The hazard is a fork child entering a suspending callable, or a
/// fork the parent does not join (join_any, join_none) while a suspending
/// callable exists: only then may two threads of the member be inside one
/// callable's registers with one of them suspended.
template <typename Operations>
[[nodiscard]] bool static_kernel_fork_holds_suspended_frame(
    const Operations& operations, const std::size_t count)
{
    struct Entry {
        InstructionIndex pc { };
        std::optional<std::uint32_t> frame;
        bool suspends { };
        std::vector<std::size_t> callees;
    };
    std::vector<Entry> entries;
    std::unordered_map<InstructionIndex, std::size_t> call_entry;
    std::unordered_map<std::uint32_t, std::size_t> frame_entry;
    std::vector<InstructionIndex> branches;
    bool unjoined_fork = false;
    for (std::size_t op = 0U; op < count; ++op) {
        const auto operation = operations.expanded(op);
        if (const auto* call = operation_get_if<Call>(&operation)) {
            if (call_entry.try_emplace(call->target, entries.size()).second) {
                entries.push_back({ call->target, std::nullopt, false, { } });
            }
        } else if (const auto* push
                   = operation_get_if<CallableFramePush>(&operation)) {
            if (frame_entry.try_emplace(push->identity, entries.size()).second) {
                entries.push_back({ static_cast<InstructionIndex>(op + 1U),
                    push->identity, false, { } });
            }
        } else if (const auto* fork = operation_get_if<Fork>(&operation)) {
            branches.insert(branches.end(), fork->branches.begin(),
                fork->branches.end());
            unjoined_fork = unjoined_fork
                || fork->join != ForkJoinKind::all;
        }
    }
    if (entries.empty() || branches.empty()) {
        return false;
    }
    // One callable's own operations: a Call continues at its return target
    // (the callee is its own entry), a Return or the matching pop ends it.
    std::vector<std::uint32_t> seen(count, 0U);
    std::uint32_t epoch = 0U;
    const auto walk = [&](const InstructionIndex begin,
                          const std::optional<std::uint32_t> frame,
                          const bool branch, const auto& visit) {
        ++epoch;
        std::vector<InstructionIndex> work { begin };
        while (!work.empty()) {
            const auto pc = work.back();
            work.pop_back();
            if (pc >= count || seen[pc] == epoch) {
                continue;
            }
            seen[pc] = epoch;
            const auto operation = operations.expanded(pc);
            if (frame) {
                if (const auto* pop
                    = operation_get_if<CallableFramePop>(&operation);
                    pop != nullptr && pop->identity == *frame) {
                    continue;
                }
            }
            if (visit(operation)) {
                return true;
            }
            if (operation_holds<Return>(operation)
                || (branch && operation_holds<ForkEnd>(operation))) {
                continue;
            }
            if (const auto* jump = operation_get_if<Jump>(&operation)) {
                work.push_back(jump->target);
            } else if (const auto* branch_op
                       = operation_get_if<Branch>(&operation)) {
                work.push_back(branch_op->when_true);
                work.push_back(branch_op->when_false);
            } else if (const auto* call = operation_get_if<Call>(&operation)) {
                work.push_back(call->return_target);
            } else {
                work.push_back(static_cast<InstructionIndex>(pc + 1U));
            }
        }
        return false;
    };
    for (auto& entry : entries) {
        entry.suspends = walk(entry.pc, entry.frame, false,
            [&](const auto& operation) {
                if (const auto* call = operation_get_if<Call>(&operation)) {
                    entry.callees.push_back(call_entry.at(call->target));
                }
                return operation_holds<WaitFor>(operation)
                    || operation_holds<WaitOn>(operation)
                    || operation_holds<WaitForever>(operation)
                    || operation_holds<WaitSensitivity>(operation)
                    || operation_holds<Fork>(operation)
                    || operation_holds<ForkEnd>(operation)
                    || operation_holds<Halt>(operation)
                    || operation_holds<Stop>(operation)
                    || operation_holds<Pause>(operation);
            });
    }
    // A callable calling a suspending callable suspends.
    for (bool changed = true; changed;) {
        changed = false;
        for (auto& entry : entries) {
            if (!entry.suspends
                && std::ranges::any_of(entry.callees, [&](const std::size_t callee) {
                       return entries[callee].suspends;
                   })) {
                entry.suspends = true;
                changed = true;
            }
        }
    }
    if (std::ranges::none_of(entries, &Entry::suspends)) {
        return false;
    }
    if (unjoined_fork) {
        return true;
    }
    return std::ranges::any_of(branches, [&](const InstructionIndex begin) {
        return walk(begin, std::nullopt, true, [&](const auto& operation) {
            if (const auto* call = operation_get_if<Call>(&operation)) {
                return entries[call_entry.at(call->target)].suspends;
            }
            if (const auto* push
                = operation_get_if<CallableFramePush>(&operation)) {
                return entries[frame_entry.at(push->identity)].suspends;
            }
            return false;
        });
    });
}

/// Operations the kernel evaluates. Structural waits, loop jumps and halts are
/// classified separately by the planner.
[[nodiscard]] inline bool static_kernel_operation_supported(
    const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ReadSignal>) {
            return value.kind == SignalReadKind::current && value.ticks == 1U
                && !value.clock && !value.gate;
        } else if constexpr (std::is_same_v<T, WriteUpdate>
            || std::is_same_v<T, WriteUpdateSlice>
            || std::is_same_v<T, WriteUpdateDynamicSlice>
            || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
            return value.domain != SignalUpdateDomain::generic;
        } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
            return !value.transaction_signal;
        } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
            // The math and conversion functions (IEEE 1800-2023 20.5, 20.8);
            // the time queries read the time (see the sequential set).
            return value.function < SystemVerilogMathFunction::Time;
        } else {
            return std::is_same_v<T, DebugPoint>
                // Real, shortreal and time arithmetic, comparisons and
                // conversions (6.12, 11.3.1) on the operation step.
                || std::is_same_v<T, SystemVerilogScalarBinary>
                // A code coverage counter bump (host data, no registers).
                || std::is_same_v<T, CodeCoverageHit>
                || std::is_same_v<T, LoadConstant>
                || std::is_same_v<T, CopyRegister>
                || std::is_same_v<T, Binary>
                || std::is_same_v<T, Reduction>
                || std::is_same_v<T, UnaryNot>
                || std::is_same_v<T, LogicalNot>
                || std::is_same_v<T, LogicalBinary>
                || std::is_same_v<T, Shift>
                || std::is_same_v<T, Extract>
                || std::is_same_v<T, Insert>
                || std::is_same_v<T, Concatenate>
                || std::is_same_v<T, ConditionalSelect>
                || std::is_same_v<T, ConvertToTwoState>
                || std::is_same_v<T, DynamicExtract>
                || std::is_same_v<T, DynamicPartSelect>
                || std::is_same_v<T, DynamicInsert>
                || std::is_same_v<T, DynamicPartInsert>
                || std::is_same_v<T, Jump>
                || std::is_same_v<T, Branch>
                || std::is_same_v<T, WriteBlocking>
                || std::is_same_v<T, WriteBlockingSlice>
                || std::is_same_v<T, WriteBlockingDynamicSlice>
                || std::is_same_v<T, WriteBlockingDynamicPartSlice>
                || std::is_same_v<T, ReadContainerObject>
                || std::is_same_v<T, ContainerRead>;
        }
    }, operation);
}

/// Operations a SystemVerilog sequential member may use beyond the static
/// set: a severity message below failure (an immediate assertion's $info,
/// $warning or $error action, IEEE 1800-2023 16.3, 20.10) and immediate
/// output ($display and $write, 21.2.1; $strobe and $monitor are postponed
/// and stay off), and the time queries $time, $stime and $realtime (20.3).
/// The member runs once per triggering edge, as the process does on the
/// host, so the text is emitted as often; the kernel's operation step emits
/// it through the report and output hooks, and a parallel batch emits it in
/// the replay (StaticKernel::LaneOutput).
[[nodiscard]] inline bool static_kernel_sequential_operation_supported(
    const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Report>) {
            return value.severity != AssertionSeverity::failure;
        } else if constexpr (std::is_same_v<T, Display>
            || std::is_same_v<T, FormatDisplay>
            || std::is_same_v<T, TimeDisplay>) {
            return !value.postponed;
        } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
            return value.function >= SystemVerilogMathFunction::Time;
        } else {
            return false;
        }
    }, operation);
}

/// A SystemVerilog combinational or sequential member's program with every
/// call in its body expanded in place. A function call runs to completion
/// inside its caller's activation (IEEE 1800-2023 13.4), and an acyclic
/// chain of callables whose frames own their registers (native_isolated)
/// needs no frame save or restore, so the expanded body runs the same
/// operations in the same order: each Call becomes its callee's code, each
/// Return a jump to the call's return target, and the frame pushes and pops
/// are dropped. The program keeps its shape, the wait and the loop jump to
/// operation 0, and leaves out the callee code after the loop jump.
struct StaticKernelInlinedProgram {
    std::vector<Operation> operations;
    std::uint32_t body_begin { };
    std::uint32_t body_end { };
    std::uint32_t exit_alt { };
};

/// `wait` is the program's static wait and `jump` its loop jump: either the
/// body precedes the wait (`jump` is `wait` + 1) or the wait is operation 0
/// and the body runs to `jump`. Empty when the body makes no call, a call
/// recurses, a frame is not native_isolated, control leaves a callee other
/// than by Return or the expansion exceeds its size limit.
[[nodiscard]] std::optional<StaticKernelInlinedProgram> static_kernel_inline_calls(
    std::span<const Operation> operations, std::uint32_t wait, std::uint32_t jump);

/// The file operations a behavioral member runs on its registers through the
/// reference's run_file (IEEE 1800-2023 21.3, IEEE 1076-2019 16.4); $fread
/// (FileBinaryRead, 21.3.4.4) fills a register, a signal or an array.
[[nodiscard]] inline bool static_kernel_file_operation(
    const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        return std::is_same_v<T, FileOpen> || std::is_same_v<T, FileClose>
            || std::is_same_v<T, FileWriteLiteral>
            || std::is_same_v<T, FileWriteFormatted>
            || std::is_same_v<T, FileWriteString>
            || std::is_same_v<T, FileReadLine>
            || std::is_same_v<T, FileEndOfFile>
            || std::is_same_v<T, FileErrorStatus>
            || std::is_same_v<T, FileScan>
            || std::is_same_v<T, FilePosition>
            || std::is_same_v<T, FileFlush>
            || std::is_same_v<T, FileBinaryRead>;
    }, operation);
}

/// A container register type the kernel's arena holds: a fixed array of
/// packed or scalar elements. A SystemVerilog behavioral member holds
/// other registers in the reference's format (Member::dynamic_containers).
[[nodiscard]] inline bool static_kernel_arena_container_type(
    const ContainerType& type)
{
    return type.fixed && !type.associative && !type.vhdl_shaped
        && (type.element_kind == ContainerElementKind::Packed
            || type.element_kind == ContainerElementKind::Scalar);
}

/// SystemVerilog container operations the kernel runs with the reference's
/// own code (simir_container_ops) on reference-format registers: dynamic
/// arrays, queues and associative arrays and their methods (IEEE 1800-2023
/// 7.5-7.12), array patterns, streaming (11.4.14) and element access to
/// unpacked aggregates.
template <typename T>
inline constexpr bool static_kernel_reference_container_operation
    = std::is_same_v<T, ResizeContainer>
    || std::is_same_v<T, ConditionalContainerSelect>
    || std::is_same_v<T, CompareContainers>
    || std::is_same_v<T, ContainerSize>
    || std::is_same_v<T, ContainerReduction>
    || std::is_same_v<T, OrderContainer>
    || std::is_same_v<T, LocateContainer>
    || std::is_same_v<T, ContainerStringRead>
    || std::is_same_v<T, ContainerStringWrite>
    || std::is_same_v<T, FormatContainerPattern>
    || std::is_same_v<T, ContainerElementRead>
    || std::is_same_v<T, ContainerElementWrite>
    || std::is_same_v<T, ContainerAggregateRead>
    || std::is_same_v<T, ContainerAggregateWrite>
    || std::is_same_v<T, CopyContainerAggregateElement>
    || std::is_same_v<T, DeleteContainer>
    || std::is_same_v<T, ContainerExists>
    || std::is_same_v<T, TraverseContainer>
    || std::is_same_v<T, PushContainer>
    || std::is_same_v<T, PopContainer>
    || std::is_same_v<T, AppendContainer>
    || std::is_same_v<T, ContainerBitstream>
    // VHDL arrays with run-time index ranges (IEEE 1076-2019 5.3.2.2).
    || std::is_same_v<T, VhdlShaped>;

/// std.env's directory, call-path and reflection operations (IEEE 1076-2019
/// 16.5.6, 16.6): the reference's run_vhdl_* functions keep their values in
/// reference-format registers.
template <typename T>
inline constexpr bool static_kernel_vhdl_api_container_operation
    = std::is_same_v<T, VhdlEnvironmentDirectory>
    || std::is_same_v<T, VhdlEnvironmentGetCallPath>
    || std::is_same_v<T, VhdlEnvironmentCallPath>
    || std::is_same_v<T, VhdlReflectionApi>;

/// Class services and std::randomize with container operands (IEEE
/// 1800-2023 7.12, 8.5, 18.7, 18.12): the reference's run_class_operation
/// and run_scope_randomize read and write them as reference-format
/// registers.
template <typename T>
inline constexpr bool static_kernel_class_container_operation
    = std::is_same_v<T, ClassPropertyContainerRead>
    || std::is_same_v<T, ClassPropertyContainerWrite>
    || std::is_same_v<T, ClassMethodCall>
    || std::is_same_v<T, ClassStaticMethodCall>
    || std::is_same_v<T, ScopeRandomize>;

/// Calls `visit(id)` for each container register a with-clause names (an
/// inline_container_prefix name, bind_inline_constraint_containers).
template <typename Visit>
inline void static_kernel_inline_constraint_containers(
    const std::span<const SystemVerilogConstraintTemplate> constraints,
    Visit&& visit)
{
    const auto walk = [&](const auto& self,
                          const SystemVerilogConstraintTemplate& node) -> void {
        if (node.text.starts_with(inline_container_prefix)) {
            visit(static_cast<ContainerRegisterId>(std::stoul(
                node.text.substr(inline_container_prefix.size()))));
            return;
        }
        for (const auto& operand : node.operands) {
            self(self, operand);
        }
    };
    for (const auto& node : constraints) {
        walk(walk, node);
    }
}

/// Calls `visit(id)` for each container register a reference container
/// operation, ContainerRead, ContainerWrite or CopyContainerRegister names.
template <typename Visit>
inline void static_kernel_container_operands(
    const Operation& operation, Visit&& visit)
{
    visit_operation([&](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, ResizeContainer>) {
            visit(op.target);
            if (op.initializer) {
                visit(*op.initializer);
            }
        } else if constexpr (std::is_same_v<T, ConditionalContainerSelect>) {
            visit(op.destination);
            visit(op.when_true);
            visit(op.when_false);
        } else if constexpr (std::is_same_v<T, CompareContainers>) {
            visit(op.lhs);
            visit(op.rhs);
        } else if constexpr (std::is_same_v<T, LocateContainer>
            || std::is_same_v<T, ContainerElementRead>
            || std::is_same_v<T, CopyContainerRegister>) {
            visit(op.destination);
            visit(op.source);
        } else if constexpr (std::is_same_v<T, ContainerElementWrite>
            || std::is_same_v<T, CopyContainerAggregateElement>) {
            visit(op.target);
            visit(op.source);
        } else if constexpr (std::is_same_v<T, AppendContainer>) {
            visit(op.target);
            if (op.container) {
                visit(*op.container);
            }
        } else if constexpr (std::is_same_v<T, ContainerBitstream>) {
            visit(op.bits);
            visit(op.container);
        } else if constexpr (std::is_same_v<T, VhdlEnvironmentDirectory>) {
            if (op.directory) {
                visit(*op.directory);
            }
        } else if constexpr (std::is_same_v<T, VhdlEnvironmentGetCallPath>) {
            visit(op.destination);
        } else if constexpr (std::is_same_v<T, VhdlEnvironmentCallPath>) {
            if (op.source_value) {
                visit(*op.source_value);
            }
        } else if constexpr (std::is_same_v<T, VhdlReflectionApi>) {
            if (op.access_heap) {
                visit(*op.access_heap);
            }
        } else if constexpr (std::is_same_v<T, ClassPropertyContainerRead>) {
            visit(op.destination);
        } else if constexpr (std::is_same_v<T, ClassPropertyContainerWrite>) {
            visit(op.source);
        } else if constexpr (std::is_same_v<T, ClassMethodCall>) {
            static_kernel_inline_constraint_containers(
                op.inline_constraints, visit);
        } else if constexpr (std::is_same_v<T, ScopeRandomize>) {
            for (const auto& container : op.containers) {
                visit(container.container);
            }
        } else if constexpr (std::is_same_v<T, ClassStaticMethodCall>) {
            // A stream actual (kind 2) is a container register.
            for (std::size_t index = 0U; index < op.actuals.size()
                 && index < op.actual_kinds.size(); ++index) {
                if (op.actual_kinds[index] == 2U) {
                    visit(op.actuals[index]);
                }
            }
        } else if constexpr (std::is_same_v<T, VhdlShaped>) {
            // The registers each operation uses (an unused one is zero).
            switch (op.operation) {
            case VhdlShapedOperation::create:
            case VhdlShapedOperation::from_packed:
            case VhdlShapedOperation::element_write:
            case VhdlShapedOperation::slice_write:
            case VhdlShapedOperation::append_packed:
                visit(op.target);
                break;
            case VhdlShapedOperation::query:
            case VhdlShapedOperation::to_packed:
            case VhdlShapedOperation::slice_read:
            case VhdlShapedOperation::equal_packed:
                visit(op.source);
                break;
            case VhdlShapedOperation::element_read:
            case VhdlShapedOperation::slice:
            case VhdlShapedOperation::assign:
            case VhdlShapedOperation::equal:
            case VhdlShapedOperation::append:
                visit(op.target);
                visit(op.source);
                break;
            }
        } else if constexpr (std::is_same_v<T, ContainerSize>
            || std::is_same_v<T, ContainerReduction>
            || std::is_same_v<T, ContainerStringRead>
            || std::is_same_v<T, FormatContainerPattern>
            || std::is_same_v<T, ContainerAggregateRead>
            || std::is_same_v<T, ContainerExists>
            || std::is_same_v<T, TraverseContainer>
            || std::is_same_v<T, ContainerRead>) {
            visit(op.source);
        } else if constexpr (std::is_same_v<T, OrderContainer>
            || std::is_same_v<T, ContainerStringWrite>
            || std::is_same_v<T, ContainerAggregateWrite>
            || std::is_same_v<T, DeleteContainer>
            || std::is_same_v<T, PushContainer>
            || std::is_same_v<T, PopContainer>
            || std::is_same_v<T, ContainerWrite>) {
            visit(op.target);
        }
    }, operation);
}

/// The container registers a behavioral member holds in reference format:
/// those a reference container operation names, those of a type the arena
/// does not hold that a SystemVerilog member uses (`by_type`; a VHDL
/// member's other registers keep their kernel shapes), and those a copy
/// connects to either. `operation_at(op)` is the member's operation `op` of
/// `count`; `types` its container register types. `host_objects` marks the
/// design objects that stay on the host: a register bound to one, or
/// written to one, holds a reference-format copy of the host's value.
template <typename OperationAt, typename Types>
[[nodiscard]] std::vector<std::uint8_t> static_kernel_dynamic_container_registers(
    const std::size_t count, OperationAt&& operation_at, const Types& types,
    const bool by_type = true,
    const std::vector<std::uint8_t>* host_objects = nullptr)
{
    const auto host_object = [&](const ContainerObjectId object) {
        return host_objects != nullptr && object < host_objects->size()
            && (*host_objects)[object] != 0U;
    };
    std::vector<std::uint8_t> dynamic(types.size(), 0U);
    const auto mark = [&](const ContainerRegisterId id) {
        if (id < dynamic.size() && dynamic[id] == 0U) {
            dynamic[id] = 1U;
            return true;
        }
        return false;
    };
    std::vector<std::pair<ContainerRegisterId, ContainerRegisterId>> copies;
    for (std::size_t op = 0U; op < count; ++op) {
        const auto& operation = operation_at(op);
        const bool reference = visit_operation([](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            return static_kernel_reference_container_operation<T>
                || static_kernel_vhdl_api_container_operation<T>
                || static_kernel_class_container_operation<T>;
        }, operation);
        static_kernel_container_operands(operation,
            [&](const ContainerRegisterId id) {
                if (reference
                    || (by_type && id < types.size()
                        && !static_kernel_arena_container_type(types[id]))) {
                    mark(id);
                }
            });
        if (const auto* bind = operation_get_if<ReadContainerObject>(&operation);
            bind != nullptr && host_object(bind->object)) {
            mark(bind->destination);
        } else if (const auto* write
                   = operation_get_if<WriteContainerObject>(&operation);
                   write != nullptr && host_object(write->object)) {
            mark(write->source);
        }
        if (!by_type) {
            if (const auto* copy
                = operation_get_if<CopyContainerRegister>(&operation)) {
                copies.emplace_back(copy->destination, copy->source);
            }
        } else if (const auto* bind
            = operation_get_if<ReadContainerObject>(&operation);
            bind != nullptr && bind->destination < types.size()
            && !static_kernel_arena_container_type(types[bind->destination])) {
            mark(bind->destination);
        } else if (const auto* write
                   = operation_get_if<WriteContainerObject>(&operation);
                   write != nullptr && write->source < types.size()
                   && !static_kernel_arena_container_type(types[write->source])) {
            mark(write->source);
        } else if (const auto* copy
                   = operation_get_if<CopyContainerRegister>(&operation)) {
            copies.emplace_back(copy->destination, copy->source);
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [destination, source] : copies) {
            if (destination < dynamic.size() && source < dynamic.size()
                && dynamic[destination] != dynamic[source]) {
                changed = mark(destination) || changed;
                changed = mark(source) || changed;
            }
        }
    }
    return dynamic;
}

/// The assertion operations of a SystemVerilog behavioral member: sampled
/// value reads (IEEE 1800-2023 16.5.1, 16.9.3), concurrent assertion
/// monitors (SvaOperation; 16.14) and a move to a later region (WaitRegion;
/// 4.4.2, 16.4). They read the host's sampled state and run in the host's
/// regions, so their member runs on the operation tier and keeps no time
/// warp.
[[nodiscard]] inline bool static_kernel_assertion_operation(
    const Operation& operation)
{
    if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
        return read->kind != SignalReadKind::current || read->ticks != 1U
            || read->clock || read->gate;
    }
    return operation_holds<SvaOperation>(operation)
        || operation_holds<WaitRegion>(operation);
}

/// Operations a behavioral member may use: the static set plus waits,
/// fork/join, calls with automatic frames of packed registers, integer
/// arithmetic, immediate runtime-library output, string constants and copies,
/// plusarg queries, $finish and element writes of process-private arrays. Postponed output and wait timeouts stay on
/// the host.
[[nodiscard]] inline bool static_kernel_behavioral_operation_supported(
    const Operation& operation)
{
    if (static_kernel_operation_supported(operation)
        || static_kernel_file_operation(operation)) {
        return true;
    }
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, WaitOn>) {
            return !value.signals.empty() && !value.timeout
                && !value.timeout_result && !value.timeout_origin
                && (value.edges.empty()
                    || value.edges.size() == value.signals.size());
        } else if constexpr (std::is_same_v<T, Halt>) {
            // $exit ends the member's program (StaticKernel::exit_program).
            return true;
        } else if constexpr (std::is_same_v<T, Display>
            || std::is_same_v<T, FormatDisplay>
            || std::is_same_v<T, StringDisplay>
            || std::is_same_v<T, TimeDisplay>) {
            return !value.postponed;
        } else if constexpr (std::is_same_v<T, ReadSignal>
            || std::is_same_v<T, SvaOperation>) {
            // Assertion operations (static_kernel_assertion_operation); the
            // planner keeps sampled signals on the host.
            return true;
        } else if constexpr (std::is_same_v<T, WaitRegion>) {
            // The later regions' hosted lists (a deferred assertion's
            // action runs in the Reactive or Postponed region; IEEE
            // 1800-2023 16.4.2).
            return value.phase == SchedulerPhase::observed
                || value.phase == SchedulerPhase::reactive
                || value.phase == SchedulerPhase::re_inactive
                || value.phase == SchedulerPhase::postponed;
        } else if constexpr (std::is_same_v<T, Fork>) {
            return !value.isolate_register_frame
                && !value.branches.empty();
        } else if constexpr (std::is_same_v<T, DisableBlock>) {
            // A named block (IEEE 1800-2023 9.6.2): of the process's own
            // code, or named by hierarchical name, whose declaring
            // processes the planner keeps members with the disabler.
            return true;
        } else if constexpr (std::is_same_v<T, ContainerWrite>
            || static_kernel_reference_container_operation<T>) {
            // Process-private fixed arrays, or reference-format registers
            // (static_kernel_dynamic_container_registers; see the planner).
            return true;
        } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
            // $time, $stime and $realtime.
            return value.function >= SystemVerilogMathFunction::Time;
        } else if constexpr (std::is_same_v<T, LoadMemory>) {
            // $readmemb/$readmemh and $writememb/$writememh (IEEE 1800-2023
            // 21.4, 21.5).
            return true;
        } else if constexpr (std::is_same_v<T, WriteContainerObject>) {
            // Only the write-back of a binding to its own memory (see the
            // planner).
            return !value.nonblocking && !value.transaction_signal;
        } else if constexpr (std::is_same_v<T, WriteStringObject>) {
            // A nonblocking string write matures in the host's NBA region.
            return !value.nonblocking;
        } else if constexpr (std::is_same_v<T, ForceSignalSlice>
            || std::is_same_v<T, ReleaseSignalSlice>) {
            // The host's force of a host signal (static_kernel_host_signal);
            // a port segment force reaches a net by another name.
            return !value.port_segment;
        } else if constexpr (std::is_same_v<T, ClassStaticMethodCall>) {
            // A static method or DPI function call (IEEE 1800-2023 8.10,
            // 35.5) through the application's services; its body may touch
            // any signal, which the planner's publish-all mode covers. A
            // VHDL driver disconnection is a VHDL member's.
            return !vhdl_disconnect_signal(value.method_identity).has_value();
        } else {
            return std::is_same_v<T, WaitFor>
                // Allocation (IEEE 1800-2023 8.7), method calls (8.6) and
                // property reads and writes (8.5, 8.9) through the
                // application's class services (Impl::run_class_operation).
                || std::is_same_v<T, ClassAllocate>
                || std::is_same_v<T, ClassMethodCall>
                // std::randomize (18.12) on the member's registers.
                || std::is_same_v<T, ScopeRandomize>
                || std::is_same_v<T, ClassPropertyContainerRead>
                || std::is_same_v<T, ClassPropertyContainerWrite>
                || std::is_same_v<T, ClassPropertyRead>
                || std::is_same_v<T, ClassPropertyWrite>
                || std::is_same_v<T, ClassStaticPropertyRead>
                || std::is_same_v<T, ClassStaticPropertyWrite>
                // History queries of host signals
                // (static_kernel_host_signal).
                || std::is_same_v<T, SignalLastValue>
                || std::is_same_v<T, SignalLastEvent>
                || std::is_same_v<T, SignalLastActive>
                || std::is_same_v<T, WaitSensitivity>
                || std::is_same_v<T, WaitForever>
                || std::is_same_v<T, ForkEnd>
                // wait fork and disable fork (IEEE 1800-2023 9.6.1, 9.6.3)
                // of the member's own threads.
                || std::is_same_v<T, WaitFork>
                || std::is_same_v<T, DisableFork>
                || std::is_same_v<T, Stop>
                || std::is_same_v<T, Pause>
                || std::is_same_v<T, Report>
                // An assertion's computed message (IEEE 1800-2023 16.3).
                || std::is_same_v<T, StringReport>
                || std::is_same_v<T, RandomValue>
                || std::is_same_v<T, ConcatenateStrings>
                || std::is_same_v<T, StringMethod>
                || std::is_same_v<T, LoadStringConstant>
                || std::is_same_v<T, CopyStringRegister>
                || std::is_same_v<T, CompareStrings>
                || std::is_same_v<T, StringLength>
                || std::is_same_v<T, StringIndex>
                || std::is_same_v<T, StringReplaceByte>
                || std::is_same_v<T, ReadStringObject>
                // $timeformat (20.4.2).
                || std::is_same_v<T, TimeFormatControl>
                // Dump controls (21.7); the planner keeps every signal a
                // host signal (observe_all).
                || std::is_same_v<T, VcdControl>
                // $monitor, $strobe, $monitoron/off (21.2.2, 21.2.3): the
                // planner keeps the monitored signals host signals.
                || std::is_same_v<T, MonitorInstall>
                || std::is_same_v<T, MonitorControl>
                // $dist_* (20.14.2).
                || std::is_same_v<T, RandomDistribution>
                // $countones and $countbits (20.9).
                || std::is_same_v<T, CountOnes>
                || std::is_same_v<T, CountBits>
                // %v of a host signal (21.2.1.5, static_kernel_host_signal).
                || std::is_same_v<T, SignalStrengthText>
                // An event's triggered state (15.5.3), from the host's
                // event history (static_kernel_host_signal).
                || std::is_same_v<T, EventTriggered>
                // Delayed, gate-delay and inertial updates of host signals
                // (10.3.3, 28.16, static_kernel_host_signal).
                || std::is_same_v<T, WriteAfter>
                || std::is_same_v<T, WriteAfterSlice>
                || std::is_same_v<T, WriteAfterDynamicSlice>
                || std::is_same_v<T, WriteAfterDynamicPartSlice>
                || std::is_same_v<T, WriteDelayed>
                || std::is_same_v<T, WriteInertial>
                || std::is_same_v<T, WriteInertialSlice>
                // Between equal-shape private arrays (see the planner).
                || std::is_same_v<T, CopyContainerRegister>
                || std::is_same_v<T, PlusArgSelect>
                || std::is_same_v<T, Call>
                || std::is_same_v<T, Return>
                || std::is_same_v<T, CallableFramePush>
                || std::is_same_v<T, CallableFramePop>
                || std::is_same_v<T, IntegerUnary>
                || std::is_same_v<T, IntegerBinary>
                || std::is_same_v<T, IntegerCheck>;
        }
    }, operation);
}

/// Operations a VHDL delta-mode member may use: the pure value operations,
/// control flow with fixed or dynamic call stacks and automatic frames,
/// VHDL integer arithmetic and checks, assertions and report statements,
/// current-value reads, zero-delay signal assignments and shared-variable
/// writes.
[[nodiscard]] inline bool static_kernel_vhdl_operation_supported(
    const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ReadSignal>) {
            return value.kind == SignalReadKind::current && value.ticks == 1U
                && !value.clock && !value.gate;
        } else if constexpr (std::is_same_v<T, WriteUpdate>
            || std::is_same_v<T, WriteUpdateSlice>
            || std::is_same_v<T, WriteUpdateDynamicSlice>
            || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
            return value.domain == SignalUpdateDomain::generic;
        } else if constexpr (std::is_same_v<T, WriteBlocking>
            || std::is_same_v<T, WriteBlockingSlice>
            || std::is_same_v<T, WriteBlockingDynamicSlice>
            || std::is_same_v<T, WriteBlockingDynamicPartSlice>) {
            // Shared variables update immediately.
            return true;
        } else if constexpr (std::is_same_v<T, WriteProjected>
            || std::is_same_v<T, WriteProjectedSlice>
            || std::is_same_v<T, WriteProjectedDynamicSlice>) {
            // A single zero-delay inertial element on a driver that never has
            // pending transactions is staged in the current delta.
            return value.delay == 0U && value.rejection == 0U
                && value.mode == ProjectedDelayMode::inertial;
        } else if constexpr (std::is_same_v<T, CallableFramePush>) {
            return value.strings.empty() && value.containers.empty();
        } else if constexpr (std::is_same_v<T, CallableFramePop>) {
            return value.preserve_strings.empty()
                && value.preserve_containers.empty();
        } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
            // REAL math (IEEE 1076-2019 16.3 MATH_REAL as lowered) and
            // conversions; the time queries are SystemVerilog only.
            return value.function < SystemVerilogMathFunction::Time;
        } else {
            return std::is_same_v<T, DebugPoint>
                // REAL arithmetic, comparisons and conversions (5.2.5, 9.2).
                || std::is_same_v<T, SystemVerilogScalarBinary>
                // A code coverage counter bump (host data, no registers).
                || std::is_same_v<T, CodeCoverageHit>
                || std::is_same_v<T, LoadConstant>
                || std::is_same_v<T, CopyRegister>
                || std::is_same_v<T, Binary>
                || std::is_same_v<T, Reduction>
                || std::is_same_v<T, UnaryNot>
                || std::is_same_v<T, LogicalNot>
                || std::is_same_v<T, LogicalBinary>
                || std::is_same_v<T, Shift>
                || std::is_same_v<T, Extract>
                || std::is_same_v<T, Insert>
                || std::is_same_v<T, Concatenate>
                || std::is_same_v<T, ConditionalSelect>
                || std::is_same_v<T, ConvertToTwoState>
                || std::is_same_v<T, DynamicExtract>
                || std::is_same_v<T, DynamicPartSelect>
                || std::is_same_v<T, DynamicInsert>
                || std::is_same_v<T, DynamicPartInsert>
                || std::is_same_v<T, Jump>
                || std::is_same_v<T, Branch>
                || std::is_same_v<T, Call>
                || std::is_same_v<T, Return>
                || std::is_same_v<T, IntegerUnary>
                || std::is_same_v<T, IntegerBinary>
                || std::is_same_v<T, IntegerCheck>
                || std::is_same_v<T, Assert>
                || std::is_same_v<T, Report>;
        }
    }, operation);
}

/// An operation a port-association conversion program (a resolver with
/// sources and no drivers, IEEE 1076-2008 6.5.7.1) may use in the kernel: a
/// pure delta-mode value operation, a read of one of its sources, or a halt,
/// which ends the pass with the output register as the result
/// (Interpreter::Impl::run_vhdl_resolver). Signal writes stay on the host.
[[nodiscard]] inline bool static_kernel_conversion_operation(
    const Operation& operation, std::span<const SignalId> sources)
{
    if (const auto* halt = operation_get_if<Halt>(&operation)) {
        return !halt->program_exit;
    }
    if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
        if (std::ranges::find(sources, read->signal) == sources.end()) {
            return false;
        }
    }
    return static_kernel_vhdl_operation_supported(operation)
        && !operation_holds<WriteUpdate>(operation)
        && !operation_holds<WriteUpdateSlice>(operation)
        && !operation_holds<WriteUpdateDynamicSlice>(operation)
        && !operation_holds<WriteUpdateDynamicPartSlice>(operation)
        && !operation_holds<WriteBlocking>(operation)
        && !operation_holds<WriteBlockingSlice>(operation)
        && !operation_holds<WriteBlockingDynamicSlice>(operation)
        && !operation_holds<WriteBlockingDynamicPartSlice>(operation)
        && !operation_holds<WriteProjected>(operation)
        && !operation_holds<WriteProjectedSlice>(operation)
        && !operation_holds<WriteProjectedDynamicSlice>(operation);
}

/// A halt that follows a failure-severity report, as the VHDL lowering emits
/// for its runtime checks (value length, slice bounds, loop iteration limit):
/// the report ends the simulation (the kernel raises the assertion failure,
/// IEEE 1076-2019 10.9), so the halt never runs. Callers also check that no
/// branch targets it.
[[nodiscard]] inline bool static_kernel_halt_after_failure(
    const Operation& previous, const Operation& operation)
{
    const auto* halt = operation_get_if<Halt>(&operation);
    const auto* report = operation_get_if<Report>(&previous);
    return halt != nullptr && !halt->program_exit && report != nullptr
        && report->severity == AssertionSeverity::failure;
}

/// Operations a VHDL behavioral member may use (IEEE 1076-2019 11.3, 10.2):
/// the VHDL delta-mode set plus the wait statement (`wait for`, `wait on`,
/// `wait until` lowered as a WaitOn loop, with or without a timeout, bare
/// `wait`), reports and `std.env` stop/finish. Signal assignments stay the
/// zero-delay inertial ones the delta-mode set already admits on kernel
/// signals; delayed, transport and waveform assignments and the signal
/// attributes 'EVENT, 'ACTIVE, 'LAST_VALUE, 'LAST_EVENT and 'LAST_ACTIVE
/// (16.2.4) and forces keep their signal a host signal
/// (static_kernel_host_signal) and use the host's driver, history and
/// force. Container registers hold private arrays and protected-type state
/// (6.5.2, 5.6.2) as the SystemVerilog threads' do. String registers carry
/// `'image`, `'value`,
/// concatenation, comparison and computed report messages (5.3.2.4, 16.2,
/// 10.9), and automatic frames may save them. TEXTIO and typed files run
/// the reference's file operations (16.4, static_kernel_file_operation).
[[nodiscard]] inline bool static_kernel_vhdl_behavioral_operation_supported(
    const Operation& operation)
{
    if (static_kernel_vhdl_operation_supported(operation)
        || static_kernel_file_operation(operation)) {
        return true;
    }
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, WaitOn>) {
            if (value.signals.empty() && !value.timeout) {
                return false;
            }
            if (!value.edges.empty()
                && value.edges.size() != value.signals.size()) {
                return false;
            }
            for (std::size_t entry = 0U; entry < value.edges.size(); ++entry) {
                if (value.edges[entry] != EdgeKind::any) {
                    return false;
                }
            }
            // Timeout metadata requires a timeout; a rearm requires the
            // result register (the lowering always provides both).
            if (!value.timeout
                && (value.timeout_result || value.timeout_origin
                    || value.timeout_source)) {
                return false;
            }
            return !value.timeout_origin || value.timeout_result.has_value();
        } else if constexpr (std::is_same_v<T, WriteUpdate>) {
            // S'TRANSACTION's driver toggles its host signal from its
            // SystemVerilog domain, as the reference's does (IEEE 1076-2019
            // 16.2.4; the planner keeps an implicit driver's signal a host
            // signal).
            return value.domain == SignalUpdateDomain::systemverilog_active;
        } else if constexpr (std::is_same_v<T, Halt>) {
            return !value.program_exit;
        } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
            // `now` (IEEE 1076-2019 16.3): the time query.
            return value.function >= SystemVerilogMathFunction::Time;
        } else if constexpr (std::is_same_v<T, ClassStaticMethodCall>) {
            // A guarded assignment's driver disconnection (IEEE 1076-2019
            // 11.6, 10.5.2.2) through the host's projected drivers.
            return vhdl_disconnect_signal(value.method_identity).has_value();
        } else if constexpr (std::is_same_v<T, SignalDriving>
            || std::is_same_v<T, SignalDrivingValue>) {
            // 'DRIVING and 'DRIVING_VALUE (16.2.4) of the member's host
            // driver (static_kernel_host_signal).
            (void)value;
            return true;
        } else if constexpr (static_kernel_vhdl_api_container_operation<T>) {
            // std.env's directory, call path and reflection (16.5.6, 16.6),
            // the reference's run_vhdl_* on the member's registers.
            (void)value;
            return true;
        } else if constexpr (std::is_same_v<T, VhdlAssertApi>
            || std::is_same_v<T, VhdlEnvironmentTime>
            || std::is_same_v<T, VhdlEnvironmentTimeToString>
            || std::is_same_v<T, VhdlEnvironmentGetenv>) {
            // std.env's assertion controls, date/time functions and GETENV
            // (16.5.5, 16.5.6), the reference's run_vhdl_* on the member's
            // registers.
            (void)value;
            return true;
        } else if constexpr (std::is_same_v<T, ContainerWrite>) {
            // Process-private arrays and copies of protected-type state
            // (see the planner's container rule).
            return !value.string_index && !value.associative_default;
        } else if constexpr (static_kernel_reference_container_operation<T>) {
            // Access values and arrays with run-time ranges in
            // reference-format registers (static_kernel_dynamic_container_
            // registers).
            return true;
        } else if constexpr (std::is_same_v<T, WriteContainerObject>) {
            return !value.nonblocking && !value.transaction_signal;
        } else if constexpr (std::is_same_v<T, WriteProjected>
            || std::is_same_v<T, WriteProjectedSlice>
            || std::is_same_v<T, WriteProjectedDynamicSlice>
            || std::is_same_v<T, WriteProjectedWaveform>
            || std::is_same_v<T, WriteProjectedWaveformSlice>
            || std::is_same_v<T, WriteProjectedWaveformDynamicSlice>) {
            // Delayed, transport and waveform assignments go to the
            // member's host driver of a host signal
            // (static_kernel_host_signal).
            (void)value;
            return true;
        } else if constexpr (std::is_same_v<T, ForceSignalSlice>
            || std::is_same_v<T, ReleaseSignalSlice>) {
            // Forces scheduled for the host's update phase (10.5.2.1).
            return !value.port_segment;
        } else {
            return std::is_same_v<T, WaitFor>
                // std.env.stop (16.5.6 STOP): the run pauses at the thread.
                || std::is_same_v<T, Pause>
                || std::is_same_v<T, SignalEvent>
                || std::is_same_v<T, SignalActive>
                || std::is_same_v<T, SignalLastValue>
                || std::is_same_v<T, SignalLastEvent>
                || std::is_same_v<T, SignalLastActive>
                || std::is_same_v<T, WaitSensitivity>
                || std::is_same_v<T, WaitForever>
                // Automatic frames save their container registers too.
                || std::is_same_v<T, CallableFramePush>
                || std::is_same_v<T, CallableFramePop>
                || std::is_same_v<T, ContainerRead>
                || std::is_same_v<T, ReadContainerObject>
                || std::is_same_v<T, CopyContainerRegister>
                || std::is_same_v<T, Report>
                || std::is_same_v<T, Stop>
                || std::is_same_v<T, LoadStringConstant>
                || std::is_same_v<T, CopyStringRegister>
                || std::is_same_v<T, ConcatenateStrings>
                || std::is_same_v<T, StringMethod>
                || std::is_same_v<T, StringReport>
                || std::is_same_v<T, CompareStrings>
                || std::is_same_v<T, StringLength>
                || std::is_same_v<T, StringIndex>
                || std::is_same_v<T, StringReplaceByte>
                || std::is_same_v<T, CountOnes>
                || std::is_same_v<T, CountBits>;
        }
    }, operation);
}

/// An operation a final procedure's member may use (IEEE 1800-2023 9.2.3):
/// a final procedure runs once, without delays, at the end of simulation,
/// where the kernel runs it outside its rounds (run_finals). It reads
/// signals, makes blocking writes to host signals (see the planner) and
/// may $stop, but forces, waits on and schedules no signal, and fills no
/// design memory.
[[nodiscard]] inline bool static_kernel_final_operation(
    const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ReadSignal>
            || std::is_same_v<T, SignalEvent>
            || std::is_same_v<T, SignalActive>
            || std::is_same_v<T, SignalLastValue>
            || std::is_same_v<T, SignalLastEvent>
            || std::is_same_v<T, SignalLastActive>
            || std::is_same_v<T, WriteBlocking>
            || std::is_same_v<T, WriteBlockingSlice>
            || std::is_same_v<T, WriteBlockingDynamicSlice>
            || std::is_same_v<T, WriteBlockingDynamicPartSlice>) {
            return true;
        } else if constexpr (requires { value.signal; }) {
            return false;
        } else {
            return !(std::is_same_v<T, WaitFor> || std::is_same_v<T, WaitOn>
                || std::is_same_v<T, WaitSensitivity>
                || std::is_same_v<T, WaitForever> || std::is_same_v<T, Fork>
                || std::is_same_v<T, ForkEnd>
                || std::is_same_v<T, WriteContainerObject>
                || std::is_same_v<T, WriteContainerObjectElement>
                || std::is_same_v<T, WriteStringObject>
                || std::is_same_v<T, FileReadLine> || std::is_same_v<T, FileScan>
                || std::is_same_v<T, FileErrorStatus>
                || std::is_same_v<T, LoadMemory>);
        }
    }, operation);
}

/// An operation a package static initializer's member may use (IEEE
/// 1800-2023 6.21, 26.3): the initializer runs once, before time zero, in
/// the host's startup order, and finishes without suspending, so it neither
/// waits nor forks. Its writes reach host signals (see the planner).
[[nodiscard]] inline bool static_kernel_startup_operation(
    const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        return !(std::is_same_v<T, WaitFor> || std::is_same_v<T, WaitOn>
            || std::is_same_v<T, WaitSensitivity>
            || std::is_same_v<T, WaitForever> || std::is_same_v<T, Fork>
            || std::is_same_v<T, ForkEnd> || std::is_same_v<T, Pause>
            || std::is_same_v<T, MonitorInstall>
            || std::is_same_v<T, MonitorControl>);
    }, operation);
}

/// The signal a behavioral member's operation keeps on the host: the target
/// of a VHDL signal assignment other than a single zero-delay inertial
/// element without a rejection limit, which has transactions in the
/// projected output waveform of its driver (IEEE 1076-2019 10.5.2.2); the
/// prefix of 'EVENT, 'ACTIVE, 'LAST_VALUE, 'LAST_EVENT or 'LAST_ACTIVE
/// (16.2.4), whose event and transaction history the host keeps; and the
/// target of a force or release (IEEE 1800-2023 10.6.2, 1076-2019
/// 10.5.2.1), whose forced value hides its drivers' in the host's
/// resolution.
[[nodiscard]] inline std::optional<SignalId> static_kernel_host_signal(
    const Operation& operation)
{
    return visit_operation([](const auto& value) -> std::optional<SignalId> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ForceSignalSlice>
            || std::is_same_v<T, ReleaseSignalSlice>) {
            return value.signal;
        } else if constexpr (std::is_same_v<T, SignalEvent>
            || std::is_same_v<T, SignalActive>
            || std::is_same_v<T, SignalLastValue>
            || std::is_same_v<T, SignalLastEvent>
            || std::is_same_v<T, SignalLastActive>
            // %v reads the host's drivers and their strengths (IEEE
            // 1800-2023 21.2.1.5).
            || std::is_same_v<T, SignalStrengthText>
            // 'DRIVING and 'DRIVING_VALUE read the member's host driver
            // (1076-2019 16.2.4).
            || std::is_same_v<T, SignalDriving>
            || std::is_same_v<T, SignalDrivingValue>) {
            return value.signal;
        } else if constexpr (std::is_same_v<T, EventTriggered>) {
            // The triggered state of an event (IEEE 1800-2023 15.5.3) is
            // the host's history of its synchronization object.
            return value.event;
        } else if constexpr (std::is_same_v<T, ClassStaticMethodCall>) {
            // A driver disconnection (IEEE 1076-2019 10.5.2.2) edits the
            // projected output waveform of the member's host driver.
            return vhdl_disconnect_signal(value.method_identity);
        } else if constexpr (std::is_same_v<T, WriteProjected>
            || std::is_same_v<T, WriteProjectedSlice>
            || std::is_same_v<T, WriteProjectedDynamicSlice>) {
            if (value.delay != 0U || value.rejection != 0U
                || value.mode != ProjectedDelayMode::inertial) {
                return value.signal;
            }
            return std::nullopt;
        } else if constexpr (std::is_same_v<T, WriteProjectedWaveform>
            || std::is_same_v<T, WriteProjectedWaveformSlice>
            || std::is_same_v<T, WriteProjectedWaveformDynamicSlice>
            // SystemVerilog delayed, gate-delay and inertial updates (IEEE
            // 1800-2023 10.3.3, 28.16): the host holds the driver's pending
            // updates and matures them.
            || std::is_same_v<T, WriteAfter>
            || std::is_same_v<T, WriteAfterSlice>
            || std::is_same_v<T, WriteAfterDynamicSlice>
            || std::is_same_v<T, WriteAfterDynamicPartSlice>
            || std::is_same_v<T, WriteDelayed>
            || std::is_same_v<T, WriteInertial>
            || std::is_same_v<T, WriteInertialSlice>) {
            return value.signal;
        } else {
            return std::nullopt;
        }
    }, operation);
}

/// A bit range of one signal that a port-association program copies into its
/// formal unchanged.
struct StaticKernelWiring {
    SignalId source { };
    std::uint32_t offset { };
    std::uint32_t width { };
};

/// Recognizes a pure-routing port-association program (IEEE 1076-2008
/// 6.5.7.1, 14.7.3.3): one that only reads current signal values, selects
/// bit ranges of them and leaves the result in `output`, with no conversion
/// function or type conversion. A formal associated with a static element or
/// slice of an actual signal is such a wire: it has the part's effective
/// value in the cycle the part updates. `signal_width` gives a signal's
/// width in bits.
template <typename Operations, typename SignalWidth>
[[nodiscard]] std::optional<StaticKernelWiring> static_kernel_wiring(
    const Operations& operations, const RegisterId output,
    SignalWidth&& signal_width)
{
    struct Piece {
        SignalId signal { };
        std::uint64_t offset { };
        std::uint64_t width { };
    };
    std::map<RegisterId, Piece> pieces;
    for (std::size_t index = 0U; index < operations.size(); ++index) {
        const auto operation = operations.expanded(index);
        if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
            if (read->kind != SignalReadKind::current || read->ticks != 1U
                || read->clock || read->gate) {
                return std::nullopt;
            }
            pieces.insert_or_assign(read->destination,
                Piece { read->signal, 0U,
                    static_cast<std::uint64_t>(signal_width(read->signal)) });
        } else if (const auto* extract = operation_get_if<Extract>(&operation)) {
            const auto piece = pieces.find(extract->source);
            if (piece == pieces.end()
                || static_cast<std::uint64_t>(extract->offset) + extract->width
                    > piece->second.width) {
                return std::nullopt;
            }
            const Piece part { piece->second.signal,
                piece->second.offset + extract->offset, extract->width };
            pieces.insert_or_assign(extract->destination, part);
        } else if (const auto* copy = operation_get_if<CopyRegister>(&operation)) {
            const auto piece = pieces.find(copy->source);
            if (piece == pieces.end()) {
                return std::nullopt;
            }
            const auto value = piece->second;
            pieces.insert_or_assign(copy->destination, value);
        } else if (!operation_holds<DebugPoint>(operation)
            && !operation_holds<Halt>(operation)) {
            return std::nullopt;
        }
    }
    const auto result = pieces.find(output);
    if (result == pieces.end() || result->second.width == 0U
        || result->second.offset + result->second.width
            > signal_width(result->second.signal)) {
        return std::nullopt;
    }
    return StaticKernelWiring { result->second.signal,
        static_cast<std::uint32_t>(result->second.offset),
        static_cast<std::uint32_t>(result->second.width) };
}

/// The SystemVerilog processes that start one delta after the other
/// processes of time zero. A process with no static sensitivity and no event
/// wait that reads a settling signal (a net, or a signal that an
/// event-driven process or port adapter drives) starts after time zero's
/// continuous assignments and event controls have settled; the order within
/// the Active region is unspecified (IEEE 1800-2023 4.7, 9.2), so this is
/// one fixed legal order, shared by the scheduler's start and the static
/// kernel's threads. `ignored` names processes that do not take part at all
/// (compact constants), `excluded` those that cannot start late themselves
/// (kernel members) but still drive settling signals, `resolved` the signals
/// that are nets, and `event_driven` the processes whose view lost a static
/// sensitivity they had (kernel members installed without it).
template <typename ViewOf, typename Ignored, typename Excluded, typename Resolved,
    typename EventDriven>
[[nodiscard]] std::vector<bool> systemverilog_late_starters(
    const std::size_t process_count, const std::size_t signal_count,
    const ViewOf& view_of, const Ignored& ignored, const Excluded& excluded,
    const Resolved& resolved, const EventDriven& event_driven)
{
    std::vector<bool> candidates(process_count, false);
    bool any_candidate { };
    for (std::size_t id = 0U; id < process_count; ++id) {
        if (ignored(id) || excluded(id)) {
            continue;
        }
        const auto& program = view_of(id);
        candidates[id] = program.startup_phase() == ProcessStartupPhase::ordinary
            && program.scheduling_domain()
                == ProcessSchedulingDomain::systemverilog
            && program.static_sensitivity().empty()
            && program.name().find(".$declaration_initializer_")
                == std::string::npos;
        any_candidate = any_candidate || candidates[id];
    }
    std::vector<bool> late(process_count, false);
    if (!any_candidate) {
        return late;
    }
    // Settling signals: nets, and the signals that event-driven processes
    // (continuous assignments, always_comb) and port adapters drive.
    std::vector<bool> settling(signal_count, false);
    for (std::size_t signal = 0U; signal < signal_count; ++signal) {
        settling[signal] = resolved(signal);
    }
    for (std::size_t id = 0U; id < process_count; ++id) {
        if (ignored(id)) {
            continue;
        }
        const auto& program = view_of(id);
        if (program.startup_phase()
            == ProcessStartupPhase::package_static_initializer) {
            continue;
        }
        if (!program.static_sensitivity().empty() || event_driven(id)
            || program.name().find(".port_actual_") != std::string::npos) {
            for (const auto& region : program.driver_regions()) {
                if (region.signal < settling.size()) {
                    settling[region.signal] = true;
                }
            }
        }
    }
    for (std::size_t id = 0U; id < process_count; ++id) {
        if (!candidates[id]) {
            continue;
        }
        const auto& program = view_of(id);
        const auto& operations = std::as_const(program.operations());
        bool reads_settling { };
        bool waits { };
        for (std::size_t index = 0U; index < operations.size() && !waits;
             ++index) {
            const auto& operation = operations[index];
            if (operation_holds<WaitOn>(operation)
                || operation_holds<WaitOrder>(operation)
                || operation_holds<WaitSensitivity>(operation)) {
                waits = true;
            } else if (const auto* read = operation_get_if<ReadSignal>(&operation);
                       read != nullptr && read->signal < settling.size()
                && settling[read->signal]) {
                reads_settling = true;
            }
        }
        late[id] = reads_settling && !waits;
    }
    return late;
}

} // namespace fsim::runtime::simir

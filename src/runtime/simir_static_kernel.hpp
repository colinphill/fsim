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

#include "fsim/runtime/simir.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
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
    /// Dead logic (plan dead-logic removal): a combinational member whose
    /// outputs no live process reads. It is never scheduled; a read of what
    /// it writes (materialize) evaluates it first from its current inputs.
    bool lazy { };
    /// VHDL delta collapsing (exact subset): a pure combinational member
    /// whose every reader is another such member or wakes only before a
    /// time step's first delta. The kernel may evaluate it in the update
    /// phase that changed its inputs (StaticKernel::plan_delta_collapse).
    bool collapse { };
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
        } else {
            return std::is_same_v<T, DebugPoint>
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
/// $warning or $error action, IEEE 1800-2023 16.3, 20.10). The member runs
/// once per triggering edge, as the process does on the host, so the
/// message is emitted as often; the kernel's operation step emits it through
/// the report hook.
[[nodiscard]] inline bool static_kernel_sequential_operation_supported(
    const Operation& operation)
{
    const auto* report = operation_get_if<Report>(&operation);
    return report != nullptr && report->severity != AssertionSeverity::failure;
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

/// Operations a behavioral member may use: the static set plus waits,
/// fork/join, calls with automatic frames of packed registers, integer
/// arithmetic, immediate runtime-library output, string constants and copies,
/// plusarg queries, $finish and element writes of process-private arrays. Postponed output and wait timeouts stay on
/// the host.
[[nodiscard]] inline bool static_kernel_behavioral_operation_supported(
    const Operation& operation)
{
    if (static_kernel_operation_supported(operation)) {
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
            return !value.program_exit;
        } else if constexpr (std::is_same_v<T, Display>
            || std::is_same_v<T, FormatDisplay>
            || std::is_same_v<T, StringDisplay>
            || std::is_same_v<T, TimeDisplay>) {
            return !value.postponed;
        } else if constexpr (std::is_same_v<T, Fork>) {
            return !value.isolate_register_frame
                && !value.branches.empty();
        } else if constexpr (std::is_same_v<T, ContainerWrite>) {
            // Process-private fixed arrays (see the planner).
            return !value.string_index;
        } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
            // $time, $stime and $realtime.
            return value.function >= SystemVerilogMathFunction::Time;
        } else {
            return std::is_same_v<T, WaitFor>
                || std::is_same_v<T, WaitSensitivity>
                || std::is_same_v<T, WaitForever>
                || std::is_same_v<T, ForkEnd>
                || std::is_same_v<T, Stop>
                || std::is_same_v<T, Pause>
                || std::is_same_v<T, Report>
                || std::is_same_v<T, RandomValue>
                || std::is_same_v<T, ConcatenateStrings>
                || std::is_same_v<T, StringMethod>
                || std::is_same_v<T, LoadStringConstant>
                || std::is_same_v<T, CopyStringRegister>
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
        } else {
            return std::is_same_v<T, DebugPoint>
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
/// zero-delay inertial ones the delta-mode set already admits; delayed,
/// transport and waveform assignments, attribute queries and string or
/// container registers stay on the host.
[[nodiscard]] inline bool static_kernel_vhdl_behavioral_operation_supported(
    const Operation& operation)
{
    if (static_kernel_vhdl_operation_supported(operation)) {
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
        } else if constexpr (std::is_same_v<T, Halt>) {
            return !value.program_exit;
        } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
            // `now` (IEEE 1076-2019 16.3): the time query.
            return value.function >= SystemVerilogMathFunction::Time;
        } else {
            return std::is_same_v<T, WaitFor>
                || std::is_same_v<T, WaitSensitivity>
                || std::is_same_v<T, WaitForever>
                || std::is_same_v<T, Report>
                || std::is_same_v<T, Stop>;
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

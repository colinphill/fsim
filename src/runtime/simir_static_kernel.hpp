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
#include <memory>
#include <type_traits>
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

struct StaticKernelRuntimeSpec {
    ProcessId host { };
    /// The host's original program; the scheduler runs the host stub.
    Process host_original;
    std::vector<StaticKernelMemberSpec> members;
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
    /// Writer ranges of owned signals in VHDL mode, merged per process; they
    /// replace the members' declared driver regions for publication.
    std::vector<StaticKernelWriterRegion> writer_regions;
    /// Host signals members read that no process writes (constant port
    /// actuals, undriven nets). They stay inputs, but host processes cannot
    /// change them between kernel rounds.
    std::vector<SignalId> unwritten_inputs;
};

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
            return !value.branches.empty();
        } else if constexpr (std::is_same_v<T, ContainerWrite>) {
            // Process-private fixed arrays (see the planner).
            return !value.string_index;
        } else {
            return std::is_same_v<T, WaitFor>
                || std::is_same_v<T, WaitSensitivity>
                || std::is_same_v<T, WaitForever>
                || std::is_same_v<T, ForkEnd>
                || std::is_same_v<T, Stop>
                || std::is_same_v<T, LoadStringConstant>
                || std::is_same_v<T, CopyStringRegister>
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
/// VHDL integer arithmetic and checks, assertions, current-value reads,
/// zero-delay signal assignments and shared-variable writes.
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
                || std::is_same_v<T, Assert>;
        }
    }, operation);
}

} // namespace fsim::runtime::simir

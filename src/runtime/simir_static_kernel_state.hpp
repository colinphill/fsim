// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 static kernel state, shared by the scheduler/generic evaluator
// (simir_static_kernel.cpp) and the compiled narrow tier
// (simir_static_kernel_compiled.cpp). Kernel-owned signal values live in one
// word arena: a slot of `words` 64-bit words stores its aval plane at
// `offset` and its bval plane at `offset + words`. Logic9 slots (VHDL delta
// mode) add the two upper Logic9 code planes after them.
#pragma once

#include "simir_internal.hpp"
#include "simir_kernel_word_ops.hpp"
#include "simir_operation_kinds.hpp"
#include "simir_static_kernel_ir.hpp"
#include "simir_static_kernel_native.hpp"
#include "simir_static_kernel.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <tuple>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

namespace static_kernel_detail {

constexpr auto no_slot = std::numeric_limits<std::uint32_t>::max();
constexpr auto no_container = std::numeric_limits<std::uint32_t>::max();
/// Container::object of a behavioral member's private array.
constexpr auto no_container_object
    = std::numeric_limits<ContainerObjectId>::max();
constexpr std::size_t run_step_limit = 100'000'000U;
constexpr std::size_t edge_iteration_limit = 100'000U;
constexpr std::uint32_t partition_tag = std::uint32_t { 1 } << 31U;

/// Why the kernel's reference evaluator runs an operation (interpretation
/// census): a member without compiled code, a VHDL prologue, generic mode
/// after a deoptimization, the deoptimization's own resume, kernel bytecode,
/// a behavioral thread on the reference path, verify mode, specialization's
/// constant folding (kernel construction), or a slow-path fallback inside a
/// compiled instruction's helper (reference).
enum class InterpretedCause : std::uint8_t {
    reference,
    uncompiled,
    prologue,
    generic_mode,
    deopt,
    bytecode,
    thread,
    verify,
    specialize,
};
inline constexpr std::array<const char*, 9> interpreted_cause_names {
    "reference", "uncompiled", "prologue", "generic_mode", "deopt", "bytecode",
    "thread", "verify", "specialize"
};

struct Reader {
    std::uint32_t member { };
    std::uint32_t offset { };
    /// Zero means the complete signal.
    std::uint32_t width { };
};

struct EdgeReader {
    std::uint32_t member { };
    EdgeKind edge { EdgeKind::posedge };
};

struct WriterRegion {
    ProcessId process { };
    std::uint32_t offset { };
    std::uint32_t width { };
};

/// A narrow value as a Logic4 word: Logic9 values qualify when every element
/// is 0, 1, X or Z (where the Logic9 and Logic4 operations agree).
[[nodiscard]] inline std::optional<kernel_word::Word> exact_word(const PackedLogic4& value)
{
    const auto width = static_cast<std::uint32_t>(value.width());
    if (width == 0U || width > 64U) {
        return std::nullopt;
    }
    const auto m = kernel_word::mask(width);
    if (!value.is_logic9()) {
        const auto a = value.aval_words();
        const auto b = value.bval_words();
        return kernel_word::Word { a[0] & m, b[0] & m };
    }
    const auto plane = [&](const std::size_t index) {
        const auto words = value.logic9_plane_words(index);
        return words.empty() ? std::uint64_t { 0 } : words[0];
    };
    return kernel_word::logic9_word(plane(0U), plane(1U), plane(2U), plane(3U), width);
}

/// A narrow value as a Logic4 word plus 'U' mask.
[[nodiscard]] inline std::optional<kernel_word::UWord> exact_uword(
    const PackedLogic4& value)
{
    const auto width = static_cast<std::uint32_t>(value.width());
    if (width == 0U || width > 64U) {
        return std::nullopt;
    }
    if (!value.is_logic9()) {
        const auto m = kernel_word::mask(width);
        return kernel_word::UWord {
            { value.aval_words()[0] & m, value.bval_words()[0] & m }, 0U };
    }
    const auto plane = [&](const std::size_t index) {
        const auto words = value.logic9_plane_words(index);
        return words.empty() ? std::uint64_t { 0 } : words[0];
    };
    return kernel_word::logic9_uword(plane(0U), plane(1U), plane(2U), plane(3U),
        width);
}

/// The debug points a debugger plan executes: all but the block markers
/// (the reference evaluator raises no execution point for those either).
[[nodiscard]] constexpr bool debug_point_executable(
    const DebugPointKind kind) noexcept
{
    return kind != DebugPointKind::block_entry
        && kind != DebugPointKind::block_exit;
}

/// A word plus 'U' mask as a value of the given kind.
[[nodiscard]] inline PackedLogic4 uword_value(const kernel_word::Word value,
    const std::uint64_t unknown, const std::uint32_t width,
    const ValueKind kind)
{
    if (unknown == 0U) {
        auto result = PackedLogic4::from_aval_bval(width, value.a, value.b);
        return kind == ValueKind::logic9 ? result.promoted_to_logic9() : result;
    }
    const auto planes = kernel_word::logic9_planes(value, width, unknown);
    const std::uint64_t words[4] = { planes.p0, planes.p1, planes.p2, planes.p3 };
    return PackedLogic4::from_logic9_word_planes(width,
        std::span { &words[0], 1U }, std::span { &words[1], 1U },
        std::span { &words[2], 1U }, std::span { &words[3], 1U });
}

} // namespace static_kernel_detail

class Interpreter::Impl::StaticKernel {
public:
    using Word = kernel_word::Word;
    using Reader = static_kernel_detail::Reader;
    using EdgeReader = static_kernel_detail::EdgeReader;
    using WriterRegion = static_kernel_detail::WriterRegion;
    using CompiledBody = static_kernel_detail::CompiledBody;

    StaticKernel(Impl& impl, StaticKernelRuntimeSpec spec);
    ~StaticKernel();
    StaticKernel(const StaticKernel&) = delete;
    StaticKernel& operator=(const StaticKernel&) = delete;

    /// Returns true when the kernel staged updates to its own inputs and
    /// must run again in the next delta.
    [[nodiscard]] bool activate();
    /// FSIM_STATIC_KERNEL_TIME_WARP=0 disables kernel-owned time.
    void set_time_warp(const bool allowed) noexcept
    {
        const char* text = std::getenv("FSIM_STATIC_KERNEL_TIME_WARP");
        time_warp_ = allowed
            && (text == nullptr || std::string_view { text } != "0");
    }
    void materialize(SignalId signal);
    /// Runs the lazy writers of a slot, their lazy inputs' writers first.
    void refresh_lazy(std::uint32_t slot);
    /// The host committed a new value to the kernel-owned signal (a process
    /// outside the kernel wrote it): take the host value into the slot and
    /// run the ordinary change handling, so readers, waiters, edges and
    /// outputs behave as for a kernel write, and wake the kernel's host
    /// process so the affected members run in the Active region of this
    /// time step. Not called for the kernel's own publication or
    /// materialization (Impl::import_static_kernel_write filters those).
    void import_host_write(SignalId signal, SignalChangeOrigin origin);
    /// Hosted tier: a wake of a hosted process, redirected from the host
    /// scheduler's queue_* entry points (Impl::static_kernel_enqueue_hosted).
    /// Runs the process to its next suspension on its hosted tier; false when
    /// it has none (the evaluator runs it).
    [[nodiscard]] bool run_hosted_tier(ProcessState& process);
    void enqueue_hosted(ProcessId process, StaticKernelHostedWake wake,
        SimulationTick time, StableOrder order);
    /// Hosted tier, region model (S25): a hosted process executing a
    /// WaitRegion resumes in `phase` of this time step. Returns false when
    /// the kernel does not run that region (the host scheduler does).
    [[nodiscard]] bool enqueue_hosted_region(
        ProcessId process, SchedulerPhase phase);
    /// Runs a hosted process to its first suspension during startup, before
    /// the first activation (package static initializers, static-wait
    /// prearming): the kernel invokes the reference evaluator itself.
    void execute_hosted_startup(ProcessId process);
    /// End of simulation (IEEE 1800-2023 9.2.3): drops what the host
    /// scheduler discarded (`discarded`) and schedules the hosted final
    /// blocks. Final blocks the kernel does not host are the caller's.
    void run_finals(bool discarded);
    /// Debugger plan: members stop at their debug points.
    [[nodiscard]] bool debug_points() const noexcept { return debug_points_; }
    /// A mixed kernel with a host boundary: its first round, the VHDL
    /// initialization, runs in the first delta after the host's time-zero
    /// SystemVerilog work it reads (vhdl_round_waits_for_host), so
    /// Interpreter::start queues its first activation instead of running it.
    [[nodiscard]] bool queues_first_activation() const noexcept
    {
        return mixed_ && host_boundary_;
    }
    /// The host process the kernel runs as, a member's or the synthetic one
    /// of a design whose processes are all hosted.
    [[nodiscard]] bool is_host(const ProcessId process) const noexcept
    {
        return process == host_process_;
    }
    /// A SystemVerilog kernel's first member that starts a delta late
    /// (Member::start_late): Interpreter::start queues the late activation
    /// (queue_late_start) at that process's place among the late starters.
    [[nodiscard]] std::optional<ProcessId> first_late_process() const;
    void queue_late_start(ProcessId order);
    void start_late_members();
    /// Debugger: whether `process` is a native (thread) member, and the
    /// values of its registers. The registers are shared by the member's
    /// threads; a compiled member keeps them in its kernel register file.
    /// The readers throw std::logic_error for an unavailable register.
    [[nodiscard]] bool debug_member(ProcessId process) const noexcept;
    /// The original program of the host process when it is a member (its
    /// scheduler program is the kernel stub); null for every other process
    /// and outside a debugger plan.
    [[nodiscard]] const Process* debug_program(ProcessId process) const noexcept
    {
        return host_original_ && process == host_process_ ? &*host_original_
                                                          : nullptr;
    }
    [[nodiscard]] PackedLogic4 debug_register(
        ProcessId process, RegisterId reg, std::size_t width) const;
    [[nodiscard]] std::string debug_string_register(
        ProcessId process, StringRegisterId reg) const;
    [[nodiscard]] ContainerValue debug_container_register(
        ProcessId process, ContainerRegisterId reg) const;
    /// The thread member a debugger process id names: the member itself, or
    /// the member whose fork created the child with that id (children get the
    /// ids the reference evaluator would give them, in a debugger plan).
    [[nodiscard]] std::optional<ProcessId> debug_owner(
        ProcessId process) const noexcept;
    /// Writes a register of a thread member (a debugger local); throws
    /// std::invalid_argument when the width differs from the register's.
    void debug_write_register(
        ProcessId process, RegisterId reg, const PackedLogic4& value);

private:
    struct NativeUnit {
        StaticKernelNativeEntry entry { };
        const CompiledBody* program { };
        std::vector<std::uint32_t> bindings;
        /// A lazily compiled template (lazy_templates_) until it has code.
        std::uint32_t lazy_template { static_kernel_detail::no_slot };
        /// The code is two-state (StaticKernelTemplate::two_state).
        bool two_state { };
        /// Full-semantics code of a two-state template, compiled once one of
        /// its members needs it (full_templates_).
        StaticKernelNativeEntry full_entry { };
        std::uint32_t full_template { static_kernel_detail::no_slot };
    };

    struct Member {
        ProcessId process { };
        StaticKernelMemberKind kind { };
        /// A VHDL process (delta rounds); otherwise SystemVerilog.
        bool vhdl { };
        bool run_at_start { };
        /// A behavioral thread that starts in the delta after time zero's.
        bool start_late { };
        /// A behavioral thread armed at its first static wait before the
        /// once members run (prepare_behavioral).
        bool prearmed { };
        /// always_comb/always_latch (ProcessStartupPhase::after_procedures):
        /// starts after the initial and always procedures.
        bool after_procedures { };
        /// Dead logic: never scheduled; materialize evaluates it from the
        /// slots in lazy_inputs (its whole sensitivity) when a slot it
        /// writes is read. lazy_stamp is the lazy_epoch_ of its last run.
        bool lazy { };
        std::vector<std::uint32_t> lazy_inputs;
        std::uint64_t lazy_stamp { };
        std::uint32_t body_begin { };
        std::uint32_t body_end { };
        std::uint32_t exit_alt { static_kernel_detail::no_slot };
        std::uint32_t level { };
        std::vector<Operation> operations;
        std::vector<PackedLogic4> registers;
        /// Register value kinds (VHDL delta mode; empty means Logic4).
        std::vector<ValueKind> register_kinds;
        std::vector<std::uint32_t> container_registers;
        /// The program's shared operation body while `operations` still
        /// expands it (null once specialization rewrote them); members
        /// with equal bodies share prune_operations results.
        const void* body_identity { };
        /// Runtime-owned call stack and automatic callable frames.
        std::vector<InstructionIndex> call_stack;
        struct Frame {
            std::uint32_t identity { };
            std::vector<RegisterId> ids;
            std::vector<PackedLogic4> values;
            /// Behavioral members also save string values and container
            /// register bindings.
            std::vector<StringRegisterId> string_ids;
            std::vector<std::string> strings;
            std::vector<ContainerRegisterId> container_ids;
            std::vector<std::uint32_t> containers;
            std::size_t bytes { };
        };
        std::vector<Frame> frames;
        std::size_t frame_bytes { };
        /// Contents of private arrays an automatic frame lists, saved at
        /// CallableFramePush and restored at CallableFramePop of the
        /// callables in private_frame_identities (sorted); those frame
        /// operations run this bookkeeping, compiled code included. The
        /// running thread owns the stack.
        struct SavedArray {
            ContainerRegisterId id { };
            std::uint32_t container { };
            std::vector<std::uint64_t> words;
        };
        std::vector<SavedArray> saved_arrays;
        std::vector<std::size_t> saved_array_marks;
        std::vector<std::uint32_t> private_frame_identities;
        [[nodiscard]] bool private_frame(const std::uint32_t identity) const
        {
            return std::ranges::binary_search(private_frame_identities, identity);
        }
        std::optional<CompiledBody> compiled;
        NativeUnit native;
        /// Every register's X plane and 'U' mask is zero, so two-state code
        /// may run; rechecked after any other run.
        bool registers_known { };
        /// Handovers to full code (FastRun::two_state_runs counts the runs);
        /// a member that keeps meeting X or 'U' values stops running
        /// two-state code.
        std::uint32_t two_state_deopts { };
        bool two_state_off { };
        /// While two-state code is off: full runs since it went off, and
        /// the count at which known registers turn it back on (doubling
        /// each time; X before a reset need not last).
        std::uint32_t off_runs { };
        std::uint32_t off_retry { 256U };
        std::uint32_t partition_key { static_kernel_detail::no_slot };
        /// The body assigns one signal more than once in a run, so an
        /// intermediate value is visible to the members it wakes.
        bool rewrites_signal { };
        std::uint32_t partition { static_kernel_detail::no_slot };
        std::uint32_t position { };
        /// VHDL delta mode: the next run starts at operation 0.
        bool fresh { };
        /// VHDL: where the process prologue enters the body (the original
        /// body_begin, before specialization).
        std::uint32_t prologue_end { };
        /// VHDL delta mode: a register holds a value compiled code cannot
        /// represent, so the member runs on the reference evaluator.
        bool generic_mode { };
        /// Automatic frames are no-ops (isolated callables, no recursion);
        /// both tiers skip them so a deoptimization can continue mid-call.
        bool frames_elided { };
        /// Behavioral members: string registers shared by the member's
        /// threads, and the signal/edge list of WaitSensitivity.
        std::vector<std::string> strings;
        std::vector<Sensitivity> wait_sensitivity;
        /// VHDL members with a specialized body (specialize_member): the
        /// original operation each operation stands for; empty otherwise.
        std::vector<InstructionIndex> origin;
        /// Debugger plan: the operation of the last debug point any thread
        /// of the member reached, and the registers that may have been
        /// written before it (debug_register_defined).
        std::uint32_t debug_pc { static_kernel_detail::no_slot };
        /// The debug points (by operation) some thread of the member has
        /// reached.
        std::vector<std::uint8_t> debug_hit;
        mutable std::vector<std::uint64_t> debug_defined;
        mutable std::uint32_t debug_defined_pc { static_kernel_detail::no_slot };
    };

    struct Partition {
        std::vector<std::uint32_t> members;
        std::uint32_t level { };
        CompiledBody program;
        NativeUnit native;
    };

    /// A deduplicated scheduling target for whole-slot readers: a member, or
    /// a partition (tagged) with the earliest reader position in it.
    struct ScheduleTarget {
        std::uint32_t id { };
        std::uint32_t min_position { };
    };

    /// Ranged readers [begin, end) of one slot that schedule the same
    /// target: a partition (id | partition_tag) or a member.
    struct RangedGroup {
        std::uint32_t id { };
        std::uint32_t begin { };
        std::uint32_t end { };
    };

    struct Slot {
        SignalId signal { };
        std::uint32_t width { };
        std::uint32_t words { };
        /// Two planes (aval, bval) or four (Logic9 codes).
        std::uint32_t planes { 2U };
        std::uint32_t offset { };
        /// After build_schedule_targets: only ranged readers, ordered by
        /// `ranged` group.
        std::vector<Reader> readers;
        std::vector<RangedGroup> ranged;
        std::vector<ScheduleTarget> targets;
        std::vector<EdgeReader> edges;
        Logic4 edge_seen { Logic4::x };
        bool trigger_pending { };
        bool output { };
        bool output_pending { };
        /// Changed during or after this activation's NBA commit: published
        /// in the NBA region; otherwise in the Active region.
        bool publish_nba { };
        /// Written by VHDL members: published in the generic domain.
        bool vhdl_written { };
        std::uint32_t published { static_kernel_detail::no_slot };
        std::vector<WriterRegion> writers;
        bool disjoint_writers { true };
        /// No reader, output or edge needs to hear about writes: every reader
        /// runs later in the writers' own partition pass.
        bool silent { };
        /// The lazy members that write the slot.
        std::vector<std::uint32_t> lazy_writers;
        /// A combinational member writes the slot in the NBA region
        /// (IEEE 1800-2023 4.5): readers see the value only after the commit,
        /// so no reader can be ordered after the writer within one pass.
        bool deferred_write { };
        ProcessId last_writer { };
        /// Every writer is one process, so last_writer never changes.
        bool single_writer { };
        /// A mirror of a host signal members read; refreshed by scan_inputs.
        bool mirror { };
    };

    struct Input {
        SignalId signal { };
        PackedLogic4 cached;
        std::uint32_t mirror { static_kernel_detail::no_slot };
        /// Host value revision at the last scan.
        std::uint64_t revision { };
        /// The host's event stamp of the signal at the last scan. A VHDL
        /// signal that changed and returned to the scanned value inside one
        /// update phase (two forces in one cycle) still had an event.
        std::optional<std::pair<SimulationTick, std::uint64_t>> event_seen;
        std::vector<Reader> readers;
        std::vector<EdgeReader> edges;
        /// No process writes the signal (StaticKernelRuntimeSpec::
        /// unwritten_inputs).
        bool unwritten { };
        /// One member process drives the signal (StaticKernelRuntimeSpec::
        /// sole_writer_inputs).
        bool sole_writer { };
    };

    struct Container {
        ContainerObjectId object { };
        ContainerType type;
        std::uint32_t count { };
        StaticKernelContainerStorage storage {
            StaticKernelContainerStorage::elements
        };
        std::uint32_t element_words { };
        std::uint32_t element_offset { };
        std::vector<std::uint32_t> element_slots;
        std::uint32_t packed_slot { static_kernel_detail::no_slot };
    };

    struct FamilyLeaf {
        std::uint32_t slot { };
        std::uint32_t offset { };
        std::uint32_t width { };
    };

    struct Family {
        SignalId proxy { };
        std::uint32_t width { };
        std::vector<FamilyLeaf> leaves;
    };

    enum class PendingKind : std::uint8_t {
        whole,
        slice,
        element,
        element_part,
    };

    struct Pending {
        PendingKind kind { };
        std::uint32_t member { };
        InstructionIndex instruction { };
        std::uint32_t target { };
        std::uint32_t offset { };
        PackedLogic4 value;
        PackedLogic4 index;
        PackedLogic4 base;
        bool signed_index { };
        bool linear_index { };
        DynamicPartIndex part;
    };

    /// One deferred nonblocking write, in program order. Narrow slot and
    /// memory element writes are plain data; others refer to a generic
    /// Pending by index.
    enum class NbaKind : std::uint8_t {
        slot,
        element,
        element_part,
        generic,
    };

    struct NbaEntry {
        NbaKind kind { };
        bool linear_index { };
        std::uint32_t member { };
        std::uint32_t target { };
        std::uint32_t offset { };
        std::uint32_t width { };
        std::uint32_t index_width { };
        InstructionIndex instruction { };
        Word value { };
        Word index { };
        Word base { };
        const DynamicPartIndex* part { };
        /// VHDL slot writes: 'U' elements of value.
        std::uint64_t unknown { };
    };

    // Storage access.
    [[nodiscard]] Word slot_word(std::uint32_t slot) const noexcept
    {
        const auto offset = slots_[slot].offset;
        return { arena_[offset], arena_[offset + 1U] };
    }
    [[nodiscard]] PackedLogic4 slot_value(std::uint32_t slot) const;
    void write_slot_word(std::uint32_t slot, Word value, std::uint32_t offset,
        std::uint32_t width, std::uint32_t member);
    void write_slot_value(std::uint32_t slot, const PackedLogic4& value,
        std::optional<std::uint32_t> offset, std::uint32_t member,
        InstructionIndex instruction);
    /// `changed_mask` holds the changed bits starting at `field_offset`.
    void slot_changed(std::uint32_t slot, std::uint64_t changed_mask,
        const std::uint64_t* before, std::uint32_t field_offset = 0U);
    /// slot_changed's work beyond the notify targets (ranged readers,
    /// outputs, waiters, edges).
    void slot_changed_general(std::uint32_t slot, std::uint64_t changed_mask,
        const std::uint64_t* before, std::uint32_t field_offset);
    [[nodiscard]] Logic4 slot_edge_bit(std::uint32_t slot) const noexcept;
    void store_slot_bits(std::uint32_t slot, const PackedLogic4& value,
        std::uint32_t offset);
    void commit_round();
    void write_slot_immediate(std::uint32_t slot, PackedLogic4 value,
        std::optional<std::uint32_t> offset, std::uint32_t member,
        InstructionIndex instruction);
    /// Stores a Logic4 word into any slot (Logic9 slots get its codes, with
    /// 'U' for the `unknown` elements; Logic4 slots read those as X).
    void store_planes_word(std::uint64_t* base, std::uint32_t words,
        std::uint32_t planes, Word value, std::uint32_t offset,
        std::uint32_t width, std::uint64_t unknown);
    void store_slot_word(std::uint32_t slot, Word value, std::uint32_t offset,
        std::uint32_t width, std::uint64_t unknown = 0U);
    /// VHDL delta mode: compiled execution met an unrepresentable value;
    /// the reference evaluator finishes the activation.
    void handle_deopt(std::uint32_t member, CompiledBody& body,
        const static_kernel_detail::KernelDeopt& deopt);
    /// Runs the reference evaluator from pc to the wait, or to `until`;
    /// returns where it stopped.
    std::uint32_t run_generic_from(std::uint32_t member, std::uint32_t pc,
        std::uint32_t until = static_kernel_detail::no_slot);
    void verify_run(std::uint32_t member);
    /// Copies the reference registers into the compiled register file, or
    /// keeps the member on the reference evaluator.
    void sync_compiled_registers(std::uint32_t member);
    [[nodiscard]] Word read_field(std::uint32_t slot, std::uint32_t offset,
        std::uint32_t width) const noexcept;
    [[nodiscard]] bool owned(SignalId signal) const noexcept
    {
        return slot_of_signal_[signal] != static_kernel_detail::no_slot
            || family_of_signal_[signal] != static_kernel_detail::no_slot;
    }
    /// The signal's current value in its own kind (Logic9 for VHDL signals).
    [[nodiscard]] PackedLogic4 read_signal(SignalId signal);
    [[nodiscard]] PackedLogic4 assemble(const Family& family) const;
    void write_signal_owned(SignalId signal, PackedLogic4 value,
        std::optional<std::uint32_t> offset, std::uint32_t member,
        InstructionIndex instruction);
    [[nodiscard]] PackedLogic4 container_element(
        const Container& container, std::size_t ordinal) const;
    [[nodiscard]] Word container_element_word(
        const Container& container, std::size_t ordinal) const;
    void write_container_element(Container& container, std::size_t ordinal,
        const PackedLogic4& value, std::uint32_t member,
        InstructionIndex instruction);
    /// Arena storage for an elements-storage container, holding `initial`.
    void place_elements(Container& container, const ContainerValue& initial);
    void write_element(const Pending& pending);
    /// Narrow element write; false when the reference path must report an
    /// error or handle an unsupported shape.
    [[nodiscard]] bool write_element_narrow(const NbaEntry& entry);
    void write_element_entry(const NbaEntry& entry);
    void push_generic_pending(Pending pending);
    /// Queues a nonblocking memory element write (SystemVerilog).
    void push_element_write(const NbaEntry& entry);
    /// A whole-array copy between private containers of one type.
    void copy_container(std::uint32_t destination, std::uint32_t source,
        std::uint32_t member, InstructionIndex instruction);
    void write_container_element_word(Container& container,
        std::size_t ordinal, Word value, std::uint32_t member);
    void write_host(std::uint32_t member, SignalId signal, PackedLogic4 value,
        std::optional<std::uint32_t> offset, bool blocking,
        SignalUpdateDomain domain);

    // Scheduling.
    void schedule(std::uint32_t member);
    void schedule_target(const ScheduleTarget& target);
    void enqueue(std::uint32_t level, std::uint32_t entry);
    void build_schedule_targets();
    [[nodiscard]] bool target_pending(std::uint32_t id) const;
    void run(std::uint32_t member);
    void run_member(std::uint32_t member);
    /// Whether a member's first run (the process prologue) is the same as
    /// an ordinary activation: its activations enter at a jump to operation
    /// 0, so both run the original operations from 0 to the wait.
    [[nodiscard]] static bool first_run_is_activation(const Member& member);
    /// A VHDL member whose compiled code runs its prologue (operations 0 to
    /// prologue_end) on the first run, then continues at the activation
    /// entry: a specialized member's prologue_end is the original body, so
    /// the compiled code jumps from there to the specialized body.
    [[nodiscard]] static bool native_prologue(const Member& member);
    void run_generic(std::uint32_t member);
    [[nodiscard]] std::uint32_t step_generic(std::uint32_t member,
        std::uint32_t pc);
    /// step_generic's semantics of one operation kind.
    template <typename Op>
    [[nodiscard]] std::uint32_t step_operation(std::uint32_t member,
        std::uint32_t pc, const Op& op);
    /// `start` static_kernel_prologue_entry: a VHDL process's first run,
    /// from operation 0 (its prologue).
    void run_compiled(std::uint32_t member, std::uint32_t start = 0U);
    void execute(CompiledBody& body, std::uint32_t member);
    void execute_body(CompiledBody& body, std::uint32_t member);
    /// build_partitions: forwards slot stores to later loads in a partition
    /// pass.
    void forward_partition_stores(CompiledBody& program,
        const std::unordered_map<std::uint32_t, std::uint32_t>& slot_of_offset);
    std::size_t forwarded_loads_ { };
    /// Returns whether a two-state caller got a result with X or U bits.
    /// execute_generic's in-place path for a narrow field written into a
    /// wide register; false when the reference path must run instead.
    bool wide_part_insert(CompiledBody& body, const Word* registers,
        const DynamicPartInsert& insert, std::uint32_t base,
        bool logic9_source);
    bool execute_generic(CompiledBody& body, Word* registers,
        const static_kernel_detail::KInst& inst, std::uint32_t member,
        bool two_state_code = false);
    /// execute_generic for operation kind K (generated code's per-kind
    /// helper native_generic_kind<K>): no dispatch over operation kinds.
    template <std::size_t K>
    bool execute_generic_kind(CompiledBody& body, Word* registers,
        const static_kernel_detail::KInst& inst, std::uint32_t member,
        bool two_state_code);
    template <std::size_t K>
    static void native_generic_kind(StaticKernelNativeFrame* frame,
        Word* registers, std::uint32_t at);
    /// A generic instruction's operands into the member's reference
    /// registers, and its result back (true: two-state code got X or 'U').
    void generic_load_operands(CompiledBody& body, const Word* registers,
        const static_kernel_detail::KInst& inst, std::uint32_t member,
        const std::vector<RegisterId>& reads);
    bool generic_store_result(CompiledBody& body, Word* registers,
        const static_kernel_detail::KInst& inst, std::uint32_t member,
        std::optional<RegisterId> write, bool two_state_code);
    void note_generic_profile(const CompiledBody& body,
        const static_kernel_detail::KInst& inst, std::uint32_t member,
        const char* mangled, const std::vector<RegisterId>& reads,
        std::optional<RegisterId> write);
    std::vector<RegisterId> generic_reads_;
    /// step_operation and behavioral_operation by operation ordinal
    /// (simir_static_kernel.cpp, simir_static_kernel_behavioral.cpp).
    using OperationStep = std::uint32_t (StaticKernel::*)(std::uint32_t member,
        std::uint32_t pc);
    using BehavioralOperationStep = bool (StaticKernel::*)(std::uint32_t member,
        std::uint32_t pc);
    [[nodiscard]] static const std::array<OperationStep,
        static_kernel_detail::operation_kind_count>& operation_steps();
    [[nodiscard]] static const std::array<BehavioralOperationStep,
        static_kernel_detail::operation_kind_count>& behavioral_operation_steps();
    template <typename Op>
    std::uint32_t step_operation_at(std::uint32_t member, std::uint32_t pc);
    template <typename Op>
    bool behavioral_operation_at(std::uint32_t member, std::uint32_t pc);
    /// The slow semantics of one instruction kind; native code reaches them
    /// through that kind's helper. The untemplated forms select the kind at
    /// run time (kernel bytecode only).
    template <static_kernel_detail::KOp Op>
    [[nodiscard]] Word evaluate_slow_op(const CompiledBody& body,
        const static_kernel_detail::KInst& inst, std::uint32_t at,
        std::uint32_t member, std::uint32_t body_begin, Word x, Word y, Word z,
        std::uint32_t resolved, const std::vector<PackedLogic4>* wide);
    template <static_kernel_detail::KOp Op>
    void effect_slow_op(const CompiledBody& body,
        const static_kernel_detail::KInst& inst, std::uint32_t at,
        std::uint32_t member, std::uint32_t body_begin, Word x, Word y, Word z,
        std::uint32_t resolved);
    [[nodiscard]] Word evaluate_slow(const CompiledBody& body,
        const static_kernel_detail::KInst& inst, std::uint32_t at,
        std::uint32_t member, std::uint32_t body_begin, Word x, Word y, Word z,
        std::uint32_t resolved, const std::vector<PackedLogic4>* wide);
    void effect_slow(const CompiledBody& body,
        const static_kernel_detail::KInst& inst, std::uint32_t at,
        std::uint32_t member, std::uint32_t body_begin, Word x, Word y, Word z,
        std::uint32_t resolved);
    void run_partition(std::uint32_t partition);
    void build_partitions(
        const std::vector<std::vector<std::uint32_t>>& successors);
    void build_native(StaticKernelCodegen& codegen);
    /// Whether every register's X plane and 'U' mask is zero (clearing them
    /// for registers each activation writes before reading).
    [[nodiscard]] static bool registers_known(CompiledBody& body) noexcept;
    /// Counts a run of a lazily compiled template, compiling the templates
    /// that became frequent once enough of them are waiting.
    void note_lazy_run(std::uint32_t lazy_template);
    void compile_lazy_templates();
    /// A behavioral template is compiled once its threads have interpreted
    /// about as many instructions as compiling it costs (200 + size / 10
    /// per template instruction). Testbench code is large and mostly cold;
    /// compiling an instruction costs about 10 µs in a small function and
    /// 25 µs or more in a large one, against about 25-40 ns to interpret
    /// one, so waiting until the interpretation has cost that much is never
    /// more than about twice the better choice.
    void note_lazy_work(std::uint32_t lazy_template, std::size_t steps);
    /// Compiles the full-semantics code of a two-state template.
    void compile_full_template(std::uint32_t full_template);
    void run_native(NativeUnit& unit, CompiledBody& body,
        std::uint32_t member,
        bool full = false, std::uint32_t start = 0U);
    /// The part of run_native after the call returns something other than
    /// 0 with frame status 0.
    void finish_native(NativeUnit& unit, CompiledBody& body,
        std::uint32_t member, StaticKernelNativeFrame& frame,
        std::uint32_t status, bool full);
    /// What an ordinary run of a member needs, packed so the run does not
    /// touch the member record: set when the member runs native code with
    /// no check beyond the call (refresh_fast_run), else `entry` is null.
    struct FastRun {
        StaticKernelNativeEntry entry { };
        /// body->registers.data(), kept here so a run does not load the
        /// body first (one dependent cache miss per member run).
        kernel_word::Word* registers { };
        const std::uint32_t* bindings { };
        const void* program { };
        CompiledBody* body { };
        bool two_state { };
        std::uint32_t two_state_runs { };
    };
    std::vector<FastRun> fast_runs_;
    void refresh_fast_run(std::uint32_t member);
    template <static_kernel_detail::KOp Op>
    static void native_evaluate(StaticKernelNativeFrame* frame,
        std::uint32_t at, std::uint64_t xa, std::uint64_t xb, std::uint64_t ya,
        std::uint64_t yb, std::uint64_t za, std::uint64_t zb,
        std::uint64_t* out);
    template <static_kernel_detail::KOp Op>
    static void native_effect(StaticKernelNativeFrame* frame,
        std::uint32_t at, std::uint64_t xa, std::uint64_t xb, std::uint64_t ya,
        std::uint64_t yb, std::uint64_t za, std::uint64_t zb,
        std::uint64_t* out);
    /// Fills `helpers.evaluate_ops` and `effect_ops`: one helper per
    /// instruction kind with slow semantics.
    static void set_instruction_helpers(StaticKernelNativeHelpers& helpers);
    static void native_notify(StaticKernelNativeFrame* frame,
        std::uint32_t slot, std::uint64_t changed);
    static void native_notify_field(StaticKernelNativeFrame* frame,
        std::uint32_t slot, std::uint64_t changed, std::uint32_t field_offset);
    void move_wide(const static_kernel_detail::CompiledBody& body,
        const static_kernel_detail::KInst& inst, std::uint32_t at,
        std::uint32_t member_index, std::uint32_t source_slot, Word base,
        std::uint32_t target_slot);
    static void native_fail(StaticKernelNativeFrame* frame, std::uint32_t at,
        std::uint32_t reason);
    void compile_members();
    /// Appends a VHDL member's body specialized on its constant control
    /// flow and enters it from then on; false when not applicable.
    bool specialize_member(std::uint32_t member);
    void eliminate_dead_constants(CompiledBody& body, std::uint32_t member);
    void track_unknowns(CompiledBody& body, std::uint32_t member) const;
    void prune_operations(std::uint32_t member,
        std::vector<std::uint8_t>& skip,
        std::vector<std::uint64_t>& live_at_entry) const;
    /// prune_operations results by (body identity, body begin, body end).
    std::map<std::tuple<const void*, std::uint32_t, std::uint32_t>,
        std::pair<std::vector<std::uint8_t>, std::vector<std::uint64_t>>>
        prune_cache_;
    [[nodiscard]] std::optional<CompiledBody> compile(std::uint32_t member);
    void settle();
    void check_owned_edges();
    void commit_pending();
    /// `immediate` commits Active-region values at once. `ending` (the end
    /// of simulation, where no later region runs and the scheduler may have
    /// discarded what earlier publishes staged) commits every output's
    /// current value at once, in every domain.
    void publish(bool immediate = false, bool ending = false);
    void initialize();
    [[nodiscard]] bool scan_inputs(bool all = true);
    /// Take one input's committed host value into its mirror and wake its
    /// readers; false when it is unchanged.
    bool refresh_input(Input& input);
    /// Take a new value into an input's mirror and wake its readers.
    void apply_input(Input& input, PackedLogic4 value, bool event = false);
    [[noreturn]] void fail(std::uint32_t member, InstructionIndex instruction,
        const std::string& message) const;

    Impl& impl_;
    std::vector<Member> members_;
    std::vector<Partition> partitions_;

    /// Hot scheduling state, packed apart from the large member and
    /// partition records; filled by build_schedule_targets.
    struct MemberSchedule {
        std::uint32_t partition { static_kernel_detail::no_slot };
        std::uint32_t position { };
        std::uint32_t level { };
        StaticKernelMemberKind kind { };
        bool queued { };
        bool triggered { };
        bool vhdl { };
        /// Mixed kernels: queued for the next VHDL round, or held for the
        /// next delta (a SystemVerilog member woken by a VHDL commit).
        bool round { };
        bool deferred { };
    };
    /// Whole-slot targets [begin, end) in notify_targets_. `general` marks
    /// a slot that also has ranged readers, an output, edges or several
    /// writers, which slot_changed handles from the full Slot record.
    struct SlotNotify {
        std::uint32_t begin { };
        std::uint32_t end { };
        bool general { };
    };
    std::vector<MemberSchedule> member_schedule_;
    std::vector<std::uint8_t> partition_queued_;
    std::vector<std::uint32_t> partition_level_;
    std::vector<SlotNotify> slot_notify_;

    /// Behavioral tier. A thread is one resumable execution context of a
    /// behavioral member; fork children are threads of the same member and
    /// share its registers.
    struct Thread {
        std::uint32_t member { };
        std::uint32_t pc { };
        std::vector<InstructionIndex> call_stack;
        std::vector<Member::Frame> frames;
        std::size_t frame_bytes { };
        std::vector<Member::SavedArray> saved_arrays;
        std::vector<std::size_t> saved_array_marks;
        /// The forking thread and the fork group this child belongs to.
        std::uint32_t parent { static_kernel_detail::no_slot };
        std::uint32_t group { static_kernel_detail::no_slot };
        /// Live children; the parent of a join-all fork resumes at zero.
        std::uint32_t children { };
        /// Bumped whenever the thread resumes, so stale waiter and timer
        /// entries from an earlier wait are ignored.
        std::uint64_t epoch { };
        bool alive { };
        bool queued { };
        /// VHDL wait with timeout (IEEE 1076-2019 10.2): the absolute
        /// deadline of the current wait statement (kept across the re-waits
        /// of a wait-until condition) and the register that receives
        /// whether the resumption was the timeout.
        std::optional<SimulationTick> deadline;
        std::optional<RegisterId> timeout_result;
        /// Compiled members: the synthetic call stack (pointer, entries)
        /// saved while the thread is suspended.
        std::vector<Word> stack;
        /// The last debug point the thread passed (a source position for
        /// its process_suspend notification); no_slot before the first.
        std::uint32_t last_point { static_kernel_detail::no_slot };
        /// Debugger plan: the process id of a fork child (as the reference
        /// evaluator numbers them); no_slot for a member's own threads.
        std::uint32_t debug_process { static_kernel_detail::no_slot };
        /// The thread's stable order as a reference process: its member's
        /// process, or for a fork child the number the reference evaluator
        /// gives the child it spawns, after the processes that exist.
        ProcessId order { };
    };
    struct ForkGroup {
        std::uint32_t parent { };
        ForkJoinKind join { ForkJoinKind::all };
        std::uint32_t remaining { };
        bool resumed { };
    };
    struct Waiter {
        std::uint32_t thread { };
        std::uint64_t epoch { };
        EdgeKind edge { EdgeKind::any };
        /// LSB-normalized any-change range; zero width means the whole signal
        /// (IEEE 1800-2023 9.4.2: a part-select event control observes only
        /// the selected bits).
        std::uint32_t offset { };
        std::uint32_t width { };
    };
    /// What a signal change tells wake_waiters about the selected bits: a
    /// slot's changed mask or pre-change words, or an input's old and new
    /// values.
    struct WaitChange {
        Logic4 before { };
        Logic4 after { };
        const std::uint64_t* slot_before { };
        const PackedLogic4* input_before { };
        const PackedLogic4* input_after { };
        std::uint32_t slot { };
        std::uint32_t field_offset { };
        std::uint64_t changed_mask { };
        bool event { };
    };
    struct Timer {
        SimulationTick time { };
        std::uint64_t sequence { };
        std::uint32_t thread { };
        std::uint64_t epoch { };
        friend bool operator>(const Timer& left, const Timer& right)
        {
            return left.time != right.time ? left.time > right.time
                                            : left.sequence > right.sequence;
        }
    };
    std::vector<Thread> threads_;
    std::vector<std::uint32_t> free_threads_;
    std::vector<ForkGroup> fork_groups_;
    std::vector<std::uint32_t> free_fork_groups_;
    std::vector<std::uint32_t> ready_threads_;
    std::vector<std::uint32_t> inactive_threads_;
    std::vector<std::vector<Waiter>> slot_waiters_;
    std::vector<std::vector<Waiter>> input_waiters_;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<>> timers_;
    std::uint64_t timer_sequence_ { };
    /// The wake time of the scheduler task that will activate the kernel.
    std::optional<SimulationTick> armed_timer_;
    /// VHDL behavioral threads (IEEE 1076-2019 14.7.5.3): threads ready to
    /// run in the next VHDL round; their timers, which expire in the update
    /// phase of their time and resume the process in the following delta;
    /// the wake time of the update-phase task that releases them; and
    /// whether that task has run (collect_timers then takes the due timers).
    std::vector<std::uint32_t> vhdl_ready_threads_;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<>> vhdl_timers_;
    std::optional<SimulationTick> armed_vhdl_timer_;
    bool vhdl_timers_released_ { };
    /// Some member is a VHDL behavioral member: kernel-owned time is off
    /// (a VHDL time step is a sequence of host deltas).
    bool vhdl_threads_ { };
    bool behavioral_ { };
    bool stopped_ { };
    /// The thread that executed $stop (Pause); it resumes at the next
    /// kernel activation, like the reference process the scheduler requeues.
    std::uint32_t paused_thread_ { static_kernel_detail::no_slot };
    /// Mixed-language kernel state (activate_mixed): VHDL members of the
    /// next round, SystemVerilog work held until the next delta while a VHDL
    /// round commits, and the parked write queue of the inactive language.
    bool mixed_ { };
    /// Mixed mode: host processes read kernel outputs or write kernel
    /// inputs, so each delta round is also a host delta.
    bool host_boundary_ { };
    /// Kernel-owned time (see activate): enabled for a closed kernel the
    /// application does not observe between time steps.
    bool time_warp_ { };
    /// One per materialize: a lazy member runs at most once in it.
    std::uint64_t lazy_epoch_ { };
    /// No host process, output or host-written input.
    bool closed_ { };
    static constexpr std::size_t max_warp_steps = 4096U;
    /// The time step the kernel runs when it advanced past the scheduler.
    std::optional<SimulationTick> warp_time_;
    std::uint64_t profile_warps_ { };
    [[nodiscard]] SimulationTick kernel_now() const noexcept
    {
        return warp_time_ ? *warp_time_ : impl_.scheduler.now();
    }
    void activate_step();
    /// Whether the pending NBA commit must wait for the host's NBA region;
    /// arms the task that performs it.
    [[nodiscard]] bool defer_nonblocking();

    [[nodiscard]] std::optional<SimulationTick> next_timer();
    void schedule_stop_at(SimulationTick time);
    /// Inputs some process writes (scan_inputs within an activation).
    std::vector<std::uint32_t> written_inputs_;
    /// scan_inputs' reused list of the inputs it refreshes.
    std::vector<std::uint32_t> scan_order_;
    bool committing_round_ { };
    std::vector<std::uint32_t> next_round_;
    /// The round being run (swapped with next_round_ to keep capacity).
    std::vector<std::uint32_t> round_;
    /// sort_round's member bitmap, all zero between rounds.
    std::vector<std::uint64_t> round_bits_;
    void sort_round();
    std::vector<ScheduleTarget> deferred_targets_;
    /// Swapped with deferred_targets_ to keep capacity.
    std::vector<ScheduleTarget> deferred_buffer_;
    std::vector<std::uint32_t> deferred_threads_;
    StaticKernelWriteQueue parked_writes_ { };
    std::vector<StaticKernelWrite> parked_storage_;
    std::vector<Pending> parked_pending_;
    void swap_write_queues();
    void sv_drain();
    /// sv_drain outside a round of a host-boundary mixed kernel: the VHDL
    /// members and threads it wakes are held for the next host delta.
    void sv_drain_outside_round();
    std::vector<std::uint32_t> held_round_;
    std::vector<std::uint32_t> held_threads_;
    void activate_mixed();
    /// The combinational member settle() is running (no_slot otherwise).
    std::uint32_t running_member_ { static_kernel_detail::no_slot };
    ProcessId host_process_ { };
    /// Slots a behavioral thread may wait on: never silent, always on the
    /// general notify path; the edge bit last seen by dynamic waiters.
    std::vector<std::uint8_t> slot_waitable_;
    std::vector<Logic4> slot_wait_seen_;
    void timer_due(SimulationTick time);
    // Activates the kernel from a due timer's host task (its own and its
    // hosted processes'), in that task's Active round.
    void activate_for_due_timer();
    void vhdl_timer_due(SimulationTick time);
    void arm_vhdl_timer();
    /// Runs the VHDL threads of this round; false when none was ready.
    [[nodiscard]] bool run_vhdl_threads();

    enum class Boundary : std::uint8_t { none, advance, suspend, finish };
    /// Behavioral: the thread and position compiled code starts at, and the
    /// suspension operation it stopped at.
    std::uint32_t behavioral_entry_ { };
    std::optional<std::uint32_t> behavioral_suspended_;
    std::uint32_t deopt_resume_ { };
    [[nodiscard]] bool behavioral_step(std::uint32_t member, std::uint32_t pc);
    /// behavioral_step's semantics of one operation kind; false when the
    /// kind is not a behavioral runtime-library operation.
    template <typename Op>
    [[nodiscard]] bool behavioral_operation(std::uint32_t member,
        std::uint32_t pc, const Op& op);
    [[nodiscard]] Boundary behavioral_boundary(std::uint32_t thread,
        std::uint32_t pc);
    /// behavioral_boundary for one operation kind (Boundary::none for kinds
    /// that are not thread boundaries), and by operation ordinal.
    template <typename Op>
    [[nodiscard]] Boundary behavioral_boundary_op(std::uint32_t thread,
        std::uint32_t pc, const Op& op);
    template <typename Op>
    [[nodiscard]] Boundary behavioral_boundary_at(std::uint32_t thread,
        std::uint32_t pc);
    using BoundaryStep = Boundary (StaticKernel::*)(std::uint32_t thread,
        std::uint32_t pc);
    [[nodiscard]] static const std::array<BoundaryStep,
        static_kernel_detail::operation_kind_count>& boundary_steps();

    /// Operation tier: native code that runs a member's SimIR operations on
    /// its reference register file through one helper per operation kind
    /// (KOp::step; build_operation_tier). It runs what compiled code does
    /// not: members without compiled bodies, VHDL threads, generic mode and
    /// the rest of a deoptimized activation.
    struct OperationTier {
        CompiledBody body;
        StaticKernelNativeEntry entry { };
    };
    /// Per member: the member variant (a run ends at the member's wait or
    /// leaves its body) and the thread variant; built on first use. Members
    /// with one shared operation body (body_identity) and equal bounds share
    /// their tier.
    std::vector<std::array<OperationTier*, 2>> operation_tiers_;
    std::vector<std::unique_ptr<OperationTier>> operation_tier_store_;
    std::map<std::tuple<const void*, std::uint32_t, std::uint32_t, std::uint32_t,
                 bool>,
        OperationTier*>
        shared_operation_tiers_;
    /// Specialized members' tiers by content (hash, then equal code).
    std::unordered_map<std::uint64_t, std::vector<OperationTier*>>
        operation_tiers_by_content_;
    /// The member's tier slot, built (not compiled) when it is new.
    [[nodiscard]] OperationTier& operation_tier_slot(std::uint32_t member, bool thread);
    [[nodiscard]] std::unique_ptr<OperationTier> build_operation_tier(
        std::uint32_t member, bool thread) const;
    [[nodiscard]] StaticKernelTemplate operation_template(
        const OperationTier& tier) const;
    [[nodiscard]] OperationTier* operation_tier(std::uint32_t member, bool thread);
    /// Runs the operation tier from `pc`: the position it stopped at
    /// (operation_tier_stop: a thread boundary, operation_tier_boundary_ at
    /// operation_tier_pc_), or nothing when the member has no tier code.
    [[nodiscard]] std::optional<std::uint32_t> run_operation_tier(
        std::uint32_t member, std::uint32_t pc, bool thread);
    /// Hosted tier: the operation tier of a process the evaluator would run
    /// (hosted processes and the programs the kernel's host runs): one
    /// `fsim_sk_hosted_step_<ordinal>` per operation, the evaluator's own
    /// semantics for that kind (Impl::hosted_step). Programs with equal step
    /// code (fork children, instances of one body) share one tier; built for
    /// the hosted processes ahead of time, else on first use. A process
    /// remembers its tier (ProcessColdState::hosted_tier).
    std::vector<std::unique_ptr<OperationTier>> hosted_tier_store_;
    std::unordered_map<std::uint64_t, std::vector<OperationTier*>>
        hosted_tiers_by_content_;
    [[nodiscard]] OperationTier& hosted_tier_slot(const OperationList& operations);
    [[nodiscard]] OperationTier* hosted_tier(ProcessState& process,
        const OperationList& operations);
    void build_hosted_tiers();
    template <std::size_t K>
    static std::uint32_t native_hosted_step_kind(StaticKernelNativeFrame* frame,
        std::uint32_t pc);
    template <std::size_t K>
    static std::uint32_t native_boundary_kind(void* kernel, std::uint32_t thread,
        std::uint32_t pc);
    /// The boundary helper native code stored at the last compiled thread
    /// suspension (StaticKernelNativeFrame::boundary).
    void* behavioral_boundary_helper_ { };
    /// The thread a thread-variant run belongs to.
    std::uint32_t operation_tier_thread_ { };
    std::size_t operation_tier_steps_ { };
    Boundary operation_tier_boundary_ { };
    std::uint32_t operation_tier_pc_ { };
    template <std::size_t K>
    static std::uint32_t native_step_kind(StaticKernelNativeFrame* frame,
        std::uint32_t pc);
    template <std::size_t K>
    static std::uint32_t native_thread_step_kind(StaticKernelNativeFrame* frame,
        std::uint32_t pc);
    [[nodiscard]] std::optional<std::uint32_t> run_behavioral_compiled(
        std::uint32_t thread, std::uint32_t start);
    void sync_behavioral_registers(std::uint32_t member, std::uint32_t keep);
    void prepare_behavioral();
    [[nodiscard]] bool start_after_procedures();
    void start_behavioral();
    [[nodiscard]] std::uint32_t spawn_thread(std::uint32_t member,
        std::uint32_t pc, std::uint32_t parent, std::uint32_t group);
    void ready_thread(std::uint32_t thread);
    [[nodiscard]] bool run_ready_threads();
    void run_thread(std::uint32_t thread);
    void wait_on(std::uint32_t thread, std::span<const Sensitivity> entries);
    void finish_thread(std::uint32_t thread);
    void wake_waiters(std::vector<Waiter>& waiters, const WaitChange& change);
    [[nodiscard]] bool waiter_range_changed(const Waiter& waiter,
        const WaitChange& change) const;
    void collect_timers();
    void arm_timer();
    void behavioral_output(std::uint32_t member, std::string_view text,
        bool newline);
    std::vector<ScheduleTarget> notify_targets_;
    std::vector<std::unique_ptr<CompiledBody>> templates_;
    /// Owns the generated code.
    std::shared_ptr<StaticKernelCodegen> codegen_;
    /// Cold member templates are compiled when they have run
    /// lazy_threshold_ times (by default at their first run).
    struct LazyTemplate {
        StaticKernelTemplate code;
        std::vector<NativeUnit*> units;
        std::uint32_t runs { };
        /// Behavioral templates: instructions interpreted so far
        /// (note_lazy_work).
        std::uint64_t work { };
    };
    std::vector<LazyTemplate> lazy_templates_;
    /// Two-state templates' full-semantics code, compiled on demand.
    std::vector<LazyTemplate> full_templates_;
    /// Per template: the instructions its full code continues two-state
    /// code at (StaticKernelTemplate::resume_points); sized once.
    std::vector<std::vector<std::uint32_t>> continuations_;
    /// The native code running now is two-state.
    bool two_state_running_ { };
    std::vector<std::uint32_t> lazy_pending_;
    std::uint32_t lazy_threshold_ { };
    /// Instructions the last execute_body run interpreted.
    std::size_t executed_steps_ { };
    StaticKernelNativeHelpers native_helpers_;
    std::exception_ptr native_exception_;
    std::size_t native_units_ { };
    std::uint32_t running_partition_ { static_kernel_detail::no_slot };
    std::uint32_t running_position_ { };
    std::vector<std::uint64_t> arena_;
    /// VHDL delta mode (StaticKernelRuntimeSpec::vhdl).
    bool vhdl_ { };
    /// Debugger plan (StaticKernelRuntimeSpec::debug_points).
    bool debug_points_ { };
    /// Debugger plan: the program the host member ran before its scheduler
    /// program became the kernel stub.
    std::optional<Process> host_original_;
    [[nodiscard]] ProcessProgramView program_of(const Member& member) const
    {
        return host_original_ && member.process == host_process_
            ? ProcessProgramView { *host_original_ }
            : impl_.processes.program_view(member.process);
    }
    /// The debugger stopped the run at a thread's debug point or at its
    /// suspension; the next activation clears the kernel's stop.
    bool debug_stopped_ { };
    /// Debugger plan: fork children by their reference-style process id, and
    /// the next id to hand out.
    std::unordered_map<ProcessId, std::uint32_t> debug_children_;
    std::size_t next_child_process_ { };
    /// The thread running now (no_slot outside one): its host updates are
    /// its process's (write_host).
    std::uint32_t running_thread_ { static_kernel_detail::no_slot };
    /// Whether the interpreter would show `reg` of `member` as written at the
    /// member's last debug point (else its debug local is <uninitialized>).
    [[nodiscard]] bool debug_register_defined(
        const Member& member, RegisterId reg) const;
    /// Raises the execution-point hook for operation `pc` of the thread's
    /// member; true when the run must stop after it (hook set and the
    /// scheduler's stop is requested).
    [[nodiscard]] bool debug_point(std::uint32_t thread, std::uint32_t pc);
    /// process_suspend of the thread at `pc` (a wait, join or halt).
    void debug_suspend(std::uint32_t thread, std::uint32_t pc);
    /// Stops the run after the current thread step: `paused` resumes first.
    void debug_stop(std::uint32_t paused);
    std::vector<std::uint32_t> touched_;
    std::vector<std::uint64_t> round_before_;
    std::optional<static_kernel_detail::KernelDeopt> pending_deopt_;
    /// VHDL delta mode: the round's assignments (word writes and references
    /// to pending_generic_), in program order.
    std::vector<StaticKernelWrite> write_storage_;
    StaticKernelWriteQueue writes_;
    void push_write(const StaticKernelWrite& write);
    /// 'U' mask of the last slow-path result (U-aware instructions).
    std::uint64_t last_unknown_ { };
    std::uint64_t profile_deopts_ { };
    std::uint64_t profile_generic_runs_ { };
    /// Interpretive work, always counted (engine v4 gate: all zero in a
    /// compiled simulation): bytecode member runs, operations stepped by the
    /// reference evaluator, and deoptimizations.
    std::uint64_t interpreted_bytecode_runs_ { };
    std::uint64_t interpreted_operations_ { };
    std::uint64_t interpreted_deopts_ { };
    std::array<std::uint64_t, static_kernel_detail::interpreted_cause_names.size()>
        interpreted_by_cause_ { };
    static_kernel_detail::InterpretedCause interpreting_ {
        static_kernel_detail::InterpretedCause::reference
    };
    /// Sets the cause of the reference evaluator's operations for a scope.
    struct InterpretingScope {
        StaticKernel& kernel;
        static_kernel_detail::InterpretedCause saved;
        InterpretingScope(StaticKernel& owner,
            const static_kernel_detail::InterpretedCause cause)
            : kernel(owner)
            , saved(std::exchange(owner.interpreting_, cause))
        {
        }
        ~InterpretingScope() { kernel.interpreting_ = saved; }
        InterpretingScope(const InterpretingScope&) = delete;
        InterpretingScope& operator=(const InterpretingScope&) = delete;
    };
    /// Why each member has no compiled body (compile_members), for the
    /// interpreted-site census.
    std::vector<const char*> not_compiled_reason_;
    /// FSIM_PROFILE_PHASES census: every site whose run would interpret
    /// (`static-kernel-interpreted reason= count= example=` rows).
    void report_interpreted_sites() const;

public:
    [[nodiscard]] InterpretationCounts interpretation_counts() const
    {
        using static_kernel_detail::InterpretedCause;
        const auto folded = interpreted_by_cause_[static_cast<std::size_t>(
            InterpretedCause::specialize)];
        InterpretationCounts counts { 0U, interpreted_bytecode_runs_,
            interpreted_operations_ - folded, interpreted_deopts_, {}, {}, 0U };
        for (std::size_t cause = 0U; cause < interpreted_by_cause_.size();
             ++cause) {
            if (interpreted_by_cause_[cause] != 0U) {
                counts.kernel_operations_by.emplace_back(
                    static_kernel_detail::interpreted_cause_names[cause],
                    interpreted_by_cause_[cause]);
            }
        }
        return counts;
    }

private:
    std::vector<std::uint64_t> profile_member_deopts_;
    /// Diagnostic (FSIM_KERNEL_VERIFY): every compiled VHDL run is replayed
    /// on the reference evaluator and compared.
    bool verify_ { };
    /// Diagnostic (FSIM_KERNEL_CHECK_INPUTS): compare every input value and
    /// report changes the host made without a revision bump.
    bool check_inputs_ { };
    std::uint64_t verify_mismatches_ { };
    std::vector<Slot> slots_;
    /// What a VHDL round's commit needs of each slot, 16 bytes apiece so
    /// four share a cache line (commit_round touches nothing else for a
    /// slot that does not change).
    struct CommitSlot {
        std::uint32_t offset { };
        /// Where the round saved the slot's prior value (round_before_), once
        /// `touched`.
        std::uint32_t before { };
        std::uint32_t words { };
        std::uint8_t planes { };
        bool touched { };
        bool single_writer { };
    };
    std::vector<CommitSlot> commit_slots_;
    /// Kernel wiring (static_kernel_wiring): the update phase that changes
    /// `source` copies its bits [offset, offset + width) into `target` at
    /// `target_offset` in the same delta cycle, so an input formal
    /// associated with an element or slice of a signal follows it, and an
    /// output formal so associated drives that part of its actual, without
    /// a host round trip (IEEE 1076-2008 6.5.7.1, 14.7.3.2, 14.7.3.3).
    struct Wire {
        std::uint32_t source { };
        std::uint32_t target { };
        std::uint32_t offset { };
        std::uint32_t width { };
        std::uint32_t target_offset { };
    };
    /// Sorted by source slot; `slot_wires_` holds each slot's [begin, end).
    std::vector<Wire> wires_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> slot_wires_;
    /// Copies the wire's bit range; true when the target changed.
    bool copy_wire(const Wire& wire);
    std::vector<std::uint32_t> slot_of_signal_;
    std::vector<std::uint32_t> family_of_signal_;
    std::vector<Family> families_;
    std::vector<Input> inputs_;
    std::vector<std::uint32_t> input_of_signal_;
    /// Mirror slot of each mirrored host signal (no_slot otherwise).
    std::vector<std::uint32_t> mirror_of_signal_;
    void write_mirror(const Input& input);
    std::vector<Container> containers_;
    /// Native view of containers_ (StaticKernelContainerInfo) and the
    /// element tables it points into.
    std::vector<StaticKernelContainerInfo> container_info_;
    std::vector<std::uint32_t> container_elements_;
    /// KOp::wide_move value planes.
    std::vector<std::uint64_t> wide_move_scratch_;
    /// members_[i].process, dense: hot paths record slot writers without
    /// touching the large member records.
    std::vector<ProcessId> member_process_;
    std::vector<std::uint32_t> container_of_object_;
    std::vector<NbaEntry> nba_;
    std::vector<Pending> pending_generic_;
    std::vector<std::uint32_t> output_queue_;
    std::vector<std::uint32_t> trigger_queue_;
    std::vector<std::uint32_t> triggered_;
    /// Level-bucketed dirty queue; `scan_level_` is the lowest level that may
    /// hold entries.
    std::vector<std::vector<std::uint32_t>> buckets_;
    std::uint32_t scan_level_ { };
    std::size_t queued_count_ { };
    bool initialized_ { };
    /// Mixed kernels with a host boundary: a round's VHDL work waits in the
    /// delta's generic Active queue for its SystemVerilog Active work
    /// (defer_vhdl_round); the activation that runs it.
    bool vhdl_round_deferred_ { };
    bool vhdl_round_task_ { };
    [[nodiscard]] bool vhdl_round_waits_for_host() const;
    void defer_vhdl_round();
    bool nonblocking_committed_ { };
    /// This activation staged an Active update to a host signal: #0
    /// threads (Inactive region) resume only after the host applies it.
    bool host_active_staged_ { };
    /// The NBA commit waits for the host's same-time Active and Inactive
    /// work (IEEE 1800-2023 4.5): a host NBA-region task performs it.
    bool nonblocking_deferred_ { };
    /// Running in that task: the first NBA commit proceeds.
    bool nonblocking_region_ { };
    bool in_host_nonblocking_region_ { };
    /// Members that start after the procedures (Member::after_procedures).
    std::vector<std::uint32_t> after_procedure_members_;
    bool yield_requested_ { };
    /// Behavioral threads waiting for the delta after time zero to start
    /// (Member::start_late), in member order.
    std::vector<std::uint32_t> late_members_;
    bool late_start_armed_ { };
    /// The running activation is the late-start task of queue_late_start.
    bool late_start_due_ { };

    // Hosted tier (simir_static_kernel_hosted.cpp). A hosted process keeps
    // its own ProcessState and runs on the reference evaluator
    // (Impl::execute), but the kernel owns its scheduling: the queue_* entry
    // points hand its wakes to these queues, and the activation loop runs
    // them in the Active region (every round, in stable ProcessId order), the
    // Inactive region (a host task of that region re-enters the kernel) and
    // the next delta (the kernel yields).
    struct HostedReady {
        StableOrder order { };
        ProcessId process { };
        /// queue_at wakes: the process's timer_epoch when queued; a disable
        /// that redirects the process advances it and abandons the wake.
        std::uint32_t epoch { };
        bool timer { };
        /// Clear waiting_on_static and the dynamic wait registrations, as the
        /// scheduler tasks of queue_at, queue_active_current, queue_next_delta
        /// and queue_zero_delay_resume do (queue_current does not).
        bool wake { };
        /// The region the wake runs in (the process's execution phase, or the
        /// region a WaitRegion or the current region names).
        SchedulerPhase phase { SchedulerPhase::active };
    };
    struct HostedDelayed {
        SimulationTick time { };
        std::uint64_t delta { };
        HostedReady ready;
    };
    struct HostedTimer {
        SimulationTick time { };
        std::uint64_t sequence { };
        HostedReady ready;
        friend bool operator>(const HostedTimer& left, const HostedTimer& right)
        {
            return left.time != right.time ? left.time > right.time
                : left.ready.order != right.ready.order
                ? left.ready.order > right.ready.order
                : left.sequence > right.sequence;
        }
    };
    bool has_hosted_ { };
    /// Inside activate(): hosted wakes wait for its loop (finish_hosted wakes
    /// the host process for what is left).
    bool in_activation_ { };
    /// This activation runs in a host Inactive-region task (or the NBA one):
    /// hosted Active work waits for the host's Active region.
    bool hosted_inactive_task_ { };
    /// The activation stopped for host Active-region work its hosted
    /// processes queued (yield_to_host_active); it resumes in that region.
    bool host_active_yield_ { };
    /// The rest of an Active round stopped for host tasks of the running
    /// Active batch that come first in stable order; the kernel resumes it
    /// in that batch at host_batch_resume_ (run_merged_active).
    std::vector<HostedReady> batch_hosted_;
    std::vector<std::uint32_t> batch_threads_;
    std::optional<StableOrder> host_batch_resume_;
    bool batch_resume_queued_ { };
    /// The one-shot token that lets the activation run hosted #0 resumptions.
    bool hosted_inactive_region_ { };
    bool hosted_inactive_deferred_ { };
    /// run_hosted_inactive is running: queue_current continues in Inactive.
    bool hosted_running_inactive_ { };
    std::vector<HostedReady> hosted_active_;
    std::vector<HostedReady> hosted_inactive_;
    /// Ready lists of the regions after NBA (observed, reactive,
    /// re-inactive, postponed), indexed by SchedulerPhase. Each non-empty
    /// list has a doorbell: a host task of that phase that runs it, so the
    /// host scheduler keeps the order of the regions (IEEE 1800-2023 4.4,
    /// 4.5, Figure 4-1) and its own events of the region.
    std::array<std::vector<HostedReady>, 8> hosted_regions_;
    std::array<bool, 8> hosted_region_bell_ { };
    /// The region run_hosted_region is running: queue_current continues there.
    std::optional<SchedulerPhase> hosted_running_region_;
    std::vector<HostedDelayed> hosted_next_delta_;
    std::priority_queue<HostedTimer, std::vector<HostedTimer>, std::greater<>>
        hosted_timers_;
    std::uint64_t hosted_sequence_ { };
    std::optional<SimulationTick> armed_hosted_timer_;
    /// Marks the specification's hosted processes in the interpreter.
    void adopt_hosted(const std::vector<StaticKernelMemberSpec>& hosted);
    /// Due timers and next-delta wakes of an earlier delta become Active.
    void collect_hosted();
    /// Runs one round of the hosted Active region; false when none was ready.
    [[nodiscard]] bool run_hosted_active();
    void hold_woken(std::size_t members, std::size_t threads);
    /// Mixed kernels: the hosted processes of one scheduling domain.
    [[nodiscard]] bool run_hosted_active_domain(bool systemverilog);
    [[nodiscard]] bool yield_to_host_active();
    /// The running host Active batch has a task to run before the kernel's
    /// process at this stable order.
    [[nodiscard]] bool host_batch_ahead(StableOrder order) const;
    /// The stable order the kernel resumes at in a later host Active batch:
    /// that of its first ready process.
    [[nodiscard]] StableOrder host_resume_order() const;
    void requeue_host_active();
    /// Queues the kernel's resumption into the running host Active batch.
    void queue_batch_resume(StableOrder order);
    [[nodiscard]] bool hosted_systemverilog(ProcessId process) const;
    [[nodiscard]] bool hosted_systemverilog_pending() const;
    /// One Active round with both hosted and native processes ready: they
    /// run interleaved in process order, as the reference round runs them.
    void run_merged_active();
    /// Runs the hosted #0 resumptions; false when none was ready.
    [[nodiscard]] bool run_hosted_inactive();
    void run_hosted(const HostedReady& ready);
    /// First activation: the hosted declaration initializers run before the
    /// members are initialized (IEEE 1800-2023 6.8).
    void run_hosted_initializers();
    void defer_hosted_inactive();
    /// Queues a wake in its region's list (Active: the Active list) and
    /// rings the region's doorbell.
    void route_hosted(const HostedReady& ready);
    void push_hosted_region(const HostedReady& ready);
    void ring_hosted_region(SchedulerPhase phase);
    void run_hosted_region(SchedulerPhase phase);
    /// Forgets queued hosted work and host tasks the scheduler discarded.
    void discard_hosted();
    void arm_hosted_timer();
    void hosted_timer_due(SimulationTick time);
    /// End of an activation: wakes the host for hosted work left behind.
    void finish_hosted();
    bool profile_ { };
    bool trace_ { };
    std::uint64_t profile_activations_ { };
    std::uint64_t profile_activation_ns_ { };
    std::uint64_t profile_native_calls_[6] { };
    std::unordered_map<const void*, std::uint64_t> profile_template_runs_;
    std::uint64_t profile_slot_changes_ { };
    std::uint64_t profile_schedules_ { };
    std::map<std::string, std::uint64_t> profile_generic_ops_;
    std::uint64_t profile_runs_[3] { };
    std::uint64_t profile_compiled_runs_ { };
    std::uint64_t profile_partition_runs_ { };
    std::vector<std::uint64_t> profile_partition_counts_;
    std::vector<std::uint64_t> profile_partition_operations_;
    std::uint64_t profile_operations_ { };
    std::uint64_t profile_input_changes_ { };
    std::uint64_t profile_publishes_ { };
    /// import_host_write's copy of a slot before the host value lands.
    std::vector<std::uint64_t> import_before_;
    std::vector<std::uint64_t> profile_member_runs_;
    std::vector<std::string> profile_names_;
    std::size_t compiled_members_ { };
    std::size_t specialized_members_ { };
    double specialize_seconds_ { };
    /// specialize_member's recipes (type-erased; see the specializer).
    std::shared_ptr<void> specialize_cache_;
    /// Specializations from an earlier run, applied instead of specializing
    /// (StaticKernelRuntimeSpec::specializations), and where this run
    /// records its own (record_specializations).
    std::shared_ptr<StaticKernelSpecializations> restored_specializations_;
    std::shared_ptr<StaticKernelSpecializations> recorded_specializations_;
    std::shared_ptr<const std::string> restored_bodies_;
    std::shared_ptr<std::string> recorded_bodies_;
    std::shared_ptr<const std::string> restored_native_;
    std::shared_ptr<std::string> recorded_native_;
    std::size_t specialize_cache_hits_ { };
    std::string compile_failure_detail_;
    std::size_t wide_members_ { };
    std::size_t wide_member_ops_ { };
    std::size_t wide_member_total_ops_ { };
};

} // namespace fsim::runtime::simir

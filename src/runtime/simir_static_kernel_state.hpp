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
#include "simir_static_kernel_pool.hpp"
#include "simir_static_kernel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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

/// Identifies the StaticKernel constructor's build (part of
/// static_kernel_image_identity).
[[nodiscard]] std::string static_kernel_constructor_identity();

/// Drains a live queue for one pass: the queue's entries move into a spare
/// vector that keeps its capacity across passes (the live queue takes the
/// spare's empty storage), so a round does not free and regrow them. The
/// spare member is taken for the pass, so a re-entrant pass finds it empty
/// and allocates as before; the larger storage is handed back when the pass
/// ends, which also destroys the entries then.
template <typename T>
class QueueDrain {
public:
    explicit QueueDrain(std::vector<T>& spare)
        : spare_(spare)
        , items_(std::move(spare))
    {
        items_.clear();
        spare_.clear();
    }
    QueueDrain(std::vector<T>& live, std::vector<T>& spare)
        : QueueDrain(spare)
    {
        take(live);
    }
    ~QueueDrain()
    {
        items_.clear();
        if (items_.capacity() > spare_.capacity()) {
            spare_.swap(items_);
        }
    }
    QueueDrain(const QueueDrain&) = delete;
    QueueDrain& operator=(const QueueDrain&) = delete;
    /// Moves the live queue's entries in (only into an empty drain).
    void take(std::vector<T>& live) { items_.swap(live); }
    [[nodiscard]] std::vector<T>& items() noexcept { return items_; }

private:
    std::vector<T>& spare_;
    std::vector<T> items_;
};

constexpr auto no_slot = std::numeric_limits<std::uint32_t>::max();
constexpr auto no_container = std::numeric_limits<std::uint32_t>::max();
/// Container::object of a behavioral member's private array.
constexpr auto no_container_object
    = std::numeric_limits<ContainerObjectId>::max();
constexpr std::size_t run_step_limit = 100'000'000U;
constexpr std::size_t edge_iteration_limit = 100'000U;
constexpr std::uint32_t partition_tag = std::uint32_t { 1 } << 31U;
/// Tags a dispatch group in edge readers and the triggered list.
constexpr std::uint32_t dispatch_group_tag = std::uint32_t { 1 } << 31U;

/// A copy of a slot's words taken before a write, for change detection. Up
/// to 16 words (1024 two-plane bits) live in the object; wider slots use the
/// heap. Local to one write, so worker lanes and nested writes never share it.
class WordSnapshot {
public:
    WordSnapshot(const std::uint64_t* first, const std::size_t count)
    {
        if (count <= inline_.size()) {
            std::copy(first, first + count, inline_.begin());
            data_ = inline_.data();
        } else {
            heap_.assign(first, first + count);
            data_ = heap_.data();
        }
        size_ = count;
    }
    WordSnapshot(const WordSnapshot&) = delete;
    WordSnapshot& operator=(const WordSnapshot&) = delete;

    [[nodiscard]] const std::uint64_t* data() const noexcept { return data_; }
    [[nodiscard]] const std::uint64_t* begin() const noexcept { return data_; }
    [[nodiscard]] const std::uint64_t* end() const noexcept
    {
        return data_ + size_;
    }

private:
    std::array<std::uint64_t, 16> inline_ { };
    std::vector<std::uint64_t> heap_;
    const std::uint64_t* data_ { };
    std::size_t size_ { };
};

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

/// Levels of a combinational dependency graph: each node runs after every
/// node it depends on, condensed over strongly connected components. A
/// node outside every cycle takes its longest-path depth; the members of
/// one cycle take consecutive levels in index order, and the nodes after a
/// cycle start past its last level. `cyclic` counts the nodes on cycles.
struct NodeLevels {
    std::vector<std::uint32_t> level;
    std::uint32_t count { };
    std::uint32_t cyclic { };
};
[[nodiscard]] NodeLevels condensed_levels(
    const std::vector<std::vector<std::uint32_t>>& successors);

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
    /// FSIM_PROFILE_KERNEL_MT (with FSIM_PROFILE_KERNEL): multi-threading
    /// Phase 0 plan report, from the compiled units and the observed runs.
    void report_mt_plan() const;
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
    /// A package static initializer that the application's class services
    /// run before time zero (IEEE 1800-2023 6.21) wrote `signal` on the
    /// host: a kernel-owned signal takes that value.
    void import_startup_write(SignalId signal);
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
    /// blocks and the final members, each at its process's place among the
    /// host's. Final blocks the kernel neither hosts nor runs are the
    /// caller's.
    void run_finals(bool discarded);
    /// Runs a final member's procedure to its end (a host task of
    /// run_finals, or a hosted wake when the kernel hosts processes).
    void run_final_member(std::uint32_t member);
    /// The package static initializers that are members, with their startup
    /// orders: the host runs each at its startup action
    /// (run_startup_member), among the other initializers and callbacks.
    struct StartupMember {
        ProcessId process { };
        std::uint32_t member { };
        std::uint64_t order { };
    };
    [[nodiscard]] const std::vector<StartupMember>& startup_members() const noexcept
    {
        return startup_members_;
    }
    /// Runs a package static initializer member to its end, before time
    /// zero (IEEE 1800-2023 6.21).
    void run_startup_member(ProcessId process);
    /// Whether the resolver program `process` is a member (run_resolver).
    [[nodiscard]] bool resolver_member(const ProcessId process) const noexcept
    {
        return resolver_members_.contains(process);
    }
    /// A driver of a unidirectional switch's source changed
    /// (Impl::wake_switch_source_readers): the strength of the switch's
    /// output follows its source's (IEEE 1800-2023 28.13). When the reader
    /// `process` is a member, the kernel resumes it in the next delta, as
    /// the host queues a reader, and the call returns true.
    [[nodiscard]] bool wake_switch_reader(ProcessId process);
    /// A generic-origin deposit to a host signal (Interpreter::deposit_signal:
    /// the class services' $sv_class_heap_change, VPI, the debugger): the
    /// kernel's readers see it from the next delta, as the host wakes its
    /// processes (queue_next_delta).
    void host_deposit(SignalId signal);
    /// The waiters of the host's mailbox, semaphore and dynamic-event
    /// tables (Impl::run_synchronization) that are kernel threads, by their
    /// process order: whether `process` is one, whether its wait still
    /// stands, a mailbox value delivered to it (written when it resumes),
    /// and its resumption, at once in the current region, as the reference
    /// queues the process (queue_active_current).
    [[nodiscard]] bool synchronization_thread(ProcessId process) const;
    [[nodiscard]] bool synchronization_live(ProcessId process) const;
    void synchronization_deliver(ProcessId process, RegisterId destination,
        const PackedLogic4& value);
    void synchronization_deliver(ProcessId process, StringRegisterId destination,
        const std::string& value);
    bool synchronization_wake(ProcessId process);
    /// A dynamic-event trigger: the thread resumes if it still waits on
    /// `handle`.
    void synchronization_event_wake(ProcessId process, std::uint64_t handle);
    /// One pass of an update-commit program (IEEE 1076-2019 14.7.3), as
    /// Impl::run_vhdl_resolver and run_vhdl_conversion_drivers run it:
    /// `driving` (a resolution function's connected drivers' values, when
    /// there is one) in register `input`, the program run to its halt over
    /// the host's current values of the signals it reads, the result from
    /// `output`.
    [[nodiscard]] PackedLogic4 run_resolver(ProcessId process,
        const std::optional<PackedLogic4>& driving, RegisterId input,
        RegisterId output);
    /// Debugger plan: members stop at their debug points.
    [[nodiscard]] bool debug_points() const noexcept { return debug_points_; }
    /// A mixed kernel with a host boundary: its first round, the VHDL
    /// initialization, runs in the first delta after the host's time-zero
    /// SystemVerilog work it reads (vhdl_round_waits_for_host), so
    /// Interpreter::start queues its first activation instead of running it.
    /// A kernel whose members make delayed writes likewise: their time-zero
    /// updates mature as host tasks of the Active region, which run between
    /// the kernel's rounds (IEEE 1800-2023 4.5, 4.7), not before the
    /// scheduler starts.
    [[nodiscard]] bool queues_first_activation() const noexcept
    {
        return (mixed_ && host_boundary_) || vhdl_host_cycles_
            || host_delayed_writes_;
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
    /// scheduler program is the kernel stub), for the time-zero start's
    /// late-start rule (systemverilog_late_starters), which the planner
    /// applies to the original programs, and for Interpreter::process_program;
    /// null for every other process.
    [[nodiscard]] const Process* startup_program(ProcessId process) const noexcept
    {
        return host_program_ && process == host_process_ ? &*host_program_
                                                         : nullptr;
    }
    /// A member's own program: the host process's original, every other
    /// member's scheduler program.
    [[nodiscard]] ProcessProgramView member_program(const std::uint32_t member) const
    {
        const auto process = members_[member].process;
        if (const auto* original = startup_program(process)) {
            return ProcessProgramView { *original };
        }
        return impl_.processes.program_view(process);
    }
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
        /// A VHDL-2019 process: reports and assertions follow the std.env
        /// controls (report_vhdl).
        bool vhdl_2019 { };
        bool run_at_start { };
        /// A behavioral thread that starts in the delta after time zero's.
        bool start_late { };
        /// A behavioral thread armed at its first static wait before the
        /// once members run (prepare_behavioral).
        bool prearmed { };
        /// The thread the member started as (spawn_thread without a parent).
        std::uint32_t root_thread { static_kernel_detail::no_slot };
        /// A behavioral process that is not initialized (an implicit
        /// signal's driver): armed on its static sensitivity without
        /// running, then run from its first operation (prepare_behavioral).
        bool start_waiting { };
        /// always_comb/always_latch (ProcessStartupPhase::after_procedures):
        /// starts after the initial and always procedures.
        bool after_procedures { };
        /// A final procedure (IEEE 1800-2023 9.2.3): never starts with the
        /// others; run_finals schedules it at the end of simulation.
        bool final_block { };
        /// A package static initializer (run_startup_member).
        bool package_startup { };
        /// A VHDL update-commit program (run_resolver): never starts; the
        /// host's update commit runs one pass per resolution or conversion.
        bool resolver { };
        /// The mirrored inputs a resolver member reads (run_resolver), once
        /// known.
        std::optional<std::vector<std::uint32_t>> resolver_inputs;
        /// The scheduler region its threads resume in (IEEE 1800-2023 4.4.2):
        /// a concurrent assertion's process runs in the Observed region, a
        /// program's in the Reactive region. Such threads run from the
        /// hosted region lists (ready_thread), outside the kernel's rounds.
        SchedulerPhase phase { SchedulerPhase::active };
        /// Runs assertion operations (sampled reads, SvaOperation) or leaves
        /// the Active region (WaitRegion): it runs on the operation tier.
        bool assertion { };
        /// A non-final process of a program (program_members_), and whether
        /// its thread has ended (the reference's halted program process).
        std::optional<std::uint32_t> program_owner;
        bool program_ended { };
        /// Dead logic: never scheduled; materialize evaluates it from the
        /// slots in lazy_inputs (its whole sensitivity) when a slot it
        /// writes is read. lazy_stamp is the lazy_epoch_ of its last run.
        bool lazy { };
        std::vector<std::uint32_t> lazy_inputs;
        std::uint64_t lazy_stamp { };
        /// VHDL delta collapsing (plan_delta_collapse): the planner admitted
        /// the member; collapse_inputs are its sensitivity slots (they cover
        /// every read) and collapse_outputs the slots it drives.
        bool collapse { };
        std::vector<std::uint32_t> collapse_inputs;
        std::vector<std::uint32_t> collapse_outputs;
        /// A port-association conversion (StaticKernelMemberSpec::
        /// conversion_target): never scheduled; run_conversions runs it in
        /// the update phase that changes one of its sources.
        bool conversion { };
        std::uint32_t body_begin { };
        std::uint32_t body_end { };
        std::uint32_t exit_alt { static_kernel_detail::no_slot };
        std::uint32_t level { };
        /// The member's operations (operations()). A member restored from
        /// the kernel image without changes to its program's copies them
        /// from operations_source's process table on first use.
        mutable std::vector<Operation> operations_;
        mutable const Impl* operations_source { };
        /// build_fresh inlined the calls of the body (static_kernel_inline_calls).
        bool inlined_calls { };
        [[nodiscard]] std::vector<Operation>& operations()
        {
            if (operations_source != nullptr) [[unlikely]] {
                materialize_operations();
            }
            return operations_;
        }
        [[nodiscard]] const std::vector<Operation>& operations() const
        {
            if (operations_source != nullptr) [[unlikely]] {
                materialize_operations();
            }
            return operations_;
        }
        void materialize_operations() const;
        std::vector<PackedLogic4> registers;
        /// Register value kinds (VHDL delta mode; empty means Logic4).
        std::vector<ValueKind> register_kinds;
        std::vector<std::uint32_t> container_registers;
        /// Per container register: the private storage a ReadContainerObject
        /// binding copies its memory into (a register the member writes
        /// elements of), or no_container; empty when there is none.
        std::vector<std::uint32_t> copy_storage;
        /// Per container register: its reference-format value (a dynamic
        /// array, queue, associative array or other shape the arena does
        /// not hold), copy-on-write as the reference's register file keeps
        /// it; null for the kernel's arrays. Empty when there is none.
        std::vector<SharedContainerValue> dynamic_containers;
        [[nodiscard]] bool dynamic_container(const ContainerRegisterId id) const
        {
            return id < dynamic_containers.size() && dynamic_containers[id];
        }
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
            /// Per container_ids entry: the saved reference-format value,
            /// or null.
            std::vector<SharedContainerValue> dynamic;
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
        /// Threads of this member can hold an automatic callable frame
        /// across a suspension (static_kernel_fork_holds_suspended_frame):
        /// each thread keeps its callable contexts (Thread::contexts), and
        /// the member runs on the operation tier.
        bool thread_contexts { };
        /// A Fork gives its children copies of the register frame
        /// (isolate_register_frame: an assertion attempt's locals): each
        /// such child keeps its own frame (Thread::isolated), and the member
        /// runs on the operation tier, whose registers are the frame.
        bool isolated_forks { };
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
        /// Slots with many ranged readers index them by bit chunk of
        /// 1 << chunk_shift bits: chunk c holds the ascending reader indices
        /// chunk_readers[chunk_begin[c], chunk_begin[c + 1]). Empty
        /// otherwise; reader_group is each indexed reader's group id.
        std::uint32_t chunk_shift { };
        std::vector<std::uint32_t> chunk_begin;
        std::vector<std::uint32_t> chunk_readers;
        std::vector<std::uint32_t> reader_group;
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
        /// The first writer; the run's last one is last_writer_[slot].
        ProcessId last_writer { };
        /// Writes record their member's process in last_writer_: the slot has
        /// several writer processes and publish and materialize commit its
        /// value as the last one's (their regions overlap or are not known;
        /// disjoint writers commit each their own region).
        bool tracks_writer { };
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
        /// The time and delta of a host deposit (Interpreter::deposit_signal,
        /// generic origin): the host wakes SystemVerilog readers in the
        /// next delta, so a scan of that delta leaves the change for later.
        std::optional<std::pair<SimulationTick, std::uint64_t>> deposited;
        /// The host's transaction stamp of the signal at the last scan, for
        /// threads that wait on its transactions (transaction_inputs_).
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction_seen;
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
    /// Builds the slot's chunk index of its ranged readers.
    static void index_ranged_readers(Slot& slot);
    [[nodiscard]] Logic4 slot_edge_bit(std::uint32_t slot) const noexcept;
    void store_slot_bits(std::uint32_t slot, const PackedLogic4& value,
        std::uint32_t offset);
    /// The VHDL update phase: commit_writes, then the collapsed members its
    /// changes woke, each set of them committed in turn (run_collapsed).
    void commit_round();
    /// Applies the queued VHDL assignments and reports the changed slots;
    /// then runs the conversions the changes queued (run_conversions).
    void commit_writes();
    void run_collapsed();
    /// Port-association conversions (IEEE 1076-2008 14.7.3.3), as the
    /// host's update commit runs them (drain_vhdl_resolutions): first in,
    /// first out, each committed before the next runs and each at most once
    /// per update phase.
    void run_conversions();
    /// The conversions' initial values (14.7.5.2), with the wires', before
    /// any activation: no events; the host's copies of the formals follow.
    void settle_conversions();
    /// Each slot's conversion members, in the host's dependents order.
    std::vector<std::vector<std::uint32_t>> slot_conversions_;
    std::vector<std::uint32_t> conversion_members_;
    std::vector<std::uint32_t> conversion_queue_;
    std::vector<std::uint8_t> conversion_queued_;
    bool running_conversions_ { };
    /// settle_conversions: commit_writes counts changed slots instead of
    /// reporting them.
    bool quiet_commit_ { };
    std::uint32_t quiet_changes_ { };
    /// FSIM_PROFILE_KERNEL: commit_writes' writes, unchanged untouched
    /// writes, calls and touched slots.
    std::array<std::uint64_t, 4> profile_commit_writes_ { };
    /// Keeps the planner's collapsed members whose compiled code is pure,
    /// closes the set under the reader rule and levels it.
    void plan_delta_collapse();
    /// Why a collapsed member's compiled code is not pure (null when it
    /// is); rejected_op names an operation it cannot run.
    [[nodiscard]] const char* collapse_impurity(const Member& member,
        static_kernel_detail::KOp& rejected_op) const;
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
    /// A behavioral member's register, from its compiled body when it runs
    /// compiled; and its write, kept coherent with that body.
    [[nodiscard]] PackedLogic4 member_register(std::uint32_t member, RegisterId reg);
    void set_member_register(std::uint32_t member, RegisterId reg,
        const PackedLogic4& value);
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
    /// Queues the nonblocking write of element `ordinal` of kernel array
    /// `target`: a whole-array `a <= b` lands element by element in the NBA
    /// region.
    void push_container_element(std::uint32_t target, std::uint32_t ordinal,
        PackedLogic4 value, std::uint32_t member, InstructionIndex instruction);
    /// A whole-array copy between private containers of one type.
    void copy_container(std::uint32_t destination, std::uint32_t source,
        std::uint32_t member, InstructionIndex instruction);
    void write_container_element_word(Container& container,
        std::size_t ordinal, Word value, std::uint32_t member);
    void write_host(std::uint32_t member, SignalId signal, PackedLogic4 value,
        std::optional<std::uint32_t> offset, bool blocking,
        SignalUpdateDomain domain);
    /// A VHDL signal assignment to a host signal: the member's driver of it
    /// is the host's projected driver (IEEE 1076-2019 10.5.2.2, 14.7.2).
    void write_host_projected(std::uint32_t member, SignalId signal,
        const std::vector<ProjectedWaveformValue>& elements,
        std::optional<std::size_t> offset, SimulationTick rejection,
        ProjectedDelayMode mode);

    // Scheduling.
    void schedule(std::uint32_t member);
    /// Schedules an edge reader: a member, or a tagged dispatch group.
    void schedule_edge(std::uint32_t id);
    void schedule_target(const ScheduleTarget& target);
    /// schedule_target for the leader thread without profiling: returns
    /// early for a target that is already pending.
    void schedule_target_serial(const ScheduleTarget& target);
    void enqueue(std::uint32_t level, std::uint32_t entry);
    void build_schedule_targets();
    void build_dispatch_groups();
    /// Runs the triggered sequential members in index order.
    void run_triggered();
    void run_triggered_round();
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
        const std::unordered_map<std::uint32_t, std::uint32_t>& slot_of_offset,
        std::vector<std::uint32_t>& register_of_slot);
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
    /// Wide-register instructions (KOp::wide_load .. wide_part_insert) on
    /// the register file `registers`, with narrow operands y (its 'U' mask
    /// y_unknown) and z; `resolved` is the slot of a load or store.
    void execute_wide(const static_kernel_detail::CompiledBody& body,
        Word* registers, const static_kernel_detail::KInst& inst,
        std::uint32_t at, std::uint32_t member_index, std::uint32_t body_begin,
        Word y, std::uint64_t y_unknown, Word z, std::uint32_t resolved);
    /// fsim_sk_wide_<op>: a wide-register instruction, or a selection from a
    /// wide register (sub 3), with the caller's register file.
    template <static_kernel_detail::KOp Op>
    static void native_wide(StaticKernelNativeFrame* frame, Word* registers,
        std::uint32_t at, std::uint64_t xa, std::uint64_t xb, std::uint64_t ya,
        std::uint64_t yb, std::uint64_t za, std::uint64_t zb,
        std::uint64_t* out);
    /// The value of wide register `reg` (storage CompiledBody::wide_base)
    /// as the reference holds it, and back (false: a value with codes the
    /// registers cannot hold, other than 0, 1, X, Z and 'U').
    [[nodiscard]] static PackedLogic4 wide_value(const CompiledBody& body,
        const Word* registers, std::uint32_t reg, ValueKind kind);
    [[nodiscard]] static bool set_wide_value(const CompiledBody& body,
        Word* registers, std::uint32_t reg, const PackedLogic4& value);
    static void native_fail(StaticKernelNativeFrame* frame, std::uint32_t at,
        std::uint32_t reason);
    void compile_members();
    /// The constructor's work up to the native code; `stage` times its
    /// steps (FSIM_PROFILE_PHASES).
    void build_fresh(StaticKernelRuntimeSpec& spec,
        const std::function<void(const char*)>& stage);
    /// The native view of containers_ (container_info_, container_elements_).
    void build_container_info();
    /// A port-association conversion's resolver program as a member body
    /// (build_fresh).
    static void rewrite_conversion(Member& member, SignalId target,
        RegisterId output);
    /// Conversion members per source slot and wired formals, and the host
    /// resolvers they replace (erased from the Impl), after the members and
    /// mirrors exist; the wired formals start at their sources' values.
    void plan_conversions_and_wires(const StaticKernelRuntimeSpec& spec);
    /// Kernel images (StaticKernelRuntimeSpec::kernel_image;
    /// simir_static_kernel_image.cpp). image_scope: the kernel may record or
    /// restore one. record_image: the image of the state build_fresh made,
    /// false outside the scope. restore_image: that state from
    /// spec.kernel_image, false (and the state as before) when there is
    /// none, it is out of scope, or it does not fit this specification.
    [[nodiscard]] bool image_scope(const StaticKernelRuntimeSpec& spec) const;
    [[nodiscard]] bool record_image(
        const StaticKernelRuntimeSpec& spec, std::string& out) const;
    [[nodiscard]] bool restore_image(StaticKernelRuntimeSpec& spec);
    [[nodiscard]] static std::uint64_t image_spec_digest(
        const StaticKernelRuntimeSpec& spec);
    /// FSIM_PROFILE_PHASES: `static-kernel-image ACTION=OUTCOME bytes=N`.
    static void note_image(const char* action, const char* outcome,
        std::size_t bytes);
    /// Calls visit(field) for each field a kernel image holds, in order.
    template <typename Self, typename Visit>
    static void visit_image_fields(Self& self, Visit&& visit);
    class ImageWriter;
    class ImageReader;
    /// Storage of restored not_compiled_reason_ texts.
    std::vector<std::string> image_reasons_;
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
        /// A collapsed member (plan_delta_collapse): woken during an update
        /// phase it runs in that phase, at collapse_level.
        bool collapse { };
        bool collapse_queued { };
        std::uint32_t collapse_level { };
        /// Position in its level's queue (LevelQueue::entries).
        std::uint32_t queue_slot { static_kernel_detail::no_slot };
    };
    /// Whole-slot targets [begin, end) in notify_targets_. `general` marks
    /// a slot that also has ranged readers, an output, edges, waiters or
    /// which slot_changed handles from the full Slot record. `tracks` marks
    /// a slot whose writes record their process (Slot::tracks_writer).
    struct SlotNotify {
        std::uint32_t begin { };
        std::uint32_t end { };
        bool general { };
        bool tracks { };
    };
    /// Per slot, the process of its last write where Slot::tracks_writer
    /// (publish and materialize commit the value as that process's).
    std::vector<ProcessId> last_writer_;
    std::vector<MemberSchedule> member_schedule_;
    std::vector<std::uint8_t> partition_queued_;
    std::vector<std::uint32_t> partition_level_;
    std::vector<std::uint32_t> partition_queue_slot_;
    std::vector<SlotNotify> slot_notify_;
    /// StaticKernelNativeFrame::fast_notify: two words per slot, the
    /// partition of its one whole-slot target (when nothing else needs the
    /// notify helper) and the target's earliest reader position.
    std::vector<std::uint32_t> fast_notify_;
    /// Gives generated code the fast_notify table where the notify helper
    /// would take its leader fast path (StaticKernelNativeFrame::fast_notify).
    void bind_fast_notify(StaticKernelNativeFrame& frame) noexcept
    {
        if (worker_lane_ == nullptr && !profile_ && !committing_round_) {
            frame.fast_notify = fast_notify_.data();
            frame.partition_queued = partition_queued_.data();
            frame.running_partition = scratch_.running_partition;
        }
        bind_write_marks(frame);
    }
    /// Gives generated code the write marks where its writes go to writes_
    /// (StaticKernelNativeFrame::write_marks).
    void bind_write_marks(StaticKernelNativeFrame& frame) noexcept
    {
        if (worker_lane_ == nullptr && !write_marks_.empty()) {
            frame.write_round = write_round_;
            frame.write_marks = write_marks_.data();
        }
    }
    /// StaticKernelNativeFrame::write_marks: VHDL kernels without a worker
    /// pool. Every write queued in the leader's writes_ marks its slot.
    std::vector<std::uint32_t> write_marks_;
    std::uint32_t write_round_ { 1U };
    void mark_write(const std::uint32_t slot) noexcept
    {
        if (slot < write_marks_.size()) {
            write_marks_[slot] = write_round_;
        }
    }

    /// Behavioral tier. A thread is one resumable execution context of a
    /// behavioral member; fork children are threads of the same member and
    /// share its registers.
    /// One activation of an automatic callable (IEEE 1800-2023 13.4.2): the
    /// values of its packed and string registers while the threads inside it
    /// are suspended. Once a fork runs inside it, the activation escapes:
    /// the fork's children share it with their parent (6.21, 9.3.2), as the
    /// reference's escaping callable contexts.
    struct CallableContext {
        std::uint32_t identity { };
        std::vector<RegisterId> ids;
        std::vector<PackedLogic4> values;
        std::vector<StringRegisterId> string_ids;
        std::vector<std::string> strings;
        /// Private arrays of the activation (arguments and locals) and their
        /// arena words; registers bound to design arrays are not saved.
        std::vector<ContainerRegisterId> container_ids;
        std::vector<std::vector<std::uint64_t>> arrays;
        /// Per container_ids entry: the reference-format value, or null.
        std::vector<SharedContainerValue> dynamic;
        bool stored { };
        bool escaping { };
    };
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
        /// A fork child's Fork operation (the reference's fork_site), for
        /// disable (IEEE 1800-2023 9.6.2).
        std::uint32_t fork_site { static_kernel_detail::no_slot };
        /// thread_contexts members: the callable activations the thread is
        /// inside (the escaping ones inherited from its forking parent, then
        /// its own frames, innermost last); stored when it parks, loaded
        /// when it resumes.
        std::vector<std::shared_ptr<CallableContext>> contexts;
        /// A child of an isolating Fork: its own copy of the member's whole
        /// register frame, loaded while it runs (the frame the other threads
        /// share is set aside meanwhile) and stored when it parks.
        std::shared_ptr<CallableContext> isolated;
        /// Live children; the parent of a join-all fork resumes at zero.
        std::uint32_t children { };
        /// Fork children whose records exist (alive, or ended with
        /// descendants of their own): an ended thread keeps its record while
        /// this is non-zero, as the reference keeps a halted process's, so
        /// a disable fork reaches every descendant (IEEE 1800-2023 9.6.3).
        std::uint32_t child_records { };
        /// Ended, its record kept for its descendants (child_records).
        bool ended_record { };
        /// Suspended in wait fork (9.6.1) until its children end.
        bool waiting_children { };
        /// A pending update (a clocking drive with a cycle delay, Fork's
        /// detached), which disable fork and wait fork leave alone (IEEE
        /// 1800-2023 9.6.1, 9.6.3).
        bool detached { };
        /// The process generation of its handle (IEEE 1800-2023 9.7): the
        /// member process's for a root thread, else the reference's next
        /// one, as Interpreter::spawn_fork numbers its children.
        std::uint32_t generation { };
        /// Its handle was taken (process::self): it has a ProcessRecord.
        bool handle { };
        /// Suspended through its handle (IEEE 1800-2023 9.7): a wake that
        /// reaches it is held until resume (suspended_wake), as is its run
        /// when it was running or queued (suspended_running).
        bool suspended { };
        bool suspended_wake { };
        bool suspended_running { };
        /// A fork child's random stream (IEEE 1800-2023 18.14: each thread
        /// has its own), seeded as the reference seeds the child process.
        std::uint64_t random_state { };
        /// Bumped whenever the thread resumes, so stale waiter and timer
        /// entries from an earlier wait are ignored.
        std::uint64_t epoch { };
        bool alive { };
        bool queued { };
        /// Killed while a ready list still names it: the slot is freed when
        /// that entry is taken (take_ready), never reused before.
        bool reclaim { };
        /// VHDL wait with timeout (IEEE 1076-2019 10.2): the absolute
        /// deadline of the current wait statement (kept across the re-waits
        /// of a wait-until condition) and the register that receives
        /// whether the resumption was the timeout.
        std::optional<SimulationTick> deadline;
        std::optional<RegisterId> timeout_result;
        /// Mailbox values delivered while it waited (IEEE 1800-2023 15.4),
        /// written to its registers when it resumes.
        struct DeliveredResult {
            std::optional<RegisterId> destination;
            std::optional<StringRegisterId> string_destination;
            PackedLogic4 value;
            std::string text;
        };
        std::vector<DeliveredResult> delivered;
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
    /// A delay's resumption. Those due at one time resume in their
    /// threads' stable order, first-queued first among equal orders, as the
    /// reference queues each process's timeout at its own order (IEEE
    /// 1800-2023 4.7).
    struct Timer {
        SimulationTick time { };
        ProcessId order { };
        std::uint64_t sequence { };
        std::uint32_t thread { };
        std::uint64_t epoch { };
        friend bool operator>(const Timer& left, const Timer& right)
        {
            if (left.time != right.time) {
                return left.time > right.time;
            }
            return left.order != right.order ? left.order > right.order
                                              : left.sequence > right.sequence;
        }
    };
    std::vector<Thread> threads_;
    std::vector<std::uint32_t> free_threads_;
    /// Fork children by process number (Thread::order).
    std::unordered_map<ProcessId, std::uint32_t> child_threads_;
    std::vector<ForkGroup> fork_groups_;
    std::vector<std::uint32_t> free_fork_groups_;
    std::vector<std::uint32_t> ready_threads_;
    std::vector<std::uint32_t> ready_threads_spare_;
    std::vector<std::uint32_t> inactive_threads_;
    std::vector<std::vector<Waiter>> slot_waiters_;
    std::vector<std::vector<Waiter>> input_waiters_;
    /// WaitOn's sensitivity list, rebuilt per suspension in place (wait_on
    /// copies out of it).
    std::vector<Sensitivity> wait_entries_;
    /// Inputs a thread waits on for transactions (an implicit signal's
    /// driver, IEEE 1076-2019 16.2.4): a scan wakes those waiters when the
    /// host's transaction stamp moved, whether or not the value changed.
    std::vector<std::uint32_t> transaction_inputs_;
    /// The members that are unidirectional switches (MOS gates), by process
    /// (wake_switch_reader).
    std::unordered_map<ProcessId, std::uint32_t> switch_members_;
    /// Switch members the host woke, with the time and delta of the wake:
    /// a scan of a later delta resumes them.
    struct SwitchWake {
        std::uint32_t member { };
        SimulationTick time { };
        std::uint64_t delta { };
    };
    std::vector<SwitchWake> switch_wakes_;
    void resume_switch_readers();
    /// Threads that yielded (Yield: the reference's queue_next_delta),
    /// with the time and delta they yielded in: a scan of a later delta
    /// resumes them.
    struct YieldWake {
        std::uint32_t thread { };
        std::uint64_t epoch { };
        SimulationTick time { };
        std::uint64_t delta { };
    };
    std::vector<YieldWake> yield_wakes_;
    void resume_yielded();
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
    std::vector<std::uint32_t> vhdl_ready_threads_spare_;
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
    /// Collapsed members exist (plan_delta_collapse); collapsing_ while an
    /// update phase after time zero runs, when their wakes go to
    /// collapse_buckets_ (by level) instead of the next round.
    bool collapse_enabled_ { };
    bool collapsing_ { };
    std::vector<std::vector<std::uint32_t>> collapse_buckets_;
    std::vector<std::uint32_t> collapse_batch_;
    std::size_t collapse_pending_ { };
    std::uint32_t collapse_low_ { };
    /// While run_collapsed commits a level, the lowest level its changes
    /// may wake (the planner orders every reader above its writers).
    std::uint32_t collapse_floor_ { };
    /// FSIM_KERNEL_VERIFY's reference replay of a compiled run
    /// (verify_run): its reports repeat the compiled run's and are not
    /// emitted again.
    bool replaying_ { };
    /// What plan_delta_collapse decided: per member its level + 1 (0 when
    /// not collapsed), and the counts FSIM_PROFILE_PHASES reports. A kernel
    /// image holds it after build_fresh's state; restore_image keeps it in
    /// restored_collapse_, and a recorded image gets it appended
    /// (recording_image_, set while the constructor records one).
    struct CollapsePlan {
        std::uint32_t planned { };
        std::uint32_t kept { };
        std::uint32_t levels { };
        bool allowed { };
        std::map<std::string, std::size_t> impurities;
        std::vector<std::uint32_t> member_levels;
    };
    std::optional<CollapsePlan> restored_collapse_;
    std::shared_ptr<std::string> recording_image_;
    /// What the constructor's scan of the members' operations found. A
    /// recorded image holds it before the collapse plan, so a restored
    /// kernel need not read the operations (Member::operations_source).
    struct HostScan {
        bool host_written { };
        bool host_delayed_writes { };
        bool vhdl_host_cycles { };
    };
    std::optional<HostScan> restored_host_scan_;
    void record_host_scan(const HostScan& scan);
    /// plan_delta_collapse's parts: deciding (the analysis), applying, and
    /// appending the plan to the image being recorded.
    [[nodiscard]] CollapsePlan decide_delta_collapse();
    void apply_delta_collapse(const CollapsePlan& plan);
    void record_collapse_plan(const CollapsePlan& plan);
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

    /// redirect: the thread continues at redirect_pc_ (a disabled block's
    /// end); killed: a disable ended the running thread itself.
    enum class Boundary : std::uint8_t { none, advance, suspend, finish, redirect,
        killed };
    std::uint32_t redirect_pc_ { };
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
    /// A member's registers and blocking writes as the reference's file
    /// operations (run_file) use them.
    struct MemberFileStorage;
    struct MemberContainerAccess;
    struct MemberRandomizeStorage;
    struct MemberVhdlApiStorage;
    struct MemberAssertionStorage;
    /// A VHDL-2019 member's report or assertion violation (Impl::report_vhdl)
    /// at the kernel's time.
    void report_vhdl(std::uint32_t member, std::uint32_t pc,
        std::string_view message, AssertionSeverity severity,
        const SourceLocation& source, bool standalone);
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
    /// Runs the declaration initializers that are thread members, before
    /// the once members and threads of time zero.
    void run_behavioral_initializers();
    void start_behavioral();
    [[nodiscard]] std::uint32_t spawn_thread(std::uint32_t member,
        std::uint32_t pc, std::uint32_t parent, std::uint32_t group,
        std::uint32_t fork_site = static_kernel_detail::no_slot,
        bool isolated = false);
    /// A thread taken from a ready list: false when it is not alive (a
    /// killed thread's slot is freed here).
    [[nodiscard]] bool take_ready(std::uint32_t thread);
    /// disable of a named block of the running thread's process (IEEE
    /// 1800-2023 9.6.2), as Interpreter::Impl::handle_fork_boundary.
    [[nodiscard]] Boundary disable_block(std::uint32_t thread, std::uint32_t pc,
        const DisableBlock& op);
    /// Interpreter::Impl::disable_process_interval on kernel threads; the
    /// running thread's position is `running_pc`.
    /// With `include_end`, an owner waiting right at the block's end is
    /// inside it too (a block of another process).
    void disable_thread_interval(std::uint32_t running, std::uint32_t running_pc,
        std::uint32_t owner, std::uint32_t begin, std::uint32_t end,
        std::optional<std::uint32_t>& running_redirect, bool include_end = false);
    /// disable of a block or task named by hierarchical name (IEEE 1800-2023
    /// 9.6.2), as Interpreter::Impl::disable_named_scope: the first name
    /// that a member's program declares ends that interval in each of the
    /// member's threads. The planner keeps every process declaring a
    /// disabled name a member when the disabling process is one.
    [[nodiscard]] Boundary disable_named_scope(std::uint32_t thread,
        std::uint32_t pc, const std::vector<std::string>& scopes);
    /// A named block_entry..block_exit interval of a member's program.
    struct ScopeInterval {
        std::uint32_t member { };
        std::uint32_t begin { };
        std::uint32_t end { };
    };
    /// A member disables a block named by hierarchical name: compiled
    /// members resume at their block ends.
    bool named_disable_ { };
    /// The members' named intervals by scope name, built at the first
    /// named disable.
    std::optional<std::unordered_map<std::string, std::vector<ScopeInterval>>>
        named_scopes_;
    /// The live fork children of a thread, in process order.
    [[nodiscard]] std::vector<std::uint32_t> live_children(std::uint32_t thread) const;
    /// The process a member's operation runs for (class services, report
    /// process): the running fork child's own number, else the member's.
    [[nodiscard]] ProcessId running_process(std::uint32_t member) const;
    /// A thread whose handle was taken (IEEE 1800-2023 9.7), kept after it
    /// ends as the reference keeps a halted process's state: its status
    /// and random state, and the threads awaiting it.
    struct ProcessRecord {
        std::uint32_t generation { };
        std::uint32_t thread { static_kernel_detail::no_slot };
        bool ended { };
        ProcessStatus status { ProcessStatus::running };
        std::uint64_t random_state { };
        std::vector<std::pair<std::uint32_t, std::uint64_t>> awaiting;
    };
    std::unordered_map<ProcessId, ProcessRecord> process_records_;
    /// A kernel thread blocked in a synchronization operation, by its
    /// process order (Impl::SynchronizationClient::wait).
    struct SynchronizationWait {
        std::uint32_t thread { static_kernel_detail::no_slot };
        std::uint64_t epoch { };
        std::optional<std::uint64_t> dynamic_event;
    };
    std::unordered_map<ProcessId, SynchronizationWait> synchronization_waits_;
    /// The input took a host deposit in this delta (host_deposit): its
    /// readers see it from the next one.
    [[nodiscard]] bool deposited_now(const Input& input) const;
    struct MemberSynchronizationClient;
    /// A member register as the thread runner holds it (compiled body or
    /// reference register file).
    [[nodiscard]] PackedLogic4 thread_register(std::uint32_t member,
        std::uint32_t pc, RegisterId reg) const;
    void set_thread_register(std::uint32_t member, std::uint32_t pc,
        RegisterId reg, const PackedLogic4& value);
    /// The running thread's handle (process::self), recording it.
    [[nodiscard]] std::uint64_t take_handle(std::uint32_t thread);
    /// The record a handle register names; fails as the reference's
    /// process_from_handle does.
    [[nodiscard]] std::pair<ProcessId, ProcessRecord*> handle_record(
        std::uint32_t member, std::uint32_t pc, const PackedLogic4& handle);
    [[nodiscard]] ProcessStatus record_status(const ProcessRecord& record) const;
    /// The random state a record's process draws from.
    [[nodiscard]] std::uint64_t& record_random_state(ProcessId id,
        ProcessRecord& record);
    /// A thread with a record ends: the record keeps its status and wakes
    /// the threads awaiting it.
    void end_record(std::uint32_t thread, ProcessStatus status);
    /// The next draw of the stream RandomValue draws from: a running fork
    /// child's own, else the member process's (IEEE 1800-2023 18.14).
    [[nodiscard]] std::uint32_t next_member_random(std::uint32_t member);
    /// Whether every live child is detached (none counts).
    [[nodiscard]] bool only_detached_children(std::uint32_t thread) const;
    /// The fork children with records, live or ended (Thread::child_records),
    /// in stable order.
    [[nodiscard]] std::vector<std::uint32_t> child_records(std::uint32_t thread) const;
    void cancel_thread_descendants(std::uint32_t thread);
    /// disable fork (IEEE 1800-2023 9.6.3) of one fork site.
    void disable_fork_site(std::uint32_t thread, std::uint32_t site);
    /// Ends a fork child as killed (complete_fork_child): its fork group
    /// counts it, its waits lapse.
    void kill_thread(std::uint32_t thread);
    void release_thread(std::uint32_t thread);
    /// Copies the running thread's innermost activation of each callable
    /// between the member's registers and its contexts (load: only the
    /// escaping ones when `escaping_only`).
    void store_contexts(std::uint32_t thread);
    void load_contexts(std::uint32_t thread, bool escaping_only = false);
    void store_context(const Member& member, CallableContext& context);
    void load_context(Member& member, const CallableContext& context);
    void free_thread_slot(std::uint32_t index);
    [[nodiscard]] std::optional<std::uint32_t> child_thread(ProcessId process) const;
public:
    /// A live (or recorded) fork child's design process and the child of
    /// its member's thread it descends from (Interpreter::design_process,
    /// dynamic_process_root); nothing for another process number.
    [[nodiscard]] std::optional<ProcessId> child_design_process(
        ProcessId process) const;
    [[nodiscard]] std::optional<ProcessId> child_dynamic_root(
        ProcessId process) const;
    /// Interpreter::kill_dynamic_processes for the kernel's fork children.
    void kill_member_children(std::span<const ProcessId> design_processes);
private:
    /// A context covering every register, string and private array of the
    /// member, holding their current values.
    [[nodiscard]] CallableContext whole_frame(const Member& member);
    void ready_thread(std::uint32_t thread);
    /// Resumes a thread in a later region of this time step (WaitRegion),
    /// or in its member's region (ready_thread), through the hosted region
    /// lists.
    void ready_thread_in(std::uint32_t thread, SchedulerPhase phase);
    [[nodiscard]] bool run_ready_threads();
    void run_thread(std::uint32_t thread);
    void wait_on(std::uint32_t thread, std::span<const Sensitivity> entries);
    void finish_thread(std::uint32_t thread);
    /// A program's members (IEEE 1800-2023 24.7), as the reference's
    /// complete_program_process and exit_program: the program ends when the
    /// last of its members' threads ends, or at $exit, which ends them all;
    /// the simulation ends with the last program.
    void complete_program_member(std::uint32_t member);
    /// A member's blocking write of a host object changed the signals its
    /// elements alias: like a blocking write of a host signal (write_host),
    /// it is visible to the kernel's readers at once (IEEE 1800-2023
    /// 10.4.1).
    void host_object_written();
    void exit_program(std::uint32_t owner);
    /// The non-final members of each program, in process order.
    std::map<std::uint32_t, std::vector<std::uint32_t>> program_members_;
    /// A fork child of `parent` in fork group `group` ended: the parent
    /// resumes as its join requires.
    void fork_child_done(std::uint32_t parent, std::uint32_t group);
    void wake_waiters(std::vector<Waiter>& waiters, const WaitChange& change);
    /// Wakes the transaction waiters of the inputs whose host transaction
    /// stamp moved since the last scan.
    void wake_transaction_waiters();
    [[nodiscard]] bool waiter_range_changed(const Waiter& waiter,
        const WaitChange& change) const;
    void collect_timers();
    void arm_timer();
    void behavioral_output(std::uint32_t member, std::string_view text,
        bool newline);
    [[nodiscard]] std::string format_display_text(const FormatDisplay& op,
        const PackedLogic4& value) const;
    [[nodiscard]] std::string time_display_text(const TimeDisplay& op) const;
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
    std::vector<std::uint32_t> lazy_pending_;
    std::uint32_t lazy_threshold_ { };
    /// Instructions the last execute_body run interpreted.
    std::size_t executed_steps_ { };
    StaticKernelNativeHelpers native_helpers_;
    std::size_t native_units_ { };
    /// What native code running on one thread reads and writes besides the
    /// arena and its unit's registers: the leader's is scratch_, a worker's
    /// lives in its lane (multithread proposal §2.3).
    struct RunScratch {
        std::uint32_t running_position { };
        /// The combinational member settle() is running, and the partition
        /// running (no_slot otherwise).
        std::uint32_t running_member { static_kernel_detail::no_slot };
        std::uint32_t running_partition { static_kernel_detail::no_slot };
        /// slot_changed_general's indexed readers of a change spanning chunks.
        std::vector<std::uint32_t> ranged_candidates;
        /// The native code running now is two-state.
        bool two_state_running { };
        /// 'U' mask of the last slow-path result (U-aware instructions).
        std::uint64_t last_unknown { };
        /// The register file sub 3 selections read (evaluate_slow_op).
        const Word* wide_file { };
        std::exception_ptr native_exception;
        std::optional<static_kernel_detail::KernelDeopt> pending_deopt;
        /// KOp::wide_move value planes.
        std::vector<std::uint64_t> wide_move;
        /// KOp::wide_concat result (aval, bval and 'U' words).
        std::vector<std::uint64_t> wide_concat;
        /// The operand registers of the generic instruction running.
        std::vector<RegisterId> generic_reads;
    };
    /// Output a worker's unit made, which the leader emits in the replay at
    /// the unit's place in the serial order: text, a %t argument (formatted
    /// at the replay, after any earlier $timeformat), a $time display, or a
    /// severity message (the member's Report at `pc`).
    struct LaneOutput {
        enum class Kind : std::uint8_t { text, format, time, report };
        Kind kind { Kind::text };
        bool newline { };
        std::uint32_t member { };
        std::uint32_t pc { };
        std::string text;
        PackedLogic4 value;
    };
    void emit_lane_output(const LaneOutput& output);
    /// A change notification made on a worker; the leader replays it.
    struct LaneChange {
        std::uint64_t changed { };
        std::uint32_t slot { };
        std::uint32_t field_offset { };
        std::uint32_t position { };
        /// Where Lane::words holds the slot's words before and after the
        /// change, for notifications that pass the old words; else no_slot.
        std::uint32_t words { static_kernel_detail::no_slot };
        /// The slot's edge bit right after the change, plus one (0: the slot
        /// is not waitable).
        std::uint8_t edge { };
    };
    /// One thread's share of a parallel batch: its run state and the
    /// effects its units logged instead of applying.
    struct alignas(64) Lane {
        RunScratch scratch;
        std::vector<LaneChange> changes;
        std::vector<std::uint64_t> words;
        std::vector<StaticKernelWrite> write_storage;
        StaticKernelWriteQueue writes;
        std::vector<NbaEntry> elements;
        std::vector<Pending> generic;
        std::vector<LaneOutput> outputs;
        /// The domain this lane is settling (no_slot: a level or triggered
        /// batch, whose units schedule nothing).
        std::uint32_t domain { static_kernel_detail::no_slot };
        /// A triggered batch: the writes the lane queued to domains'
        /// private queues, and those domains (bit per domain below 64;
        /// private_more: others).
        std::uint32_t private_count { };
        std::uint64_t private_domains { };
        bool private_more { };
        /// The batch items it ran, as it left them: 1 serial (the leader
        /// runs it), 2 something to replay.
        std::vector<std::uint8_t> item_state;
        /// The dispatch (lane_epoch_) its logs belong to.
        std::uint64_t epoch { };
    };
    /// A unit of a parallel batch: where its records are and how its native
    /// run ended (a nonzero status is finished by the leader in the replay).
    struct alignas(64) LaneRun {
        std::uint32_t lane { };
        std::uint32_t changes_begin { };
        std::uint32_t changes_end { };
        std::uint32_t writes_begin { };
        std::uint32_t writes_end { };
        std::uint32_t outputs_begin { };
        std::uint32_t outputs_end { };
        /// Not eligible: the leader runs it at its place in the replay.
        bool serial { };
        /// A batch item with something for the leader's replay: logged
        /// changes or writes, or a status to finish.
        bool effects { };
        std::uint32_t status { };
        StaticKernelNativeFrame frame;
        std::optional<static_kernel_detail::KernelDeopt> deopt;
        std::exception_ptr exception;
    };
    RunScratch scratch_;
    /// The lane of the parallel batch this thread is running; null outside
    /// one (the leader's serial path).
    FSIM_STATIC_KERNEL_TLS static thread_local Lane* worker_lane_;
    /// Counts the pool's dispatches: a lane clears its logs when it first
    /// runs in one (the leader does not touch the workers' lines).
    std::uint64_t lane_epoch_ { };
    void prepare_lane(Lane& lane) noexcept
    {
        if (lane.epoch != lane_epoch_) {
            lane.epoch = lane_epoch_;
            lane.changes.clear();
            lane.words.clear();
            lane.elements.clear();
            lane.generic.clear();
            lane.outputs.clear();
            lane.writes.count = 0U;
            lane.private_count = 0U;
            lane.private_domains = 0U;
            lane.private_more = false;
        }
    }
    [[nodiscard]] RunScratch& scratch() noexcept
    {
        auto* const lane = worker_lane_;
        return lane != nullptr ? lane->scratch : scratch_;
    }
    /// Where a run's queued writes go: the lane's in a parallel batch.
    [[nodiscard]] StaticKernelWriteQueue& write_queue() noexcept
    {
        auto* const lane = worker_lane_;
        return lane != nullptr ? lane->writes : writes_;
    }
    std::unique_ptr<static_kernel_detail::KernelPool> pool_;
    std::vector<Lane> lanes_;
    std::vector<LaneRun> lane_runs_;
    /// The level snapshot settle() is running, in canonical order.
    std::vector<std::uint32_t> batch_;
    /// Per unit (members by index, then partitions): may run in a parallel
    /// level batch (build_parallel_plan), and its code size.
    std::vector<std::uint8_t> unit_parallel_;
    std::vector<std::uint32_t> unit_cost_;
    bool parallel_planned_ { };
    /// FSIM_KERNEL_THREADS: lanes for parallel batches (1: serial).
    std::uint32_t mt_threads_ { 1U };
    static_kernel_detail::KernelPool::Body parallel_body_;
    /// The slot's last words of a batch while a replayed change sees the
    /// words its write left.
    std::vector<std::uint64_t> replay_words_;
    /// The entry may run on a worker now: eligible and on native code.
    [[nodiscard]] bool parallel_now(std::uint32_t entry) const noexcept;
    /// FSIM_KERNEL_MT_MIN: the least code size of a batch's eligible units
    /// that runs in parallel; FSIM_KERNEL_MT_FORCE: every batch with two.
    std::uint64_t mt_min_cost_ { };
    bool mt_force_ { };
    /// FSIM_KERNEL_MT_SHUFFLE: workers take a batch's units in a random
    /// order and pause at random, to expose any dependence on the order.
    bool mt_shuffle_ { };
    std::vector<std::uint32_t> shuffle_order_;
    std::uint64_t shuffle_state_ { 0x9e3779b97f4a7c15ULL };
    /// Replay of a logged change: its edge bit plus one (slot_changed_general).
    std::uint8_t replay_edge_ { };
    void build_parallel_plan();
    /// Domain components grouped by instance: each component root's cluster
    /// representative, and each sequential member's (no_slot: none).
    struct Clusters {
        std::vector<std::uint32_t> of_root;
        std::vector<std::uint32_t> of_member;
    };
    /// component_of maps a unit to its component root (no_slot: not a
    /// domain unit).
    [[nodiscard]] Clusters cluster_components(
        const std::vector<std::uint32_t>& component_of) const;
    /// FSIM_KERNEL_MT_STATS: per batch phase, the batches offered, those run
    /// in parallel, their units and eligible units, and the time on the pool
    /// and in the replay.
    struct MtStats {
        bool enabled { };
        std::array<std::uint64_t, 3> offered { };
        std::array<std::uint64_t, 3> batches { };
        std::array<std::uint64_t, 3> units { };
        std::array<std::uint64_t, 3> eligible { };
        std::array<std::uint64_t, 3> run_ns { };
        std::array<std::uint64_t, 3> replay_ns { };
        /// Batches run serially: their units, eligible units and cost.
        std::array<std::uint64_t, 3> declined_units { };
        std::array<std::uint64_t, 3> declined_eligible { };
        std::array<std::uint64_t, 3> declined_cost { };
        /// Eligible units each lane ran.
        std::vector<std::uint64_t> lane_units;
        /// Domain settles: count, active domains, records, logged changes,
        /// time on the pool and in the merge.
        std::uint64_t domain_settles { };
        std::uint64_t domain_active { };
        std::uint64_t domain_records { };
        std::uint64_t domain_changes { };
        std::uint64_t domain_run_ns { };
        std::uint64_t domain_merge_ns { };
        std::uint64_t domain_declined { };
        /// Per settle, the longest domain run (the critical path) and the
        /// sum of the domain runs.
        std::uint64_t domain_critical_ns { };
        std::uint64_t domain_merges { };
        /// The leader's time in settle(), run_triggered() and activations.
        std::array<std::uint64_t, 3> phase_ns { };
        std::uint64_t domain_work_ns { };
        /// Commits offered to the domains, declined (too few domain entries,
        /// or a leader write a committing domain reads), run; their domain
        /// and leader entries and the leader's commit merge time.
        std::uint64_t commit_offered { };
        std::uint64_t commit_small { };
        std::uint64_t commit_read { };
        std::uint64_t commit_parallel { };
        std::uint64_t commit_domain_entries { };
        std::uint64_t commit_leader_entries { };
        std::uint64_t commit_private_entries { };
        std::uint64_t commit_merge_ns { };
    } mt_stats_;
    /// Adds the scope's time to one of MtStats::phase_ns.
    struct MtPhaseTimer {
        MtStats& stats;
        std::size_t phase;
        std::chrono::steady_clock::time_point started;
        MtPhaseTimer(MtStats& owner, const std::size_t index)
            : stats(owner)
            , phase(index)
            , started(owner.enabled ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point { })
        {
        }
        ~MtPhaseTimer()
        {
            if (stats.enabled && started != std::chrono::steady_clock::time_point { }) {
                stats.phase_ns[phase] += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started).count());
            }
        }
        MtPhaseTimer(const MtPhaseTimer&) = delete;
        MtPhaseTimer& operator=(const MtPhaseTimer&) = delete;
    };
    void report_mt_stats() const;
    /// What batch_ holds: a settle snapshot, or a triggered round's members
    /// in run order, from merged dispatch groups (batch_groups_ names each
    /// member's group) or from member flags.
    enum class BatchPhase : std::uint8_t { settle, merged, flagged };
    /// Runs batch_ on the pool and replays it; false when the batch is not
    /// worth it (the caller runs it serially).
    [[nodiscard]] bool run_parallel_batch(BatchPhase phase, std::size_t& evaluations,
        std::size_t limit);
    void run_in_lane(Lane& lane, std::uint32_t entry, LaneRun& run);
    void replay(std::uint32_t entry, LaneRun& run, bool settle);
    void log_change(Lane& lane, std::uint32_t slot, std::uint64_t changed,
        const std::uint64_t* before, std::uint32_t field_offset);
    /// Runs one settle entry (a member or partition | partition_tag).
    void run_settle_entry(std::uint32_t entry, std::size_t& evaluations,
        std::size_t limit);
    std::vector<std::uint64_t> arena_;
    /// VHDL delta mode (StaticKernelRuntimeSpec::vhdl).
    bool vhdl_ { };
    /// Debugger plan (StaticKernelRuntimeSpec::debug_points).
    bool debug_points_ { };
    /// Debugger plan: the program the host member ran before its scheduler
    /// program became the kernel stub.
    std::optional<Process> host_original_;
    /// The program the host member ran before its scheduler program became
    /// the kernel stub (startup_program).
    std::optional<Process> host_program_;
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
    /// VHDL delta mode: the round's assignments (word writes and references
    /// to pending_generic_), in program order.
    std::vector<StaticKernelWrite> write_storage_;
    StaticKernelWriteQueue writes_;
    void push_write(const StaticKernelWrite& write);
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
        bool tracks_writer { };
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
    void write_mirror(const Input& input, const PackedLogic4& value);
    std::vector<Container> containers_;
    /// Native view of containers_ (StaticKernelContainerInfo) and the
    /// element tables it points into.
    std::vector<StaticKernelContainerInfo> container_info_;
    std::vector<std::uint32_t> container_elements_;
    /// members_[i].process, dense: hot paths record slot writers without
    /// touching the large member records.
    std::vector<ProcessId> member_process_;
    std::vector<std::uint32_t> container_of_object_;
    std::vector<NbaEntry> nba_;
    std::vector<Pending> pending_generic_;
    std::vector<std::uint32_t> output_queue_;
    std::vector<std::uint32_t> trigger_queue_;
    std::vector<std::uint32_t> triggered_;
    /// Spare storage the drained queues above swap with (QueueDrain).
    std::vector<NbaEntry> nba_spare_;
    std::vector<Pending> pending_generic_spare_;
    std::vector<std::uint32_t> output_queue_spare_;
    std::vector<std::uint32_t> triggered_spare_;
    /// Sequential SystemVerilog members with one edge-sensitivity set and no
    /// other wake-up (build_dispatch_groups). An edge schedules the group
    /// once, and the group runs its members in index order, as the sorted
    /// triggered list would.
    struct DispatchGroup {
        std::vector<std::uint32_t> members;
        bool triggered { };
        /// Expanded into members in the current run_triggered pass: an edge
        /// then schedules the members, whose flags tell what is pending.
        bool expanded { };
        /// Run by run_triggered's merge: members before `next` have run
        /// (or are running) in this pass.
        bool running { };
        std::uint32_t next { };
    };
    std::vector<DispatchGroup> dispatch_groups_;
    std::vector<std::uint32_t> expanded_groups_;
    /// A merged triggered batch: the group of each member in batch_.
    std::vector<DispatchGroup*> batch_groups_;
    /// One level's dirty queue: the units that can be queued at the level
    /// in ascending order (members by index, then partitions) and a bit per
    /// unit; every set bit is in a word at or after `cursor`.
    struct LevelQueue {
        std::vector<std::uint32_t> entries;
        std::vector<std::uint64_t> bits;
        std::uint32_t count { };
        std::uint32_t cursor { };
    };
    /// Level-bucketed dirty queue; `scan_level_` is the lowest level that may
    /// hold entries.
    std::vector<LevelQueue> buckets_;
    std::uint32_t scan_level_ { };
    std::size_t queued_count_ { };
    /// Domain-private settle (multithread proposal §2.4). A domain is a
    /// connected component of the combinational units over the slots,
    /// containers and host signals they share with a write, and over the
    /// notifications between them; no combinational notification leaves it.
    /// A lane settles a domain level by level with its own queues, scheduling
    /// inside the domain and logging what the leader must deliver (changes
    /// with other targets or general effects, queued writes). The leader then
    /// merges the domains' passes with the serial units' passes by level and
    /// queue position, which is the serial kernel's order, and replays the
    /// logs in it.
    struct DomainRecord {
        std::uint32_t entry { };
        /// A continuation of the record before it (the leader finished a
        /// deoptimized run or ran a unit without native code).
        bool continued { };
    };
    /// Writes [begin, end) of a domain's private queue, queued by one run:
    /// where they go in the serial NBA queue is the queue length when the
    /// run's lane writes were replayed (step_item_base_[item]) plus `before`,
    /// the run's writes queued ahead of them there; they keep queue order.
    struct PrivateSegment {
        std::uint32_t item { };
        std::uint32_t before { };
        std::uint32_t begin { };
        std::uint32_t end { };
    };
    /// A committed write with logged changes: its serial place (queue
    /// position; private writes first at a position, by item and order),
    /// its changes on the domain's lane, and whether it raised the error.
    struct LoggedCommit {
        std::uint32_t position { };
        std::uint32_t queued { };
        std::uint32_t item { };
        std::uint32_t before { };
        /// A private write's index in its domain's queue.
        std::uint32_t order { };
        std::uint32_t begin { };
        std::uint32_t end { };
        bool error { };
    };
    struct alignas(64) Domain {
        /// The line its lane writes and the leader reads after a dispatch:
        /// the lane, how the lane stopped (DomainStop: the leader finishes
        /// the last record's run or runs the next unit, then the domain
        /// continues), the lane's summary of the records (evaluations, and
        /// whether any logged changes or writes), the commit's progress (how
        /// many of its writes in sequence it committed: an element write on
        /// the reference path stops it for the leader) and error, the
        /// settle's time on the lane (FSIM_KERNEL_MT_STATS), and the
        /// committed writes whose changes the leader delivers.
        std::uint32_t lane { };
        std::uint8_t stop { };
        bool effects { };
        std::size_t evaluated { };
        std::uint32_t commit_done { };
        std::uint32_t commit_total { };
        std::exception_ptr commit_error;
        std::uint64_t run_ns { };
        std::vector<LoggedCommit> logged;
        /// commit_domains: the NBA queue entries the lane commits (indices
        /// in writes_), which the leader fills.
        alignas(64) std::vector<std::uint32_t> commit;
        /// Its levels, ascending, and one queue per level: the domain's
        /// units of the level in global queue order. A unit's bit is its
        /// queued flag while the lanes run (the shared flags stay clear).
        alignas(64) std::vector<std::uint32_t> levels;
        std::vector<LevelQueue> buckets;
        std::uint32_t scan { };
        std::uint32_t queued { };
        /// This settle: the passes (level, first record), the records and
        /// their runs, and the running snapshot.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> passes;
        std::vector<DomainRecord> records;
        std::vector<LaneRun> runs;
        std::vector<std::uint32_t> snapshot;
        std::uint32_t next { };
        std::size_t evaluations { };
        /// The leader's merge: the next pass.
        std::uint32_t pass { };
        /// Code size of its units: lanes take the largest domains first.
        std::uint64_t cost { };
        /// Private NBA queue: writes its own sequential members queued to its
        /// slots and containers on its lane (element entries in
        /// nba_elements), in segments keyed by their place in the serial
        /// queue. A member that queues only to its domain swaps the queue
        /// into its lane while it runs.
        std::vector<StaticKernelWrite> nba_storage;
        StaticKernelWriteQueue nba;
        std::vector<NbaEntry> nba_elements;
        std::vector<PrivateSegment> segments;
        /// commit_in_domain: the domain's writes in serial order (private
        /// ones tagged) when writes_ has some (else the segments' order).
        std::vector<std::uint32_t> sequence;
    };
    enum DomainStop : std::uint8_t {
        domain_running,
        /// The last record's native run ended with a status to finish.
        domain_finish,
        /// The next unit has no native code to run on a lane.
        domain_unit,
        /// The evaluation limit: the merge reports the failure.
        domain_limit,
        /// The last record raised an error: the merge rethrows it there.
        domain_error,
    };
    std::vector<Domain> domains_;
    /// Per unit (members by index, then partitions): its domain (no_slot:
    /// the global queue), its bucket in the domain and its bit there.
    std::vector<std::uint32_t> unit_domain_;
    std::vector<std::uint32_t> unit_bucket_;
    std::vector<std::uint32_t> unit_bit_;
    /// A change of the slot has a target or effect outside its writers'
    /// domain: a domain lane logs it for the leader.
    std::vector<std::uint8_t> slot_logged_;
    std::vector<std::uint32_t> active_domains_;
    /// settle_domains: the queued domain units it moves, as (level, entry).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> domain_moves_;
    std::vector<std::uint32_t> domain_seen_;
    std::uint32_t domain_epoch_ { };
    /// The merge's pass: (queue position, domain or no_slot, record or entry).
    struct MergeItem {
        std::uint32_t position { };
        std::uint32_t domain { };
        std::uint32_t item { };
    };
    std::vector<MergeItem> merge_items_;
    std::size_t domain_limit_ { };
    static_kernel_detail::KernelPool::Body domain_body_;
    /// Domain time steps (commit_domains): per slot and container, the
    /// domain whose lane commits its queued writes (no_slot: the leader),
    /// and the domain of its combinational readers (no_slot: none,
    /// domain_many: several or a serial one).
    std::vector<std::uint32_t> slot_commit_domain_;
    std::vector<std::uint32_t> container_commit_domain_;
    std::vector<std::uint32_t> slot_reader_domain_;
    std::vector<std::uint32_t> container_reader_domain_;
    static constexpr std::uint32_t domain_many = static_kernel_detail::no_slot - 1U;
    /// The committing domain of each entry of the NBA queue.
    std::vector<std::uint32_t> commit_owner_;
    /// Domain time steps: each sequential member's domain (no_slot: none),
    /// the NBA queue length when each triggered-batch item of this step was
    /// replayed (items number on across the step's batches), and the
    /// writes waiting in domains' private queues.
    std::vector<std::uint32_t> member_domain_;
    /// Per sequential member: every write it queues goes to its domain.
    std::vector<std::uint8_t> member_private_;
    /// Its queued writes' targets another domain or the leader commits:
    /// slots, and containers tagged private_tag.
    std::vector<std::vector<std::uint32_t>> member_foreign_;
    std::vector<std::uint32_t> step_item_base_;
    std::size_t private_writes_ { };
    /// The domains with private writes (bit per domain below 64), and
    /// whether others have some.
    std::uint64_t private_domains_ { };
    bool private_more_ { };
    /// The lane that ran (or would have run) a batch item.
    [[nodiscard]] std::uint32_t item_lane(std::uint32_t item) const noexcept;
    enum class BatchDispatch : std::uint8_t { dynamic, pinned, inline_items };
    BatchDispatch batch_dispatch_ { };
    /// Moves the private queues' writes into writes_ at their serial places.
    void materialize_private_writes();
    [[nodiscard]] bool nba_pending() const noexcept
    {
        return writes_.count != 0U || private_writes_ != 0U;
    }
    static_kernel_detail::KernelPool::Body commit_body_;
    /// FSIM_KERNEL_MT_AFFINITY (default on): each domain runs on one lane
    /// (balanced by cost), so its slots stay in that core's cache across
    /// settles and commits; otherwise lanes take domains as they come.
    bool mt_affinity_ { };
    bool mt_fused_ { };
    std::vector<std::uint32_t> domain_lane_;
    /// run_domain_items: the pinned body's item count and item body.
    std::uint32_t pinned_items_ { };
    const static_kernel_detail::KernelPool::Body* pinned_target_ { };
    static_kernel_detail::KernelPool::Body pinned_body_;
    /// Runs body(lane, item) for each active domain item, on the pool.
    void run_domain_items(const static_kernel_detail::KernelPool::Body& body,
        std::uint32_t count);
    /// FSIM_KERNEL_MT_COMMIT_MIN: the least domain-owned entries a commit
    /// runs in parallel (FSIM_KERNEL_MT_COMMIT=0: never).
    std::size_t mt_commit_min_ { };
    bool mt_commit_ { };
    /// The leader replays domain logs: combinational targets were scheduled
    /// in the domain already.
    bool replay_external_ { };
    /// The leader merges domain passes: a serial unit cannot reach a domain.
    bool reconstructing_ { };
    [[nodiscard]] std::uint32_t unit_of_entry(std::uint32_t entry) const noexcept
    {
        return (entry & static_kernel_detail::partition_tag) != 0U
            ? static_cast<std::uint32_t>(members_.size())
                + (entry & ~static_kernel_detail::partition_tag)
            : entry;
    }
    [[nodiscard]] std::uint32_t queue_position(std::uint32_t entry) const noexcept;
    void enqueue_domain(Domain& domain, std::uint32_t entry);
    /// A domain lane's schedule of a combinational unit: queued in the
    /// lane's domain (every combinational reader of a domain's writes is in
    /// it).
    void schedule_in_domain(Lane& lane, std::uint32_t entry);
    /// The domain's next record and its run.
    LaneRun& add_domain_record(Domain& domain, std::uint32_t entry, bool continued);
    /// The unit's native code, if any (what a lane can run).
    [[nodiscard]] bool native_ready(std::uint32_t entry) const noexcept;
    /// Settles nothing more: the domains' queued units are dropped (an
    /// error aborts the settle).
    void abandon_domains() noexcept;
    /// The units domain lanes queued in their domains go to the global queue.
    void drain_domain_queues() noexcept;
    /// Replays a domain record and its continuations at their place in the
    /// merge; an error the record raised is rethrown there.
    void replay_domain_record(Domain& domain, std::uint32_t record,
        std::size_t& evaluations, std::size_t limit);
    /// settle() through the domains; false when fewer than two domains have
    /// queued units (nothing has moved).
    [[nodiscard]] bool settle_domains(std::size_t& evaluations, std::size_t limit);
    /// The active domains after the pool: the leader finishes what lanes
    /// could not, merges their passes with the serial units' and counts.
    void finish_domains(std::size_t& evaluations, std::size_t limit,
        std::chrono::steady_clock::time_point run_started);
    /// A triggered batch's item on a lane (run_parallel_batch).
    void run_batch_item(std::uint32_t lane, std::uint32_t item);
    static_kernel_detail::KernelPool::Body batch_pinned_body_;
    bool batch_private_ { };
    std::uint32_t step_item_offset_ { };
    /// One of the domain's writes in sequence: the write and its element.
    /// False: an element write needs the reference path (not on a lane).
    [[nodiscard]] bool commit_write(Domain& domain, std::uint32_t at, bool reference);
    /// Records a committed write's changes (from `begin` on the domain's
    /// lane) for the leader to deliver at its serial place.
    void log_commit(Domain& domain, std::uint32_t at, std::uint32_t begin, bool error);
    /// Domain::sequence from the sorted segments and writes_ entries.
    void build_commit_sequence(Domain& domain);
    /// Appends a write to the domain's private queue.
    void push_private(Domain& domain, std::uint32_t item, std::uint32_t before,
        const StaticKernelWrite& write);
    /// Domain::sequence: a private write's index carries this bit.
    static constexpr std::uint32_t private_tag = std::uint32_t { 1 } << 31U;
    /// commit_pending() and the settle after it through the domains: each
    /// domain's lane commits the queued writes of its slots and settles;
    /// the leader commits the rest in queue order with the domains' logged
    /// changes, then merges the settle. False when declined (nothing done).
    [[nodiscard]] bool commit_domains();
    /// A lane's share: the domain's entries in queue order, up to an element
    /// write on the reference path.
    void commit_in_domain(std::uint32_t domain);
    /// The leader on the domain's lane: the rest of its commit, then its
    /// settle.
    void resume_commit(std::uint32_t domain, std::size_t limit);
    /// A logged change, delivered as the leader would have made it.
    void replay_change(const Lane& lane, const LaneChange& change);
    void run_domain(std::uint32_t domain, std::size_t limit);
    /// Domain::evaluated and Domain::effects from its records.
    void summarize_domain(Domain& domain) noexcept;
    /// FSIM_KERNEL_MT_SHUFFLE: a random order of `count` batch items.
    void shuffle_items(std::uint32_t count);
    void resume_domain(std::uint32_t domain, std::size_t limit);
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
    /// A member staged an NBA-region update to a host signal at this time:
    /// the kernel's NBA commit joins the host's NBA region, after it, so
    /// what both wake runs after every update (IEEE 1800-2023 4.5).
    bool host_nba_staged_ { };
    SimulationTick host_nba_time_ { };
    /// Kernel members read a host signal with such an update: they run
    /// again in that region, not in the next delta.
    bool host_nba_wakes_ { };
    /// The deferred NBA commit runs after the host's updates: it commits
    /// before the activation settles.
    bool nonblocking_after_host_ { };
    /// The NBA commit waits for the host's same-time Active and Inactive
    /// work (IEEE 1800-2023 4.5): a host NBA-region task performs it.
    bool nonblocking_deferred_ { };
    /// Running in that task: the first NBA commit proceeds.
    bool nonblocking_region_ { };
    bool in_host_nonblocking_region_ { };
    /// Members that start after the procedures (Member::after_procedures).
    std::vector<std::uint32_t> after_procedure_members_;
    /// The once members of time zero that run in stable order with the
    /// kernel's threads and hosted processes (run_merged_active), from
    /// start_once_next_ on.
    std::vector<std::uint32_t> start_once_;
    std::size_t start_once_next_ { };
    [[nodiscard]] bool start_once_pending() const
    {
        return start_once_next_ < start_once_.size();
    }
    bool yield_requested_ { };
    /// A VHDL member stages updates of host signals (a delayed assignment's
    /// target), which the host applies in its update phase, or reads a host
    /// signal's attributes from the host's history: the kernel's rounds are
    /// the host's deltas from the initialization on (queues_first_activation)
    /// and the kernel never owns time.
    bool vhdl_host_cycles_ { };
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
        /// A member thread resuming in the region (ready_thread_in), not a
        /// hosted process.
        std::uint32_t thread { static_kernel_detail::no_slot };
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
    /// Spare storage for the hosted queues and the merged round's lists
    /// (QueueDrain).
    std::vector<HostedReady> hosted_active_spare_;
    std::vector<HostedReady> hosted_inactive_spare_;
    std::vector<HostedReady> merged_hosted_spare_;
    std::vector<std::uint32_t> merged_threads_spare_;
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
    /// The final members' processes and member indices (run_finals).
    std::vector<std::pair<ProcessId, std::uint32_t>> final_members_;
    std::vector<StartupMember> startup_members_;
    /// The resolver members' processes and member indices (run_resolver).
    std::unordered_map<ProcessId, std::uint32_t> resolver_members_;
    /// A SystemVerilog member makes delayed writes (after, delayed and
    /// inertial updates), which the host matures at the writer's order.
    bool host_delayed_writes_ { };
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
    /// FSIM_PROFILE_KERNEL_BATCHES (diagnostic, multi-threading plan): the
    /// batches a parallel kernel could run at once. A batch is one settle
    /// level visit or one triggered round; per size class (log2 buckets)
    /// it keeps the batches, units and time, and per thread count the time a
    /// batch would take split over min(threads, size) workers plus a barrier
    /// pair.
    struct BatchProfile {
        static constexpr std::size_t classes = 10U;
        static constexpr std::array<std::uint32_t, 4> threads { 2U, 4U, 8U, 16U };
        bool enabled { };
        /// Cost of one fork/join barrier pair (FSIM_PROFILE_KERNEL_BARRIER_NS).
        double barrier_pair_ns { 500.0 };
        std::array<std::uint64_t, classes> batches { };
        std::array<std::uint64_t, classes> units { };
        std::array<std::uint64_t, classes> ns { };
        std::array<double, threads.size()> modeled_ns { };
        std::uint64_t settle_ns { };
        std::uint64_t triggered_ns { };
        /// Settle batches of one member (count, ns), by member name with digits
        /// folded.
        std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> singles;
        void record(std::uint64_t size, std::uint64_t elapsed, bool settle);
    } batch_profile_;
    std::uint64_t profile_activations_ { };
    std::uint64_t profile_activation_ns_ { };
    std::uint64_t profile_native_calls_[6] { };
    std::unordered_map<const void*, std::uint64_t> profile_template_runs_;
    std::uint64_t profile_slot_changes_ { };
    /// FSIM_PROFILE_KERNEL_SLOTS: changes per slot.
    std::vector<std::uint64_t> profile_slot_change_counts_;
    std::uint64_t profile_schedules_ { };
    /// Partition schedules: later in the running partition, already
    /// queued, enqueued; member schedules; slot changes with a fast_notify
    /// entry.
    std::array<std::uint64_t, 5> profile_schedule_outcomes_ { };
    std::uint64_t profile_collapsed_runs_ { };
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
    /// Copies the host's value of a kernel-owned signal into its slot: the
    /// slot when that changed it (import_before_ holds the old words), else
    /// no_slot.
    [[nodiscard]] std::uint32_t import_slot(SignalId signal);
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
    std::shared_ptr<const std::string_view> restored_bodies_;
    std::shared_ptr<std::string> recorded_bodies_;
    std::shared_ptr<const std::string_view> restored_native_;
    std::shared_ptr<std::string> recorded_native_;
    std::size_t specialize_cache_hits_ { };
    std::string compile_failure_detail_;
    std::size_t wide_members_ { };
    std::size_t wide_member_ops_ { };
    std::size_t wide_member_total_ops_ { };
};

} // namespace fsim::runtime::simir

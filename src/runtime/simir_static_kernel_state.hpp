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
#include "simir_static_kernel_ir.hpp"
#include "simir_static_kernel_native.hpp"
#include "simir_static_kernel.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
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
        /// Handovers to full code (FastRun::two_state_runs counts the runs),
        /// and runs since the registers were last known; a member that keeps
        /// meeting X or 'U' values stops running two-state code.
        std::uint32_t two_state_deopts { };
        std::uint32_t unknown_runs { };
        bool two_state_off { };
        /// While two-state code is off: full runs since it went off, and
        /// the count at which known registers turn it back on (doubling
        /// each time; X before a reset need not last).
        std::uint32_t off_runs { };
        std::uint32_t off_retry { 256U };
        std::uint32_t partition_key { static_kernel_detail::no_slot };
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
        std::vector<Reader> readers;
        std::vector<EdgeReader> edges;
        /// No process writes the signal (StaticKernelRuntimeSpec::
        /// unwritten_inputs).
        bool unwritten { };
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
    void run_generic(std::uint32_t member);
    [[nodiscard]] std::uint32_t step_generic(std::uint32_t member,
        std::uint32_t pc);
    void run_compiled(std::uint32_t member);
    void execute(CompiledBody& body, std::uint32_t member);
    void execute_body(CompiledBody& body, std::uint32_t member);
    /// Returns whether a two-state caller got a result with X or U bits.
    /// execute_generic's in-place path for a narrow field written into a
    /// wide register; false when the reference path must run instead.
    bool wide_part_insert(CompiledBody& body, const Word* registers,
        const DynamicPartInsert& insert, std::uint32_t base);
    bool execute_generic(CompiledBody& body, Word* registers,
        const static_kernel_detail::KInst& inst, std::uint32_t member,
        bool two_state_code = false);
    static void native_generic(StaticKernelNativeFrame* frame,
        Word* registers, std::uint32_t at);
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
        bool full = false);
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
    static void native_evaluate(StaticKernelNativeFrame* frame,
        std::uint32_t at, std::uint64_t xa, std::uint64_t xb, std::uint64_t ya,
        std::uint64_t yb, std::uint64_t za, std::uint64_t zb,
        std::uint64_t* out);
    static void native_effect(StaticKernelNativeFrame* frame,
        std::uint32_t at, std::uint64_t xa, std::uint64_t xb, std::uint64_t ya,
        std::uint64_t yb, std::uint64_t za, std::uint64_t zb,
        std::uint64_t* out);
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
    void publish();
    void initialize();
    [[nodiscard]] bool scan_inputs(bool all = true);
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
        /// Compiled members: the synthetic call stack (pointer, entries)
        /// saved while the thread is suspended.
        std::vector<Word> stack;
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
    bool behavioral_ { };
    bool stopped_ { };
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

    [[nodiscard]] std::optional<SimulationTick> next_timer();
    void schedule_stop_at(SimulationTick time);
    /// Inputs some process writes (scan_inputs within an activation).
    std::vector<std::uint32_t> written_inputs_;
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
    void activate_mixed();
    /// The combinational member settle() is running (no_slot otherwise).
    std::uint32_t running_member_ { static_kernel_detail::no_slot };
    ProcessId host_process_ { };
    /// Slots a behavioral thread may wait on: never silent, always on the
    /// general notify path; the edge bit last seen by dynamic waiters.
    std::vector<std::uint8_t> slot_waitable_;
    std::vector<Logic4> slot_wait_seen_;
    void timer_due(SimulationTick time);

    enum class Boundary : std::uint8_t { none, advance, suspend, finish };
    /// Behavioral: the thread and position compiled code starts at, and the
    /// suspension operation it stopped at.
    std::uint32_t behavioral_entry_ { };
    std::optional<std::uint32_t> behavioral_suspended_;
    std::uint32_t deopt_resume_ { };
    [[nodiscard]] bool behavioral_step(std::uint32_t member, std::uint32_t pc);
    [[nodiscard]] Boundary behavioral_boundary(std::uint32_t thread,
        std::uint32_t pc);
    [[nodiscard]] std::optional<std::uint32_t> run_behavioral_compiled(
        std::uint32_t thread, std::uint32_t start);
    void sync_behavioral_registers(std::uint32_t member, std::uint32_t keep);
    void start_behavioral();
    [[nodiscard]] std::uint32_t spawn_thread(std::uint32_t member,
        std::uint32_t pc, std::uint32_t parent, std::uint32_t group);
    void ready_thread(std::uint32_t thread);
    [[nodiscard]] bool run_ready_threads();
    void run_thread(std::uint32_t thread);
    void wait_on(std::uint32_t thread, std::span<const Sensitivity> entries);
    void finish_thread(std::uint32_t thread);
    void wake_waiters(std::vector<Waiter>& waiters, Logic4 before,
        Logic4 after);
    void collect_timers();
    void arm_timer();
    void behavioral_output(std::uint32_t member, std::string_view text,
        bool newline);
    std::vector<ScheduleTarget> notify_targets_;
    std::vector<std::unique_ptr<CompiledBody>> templates_;
    /// Owns the generated code.
    std::shared_ptr<StaticKernelCodegen> codegen_;
    /// Cold member templates are compiled only once they have run
    /// lazy_threshold_ times; until then their members run interpreted.
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
    std::vector<std::uint64_t> profile_member_deopts_;
    /// Diagnostic (FSIM_KERNEL_VERIFY): every compiled VHDL run is replayed
    /// on the reference evaluator and compared.
    bool verify_ { };
    /// Diagnostic (FSIM_KERNEL_CHECK_INPUTS): compare every input value and
    /// report changes the host made without a revision bump.
    bool check_inputs_ { };
    std::uint64_t verify_mismatches_ { };
    std::vector<Slot> slots_;
    /// What a VHDL round's commit needs of each slot, one cache line apiece
    /// (commit_round touches nothing else for a slot that does not change).
    struct alignas(64) CommitSlot {
        std::uint32_t offset { };
        std::uint32_t words { };
        std::uint32_t planes { };
        /// Written in the current round; `before` indexes the round's saved
        /// prior value when it does not fit `small_before`.
        bool touched { };
        bool single_writer { };
        std::uint32_t before { };
        std::array<std::uint64_t, 4> small_before { };
    };
    std::vector<CommitSlot> commit_slots_;
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
    bool nonblocking_committed_ { };
    bool yield_requested_ { };
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
    std::shared_ptr<const StaticKernelSpecializations> restored_specializations_;
    std::shared_ptr<StaticKernelSpecializations> recorded_specializations_;
    std::shared_ptr<const std::string> restored_bodies_;
    std::shared_ptr<std::string> recorded_bodies_;
    std::size_t specialize_cache_hits_ { };
    std::string compile_failure_detail_;
    std::size_t wide_members_ { };
    std::size_t wide_member_ops_ { };
    std::size_t wide_member_total_ops_ { };
};

} // namespace fsim::runtime::simir

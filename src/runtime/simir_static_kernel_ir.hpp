// SPDX-License-Identifier: Apache-2.0
//
// Compiled narrow instruction set of the engine v4 static kernel, shared by
// the kernel's interpreter tier (simir_static_kernel_compiled.cpp) and the
// native code generator (src/compiler/static_kernel_codegen.cpp).
#pragma once

#include "fsim/runtime/simir.hpp"
#include "fsim/semantic/sva_plan.hpp"
#include "simir_kernel_word_ops.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace fsim::runtime::simir::static_kernel_detail {

/// The return targets of a compiled VHDL body's call stack (IEEE 1076-2019
/// 4.3, 10.7: a subprogram calls itself to any depth). Generated code reads
/// `entries` and `capacity` (offsets 0 and 8, checked in the code generator)
/// through StaticKernelNativeFrame::call_stack; a call with depth at the
/// capacity asks the runtime to grow it (`reserve`, the one helper call a
/// call makes). The depth word stays in the body's register file.
struct NativeCallStack {
    std::uint64_t* entries { };
    std::uint64_t capacity { };

    NativeCallStack() = default;
    NativeCallStack(const NativeCallStack& other)
    {
        copy_from(other);
    }
    NativeCallStack(NativeCallStack&& other) noexcept
        : entries { std::exchange(other.entries, nullptr) },
          capacity { std::exchange(other.capacity, 0U) }
    {
    }
    NativeCallStack& operator=(const NativeCallStack& other)
    {
        if (this != &other) {
            std::free(entries);
            entries = nullptr;
            capacity = 0U;
            copy_from(other);
        }
        return *this;
    }
    NativeCallStack& operator=(NativeCallStack&& other) noexcept
    {
        if (this != &other) {
            std::free(entries);
            entries = std::exchange(other.entries, nullptr);
            capacity = std::exchange(other.capacity, 0U);
        }
        return *this;
    }
    ~NativeCallStack()
    {
        std::free(entries);
    }

    /// Room for `needed` entries, kept (capacity doubles).
    void reserve(const std::uint64_t needed)
    {
        if (needed <= capacity) {
            return;
        }
        auto grown = capacity == 0U ? std::uint64_t { 64 } : capacity * 2U;
        if (grown < needed) {
            grown = needed;
        }
        auto* next = static_cast<std::uint64_t*>(
            std::realloc(entries, grown * sizeof(std::uint64_t)));
        if (next == nullptr) {
            throw std::bad_alloc { };
        }
        entries = next;
        capacity = grown;
    }

private:
    void copy_from(const NativeCallStack& other)
    {
        if (other.capacity == 0U) {
            return;
        }
        reserve(other.capacity);
        std::memcpy(entries, other.entries, other.capacity * sizeof(std::uint64_t));
    }
};

constexpr std::uint8_t flag_blocking = 1U << 0U;
constexpr std::uint8_t flag_nba = 1U << 1U;
/// Slice stores; on branches, unknown conditions select when_false.
constexpr std::uint8_t flag_linear = 1U << 2U;
constexpr std::uint8_t flag_strict = 1U << 3U;
constexpr std::uint8_t flag_signed = 1U << 4U;
constexpr std::uint8_t flag_increasing = 1U << 5U;
constexpr std::uint8_t flag_descending = 1U << 6U;
constexpr std::uint8_t flag_two_state = 1U << 7U;
/// store_slot_part and store_slot_dynamic with flag_nba only: the store is a
/// later 64-bit piece of one wide nonblocking write (expand_family_accesses),
/// the same update event as the pieces before it.
constexpr std::uint8_t flag_continues = flag_blocking | flag_nba;
/// High bit of StaticKernelWrite::instruction (kinds 0, never read as an
/// instruction): the queued word continues the update of the word before it
/// (one NBA's pieces; IEEE 1800-2023 4.4.2.2, 10.4.2). Every other adjacent
/// write is an update event of its own.
constexpr std::uint32_t write_continues = 0x80000000U;

/// Compiled narrow instruction set; see simir_static_kernel_compiled.cpp.
enum class KOp : std::uint8_t {
    nop,
    constant,
    copy,
    load_slot,
    load_host,
    binary,
    reduce,
    unary_not,
    logical_not,
    logical_binary,
    shift,
    extract,
    insert,
    concat,
    conditional,
    two_state,
    dynamic_extract,
    dynamic_part_select,
    dynamic_insert,
    dynamic_part_insert,
    jump,
    branch,
    store_slot,
    store_slot_nba,
    store_slot_dynamic,
    store_slot_part,
    store_host,
    mem_bind,
    mem_read,
    mem_write,
    /// Whole-array copy of private container d from private container x
    /// (equal shapes; no register operands).
    mem_copy,
    /// Partition member boundary: d is the member's position, x its index.
    mark,
    /// Executes SimIR operation x of the current member through the
    /// reference value functions; y is the member's register base.
    generic,
    /// d = bits [offset, offset + width) of slot x; imm_a is the slot's word
    /// count (aval words, then bval words).
    load_field,
    // VHDL delta-mode instructions. Registers hold Logic4 words plus, for
    // Logic9 values, a 'U' mask and a weak mask (see CompiledBody::
    // shadow_base): all nine std_logic values are representable.
    /// d = narrow Logic9 slot x (four code planes), as a Logic4 word with
    /// 'U' and weak masks (sub 0), or coerced to Logic4 (sub 1).
    load_slot9,
    /// Signal assignment of register x to slot d: deferred to the end of the
    /// round, or immediate (flag_blocking, shared variables). sub 0: static
    /// bit offset `offset`; sub 1: dynamic index register y (indices[aux]),
    /// imm_a the target slot's width.
    store_vhdl,
    /// d = x op y (IntegerBinaryOperator sub) on width-bit VHDL integers.
    integer_binary,
    /// d = op x (IntegerUnaryOperator sub).
    integer_unary,
    /// Range check of x against [imm_a, imm_b] (signed); flag_signed marks
    /// a negative lower bound for narrow enumeration ordinals.
    integer_check,
    /// Fixed-register call: entries register y + pointer x receives imm_a
    /// (the SimIR return target), x is incremented, control goes to d. z is
    /// the stack capacity. With flag_linear the stack is the body's growable
    /// `call_entries` (x is its depth word, y = x, z = 1).
    call,
    /// Fixed-register return through pointer x and entries y (capacity z):
    /// control goes to the instruction for the popped SimIR target.
    ret,
    /// VHDL assertion of condition register x.
    assert_check,
    /// d = bits [offset, offset + width) of Logic9 slot x (imm_a words per
    /// plane); sub 1 coerces to Logic4, sub 0 is exact.
    load_field9,
    /// SystemVerilog write of a wide value read from slot x into bits
    /// [offset, offset + width) of slot d, fused from a ReadSignal, an
    /// optional DynamicPartSelect (sub 1: base register y, parts[aux], the
    /// selection flags) and the write. flag_nba defers it to the NBA queue.
    wide_move,
    /// Behavioral threads: stop at SimIR operation x (a wait, fork, halt or
    /// $finish) and return it to the thread runner.
    suspend,
    /// Operation tier: runs SimIR operation x through the helper of its
    /// kind (sub, the operation ordinal; aux 1: a thread's step, with its
    /// boundary). Control continues at the returned position when it is a
    /// static successor (d, then y: z of them; offset 1 adds the return
    /// targets); any other position goes back to the runner.
    step,
    // Wide registers (fixed widths over 64 bits). A wide register's value
    // lives in the register file at its storage index s (CompiledBody::
    // wide_base): chunk j (bits 64j .. 64j + 63) is register s + j, and a
    // Logic9 register's masks follow as registers s + n + j (n chunks; their
    // `a` words the 'U' mask, their `b` words the weak mask, see
    // constant_weak). Instructions name storage indices; analyses treat each
    // as one register.
    /// d = slot x (width bits; imm_a words per plane; aux 1: the register
    /// has masks, cleared unless sub 1). sub 0: a Logic4 slot; sub 1: a
    /// Logic9 slot, exact (all nine codes); sub 2: a Logic9 slot coerced to
    /// a Logic4 register.
    wide_load,
    /// Registers d .. d + aux - 1 = x .. x + aux - 1, then imm_a registers
    /// from d + aux cleared (a Logic4 value's masks).
    wide_copy,
    /// Registers d .. d + aux - 1 = CompiledBody::wide_constants from imm_a.
    wide_const,
    /// d (width bits; sub 1: with masks) = the x operands from
    /// concat[aux], most significant first: narrow registers, or wide
    /// storage (width over 64; ConcatOperand::unknown: with masks).
    wide_concat,
    /// Register x (width bits; aux 1: with masks) to bits offset ..
    /// offset + width - 1 of slot d, by 64-bit chunk. sub 0: SystemVerilog
    /// blocking, 1: nonblocking, 2: VHDL signal assignment (flag_blocking:
    /// immediate, a shared variable).
    wide_store,
    /// d (width bits; imm_a 1: with masks) = target x with narrow
    /// source y written at the part selection parts[aux] of base z, as
    /// dynamic_part_insert.
    wide_part_insert,
    /// Interconnect delay write (IEEE 1364-2005 16.2.4): SimIR operation y
    /// of the member (a WriteNetDelay) writes register x (width bits; aux
    /// 1: with 'U' masks) through the kernel's delayed-write helper, which
    /// holds its pending changes as kernel timers.
    net_delay,
    // y6
    /// Arithmetic and logic on chunked wide registers (widths over 64): the
    /// family is `offset` (WideFamily), the operator `sub` (BinaryOperator,
    /// ShiftOperator, ReductionOperator or LogicalBinaryOperator), the
    /// operand width `width`. d, x, y and z are register-file storage indices
    /// of wide operands or narrow registers; aux (wide_alu_* bits, one group
    /// per operand slot d x y z) tells which are wide, which wide ones carry
    /// 'U' and weak masks and which registers are of Logic9 kind. imm_a is
    /// y's width and imm_b x's width; flag_signed: a signed shift amount.
    /// Generated code computes it over the chunks (static_kernel_codegen);
    /// the bytecode tier through the reference value functions.
    wide_alu,
    // Concurrent assertions (IEEE 1800-2023 16.14, 39.4.2; task #186). A
    // directive's attempt state is its monitor's flat block
    // (sva::AttemptStorage), published in the kernel's table of block bases
    // (StaticKernelSvaTable::blocks) at entry member_base[member] + aux.
    /// One clock tick of the SvaOperation at SimIR index x of the member
    /// (y its register base, aux the directive's ordinal in the member):
    /// the guards, start and disabled registers are read from the register
    /// file, the monitor advances its attempts (sva::Monitor::tick), and
    /// d (32 bits) receives the pending outcome count. The monitor is
    /// created and bound to its table entry at the first tick.
    sva_tick,
    /// The next outcome of that directive into d (32 bits; 0 when none, else
    /// sva::Outcome): the inline ring pop of the block, which also records
    /// the outcome's start and fail position and the pop serial
    /// (h_last_start, h_last_fail_*, h_pop_serial) for the host's callback.
    /// A null entry (the monitor was restored, or none exists yet) binds
    /// through fsim_sk_sva_bind first.
    sva_outcome,
    /// A sampled-value read (16.5.1, 16.9.3) into the narrow register d
    /// (width bits): sub the SignalReadKind. aux bit 0 set: $sampled of the
    /// Preponed value, x the offset in Impl::sampled_value_words; clear: a
    /// clocked history read, x the ring's start in sampled_ring_words,
    /// offset the signal's width, z the $past depth, imm_a/imm_b the
    /// signal's default (an empty history). For $rose, $fell, $stable and
    /// $changed y is 1 + the signal's offset in Impl::sampled_value_words:
    /// in a time step that is not a tick of the clock (the ring's last
    /// sample is of an earlier one) they compare the Preponed value with
    /// the ring's last sample (16.9.3).
    sva_sampled_load,
    /// An automatic callable's frame (VHDL, a recursive callable): saves
    /// the registers of CompiledBody::frame_saves[aux] (a header word pair
    /// holding the identity, then every saved register-file pair) on the
    /// body's growable frame stack; the depth word x counts pairs. Only a
    /// full store asks the runtime to grow it (the capacity helper). Sub 1:
    /// the callable's frame also lists strings, containers or private
    /// arrays, values the runtime library owns, which the runtime then
    /// saves (StaticKernel::frame_storage of operation y).
    frame_push,
    /// Restores the registers of frame_saves[aux] (the saved pairs, except
    /// those of the registers the call's results keep) and pops the frame;
    /// sub 1 as for frame_push.
    frame_pop,
};

/// Number of kernel instruction kinds (KOp values 0 .. frame_pop).
inline constexpr std::size_t kernel_op_count
    = static_cast<std::size_t>(KOp::frame_pop) + 1U;

/// KOp::wide_alu operation families (KInst::offset).
enum class WideFamily : std::uint32_t {
    binary,
    shift,
    unary_not,
    reduce,
    conditional,
    logical_not,
    logical_binary,
    two_state,
    /// Bits [imm_a, imm_a + width) of the wide x (imm_b bits) into the wide
    /// d (width bits): Extract between wide registers.
    extract,
    /// Insert: the source y (imm_a bits; a narrow register or wide) at bit
    /// imm_b of the wide target x, into the wide d (width bits).
    insert,
    /// DynamicInsert: as insert at the offset the index register z selects
    /// (CompiledBody::indices[imm_b]); an unknown or out-of-range index, or
    /// a field past the target, leaves the target as it is (flag_strict:
    /// an error), IEEE 1800-2023 11.5.1.
    dynamic_insert,
    /// DynamicPartSelect with a wide result: the bits of the wide x (imm_a
    /// bits) that the 32 bit base register y and the part imm_b
    /// (CompiledBody::parts) select, into the wide d (width bits); flags the
    /// part's flag_increasing, flag_descending and flag_two_state. Bits
    /// outside the source read X (0 for two-state) and an unknown base
    /// selects nothing, as dynamic_part_select_value (IEEE 1800-2023 11.5.1).
    dynamic_part_select,
};

/// KOp::wide_alu operand slots (d, x, y, z are 0 .. 3): KInst::aux holds
/// `wide_alu_masks << slot` for a wide operand with masks, `wide_alu_wide <<
/// slot` for a wide operand (else a narrow register), and `wide_alu_logic9
/// << slot` for a register of Logic9 kind.
inline constexpr std::uint32_t wide_alu_masks = 1U;
inline constexpr std::uint32_t wide_alu_wide = 1U << 4U;
inline constexpr std::uint32_t wide_alu_logic9 = 1U << 8U;

/// Chunks of a wide register of `width` bits.
[[nodiscard]] constexpr std::uint32_t wide_chunks(const std::uint32_t width) noexcept
{
    return (width + 63U) / 64U;
}

/// CompiledBody::wide_base entries: the storage index, with this bit set
/// when the register has masks (a Logic9 register).
inline constexpr std::uint32_t wide_unknown_flag = 1U << 31U;

// Logic9 values in registers. A register holds a Logic4 word (a, b) and, when
// it may hold 'U' or one of 'H', 'L', 'W' and '-' (a tracked register, or a
// wide Logic9 register), a mask pair (u, w): u marks the 'U' elements, w the
// weak ones. The word of such an element holds the Logic4 value the
// std_logic_1164 logic tables treat it as, with w set:
//   'U'  u = 1, word X       'L'  w = 1, word 0
//   'W'  w = 1, word X       'H'  w = 1, word 1
//   '-'  w = 1, word Z (the Logic4 combination no other element of a
//        Logic9 register uses)
// and 'X', 'Z', '0', '1' have both masks clear. u and w are disjoint. A
// narrow tracked register r keeps the pair as the shadow word at register
// shadow_base + r (a = u, b = w); a wide register keeps it in the `a` and
// `b` words of its mask chunks. Operations whose result is the same for the
// word as for the element ('L' is 0 and 'H' is 1 in every operation but the
// enumeration compare, the copy-like operations and assignments, 'W' and '-'
// are unknown) ignore the mask pair; the others carry or read it. A Logic4
// destination reads the pair through coerce_weak ('W' and '-' read as X).


/// For KOp::constant in a VHDL body: the 'U' mask of the constant is
/// offset | aux << 32, and its weak mask x | y << 32 (a constant with a weak
/// mask keeps sub 0, so x and y are never bindings).
[[nodiscard]] constexpr std::uint64_t constant_unknown(
    const std::uint32_t offset, const std::uint32_t aux) noexcept
{
    return static_cast<std::uint64_t>(offset)
        | (static_cast<std::uint64_t>(aux) << 32U);
}

[[nodiscard]] constexpr std::uint64_t constant_weak(
    const std::uint32_t x, const std::uint32_t y) noexcept
{
    return static_cast<std::uint64_t>(x) | (static_cast<std::uint64_t>(y) << 32U);
}

struct KInst {
    KOp op { KOp::nop };
    std::uint8_t sub { };
    std::uint8_t flags { };
    std::uint32_t width { };
    std::uint32_t d { };
    std::uint32_t x { };
    std::uint32_t y { };
    std::uint32_t z { };
    std::uint32_t offset { };
    std::uint32_t aux { };
    std::uint64_t imm_a { };
    std::uint64_t imm_b { };
};

/// The shape of the resolved slot a VHDL assignment (store_vhdl, wide_store
/// sub 2) writes, which generated code resolves into the slot's next block
/// (Slot::net_processes): the source count, the member's source and the
/// slot's width. store_vhdl carries it per form: sub 0 and 2 imm_b the
/// sources and own << 32 and aux the width (aux 0: the slot is not
/// resolved); sub 1 imm_b the same (0: not resolved) and imm_a the width;
/// sub 3 imm_a the width and own << 32, imm_b the constants' index and the
/// sources << 32. wide_store: imm_a words and width << 32, imm_b as sub 0.
/// The source count carries net_nine_bit when the slot is a VHDL signal of
/// nine values (std_logic resolution: its value holds the four code planes,
/// not the two of a SystemVerilog net).
inline constexpr std::uint32_t net_nine_bit = 0x80000000U;

struct NetShape {
    std::uint32_t sources { };
    std::uint32_t own { };
    std::uint32_t width { };
    /// The resolved value has nine states (four code planes), resolved by
    /// the std_logic_1164 table over the sources that drive each element.
    bool nine { };

    /// The planes of the slot's value, before its sources' planes.
    [[nodiscard]] std::uint32_t rows() const noexcept
    {
        return nine ? 4U : 2U;
    }
};

/// The shape of an immediate's source count (with net_nine_bit), the
/// member's source and the slot's width.
[[nodiscard]] constexpr NetShape make_net_shape(const std::uint32_t counted,
    const std::uint32_t own, const std::uint32_t width) noexcept
{
    return NetShape { counted & ~net_nine_bit, own, width,
        (counted & net_nine_bit) != 0U };
}

[[nodiscard]] inline std::optional<NetShape> net_shape(const KInst& inst) noexcept
{
    const auto low = [](const std::uint64_t value) {
        return static_cast<std::uint32_t>(value);
    };
    const auto high = [](const std::uint64_t value) {
        return static_cast<std::uint32_t>(value >> 32U);
    };
    if (inst.op == KOp::wide_store) {
        if (inst.imm_a == 0U || inst.imm_b == 0U) {
            return std::nullopt;
        }
        return make_net_shape(low(inst.imm_b), high(inst.imm_b), high(inst.imm_a));
    }
    if (inst.op != KOp::store_vhdl) {
        return std::nullopt;
    }
    switch (inst.sub) {
    case 0U:
    case 2U:
        if (inst.aux == 0U) {
            return std::nullopt;
        }
        return make_net_shape(low(inst.imm_b), high(inst.imm_b), inst.aux);
    case 1U:
        if (inst.imm_b == 0U) {
            return std::nullopt;
        }
        return make_net_shape(low(inst.imm_b), high(inst.imm_b), low(inst.imm_a));
    default:
        if (high(inst.imm_b) == 0U) {
            return std::nullopt;
        }
        return make_net_shape(high(inst.imm_b), high(inst.imm_a), low(inst.imm_a));
    }
}

struct ConcatOperand {
    std::uint32_t reg { };
    std::uint32_t width { };
    /// wide_concat: a wide operand (width over 64) with masks.
    std::uint32_t unknown { };
};

/// The constants of one directive's property plan (sva::CompiledPlan with
/// its bounds resolved), which generated code specializes the attempt
/// automaton on (KOp::sva_tick with z != 0; task #186). Only plans of the
/// shapes sva_native_plan accepts have one.
struct SvaNativeStep {
    semantic::sva::Op op { semantic::sva::Op::check };
    std::uint32_t a { };
    std::uint32_t b { };
    std::uint32_t c { };
    std::int64_t lo { };
    std::int64_t hi { };
    std::uint32_t line { };
    std::uint32_t column { };
    /// A bound read from the block's bounds region (layout 5): the value is
    /// bounds[slot] + lo (or hi); -1 for a literal or `$`.
    std::int32_t lo_slot { -1 };
    std::int32_t hi_slot { -1 };
    friend bool operator==(const SvaNativeStep&, const SvaNativeStep&) = default;
};

struct SvaNativeSequence {
    std::vector<SvaNativeStep> code;
    /// The thread's counter words: the repetition counters, then the
    /// valuation of the plan's local variables from locals_base (16.10).
    std::uint32_t counters { };
    std::uint32_t locals_base { };
    /// CompiledPlan::state_base: the first state id of the sequence's
    /// program counters.
    std::uint32_t state_base { };
    friend bool operator==(const SvaNativeSequence&, const SvaNativeSequence&)
        = default;
};

struct SvaNativeProperty {
    semantic::sva::PropertyKind kind { semantic::sva::PropertyKind::sequence };
    bool overlapped { };
    /// sync_accept_on / sync_reject_on.
    bool synchronous { };
    /// A multiclocked plan (16.13): the clock that flows into the property
    /// (semantic::sva::Property::clock).
    std::uint32_t clock { };
    std::uint32_t sequence { };
    /// The one child (-1 when none): the consequent of an implication, the
    /// operand of a negation.
    std::int32_t child { -1 };
    /// All children in order (the operands of and, or, implies, iff; the
    /// branches of if/else).
    std::vector<std::int32_t> children;
    /// The guard of if/else, accept_on and reject_on.
    std::uint32_t guard { };
    /// nexttime's offset: lo (+ bounds[lo_slot] when lo_slot >= 0).
    std::int64_t lo { };
    std::int32_t lo_slot { -1 };
    /// always and eventually: the window's end (INT64_MAX for `$`), as lo.
    std::int64_t hi { };
    std::int32_t hi_slot { -1 };
    std::uint32_t line { };
    std::uint32_t column { };
    /// The if/else condition is expression `guard` over the instance's local
    /// variables rather than a sampled guard (16.10).
    bool guard_expression { };
    /// The initialization assignments of the property's local variables
    /// (expressions), run when an instance's evaluation begins.
    std::vector<std::uint32_t> initializers;
    friend bool operator==(const SvaNativeProperty&, const SvaNativeProperty&)
        = default;
};

struct SvaNativePlan {
    std::vector<SvaNativeSequence> sequences;
    std::vector<SvaNativeProperty> properties;
    std::uint32_t root { };
    /// `cover sequence` (16.14.3) of a sequence property.
    bool every_match { };
    /// A multiclocked plan (16.13): the guard of each clock's tick.
    std::vector<std::uint32_t> clock_guards;
    /// The source position of each guard (step events' matched expression).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> guard_positions;
    /// The local variables (IEEE 1800-2023 16.10; CompiledPlan): each
    /// local's word offset and size in a valuation and its width in bits,
    /// the valuation's words, each expression's target (0 a check, else the
    /// assigned local plus one) and the local sets of the join ends. A plan
    /// with expressions evaluates them through the block's memo; a plan
    /// whose only use is the request operations (KOp::sva_tick sub 1 to 4)
    /// has these and no sequences.
    std::vector<std::uint32_t> local_offset;
    std::vector<std::uint32_t> local_size;
    std::vector<std::uint32_t> local_width;
    std::uint32_t locals_words { };
    std::vector<std::uint32_t> expression_target;
    std::vector<std::vector<std::uint32_t>> local_sets;
    /// The memo's entry words (layout::memo_e_locals + locals_words + the
    /// widest result), 0 without expressions.
    std::uint32_t memo_stride { };
    friend bool operator==(const SvaNativePlan&, const SvaNativePlan&) = default;
};

struct CompiledBody {
    std::vector<KInst> code;
    std::vector<kernel_word::Word> registers;
    std::vector<std::uint32_t> register_widths;
    std::vector<DynamicIndex> indices;
    std::vector<DynamicPartIndex> parts;
    std::vector<ConcatOperand> concat;
    std::vector<std::uint32_t> containers;
    std::uint32_t entry { };
    /// Polymorphic registers (no fixed width), used only by generic
    /// instructions.
    std::vector<PackedLogic4> wide_registers;
    /// By register: the storage index of a wide register (see KOp::wide_load;
    /// with wide_unknown_flag), 0 for any other register.
    std::vector<std::uint32_t> wide_base;
    /// wide_const values, as registers.
    std::vector<kernel_word::Word> wide_constants;
    /// The plans of the directives whose tick is generated code
    /// (KOp::sva_tick, z the index plus one).
    std::vector<SvaNativePlan> sva_plans;
    /// Return targets (SimIR operation indices) a ret may reach, for the
    /// native dispatch.
    std::vector<std::uint32_t> return_targets;
    /// VHDL: registers live at the entry (one bit each); only these must
    /// carry exact values from one activation to the next.
    std::vector<std::uint64_t> live_at_entry;
    /// VHDL 'U' and weak tracking. A tracked register r has a shadow word at
    /// register shadow_base + r whose `a` marks its 'U' elements and, in a
    /// design with weak elements (`weak`), `b` its weak ones ('H', 'L', 'W',
    /// '-'; see above); those elements read as X (U) or the Logic4 value
    /// their logic tables use (weak) in the register itself, so operations
    /// whose result is the same for the element as for that value
    /// (comparisons, arithmetic, reductions, conditions) ignore the shadow.
    /// shadow_base 0 means no tracking.
    std::uint32_t shadow_base { };
    /// Nonzero when the design can hold weak elements ('H', 'L', 'W', '-':
    /// StaticKernel::design_may_hold_weak). Only then do the tracked
    /// registers' shadow words carry weak masks (their `b` words) and the
    /// code that loads, moves and stores them; a body of a design without
    /// weak elements has exactly the code it has for 'U' alone.
    std::uint8_t weak { };
    std::vector<std::uint8_t> tracked;
    /// Per instruction: u_check (operands must have no 'U', else
    /// deoptimize), u_clear (the result has no 'U'), u_aware (propagates).
    std::vector<std::uint8_t> u_mode;
    /// Tracked operands of u_check instruction n:
    /// u_operands[u_operand_begin[n] .. u_operand_begin[n + 1]).
    std::vector<std::uint32_t> u_operand_begin;
    std::vector<std::uint32_t> u_operands;
    /// VHDL: register index of the call stack's depth word (the return
    /// targets are in `call_entries`); 0 when the member makes no
    /// runtime-stack calls.
    std::uint32_t call_stack_base { };
    NativeCallStack call_entries;
    /// A VHDL body whose recursive callables keep real frames: the register
    /// index of the frame stack's depth word (register-file pairs saved,
    /// headers included), and the stack itself (two entries per pair); 0
    /// when the body has none.
    std::uint32_t frame_stack_base { };
    NativeCallStack frame_entries;
    /// One KOp::frame_push or frame_pop's registers: the identity, the
    /// items (register-file index, pairs, offset after the header) to
    /// save or restore, the pairs a frame holds (header included), and the
    /// SimIR registers it names, for conversion to the reference's frame.
    struct FrameSave {
        std::uint32_t identity { };
        std::uint32_t item_begin { };
        std::uint32_t item_end { };
        std::uint32_t pairs { };
        std::uint32_t register_begin { };
        std::uint32_t register_end { };
    };
    struct FrameItem {
        std::uint32_t index { };
        std::uint32_t count { };
        std::uint32_t offset { };
    };
    std::vector<FrameSave> frame_saves;
    std::vector<FrameItem> frame_items;
    std::vector<std::uint32_t> frame_registers;
    /// Behavioral bodies: instructions a thread may start at (operation 0,
    /// after each suspension, fork branches), for the native entry switch.
    std::vector<std::uint32_t> resume_entries;
};

/// A VHDL process body whose first run enters its native code at operation
/// 0, the process prologue (static_kernel_prologue_entry), and later runs at
/// the activation entry.
inline bool has_prologue_entry(const CompiledBody& body, const bool vhdl)
{
    return vhdl && body.resume_entries.empty() && body.entry != 0U
        && !body.code.empty();
}

constexpr std::uint8_t u_check = 1U;
constexpr std::uint8_t u_clear = 2U;
constexpr std::uint8_t u_aware = 4U;

/// Thrown by slow paths when compiled execution cannot continue (an error
/// the reference evaluator reports, a 'U' in an
/// operand of an operation that does not carry it); `resume` is the SimIR
/// operation at which the reference evaluator takes
/// over, and `executed` says whether that operation already ran.
struct KernelDeopt {
    std::uint32_t resume { };
    bool executed { };
    /// With `executed`, the register the operation wrote; the reference
    /// register file already holds its value.
    std::uint32_t keep { 0xffffffffU };
};

} // namespace fsim::runtime::simir::static_kernel_detail

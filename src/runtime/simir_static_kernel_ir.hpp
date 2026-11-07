// SPDX-License-Identifier: Apache-2.0
//
// Compiled narrow instruction set of the engine v4 static kernel, shared by
// the kernel's interpreter tier (simir_static_kernel_compiled.cpp) and the
// native code generator (src/compiler/static_kernel_codegen.cpp).
#pragma once

#include "fsim/runtime/simir.hpp"
#include "simir_kernel_word_ops.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace fsim::runtime::simir::static_kernel_detail {

constexpr std::uint8_t flag_blocking = 1U << 0U;
constexpr std::uint8_t flag_nba = 1U << 1U;
/// Slice stores; on branches, unknown conditions select when_false.
constexpr std::uint8_t flag_linear = 1U << 2U;
constexpr std::uint8_t flag_strict = 1U << 3U;
constexpr std::uint8_t flag_signed = 1U << 4U;
constexpr std::uint8_t flag_increasing = 1U << 5U;
constexpr std::uint8_t flag_descending = 1U << 6U;
constexpr std::uint8_t flag_two_state = 1U << 7U;

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
    /// the stack capacity.
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
};

/// Number of kernel instruction kinds (KOp values 0 .. net_delay).
inline constexpr std::size_t kernel_op_count
    = static_cast<std::size_t>(KOp::net_delay) + 1U;

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
struct NetShape {
    std::uint32_t sources { };
    std::uint32_t own { };
    std::uint32_t width { };
};

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
        return NetShape { low(inst.imm_b), high(inst.imm_b), high(inst.imm_a) };
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
        return NetShape { low(inst.imm_b), high(inst.imm_b), inst.aux };
    case 1U:
        if (inst.imm_b == 0U) {
            return std::nullopt;
        }
        return NetShape { low(inst.imm_b), high(inst.imm_b), low(inst.imm_a) };
    default:
        if (high(inst.imm_b) == 0U) {
            return std::nullopt;
        }
        return NetShape { high(inst.imm_b), high(inst.imm_a), low(inst.imm_a) };
    }
}

struct ConcatOperand {
    std::uint32_t reg { };
    std::uint32_t width { };
    /// wide_concat: a wide operand (width over 64) with masks.
    std::uint32_t unknown { };
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
    /// VHDL: register index of the synthetic call stack pointer (entries
    /// follow it); 0 when the member makes no runtime-stack calls.
    std::uint32_t call_stack_base { };
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

/// Entries of a synthetic VHDL call stack (deeper calls deoptimize).
constexpr std::uint32_t call_stack_depth = 16U;

constexpr std::uint8_t u_check = 1U;
constexpr std::uint8_t u_clear = 2U;
constexpr std::uint8_t u_aware = 4U;

/// Thrown by slow paths when compiled execution cannot continue (an error
/// the reference evaluator reports, a call stack overflow, a 'U' in an
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

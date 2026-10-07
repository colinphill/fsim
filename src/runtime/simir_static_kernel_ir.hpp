// SPDX-License-Identifier: Apache-2.0
//
// Compiled narrow instruction set of the engine v4 static kernel, shared by
// the kernel's interpreter tier (simir_static_kernel_compiled.cpp) and the
// native code generator (src/compiler/static_kernel_codegen.cpp).
#pragma once

#include "fsim/runtime/simir.hpp"
#include "simir_kernel_word_ops.hpp"

#include <cstdint>
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
    /// Partition member boundary: d is the member's position, x its index.
    mark,
    /// Executes SimIR operation x of the current member through the
    /// reference value functions; y is the member's register base.
    generic,
    /// d = bits [offset, offset + width) of slot x; imm_a is the slot's word
    /// count (aval words, then bval words).
    load_field,
    // VHDL delta-mode instructions. Registers hold Logic4 words; a Logic9
    // value outside {0, 1, X, Z} leaves compiled execution for the
    // reference evaluator (a deoptimization) at the instruction that meets
    // it.
    /// d = narrow Logic9 slot x (four code planes), as a Logic4 word.
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
    /// Always leaves compiled execution (an unrepresentable constant).
    deopt,
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
};

/// For KOp::constant in a VHDL body: the 'U' mask of the constant is
/// offset | aux << 32.
[[nodiscard]] constexpr std::uint64_t constant_unknown(
    const std::uint32_t offset, const std::uint32_t aux) noexcept
{
    return static_cast<std::uint64_t>(offset)
        | (static_cast<std::uint64_t>(aux) << 32U);
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

struct ConcatOperand {
    std::uint32_t reg { };
    std::uint32_t width { };
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
    /// Registers wider than 64 bits, used only by generic instructions.
    std::vector<PackedLogic4> wide_registers;
    /// Return targets (SimIR operation indices) a ret may reach, for the
    /// native dispatch.
    std::vector<std::uint32_t> return_targets;
    /// VHDL: registers live at the entry (one bit each); only these must
    /// carry exact values from one activation to the next.
    std::vector<std::uint64_t> live_at_entry;
    /// VHDL 'U' tracking. A tracked register r has a shadow word at register
    /// shadow_base + r whose `a` marks its 'U' elements; those elements read
    /// as X in the register itself, so operations whose result is the same
    /// for U and X (comparisons, arithmetic, reductions, conditions) ignore
    /// the shadow. shadow_base 0 means no tracking.
    std::uint32_t shadow_base { };
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

/// Entries of a synthetic VHDL call stack (deeper calls deoptimize).
constexpr std::uint32_t call_stack_depth = 16U;

constexpr std::uint8_t u_check = 1U;
constexpr std::uint8_t u_clear = 2U;
constexpr std::uint8_t u_aware = 4U;

/// Thrown by slow paths when a value cannot be represented as a Logic4 word;
/// `resume` is the SimIR operation at which the reference evaluator takes
/// over, and `executed` says whether that operation already ran.
struct KernelDeopt {
    std::uint32_t resume { };
    bool executed { };
    /// With `executed`, the register the operation wrote; the reference
    /// register file already holds its value.
    std::uint32_t keep { 0xffffffffU };
};

} // namespace fsim::runtime::simir::static_kernel_detail

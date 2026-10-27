// SPDX-License-Identifier: Apache-2.0
//
// A SystemVerilog constant function (IEEE 1800-2017 13.4.3) compiled to
// register code over values of at most 64 known bits.
// HirConstantEvaluator compiles a function's body once per specialization
// and runs the code instead of interpreting the body. The code reproduces
// the evaluator's own semantics: operand widths, signedness and value
// profiles are fixed when the function is compiled. A value the code cannot
// hold, such as an x or z bit, abandons the run, and the evaluator then
// interprets the call as before.
#pragma once

#include "fsim/frontend/design_core.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::elaboration::sv_constant_program {

/// The profile of an integral constant other than its bits: the fields of
/// HirSystemVerilogConstant that the evaluator carries from one operation
/// to the next.
struct Shape {
    std::uint32_t width { 1U };
    bool signed_value { };
    bool unsized { };
    frontend::ValueDomain domain { frontend::ValueDomain::Logic4 };
    std::string nominal_type;
    std::optional<semantic::sv::PackedRange> packed_range;

    friend bool operator==(const Shape& left, const Shape& right)
    {
        return left.width == right.width
            && left.signed_value == right.signed_value
            && left.unsized == right.unsized && left.domain == right.domain
            && left.nominal_type == right.nominal_type
            && left.packed_range == right.packed_range
            && (!left.packed_range
                || left.packed_range->element_signed
                    == right.packed_range->element_signed);
    }
};

enum class OpCode : std::uint8_t {
    constant, // r[a] = immediate
    copy, // r[a] = r[b]
    store, // r[a] = r[b], known, shape[a] = immediate
    set_unknown, // r[a] unknown, shape[a] = immediate
    require_known, // abandon unless r[a] is known
    require_shape, // abandon unless shape[a] == immediate
    fill, // r[a] = r[b] bit 0 across `width`
    extend, // r[a] = r[b] from `aux` bits to `width`, signed by flag
    negate,
    invert,
    logical_not,
    reduce_and,
    reduce_or,
    reduce_xor,
    add,
    subtract,
    multiply,
    divide_unsigned,
    modulo_unsigned,
    divide_signed,
    modulo_signed,
    bit_and,
    bit_or,
    bit_xor,
    bit_xnor,
    equal,
    not_equal,
    less_unsigned,
    less_equal_unsigned,
    greater_unsigned,
    greater_equal_unsigned,
    less_signed,
    less_equal_signed,
    greater_signed,
    greater_equal_signed,
    shift_left,
    shift_right,
    shift_right_arithmetic,
    shift_or, // r[a] = (r[b] << aux) | r[c], masked to `width`
    select, // r[a] = selects[immediate] of r[b] at index r[c]
    write_select, // selects[immediate] of slot r[a] = r[b] at index r[c]
    jump, // pc = immediate
    jump_if_zero, // pc = immediate when r[a] == 0
    jump_if_nonzero,
    loop_start, // r[a] = 0
    loop_step, // abandon when ++r[a] reaches the iteration limit
    return_value, // result = r[a] with shape immediate; stop
    finish, // result = slot r[a] (known), shape[a]; stop
    abandon,
};

struct Op {
    OpCode code { OpCode::abandon };
    bool flag { };
    std::uint32_t width { };
    std::uint32_t aux { };
    std::uint32_t a { };
    std::uint32_t b { };
    std::uint32_t c { };
    std::uint64_t immediate { };
};

/// A bit-select or part-select: which bits of a base `width` bits wide an
/// index value selects (evaluate_selection, assign_target). The selected
/// field is `count` bits starting at offset `start + index` (or `start -
/// index` when `index_negated`), where the index is the operand read as
/// `index_width` bits, signed when `index_signed`.
struct Select {
    std::uint32_t width { };
    std::uint32_t count { 1U };
    std::int64_t start { };
    bool index_negated { };
    bool index_signed { };
    std::uint32_t index_width { };
    // A constant field position: the index operand is not read.
    bool constant_index { };
    // A part-select fails on an index that is no integer; a bit-select
    // reads the default value and a bit write is skipped.
    bool part_select { };
    // An out-of-range bit reads as 0 (2-state) rather than x.
    bool two_state { };
};

/// An entry slot: a frame value loaded before the run.
struct EntrySlot {
    std::uint32_t declaration { };
    std::uint32_t slot { };
    std::uint32_t shape { };
};

struct Program {
    std::vector<Op> ops;
    std::vector<Select> selects;
    std::vector<Shape> shapes;
    std::vector<EntrySlot> entry;
    std::uint32_t registers { };
    // Declarations folded into constants. A frame that binds one of them
    // at the time of a call would have shadowed the folded value.
    std::vector<std::uint32_t> folded_declarations;
};

/// A slot's state when the run starts.
struct EntryValue {
    std::uint64_t bits { };
    bool known { };
};

struct Result {
    std::uint64_t bits { };
    std::uint32_t shape { };
};

/// The iteration limit of one loop (maximum_constant_work_units).
inline constexpr std::uint64_t maximum_loop_iterations
    = 64U * 1024U * 1024U;

/// Run `program` from the entry values (one per Program::entry); nothing
/// when the run is abandoned.
[[nodiscard]] std::optional<Result> run(
    const Program& program, std::span<const EntryValue> entry);

[[nodiscard]] constexpr std::uint64_t width_mask(
    const std::uint32_t width) noexcept
{
    return width >= 64U ? ~std::uint64_t { 0 }
                        : (std::uint64_t { 1 } << width) - 1U;
}

} // namespace fsim::elaboration::sv_constant_program

// SPDX-License-Identifier: Apache-2.0
//
// Compiled narrow instruction set of the engine v4 static kernel, shared by
// the kernel's interpreter tier (simir_static_kernel_compiled.cpp) and the
// native code generator (src/compiler/static_kernel_codegen.cpp).
#pragma once

#include "fsim/runtime/simir.hpp"
#include "fsim/semantic/sva_plan.hpp"
#include "simir_kernel_word_ops.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <string>
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
    /// A concurrent assertion outcome or pending report (the HostRequest at
    /// SimIR index x of the member, an SvaOutcomeRequest other than the
    /// registration and attempt-start reports, or an SvaPendingRequest):
    /// appends one row of StaticKernelSvaReports (sva_report_* words) with
    /// the time step, its delta, the running thread's process and the start
    /// and fail position of the attempt the member popped last (the largest
    /// h_pop_serial among its aux directive blocks, IEEE 1800-2023 39.4.2).
    /// The host delivers the rows in order at the end of the region and
    /// before any later output (Impl::drain_host_request_rows). Only a full
    /// buffer calls the helper (fsim_sk_sva_report_grow).
    sva_report,
    /// A queue or dynamic array of packed elements of at most 64 bits held
    /// in a kernel container record of storage `dynamic` (IEEE 1800-2023
    /// 7.5, 7.10): the operation `sub` (DynOp) on container aux (a kernel
    /// container index, bound per instance like the mem_* containers). d is
    /// the destination register, x the source register, y the index
    /// register (width bits; flag_signed: a signed index), z a second
    /// container (the copy source or the new[] initializer; no_container
    /// when none) and offset the element width. The elements live in the
    /// container's StaticKernelDynamicContainer; only capacity growth calls
    /// the runtime (fsim_sk_dyn_grow).
    dyn,
    /// A string operation `sub` (StrOp; IEEE 1800-2023 6.16, 11.4) on the
    /// member's string registers, which live in Member::strings: aux is the
    /// member's string file (a kernel container record of storage
    /// `strings`, bound per instance), whose `elements` points at the first
    /// std::string. String register fields name string registers; the
    /// others are kernel registers (see StrOp). Only capacity growth calls
    /// the runtime (fsim_sk_str_reserve).
    str,
    /// RandomValue (IEEE 1800-2023 18.13, 20.15): d = the next draw of
    /// RandomKind `sub` from the running process's stream (a fork child's
    /// own, 18.14) or, for $random, the system seed. $urandom_range reads
    /// its maximum from x (imm_a bit 0) and its minimum from y (imm_a bit
    /// 1); imm_b holds their widths (x's low, y's high).
    random,
    /// Real, shortreal and time arithmetic, comparisons and conversions
    /// (sub 0, SystemVerilogScalarBinary; IEEE 1800-2023 6.12, 11.3.1) and
    /// the math and conversion functions (sub 1, SystemVerilogMath; 20.5,
    /// 20.8) on the payloads in registers x and y (ScalarFields in imm_a;
    /// imm_b holds x's width low and y's high), into d (width bits). A
    /// failure the reference reports reaches fsim_sk_scalar_fail.
    scalar,
    /// A code coverage counter update (CodeCoverageHit, CodeCoverageToggle;
    /// the reference's CodeCoverageCounters::record). Sub bit 0: a toggle
    /// of the value in x to the value in y (imm_b bits; sub bit 2, the
    /// chunks of wide registers x and y), bit i
    /// rising counting CompiledBody::coverage_counters[imm_a + 2i] and
    /// falling [imm_a + 2i + 1]; clear, one hit of counter imm_a. Sub bit 1:
    /// a native template's binding holds the counters instead: a hit's is
    /// binding z's first word, bit i of a toggle rises into binding z + i's
    /// first word and falls into its second. aux holds the operation's
    /// position, for the reference's failures.
    coverage,
};

/// Number of kernel instruction kinds (KOp values 0 .. coverage).
inline constexpr std::size_t kernel_op_count
    = static_cast<std::size_t>(KOp::coverage) + 1U;

/// KOp::coverage sub bits: a toggle (else a hit), counters in bindings,
/// and a toggle of chunked wide registers (x and y their storage).
inline constexpr std::uint8_t coverage_toggle = 1U;
inline constexpr std::uint8_t coverage_bound = 2U;
inline constexpr std::uint8_t coverage_wide = 4U;

/// KOp::scalar's imm_a: the operator or function, the operands' and the
/// result's scalar kinds (SystemVerilogScalarKind values), the operands'
/// signedness, and whether y is read.
struct ScalarFields {
    std::uint8_t operation { };
    std::uint8_t first_kind { };
    std::uint8_t second_kind { };
    std::uint8_t result_kind { };
    bool first_signed { };
    bool second_signed { };
    bool has_second { };
};

[[nodiscard]] constexpr std::uint64_t scalar_pack(const ScalarFields& fields) noexcept
{
    return std::uint64_t { fields.operation } | (std::uint64_t { fields.first_kind } << 8U)
        | (std::uint64_t { fields.second_kind } << 16U)
        | (std::uint64_t { fields.result_kind } << 24U)
        | (fields.first_signed ? std::uint64_t { 1 } << 32U : 0U)
        | (fields.second_signed ? std::uint64_t { 1 } << 33U : 0U)
        | (fields.has_second ? std::uint64_t { 1 } << 34U : 0U);
}

[[nodiscard]] constexpr ScalarFields scalar_fields(const std::uint64_t packed) noexcept
{
    return { static_cast<std::uint8_t>(packed), static_cast<std::uint8_t>(packed >> 8U),
        static_cast<std::uint8_t>(packed >> 16U), static_cast<std::uint8_t>(packed >> 24U),
        ((packed >> 32U) & 1U) != 0U, ((packed >> 33U) & 1U) != 0U,
        ((packed >> 34U) & 1U) != 0U };
}

/// KOp::str operations (KInst::sub). Kernel registers as elsewhere: d the
/// one written (a numeric result), y an index (width bits, flag_signed:
/// signed) and x a byte read; string registers in offset (the string
/// written) and z and imm_b (strings read).
enum class StrOp : std::uint8_t {
    /// String offset = string z.
    copy,
    /// String offset = the bytes [imm_a, imm_a + imm_b) of
    /// CompiledBody::string_bytes.
    load_constant,
    /// String offset = the concatenation of the string registers
    /// CompiledBody::string_operands[imm_a .. imm_a + imm_b). width 1: offset
    /// is the first operand (appended in place); width 2: offset is another
    /// operand, so the result is built in the member's scratch string z first.
    concat,
    /// d = (string z == string imm_b) != imm_a (1 bit).
    compare,
    /// d = string z's length (32 bits).
    length,
    /// d = byte y of string z, 0 out of range (32 bits).
    index,
    /// Byte y of string offset = the low byte of register x (imm_a bits),
    /// unless y is out of range or the byte is zero. A byte with unknown
    /// bits is an error, or (imm_b 1, putc) leaves the string unchanged.
    replace_byte,
    /// String offset = string z in upper case (imm_a 1) or lower case
    /// (toupper, tolower; 6.16.4, 6.16.5).
    change_case,
    /// d = -1, 0 or 1 (32 bits) as string z orders before, equal to or
    /// after string imm_b, folding letters to lower case when imm_a is 1
    /// (compare, icompare; 6.16.8, 6.16.9).
    order,
    /// String offset = bytes y .. x of string z, or the empty string when
    /// the range is unknown or not inside the string (substr; 6.16.10).
    /// The index registers are 32-bit integers (y: width bits, x: imm_a
    /// bits; at most 32, a 32-bit one signed).
    substr,
    /// d = the integer string z spells in radix imm_a (32 bits; atoi,
    /// atohex, atooct, atobin; 6.16.11).
    to_integer,
    /// String offset = register y (an integer as for substr) in radix
    /// imm_a, signed in radix 10, 0 when unknown (itoa, hextoa, octtoa,
    /// bintoa; 6.16.12).
    from_integer,
    /// imm_a 0 or 1: d = 1 when a plusarg (its leading `+` dropped) starts
    /// with string z, else 0 (32 bits; $test$plusargs, select_plusarg);
    /// with imm_a 1 string offset is also set to the matching plusarg, or
    /// the empty string. imm_a 2: d = $reset_count (imm_b 1) or
    /// $reset_value (imm_b 2) (IEEE 1800-2023 D.8); no string is used.
    plusarg,
    /// String offset = string variable imm_a (a StringObjectId; IEEE
    /// 1800-2023 6.16), read in place (StaticKernelStringCell).
    read_object,
    /// String variable imm_a = string z (a blocking write).
    write_object,
    /// String variable imm_a takes string z in the NBA region (a
    /// nonblocking write, IEEE 1800-2023 10.4.2): queued among the
    /// thread's nonblocking updates in execution order (fsim_sk_str_nba).
    write_object_nba,
    /// String offset += one formatted value: an argument of $sformatf,
    /// $sformat and $swrite (IEEE 1800-2023 21.3.3, 21.2.1) or a VHDL TextIO
    /// WRITE (IEEE 1076-2019 16.4): the StringMethod format record
    /// CompiledBody::string_formats[imm_a .. imm_a + 3)
    /// (string_format_method) applied to register x of width bits (the
    /// storage of a chunked register when flags has str_format_wide, its
    /// 'U' and weak masks after the chunks with str_format_masks; width 0
    /// reads no value) or to string z, with a dynamic field width in the
    /// narrow register y of imm_b bits, by the reference's
    /// execute_string_format (fsim_sk_str_format in generated code, a pure
    /// fsim formatter).
    format,
    /// String offset = TO_HSTRING (imm_a bit 0) or TO_OSTRING of the
    /// element characters in string z, sign extended when imm_a has bit 1
    /// (IEEE 1076-2019 16.7; vhdl_logic9_radix_string, fsim_sk_str_radix in
    /// generated code, a pure fsim formatter).
    radix,
    /// A StringMethod value parse or split (str_method_operation: 'VALUE
    /// of an integer, enumeration, physical or real type, the elements of a
    /// composite value's string representation, an allocator's fill; IEEE
    /// 1076-2019 16.2.2, 9.3.7; atoreal, IEEE 1800-2023 6.16.11) by the
    /// reference's execute_string_method (fsim_sk_str_method in generated
    /// code, a pure fsim routine): the operator in imm_a bits 0-7, string z
    /// the source, string imm_b the argument (flags str_method_argument),
    /// the integer registers x (first, imm_a bits 8-15 its width) and y
    /// (second, bits 16-23) when flags has str_method_first/second; the
    /// result's 64 bits in d (str_method_scalar) or its string in string
    /// offset (str_method_string).
    method,
};

/// KOp::str method flags.
inline constexpr std::uint8_t str_method_first = 1U;
inline constexpr std::uint8_t str_method_second = 2U;
inline constexpr std::uint8_t str_method_argument = 4U;
inline constexpr std::uint8_t str_method_scalar = 8U;
inline constexpr std::uint8_t str_method_string = 16U;

/// KOp::str format: register x is a chunked wide register.
inline constexpr std::uint8_t str_format_wide = 1U;
/// KOp::str format: the chunked register x has 'U' and weak masks.
inline constexpr std::uint8_t str_format_masks = 2U;
/// The words of one StringMethod format record in
/// CompiledBody::string_formats.
inline constexpr std::size_t string_format_words = 3U;

/// A StringMethod format operation (format_packed, format_string,
/// format_time, format_vhdl_time and the dynamic field forms) as the three
/// words string_format_method reads back, with the value kinds of its value
/// register and of its field width register (string_format_value_kind,
/// string_format_width_kind).
[[nodiscard]] inline std::array<std::uint64_t, string_format_words>
string_format_record(const StringMethod& method, const ValueKind value_kind,
    const ValueKind width_kind) noexcept
{
    const auto flag = [](const bool value, const unsigned bit) {
        return value ? std::uint64_t { 1 } << bit : std::uint64_t { 0 };
    };
    return {
        static_cast<std::uint64_t>(method.operation)
            | static_cast<std::uint64_t>(method.format) << 8U
            | static_cast<std::uint64_t>(method.scalar_kind) << 16U
            | flag(method.signed_decimal, 24U) | flag(method.suppress_leading_zero, 25U)
            | flag(method.left_justify, 26U) | flag(method.zero_pad, 27U)
            | flag(method.use_timeformat_width, 28U) | flag(method.time_signed, 29U)
            | static_cast<std::uint64_t>(method.minimum_width) << 32U,
        method.precision | static_cast<std::uint64_t>(value_kind) << 32U
            | static_cast<std::uint64_t>(width_kind) << 40U,
        method.time_unit_femtoseconds,
    };
}

[[nodiscard]] inline ValueKind string_format_value_kind(const std::uint64_t* words) noexcept
{
    return static_cast<ValueKind>((words[1] >> 32U) & 0xffU);
}

[[nodiscard]] inline ValueKind string_format_width_kind(const std::uint64_t* words) noexcept
{
    return static_cast<ValueKind>((words[1] >> 40U) & 0xffU);
}

/// The StringMethod operations KOp::str method carries out: pure functions
/// of the source string, the argument string and two integers.
[[nodiscard]] inline bool str_method_operation(const StringMethodOperator operation) noexcept
{
    using M = StringMethodOperator;
    return operation == M::vhdl_integer_value || operation == M::vhdl_enumeration_value
        || operation == M::vhdl_physical_value || operation == M::atoreal
        || operation == M::vhdl_composite_element || operation == M::vhdl_character_element
        || operation == M::vhdl_string_fill;
}

/// The StringMethod format operations KOp::str format carries out.
[[nodiscard]] inline bool string_format_operation(const StringMethodOperator operation) noexcept
{
    using M = StringMethodOperator;
    return operation == M::format_packed || operation == M::format_string
        || operation == M::format_time || operation == M::format_vhdl_time
        || operation == M::format_packed_dynamic_field
        || operation == M::format_string_dynamic_field;
}

[[nodiscard]] inline StringMethod string_format_method(const std::uint64_t* words) noexcept
{
    const auto bit = [&](const unsigned at) { return ((words[0] >> at) & 1U) != 0U; };
    StringMethod method;
    method.operation = static_cast<StringMethodOperator>(words[0] & 0xffU);
    method.format = static_cast<OutputFormat>((words[0] >> 8U) & 0xffU);
    method.scalar_kind = static_cast<SystemVerilogScalarKind>((words[0] >> 16U) & 0xffU);
    method.signed_decimal = bit(24U);
    method.suppress_leading_zero = bit(25U);
    method.left_justify = bit(26U);
    method.zero_pad = bit(27U);
    method.use_timeformat_width = bit(28U);
    method.time_signed = bit(29U);
    method.minimum_width = static_cast<std::uint32_t>(words[0] >> 32U);
    method.precision = static_cast<std::uint32_t>(words[1]);
    method.time_unit_femtoseconds = words[2];
    return method;
}

[[nodiscard]] inline bool str_writes_string(const StrOp op) noexcept
{
    return op == StrOp::copy || op == StrOp::load_constant || op == StrOp::concat
        || op == StrOp::replace_byte || op == StrOp::change_case || op == StrOp::substr
        || op == StrOp::from_integer || op == StrOp::radix;
}

/// Writes register d.
[[nodiscard]] inline bool str_writes_register(const StrOp op) noexcept
{
    return op == StrOp::compare || op == StrOp::length || op == StrOp::index
        || op == StrOp::order || op == StrOp::to_integer || op == StrOp::plusarg;
}

/// Reads index register y.
[[nodiscard]] inline bool str_reads_index(const StrOp op) noexcept
{
    return op == StrOp::index || op == StrOp::replace_byte || op == StrOp::substr
        || op == StrOp::from_integer;
}

/// Reads register x.
[[nodiscard]] inline bool str_reads_byte(const StrOp op) noexcept
{
    return op == StrOp::replace_byte || op == StrOp::substr;
}


/// KOp::dyn operations (KInst::sub), the SimIR container operations they
/// carry out on a kernel queue or dynamic array.
enum class DynOp : std::uint8_t {
    /// ContainerRead: d = element y.
    read,
    /// ContainerWrite: element y = x (an invalid index writes nothing; the
    /// index one past a queue's last element appends, 7.10.1).
    write,
    /// ContainerSize: d (32 bits) = the element count.
    size,
    /// ResizeContainer: new[x], keeping the elements of container z when
    /// it is not no_container (7.5.1).
    resize,
    /// DeleteContainer without an index.
    delete_all,
    /// DeleteContainer of queue element y (7.10.2.3).
    delete_at,
    /// PushContainer: push_back(x), push_front(x), insert(y, x).
    push_back,
    push_front,
    insert,
    /// PopContainer: d = pop_front(), pop_back().
    pop_front,
    pop_back,
    /// CopyContainerRegister: container aux = container z.
    copy,
    /// The associative operations (integral keys; key in y):
    /// ContainerRead: d = element y, or the default when y is missing.
    assoc_read,
    /// ContainerWrite: element y = x, inserted when missing.
    assoc_write,
    /// ContainerWrite of the `default:` value (7.9.11): fill = x.
    assoc_default,
    /// DeleteContainer with an index: removes key y when present.
    assoc_delete,
    /// ContainerExists: d (32 bits) = whether key y is present.
    exists,
    /// TraverseContainer: d (32 bits) = whether a key was selected, which
    /// is written to y (7.9.4 - 7.9.7); next and prev read y first.
    first,
    last,
    next,
    prev,
    /// The operations of a container of strings (dyn_layout_text): its
    /// elements are text slots (dyn_text_slot), z is the member's string
    /// file and offset a string register.
    /// ContainerStringRead: string offset = element y, the empty string
    /// when y is invalid or out of range (7.4.6).
    text_read,
    /// ContainerStringWrite: element y = string offset (the write rules
    /// of `write`). An associative array's key y (integral) selects the
    /// element, a missing key reading the `default:` text (fill) and a
    /// write inserting it.
    text_write,
    /// ContainerStringWrite of an associative array's `default:` text
    /// (7.9.11): the fill text slot = string offset.
    text_default,
    /// AppendContainer (10.10): imm_b bit 0 empties the target first;
    /// append_clear appends nothing, append_value register x,
    /// append_text string offset (z the string file), append_container
    /// the elements of container z. A bounded queue keeps its first
    /// elements (7.10.5).
    append_clear,
    append_value,
    append_text,
    append_container,
    /// The whole-array operations (7.12, 11.4.11, 11.4.5): the container
    /// is aux.
    /// CompareContainers: d = aux == container z (imm_b bit 0: ===), one
    /// bit, X when unknown bits decide (packed elements); scalar elements
    /// compare as dyn_layout_scalar_* says.
    compare,
    /// ConditionalContainerSelect: aux = register x (one bit) ? container
    /// z : container y; an unknown condition merges them (11.4.11).
    select,
    /// ContainerReduction: d = the ContainerReductionOperator x of the
    /// elements, each first through the predicate graph imm_b
    /// (dyn_graph_begin / dyn_graph_count; none when the count is 0).
    reduce,
    /// OrderContainer: the ContainerOrderingOperator x on aux (reverse,
    /// or a stable sort by the key graph imm_b or the elements, 7.12.2).
    order,
    /// LocateContainer: queue aux = the ContainerLocatorOperator x over
    /// container z: the graph imm_b is the predicate of the find
    /// locators, else the transformation (7.12.1).
    locate,
    /// ContainerBitstream (11.4.14): aux is the bit stream, a queue of
    /// one-bit elements. stream_clear empties it; stream_append_value
    /// appends the `offset` bits of register x and stream_append_container
    /// every bit of container z, most significant bit and leftmost element
    /// first; stream_reverse reverses it in imm_b-bit slices taken from its
    /// right end; stream_to_value / stream_take_value write its first imm_b
    /// bits to d (left-justified; take removes them, to requires every bit
    /// to fit); stream_to_container / stream_take_container fill container
    /// z's elements (resizing a queue or dynamic array; take leaves imm_b
    /// bits for the later targets and removes the bits it used).
    stream_clear,
    stream_append_value,
    stream_append_container,
    stream_reverse,
    stream_to_value,
    stream_take_value,
    stream_to_container,
    stream_take_container,
    /// A flat aggregate object the host bridges to a packed signal (IEEE
    /// 1800-2023 7.2, 37.17; storage packed_signal, container z's info
    /// storage 2): its leaves are bit fields of the slot, the first leaf
    /// leftmost. aggregate_unpack (ReadContainerObject): the leaves of
    /// container aux = the slot's fields; aggregate_pack (a blocking
    /// WriteContainerObject): the slot's fields = container aux's leaves, a
    /// changed field announced as a slot store does. The graph imm_b holds
    /// one node per leaf of an element, its width in the node's first word
    /// (dyn_graph_node_words words a node).
    aggregate_unpack,
    aggregate_pack,
    /// ContainerElementRead / ContainerElementWrite of a flat aggregate
    /// (7.2): the `offset` leaves of the element index y selects in
    /// container z (read) or aux (write), from leaf dyn_aggregate_leaf of
    /// it (a nested structure member), to or from the structure register's
    /// container (aux on a read, z on a write) as a dense fixed-size
    /// array's elements are (dyn_layout_aggregate_box: the indexed
    /// container is a structure value). Register x is the first of the
    /// leaves' nodes in CompiledBody::predicate_words: width | the
    /// zero-initial flag << 32 (an invalid read index reads each leaf's
    /// initial value, 7.4.6).
    aggregate_element_read,
    aggregate_element_write,
};

/// A container predicate graph of CompiledBody::predicate_words, as
/// KInst::imm_b holds it: the first node and the node count.
[[nodiscard]] inline std::uint64_t dyn_graph(const std::uint64_t begin,
    const std::uint64_t count) noexcept
{
    return begin | count << 32U;
}

[[nodiscard]] inline std::uint64_t dyn_graph_begin(const std::uint64_t imm) noexcept
{
    return imm & 0xFFFF'FFFFU;
}

[[nodiscard]] inline std::uint64_t dyn_graph_count(const std::uint64_t imm) noexcept
{
    return imm >> 32U;
}

/// The words of one predicate node in CompiledBody::predicate_words:
/// operator | value kind << 8 | constant width << 16, left | right << 32,
/// third, and the constant's two planes.
inline constexpr std::size_t dyn_graph_node_words = 5U;

/// The failure codes of KOp::dyn (native fail reasons from
/// dyn_fail_first on): the reference operations' messages.
enum class DynFailure : std::uint32_t {
    none = 0U,
    index_unknown,
    index_negative,
    index_range,
    size_unknown,
    array_budget,
    queue_budget,
    write_mismatch,
    queue_write_mismatch,
    out_of_memory,
    key_unknown,
    assoc_budget,
    fixed_unknown,
    fixed_range,
    linear_unknown,
    linear_negative,
    linear_range,
    sparse_budget,
    concat_limit,
    byte_unknown,
    string_limit,
    text_assoc_budget,
    key_bytes,
    stream_short,
    stream_wide,
    stream_partial,
    // Warnings, not failures (dyn_warning): the operation completed. A read
    // of a nonexistent associative-array entry, or with an index holding x
    // or z bits, returned the nonexistent-entry value; a write with such an
    // index was ignored (IEEE 1800-2023 7.8.6).
    missing_entry,
    invalid_read_index,
    invalid_write_index,
    // A flat aggregate's member write of a value the leaf cannot hold
    // (apply_container_aggregate_member).
    aggregate_leaf,
};

/// KInst::imm_b of a DynOp::assoc_read: the read of an update's own target,
/// which allocates a missing entry before referring to it (IEEE 1800-2023
/// 7.8.7): no missing_entry warning.
inline constexpr std::uint64_t dyn_read_allocating = 1U;

/// True for the DynFailure values that are warnings of a completed
/// operation.
[[nodiscard]] constexpr bool dyn_warning(const DynFailure failure) noexcept
{
    return failure == DynFailure::missing_entry
        || failure == DynFailure::invalid_read_index
        || failure == DynFailure::invalid_write_index;
}

/// The native fail reason of the first DynFailure (native_fail_message).
inline constexpr std::uint32_t dyn_fail_first = 32U;

/// KInst::imm_a of KOp::dyn: generated code's layout choice.
inline constexpr std::uint64_t dyn_layout_associative = 1U;
inline constexpr std::uint64_t dyn_layout_signed_keys = 2U;
inline constexpr std::uint64_t dyn_layout_sparse = 4U;
/// A sparse array's index is a linear ordinal (a multidimensional array).
inline constexpr std::uint64_t dyn_layout_linear = 8U;
/// String elements: each element slot is a text slot (dyn_text_slot).
inline constexpr std::uint64_t dyn_layout_text = 16U;
/// String keys: each key slot is a text slot (two words), in std::string
/// order (dyn_text_keys).
inline constexpr std::uint64_t dyn_layout_text_keys = 32U;
/// Signed packed elements (predicate comparisons).
inline constexpr std::uint64_t dyn_layout_signed_elements = 64U;
/// Scalar elements that compare by their bits (time, chandle), as a double
/// (real, realtime) or as a float (shortreal).
inline constexpr std::uint64_t dyn_layout_scalar_bits = 128U;
inline constexpr std::uint64_t dyn_layout_scalar_double = 256U;
inline constexpr std::uint64_t dyn_layout_scalar_float = 512U;
/// A bit-stream operation's container z (its element width in offset):
/// two-state elements (X and Z bits stream in as 0), strings, an
/// associative array.
inline constexpr std::uint64_t dyn_stream_other_two_state = 1024U;
inline constexpr std::uint64_t dyn_stream_other_text = 2048U;
inline constexpr std::uint64_t dyn_stream_other_associative = 4096U;
/// A dense fixed-size unpacked array (IEEE 1800-2023 7.4): its elements in
/// declared order, left first, at ordinals 0 .. size - 1 (the dynamic
/// array layout). An index is a declared index (the declared range in
/// imm_b, left in the low half, as a sparse array's) and an iterator index
/// (item.index, an index locator's result) is the declared index of the
/// ordinal: imm_a's high half holds the left bound, dyn_layout_descending
/// whether the left bound is the higher one (container_declared_index).
inline constexpr std::uint64_t dyn_layout_fixed = 8192U;
inline constexpr std::uint64_t dyn_layout_descending = 16384U;
/// A flat unpacked aggregate's member read or write (DynOp::read, write of
/// ContainerAggregateRead, ContainerAggregateWrite; IEEE 1800-2023 7.2): the
/// container holds each element's leaves (flat_aggregate_type), and the
/// member is leaf dyn_aggregate_leaf(imm_a) of the element the index
/// selects, among dyn_aggregate_leaves(imm_a) leaves per element (imm_a's
/// high half; the index as a dense fixed-size array's). A structure value
/// (dyn_layout_aggregate_box) is its own element: the index is ignored.
/// An unknown or out-of-range read index reads the leaf type's initial
/// value: zero for a two-state or real leaf (dyn_layout_leaf_zero), else
/// all X (the leaf's width in offset). A write of a value with X or Z bits
/// to a two-state leaf (dyn_layout_leaf_two_state) fails (aggregate_leaf).
inline constexpr std::uint64_t dyn_layout_aggregate = 32768U;
inline constexpr std::uint64_t dyn_layout_aggregate_box = 65536U;
inline constexpr std::uint64_t dyn_layout_leaf_zero = 131072U;
inline constexpr std::uint64_t dyn_layout_leaf_two_state = 262144U;
[[nodiscard]] constexpr std::uint64_t dyn_aggregate_leaf(const std::uint64_t imm_a) noexcept
{
    return (imm_a >> 32U) & 0xFFFFU;
}
[[nodiscard]] constexpr std::uint64_t dyn_aggregate_leaves(const std::uint64_t imm_a) noexcept
{
    return imm_a >> 48U;
}

/// A string element of a kernel container: one element slot (two words)
/// holding the element's bytes, which the slot owns (malloc'd, no
/// terminator), and its length and capacity. Slots move by copying their
/// words; a slot keeps its block while the element count shrinks, so
/// generated code allocates only to grow a block (fsim_sk_dyn_text_reserve).
struct DynTextSlot {
    char* data { };
    std::uint32_t size { };
    std::uint32_t capacity { };
};
static_assert(sizeof(DynTextSlot) == 16U);

/// The registers a KOp::dyn operation reads (y the index, x the source)
/// and writes (d).
[[nodiscard]] inline bool dyn_reads_index(const DynOp op) noexcept
{
    return op == DynOp::read || op == DynOp::write || op == DynOp::delete_at
        || op == DynOp::insert || op == DynOp::assoc_read || op == DynOp::assoc_write
        || op == DynOp::assoc_delete || op == DynOp::exists || op == DynOp::next
        || op == DynOp::prev || op == DynOp::text_read || op == DynOp::text_write
        || op == DynOp::aggregate_element_read || op == DynOp::aggregate_element_write;
}

[[nodiscard]] inline bool dyn_reads_source(const DynOp op) noexcept
{
    return op == DynOp::write || op == DynOp::resize || op == DynOp::push_back
        || op == DynOp::push_front || op == DynOp::insert || op == DynOp::assoc_write
        || op == DynOp::assoc_default || op == DynOp::append_value || op == DynOp::select
        || op == DynOp::stream_append_value;
}

/// Whether KInst::y is a third container (bound like aux and z).
[[nodiscard]] inline bool dyn_third_container(const DynOp op) noexcept
{
    return op == DynOp::select;
}

/// Whether KInst::imm_b is a predicate graph (dyn_graph).
[[nodiscard]] inline bool dyn_has_graph(const DynOp op) noexcept
{
    return op == DynOp::reduce || op == DynOp::order || op == DynOp::locate
        || op == DynOp::aggregate_unpack || op == DynOp::aggregate_pack;
}

/// The whole-array operations, which the bytecode tier runs through the
/// reference's routines (execute_dyn_whole).
[[nodiscard]] inline bool dyn_whole(const DynOp op) noexcept
{
    return op == DynOp::compare || op == DynOp::select || op == DynOp::reduce
        || op == DynOp::order || op == DynOp::locate;
}

/// The bit-stream operations (ContainerBitstream), which the bytecode tier
/// runs through the reference's routine (execute_dyn_stream).
[[nodiscard]] inline bool dyn_stream(const DynOp op) noexcept
{
    return op >= DynOp::stream_clear && op <= DynOp::stream_take_container;
}

/// Container z is the member's string file (not a second container).
[[nodiscard]] inline bool dyn_uses_string_file(const DynOp op) noexcept
{
    return op == DynOp::text_read || op == DynOp::text_write || op == DynOp::text_default
        || op == DynOp::append_text;
}

[[nodiscard]] inline bool dyn_writes_destination(const DynOp op) noexcept
{
    return op == DynOp::read || op == DynOp::size || op == DynOp::pop_front
        || op == DynOp::pop_back || op == DynOp::assoc_read || op == DynOp::exists
        || op == DynOp::first || op == DynOp::last || op == DynOp::next
        || op == DynOp::prev || op == DynOp::compare || op == DynOp::reduce
        || op == DynOp::stream_to_value || op == DynOp::stream_take_value;
}

/// A traversal also writes the selected key to its index register y.
[[nodiscard]] inline bool dyn_writes_index(const DynOp op) noexcept
{
    return op == DynOp::first || op == DynOp::last || op == DynOp::next
        || op == DynOp::prev;
}

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

/// KOp::str format reading the narrow register x (a wide one is chunked
/// storage, which no use list names).
[[nodiscard]] inline bool str_format_reads_narrow(const KInst& inst) noexcept
{
    return static_cast<StrOp>(inst.sub) == StrOp::format && inst.width != 0U
        && (inst.flags & str_format_wide) == 0U;
}

/// KOp::str method reading its first integer from register x.
[[nodiscard]] inline bool str_method_reads_first(const KInst& inst) noexcept
{
    return static_cast<StrOp>(inst.sub) == StrOp::method
        && (inst.flags & str_method_first) != 0U;
}

/// KOp::str method reading its second integer from register y.
[[nodiscard]] inline bool str_method_reads_second(const KInst& inst) noexcept
{
    return static_cast<StrOp>(inst.sub) == StrOp::method
        && (inst.flags & str_method_second) != 0U;
}

/// KOp::str writing register d.
[[nodiscard]] inline bool str_inst_writes_register(const KInst& inst) noexcept
{
    const auto op = static_cast<StrOp>(inst.sub);
    return str_writes_register(op)
        || (op == StrOp::method && (inst.flags & str_method_scalar) != 0U);
}

/// KOp::str format reading a dynamic field width from the narrow register
/// y (imm_b bits).
[[nodiscard]] inline bool str_format_reads_width(const KInst& inst) noexcept
{
    return static_cast<StrOp>(inst.sub) == StrOp::format && inst.imm_b != 0U;
}

/// An associative array with string keys (dyn_layout_text_keys): its key
/// operand y is a string register of the member's string file (z), not a
/// register, and a traversal writes that string.
[[nodiscard]] inline bool dyn_text_keys(const KInst& inst) noexcept
{
    return inst.op == KOp::dyn && (inst.imm_a & dyn_layout_text_keys) != 0U;
}

/// dyn_reads_index / dyn_writes_index of an instruction: never for string
/// keys, which are string registers.
[[nodiscard]] inline bool dyn_reads_index(const KInst& inst) noexcept
{
    return !dyn_text_keys(inst) && dyn_reads_index(static_cast<DynOp>(inst.sub));
}

[[nodiscard]] inline bool dyn_writes_index(const KInst& inst) noexcept
{
    return !dyn_text_keys(inst) && dyn_writes_index(static_cast<DynOp>(inst.sub));
}

/// Whether z of the instruction is the member's string file: a string
/// element or `default:` operand, or a string key.
[[nodiscard]] inline bool dyn_uses_string_file(const KInst& inst) noexcept
{
    const auto op = static_cast<DynOp>(inst.sub);
    return dyn_uses_string_file(op)
        || (dyn_text_keys(inst) && (dyn_reads_index(op) || dyn_writes_index(op)));
}

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
/// The source count carries net_user_bit when the slot is a VHDL signal
/// with a resolution function of the design's own (Slot::user): a VHDL
/// assignment stores its source only, and the update phase runs the
/// function over the sources (StaticKernel::resolve_user_slot).
inline constexpr std::uint32_t net_user_bit = 0x40000000U;

struct NetShape {
    std::uint32_t sources { };
    std::uint32_t own { };
    std::uint32_t width { };
    /// The resolved value has nine states (four code planes), resolved by
    /// the std_logic_1164 table over the sources that drive each element.
    bool nine { };
    /// The slot is resolved by a resolution function of the design's own
    /// (net_user_bit): generated code never resolves it.
    bool user { };

    /// The planes of the slot's value, before its sources' planes.
    [[nodiscard]] std::uint32_t rows() const noexcept
    {
        return nine ? 4U : 2U;
    }
};

/// The shape of an immediate's source count (with net_nine_bit and
/// net_user_bit), the
/// member's source and the slot's width.
[[nodiscard]] constexpr NetShape make_net_shape(const std::uint32_t counted,
    const std::uint32_t own, const std::uint32_t width) noexcept
{
    return NetShape { counted & ~(net_nine_bit | net_user_bit), own, width,
        (counted & net_nine_bit) != 0U, (counted & net_user_bit) != 0U };
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
    /// s_nexttime, s_always, s_eventually, s_until (16.12): a strong
    /// operator, which a recursive plan's weak nexttime alone forwards past.
    bool strong { };
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

inline constexpr std::uint32_t no_variable_store = 0xffffffffU;

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
    /// KOp::str: concatenation operand lists (string registers) and the
    /// bytes of string constants.
    std::vector<std::uint32_t> string_operands;
    std::string string_bytes;
    /// KOp::coverage toggles: each bit's rise and fall counters (imm_a the
    /// first); a native template binds them instead.
    std::vector<std::uint32_t> coverage_counters;
    /// KOp::str format: StringMethod format records (string_format_words
    /// words each).
    std::vector<std::uint64_t> string_formats;
    /// KOp::dyn: container predicate graphs (dyn_graph_node_words words a
    /// node).
    std::vector<std::uint64_t> predicate_words;
    /// The plans of the directives whose tick is generated code
    /// (KOp::sva_tick, z the index plus one).
    std::vector<SvaNativePlan> sva_plans;
    /// Return targets (SimIR operation indices) a ret may reach, for the
    /// native dispatch.
    std::vector<std::uint32_t> return_targets;
    /// VHDL: registers live at the entry (one bit each); only these must
    /// carry exact values from one activation to the next.
    std::vector<std::uint64_t> live_at_entry;
    /// By instruction: the register of the VHDL variable (a debug local of
    /// the member's program) it stores, or no_variable_store; empty in a
    /// body that stores none. While the debug interface forces or watches a
    /// variable of the member (StaticKernel::variable_hook_members_), every
    /// executor reports these stores (StaticKernel::note_variable_store,
    /// IEEE 1076-2019 22.5.2, 21.3.2.2).
    std::vector<std::uint32_t> variable_stores;
    /// By instruction: the kernel dynamic object (a design queue, dynamic,
    /// associative or sparse array of storage dynamic, by container) whose
    /// value it leaves changed (a write-back of a binding, an element
    /// write), or no_variable_store; empty in a body that writes none.
    /// Code compiled for an observer (StaticKernelTemplate::observer_hooks)
    /// calls `fsim_sk_container_publish` after them, so a host observer of
    /// the object (a VPI word or value-change watch, IEEE 1800-2023 37.17,
    /// 38.36.1) hears each change.
    std::vector<std::uint32_t> container_publishes;
    /// By instruction: nonzero where a statement of a SystemVerilog member
    /// begins (a debug point of the program, a no-op without the debug
    /// plan) that may follow a store: its code-order predecessor stores a
    /// signal or calls, or a jump enters it; empty in a body without any.
    /// While an observer watches a signal, the writes of the statements
    /// before reach the host there (StaticKernel::flush_debug_writes), so
    /// each blocking assignment is its own value change, as the reference
    /// reports it, and the stores of one continuous assignment's update one
    /// change (IEEE 1800-2023 38.36.1).
    std::vector<std::uint8_t> statement_starts;
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

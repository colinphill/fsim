// SPDX-License-Identifier: Apache-2.0
//
// Helpers shared by the compiled-tier translation units of the engine v4
// static kernel (simir_static_kernel_compiled*.cpp).
#pragma once

#include "simir_static_kernel_state.hpp"
#include "simir_container_helpers.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fsim::runtime::simir::static_kernel_compiled_detail {

using namespace static_kernel_detail;
namespace kw = kernel_word;


/// Behavioral operations at which a thread suspends: other threads may run
/// (and change any state) before it resumes.
template <typename T>
inline constexpr bool behavioral_suspension = std::is_same_v<T, WaitFor>
    || std::is_same_v<T, WaitOn> || std::is_same_v<T, WaitForever>
    || std::is_same_v<T, Fork> || std::is_same_v<T, ForkEnd>
    || std::is_same_v<T, Stop> || std::is_same_v<T, Pause>
    // A disable may end or move other threads, or this one.
    || std::is_same_v<T, DisableBlock>
    || std::is_same_v<T, DisableFork> || std::is_same_v<T, WaitFork>
    // await waits for another process; kill may end this thread or others.
    || std::is_same_v<T, ProcessAwait> || std::is_same_v<T, ProcessKill>
    // suspend may stop this thread; resume readies another.
    || std::is_same_v<T, ProcessSuspend> || std::is_same_v<T, ProcessResume>
    // A yield resumes in the next delta.
    || std::is_same_v<T, Yield>
    // Mailbox, semaphore and dynamic-event operations may block, and their
    // puts and triggers resume other threads (Impl::run_synchronization).
    || std::is_same_v<T, MailboxCreate> || std::is_same_v<T, MailboxPut>
    || std::is_same_v<T, MailboxGet> || std::is_same_v<T, MailboxNum>
    || std::is_same_v<T, SemaphoreCreate> || std::is_same_v<T, SemaphoreGet>
    || std::is_same_v<T, SemaphorePut> || std::is_same_v<T, DynamicEvent>
    // A move to a later scheduler region (IEEE 1800-2023 4.4.2, 16.4): other
    // processes run in the regions between.
    || std::is_same_v<T, WaitRegion>;

[[nodiscard]] inline bool comparison(const BinaryOperator operation) noexcept
{
    switch (operation) {
    case BinaryOperator::equal:
    case BinaryOperator::case_equal:
    case BinaryOperator::casez_equal:
    case BinaryOperator::casex_equal:
    case BinaryOperator::wildcard_equal:
    case BinaryOperator::not_equal:
    case BinaryOperator::less_unsigned:
    case BinaryOperator::less_equal_unsigned:
    case BinaryOperator::greater_unsigned:
    case BinaryOperator::greater_equal_unsigned:
    case BinaryOperator::less_signed:
    case BinaryOperator::less_equal_signed:
    case BinaryOperator::greater_signed:
    case BinaryOperator::greater_equal_signed:
    case BinaryOperator::vhdl_match_equal:
        return true;
    default:
        return false;
    }
}

/// A ReadSignal of the assertion services (IEEE 1800-2023 16.5.1, 16.9.3):
/// a sampled value, a sampled-value function ($past, $rose, ...) or a
/// clocked or gated history read. Its value is the host's sampled history,
/// not the signal's current value. `$future_gclk` (16.9.4) is the signal's
/// current value, a plain read whatever clock it names.
[[nodiscard]] inline bool sampled_read(const ReadSignal& read) noexcept
{
    if (read.kind == SignalReadKind::future) {
        return false;
    }
    return read.kind != SignalReadKind::current || read.ticks != 1U
        || read.clock || read.gate;
}

/// The width of a ReadSignal's result: $rose, $fell, $stable, $changed and
/// their future forms are one bit (run_sampled_read); every other read has
/// the signal's width.
[[nodiscard]] inline std::uint32_t sampled_read_width(const ReadSignal& read,
    const std::uint32_t signal_width) noexcept
{
    switch (read.kind) {
    case SignalReadKind::rose:
    case SignalReadKind::fell:
    case SignalReadKind::stable:
    case SignalReadKind::changed:
    case SignalReadKind::rising:
    case SignalReadKind::falling:
    case SignalReadKind::steady:
    case SignalReadKind::changing:
        return 1U;
    default:
        return signal_width;
    }
}

struct CompileFailure {
    const char* reason;
};

/// Register operands of the operations the kernel supports, for one
/// operation kind.
template <typename T>
inline void operation_registers_of(const T& op, std::vector<RegisterId>& reads,
    std::optional<RegisterId>& write)
{
    reads.clear();
    write.reset();
    {
        if constexpr (std::is_same_v<T, LoadConstant>
            || std::is_same_v<T, ReadSignal>
            // The attributes of a host signal (IEEE 1076-2019 16.2.4) store
            // their result through the kernel's operation step.
            || std::is_same_v<T, SignalEvent>
            || std::is_same_v<T, SignalActive>
            || std::is_same_v<T, SignalLastValue>
            || std::is_same_v<T, SignalLastEvent>
            || std::is_same_v<T, SignalLastActive>) {
            write = op.destination;
        } else if constexpr (std::is_same_v<T, CopyRegister>
            || std::is_same_v<T, UnaryNot>
            || std::is_same_v<T, ConvertToTwoState>
            || std::is_same_v<T, LogicalNot>
            || std::is_same_v<T, Reduction>
            || std::is_same_v<T, CountOnes>
            || std::is_same_v<T, CountBits>
            || std::is_same_v<T, Extract>) {
            reads.push_back(op.source);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Binary>
            || std::is_same_v<T, LogicalBinary>) {
            reads.push_back(op.lhs);
            reads.push_back(op.rhs);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Shift>) {
            reads.push_back(op.value);
            reads.push_back(op.amount);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, DynamicExtract>) {
            reads.push_back(op.source);
            reads.push_back(op.selection.index);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, DynamicPartSelect>) {
            reads.push_back(op.source);
            reads.push_back(op.base);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Insert>) {
            reads.push_back(op.target);
            reads.push_back(op.source);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, DynamicInsert>) {
            reads.push_back(op.target);
            reads.push_back(op.source);
            reads.push_back(op.selection.index);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, DynamicPartInsert>) {
            reads.push_back(op.target);
            reads.push_back(op.source);
            reads.push_back(op.selection.base);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Concatenate>) {
            reads.insert(reads.end(), op.operands.begin(), op.operands.end());
            write = op.destination;
        } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
            reads.push_back(op.condition);
            reads.push_back(op.when_true);
            reads.push_back(op.when_false);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Branch>) {
            reads.push_back(op.condition);
        } else if constexpr (std::is_same_v<T, FormatDisplay>) {
            reads.push_back(op.source);
        } else if constexpr (std::is_same_v<T, SvaOperation>) {
            // A concurrent assertion's monitor (IEEE 1800-2023 16.14): a tick
            // reads the packed guards, the start and disable flags and (at
            // its first tick) the delay and repetition bounds; an outcome
            // query reads nothing. Both write the 32 bit result.
            if (op.kind == SvaKind::tick) {
                if (op.guard_count != 0U) {
                    reads.push_back(op.guards);
                }
                reads.push_back(op.start);
                reads.push_back(op.disabled);
                reads.insert(reads.end(), op.bounds.begin(), op.bounds.end());
            }
            write = op.result;
        } else if constexpr (std::is_same_v<T, PlusArgSelect>) {
            write = op.destination;
        } else if constexpr (std::is_same_v<T, StringMethod>) {
            // The reference's execute_string_method and execute_string_format
            // (StaticKernel::behavioral_step): which registers an operator
            // reads and which it writes.
            using M = StringMethodOperator;
            const auto method = op.operation;
            const bool dynamic_packed = method == M::format_packed_dynamic_field;
            const bool dynamic_string = method == M::format_string_dynamic_field;
            if (method == M::format_packed || dynamic_packed || dynamic_string
                || method == M::format_string || method == M::format_time
                || method == M::format_vhdl_time) {
                if (method == M::format_packed || dynamic_packed
                    || dynamic_string || method == M::format_vhdl_time
                    || (method == M::format_time && op.time_unit_femtoseconds != 0U)) {
                    reads.push_back(op.first);
                }
                if (dynamic_packed) {
                    reads.push_back(op.second);
                }
            } else {
                const bool element = method == M::vhdl_composite_element
                    || method == M::vhdl_character_element
                    || method == M::vhdl_string_fill;
                if (method == M::getc || method == M::putc || method == M::substr
                    || element || (method >= M::itoa && method <= M::realtoa)) {
                    reads.push_back(op.first);
                }
                if (method == M::putc || method == M::substr || element) {
                    reads.push_back(op.second);
                }
                if (method == M::getc || method == M::compare
                    || method == M::icompare || method == M::atoi
                    || method == M::atohex || method == M::atooct
                    || method == M::atobin || method == M::atoreal
                    || method == M::vhdl_enumeration_value
                    || method == M::vhdl_physical_value
                    || method == M::vhdl_integer_value) {
                    write = op.destination;
                }
            }
        } else if constexpr (std::is_same_v<T, StringLength>
            || std::is_same_v<T, CompareStrings>) {
            write = op.destination;
        } else if constexpr (std::is_same_v<T, StringIndex>) {
            reads.push_back(op.index);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, StringReport>) {
            // A VHDL report with a computed message: the severity register.
            reads.push_back(op.severity);
        } else if constexpr (std::is_same_v<T, StringReplaceByte>) {
            reads.push_back(op.index);
            reads.push_back(op.source);
        } else if constexpr (std::is_same_v<T, FileOpen>) {
            // The handle register is read as well (a VHDL file already open).
            reads.push_back(op.destination);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, FileClose>) {
            reads.push_back(op.handle);
            if (op.clear_handle) {
                write = op.handle;
            }
        } else if constexpr (std::is_same_v<T, FileWriteLiteral>
            || std::is_same_v<T, FileWriteString>) {
            reads.push_back(op.handle);
        } else if constexpr (std::is_same_v<T, FileFlush>) {
            if (!op.all) {
                reads.push_back(op.handle);
            }
        } else if constexpr (std::is_same_v<T, FileWriteFormatted>) {
            reads.push_back(op.handle);
            reads.push_back(op.source);
        } else if constexpr (std::is_same_v<T, FileEndOfFile>
            || std::is_same_v<T, FileErrorStatus>) {
            reads.push_back(op.handle);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, FileReadLine>) {
            reads.push_back(op.handle);
            if (op.kind == FileReadKind::unget) {
                reads.push_back(op.source);
            }
            write = op.destination;
        } else if constexpr (std::is_same_v<T, FileScan>) {
            // The handle of a file scan (a string source is a string
            // register); the targets are written (operation_written_registers).
            if (!op.string_source) {
                reads.push_back(op.handle);
            }
            write = op.destination;
        } else if constexpr (std::is_same_v<T, FilePosition>) {
            reads.push_back(op.handle);
            if (file_position_uses_operands(op.kind)) {
                reads.push_back(op.offset);
                reads.push_back(op.origin);
            }
            write = op.destination;
        } else if constexpr (std::is_same_v<T, FileBinaryRead>) {
            reads.push_back(op.handle);
            if (op.has_start) {
                reads.push_back(op.start);
            }
            if (op.has_count) {
                reads.push_back(op.count);
            }
            write = op.destination;
        } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
            // The time queries read no register.
            if (op.function < SystemVerilogMathFunction::Time) {
                reads.push_back(op.first);
                if (op.second_width != 0U) {
                    reads.push_back(op.second);
                }
            }
            write = op.destination;
        } else if constexpr (std::is_same_v<T, SystemVerilogScalarBinary>) {
            reads.push_back(op.lhs);
            if (op.operation != SystemVerilogScalarBinaryOperator::Convert) {
                reads.push_back(op.rhs);
            }
            write = op.destination;
        } else if constexpr (std::is_same_v<T, RandomValue>) {
            if (op.maximum) {
                reads.push_back(*op.maximum);
            }
            if (op.minimum) {
                reads.push_back(*op.minimum);
            }
            write = op.destination;
        } else if constexpr (std::is_same_v<T, Assert>) {
            reads.push_back(op.condition);
        } else if constexpr (std::is_same_v<T, CodeCoverageToggle>) {
            reads.push_back(op.previous);
            reads.push_back(op.current);
        } else if constexpr (std::is_same_v<T, IntegerBinary>) {
            reads.push_back(op.lhs);
            reads.push_back(op.rhs);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, IntegerUnary>) {
            reads.push_back(op.source);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, IntegerCheck>) {
            reads.push_back(op.source);
        } else if constexpr (std::is_same_v<T, WriteBlocking>
            || std::is_same_v<T, WriteBlockingSlice>
            || std::is_same_v<T, WriteUpdate>
            || std::is_same_v<T, WriteUpdateSlice>
            || std::is_same_v<T, WriteProjected>
            || std::is_same_v<T, WriteProjectedSlice>
            || std::is_same_v<T, WriteNetDelay>) {
            reads.push_back(op.source);
        } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
            || std::is_same_v<T, WriteUpdateDynamicSlice>
            || std::is_same_v<T, WriteProjectedDynamicSlice>) {
            reads.push_back(op.source);
            reads.push_back(op.selection.index);
        } else if constexpr (std::is_same_v<T, WriteBlockingDynamicPartSlice>
            || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
            reads.push_back(op.source);
            reads.push_back(op.selection.base);
        } else if constexpr (std::is_same_v<T, WriteProjectedWaveform>
            || std::is_same_v<T, WriteProjectedWaveformSlice>
            || std::is_same_v<T, WriteProjectedWaveformDynamicSlice>) {
            // A waveform's values and run-time delays (IEEE 1076-2019
            // 10.5.2.1) and a dynamic target's index.
            for (const auto& element : op.elements) {
                reads.push_back(element.source);
                if (element.delay_source) {
                    reads.push_back(*element.delay_source);
                }
            }
            if constexpr (std::is_same_v<T, WriteProjectedWaveformDynamicSlice>) {
                reads.push_back(op.selection.index);
            }
        } else if constexpr (std::is_same_v<T, ContainerRead>) {
            reads.push_back(op.index);
            write = op.destination;
        } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
            reads.push_back(op.index);
            reads.push_back(op.source);
            if (op.dynamic_part) {
                reads.push_back(op.dynamic_part->base);
            }
            for (const auto& level : op.levels) {
                reads.push_back(level.index);
            }
        }
    }
}

/// Every numeric register an operation writes, with the width of the value
/// (the primary result of operation_registers_of, and the status, count and
/// target registers of the file operations the reference evaluator stores in
/// run_file).
template <typename T>
inline void operation_written_registers_of(const T& op,
    std::vector<WrittenRegister>& writes)
{
    writes.clear();
    if constexpr (std::is_same_v<T, FileOpen>) {
        writes.push_back({ op.destination, 32U });
        if (op.status) {
            writes.push_back({ *op.status, 2U });
        }
    } else if constexpr (std::is_same_v<T, FileClose>) {
        if (op.clear_handle) {
            writes.push_back({ op.handle, 32U });
        }
    } else if constexpr (std::is_same_v<T, FileEndOfFile>) {
        writes.push_back({ op.destination, op.lookahead ? 1U : 32U });
    } else if constexpr (std::is_same_v<T, FileReadLine>
        || std::is_same_v<T, FileErrorStatus>) {
        writes.push_back({ op.destination, 32U });
        if (op.target_kind == FileTextTargetKind::packed_register) {
            writes.push_back({ op.target, op.target_width });
        }
    } else if constexpr (std::is_same_v<T, FileScan>) {
        writes.push_back({ op.destination, 32U });
        if (op.success) {
            writes.push_back({ *op.success, 1U });
        }
        for (const auto& conversion : op.conversions) {
            if (!conversion.suppress
                && conversion.target.kind == InputScanTargetKind::packed_register) {
                writes.push_back({ conversion.target.id, conversion.target.width });
            }
        }
    } else if constexpr (std::is_same_v<T, FilePosition>) {
        writes.push_back({ op.destination, 32U });
    } else if constexpr (std::is_same_v<T, FileBinaryRead>) {
        writes.push_back({ op.destination, 32U });
        if (op.target_kind == FileBinaryTargetKind::packed_register) {
            writes.push_back({ op.target, op.width });
        }
    } else {
        std::vector<RegisterId> reads;
        std::optional<RegisterId> write;
        operation_registers_of(op, reads, write);
        if (write) {
            writes.push_back({ *write, 0U });
        }
    }
}

inline void operation_written_registers(const Operation& operation,
    std::vector<WrittenRegister>& writes)
{
    visit_operation([&](const auto& op) {
        operation_written_registers_of(op, writes);
    }, operation);
}

/// Register operands of the operations the kernel supports.
inline void operation_registers(const Operation& operation,
    std::vector<RegisterId>& reads, std::optional<RegisterId>& write)
{
    visit_operation([&](const auto& op) {
        operation_registers_of(op, reads, write);
    }, operation);
}

/// Bits [offset, offset + width) of a Logic4 value (a Logic9 value goes
/// through kernel_word::logic9_uword).
[[nodiscard]] inline kw::Word packed_field(const PackedLogic4& value,
    const std::uint32_t offset, const std::uint32_t width)
{
    const auto a = value.aval_words();
    const auto b = value.bval_words();
    const auto word = offset / 64U;
    const auto shift = offset % 64U;
    if (word >= a.size()) {
        return kw::all_x(width);
    }
    auto value_a = a[word] >> shift;
    auto value_b = b[word] >> shift;
    if (shift != 0U && shift + width > 64U && word + 1U < a.size()) {
        value_a |= a[word + 1U] << (64U - shift);
        value_b |= b[word + 1U] << (64U - shift);
    }
    const auto m = kw::mask(width);
    return { value_a & m, value_b & m };
}

/// Instructions whose static bit offset a canonical template may take from a
/// per-unit binding instead (StaticKernel::build_native, step 2):
/// load_field (then sub 1, binding y), load_field9 and static store_vhdl
/// (then z, the binding plus one; not to a resolved slot, whose generated
/// code needs the bit offset: store_vhdl aux is its width, see net_shape).
[[nodiscard]] inline bool binding_offset_field(const KInst& inst) noexcept
{
    return (inst.op == KOp::load_field && inst.sub == 0U)
        || inst.op == KOp::load_field9
        || (inst.op == KOp::store_vhdl && inst.sub == 0U && inst.aux == 0U);
}

/// `inst` with its bit offset resolved from the unit's bindings when a
/// canonical template takes it from there (load_field9, store_vhdl).
[[nodiscard]] inline KInst with_bound_offset(const KInst& inst,
    const std::uint32_t* bindings) noexcept
{
    auto resolved = inst;
    if (inst.z != 0U && binding_offset_field(inst)) {
        resolved.offset = bindings[2U * (inst.z - 1U)];
        resolved.z = 0U;
    }
    return resolved;
}

/// Registers a compiled instruction reads and writes, and its successors.
inline void kinst_flow(const KInst& inst, const std::uint32_t at,
    const CompiledBody& body, const std::vector<Operation>& operations,
    const std::uint32_t stop, std::vector<std::uint32_t>& use,
    std::optional<std::uint32_t>& def, std::vector<std::uint32_t>& next)
{
    use.clear();
    def.reset();
    next.clear();
    const auto size = static_cast<std::uint32_t>(body.code.size());
    std::vector<RegisterId> reads;
    std::optional<RegisterId> write;
    const auto fall = [&] { next.push_back(at + 1U); };
    // A chunked wide register by its storage index.
    const auto named = [&](const std::uint32_t reg) {
        return reg < body.wide_base.size() && body.wide_base[reg] != 0U
            ? body.wide_base[reg] & ~wide_unknown_flag : reg;
    };
    switch (inst.op) {
    case KOp::step:
        // Operation tier: the operation's static successors.
        if (inst.z >= 1U) {
            next.push_back(inst.d);
        }
        if (inst.z >= 2U) {
            next.push_back(inst.y);
        }
        if (inst.offset != 0U) {
            next.insert(next.end(), body.return_targets.begin(),
                body.return_targets.end());
        }
        break;
    case KOp::nop:
    case KOp::mark:
    case KOp::mem_bind:
    case KOp::suspend:
        fall();
        break;
    case KOp::constant:
    case KOp::load_slot:
    case KOp::load_slot9:
    case KOp::load_field:
    case KOp::load_field9:
    case KOp::load_host:
        def = inst.d;
        fall();
        break;
    case KOp::copy:
    case KOp::reduce:
    case KOp::unary_not:
    case KOp::logical_not:
    case KOp::two_state:
    case KOp::integer_unary:
    case KOp::extract:
        use.push_back(inst.x);
        def = inst.d;
        fall();
        break;
    case KOp::binary:
    case KOp::logical_binary:
    case KOp::insert:
    case KOp::shift:
    case KOp::integer_binary:
        use.push_back(inst.x);
        use.push_back(inst.y);
        def = inst.d;
        fall();
        break;
    case KOp::concat:
        for (std::uint32_t index = 0U; index < inst.x; ++index) {
            use.push_back(body.concat[inst.aux + index].reg);
        }
        def = inst.d;
        fall();
        break;
    case KOp::conditional:
    case KOp::dynamic_insert:
    case KOp::dynamic_part_insert:
        use.push_back(inst.x);
        use.push_back(inst.y);
        use.push_back(inst.z);
        def = inst.d;
        fall();
        break;
    case KOp::dynamic_extract:
    case KOp::dynamic_part_select:
        if (inst.sub != 1U) {
            use.push_back(inst.x);
        }
        use.push_back(inst.y);
        def = inst.d;
        fall();
        break;
    case KOp::mem_read:
        use.push_back(inst.y);
        def = inst.d;
        fall();
        break;
    case KOp::mem_write:
        use.push_back(inst.x);
        use.push_back(inst.y);
        if (inst.aux != 0U) {
            use.push_back(inst.z);
        }
        fall();
        break;
    case KOp::mem_copy:
        fall();
        break;
    case KOp::wide_move:
        if (inst.sub == 1U) {
            use.push_back(inst.y);
        }
        fall();
        break;
    case KOp::store_host:
        use.push_back(inst.x);
        if (inst.aux != 0U) {
            // A write through a dynamic index (offset 0) or part base
            // (offset 1) in register y.
            use.push_back(inst.y);
        }
        fall();
        break;
    case KOp::store_slot:
    case KOp::store_slot_nba:
    case KOp::integer_check:
    case KOp::assert_check:
        use.push_back(inst.x);
        fall();
        break;
    case KOp::store_slot_dynamic:
    case KOp::store_slot_part:
        use.push_back(inst.x);
        use.push_back(inst.y);
        fall();
        break;
    case KOp::store_vhdl:
        use.push_back(inst.x);
        if (inst.sub == 1U || inst.sub == 3U) {
            use.push_back(inst.y);
        }
        fall();
        break;
    case KOp::generic: {
        operation_registers(operations[inst.x], reads, write);
        for (const auto reg : reads) {
            use.push_back(named(inst.y + reg));
        }
        if (write) {
            def = named(inst.y + *write);
        }
        fall();
        break;
    }
    case KOp::net_delay:
        use.push_back(named(inst.x));
        fall();
        break;
    case KOp::sva_tick: {
        // The tick reads the monitor's operands (guards, start, disabled,
        // bounds) from the register file and defines its result.
        operation_registers(operations[inst.x], reads, write);
        for (const auto reg : reads) {
            use.push_back(named(inst.y + reg));
        }
        def = inst.d;
        fall();
        break;
    }
    case KOp::sva_outcome:
    case KOp::sva_sampled_load:
        def = inst.d;
        fall();
        break;
    case KOp::wide_load:
    case KOp::wide_const:
        def = inst.d;
        fall();
        break;
    case KOp::wide_copy:
        use.push_back(inst.x);
        def = inst.d;
        fall();
        break;
    case KOp::wide_concat:
        for (std::uint32_t index = 0U; index < inst.x; ++index) {
            use.push_back(body.concat[inst.aux + index].reg);
        }
        def = inst.d;
        fall();
        break;
    case KOp::wide_store:
        use.push_back(inst.x);
        if (inst.sub == 3U && inst.imm_a != 0U) {
            // A host signal's write through a dynamic index (or part base).
            use.push_back(inst.y);
        }
        fall();
        break;
    case KOp::wide_part_insert:
        use.push_back(inst.x);
        use.push_back(inst.y);
        use.push_back(inst.z);
        def = inst.d;
        fall();
        break;
    case KOp::wide_alu:
        // The operands of the family; the destination (a chunked register's
        // first storage index, or a narrow register) is the definition.
        use.push_back(inst.x);
        switch (static_cast<WideFamily>(inst.offset)) {
        case WideFamily::binary:
        case WideFamily::shift:
        case WideFamily::logical_binary:
        case WideFamily::insert:
        case WideFamily::dynamic_part_select:
            use.push_back(inst.y);
            break;
        case WideFamily::conditional:
        case WideFamily::dynamic_insert:
            use.push_back(inst.y);
            use.push_back(inst.z);
            break;
        case WideFamily::unary_not:
        case WideFamily::reduce:
        case WideFamily::logical_not:
        case WideFamily::two_state:
        case WideFamily::extract:
            break;
        }
        def = inst.d;
        fall();
        break;
    case KOp::jump:
        next.push_back(std::min(inst.d, size));
        break;
    case KOp::branch:
        use.push_back(inst.x);
        next.push_back(std::min(inst.y, size));
        next.push_back(std::min(inst.z, size));
        break;
    case KOp::call:
        if (inst.z != 0U) {
            use.push_back(inst.x);
        }
        next.push_back(std::min(inst.d, size));
        break;
    case KOp::frame_push:
        // Reads the registers it saves (and its depth word).
        use.push_back(inst.x);
        for (auto item = body.frame_saves[inst.aux].item_begin;
             item < body.frame_saves[inst.aux].item_end; ++item) {
            for (std::uint32_t index = 0U; index < body.frame_items[item].count;
                 ++index) {
                use.push_back(body.frame_items[item].index + index);
            }
        }
        fall();
        break;
    case KOp::frame_pop:
        use.push_back(inst.x);
        fall();
        break;
    case KOp::ret:
        if (inst.z != 0U) {
            use.push_back(inst.x);
            for (std::uint32_t slot = 0U; slot < inst.z; ++slot) {
                use.push_back(inst.y + slot);
            }
        }
        for (const auto target : body.return_targets) {
            next.push_back(target == stop ? size
                                                     : std::min(target, size));
        }
        break;
    }
}

/// Bytes of each size class in BodyWriter::group.
inline constexpr std::array<unsigned, 4U> group_sizes { 0U, 1U, 4U, 8U };

/// Compiled member bodies as bytes: scalars as LEB128 varints, arrays four
/// values to a group (group), field by field (most are small or zero). They
/// are read back only by the same build (the cache key includes
/// static_kernel_compiler_identity).
class BodyWriter {
public:
    explicit BodyWriter(std::string& out) : out_ { out } { }

    void put(std::uint64_t value)
    {
        while (value >= 0x80U) {
            out_.push_back(static_cast<char>((value & 0x7fU) | 0x80U));
            value >>= 7U;
        }
        out_.push_back(static_cast<char>(value));
    }
    void put_signed(const std::int64_t value)
    {
        // Zigzag: small negative values stay short.
        put((static_cast<std::uint64_t>(value) << 1U)
            ^ static_cast<std::uint64_t>(value >> 63));
    }
    template <typename T, typename F>
    void list(const std::vector<T>& values, F&& each)
    {
        put(values.size());
        for (const auto& value : values) {
            each(value);
        }
    }
    template <typename T>
    void integers(const std::vector<T>& values)
    {
        list(values, [&](const T value) { put(static_cast<std::uint64_t>(value)); });
    }
    /// Four values led by a byte of their size classes, two bits each:
    /// zero, one byte, four bytes or eight (little-endian). About as short as
    /// varints, and BodyReader::group decodes it without a branch per byte.
    void group(const std::array<std::uint64_t, 4U>& values)
    {
        const auto size_class = [](const std::uint64_t value) -> unsigned {
            return value == 0U ? 0U
                : value <= 0xffU ? 1U
                : value <= 0xffffffffU ? 2U : 3U;
        };
        unsigned classes { };
        for (std::size_t index = 0U; index < values.size(); ++index) {
            classes |= size_class(values[index]) << (2U * index);
        }
        out_.push_back(static_cast<char>(classes));
        for (std::size_t index = 0U; index < values.size(); ++index) {
            for (unsigned byte = 0U; byte < group_sizes[(classes >> (2U * index)) & 3U];
                 ++byte) {
                out_.push_back(static_cast<char>(values[index] >> (8U * byte)));
            }
        }
    }
    /// The count, then the values four at a time (the last group padded with
    /// zeros).
    template <typename T>
        requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
    void grouped(const std::vector<T>& values)
    {
        put(values.size());
        for (std::size_t index = 0U; index < values.size(); index += 4U) {
            std::array<std::uint64_t, 4U> four { };
            for (std::size_t lane = 0U; lane < 4U && index + lane < values.size(); ++lane) {
                four[lane] = static_cast<std::uint64_t>(values[index + lane]);
            }
            group(four);
        }
    }
    void grouped(const std::vector<kernel_word::Word>& words)
    {
        put(words.size());
        for (std::size_t index = 0U; index < words.size(); index += 2U) {
            std::array<std::uint64_t, 4U> four { };
            four[0] = words[index].a;
            four[1] = words[index].b;
            if (index + 1U < words.size()) {
                four[2] = words[index + 1U].a;
                four[3] = words[index + 1U].b;
            }
            group(four);
        }
    }
    /// Instructions: three groups each (opcode, sub, flags, width; d, x, y,
    /// z; offset, aux and the two immediates).
    void grouped(const std::vector<KInst>& code)
    {
        put(code.size());
        for (const auto& inst : code) {
            group({ static_cast<std::uint64_t>(inst.op), inst.sub, inst.flags, inst.width });
            group({ inst.d, inst.x, inst.y, inst.z });
            group({ inst.offset, inst.aux, inst.imm_a, inst.imm_b });
        }
    }
    /// Bytes as they are.
    void raw(const std::vector<std::uint8_t>& values)
    {
        put(values.size());
        out_.append(reinterpret_cast<const char*>(values.data()), values.size());
    }
    void body(const static_kernel_detail::CompiledBody& body)
    {
        grouped(body.code);
        const bool zero = std::ranges::all_of(body.registers,
            [](const kernel_word::Word& word) { return word.a == 0U && word.b == 0U; });
        put(zero ? 1U : 0U);
        if (zero) {
            put(body.registers.size());
        } else {
            grouped(body.registers);
        }
        grouped(body.register_widths);
        list(body.indices, [&](const DynamicIndex& index) {
            put(index.index);
            put_signed(index.left);
            put_signed(index.right);
            put(index.base_offset);
            put(index.strict ? 1U : 0U);
        });
        list(body.parts, [&](const DynamicPartIndex& part) {
            put_signed(part.left);
            put_signed(part.right);
            put(part.base);
            put(part.base_offset);
            put(part.width);
            put(part.increasing ? 1U : 0U);
            put(part.source_descending ? 1U : 0U);
        });
        list(body.concat, [&](const ConcatOperand& operand) {
            put(operand.reg);
            put(operand.width);
            put(operand.unknown);
        });
        grouped(body.containers);
        put(body.entry);
        list(body.wide_registers, [&](const PackedLogic4& value) { packed(value); });
        grouped(body.wide_base);
        grouped(body.wide_constants);
        grouped(body.return_targets);
        grouped(body.live_at_entry);
        put(body.shadow_base);
        put(body.weak);
        raw(body.tracked);
        raw(body.u_mode);
        grouped(body.u_operand_begin);
        grouped(body.u_operands);
        put(body.call_stack_base);
        grouped(body.resume_entries);
        put(body.frame_stack_base);
        list(body.frame_saves, [&](const CompiledBody::FrameSave& save) {
            put(save.identity);
            put(save.item_begin);
            put(save.item_end);
            put(save.pairs);
            put(save.register_begin);
            put(save.register_end);
        });
        list(body.frame_items, [&](const CompiledBody::FrameItem& item) {
            put(item.index);
            put(item.count);
            put(item.offset);
        });
        grouped(body.frame_registers);
        // The plans of the generated attempt automata (KOp::sva_tick, z the
        // index plus one): a restored body compiles its ticks from them.
        list(body.sva_plans, [&](const SvaNativePlan& plan) {
            list(plan.sequences, [&](const SvaNativeSequence& sequence) {
                list(sequence.code, [&](const SvaNativeStep& step) {
                    put(static_cast<std::uint64_t>(step.op));
                    put(step.a);
                    put(step.b);
                    put(step.c);
                    put_signed(step.lo);
                    put_signed(step.hi);
                    put(step.line);
                    put(step.column);
                    put_signed(step.lo_slot);
                    put_signed(step.hi_slot);
                });
                put(sequence.counters);
                put(sequence.state_base);
            });
            list(plan.properties, [&](const SvaNativeProperty& property) {
                put(static_cast<std::uint64_t>(property.kind));
                put(property.overlapped ? 1U : 0U);
                put(property.sequence);
                put_signed(property.child);
                list(property.children,
                    [&](const std::int32_t child) { put_signed(child); });
                put(property.guard);
                put_signed(property.lo);
                put_signed(property.lo_slot);
                put_signed(property.hi);
                put_signed(property.hi_slot);
                put(property.line);
                put(property.column);
            });
            put(plan.root);
            put(plan.every_match ? 1U : 0U);
            grouped(plan.clock_guards);
            list(plan.guard_positions,
                [&](const std::pair<std::uint32_t, std::uint32_t>& position) {
                    put(position.first);
                    put(position.second);
                });
        });
    }
    /// Width plus the raw bit planes (two four-state, four logic9).
    void packed(const PackedLogic4& value)
    {
        const bool logic9 = value.is_logic9();
        put(logic9 ? 1U : 0U);
        put(value.width());
        const auto word = [&](const std::uint64_t bits) {
            out_.append(reinterpret_cast<const char*>(&bits), sizeof(bits));
        };
        if (logic9) {
            for (std::size_t plane = 0; plane < 4U; ++plane) {
                for (const auto bits : value.logic9_plane_words(plane)) {
                    word(bits);
                }
            }
        } else {
            for (const auto bits : value.aval_words()) {
                word(bits);
            }
            for (const auto bits : value.bval_words()) {
                word(bits);
            }
        }
    }
    void text(const std::string_view value)
    {
        put(value.size());
        out_.append(value);
    }

private:
    std::string& out_;
};

class BodyReader {
public:
    explicit BodyReader(std::string_view bytes) : bytes_ { bytes } { }

    bool get(std::uint64_t& value)
    {
        const auto* data = reinterpret_cast<const unsigned char*>(bytes_.data());
        // Most values are one byte.
        if (at_ < bytes_.size() && data[at_] < 0x80U) {
            value = data[at_++];
            return true;
        }
        value = 0U;
        // A varint takes at most ten bytes; with that many left, no bounds
        // check per byte.
        const bool room = bytes_.size() - at_ >= 10U;
        for (unsigned shift = 0U; shift < 64U; shift += 7U) {
            if (!room && at_ == bytes_.size()) {
                return false;
            }
            const auto byte = data[at_++];
            value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
            if ((byte & 0x80U) == 0U) {
                return true;
            }
        }
        return false;
    }
    template <typename T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    bool get(T& value)
    {
        std::uint64_t raw { };
        if (!get(raw)) {
            return false;
        }
        if constexpr (std::is_same_v<T, bool>) {
            if (raw > 1U) {
                return false;
            }
            value = raw != 0U;
        } else if constexpr (std::is_enum_v<T>) {
            using U = std::underlying_type_t<T>;
            if (raw > std::numeric_limits<U>::max()) {
                return false;
            }
            value = static_cast<T>(static_cast<U>(raw));
        } else {
            if (raw > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                return false;
            }
            value = static_cast<T>(raw);
        }
        return true;
    }
    bool get_signed(std::int64_t& value)
    {
        std::uint64_t raw { };
        if (!get(raw)) {
            return false;
        }
        value = static_cast<std::int64_t>(raw >> 1U) ^ -static_cast<std::int64_t>(raw & 1U);
        return true;
    }
    /// A signed value of a narrower field; one outside its range is corrupt.
    bool get_narrow(std::int32_t& value)
    {
        std::int64_t wide { };
        if (!get_signed(wide) || wide < std::numeric_limits<std::int32_t>::min()
            || wide > std::numeric_limits<std::int32_t>::max()) {
            return false;
        }
        value = static_cast<std::int32_t>(wide);
        return true;
    }
    /// A count beyond the remaining bytes is corrupt (each item takes one).
    template <typename T, typename F>
    bool list(std::vector<T>& values, F&& each)
    {
        std::uint64_t count { };
        if (!get(count) || count > bytes_.size() - at_) {
            return false;
        }
        values.resize(static_cast<std::size_t>(count));
        for (auto& value : values) {
            if (!each(value)) {
                return false;
            }
        }
        return true;
    }
    template <typename T>
    bool integers(std::vector<T>& values)
    {
        return list(values, [&](T& value) { return get(value); });
    }
    /// BodyWriter::group. The bytes come from the same build, so each value
    /// fits the field it is read into.
    bool group(std::array<std::uint64_t, 4U>& values)
    {
        // The longest group: its class byte and four eight-byte values. With
        // that many bytes left each value is one unaligned load, masked.
        constexpr std::size_t longest = 1U + 4U * 8U;
        static constexpr std::array<std::uint64_t, 4U> masks {
            0U, 0xffU, 0xffffffffU, ~std::uint64_t { 0 } };
        const auto* data = bytes_.data();
        if (bytes_.size() - at_ >= longest + 8U) {
            const auto classes = static_cast<unsigned char>(data[at_++]);
            for (std::size_t index = 0U; index < values.size(); ++index) {
                const auto size_class = (classes >> (2U * index)) & 3U;
                std::uint64_t word { };
                std::memcpy(&word, data + at_, sizeof(word));
                values[index] = word & masks[size_class];
                at_ += group_sizes[size_class];
            }
            return true;
        }
        if (at_ == bytes_.size()) {
            return false;
        }
        const auto classes = static_cast<unsigned char>(data[at_++]);
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const auto size = group_sizes[(classes >> (2U * index)) & 3U];
            if (bytes_.size() - at_ < size) {
                return false;
            }
            values[index] = 0U;
            for (unsigned byte = 0U; byte < size; ++byte) {
                values[index] |= static_cast<std::uint64_t>(
                    static_cast<unsigned char>(data[at_++])) << (8U * byte);
            }
        }
        return true;
    }
    template <typename T>
        requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
    bool grouped(std::vector<T>& values)
    {
        std::uint64_t count { };
        // Each group of four takes at least one byte.
        if (!get(count) || count / 4U > bytes_.size() - at_) {
            return false;
        }
        values.resize(static_cast<std::size_t>(count));
        std::array<std::uint64_t, 4U> four { };
        for (std::size_t index = 0U; index < values.size(); index += 4U) {
            if (!group(four)) {
                return false;
            }
            for (std::size_t lane = 0U; lane < 4U && index + lane < values.size(); ++lane) {
                values[index + lane] = static_cast<T>(four[lane]);
            }
        }
        return true;
    }
    bool grouped(std::vector<kernel_word::Word>& words)
    {
        std::uint64_t count { };
        if (!get(count) || count / 2U > bytes_.size() - at_) {
            return false;
        }
        words.resize(static_cast<std::size_t>(count));
        std::array<std::uint64_t, 4U> four { };
        for (std::size_t index = 0U; index < words.size(); index += 2U) {
            if (!group(four)) {
                return false;
            }
            words[index] = { four[0], four[1] };
            if (index + 1U < words.size()) {
                words[index + 1U] = { four[2], four[3] };
            }
        }
        return true;
    }
    bool grouped(std::vector<KInst>& code)
    {
        std::uint64_t count { };
        // Each instruction takes at least three bytes.
        if (!get(count) || count > (bytes_.size() - at_) / 3U) {
            return false;
        }
        code.resize(static_cast<std::size_t>(count));
        std::array<std::uint64_t, 4U> four { };
        for (auto& inst : code) {
            if (!group(four)) {
                return false;
            }
            inst.op = static_cast<KOp>(four[0]);
            inst.sub = static_cast<std::uint8_t>(four[1]);
            inst.flags = static_cast<std::uint8_t>(four[2]);
            inst.width = static_cast<std::uint32_t>(four[3]);
            if (!group(four)) {
                return false;
            }
            inst.d = static_cast<std::uint32_t>(four[0]);
            inst.x = static_cast<std::uint32_t>(four[1]);
            inst.y = static_cast<std::uint32_t>(four[2]);
            inst.z = static_cast<std::uint32_t>(four[3]);
            if (!group(four)) {
                return false;
            }
            inst.offset = static_cast<std::uint32_t>(four[0]);
            inst.aux = static_cast<std::uint32_t>(four[1]);
            inst.imm_a = four[2];
            inst.imm_b = four[3];
        }
        return true;
    }
    bool raw(std::vector<std::uint8_t>& values)
    {
        std::uint64_t count { };
        if (!get(count) || count > bytes_.size() - at_) {
            return false;
        }
        values.assign(bytes_.data() + at_, bytes_.data() + at_ + count);
        at_ += static_cast<std::size_t>(count);
        return true;
    }
    bool body(static_kernel_detail::CompiledBody& body)
    {
        bool zero { };
        if (!grouped(body.code) || !get(zero)) {
            return false;
        }
        if (zero) {
            std::uint64_t registers { };
            if (!get(registers) || registers > (std::uint64_t { 1 } << 32U)) {
                return false;
            }
            body.registers.assign(static_cast<std::size_t>(registers), { });
        } else if (!grouped(body.registers)) {
            return false;
        }
        return grouped(body.register_widths)
            && list(body.indices, [&](DynamicIndex& index) {
                   return get(index.index) && get_signed(index.left)
                       && get_signed(index.right) && get(index.base_offset)
                       && get(index.strict);
               })
            && list(body.parts, [&](DynamicPartIndex& part) {
                   return get_signed(part.left) && get_signed(part.right)
                       && get(part.base) && get(part.base_offset) && get(part.width)
                       && get(part.increasing) && get(part.source_descending);
               })
            && list(body.concat, [&](ConcatOperand& operand) {
                   return get(operand.reg) && get(operand.width)
                       && get(operand.unknown);
               })
            && grouped(body.containers) && get(body.entry)
            && list(body.wide_registers, [&](PackedLogic4& value) { return packed(value); })
            && grouped(body.wide_base) && grouped(body.wide_constants)
            && grouped(body.return_targets) && grouped(body.live_at_entry)
            && get(body.shadow_base) && get(body.weak) && raw(body.tracked)
            && raw(body.u_mode) && grouped(body.u_operand_begin)
            && grouped(body.u_operands) && get(body.call_stack_base)
            && grouped(body.resume_entries) && get(body.frame_stack_base)
            && list(body.frame_saves, [&](CompiledBody::FrameSave& save) {
                   return get(save.identity) && get(save.item_begin)
                       && get(save.item_end) && get(save.pairs)
                       && get(save.register_begin) && get(save.register_end);
               })
            && list(body.frame_items, [&](CompiledBody::FrameItem& item) {
                   return get(item.index) && get(item.count) && get(item.offset);
               })
            && grouped(body.frame_registers)
            && list(body.sva_plans, [&](SvaNativePlan& plan) {
                   return list(plan.sequences, [&](SvaNativeSequence& sequence) {
                              return list(sequence.code, [&](SvaNativeStep& step) {
                                         return get(step.op) && get(step.a)
                                             && get(step.b) && get(step.c)
                                             && get_signed(step.lo)
                                             && get_signed(step.hi)
                                             && get(step.line) && get(step.column)
                                             && get_narrow(step.lo_slot)
                                             && get_narrow(step.hi_slot);
                                     })
                                  && get(sequence.counters)
                                  && get(sequence.state_base);
                          })
                       && list(plan.properties, [&](SvaNativeProperty& property) {
                              return get(property.kind) && get(property.overlapped)
                                  && get(property.sequence)
                                  && get_narrow(property.child)
                                  && list(property.children,
                                      [&](std::int32_t& child) {
                                          return get_narrow(child);
                                      })
                                  && get(property.guard)
                                  && get_signed(property.lo)
                                  && get_narrow(property.lo_slot)
                                  && get_signed(property.hi)
                                  && get_narrow(property.hi_slot)
                                  && get(property.line) && get(property.column);
                          })
                       && get(plan.root) && get(plan.every_match)
                       && grouped(plan.clock_guards)
                       && list(plan.guard_positions,
                           [&](std::pair<std::uint32_t, std::uint32_t>& position) {
                               return get(position.first) && get(position.second);
                           });
               });
    }
    bool packed(PackedLogic4& value)
    {
        bool logic9 { };
        std::uint64_t width { };
        if (!get(logic9) || !get(width)) {
            return false;
        }
        if (width == 0U) {
            value = logic9 ? PackedLogic4::from_logic9_msb_string({ })
                           : PackedLogic4::from_msb_string({ });
            return true;
        }
        const std::uint64_t planes = logic9 ? 4U : 2U;
        const std::uint64_t remaining = bytes_.size() - at_;
        // Check the bytes exist before any width-sized allocation.
        if (width > remaining * 8U) {
            return false;
        }
        const std::uint64_t words = (width + 63U) / 64U;
        if (words > remaining / 8U / planes) {
            return false;
        }
        std::vector<std::uint64_t> storage(
            static_cast<std::size_t>(words * planes));
        std::memcpy(storage.data(), bytes_.data() + at_,
            storage.size() * sizeof(std::uint64_t));
        at_ += storage.size() * sizeof(std::uint64_t);
        const auto plane = [&](const std::uint64_t index) {
            return std::span<const std::uint64_t> {
                storage.data() + index * words,
                static_cast<std::size_t>(words) };
        };
        value = logic9
            ? PackedLogic4::from_logic9_word_planes(
                static_cast<std::size_t>(width), plane(0U),
                plane(1U), plane(2U), plane(3U))
            : PackedLogic4::from_word_planes(
                static_cast<std::size_t>(width), plane(0U),
                plane(1U));
        return true;
    }
    bool text(std::string& value)
    {
        std::uint64_t size { };
        if (!get(size) || size > bytes_.size() - at_) {
            return false;
        }
        value.assign(bytes_.data() + at_, static_cast<std::size_t>(size));
        at_ += static_cast<std::size_t>(size);
        return true;
    }
    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return bytes_.size() - at_;
    }
    [[nodiscard]] bool done() const noexcept { return at_ == bytes_.size(); }

private:
    std::string_view bytes_;
    std::size_t at_ { };
};

} // namespace fsim::runtime::simir::static_kernel_compiled_detail

// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_impl.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace fsim::compiler {
using namespace llvm_detail;
using runtime::simir::Process;
using runtime::simir::ValueKind;

namespace {

static_assert(
    std::is_standard_layout_v<runtime::simir::PureWavePreparedMember>);
static_assert(std::is_standard_layout_v<NativePureWavePreparedView>);

template <typename Alternative>
[[nodiscard]] const Alternative* pure_wave_operation(
    const Process& process, const std::size_t index)
{
    if (index >= process.operations.size()) {
        return nullptr;
    }
    return runtime::simir::operation_get_if<Alternative>(
        &process.operations[index]);
}

template <typename Alternative>
[[nodiscard]] bool pure_wave_has_operation(
    const Process& process, const std::size_t index)
{
    return pure_wave_operation<Alternative>(process, index) != nullptr;
}

[[nodiscard]] std::optional<std::uint32_t> pure_wave_read_slot(
    const fsim_jit_runtime_v1& runtime,
    const std::uint32_t expected_count,
    const runtime::simir::SignalId signal)
{
    if (runtime.direct_read_signals == nullptr
        || runtime.direct_read_signal_count != expected_count) {
        return std::nullopt;
    }
    std::optional<std::uint32_t> result;
    for (std::uint32_t index = 0U;
         index < runtime.direct_read_signal_count; ++index) {
        if (runtime.direct_read_signals[index] != signal) {
            continue;
        }
        if (result) {
            return std::nullopt;
        }
        result = index;
    }
    return result;
}

} // namespace

std::optional<LlvmJit::Impl::NativePureWaveMemberPlan>
LlvmJit::Impl::classify_pure_wave_member(
    const JitPureWaveMember& member,
    const LlvmJit::Impl::NativeEntry& native)
{
    using namespace runtime::simir;
    const auto* const process = member.process;
    const auto* const runtime = member.native.runtime;
    if (process == nullptr || runtime == nullptr
        || member.native.frame == nullptr || member.native.result == nullptr
        || member.native.queued == nullptr
        || member.native.waiting_on_static == nullptr
        || member.native.process_status == nullptr
        || member.native.active != nullptr
        || process->static_sensitivity.empty()
        || !process->static_trigger_regions.empty()
        || !process->debug_locals.empty()
        || !process->debug_string_locals.empty()
        || !process->debug_container_locals.empty()
        || !process->container_register_types.empty()) {
        return std::nullopt;
    }
    if (runtime->abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1
        || runtime->struct_size < sizeof(fsim_jit_runtime_v1)) {
        return std::nullopt;
    }

    const auto& info = native.info;
    const auto& layout = info.frame_layout;
    if (info.operation_count != process->operations.size()
        || process->register_count != layout.register_count
        || process->operations.size() < 6U
        || !info.requires_resume
        || (!info.uses_write_update_slice && !info.uses_wide_signal_write)
        || layout.uses_logic9 || layout.tracks_register_initialization
        || layout.string_register_count != 0U
        || layout.register_widths.size() != layout.register_count
        || layout.register_word_offsets.size() != layout.register_count
        || layout.direct_update_signals.size()
            != member.direct_update_signals.size()) {
        return std::nullopt;
    }
    if (!process->register_value_kinds.empty()
        && (process->register_value_kinds.size() != process->register_count
            || std::ranges::any_of(
                process->register_value_kinds,
                [](const auto kind) { return kind != ValueKind::logic4; }))) {
        return std::nullopt;
    }
    for (const auto flag : ProcessInfo::flags) {
        const bool supported_flag
            = flag == &ProcessInfo::requires_resume
            || flag == &ProcessInfo::uses_debug_points
            || flag == &ProcessInfo::uses_write_update_slice
            || flag == &ProcessInfo::uses_wide_signal_read
            || flag == &ProcessInfo::uses_wide_signal_write;
        if (info.*flag && !supported_flag) {
            return std::nullopt;
        }
    }

    const auto count = process->operations.size();
    if (!pure_wave_has_operation<DebugPoint>(*process, 0U)
        || !pure_wave_has_operation<DebugPoint>(*process, 1U)
        || !pure_wave_has_operation<WaitSensitivity>(*process, count - 2U)) {
        return std::nullopt;
    }
    const auto* const terminal_jump
        = pure_wave_operation<Jump>(*process, count - 1U);
    if (terminal_jump == nullptr || terminal_jump->target != 0U) {
        return std::nullopt;
    }

    NativePureWaveMemberPlan plan;
    plan.native = &native;
    plan.process = process;
    plan.expected_wait_instruction
        = static_cast<std::uint32_t>(count - 2U);
    plan.expected_resume_instruction
        = static_cast<std::uint32_t>(count - 1U);
    const auto save_register = [&](const std::size_t slot,
                                   const RegisterId id) {
        if (slot >= plan.registers.size() || id >= layout.register_count) {
            return false;
        }
        plan.registers[slot] = id;
        plan.register_widths[slot] = layout.register_widths[id];
        plan.register_word_offsets[slot]
            = layout.register_word_offsets[id];
        return plan.register_widths[slot] != 0U;
    };
    const auto save_read = [&](const std::size_t slot,
                               const ReadSignal& read) {
        if (slot >= plan.read_slots.size()
            || read.kind != SignalReadKind::current || read.ticks != 1U
            || read.clock || read.gate) {
            return false;
        }
        const auto read_slot = pure_wave_read_slot(
            *runtime,
            static_cast<std::uint32_t>(layout.direct_read_signals.size()),
            read.signal);
        if (!read_slot) {
            return false;
        }
        plan.read_slots[slot] = *read_slot;
        plan.read_signals[slot] = read.signal;
        plan.read_count = std::max(
            plan.read_count, static_cast<std::uint32_t>(slot + 1U));
        return save_register(slot, read.destination);
    };
    const auto save_update = [&](const WriteUpdateSlice& update,
                                 const RegisterId source) {
        const auto found = std::ranges::find(
            member.direct_update_signals, update.signal);
        if (found == member.direct_update_signals.end()
            || std::ranges::find(std::next(found),
                   member.direct_update_signals.end(), update.signal)
                != member.direct_update_signals.end()
            || runtime->direct_update_slots == nullptr
            || runtime->direct_update_slot_count
                != member.direct_update_signals.size()) {
            return false;
        }
        const auto slot = static_cast<std::size_t>(
            found - member.direct_update_signals.begin());
        if (slot > std::numeric_limits<std::uint32_t>::max()
            || source >= layout.register_count) {
            return false;
        }
        const auto source_width = layout.register_widths[source];
        const auto& update_slot = runtime->direct_update_slots[slot];
        if (source_width == 0U || update.offset >= update_slot.width
            || source_width > update_slot.width - update.offset) {
            return false;
        }
        plan.update_slot = static_cast<std::uint32_t>(slot);
        plan.update_signal = update.signal;
        plan.update_offset = update.offset;
        plan.update_width = update_slot.width;
        plan.output_slice_offset = update.offset;
        return save_register(4U, source);
    };

    const auto check_driver = [&](const std::uint32_t width) {
        return process->driver_regions.size() == 1U
            && !process->driver_regions.front().whole
            && process->driver_regions.front().signal == plan.update_signal
            && process->driver_regions.front().offset == plan.update_offset
            && process->driver_regions.front().width == width;
    };
    const auto roles_are_distinct = [](const auto& roles) {
        for (std::size_t index = 0U; index < roles.size(); ++index) {
            for (std::size_t prior = 0U; prior < index; ++prior) {
                if (roles[index] == roles[prior]) {
                    return false;
                }
            }
        }
        return true;
    };

    if (count == 10U) {
        const auto* const lhs_read
            = pure_wave_operation<ReadSignal>(*process, 2U);
        const auto* const lhs_extract
            = pure_wave_operation<Extract>(*process, 3U);
        const auto* const rhs_read
            = pure_wave_operation<ReadSignal>(*process, 4U);
        const auto* const rhs_extract
            = pure_wave_operation<Extract>(*process, 5U);
        const auto* const binary
            = pure_wave_operation<Binary>(*process, 6U);
        const auto* const update
            = pure_wave_operation<WriteUpdateSlice>(*process, 7U);
        if (lhs_read == nullptr || lhs_extract == nullptr
            || rhs_read == nullptr || rhs_extract == nullptr
            || binary == nullptr || update == nullptr
            || binary->operation != BinaryOperator::bit_and
            || lhs_extract->source != lhs_read->destination
            || rhs_extract->source != rhs_read->destination
            || lhs_extract->width != 1U || rhs_extract->width != 1U
            || binary->lhs != lhs_extract->destination
            || binary->rhs != rhs_extract->destination
            || update->source != binary->destination
            || !save_read(0U, *lhs_read)
            || !save_read(1U, *rhs_read)
            || !save_register(2U, lhs_extract->destination)
            || !save_register(3U, rhs_extract->destination)
            || !save_update(*update, binary->destination)
            || !roles_are_distinct(std::array<RegisterId, 5U> {
                lhs_read->destination,
                rhs_read->destination,
                lhs_extract->destination,
                rhs_extract->destination,
                binary->destination,
            })
            || !roles_are_distinct(std::array<RegisterId, 5U> {
                lhs_read->destination,
                rhs_read->destination,
                lhs_extract->destination,
                rhs_extract->destination,
                binary->destination,
            })
            || plan.register_widths[0] > 64U
            || plan.register_widths[1] > 64U
            || plan.register_widths[2] != 1U
            || plan.register_widths[3] != 1U
            || plan.register_widths[4] != 1U) {
            return std::nullopt;
        }
        plan.shape = PureWaveShape::logic4_bit_and;
        plan.input_lhs_offset = lhs_extract->offset;
        plan.input_rhs_offset = rhs_extract->offset;
        if (lhs_extract->offset >= plan.register_widths[0]
            || rhs_extract->offset >= plan.register_widths[1]
            || !check_driver(1U)) {
            return std::nullopt;
        }
    } else if (count == 31U) {
        const auto* const read_first = pure_wave_operation<ReadSignal>(*process, 2U);
        const auto* const outer_extract = pure_wave_operation<Extract>(*process, 3U);
        const auto* const selector_extract = pure_wave_operation<Extract>(*process, 4U);
        const auto* const one = pure_wave_operation<LoadConstant>(*process, 5U);
        const auto* const zero = pure_wave_operation<LoadConstant>(*process, 6U);
        const auto* const eq_one = pure_wave_operation<Binary>(*process, 7U);
        const auto* const eq_zero = pure_wave_operation<Binary>(*process, 8U);
        const auto* const first_branch = pure_wave_operation<Branch>(*process, 9U);
        const auto* const read_first_branch = pure_wave_operation<ReadSignal>(*process, 10U);
        const auto* const extract_first_branch = pure_wave_operation<Extract>(*process, 11U);
        const auto* const read_shifted = pure_wave_operation<ReadSignal>(*process, 12U);
        const auto* const and_first_branch = pure_wave_operation<Binary>(*process, 13U);
        const auto* const copy_first_branch = pure_wave_operation<CopyRegister>(*process, 14U);
        const auto* const jump_first_branch = pure_wave_operation<Jump>(*process, 15U);
        const auto* const second_branch = pure_wave_operation<Branch>(*process, 16U);
        const auto* const read_second_branch = pure_wave_operation<ReadSignal>(*process, 17U);
        const auto* const extract_second_branch = pure_wave_operation<Extract>(*process, 18U);
        const auto* const copy_second_branch = pure_wave_operation<CopyRegister>(*process, 19U);
        const auto* const jump_second_branch = pure_wave_operation<Jump>(*process, 20U);
        const auto* const read_unknown_branch = pure_wave_operation<ReadSignal>(*process, 21U);
        const auto* const extract_unknown_branch = pure_wave_operation<Extract>(*process, 22U);
        const auto* const read_unknown_shifted = pure_wave_operation<ReadSignal>(*process, 23U);
        const auto* const and_unknown_branch = pure_wave_operation<Binary>(*process, 24U);
        const auto* const read_unknown_original = pure_wave_operation<ReadSignal>(*process, 25U);
        const auto* const extract_unknown_original = pure_wave_operation<Extract>(*process, 26U);
        const auto* const select = pure_wave_operation<ConditionalSelect>(*process, 27U);
        const auto* const update = pure_wave_operation<WriteUpdateSlice>(*process, 28U);
        if (read_first == nullptr || outer_extract == nullptr
            || selector_extract == nullptr || one == nullptr || zero == nullptr
            || eq_one == nullptr || eq_zero == nullptr || first_branch == nullptr
            || read_first_branch == nullptr || extract_first_branch == nullptr
            || read_shifted == nullptr || and_first_branch == nullptr
            || copy_first_branch == nullptr || jump_first_branch == nullptr
            || second_branch == nullptr || read_second_branch == nullptr
            || extract_second_branch == nullptr || copy_second_branch == nullptr
            || jump_second_branch == nullptr || read_unknown_branch == nullptr
            || extract_unknown_branch == nullptr || read_unknown_shifted == nullptr
            || and_unknown_branch == nullptr || read_unknown_original == nullptr
            || extract_unknown_original == nullptr || select == nullptr
            || update == nullptr) {
            return std::nullopt;
        }
        const auto constant_is = [](const LoadConstant& constant,
                                    const fsim::runtime::Logic4 value) {
            return constant.value.width() == 1U
                && !constant.value.is_logic9()
                && constant.value.get(0U) == value;
        };
        const auto has_current_read_contract = [](const ReadSignal& read) {
            return read.kind == SignalReadKind::current
                && read.ticks == 1U && !read.clock && !read.gate;
        };
        const auto same_read = [&](const ReadSignal& lhs,
                                   const ReadSignal& rhs) {
            return lhs.signal == rhs.signal
                && has_current_read_contract(lhs)
                && has_current_read_contract(rhs);
        };
        if (!constant_is(*one, fsim::runtime::Logic4::one)
            || !constant_is(*zero, fsim::runtime::Logic4::zero)
            || eq_one->operation != BinaryOperator::case_equal
            || eq_zero->operation != BinaryOperator::case_equal
            || eq_one->lhs != selector_extract->destination
            || eq_one->rhs != one->destination
            || eq_zero->lhs != selector_extract->destination
            || eq_zero->rhs != zero->destination
            || first_branch->condition != eq_one->destination
            || first_branch->when_true != 10U
            || first_branch->when_false != 16U
            || first_branch->unknown_policy != UnknownBranchPolicy::when_false
            || !same_read(*read_first, *read_first_branch)
            || !same_read(*read_first, *read_second_branch)
            || !same_read(*read_first, *read_unknown_branch)
            || !same_read(*read_first, *read_unknown_original)
            || !same_read(*read_shifted, *read_unknown_shifted)
            || outer_extract->source != read_first->destination
            || outer_extract->width != 15U
            || selector_extract->source != outer_extract->destination
            || selector_extract->width != 1U
            || extract_first_branch->source != read_first_branch->destination
            || extract_first_branch->width != outer_extract->width
            || extract_first_branch->offset != outer_extract->offset
            || extract_second_branch->source != read_second_branch->destination
            || extract_second_branch->width != outer_extract->width
            || extract_second_branch->offset != outer_extract->offset
            || extract_unknown_branch->source != read_unknown_branch->destination
            || extract_unknown_branch->width != outer_extract->width
            || extract_unknown_branch->offset != outer_extract->offset
            || extract_unknown_original->source != read_unknown_original->destination
            || extract_unknown_original->width != outer_extract->width
            || extract_unknown_original->offset != outer_extract->offset
            || and_first_branch->operation != BinaryOperator::bit_xor
            || and_unknown_branch->operation != BinaryOperator::bit_xor
            || and_first_branch->lhs != extract_first_branch->destination
            || and_first_branch->rhs != read_shifted->destination
            || and_unknown_branch->lhs != extract_unknown_branch->destination
            || and_unknown_branch->rhs != read_unknown_shifted->destination
            || copy_first_branch->destination != copy_second_branch->destination
            || copy_first_branch->destination != select->destination
            || copy_first_branch->source != and_first_branch->destination
            || copy_second_branch->source != extract_second_branch->destination
            || jump_first_branch->target != 28U
            || second_branch->condition != eq_zero->destination
            || second_branch->when_true != 17U
            || second_branch->when_false != 21U
            || second_branch->unknown_policy != UnknownBranchPolicy::when_false
            || jump_second_branch->target != 28U
            || select->condition != selector_extract->destination
            || select->when_true != and_unknown_branch->destination
            || select->when_false != extract_unknown_original->destination
            || update->source != select->destination
            || !save_read(0U, *read_first)
            || !save_read(1U, *read_shifted)
            || !save_update(*update, select->destination)) {
            return std::nullopt;
        }
        const std::array<RegisterId, 20U> reducer_register_roles {
            read_first->destination,
            outer_extract->destination,
            selector_extract->destination,
            select->destination,
            one->destination,
            zero->destination,
            eq_one->destination,
            eq_zero->destination,
            read_first_branch->destination,
            extract_first_branch->destination,
            read_shifted->destination,
            and_first_branch->destination,
            read_second_branch->destination,
            extract_second_branch->destination,
            read_unknown_branch->destination,
            extract_unknown_branch->destination,
            read_unknown_shifted->destination,
            and_unknown_branch->destination,
            read_unknown_original->destination,
            extract_unknown_original->destination,
        };
        if (process->register_count != reducer_register_roles.size()
            || !roles_are_distinct(reducer_register_roles)) {
            return std::nullopt;
        }
        const auto has_register_width = [&](const RegisterId id,
                                            const std::uint32_t width) {
            return id < layout.register_widths.size()
                && layout.register_widths[id] == width;
        };
        const std::array<std::pair<RegisterId, std::uint32_t>, 20U>
            reducer_register_widths {{
                { read_first->destination, 120U },
                { outer_extract->destination, 15U },
                { selector_extract->destination, 1U },
                { select->destination, 15U },
                { one->destination, 1U },
                { zero->destination, 1U },
                { eq_one->destination, 1U },
                { eq_zero->destination, 1U },
                { read_first_branch->destination, 120U },
                { extract_first_branch->destination, 15U },
                { read_shifted->destination, 15U },
                { and_first_branch->destination, 15U },
                { read_second_branch->destination, 120U },
                { extract_second_branch->destination, 15U },
                { read_unknown_branch->destination, 120U },
                { extract_unknown_branch->destination, 15U },
                { read_unknown_shifted->destination, 15U },
                { and_unknown_branch->destination, 15U },
                { read_unknown_original->destination, 120U },
                { extract_unknown_original->destination, 15U },
            }};
        if (std::ranges::any_of(
                reducer_register_widths,
                [&](const auto& role) {
                    return !has_register_width(role.first, role.second);
                })) {
            return std::nullopt;
        }
        plan.shape = PureWaveShape::reducer31;
        plan.slice_offset = outer_extract->offset;
        plan.selector_offset = selector_extract->offset;
        if (plan.register_widths[0] != 120U
            || plan.register_widths[1] != 15U
            || plan.register_widths[4] != 15U
            || plan.update_width != 120U
            || plan.slice_offset > 120U - 15U
            || plan.selector_offset >= 15U
            || plan.update_offset > 120U - 15U
            || !check_driver(15U)) {
            return std::nullopt;
        }
    } else if (count == 7U) {
        const auto* const read = pure_wave_operation<ReadSignal>(*process, 2U);
        const auto* const reduction = pure_wave_operation<Reduction>(*process, 3U);
        const auto* const update = pure_wave_operation<WriteUpdateSlice>(*process, 4U);
        if (read == nullptr || reduction == nullptr || update == nullptr
            || reduction->operation != ReductionOperator::bit_xor
            || reduction->source != read->destination
            || update->source != reduction->destination
            || reduction->source == reduction->destination
            || !save_read(0U, *read)
            || !save_update(*update, reduction->destination)
            || plan.register_widths[0] != 8U
            || plan.register_widths[4] != 1U
            || plan.update_width != 15U
            || !check_driver(1U)) {
            return std::nullopt;
        }
        plan.shape = PureWaveShape::reduction7;
    } else if (count == 6U) {
        const auto* const read = pure_wave_operation<ReadSignal>(*process, 2U);
        const auto* const update = pure_wave_operation<WriteUpdateSlice>(*process, 3U);
        if (read == nullptr || update == nullptr
            || update->source != read->destination
            || !save_read(0U, *read)
            || !save_update(*update, read->destination)
            || plan.register_widths[0] != 15U
            || plan.update_width != 120U
            || !check_driver(plan.register_widths[0])) {
            return std::nullopt;
        }
        plan.shape = PureWaveShape::wide_copy6;
    } else {
        return std::nullopt;
    }

    if (runtime->flags != 0U
        || runtime->direct_signal_aval == nullptr
        || runtime->direct_signal_bval == nullptr
        || runtime->direct_update_slots == nullptr
        || runtime->direct_update_active_words == nullptr
        || runtime->direct_update_active_word_count
            <= plan.update_slot / 64U) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0U; index < plan.read_count; ++index) {
        const auto signal = plan.read_signals[index];
        if (signal >= runtime->direct_signal_count
            || (plan.register_widths[index] > 64U
                && (runtime->direct_wide_signal_aval == nullptr
                    || runtime->direct_wide_signal_bval == nullptr
                    || runtime->direct_wide_signal_offsets == nullptr
                    || signal >= runtime->direct_wide_signal_offset_count))) {
            return std::nullopt;
        }
        if (plan.register_widths[index] > 64U) {
            const auto offset = runtime->direct_wide_signal_offsets[signal];
            const auto words = (plan.register_widths[index] + 63U) / 64U;
            if (offset > runtime->direct_wide_word_count
                || words > runtime->direct_wide_word_count - offset) {
                return std::nullopt;
            }
        }
    }
    return plan;
}

void LlvmJit::Impl::ensure_pure_wave_kernels()
{
    if (pure_wave_kernels_initialized) {
        return;
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    const auto symbol_base
        = "fsim_pure_wave_" + std::to_string(next_cohort++);
    auto module = std::make_unique<llvm::Module>(
        symbol_base + ".module", *context);
    module->setDataLayout(jit->getDataLayout());
    module->setTargetTriple(jit->getTargetTriple());

    auto* const void_type = llvm::Type::getVoidTy(*context);
    auto* const i8 = llvm::Type::getInt8Ty(*context);
    auto* const i32 = llvm::Type::getInt32Ty(*context);
    auto* const i64 = llvm::Type::getInt64Ty(*context);
    auto* const pointer = llvm::PointerType::getUnqual(*context);
    auto* const member_type = llvm::StructType::create(
        *context,
        { pointer, pointer, pointer, pointer, i32, pointer, pointer, i64,
            pointer, pointer, pointer, i32, i32, i32, i32, i32, i32, i32 },
        "fsim_pure_wave_kernel_member");
    const auto& data_layout = jit->getDataLayout();
    const std::array<std::uint64_t, 18U> member_offsets {
        offsetof(NativePureWaveKernelMember, direct_signal_aval),
        offsetof(NativePureWaveKernelMember, direct_signal_bval),
        offsetof(NativePureWaveKernelMember, direct_wide_signal_aval),
        offsetof(NativePureWaveKernelMember, direct_wide_signal_bval),
        offsetof(NativePureWaveKernelMember, wide_signal_word_offset),
        offsetof(NativePureWaveKernelMember, update_slot),
        offsetof(NativePureWaveKernelMember, active_word),
        offsetof(NativePureWaveKernelMember, active_mask),
        offsetof(NativePureWaveKernelMember, queued),
        offsetof(NativePureWaveKernelMember, waiting),
        offsetof(NativePureWaveKernelMember, process_status),
        offsetof(NativePureWaveKernelMember, read_signal0),
        offsetof(NativePureWaveKernelMember, read_signal1),
        offsetof(NativePureWaveKernelMember, input_offset0),
        offsetof(NativePureWaveKernelMember, input_offset1),
        offsetof(NativePureWaveKernelMember, slice_offset),
        offsetof(NativePureWaveKernelMember, selector_offset),
        offsetof(NativePureWaveKernelMember, update_offset),
    };
    if (data_layout.getTypeAllocSize(member_type).getFixedValue()
            != sizeof(NativePureWaveKernelMember)) {
        throw LlvmJitError(
            "LLVM pure-wave member ABI size does not match the host ABI");
    }
    for (unsigned field = 0U; field < member_offsets.size(); ++field) {
        if (data_layout.getStructLayout(member_type)->getElementOffset(field)
            != member_offsets[field]) {
            throw LlvmJitError(
                "LLVM pure-wave member ABI offset does not match the host ABI");
        }
    }

    auto* const slot_type = llvm::StructType::create(
        *context,
        { i64, i64, i64, i64, i64, i32, i32, pointer, pointer, pointer,
            i32, i32 },
        "fsim_jit_update_slot_v1");
    auto* const prepared_view_type = llvm::StructType::create(
        *context, { pointer, i64, pointer, i32, i32, i32, pointer },
        "fsim_pure_wave_prepared_view");
    const std::array<std::uint64_t, 7U> prepared_view_offsets {
        offsetof(NativePureWavePreparedView, owner),
        offsetof(NativePureWavePreparedView, generation),
        offsetof(NativePureWavePreparedView, released),
        offsetof(NativePureWavePreparedView, shape),
        offsetof(NativePureWavePreparedView, signal0),
        offsetof(NativePureWavePreparedView, signal1),
        offsetof(NativePureWavePreparedView, kernel_member),
    };
    if (data_layout.getPointerSize() != sizeof(void*)
        || data_layout.getTypeAllocSize(prepared_view_type)
                .getFixedValue()
            != sizeof(NativePureWavePreparedView)) {
        throw LlvmJitError(
            "LLVM pure-wave prepared-view ABI does not match the host ABI");
    }
    for (unsigned field = 0U; field < prepared_view_offsets.size(); ++field) {
        if (data_layout.getStructLayout(prepared_view_type)
                ->getElementOffset(field)
            != prepared_view_offsets[field]) {
            throw LlvmJitError(
                "LLVM pure-wave prepared-view offset does not match the host ABI");
        }
    }
    const auto load_prepared_kernel_member = [&](llvm::IRBuilder<>& builder,
                                                 llvm::Value* prepared) {
        auto* const view_address = builder.CreateGEP(
            i8, prepared,
            llvm::ConstantInt::get(
                i64,
                offsetof(runtime::simir::PureWavePreparedMember,
                         compiler_view)));
        auto* const view = builder.CreateLoad(pointer, view_address);
        auto* const member_address = builder.CreateStructGEP(
            prepared_view_type, view, 6U);
        return builder.CreateLoad(pointer, member_address);
    };
    const auto load_member_pointer = [&](llvm::IRBuilder<>& builder,
                                         llvm::Value* member,
                                         const unsigned field) {
        return builder.CreateLoad(
            pointer, builder.CreateStructGEP(member_type, member, field));
    };
    const auto load_member_i32 = [&](llvm::IRBuilder<>& builder,
                                     llvm::Value* member,
                                     const unsigned field) {
        return builder.CreateLoad(
            i32, builder.CreateStructGEP(member_type, member, field));
    };
    const auto load_member_i64 = [&](llvm::IRBuilder<>& builder,
                                     llvm::Value* member,
                                     const unsigned field) {
        return builder.CreateLoad(
            i64, builder.CreateStructGEP(member_type, member, field));
    };
    const auto emit_terminal = [&](llvm::IRBuilder<>& builder,
                                   llvm::Value* member) {
        builder.CreateStore(
            llvm::ConstantInt::get(i8, 0U),
            load_member_pointer(builder, member, 8U));
        builder.CreateStore(
            llvm::ConstantInt::get(i8, 1U),
            load_member_pointer(builder, member, 9U));
        builder.CreateStore(
            llvm::ConstantInt::get(i8, 2U),
            load_member_pointer(builder, member, 10U));
    };

    const auto emit_narrow_update = [&](llvm::Function* function,
                                        llvm::IRBuilder<>& builder,
                                        llvm::Value* member,
                                        llvm::Value* source_aval,
                                        llvm::Value* source_bval) {
        auto* const slot = load_member_pointer(builder, member, 5U);
        auto* const active_word = load_member_pointer(builder, member, 6U);
        auto* const active_mask = load_member_i64(builder, member, 7U);
        auto* const update_offset = load_member_i32(builder, member, 17U);
        auto* const update_mask = builder.CreateShl(
            llvm::ConstantInt::get(i64, 1U),
            builder.CreateZExt(update_offset, i64));
        auto* const shifted_aval = builder.CreateSelect(
            builder.CreateICmpNE(
                source_aval, llvm::ConstantInt::get(i64, 0U)),
            update_mask, llvm::ConstantInt::get(i64, 0U));
        auto* const shifted_bval = builder.CreateSelect(
            builder.CreateICmpNE(
                source_bval, llvm::ConstantInt::get(i64, 0U)),
            update_mask, llvm::ConstantInt::get(i64, 0U));
        auto* const aval_address
            = builder.CreateStructGEP(slot_type, slot, 0U);
        auto* const bval_address
            = builder.CreateStructGEP(slot_type, slot, 1U);
        auto* const previous_aval = builder.CreateLoad(i64, aval_address);
        auto* const previous_bval = builder.CreateLoad(i64, bval_address);
        auto* const changed = builder.CreateICmpNE(
            builder.CreateAnd(
                builder.CreateOr(
                    builder.CreateXor(previous_aval, shifted_aval),
                    builder.CreateXor(previous_bval, shifted_bval)),
                update_mask),
            llvm::ConstantInt::get(i64, 0U));
        auto* const reserved = builder.CreateLoad(
            i32, builder.CreateStructGEP(slot_type, slot, 6U));
        auto* const shadow_valid = builder.CreateICmpNE(
            builder.CreateAnd(reserved, llvm::ConstantInt::get(i32, 1U)),
            llvm::ConstantInt::get(i32, 0U));
        auto* const enabled = builder.CreateOr(
            builder.CreateNot(shadow_valid), changed);
        auto* const store_block = llvm::BasicBlock::Create(
            *context, "update.enabled", function);
        auto* const terminal_block = llvm::BasicBlock::Create(
            *context, "update.terminal", function);
        builder.CreateCondBr(enabled, store_block, terminal_block);

        builder.SetInsertPoint(store_block);
        const auto merge_slot_plane = [&](const unsigned field,
                                          llvm::Value* shifted) {
            auto* const address
                = builder.CreateStructGEP(slot_type, slot, field);
            auto* const previous = builder.CreateLoad(i64, address);
            auto* const merged = builder.CreateOr(
                builder.CreateAnd(
                    previous, builder.CreateNot(update_mask)),
                builder.CreateAnd(shifted, update_mask));
            builder.CreateStore(merged, address);
        };
        merge_slot_plane(0U, shifted_aval);
        merge_slot_plane(1U, shifted_bval);
        auto* const mask_address
            = builder.CreateStructGEP(slot_type, slot, 4U);
        builder.CreateStore(
            builder.CreateOr(
                builder.CreateLoad(i64, mask_address), update_mask),
            mask_address);
        auto* const active_address
            = builder.CreateStructGEP(slot_type, slot, 5U);
        builder.CreateStore(
            builder.CreateOr(
                builder.CreateLoad(i32, active_address),
                llvm::ConstantInt::get(i32, 1U)),
            active_address);
        builder.CreateStore(
            builder.CreateOr(
                builder.CreateLoad(i64, active_word), active_mask),
            active_word);
        builder.CreateBr(terminal_block);
        builder.SetInsertPoint(terminal_block);
        emit_terminal(builder, member);
    };

    const auto emit_wide_update = [&](llvm::Function* function,
                                      llvm::IRBuilder<>& builder,
                                      llvm::Value* member,
                                      llvm::Value* source_aval,
                                      llvm::Value* source_bval) {
        auto* const slot = load_member_pointer(builder, member, 5U);
        auto* const active_word = load_member_pointer(builder, member, 6U);
        auto* const active_mask = load_member_i64(builder, member, 7U);
        auto* const update_offset = load_member_i32(builder, member, 17U);
        auto* const word_index = builder.CreateUDiv(
            update_offset, llvm::ConstantInt::get(i32, 64U));
        auto* const bit_offset = builder.CreateURem(
            update_offset, llvm::ConstantInt::get(i32, 64U));
        auto* const bit_offset64 = builder.CreateZExt(bit_offset, i64);
        auto* const shifted_aval
            = builder.CreateShl(source_aval, bit_offset64);
        auto* const shifted_bval
            = builder.CreateShl(source_bval, bit_offset64);
        auto* const first_mask = builder.CreateShl(
            llvm::ConstantInt::get(i64, 0x7fffU), bit_offset64);
        auto* const aval_words = builder.CreateLoad(
            pointer, builder.CreateStructGEP(slot_type, slot, 7U));
        auto* const bval_words = builder.CreateLoad(
            pointer, builder.CreateStructGEP(slot_type, slot, 8U));
        auto* const mask_words = builder.CreateLoad(
            pointer, builder.CreateStructGEP(slot_type, slot, 9U));
        auto* const spills = builder.CreateICmpUGT(
            bit_offset, llvm::ConstantInt::get(i32, 49U));
        const auto merge_word = [&](llvm::Value* words,
                                    llvm::Value* index,
                                    llvm::Value* source,
                                    llvm::Value* mask) {
            auto* const address = builder.CreateGEP(i64, words, index);
            auto* const previous = builder.CreateLoad(i64, address);
            auto* const merged = builder.CreateOr(
                builder.CreateAnd(previous, builder.CreateNot(mask)),
                builder.CreateAnd(source, mask));
            builder.CreateStore(merged, address);
        };
        merge_word(aval_words, word_index, shifted_aval, first_mask);
        merge_word(bval_words, word_index, shifted_bval, first_mask);
        merge_word(mask_words, word_index, first_mask, first_mask);
        auto* const spill_store = llvm::BasicBlock::Create(
            *context, "wide.update.spill", function);
        auto* const active_block = llvm::BasicBlock::Create(
            *context, "wide.update.active", function);
        builder.CreateCondBr(spills, spill_store, active_block);
        builder.SetInsertPoint(spill_store);
        auto* const store_spill_width = builder.CreateSub(
            bit_offset, llvm::ConstantInt::get(i32, 49U));
        auto* const store_spill_width64
            = builder.CreateZExt(store_spill_width, i64);
        auto* const store_spill_mask = builder.CreateSub(
            builder.CreateShl(
                llvm::ConstantInt::get(i64, 1U), store_spill_width64),
            llvm::ConstantInt::get(i64, 1U));
        auto* const store_shift_right = builder.CreateSub(
            llvm::ConstantInt::get(i32, 64U), bit_offset);
        auto* const store_shift_right64
            = builder.CreateZExt(store_shift_right, i64);
        auto* const store_next_word = builder.CreateAdd(
            word_index, llvm::ConstantInt::get(i32, 1U));
        auto* const store_spill_aval
            = builder.CreateLShr(source_aval, store_shift_right64);
        auto* const store_spill_bval
            = builder.CreateLShr(source_bval, store_shift_right64);
        merge_word(
            aval_words, store_next_word, store_spill_aval, store_spill_mask);
        merge_word(
            bval_words, store_next_word, store_spill_bval, store_spill_mask);
        merge_word(
            mask_words, store_next_word, store_spill_mask, store_spill_mask);
        builder.CreateBr(active_block);
        builder.SetInsertPoint(active_block);
        auto* const active_address
            = builder.CreateStructGEP(slot_type, slot, 5U);
        builder.CreateStore(
            builder.CreateOr(
                builder.CreateLoad(i32, active_address),
                llvm::ConstantInt::get(i32, 1U)),
            active_address);
        builder.CreateStore(
            builder.CreateOr(
                builder.CreateLoad(i64, active_word), active_mask),
            active_word);
        emit_terminal(builder, member);
    };

    const auto create_single = [&](const std::string& name) {
        auto* const type = llvm::FunctionType::get(
            void_type, { pointer }, false);
        auto* const function = llvm::Function::Create(
            type, llvm::Function::ExternalLinkage, name, *module);
        function->setCallingConv(llvm::CallingConv::C);
        return function;
    };

    const auto logic4_and_name = symbol_base + "_logic4_bit_and";
    auto* const and_function_type = llvm::FunctionType::get(
        void_type, { pointer, i32 }, false);
    auto* const and_function = llvm::Function::Create(
        and_function_type, llvm::Function::ExternalLinkage,
        logic4_and_name, *module);
    and_function->setCallingConv(llvm::CallingConv::C);
    auto* const and_members = and_function->getArg(0);
    auto* const and_count = and_function->getArg(1);
    auto* const and_entry = llvm::BasicBlock::Create(
        *context, "entry", and_function);
    auto* const and_header = llvm::BasicBlock::Create(
        *context, "loop.header", and_function);
    auto* const and_body = llvm::BasicBlock::Create(
        *context, "loop.body", and_function);
    auto* const and_exit = llvm::BasicBlock::Create(
        *context, "loop.exit", and_function);
    llvm::IRBuilder<> and_builder(and_entry);
    auto* const first_prepared_member = and_builder.CreateLoad(
        pointer,
        and_builder.CreateGEP(
            pointer, and_members, llvm::ConstantInt::get(i32, 0U)));
    auto* const first_member = load_prepared_kernel_member(
        and_builder, first_prepared_member);
    auto* const lhs_aval_array
        = load_member_pointer(and_builder, first_member, 0U);
    auto* const lhs_bval_array
        = load_member_pointer(and_builder, first_member, 1U);
    auto* const lhs_signal
        = load_member_i32(and_builder, first_member, 11U);
    auto* const rhs_signal
        = load_member_i32(and_builder, first_member, 12U);
    auto* const lhs_aval = and_builder.CreateLoad(
        i64, and_builder.CreateGEP(
            i64, lhs_aval_array, and_builder.CreateZExt(lhs_signal, i64)));
    auto* const lhs_bval = and_builder.CreateLoad(
        i64, and_builder.CreateGEP(
            i64, lhs_bval_array, and_builder.CreateZExt(lhs_signal, i64)));
    auto* const rhs_aval = and_builder.CreateLoad(
        i64, and_builder.CreateGEP(
            i64, lhs_aval_array, and_builder.CreateZExt(rhs_signal, i64)));
    auto* const rhs_bval = and_builder.CreateLoad(
        i64, and_builder.CreateGEP(
            i64, lhs_bval_array, and_builder.CreateZExt(rhs_signal, i64)));
    and_builder.CreateBr(and_header);
    and_builder.SetInsertPoint(and_header);
    auto* const and_index = and_builder.CreatePHI(i32, 2U);
    and_index->addIncoming(llvm::ConstantInt::get(i32, 0U), and_entry);
    and_builder.CreateCondBr(
        and_builder.CreateICmpULT(and_index, and_count),
        and_body, and_exit);
    and_builder.SetInsertPoint(and_body);
    auto* const and_prepared_member = and_builder.CreateLoad(
        pointer, and_builder.CreateGEP(pointer, and_members, and_index));
    auto* const and_member = load_prepared_kernel_member(
        and_builder, and_prepared_member);
    const auto bit_from_signal = [&](llvm::IRBuilder<>& builder,
                                     llvm::Value* signal,
                                     const unsigned offset_field) {
        auto* const offset = load_member_i32(
            builder, and_member, offset_field);
        auto* const shifted = builder.CreateLShr(
            signal, builder.CreateZExt(offset, i64));
        return builder.CreateAnd(shifted, llvm::ConstantInt::get(i64, 1U));
    };
    auto* const lhs_a = bit_from_signal(and_builder, lhs_aval, 13U);
    auto* const lhs_b = bit_from_signal(and_builder, lhs_bval, 13U);
    auto* const rhs_a = bit_from_signal(and_builder, rhs_aval, 14U);
    auto* const rhs_b = bit_from_signal(and_builder, rhs_bval, 14U);
    auto* const zero_i64 = llvm::ConstantInt::get(i64, 0U);
    auto* const lhs_zero = and_builder.CreateAnd(
        and_builder.CreateICmpEQ(lhs_a, zero_i64),
        and_builder.CreateICmpEQ(lhs_b, zero_i64));
    auto* const rhs_zero = and_builder.CreateAnd(
        and_builder.CreateICmpEQ(rhs_a, zero_i64),
        and_builder.CreateICmpEQ(rhs_b, zero_i64));
    auto* const known_zero = and_builder.CreateOr(lhs_zero, rhs_zero);
    auto* const lhs_one = and_builder.CreateAnd(
        and_builder.CreateICmpNE(lhs_a, zero_i64),
        and_builder.CreateICmpEQ(lhs_b, zero_i64));
    auto* const rhs_one = and_builder.CreateAnd(
        and_builder.CreateICmpNE(rhs_a, zero_i64),
        and_builder.CreateICmpEQ(rhs_b, zero_i64));
    auto* const known_one = and_builder.CreateAnd(lhs_one, rhs_one);
    auto* const and_result_aval = and_builder.CreateZExt(
        and_builder.CreateNot(known_zero), i64);
    auto* const and_result_bval = and_builder.CreateZExt(
        and_builder.CreateNot(
            and_builder.CreateOr(known_zero, known_one)), i64);
    emit_narrow_update(and_function, and_builder, and_member,
                       and_result_aval, and_result_bval);
    auto* const and_latch = llvm::BasicBlock::Create(
        *context, "loop.latch", and_function);
    and_builder.CreateBr(and_latch);
    and_builder.SetInsertPoint(and_latch);
    auto* const next_and_index = and_builder.CreateAdd(
        and_index, llvm::ConstantInt::get(i32, 1U));
    and_builder.CreateBr(and_header);
    and_index->addIncoming(next_and_index, and_latch);
    and_builder.SetInsertPoint(and_exit);
    and_builder.CreateRetVoid();

    const auto reducer_name = symbol_base + "_reducer31";
    auto* const reducer_function = create_single(reducer_name);
    auto* const reducer_entry = llvm::BasicBlock::Create(
        *context, "entry", reducer_function);
    llvm::IRBuilder<> reducer_builder(reducer_entry);
    auto* const reducer_member = reducer_function->getArg(0);
    auto* const wide_aval_array
        = load_member_pointer(reducer_builder, reducer_member, 2U);
    auto* const wide_bval_array
        = load_member_pointer(reducer_builder, reducer_member, 3U);
    auto* const base_word
        = load_member_i32(reducer_builder, reducer_member, 4U);
    auto* const slice_offset
        = load_member_i32(reducer_builder, reducer_member, 15U);
    auto* const source_word_index = reducer_builder.CreateAdd(
        base_word,
        reducer_builder.CreateUDiv(
            slice_offset, llvm::ConstantInt::get(i32, 64U)));
    auto* const source_bit_offset = reducer_builder.CreateURem(
        slice_offset, llvm::ConstantInt::get(i32, 64U));
    auto* const source_bit_offset64
        = reducer_builder.CreateZExt(source_bit_offset, i64);
    auto* const first_aval = reducer_builder.CreateLoad(
        i64, reducer_builder.CreateGEP(
            i64, wide_aval_array,
            reducer_builder.CreateZExt(source_word_index, i64)));
    auto* const first_bval = reducer_builder.CreateLoad(
        i64, reducer_builder.CreateGEP(
            i64, wide_bval_array,
            reducer_builder.CreateZExt(source_word_index, i64)));
    auto* const first_shifted_aval = reducer_builder.CreateLShr(
        first_aval, source_bit_offset64);
    auto* const first_shifted_bval = reducer_builder.CreateLShr(
        first_bval, source_bit_offset64);
    auto* const reducer_spill = llvm::BasicBlock::Create(
        *context, "input.spill", reducer_function);
    auto* const reducer_join = llvm::BasicBlock::Create(
        *context, "input.join", reducer_function);
    auto* const needs_input_spill = reducer_builder.CreateICmpUGT(
        source_bit_offset, llvm::ConstantInt::get(i32, 49U));
    reducer_builder.CreateCondBr(
        needs_input_spill, reducer_spill, reducer_join);
    reducer_builder.SetInsertPoint(reducer_spill);
    auto* const next_source_word = reducer_builder.CreateAdd(
        source_word_index, llvm::ConstantInt::get(i32, 1U));
    auto* const second_aval = reducer_builder.CreateLoad(
        i64, reducer_builder.CreateGEP(
            i64, wide_aval_array,
            reducer_builder.CreateZExt(next_source_word, i64)));
    auto* const second_bval = reducer_builder.CreateLoad(
        i64, reducer_builder.CreateGEP(
            i64, wide_bval_array,
            reducer_builder.CreateZExt(next_source_word, i64)));
    auto* const shift_left = reducer_builder.CreateSub(
        llvm::ConstantInt::get(i32, 64U), source_bit_offset);
    auto* const shift_left64 = reducer_builder.CreateZExt(shift_left, i64);
    auto* const spill_aval = reducer_builder.CreateOr(
        first_shifted_aval,
        reducer_builder.CreateShl(second_aval, shift_left64));
    auto* const spill_bval = reducer_builder.CreateOr(
        first_shifted_bval,
        reducer_builder.CreateShl(second_bval, shift_left64));
    reducer_builder.CreateBr(reducer_join);
    reducer_builder.SetInsertPoint(reducer_join);
    auto* const input_aval = reducer_builder.CreatePHI(i64, 2U);
    input_aval->addIncoming(first_shifted_aval, reducer_entry);
    input_aval->addIncoming(spill_aval, reducer_spill);
    auto* const input_bval = reducer_builder.CreatePHI(i64, 2U);
    input_bval->addIncoming(first_shifted_bval, reducer_entry);
    input_bval->addIncoming(spill_bval, reducer_spill);
    auto* const value_mask = llvm::ConstantInt::get(i64, 0x7fffU);
    auto* const original_aval
        = reducer_builder.CreateAnd(input_aval, value_mask);
    auto* const original_bval
        = reducer_builder.CreateAnd(input_bval, value_mask);
    auto* const narrow_aval_array
        = load_member_pointer(reducer_builder, reducer_member, 0U);
    auto* const narrow_bval_array
        = load_member_pointer(reducer_builder, reducer_member, 1U);
    auto* const shifted_signal
        = load_member_i32(reducer_builder, reducer_member, 12U);
    auto* const shifted_aval = reducer_builder.CreateLoad(
        i64, reducer_builder.CreateGEP(
            i64, narrow_aval_array,
            reducer_builder.CreateZExt(shifted_signal, i64)));
    auto* const shifted_bval = reducer_builder.CreateLoad(
        i64, reducer_builder.CreateGEP(
            i64, narrow_bval_array,
            reducer_builder.CreateZExt(shifted_signal, i64)));
    auto* const unknown = reducer_builder.CreateAnd(
        reducer_builder.CreateOr(original_bval, shifted_bval), value_mask);
    auto* const xor_aval = reducer_builder.CreateOr(
        reducer_builder.CreateAnd(
            reducer_builder.CreateXor(original_aval, shifted_aval),
            reducer_builder.CreateAnd(
                value_mask, reducer_builder.CreateNot(unknown))),
        unknown);
    auto* const xor_bval = unknown;
    auto* const selector_offset
        = load_member_i32(reducer_builder, reducer_member, 16U);
    auto* const selector = reducer_builder.CreateAnd(
        reducer_builder.CreateLShr(
            original_aval,
            reducer_builder.CreateZExt(selector_offset, i64)),
        llvm::ConstantInt::get(i64, 1U));
    auto* const selector_unknown = reducer_builder.CreateAnd(
        reducer_builder.CreateLShr(
            original_bval,
            reducer_builder.CreateZExt(selector_offset, i64)),
        llvm::ConstantInt::get(i64, 1U));
    auto* const selector_is_known = reducer_builder.CreateICmpEQ(
        selector_unknown, llvm::ConstantInt::get(i64, 0U));
    auto* const selector_is_one = reducer_builder.CreateAnd(
        selector_is_known,
        reducer_builder.CreateICmpNE(
            selector, llvm::ConstantInt::get(i64, 0U)));
    auto* const selector_is_zero = reducer_builder.CreateAnd(
        selector_is_known,
        reducer_builder.CreateICmpEQ(
            selector, llvm::ConstantInt::get(i64, 0U)));
    auto* const equal_bits = reducer_builder.CreateAnd(
        value_mask,
        reducer_builder.CreateNot(
            reducer_builder.CreateOr(
                reducer_builder.CreateXor(xor_aval, original_aval),
                reducer_builder.CreateXor(xor_bval, original_bval))));
    auto* const merged_unknown = reducer_builder.CreateAnd(
        value_mask, reducer_builder.CreateNot(equal_bits));
    auto* const unknown_aval = reducer_builder.CreateOr(
        reducer_builder.CreateAnd(xor_aval, equal_bits), merged_unknown);
    auto* const unknown_bval = reducer_builder.CreateOr(
        reducer_builder.CreateAnd(xor_bval, equal_bits), merged_unknown);
    auto* const reducer_result_aval = reducer_builder.CreateSelect(
        selector_is_one, xor_aval,
        reducer_builder.CreateSelect(
            selector_is_zero, original_aval, unknown_aval));
    auto* const reducer_result_bval = reducer_builder.CreateSelect(
        selector_is_one, xor_bval,
        reducer_builder.CreateSelect(
            selector_is_zero, original_bval, unknown_bval));
    emit_wide_update(reducer_function, reducer_builder, reducer_member,
                     reducer_result_aval, reducer_result_bval);
    reducer_builder.CreateRetVoid();

    const auto reduction_name = symbol_base + "_reduction7";
    auto* const reduction_function = create_single(reduction_name);
    auto* const reduction_entry = llvm::BasicBlock::Create(
        *context, "entry", reduction_function);
    llvm::IRBuilder<> reduction_builder(reduction_entry);
    auto* const reduction_member = reduction_function->getArg(0);
    auto* const reduction_aval_array
        = load_member_pointer(reduction_builder, reduction_member, 0U);
    auto* const reduction_bval_array
        = load_member_pointer(reduction_builder, reduction_member, 1U);
    auto* const reduction_signal
        = load_member_i32(reduction_builder, reduction_member, 11U);
    auto* const reduction_aval = reduction_builder.CreateLoad(
        i64, reduction_builder.CreateGEP(
            i64, reduction_aval_array,
            reduction_builder.CreateZExt(reduction_signal, i64)));
    auto* const reduction_bval = reduction_builder.CreateLoad(
        i64, reduction_builder.CreateGEP(
            i64, reduction_bval_array,
            reduction_builder.CreateZExt(reduction_signal, i64)));
    auto* const reduction_fold = reduction_builder.CreateXor(
        reduction_aval,
        reduction_builder.CreateLShr(
            reduction_aval, llvm::ConstantInt::get(i64, 4U)));
    auto* const reduction_fold2 = reduction_builder.CreateXor(
        reduction_fold,
        reduction_builder.CreateLShr(
            reduction_fold, llvm::ConstantInt::get(i64, 2U)));
    auto* const reduction_parity = reduction_builder.CreateAnd(
        reduction_builder.CreateXor(
            reduction_fold2,
            reduction_builder.CreateLShr(
                reduction_fold2, llvm::ConstantInt::get(i64, 1U))),
        llvm::ConstantInt::get(i64, 1U));
    auto* const reduction_unknown = reduction_builder.CreateICmpNE(
        reduction_builder.CreateAnd(
            reduction_bval, llvm::ConstantInt::get(i64, 0xffU)),
        llvm::ConstantInt::get(i64, 0U));
    auto* const reduced_aval = reduction_builder.CreateSelect(
        reduction_unknown, llvm::ConstantInt::get(i64, 1U),
        reduction_parity);
    auto* const reduced_bval = reduction_builder.CreateZExt(
        reduction_unknown, i64);
    emit_narrow_update(reduction_function, reduction_builder,
                       reduction_member, reduced_aval, reduced_bval);
    reduction_builder.CreateRetVoid();

    const auto copy_name = symbol_base + "_wide_copy6";
    auto* const copy_function = create_single(copy_name);
    auto* const copy_entry = llvm::BasicBlock::Create(
        *context, "entry", copy_function);
    llvm::IRBuilder<> copy_builder(copy_entry);
    auto* const copy_member = copy_function->getArg(0);
    auto* const copy_aval_array
        = load_member_pointer(copy_builder, copy_member, 0U);
    auto* const copy_bval_array
        = load_member_pointer(copy_builder, copy_member, 1U);
    auto* const copy_signal = load_member_i32(copy_builder, copy_member, 11U);
    auto* const copy_aval = copy_builder.CreateAnd(
        copy_builder.CreateLoad(
            i64, copy_builder.CreateGEP(
                i64, copy_aval_array,
                copy_builder.CreateZExt(copy_signal, i64))),
        value_mask);
    auto* const copy_bval = copy_builder.CreateAnd(
        copy_builder.CreateLoad(
            i64, copy_builder.CreateGEP(
                i64, copy_bval_array,
                copy_builder.CreateZExt(copy_signal, i64))),
        value_mask);
    emit_wide_update(copy_function, copy_builder, copy_member,
                     copy_aval, copy_bval);
    copy_builder.CreateRetVoid();

    const auto dispatch_name = symbol_base + "_dispatch";
    auto* const dispatch_type = llvm::FunctionType::get(
        void_type, { pointer, pointer, pointer, i32 }, false);
    auto* const dispatch_function = llvm::Function::Create(
        dispatch_type, llvm::Function::ExternalLinkage,
        dispatch_name, *module);
    dispatch_function->setCallingConv(llvm::CallingConv::C);
    auto* const dispatch_members = dispatch_function->getArg(0);
    auto* const task_ends = dispatch_function->getArg(1);
    auto* const task_shapes = dispatch_function->getArg(2);
    auto* const task_count = dispatch_function->getArg(3);
    auto* const dispatch_entry = llvm::BasicBlock::Create(
        *context, "entry", dispatch_function);
    auto* const dispatch_header = llvm::BasicBlock::Create(
        *context, "task.header", dispatch_function);
    auto* const dispatch_task = llvm::BasicBlock::Create(
        *context, "task.dispatch", dispatch_function);
    auto* const dispatch_latch = llvm::BasicBlock::Create(
        *context, "task.latch", dispatch_function);
    auto* const dispatch_invalid = llvm::BasicBlock::Create(
        *context, "task.invalid", dispatch_function);
    auto* const dispatch_exit = llvm::BasicBlock::Create(
        *context, "exit", dispatch_function);
    llvm::IRBuilder<> dispatch_builder(dispatch_entry);
    dispatch_builder.CreateBr(dispatch_header);
    dispatch_builder.SetInsertPoint(dispatch_header);
    auto* const task_index = dispatch_builder.CreatePHI(i32, 2U);
    task_index->addIncoming(
        llvm::ConstantInt::get(i32, 0U), dispatch_entry);
    auto* const first_member_index = dispatch_builder.CreatePHI(i32, 2U);
    first_member_index->addIncoming(
        llvm::ConstantInt::get(i32, 0U), dispatch_entry);
    dispatch_builder.CreateCondBr(
        dispatch_builder.CreateICmpULT(task_index, task_count),
        dispatch_task, dispatch_exit);

    dispatch_builder.SetInsertPoint(dispatch_task);
    auto* const task_index64 = dispatch_builder.CreateZExt(task_index, i64);
    auto* const task_end = dispatch_builder.CreateLoad(
        i32, dispatch_builder.CreateGEP(i32, task_ends, task_index64));
    auto* const task_shape = dispatch_builder.CreateLoad(
        i8, dispatch_builder.CreateGEP(i8, task_shapes, task_index64));
    auto* const task_member_count = dispatch_builder.CreateSub(
        task_end, first_member_index);
    auto* const task_first_member = dispatch_builder.CreateGEP(
        pointer, dispatch_members, first_member_index);
    auto* const task_first_prepared_member = dispatch_builder.CreateLoad(
        pointer, task_first_member);
    auto* const task_first_member_value = load_prepared_kernel_member(
        dispatch_builder, task_first_prepared_member);
    auto* const and_block = llvm::BasicBlock::Create(
        *context, "task.and", dispatch_function);
    auto* const reducer_block = llvm::BasicBlock::Create(
        *context, "task.reducer", dispatch_function);
    auto* const reduction_block = llvm::BasicBlock::Create(
        *context, "task.reduction", dispatch_function);
    auto* const copy_block = llvm::BasicBlock::Create(
        *context, "task.copy", dispatch_function);
    auto* const task_switch = dispatch_builder.CreateSwitch(
        task_shape, dispatch_invalid, 4U);
    task_switch->addCase(
        llvm::ConstantInt::get(i8, static_cast<unsigned>(
            PureWaveShape::logic4_bit_and)), and_block);
    task_switch->addCase(
        llvm::ConstantInt::get(i8, static_cast<unsigned>(
            PureWaveShape::reducer31)), reducer_block);
    task_switch->addCase(
        llvm::ConstantInt::get(i8, static_cast<unsigned>(
            PureWaveShape::reduction7)), reduction_block);
    task_switch->addCase(
        llvm::ConstantInt::get(i8, static_cast<unsigned>(
            PureWaveShape::wide_copy6)), copy_block);

    dispatch_builder.SetInsertPoint(and_block);
    dispatch_builder.CreateCall(
        and_function,
        { task_first_member, task_member_count });
    dispatch_builder.CreateBr(dispatch_latch);
    dispatch_builder.SetInsertPoint(reducer_block);
    dispatch_builder.CreateCall(
        reducer_function, { task_first_member_value });
    dispatch_builder.CreateBr(dispatch_latch);
    dispatch_builder.SetInsertPoint(reduction_block);
    dispatch_builder.CreateCall(
        reduction_function, { task_first_member_value });
    dispatch_builder.CreateBr(dispatch_latch);
    dispatch_builder.SetInsertPoint(copy_block);
    dispatch_builder.CreateCall(copy_function, { task_first_member_value });
    dispatch_builder.CreateBr(dispatch_latch);
    dispatch_builder.SetInsertPoint(dispatch_invalid);
    dispatch_builder.CreateRetVoid();
    dispatch_builder.SetInsertPoint(dispatch_latch);
    auto* const next_task_index = dispatch_builder.CreateAdd(
        task_index, llvm::ConstantInt::get(i32, 1U));
    task_index->addIncoming(next_task_index, dispatch_latch);
    first_member_index->addIncoming(task_end, dispatch_latch);
    dispatch_builder.CreateBr(dispatch_header);
    dispatch_builder.SetInsertPoint(dispatch_exit);
    dispatch_builder.CreateRetVoid();

    if (auto message = verify_error(*module); !message.empty()) {
        throw LlvmJitError(
            "generated invalid LLVM pure-wave kernels: " + message);
    }
    optimize_module(*module, options.optimization);
    if (auto error = jit->addIRModule(
            llvm::orc::ThreadSafeModule(
                std::move(module), std::move(context)))) {
        throw LlvmJitError(
            "cannot add LLVM pure-wave kernels: "
            + llvm_error(std::move(error)));
    }
    const auto lookup_kernel = [&](const std::string& symbol) {
        auto address = unwrap(
            jit->lookup(symbol), "cannot materialize LLVM pure-wave kernel");
        auto* const function = address.template toPtr<NativePureWaveSingle>();
        if (function == nullptr) {
            throw LlvmJitError(
                "LLVM returned a null pure-wave kernel address");
        }
        return function;
    };
    auto address = unwrap(
        jit->lookup(logic4_and_name),
        "cannot materialize LLVM pure-wave AND kernel");
    pure_wave_kernels.logic4_bit_and
        = address.template toPtr<NativePureWaveAndTask>();
    if (pure_wave_kernels.logic4_bit_and == nullptr) {
        throw LlvmJitError(
            "LLVM returned a null pure-wave AND kernel address");
    }
    pure_wave_kernels.reducer31 = lookup_kernel(reducer_name);
    pure_wave_kernels.reduction7 = lookup_kernel(reduction_name);
    pure_wave_kernels.wide_copy6 = lookup_kernel(copy_name);
    address = unwrap(
        jit->lookup(dispatch_name),
        "cannot materialize LLVM pure-wave dispatcher");
    pure_wave_kernels.dispatch
        = address.template toPtr<NativePureWaveDispatch>();
    if (pure_wave_kernels.dispatch == nullptr) {
        throw LlvmJitError(
            "LLVM returned a null pure-wave dispatcher address");
    }
    pure_wave_prepared_member_scratch.reserve(8192U);
    pure_wave_member_scratch.reserve(8192U);
    pure_wave_task_end_scratch.reserve(256U);
    pure_wave_task_shape_scratch.reserve(256U);
    pure_wave_kernels_initialized = true;
}

namespace {

template <typename ProcessInfo>
void validate_native_service_callbacks(
    const ProcessInfo& info, const fsim_jit_runtime_v1& runtime)
{
    if (!info.uses_coverage_sample && !info.uses_class_property_operation
        && !info.uses_event_triggered) {
        return;
    }
    if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1) {
        throw LlvmJitError("JIT runtime ABI version mismatch");
    }
    if (runtime.struct_size < kJitRuntimeV1PrefixSize) {
        throw LlvmJitError("JIT runtime ABI structure is too small");
    }
    if (info.uses_coverage_sample
        && (runtime.struct_size < kJitRuntimeCoverageSampleSize
            || runtime.sample_coverage == nullptr)) {
        throw LlvmJitError(
            "JIT runtime ABI requires sample_coverage for this process");
    }
    if (info.uses_class_property_operation
        && (runtime.struct_size < kJitRuntimeClassPropertySize
            || runtime.execute_class_property_operation == nullptr)) {
        throw LlvmJitError(
            "JIT runtime ABI requires class-property service callbacks "
            "for this process");
    }
    if (info.uses_event_triggered
        && (runtime.struct_size < kJitRuntimeEventTriggeredSize
            || runtime.query_event_triggered == nullptr)) {
        throw LlvmJitError(
            "JIT runtime ABI requires query_event_triggered for this "
            "process");
    }
}

} // namespace

JitResumeStatus
LlvmJit::resume(const JitProcessHandle process,
    const fsim_jit_runtime_v1& runtime,
    fsim_jit_frame_v1& frame,
    fsim_jit_resume_result_v1& result) const
{
    return resume(bind(process), runtime, frame, result);
}

JitResumeStatus LlvmJit::resume_prevalidated(
    const JitProcessBinding process,
    const fsim_jit_runtime_v1& runtime,
    fsim_jit_frame_v1& frame,
    fsim_jit_resume_result_v1& result) const
{
    if (!impl_ || process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid prevalidated LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    validate_native_service_callbacks(entry.info, runtime);
    const auto raw_status = entry.function(&runtime, &frame, &result);
    if (raw_status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
        if (const auto reason = decode_generated_runtime_error(result.delay)) {
            throw LlvmJitGeneratedRuntimeError(
                result.instruction, *reason);
        }
        throw LlvmJitError(
            "generated process returned an invalid runtime error reason");
    }
    return static_cast<JitResumeStatus>(raw_status);
}

std::size_t LlvmJit::resume_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("invalid LLVM process cohort");
    }

    constexpr std::size_t inline_cohort_capacity = 512U;
    std::array<const Impl::NativeEntry*, inline_cohort_capacity>
        inline_native_entries { };
    std::vector<const Impl::NativeEntry*> overflow_native_entries;
    std::span<const Impl::NativeEntry*> native_entries;
    if (entries.size() <= inline_cohort_capacity) {
        native_entries = {
            inline_native_entries.data(), entries.size()
        };
    } else {
        overflow_native_entries.resize(entries.size());
        native_entries = overflow_native_entries;
    }
    std::size_t prepared_index { };
    std::size_t key = entries.size();
    const bool region_mode = std::ranges::all_of(
        entries, [](const auto& entry) { return entry.active != nullptr; });
    const bool has_partial_region = std::ranges::any_of(
                                        entries, [](const auto& entry) { return entry.active != nullptr; })
        && !region_mode;
    if (has_partial_region) {
        throw LlvmJitError(
            "LLVM process region has incomplete active state");
    }
    const bool manages_process_state = std::ranges::all_of(
        entries,
        [](const auto& entry) {
            return entry.queued != nullptr
                && entry.waiting_on_static != nullptr
                && entry.process_status != nullptr;
        });
    const bool has_partial_process_state = std::ranges::any_of(
        entries,
        [](const auto& entry) {
            const auto count = static_cast<unsigned>(entry.queued != nullptr)
                + static_cast<unsigned>(entry.waiting_on_static != nullptr)
                + static_cast<unsigned>(entry.process_status != nullptr);
            return count != 0U && count != 3U;
        });
    if (has_partial_process_state
        || (!manages_process_state
            && std::ranges::any_of(
                entries,
                [](const auto& entry) {
                    return entry.queued != nullptr
                        || entry.waiting_on_static != nullptr
                        || entry.process_status != nullptr;
                }))) {
        throw LlvmJitError(
            "LLVM process cohort has incomplete scheduler state");
    }
    if (manages_process_state) {
        key ^= static_cast<std::size_t>(0x51f15e5dU)
            + (key << 6U) + (key >> 2U);
    }
    if (region_mode) {
        key ^= static_cast<std::size_t>(0x8f31a9c7U)
            + (key << 6U) + (key >> 2U);
    }
    for (const auto& entry : entries) {
        if (entry.process.owner_ != impl_.get()
            || entry.process.entry_ == nullptr || entry.runtime == nullptr
            || entry.frame == nullptr || entry.result == nullptr) {
            throw LlvmJitError("invalid prevalidated LLVM process cohort entry");
        }
        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        if (!region_mode || *entry.active != 0U) {
            validate_native_service_callbacks(native->info, *entry.runtime);
        }
        native_entries[prepared_index++] = native;
        const auto member_hash = std::hash<const void*> { }(native);
        key ^= member_hash + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    }

    NativeCohort* cohort { };
    {
        const std::scoped_lock lock { impl_->cohort_mutex };
        const auto found = impl_->cohort_functions.find(key);
        if (found != impl_->cohort_functions.end()) {
            const auto match = std::ranges::find_if(
                found->second,
                [&](const auto& candidate) {
                    return std::ranges::equal(
                               candidate->members, native_entries)
                        && candidate->manages_process_state
                        == manages_process_state
                        && candidate->region_mode == region_mode;
                });
            if (match != found->second.end()) {
                cohort = (*match)->function;
            }
        }
        if (cohort == nullptr) {
            const auto cohort_number = impl_->next_cohort++;
            const auto symbol = "fsim_process_cohort_"
                + std::to_string(cohort_number);
            auto context = std::make_unique<llvm::LLVMContext>();
            auto module = std::make_unique<llvm::Module>(
                symbol + ".module", *context);
            module->setDataLayout(impl_->jit->getDataLayout());
            module->setTargetTriple(impl_->jit->getTargetTriple());

            auto* const i32 = llvm::Type::getInt32Ty(*context);
            auto* const i8 = llvm::Type::getInt8Ty(*context);
            auto* const pointer = llvm::PointerType::getUnqual(*context);
            auto* const process_type = llvm::FunctionType::get(
                i32, { pointer, pointer, pointer }, false);
            auto* const cohort_type = llvm::FunctionType::get(
                i32,
                { pointer, pointer, pointer, pointer,
                    pointer, pointer, pointer, pointer, i32 },
                false);
            auto* const function = llvm::Function::Create(
                cohort_type, llvm::Function::ExternalLinkage,
                symbol, *module);
            function->setCallingConv(llvm::CallingConv::C);
            auto arguments = function->arg_begin();
            auto* const runtimes = &*arguments++;
            auto* const frames = &*arguments++;
            auto* const results = &*arguments++;
            auto* const statuses = &*arguments++;
            auto* const queued_states = &*arguments++;
            auto* const waiting_states = &*arguments++;
            auto* const process_statuses = &*arguments++;
            auto* const active_states = &*arguments++;
            auto* const count = &*arguments;

            auto* const entry_block = llvm::BasicBlock::Create(
                *context, "entry", function);
            llvm::IRBuilder<> builder(entry_block);
            auto* const valid_block = llvm::BasicBlock::Create(
                *context, "run", function);
            auto* const invalid_block = llvm::BasicBlock::Create(
                *context, "invalid", function);
            builder.CreateCondBr(
                builder.CreateICmpEQ(
                    count,
                    llvm::ConstantInt::get(i32, entries.size())),
                valid_block, invalid_block);
            builder.SetInsertPoint(invalid_block);
            builder.CreateRet(llvm::ConstantInt::get(i32, 0U));
            builder.SetInsertPoint(valid_block);

            for (std::size_t index = 0; index < native_entries.size(); ++index) {
                auto* const offset = llvm::ConstantInt::get(i32, index);
                const auto load_pointer = [&](llvm::Value* array) {
                    return builder.CreateLoad(
                        pointer,
                        builder.CreateGEP(pointer, array, offset));
                };
                auto* const queued_state = load_pointer(queued_states);
                auto* const waiting_state = load_pointer(waiting_states);
                auto* const process_status = load_pointer(process_statuses);
                if (region_mode) {
                    auto* const active_state = load_pointer(active_states);
                    auto* const run_member = llvm::BasicBlock::Create(
                        *context, "active", function);
                    auto* const continue_region = llvm::BasicBlock::Create(
                        *context, "continue", function);
                    builder.CreateCondBr(
                        builder.CreateICmpNE(
                            builder.CreateLoad(i8, active_state),
                            llvm::ConstantInt::get(i8, 0U)),
                        run_member, continue_region);
                    builder.SetInsertPoint(run_member);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), active_state);
                    if (manages_process_state) {
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 0U), queued_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 0U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), process_status);
                    }
                    auto callee = module->getOrInsertFunction(
                        native_entries[index]->symbol, process_type);
                    auto* const status = builder.CreateCall(
                        callee,
                        { load_pointer(runtimes), load_pointer(frames),
                            load_pointer(results) });
                    builder.CreateStore(
                        status,
                        builder.CreateGEP(i32, statuses, offset));
                    auto* const rearm = llvm::BasicBlock::Create(
                        *context, "rearm", function);
                    auto* const stop = llvm::BasicBlock::Create(
                        *context, "stop", function);
                    builder.CreateCondBr(
                        builder.CreateICmpEQ(
                            status,
                            llvm::ConstantInt::get(
                                i32,
                                FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY)),
                        rearm, stop);
                    builder.SetInsertPoint(stop);
                    builder.CreateRet(llvm::ConstantInt::get(
                        i32, static_cast<std::uint32_t>(index + 1U)));
                    builder.SetInsertPoint(rearm);
                    if (manages_process_state) {
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 2U), process_status);
                    }
                    builder.CreateBr(continue_region);
                    builder.SetInsertPoint(continue_region);
                    if (index + 1U == native_entries.size()) {
                        builder.CreateRet(llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                native_entries.size())));
                    }
                    continue;
                }
                if (manages_process_state) {
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), queued_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), waiting_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 1U), process_status);
                }
                auto callee = module->getOrInsertFunction(
                    native_entries[index]->symbol, process_type);
                auto* const status = builder.CreateCall(
                    callee,
                    { load_pointer(runtimes), load_pointer(frames),
                        load_pointer(results) });
                builder.CreateStore(
                    status,
                    builder.CreateGEP(i32, statuses, offset));
                const auto executed = static_cast<std::uint32_t>(index + 1U);
                if (static_cast<std::size_t>(executed)
                    == native_entries.size()) {
                    if (manages_process_state) {
                        auto* const rearm = llvm::BasicBlock::Create(
                            *context, "rearm", function);
                        auto* const finish = llvm::BasicBlock::Create(
                            *context, "finish", function);
                        builder.CreateCondBr(
                            builder.CreateICmpEQ(
                                status,
                                llvm::ConstantInt::get(
                                    i32,
                                    FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY)),
                            rearm, finish);
                        builder.SetInsertPoint(rearm);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 2U), process_status);
                        builder.CreateBr(finish);
                        builder.SetInsertPoint(finish);
                    }
                    builder.CreateRet(
                        llvm::ConstantInt::get(i32, executed));
                    break;
                }
                auto* const next = llvm::BasicBlock::Create(
                    *context, "next", function);
                auto* const stop = llvm::BasicBlock::Create(
                    *context, "stop", function);
                builder.CreateCondBr(
                    builder.CreateICmpEQ(
                        status,
                        llvm::ConstantInt::get(
                            i32,
                            FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY)),
                    next, stop);
                builder.SetInsertPoint(stop);
                builder.CreateRet(llvm::ConstantInt::get(i32, executed));
                builder.SetInsertPoint(next);
                if (manages_process_state) {
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 1U), waiting_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 2U), process_status);
                }
            }

            if (auto message = verify_error(*module); !message.empty()) {
                throw LlvmJitError(
                    "generated invalid LLVM process cohort: " + message);
            }
            optimize_module(*module, impl_->options.optimization);
            if (auto error = impl_->jit->addIRModule(
                    llvm::orc::ThreadSafeModule(
                        std::move(module), std::move(context)))) {
                throw LlvmJitError(
                    "cannot add LLVM process cohort: "
                    + llvm_error(std::move(error)));
            }
            auto address = unwrap(
                impl_->jit->lookup(symbol),
                "cannot materialize LLVM process cohort");
            cohort = address.template toPtr<NativeCohort>();
            if (cohort == nullptr) {
                throw LlvmJitError(
                    "LLVM returned a null process cohort address");
            }
            auto members = std::vector<const Impl::NativeEntry*> {
                native_entries.begin(), native_entries.end()
            };
            impl_->cohort_functions[key].push_back(
                std::make_unique<Impl::NativeCohortEntry>(
                    Impl::NativeCohortEntry {
                        cohort, std::move(members), manages_process_state,
                        region_mode }));
        }
    }

    std::array<const fsim_jit_runtime_v1*, inline_cohort_capacity>
        inline_runtimes { };
    std::array<fsim_jit_frame_v1*, inline_cohort_capacity>
        inline_frames { };
    std::array<fsim_jit_resume_result_v1*, inline_cohort_capacity>
        inline_results { };
    std::array<std::uint32_t, inline_cohort_capacity> inline_statuses { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_queued { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_waiting { };
    std::array<std::uint8_t*, inline_cohort_capacity>
        inline_process_statuses { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_active { };
    std::vector<const fsim_jit_runtime_v1*> overflow_runtimes;
    std::vector<fsim_jit_frame_v1*> overflow_frames;
    std::vector<fsim_jit_resume_result_v1*> overflow_results;
    std::vector<std::uint32_t> overflow_statuses;
    std::vector<std::uint8_t*> overflow_queued;
    std::vector<std::uint8_t*> overflow_waiting;
    std::vector<std::uint8_t*> overflow_process_statuses;
    std::vector<std::uint8_t*> overflow_active;
    std::span<const fsim_jit_runtime_v1*> runtimes;
    std::span<fsim_jit_frame_v1*> frames;
    std::span<fsim_jit_resume_result_v1*> results;
    std::span<std::uint32_t> statuses;
    std::span<std::uint8_t*> queued;
    std::span<std::uint8_t*> waiting;
    std::span<std::uint8_t*> process_statuses;
    std::span<std::uint8_t*> active;
    if (entries.size() <= inline_cohort_capacity) {
        runtimes = { inline_runtimes.data(), entries.size() };
        frames = { inline_frames.data(), entries.size() };
        results = { inline_results.data(), entries.size() };
        statuses = { inline_statuses.data(), entries.size() };
        queued = { inline_queued.data(), entries.size() };
        waiting = { inline_waiting.data(), entries.size() };
        process_statuses = {
            inline_process_statuses.data(), entries.size()
        };
        active = { inline_active.data(), entries.size() };
    } else {
        overflow_runtimes.resize(entries.size());
        overflow_frames.resize(entries.size());
        overflow_results.resize(entries.size());
        overflow_statuses.resize(entries.size());
        overflow_queued.resize(entries.size());
        overflow_waiting.resize(entries.size());
        overflow_process_statuses.resize(entries.size());
        overflow_active.resize(entries.size());
        runtimes = overflow_runtimes;
        frames = overflow_frames;
        results = overflow_results;
        statuses = overflow_statuses;
        queued = overflow_queued;
        waiting = overflow_waiting;
        process_statuses = overflow_process_statuses;
        active = overflow_active;
    }
    std::ranges::fill(
        statuses, std::numeric_limits<std::uint32_t>::max());
    for (std::size_t entry_index = 0U;
        entry_index < entries.size(); ++entry_index) {
        runtimes[entry_index] = entries[entry_index].runtime;
        frames[entry_index] = entries[entry_index].frame;
        results[entry_index] = entries[entry_index].result;
        queued[entry_index] = entries[entry_index].queued;
        waiting[entry_index] = entries[entry_index].waiting_on_static;
        process_statuses[entry_index]
            = entries[entry_index].process_status;
        active[entry_index] = entries[entry_index].active;
    }
    const auto executed = cohort(
        runtimes.data(), frames.data(), results.data(), statuses.data(),
        queued.data(), waiting.data(), process_statuses.data(),
        active.data(),
        static_cast<std::uint32_t>(entries.size()));
    if (executed == 0U || executed > entries.size()) {
        throw LlvmJitError(
            "generated LLVM process cohort returned an invalid count");
    }
    for (std::size_t index = 0; index < executed; ++index) {
        if (statuses[index]
            == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        entries[index].status = statuses[index];
        if (statuses[index] == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        entries[index].result->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        entries[index].result->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error reason");
            } catch (...) {
                entries[index].failure = std::current_exception();
            }
        }
    }
    return executed;
}

JitProcessCohortBinding LlvmJit::bind_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("invalid LLVM process cohort binding");
    }

    std::vector<const Impl::NativeEntry*> members;
    members.reserve(entries.size());
    std::size_t key = entries.size();
    const bool region_mode = std::ranges::all_of(
        entries, [](const auto& entry) { return entry.active != nullptr; });
    if (region_mode || std::ranges::any_of(entries, [](const auto& entry) {
            return entry.active != nullptr;
        })) {
        throw LlvmJitError(
            "stable LLVM cohort binding does not accept region entries");
    }
    const bool manages_process_state = std::ranges::all_of(
        entries,
        [](const auto& entry) {
            return entry.queued != nullptr
                && entry.waiting_on_static != nullptr
                && entry.process_status != nullptr;
        });
    if (std::ranges::any_of(
            entries,
            [&](const auto& entry) {
                const auto count
                    = static_cast<unsigned>(entry.queued != nullptr)
                    + static_cast<unsigned>(
                        entry.waiting_on_static != nullptr)
                    + static_cast<unsigned>(
                        entry.process_status != nullptr);
                return entry.process.owner_ != impl_.get()
                    || entry.process.entry_ == nullptr
                    || entry.runtime == nullptr || entry.frame == nullptr
                    || entry.result == nullptr
                    || (count != 0U && count != 3U)
                    || (manages_process_state && count != 3U);
            })) {
        throw LlvmJitError("invalid LLVM process cohort binding entry");
    }
    if (manages_process_state) {
        key ^= static_cast<std::size_t>(0x51f15e5dU)
            + (key << 6U) + (key >> 2U);
    }
    for (const auto& entry : entries) {
        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        members.push_back(native);
        const auto member_hash = std::hash<const void*> { }(native);
        key ^= member_hash + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    }

    const Impl::NativeCohortEntry* native_cohort { };
    std::scoped_lock lock { impl_->cohort_mutex };
    const auto found = impl_->cohort_functions.find(key);
    if (found != impl_->cohort_functions.end()) {
        const auto match = std::ranges::find_if(
            found->second,
            [&](const auto& candidate) {
                return std::ranges::equal(candidate->members, members)
                    && candidate->manages_process_state
                    == manages_process_state
                    && !candidate->region_mode;
            });
        if (match != found->second.end()) {
            native_cohort = match->get();
        }
    }
    if (native_cohort == nullptr) {
        throw LlvmJitError(
            "LLVM process cohort must be materialized before binding");
    }

    if (impl_->next_bound_cohort_generation == 0U) {
        throw LlvmJitError("LLVM process cohort binding generation exhausted");
    }
    auto bound = std::make_shared<Impl::NativeBoundCohortEntry>();
    bound->function = native_cohort->function;
    bound->generation = impl_->next_bound_cohort_generation++;
    bound->members = std::move(members);
    bound->manages_process_state = manages_process_state;
    bound->region_mode = false;
    bound->runtimes.reserve(entries.size());
    bound->frames.reserve(entries.size());
    bound->results.reserve(entries.size());
    bound->statuses.resize(entries.size());
    bound->queued.reserve(entries.size());
    bound->waiting.reserve(entries.size());
    bound->process_statuses.reserve(entries.size());
    bound->active.reserve(entries.size());
    for (const auto& entry : entries) {
        bound->runtimes.push_back(entry.runtime);
        bound->frames.push_back(entry.frame);
        bound->results.push_back(entry.result);
        bound->queued.push_back(entry.queued);
        bound->waiting.push_back(entry.waiting_on_static);
        bound->process_statuses.push_back(entry.process_status);
        bound->active.push_back(entry.active);
    }
    auto* const result = bound.get();
    impl_->bound_cohorts.push_back(std::move(bound));
    return JitProcessCohortBinding {
        impl_.get(), result, result->generation };
}

std::optional<JitPureWaveMemberBinding>
LlvmJit::bind_pure_wave_member_prevalidated(
    const JitPureWaveMember& member) const
{
    if (!impl_ || impl_->options.debug_instrumentation
        || member.native.process.owner_ != impl_.get()
        || member.native.process.entry_ == nullptr
        || member.native.runtime == nullptr || member.native.frame == nullptr
        || member.native.result == nullptr || member.native.queued == nullptr
        || member.native.waiting_on_static == nullptr
        || member.native.process_status == nullptr
        || member.native.active != nullptr) {
        return std::nullopt;
    }
    const auto* const native
        = static_cast<const Impl::NativeEntry*>(
            member.native.process.entry_);
    if (native->function == nullptr) {
        return std::nullopt;
    }
    const auto plan = Impl::classify_pure_wave_member(member, *native);
    if (!plan) {
        return std::nullopt;
    }

    const auto& runtime = *member.native.runtime;
    const auto& frame = *member.native.frame;
    const auto& layout = native->info.frame_layout;
    if (frame.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V1
        || frame.struct_size < sizeof(fsim_jit_frame_v1)
        || frame.layout_id_low != layout.layout_id_low
        || frame.layout_id_high != layout.layout_id_high
        || frame.register_count != layout.register_count
        || frame.program_counter != plan->expected_resume_instruction
        || frame.state != FSIM_JIT_FRAME_STATE_READY
        || frame.last_instruction != plan->expected_wait_instruction
        || frame.native_call_depth != 0U
        || *member.native.queued == 0U
        || *member.native.waiting_on_static == 0U
        || *member.native.process_status != 2U
        || runtime.direct_update_active_words == nullptr
        || runtime.direct_update_active_word_count
            <= plan->update_slot / 64U) {
        return std::nullopt;
    }
    const auto& update_slot
        = runtime.direct_update_slots[plan->update_slot];
    if (update_slot.width != plan->update_width) {
        return std::nullopt;
    }
    if (update_slot.width <= 64U) {
        if (plan->update_offset > update_slot.width
            || plan->register_widths[4]
                > update_slot.width - plan->update_offset
            || plan->update_offset + plan->register_widths[4] > 64U) {
            return std::nullopt;
        }
    } else if (update_slot.wide_aval == nullptr
        || update_slot.wide_bval == nullptr || update_slot.wide_mask == nullptr
        || update_slot.word_count < (update_slot.width + 63U) / 64U
        || plan->update_offset > update_slot.width
        || plan->register_widths[4]
            > update_slot.width - plan->update_offset) {
        return std::nullopt;
    }
    auto bound
        = std::make_shared<Impl::NativeBoundPureWaveMemberEntry>();
    bound->plan = *plan;
    bound->runtime = member.native.runtime;
    bound->frame = member.native.frame;
    bound->result = member.native.result;
    bound->queued = member.native.queued;
    bound->waiting = member.native.waiting_on_static;
    bound->process_status = member.native.process_status;
    bound->direct_read_signals = runtime.direct_read_signals;
    bound->direct_read_signal_count = runtime.direct_read_signal_count;
    bound->direct_update_slots = runtime.direct_update_slots;
    bound->direct_update_slot_count = runtime.direct_update_slot_count;
    bound->direct_update_active_words = runtime.direct_update_active_words;
    bound->direct_update_active_word_count
        = runtime.direct_update_active_word_count;
    bound->direct_signal_aval = runtime.direct_signal_aval;
    bound->direct_signal_bval = runtime.direct_signal_bval;
    bound->direct_signal_count = runtime.direct_signal_count;
    bound->direct_wide_signal_aval = runtime.direct_wide_signal_aval;
    bound->direct_wide_signal_bval = runtime.direct_wide_signal_bval;
    bound->direct_wide_signal_offsets = runtime.direct_wide_signal_offsets;
    bound->direct_wide_signal_offset_count
        = runtime.direct_wide_signal_offset_count;
    bound->direct_wide_word_count = runtime.direct_wide_word_count;
    bound->layout_id_low = layout.layout_id_low;
    bound->layout_id_high = layout.layout_id_high;
    bound->frame_register_count = layout.register_count;
    bound->expected_resume_instruction = plan->expected_resume_instruction;
    bound->expected_wait_instruction = plan->expected_wait_instruction;
    auto& kernel_member = bound->kernel_member;
    kernel_member.direct_signal_aval = runtime.direct_signal_aval;
    kernel_member.direct_signal_bval = runtime.direct_signal_bval;
    kernel_member.direct_wide_signal_aval
        = runtime.direct_wide_signal_aval;
    kernel_member.direct_wide_signal_bval
        = runtime.direct_wide_signal_bval;
    if (plan->shape == Impl::PureWaveShape::reducer31) {
        kernel_member.wide_signal_word_offset
            = runtime.direct_wide_signal_offsets[plan->read_signals[0]];
    }
    kernel_member.update_slot
        = &runtime.direct_update_slots[plan->update_slot];
    kernel_member.active_word
        = &runtime.direct_update_active_words[plan->update_slot / 64U];
    kernel_member.active_mask
        = UINT64_C(1) << (plan->update_slot % 64U);
    kernel_member.queued = bound->queued;
    kernel_member.waiting = bound->waiting;
    kernel_member.process_status = bound->process_status;
    kernel_member.read_signal0 = plan->read_signals[0];
    kernel_member.read_signal1 = plan->read_signals[1];
    kernel_member.input_offset0 = plan->input_lhs_offset;
    kernel_member.input_offset1 = plan->input_rhs_offset;
    kernel_member.slice_offset = plan->slice_offset;
    kernel_member.selector_offset = plan->selector_offset;
    kernel_member.update_offset = plan->update_offset;

    const auto* const result_pointer = bound.get();
    std::uint64_t generation { };
    {
        const std::scoped_lock lock { impl_->cohort_mutex };
        if (impl_->next_pure_wave_member_generation == 0U) {
            return std::nullopt;
        }
        generation = impl_->next_pure_wave_member_generation++;
        bound->generation = generation;
        bound->prepared_view = {
            impl_.get(), generation, &bound->released,
            static_cast<std::uint32_t>(plan->shape), plan->read_signals[0],
            plan->read_signals[1], &bound->kernel_member };
        impl_->bound_pure_wave_members.emplace(result_pointer, bound);
    }
    return JitPureWaveMemberBinding {
        impl_.get(), result_pointer, generation };
}

std::optional<JitPureWaveMemberLease>
LlvmJit::acquire_pure_wave_member_lease(
    const JitPureWaveMemberBinding member) const
{
    if (!impl_ || member.owner_ != impl_.get() || member.entry_ == nullptr) {
        return std::nullopt;
    }
    const auto* const entry
        = static_cast<const Impl::NativeBoundPureWaveMemberEntry*>(
            member.entry_);
    const std::scoped_lock lock { impl_->cohort_mutex };
    const auto found = impl_->bound_pure_wave_members.find(entry);
    if (found == impl_->bound_pure_wave_members.end()
        || found->second->generation != member.generation_
        || found->second->released.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return JitPureWaveMemberLease {
        impl_.get(), entry, &found->second->prepared_view,
        member.generation_, found->second };
}

bool LlvmJit::release_pure_wave_member_binding(
    const JitPureWaveMemberBinding member) const
{
    if (!impl_ || member.owner_ != impl_.get() || member.entry_ == nullptr) {
        return false;
    }
    const auto* const entry
        = static_cast<const Impl::NativeBoundPureWaveMemberEntry*>(
            member.entry_);
    const std::scoped_lock lock { impl_->cohort_mutex };
    const auto found = impl_->bound_pure_wave_members.find(entry);
    if (found == impl_->bound_pure_wave_members.end()
        || found->second->generation != member.generation_) {
        return false;
    }
    found->second->released.store(true, std::memory_order_release);
    impl_->bound_pure_wave_members.erase(found);
    return true;
}

std::optional<JitProcessCohortBinding>
LlvmJit::bind_pure_wave_prevalidated(
    const std::span<const JitPureWaveMemberBinding> members,
    const std::span<const std::size_t> task_ends) const
{
    if (!impl_ || members.empty() || task_ends.empty()
        || members.size() > 8192U || task_ends.size() > 256U
        || task_ends.back() != members.size()) {
        return std::nullopt;
    }

    std::vector<std::shared_ptr<Impl::NativeBoundPureWaveMemberEntry>>
        bound_members;
    bound_members.reserve(members.size());
    std::unordered_set<const void*> output_slots;
    output_slots.reserve(members.size());
    std::unordered_set<const void*> active_words;
    active_words.reserve(members.size());
    std::unordered_set<const void*> scheduler_addresses;
    scheduler_addresses.reserve(members.size() * 3U);

    std::size_t task_begin { };
    std::scoped_lock lock { impl_->cohort_mutex };
    for (const auto task_end : task_ends) {
        if (task_end <= task_begin || task_end > members.size()) {
            return std::nullopt;
        }
        std::optional<Impl::PureWaveShape> task_shape;
        const Impl::NativeBoundPureWaveMemberEntry* first_member { };
        for (std::size_t index = task_begin; index < task_end; ++index) {
            const auto& token = members[index];
            if (token.owner_ != impl_.get() || token.entry_ == nullptr) {
                return std::nullopt;
            }
            const auto* const key
                = static_cast<const Impl::NativeBoundPureWaveMemberEntry*>(
                    token.entry_);
            const auto found = impl_->bound_pure_wave_members.find(key);
            if (found == impl_->bound_pure_wave_members.end()
                || found->second->generation != token.generation_
                || found->second->released.load(std::memory_order_acquire)) {
                return std::nullopt;
            }
            const auto& bound = *found->second;
            const auto& runtime = *bound.runtime;
            const auto& frame = *bound.frame;
            if (runtime.flags != 0U
                || runtime.direct_read_signals
                    != bound.direct_read_signals
                || runtime.direct_read_signal_count
                    != bound.direct_read_signal_count
                || runtime.direct_update_slots != bound.direct_update_slots
                || runtime.direct_update_slot_count
                    != bound.direct_update_slot_count
                || runtime.direct_update_active_words
                    != bound.direct_update_active_words
                || runtime.direct_update_active_word_count
                    != bound.direct_update_active_word_count
                || runtime.direct_signal_aval != bound.direct_signal_aval
                || runtime.direct_signal_bval != bound.direct_signal_bval
                || runtime.direct_signal_count != bound.direct_signal_count
                || runtime.direct_wide_signal_aval
                    != bound.direct_wide_signal_aval
                || runtime.direct_wide_signal_bval
                    != bound.direct_wide_signal_bval
                || runtime.direct_wide_signal_offsets
                    != bound.direct_wide_signal_offsets
                || runtime.direct_wide_signal_offset_count
                    != bound.direct_wide_signal_offset_count
                || runtime.direct_wide_word_count
                    != bound.direct_wide_word_count
                || frame.layout_id_low != bound.layout_id_low
                || frame.layout_id_high != bound.layout_id_high
                || frame.register_count != bound.frame_register_count
                || frame.program_counter
                    != bound.expected_resume_instruction
                || frame.state != FSIM_JIT_FRAME_STATE_READY
                || frame.last_instruction != bound.expected_wait_instruction
                || frame.native_call_depth != 0U
                || *bound.queued == 0U || *bound.waiting == 0U
                || *bound.process_status != 2U) {
                return std::nullopt;
            }
            if (task_shape && *task_shape != bound.plan.shape) {
                return std::nullopt;
            }
            task_shape = bound.plan.shape;
            if (first_member == nullptr) {
                first_member = &bound;
            } else if (bound.plan.shape
                    == Impl::PureWaveShape::logic4_bit_and
                && (bound.plan.read_signals[0]
                        != first_member->plan.read_signals[0]
                    || bound.plan.read_signals[1]
                        != first_member->plan.read_signals[1]
                    || runtime.direct_signal_aval
                        != first_member->runtime->direct_signal_aval
                    || runtime.direct_signal_bval
                        != first_member->runtime->direct_signal_bval)) {
                return std::nullopt;
            }
            if (bound.plan.shape != Impl::PureWaveShape::logic4_bit_and
                && task_end - task_begin != 1U) {
                return std::nullopt;
            }
            const auto* const slot = &runtime.direct_update_slots[
                bound.plan.update_slot];
            const auto* const active_word
                = &runtime.direct_update_active_words[
                    bound.plan.update_slot / 64U];
            if (!output_slots.insert(slot).second
                || !active_words.insert(active_word).second) {
                return std::nullopt;
            }
            for (const auto* address : {
                     bound.queued, bound.waiting, bound.process_status }) {
                if (address == nullptr
                    || !scheduler_addresses.insert(address).second) {
                    return std::nullopt;
                }
            }
            bound_members.push_back(found->second);
        }
        task_begin = task_end;
    }

    if (impl_->next_bound_cohort_generation == 0U) {
        return std::nullopt;
    }
    impl_->ensure_pure_wave_kernels();
    auto bound = std::make_shared<Impl::NativeBoundPureWaveEntry>();
    bound->generation = impl_->next_bound_cohort_generation++;
    bound->members = std::move(bound_members);
    bound->task_ends.assign(task_ends.begin(), task_ends.end());
    bound->task_shapes.reserve(task_ends.size());
    bound->kernel_task_ends.reserve(task_ends.size());
    bound->kernel_task_shapes.reserve(task_ends.size());
    bound->kernel_members.reserve(members.size());
    std::size_t task_shape_begin { };
    for (const auto task_end : task_ends) {
        const auto shape
            = bound->members[task_shape_begin]->plan.shape;
        bound->task_shapes.push_back(shape);
        bound->kernel_task_ends.push_back(
            static_cast<std::uint32_t>(task_end));
        bound->kernel_task_shapes.push_back(
            static_cast<std::uint8_t>(shape));
        task_shape_begin = task_end;
    }
    for (const auto& member : bound->members) {
        const auto& runtime = *member->runtime;
        const auto& plan = member->plan;
        NativePureWaveKernelMember kernel_member;
        kernel_member.direct_signal_aval = runtime.direct_signal_aval;
        kernel_member.direct_signal_bval = runtime.direct_signal_bval;
        kernel_member.direct_wide_signal_aval
            = runtime.direct_wide_signal_aval;
        kernel_member.direct_wide_signal_bval
            = runtime.direct_wide_signal_bval;
        if (plan.shape == Impl::PureWaveShape::reducer31) {
            kernel_member.wide_signal_word_offset
                = runtime.direct_wide_signal_offsets[plan.read_signals[0]];
        }
        kernel_member.update_slot
            = &runtime.direct_update_slots[plan.update_slot];
        kernel_member.active_word
            = &runtime.direct_update_active_words[
                plan.update_slot / 64U];
        kernel_member.active_mask
            = UINT64_C(1) << (plan.update_slot % 64U);
        kernel_member.queued = member->queued;
        kernel_member.waiting = member->waiting;
        kernel_member.process_status = member->process_status;
        kernel_member.read_signal0 = plan.read_signals[0];
        kernel_member.read_signal1 = plan.read_signals[1];
        kernel_member.input_offset0 = plan.input_lhs_offset;
        kernel_member.input_offset1 = plan.input_rhs_offset;
        kernel_member.slice_offset = plan.slice_offset;
        kernel_member.selector_offset = plan.selector_offset;
        kernel_member.update_offset = plan.update_offset;
        bound->kernel_members.push_back(kernel_member);
    }
    auto* const result = bound.get();
    const auto generation = bound->generation;
    impl_->bound_pure_waves.emplace(result, std::move(bound));
    return JitProcessCohortBinding { impl_.get(), result, generation };
}

std::optional<JitProcessCohortBinding>
LlvmJit::bind_logic4_bit_and_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries,
    const std::span<const JitProcessCohortLogic4BitAndMember> members) const
{
    if (!impl_ || impl_->options.debug_instrumentation
        || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()
        || members.size() != entries.size()) {
        return std::nullopt;
    }

    const auto state_pointer_count = [](const auto& entry) {
        return static_cast<unsigned>(entry.queued != nullptr)
            + static_cast<unsigned>(entry.waiting_on_static != nullptr)
            + static_cast<unsigned>(entry.process_status != nullptr);
    };
    const bool manages_process_state = state_pointer_count(entries.front()) == 3U;
    std::vector<const Impl::NativeEntry*> native_members;
    native_members.reserve(entries.size());
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        const auto& member = members[index];
        if (entry.process.owner_ != impl_.get()
            || entry.process.entry_ == nullptr || entry.runtime == nullptr
            || entry.frame == nullptr || entry.result == nullptr
            || entry.active != nullptr
            || state_pointer_count(entry) != (manages_process_state ? 3U : 0U)) {
            return std::nullopt;
        }

        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        const auto& info = native->info;
        const auto& layout = info.frame_layout;
        if (info.operation_count != 10U || !info.requires_resume
            || !info.uses_write_update_slice
            || layout.uses_logic9 || layout.string_register_count != 0U
            || layout.tracks_register_initialization
                != member.tracks_register_initialization
            || layout.register_widths.size() != layout.register_count
            || layout.register_word_offsets.size() != layout.register_count) {
            return std::nullopt;
        }
        for (const auto flag : Impl::ProcessInfo::flags) {
            const bool supported_flag
                = flag == &Impl::ProcessInfo::requires_resume
                || flag == &Impl::ProcessInfo::uses_debug_points
                || flag == &Impl::ProcessInfo::uses_write_update_slice;
            if (info.*flag && !supported_flag) {
                return std::nullopt;
            }
        }

        const std::array<const JitProcessCohortLogic4BitAndRegisterSlot*, 5>
            register_slots {
                &member.read_lhs,
                &member.read_rhs,
                &member.extract_lhs,
                &member.extract_rhs,
                &member.result,
            };
        std::array<std::uint64_t, 5> slot_begin { };
        std::array<std::uint64_t, 5> slot_end { };
        for (std::size_t slot_index = 0U;
            slot_index < register_slots.size(); ++slot_index) {
            const auto& slot = *register_slots[slot_index];
            if (slot.register_id >= layout.register_count
                || slot.width == 0U || slot.width > 64U
                || (slot.frame_resident
                    && !member.tracks_register_initialization)
                || layout.register_widths[slot.register_id] != slot.width
                || layout.register_word_offsets[slot.register_id]
                    != slot.word_offset) {
                return std::nullopt;
            }
            const auto word_count
                = (static_cast<std::uint64_t>(slot.width) + 63U) / 64U;
            slot_begin[slot_index] = slot.word_offset;
            slot_end[slot_index]
                = static_cast<std::uint64_t>(slot.word_offset) + word_count;
            if (slot_end[slot_index] > layout.register_word_count) {
                return std::nullopt;
            }
            for (std::size_t prior = 0U; prior < slot_index; ++prior) {
                if (slot.register_id == register_slots[prior]->register_id
                    || (slot_begin[slot_index] < slot_end[prior]
                        && slot_begin[prior] < slot_end[slot_index])) {
                    return std::nullopt;
                }
            }
        }
        if (member.extract_lhs.width != 1U
            || member.extract_rhs.width != 1U || member.result.width != 1U
            || member.extract_lhs_offset >= member.read_lhs.width
            || member.extract_rhs_offset >= member.read_rhs.width
            || member.direct_read_lhs_slot >= layout.direct_read_signals.size()
            || member.direct_read_rhs_slot >= layout.direct_read_signals.size()
            || member.direct_update_slot >= layout.direct_update_signals.size()
            || member.update_offset >= 64U) {
            return std::nullopt;
        }

        const auto& runtime = *entry.runtime;
        const auto& frame = *entry.frame;
        const auto& result = *entry.result;
        const bool has_frame_resident_register
            = std::ranges::any_of(
                register_slots,
                [](const auto* slot) { return slot->frame_resident; });
        if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1
            || runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || frame.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V1
            || frame.struct_size < sizeof(fsim_jit_frame_v1)
            || frame.layout_id_low != layout.layout_id_low
            || frame.layout_id_high != layout.layout_id_high
            || frame.register_count != layout.register_count
            || (has_frame_resident_register
                && (frame.register_aval == nullptr
                    || frame.register_bval == nullptr))
            || (member.tracks_register_initialization
                && frame.register_initialized == nullptr)
            || result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1
            || result.struct_size < sizeof(fsim_jit_resume_result_v1)
            || runtime.direct_signal_aval == nullptr
            || runtime.direct_signal_bval == nullptr
            || runtime.direct_read_signals == nullptr
            || runtime.direct_read_signal_count
                <= std::max(
                    member.direct_read_lhs_slot,
                    member.direct_read_rhs_slot)
            || runtime.direct_signal_count == 0U
            || runtime.direct_update_slots == nullptr
            || runtime.direct_update_slot_count <= member.direct_update_slot) {
            return std::nullopt;
        }

        const auto lhs_signal = runtime.direct_read_signals[
            member.direct_read_lhs_slot];
        const auto rhs_signal = runtime.direct_read_signals[
            member.direct_read_rhs_slot];
        if (lhs_signal >= runtime.direct_signal_count
            || rhs_signal >= runtime.direct_signal_count) {
            return std::nullopt;
        }
        const auto& update
            = runtime.direct_update_slots[member.direct_update_slot];
        if (update.width == 0U || update.width > 64U
            || member.update_offset >= update.width) {
            return std::nullopt;
        }
        if (impl_->options.require_direct_update_slots
            && (runtime.direct_update_active_words == nullptr
                || runtime.direct_update_active_word_count
                    <= member.direct_update_slot / 64U)) {
            return std::nullopt;
        }
        if (runtime.direct_update_active_words != nullptr
            && runtime.direct_update_active_word_count
                <= member.direct_update_slot / 64U) {
            return std::nullopt;
        }
        native_members.push_back(native);
    }

    std::size_t key = members.size();
    const auto combine = [&](const std::size_t value) {
        key ^= value + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    };
    combine(static_cast<std::size_t>(manages_process_state));
    for (const auto& member : members) {
        const auto combine_slot = [&](
            const JitProcessCohortLogic4BitAndRegisterSlot& slot) {
            combine(slot.register_id);
            combine(slot.word_offset);
            combine(slot.width);
            combine(static_cast<std::size_t>(slot.frame_resident));
        };
        combine_slot(member.read_lhs);
        combine_slot(member.read_rhs);
        combine_slot(member.extract_lhs);
        combine_slot(member.extract_rhs);
        combine_slot(member.result);
        combine(member.direct_read_lhs_slot);
        combine(member.direct_read_rhs_slot);
        combine(member.extract_lhs_offset);
        combine(member.extract_rhs_offset);
        combine(member.direct_update_slot);
        combine(member.update_offset);
        combine(static_cast<std::size_t>(
            member.tracks_register_initialization));
    }

    NativeCohort* fused_cohort { };
    std::scoped_lock lock { impl_->cohort_mutex };
    const auto found = impl_->logic4_bit_and_cohort_functions.find(key);
    if (found != impl_->logic4_bit_and_cohort_functions.end()) {
        const auto match = std::ranges::find_if(
            found->second,
            [&](const auto& candidate) {
                return candidate->manages_process_state
                        == manages_process_state
                    && std::ranges::equal(candidate->members, members);
            });
        if (match != found->second.end()) {
            fused_cohort = (*match)->function;
        }
    }

    if (fused_cohort == nullptr) {
        const auto cohort_number = impl_->next_cohort++;
        const auto symbol = "fsim_logic4_bit_and_cohort_"
            + std::to_string(cohort_number);
        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = std::make_unique<llvm::Module>(
            symbol + ".module", *context);
        module->setDataLayout(impl_->jit->getDataLayout());
        module->setTargetTriple(impl_->jit->getTargetTriple());

        auto* const i8 = llvm::Type::getInt8Ty(*context);
        auto* const i32 = llvm::Type::getInt32Ty(*context);
        auto* const i64 = llvm::Type::getInt64Ty(*context);
        auto* const pointer = llvm::PointerType::getUnqual(*context);
        auto* const function_type = llvm::FunctionType::get(
            i32,
            { pointer, pointer, pointer, pointer, pointer, pointer, pointer,
                pointer, i32 },
            false);
        auto* const function = llvm::Function::Create(
            function_type, llvm::Function::ExternalLinkage, symbol, *module);
        function->setCallingConv(llvm::CallingConv::C);
        auto arguments = function->arg_begin();
        auto* const runtimes = &*arguments++;
        auto* const frames = &*arguments++;
        auto* const results = &*arguments++;
        auto* const statuses = &*arguments++;
        auto* const queued_states = &*arguments++;
        auto* const waiting_states = &*arguments++;
        auto* const process_statuses = &*arguments++;
        ++arguments;
        auto* const count = &*arguments;

        auto* const entry_block = llvm::BasicBlock::Create(
            *context, "entry", function);
        auto* const run_block = llvm::BasicBlock::Create(
            *context, "run", function);
        auto* const invalid_block = llvm::BasicBlock::Create(
            *context, "invalid", function);

        std::vector<std::array<llvm::Value*, 5>> local_register_aval(
            members.size());
        std::vector<std::array<llvm::Value*, 5>> local_register_bval(
            members.size());
        {
            llvm::IRBuilder<> alloca_builder(entry_block);
            for (std::size_t member_index = 0U;
                member_index < members.size(); ++member_index) {
                const auto& member = members[member_index];
                const std::array<
                    const JitProcessCohortLogic4BitAndRegisterSlot*, 5>
                    register_slots {
                        &member.read_lhs,
                        &member.read_rhs,
                        &member.extract_lhs,
                        &member.extract_rhs,
                        &member.result,
                    };
                for (std::size_t slot_index = 0U;
                    slot_index < register_slots.size(); ++slot_index) {
                    if (register_slots[slot_index]->frame_resident) {
                        continue;
                    }
                    local_register_aval[member_index][slot_index]
                        = alloca_builder.CreateAlloca(
                            i64, nullptr, "register.aval.local");
                    local_register_bval[member_index][slot_index]
                        = alloca_builder.CreateAlloca(
                            i64, nullptr, "register.bval.local");
                }
            }
        }

        llvm::IRBuilder<> builder(entry_block);
        builder.CreateCondBr(
            builder.CreateICmpEQ(
                count, llvm::ConstantInt::get(i32, members.size())),
            run_block, invalid_block);
        builder.SetInsertPoint(invalid_block);
        builder.CreateRet(llvm::ConstantInt::get(i32, 0U));
        builder.SetInsertPoint(run_block);

        const auto abi_field_address = [&](llvm::Value* object,
                                           const std::size_t offset,
                                           llvm::Type*) {
            auto* const byte_address = builder.CreateGEP(
                i8, object, llvm::ConstantInt::get(i64, offset));
            return byte_address;
        };
        const auto load_abi_field = [&](llvm::Value* object,
                                        const std::size_t offset,
                                        llvm::Type* type,
                                        const llvm::Twine& name) {
            return builder.CreateLoad(
                type, abi_field_address(object, offset, type), name);
        };
        auto* const update_slot_type = llvm::StructType::create(
            *context,
            { i64, i64, i64, i64, i64, i32, i32,
                pointer, pointer, pointer, i32, i32 },
            "fsim_jit_update_slot_v1");

        for (std::size_t index = 0U; index < members.size(); ++index) {
            const auto& member = members[index];
            auto* const member_offset = llvm::ConstantInt::get(i32, index);
            const auto load_array_pointer = [&](llvm::Value* array) {
                return builder.CreateLoad(
                    pointer, builder.CreateGEP(pointer, array, member_offset));
            };
            auto* const runtime = load_array_pointer(runtimes);
            auto* const frame = load_array_pointer(frames);
            auto* const result = load_array_pointer(results);

            if (manages_process_state) {
                auto* const queued = load_array_pointer(queued_states);
                auto* const waiting = load_array_pointer(waiting_states);
                auto* const process_status
                    = load_array_pointer(process_statuses);
                builder.CreateStore(llvm::ConstantInt::get(i8, 0U), queued);
                builder.CreateStore(llvm::ConstantInt::get(i8, 0U), waiting);
                builder.CreateStore(
                    llvm::ConstantInt::get(i8, 1U), process_status);
            }

            const std::array<const JitProcessCohortLogic4BitAndRegisterSlot*, 5>
                register_slots {
                    &member.read_lhs,
                    &member.read_rhs,
                    &member.extract_lhs,
                    &member.extract_rhs,
                    &member.result,
                };
            const bool has_frame_resident_register
                = std::ranges::any_of(
                    register_slots,
                    [](const auto* slot) { return slot->frame_resident; });
            llvm::Value* register_aval { };
            llvm::Value* register_bval { };
            if (has_frame_resident_register) {
                register_aval = load_abi_field(
                    frame, offsetof(fsim_jit_frame_v1, register_aval), pointer,
                    "register.aval.base");
                register_bval = load_abi_field(
                    frame, offsetof(fsim_jit_frame_v1, register_bval), pointer,
                    "register.bval.base");
            }
            llvm::Value* register_initialized { };
            if (member.tracks_register_initialization) {
                register_initialized = load_abi_field(
                    frame,
                    offsetof(fsim_jit_frame_v1, register_initialized),
                    pointer, "register.initialized.base");
            }
            auto& member_local_register_aval = local_register_aval[index];
            auto& member_local_register_bval = local_register_bval[index];
            const auto register_slot_index = [&](
                const JitProcessCohortLogic4BitAndRegisterSlot& slot) {
                for (std::size_t slot_index = 0U;
                    slot_index < register_slots.size(); ++slot_index) {
                    if (register_slots[slot_index]->register_id
                        == slot.register_id) {
                        return slot_index;
                    }
                }
                throw LlvmJitError(
                    "Logic4 bit-and cohort references an unknown register");
            };

            const auto store_register = [&](
                const JitProcessCohortLogic4BitAndRegisterSlot& slot,
                llvm::Value* aval,
                llvm::Value* bval) {
                auto* const word = llvm::ConstantInt::get(i32, slot.word_offset);
                const auto mask_value = slot.width == 64U
                    ? std::numeric_limits<std::uint64_t>::max()
                    : (std::uint64_t { 1 } << slot.width) - 1U;
                auto* const mask
                    = llvm::ConstantInt::get(i64, mask_value);
                const auto slot_index = register_slot_index(slot);
                auto* const stored_aval = builder.CreateAnd(aval, mask);
                auto* const stored_bval = builder.CreateAnd(bval, mask);
                if (slot.frame_resident) {
                    auto* const aval_address
                        = builder.CreateGEP(i64, register_aval, word);
                    auto* const bval_address
                        = builder.CreateGEP(i64, register_bval, word);
                    builder.CreateStore(stored_aval, aval_address);
                    builder.CreateStore(stored_bval, bval_address);
                } else {
                    builder.CreateStore(
                        stored_aval,
                        member_local_register_aval[slot_index]);
                    builder.CreateStore(
                        stored_bval,
                        member_local_register_bval[slot_index]);
                }
                if (slot.frame_resident
                    && register_initialized != nullptr) {
                    auto* const initialized_address = builder.CreateGEP(
                        i8, register_initialized,
                        llvm::ConstantInt::get(i32, slot.register_id));
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 1U),
                        initialized_address);
                }
            };
            const auto load_register = [&] (
                const JitProcessCohortLogic4BitAndRegisterSlot& slot) {
                if (slot.frame_resident) {
                    auto* const word
                        = llvm::ConstantInt::get(i32, slot.word_offset);
                    return std::pair {
                        static_cast<llvm::Value*>(builder.CreateLoad(
                            i64,
                            builder.CreateGEP(i64, register_aval, word))),
                        static_cast<llvm::Value*>(builder.CreateLoad(
                            i64,
                            builder.CreateGEP(i64, register_bval, word))) };
                }
                const auto slot_index = register_slot_index(slot);
                return std::pair {
                    static_cast<llvm::Value*>(builder.CreateLoad(
                        i64, member_local_register_aval[slot_index])),
                    static_cast<llvm::Value*>(builder.CreateLoad(
                        i64, member_local_register_bval[slot_index])) };
            };

            auto* const direct_signal_aval = load_abi_field(
                runtime, offsetof(fsim_jit_runtime_v1, direct_signal_aval),
                pointer, "direct.signal.aval");
            auto* const direct_signal_bval = load_abi_field(
                runtime, offsetof(fsim_jit_runtime_v1, direct_signal_bval),
                pointer, "direct.signal.bval");
            auto* const direct_read_map = load_abi_field(
                runtime, offsetof(fsim_jit_runtime_v1, direct_read_signals),
                pointer, "direct.read.map");

            const auto load_read = [&](const std::uint32_t slot,
                                       const char* const name) {
                auto* const actual = builder.CreateLoad(
                    i32,
                    builder.CreateGEP(
                        i32, direct_read_map,
                        llvm::ConstantInt::get(i32, slot)),
                    std::string { name } + ".actual");
                auto* const aval = builder.CreateLoad(
                    i64,
                    builder.CreateGEP(i64, direct_signal_aval, actual),
                    std::string { name } + ".aval");
                auto* const bval = builder.CreateLoad(
                    i64,
                    builder.CreateGEP(i64, direct_signal_bval, actual),
                    std::string { name } + ".bval");
                return std::pair { aval, bval };
            };

            const auto [lhs_read_aval, lhs_read_bval]
                = load_read(member.direct_read_lhs_slot, "read.lhs");
            store_register(member.read_lhs, lhs_read_aval, lhs_read_bval);

            const auto extract_bit = [&](
                const JitProcessCohortLogic4BitAndRegisterSlot& source,
                const JitProcessCohortLogic4BitAndRegisterSlot& destination,
                const std::uint32_t bit_offset) {
                const auto [source_aval, source_bval]
                    = load_register(source);
                auto* const shift = llvm::ConstantInt::get(i64, bit_offset);
                auto* const bit_mask = llvm::ConstantInt::get(i64, 1U);
                auto* const bit_aval = builder.CreateAnd(
                    builder.CreateLShr(source_aval, shift), bit_mask);
                auto* const bit_bval = builder.CreateAnd(
                    builder.CreateLShr(source_bval, shift), bit_mask);
                store_register(destination, bit_aval, bit_bval);
            };
            extract_bit(
                member.read_lhs, member.extract_lhs,
                member.extract_lhs_offset);
            const auto [rhs_read_aval, rhs_read_bval]
                = load_read(member.direct_read_rhs_slot, "read.rhs");
            store_register(member.read_rhs, rhs_read_aval, rhs_read_bval);
            extract_bit(
                member.read_rhs, member.extract_rhs,
                member.extract_rhs_offset);

            const auto [lhs_aval, lhs_bval]
                = load_register(member.extract_lhs);
            const auto [rhs_aval, rhs_bval]
                = load_register(member.extract_rhs);
            auto* const zero = llvm::ConstantInt::get(i64, 0U);
            auto* const lhs_zero = builder.CreateAnd(
                builder.CreateICmpEQ(lhs_aval, zero),
                builder.CreateICmpEQ(lhs_bval, zero));
            auto* const rhs_zero = builder.CreateAnd(
                builder.CreateICmpEQ(rhs_aval, zero),
                builder.CreateICmpEQ(rhs_bval, zero));
            auto* const known_zero = builder.CreateOr(lhs_zero, rhs_zero);
            auto* const lhs_one = builder.CreateAnd(
                builder.CreateICmpNE(lhs_aval, zero),
                builder.CreateICmpEQ(lhs_bval, zero));
            auto* const rhs_one = builder.CreateAnd(
                builder.CreateICmpNE(rhs_aval, zero),
                builder.CreateICmpEQ(rhs_bval, zero));
            auto* const known_one = builder.CreateAnd(lhs_one, rhs_one);
            auto* const unknown = builder.CreateNot(
                builder.CreateOr(known_zero, known_one));
            auto* const result_aval = builder.CreateZExt(
                builder.CreateNot(known_zero), i64);
            auto* const result_bval = builder.CreateZExt(unknown, i64);
            store_register(member.result, result_aval, result_bval);

            const auto [update_source_aval, update_source_bval]
                = load_register(member.result);

            auto* const direct_update_slots = load_abi_field(
                runtime, offsetof(fsim_jit_runtime_v1, direct_update_slots),
                pointer, "direct.update.slots");
            auto* const update_slot = builder.CreateGEP(
                update_slot_type, direct_update_slots,
                llvm::ConstantInt::get(i32, member.direct_update_slot),
                "update.slot");
            const auto slot_field = [&](const unsigned field) {
                return builder.CreateStructGEP(
                    update_slot_type, update_slot, field);
            };
            auto* const previous_aval = builder.CreateLoad(
                i64, slot_field(0U));
            auto* const previous_bval = builder.CreateLoad(
                i64, slot_field(1U));
            auto* const slot_reserved = builder.CreateLoad(
                i32, slot_field(6U));
            const auto shifted_mask
                = std::uint64_t { 1 } << member.update_offset;
            auto* const update_mask
                = llvm::ConstantInt::get(i64, shifted_mask);
            auto* const shifted_aval = builder.CreateShl(
                update_source_aval,
                llvm::ConstantInt::get(i64, member.update_offset));
            auto* const shifted_bval = builder.CreateShl(
                update_source_bval,
                llvm::ConstantInt::get(i64, member.update_offset));
            auto* const changed = builder.CreateICmpNE(
                builder.CreateAnd(
                    builder.CreateOr(
                        builder.CreateXor(previous_aval, shifted_aval),
                        builder.CreateXor(previous_bval, shifted_bval)),
                    update_mask),
                llvm::ConstantInt::get(i64, 0U));
            auto* const shadow_valid = builder.CreateICmpNE(
                builder.CreateAnd(
                    slot_reserved, llvm::ConstantInt::get(i32, 1U)),
                llvm::ConstantInt::get(i32, 0U));
            auto* const enabled = builder.CreateOr(
                builder.CreateNot(shadow_valid), changed);
            auto* const effective_mask = builder.CreateSelect(
                enabled, update_mask, llvm::ConstantInt::get(i64, 0U));
            const auto merge_update_plane = [&](const unsigned field,
                                                llvm::Value* shifted_value) {
                auto* const address = slot_field(field);
                auto* const previous = builder.CreateLoad(i64, address);
                auto* const merged = builder.CreateOr(
                    builder.CreateAnd(
                        previous, builder.CreateNot(effective_mask)),
                    builder.CreateAnd(shifted_value, effective_mask));
                builder.CreateStore(merged, address);
            };
            merge_update_plane(0U, shifted_aval);
            merge_update_plane(1U, shifted_bval);
            auto* const slot_mask_address = slot_field(4U);
            builder.CreateStore(
                builder.CreateOr(
                    builder.CreateLoad(i64, slot_mask_address),
                    effective_mask),
                slot_mask_address);
            auto* const slot_active_address = slot_field(5U);
            builder.CreateStore(
                builder.CreateOr(
                    builder.CreateLoad(i32, slot_active_address),
                    builder.CreateZExt(enabled, i32)),
                slot_active_address);

            auto* const active_words = load_abi_field(
                runtime,
                offsetof(fsim_jit_runtime_v1, direct_update_active_words),
                pointer, "direct.update.active.words");
            const auto word_index = member.direct_update_slot / 64U;
            const auto bit_index = member.direct_update_slot % 64U;
            const auto mark_bitmap = [&] {
                auto* const bitmap_word = builder.CreateGEP(
                    i64, active_words,
                    llvm::ConstantInt::get(i32, word_index));
                builder.CreateStore(
                    builder.CreateOr(
                        builder.CreateLoad(i64, bitmap_word),
                        builder.CreateMul(
                            builder.CreateZExt(enabled, i64),
                            llvm::ConstantInt::get(
                                i64, std::uint64_t { 1 } << bit_index))),
                    bitmap_word);
            };
            if (impl_->options.require_direct_update_slots) {
                mark_bitmap();
            } else {
                auto* const mark_block = llvm::BasicBlock::Create(
                    *context, "update.bitmap", function);
                auto* const update_done = llvm::BasicBlock::Create(
                    *context, "update.done", function);
                builder.CreateCondBr(
                    builder.CreateICmpNE(
                        active_words,
                        llvm::ConstantPointerNull::get(pointer)),
                    mark_block, update_done);
                builder.SetInsertPoint(mark_block);
                mark_bitmap();
                builder.CreateBr(update_done);
                builder.SetInsertPoint(update_done);
            }

            builder.CreateStore(
                llvm::ConstantInt::get(i32, 9U),
                abi_field_address(
                    frame, offsetof(fsim_jit_frame_v1, program_counter), i32));
            builder.CreateStore(
                llvm::ConstantInt::get(
                    i32, FSIM_JIT_FRAME_STATE_READY),
                abi_field_address(frame, offsetof(fsim_jit_frame_v1, state), i32));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, 8U),
                abi_field_address(
                    frame, offsetof(fsim_jit_frame_v1, last_instruction), i32));
            builder.CreateStore(
                llvm::ConstantInt::get(
                    i32, FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY),
                abi_field_address(result, offsetof(fsim_jit_resume_result_v1, status), i32));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, 8U),
                abi_field_address(
                    result, offsetof(fsim_jit_resume_result_v1, instruction), i32));
            builder.CreateStore(
                llvm::ConstantInt::get(i64, 0U),
                abi_field_address(result, offsetof(fsim_jit_resume_result_v1, delay), i64));
            builder.CreateStore(
                llvm::ConstantInt::get(
                    i32, FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY),
                builder.CreateGEP(i32, statuses, member_offset));
            if (manages_process_state) {
                auto* const waiting = load_array_pointer(waiting_states);
                auto* const process_status
                    = load_array_pointer(process_statuses);
                builder.CreateStore(llvm::ConstantInt::get(i8, 1U), waiting);
                builder.CreateStore(
                    llvm::ConstantInt::get(i8, 2U), process_status);
            }
        }

        builder.CreateRet(
            llvm::ConstantInt::get(i32, members.size()));
        if (auto message = verify_error(*module); !message.empty()) {
            throw LlvmJitError(
                "generated invalid LLVM Logic4 bit-and cohort: " + message);
        }
        optimize_module(*module, impl_->options.optimization);
        if (auto error = impl_->jit->addIRModule(
                llvm::orc::ThreadSafeModule(
                    std::move(module), std::move(context)))) {
            throw LlvmJitError(
                "cannot add LLVM Logic4 bit-and cohort: "
                + llvm_error(std::move(error)));
        }
        auto address = unwrap(
            impl_->jit->lookup(symbol),
            "cannot materialize LLVM Logic4 bit-and cohort");
        fused_cohort = address.template toPtr<NativeCohort>();
        if (fused_cohort == nullptr) {
            throw LlvmJitError(
                "LLVM returned a null Logic4 bit-and cohort address");
        }
        impl_->logic4_bit_and_cohort_functions[key].push_back(
            std::make_unique<Impl::NativeLogic4BitAndCohortEntry>(
                Impl::NativeLogic4BitAndCohortEntry {
                    fused_cohort,
                    std::vector<JitProcessCohortLogic4BitAndMember> {
                        members.begin(), members.end() },
                    manages_process_state }));
    }

    if (impl_->next_bound_cohort_generation == 0U) {
        throw LlvmJitError("LLVM process cohort binding generation exhausted");
    }
    auto bound
        = std::make_shared<Impl::NativeBoundLogic4BitAndCohortEntry>();
    bound->function = fused_cohort;
    bound->generation = impl_->next_bound_cohort_generation++;
    bound->native_members = std::move(native_members);
    bound->members.assign(members.begin(), members.end());
    bound->manages_process_state = manages_process_state;
    bound->runtimes.reserve(entries.size());
    bound->frames.reserve(entries.size());
    bound->results.reserve(entries.size());
    bound->statuses.resize(entries.size());
    bound->queued.reserve(entries.size());
    bound->waiting.reserve(entries.size());
    bound->process_statuses.reserve(entries.size());
    for (const auto& entry : entries) {
        bound->runtimes.push_back(entry.runtime);
        bound->frames.push_back(entry.frame);
        bound->results.push_back(entry.result);
        bound->queued.push_back(entry.queued);
        bound->waiting.push_back(entry.waiting_on_static);
        bound->process_statuses.push_back(entry.process_status);
    }
    auto* const result = bound.get();
    impl_->bound_logic4_bit_and_cohorts.push_back(std::move(bound));
    return JitProcessCohortBinding {
        impl_.get(), result, result->generation };
}

std::optional<JitProcessCohortBinding>
LlvmJit::bind_compact_logic4_bit_and_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries,
    const std::span<const JitProcessCohortLogic4BitAndMember> shapes) const
{
    if (!impl_ || impl_->options.debug_instrumentation
        || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()
        || shapes.size() != entries.size()) {
        return std::nullopt;
    }

    std::vector<fsim_jit_update_slot_v1*> output_slots;
    std::vector<std::uint64_t*> active_words;
    std::vector<std::uint64_t> active_masks;
    std::vector<std::uint8_t*> queued;
    std::vector<std::uint8_t*> waiting;
    std::vector<std::uint8_t*> process_statuses;
    std::vector<Impl::NativeCompactLogic4BitAndMemberShape> code_shapes;
    output_slots.reserve(entries.size());
    active_words.reserve(entries.size());
    active_masks.reserve(entries.size());
    queued.reserve(entries.size());
    waiting.reserve(entries.size());
    process_statuses.reserve(entries.size());
    code_shapes.reserve(entries.size());

    const std::uint64_t* input_aval { };
    const std::uint64_t* input_bval { };
    std::uint32_t input_lhs_signal { };
    std::uint32_t input_rhs_signal { };
    bool have_shared_inputs { };
    std::vector<std::uint8_t*> state_addresses;
    state_addresses.reserve(entries.size() * 3U);
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        const auto& member = shapes[index];
        if (entry.process.owner_ != impl_.get()
            || entry.process.entry_ == nullptr || entry.runtime == nullptr
            || entry.active != nullptr || entry.queued == nullptr
            || entry.waiting_on_static == nullptr
            || entry.process_status == nullptr) {
            return std::nullopt;
        }

        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        const auto& info = native->info;
        const auto& layout = info.frame_layout;
        if (info.operation_count != 10U || !info.requires_resume
            || !info.uses_write_update_slice || layout.uses_logic9
            || layout.string_register_count != 0U
            || layout.tracks_register_initialization
                != member.tracks_register_initialization
            || layout.register_widths.size() != layout.register_count
            || layout.register_word_offsets.size() != layout.register_count) {
            return std::nullopt;
        }
        for (const auto flag : Impl::ProcessInfo::flags) {
            const bool supported_flag
                = flag == &Impl::ProcessInfo::requires_resume
                || flag == &Impl::ProcessInfo::uses_debug_points
                || flag == &Impl::ProcessInfo::uses_write_update_slice;
            if (info.*flag && !supported_flag) {
                return std::nullopt;
            }
        }

        const std::array<const JitProcessCohortLogic4BitAndRegisterSlot*, 5>
            register_slots {
                &member.read_lhs,
                &member.read_rhs,
                &member.extract_lhs,
                &member.extract_rhs,
                &member.result,
            };
        std::array<std::uint64_t, 5> slot_begin { };
        std::array<std::uint64_t, 5> slot_end { };
        for (std::size_t slot_index = 0U;
            slot_index < register_slots.size(); ++slot_index) {
            const auto& slot = *register_slots[slot_index];
            if (slot.frame_resident || slot.register_id >= layout.register_count
                || slot.width == 0U || slot.width > 64U
                || layout.register_widths[slot.register_id] != slot.width
                || layout.register_word_offsets[slot.register_id]
                    != slot.word_offset) {
                return std::nullopt;
            }
            const auto word_count
                = (static_cast<std::uint64_t>(slot.width) + 63U) / 64U;
            slot_begin[slot_index] = slot.word_offset;
            slot_end[slot_index]
                = static_cast<std::uint64_t>(slot.word_offset) + word_count;
            if (slot_end[slot_index] > layout.register_word_count) {
                return std::nullopt;
            }
            for (std::size_t prior = 0U; prior < slot_index; ++prior) {
                if (slot.register_id == register_slots[prior]->register_id
                    || (slot_begin[slot_index] < slot_end[prior]
                        && slot_begin[prior] < slot_end[slot_index])) {
                    return std::nullopt;
                }
            }
        }
        if (member.extract_lhs.width != 1U
            || member.extract_rhs.width != 1U || member.result.width != 1U
            || member.extract_lhs_offset >= member.read_lhs.width
            || member.extract_rhs_offset >= member.read_rhs.width
            || member.direct_read_lhs_slot >= layout.direct_read_signals.size()
            || member.direct_read_rhs_slot >= layout.direct_read_signals.size()
            || member.direct_update_slot
                >= layout.direct_update_signals.size()) {
            return std::nullopt;
        }

        const auto& runtime = *entry.runtime;
        if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1
            || runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.direct_signal_aval == nullptr
            || runtime.direct_signal_bval == nullptr
            || runtime.direct_read_signals == nullptr
            || runtime.direct_read_signal_count
                <= std::max(
                    member.direct_read_lhs_slot,
                    member.direct_read_rhs_slot)
            || runtime.direct_signal_count == 0U
            || runtime.direct_update_slots == nullptr
            || runtime.direct_update_slot_count <= member.direct_update_slot
            || runtime.direct_update_active_words == nullptr
            || runtime.direct_update_active_word_count
                <= member.direct_update_slot / 64U) {
            return std::nullopt;
        }

        const auto lhs_signal = runtime.direct_read_signals[
            member.direct_read_lhs_slot];
        const auto rhs_signal = runtime.direct_read_signals[
            member.direct_read_rhs_slot];
        if (lhs_signal >= runtime.direct_signal_count
            || rhs_signal >= runtime.direct_signal_count) {
            return std::nullopt;
        }
        if (!have_shared_inputs) {
            input_aval = runtime.direct_signal_aval;
            input_bval = runtime.direct_signal_bval;
            input_lhs_signal = lhs_signal;
            input_rhs_signal = rhs_signal;
            have_shared_inputs = true;
        } else if (runtime.direct_signal_aval != input_aval
            || runtime.direct_signal_bval != input_bval
            || lhs_signal != input_lhs_signal
            || rhs_signal != input_rhs_signal) {
            return std::nullopt;
        }

        auto* const output_slot
            = &runtime.direct_update_slots[member.direct_update_slot];
        if (output_slot->width == 0U || output_slot->width > 64U
            || member.update_offset >= output_slot->width) {
            return std::nullopt;
        }
        auto* const active_word = &runtime.direct_update_active_words[
            member.direct_update_slot / 64U];
        if (std::ranges::find(output_slots, output_slot) != output_slots.end()
            || std::ranges::find(active_words, active_word)
                != active_words.end()) {
            return std::nullopt;
        }
        const std::array<std::uint8_t*, 3> member_state_addresses {
            entry.queued,
            entry.waiting_on_static,
            entry.process_status,
        };
        for (std::size_t address_index = 0U;
            address_index < member_state_addresses.size(); ++address_index) {
            if (std::ranges::find(
                    state_addresses, member_state_addresses[address_index])
                != state_addresses.end()) {
                return std::nullopt;
            }
            for (std::size_t prior = 0U; prior < address_index; ++prior) {
                if (member_state_addresses[address_index]
                    == member_state_addresses[prior]) {
                    return std::nullopt;
                }
            }
        }

        output_slots.push_back(output_slot);
        active_words.push_back(active_word);
        active_masks.push_back(
            std::uint64_t { 1 } << (member.direct_update_slot % 64U));
        queued.push_back(entry.queued);
        waiting.push_back(entry.waiting_on_static);
        process_statuses.push_back(entry.process_status);
        state_addresses.insert(
            state_addresses.end(), member_state_addresses.begin(),
            member_state_addresses.end());
        code_shapes.push_back(
            Impl::NativeCompactLogic4BitAndMemberShape {
                member.extract_lhs_offset,
                member.extract_rhs_offset,
                member.update_offset });
    }

    std::size_t key = code_shapes.size();
    const auto combine = [&](const std::size_t value) {
        key ^= value + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    };
    for (const auto& shape : code_shapes) {
        combine(shape.extract_lhs_offset);
        combine(shape.extract_rhs_offset);
        combine(shape.update_offset);
    }

    NativeCompactCohort* compact_cohort { };
    std::scoped_lock lock { impl_->cohort_mutex };
    const auto found
        = impl_->compact_logic4_bit_and_cohort_functions.find(key);
    if (found != impl_->compact_logic4_bit_and_cohort_functions.end()) {
        const auto match = std::ranges::find_if(
            found->second,
            [&](const auto& candidate) {
                return std::ranges::equal(candidate->members, code_shapes);
            });
        if (match != found->second.end()) {
            compact_cohort = (*match)->function;
        }
    }

    if (compact_cohort == nullptr) {
        const auto cohort_number = impl_->next_cohort++;
        const auto symbol = "fsim_compact_logic4_bit_and_cohort_"
            + std::to_string(cohort_number);
        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = std::make_unique<llvm::Module>(
            symbol + ".module", *context);
        module->setDataLayout(impl_->jit->getDataLayout());
        module->setTargetTriple(impl_->jit->getTargetTriple());

        auto* const i8 = llvm::Type::getInt8Ty(*context);
        auto* const i32 = llvm::Type::getInt32Ty(*context);
        auto* const i64 = llvm::Type::getInt64Ty(*context);
        auto* const pointer = llvm::PointerType::getUnqual(*context);
        auto* const function_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            { pointer, pointer, i32, i32, pointer, pointer, pointer, pointer,
                pointer, pointer },
            false);
        auto* const function = llvm::Function::Create(
            function_type, llvm::Function::ExternalLinkage, symbol, *module);
        function->setCallingConv(llvm::CallingConv::C);
        auto arguments = function->arg_begin();
        auto* const signal_aval = &*arguments++;
        auto* const signal_bval = &*arguments++;
        auto* const lhs_signal = &*arguments++;
        auto* const rhs_signal = &*arguments++;
        auto* const update_slots = &*arguments++;
        auto* const active_words_argument = &*arguments++;
        auto* const active_masks_argument = &*arguments++;
        auto* const queued_states = &*arguments++;
        auto* const waiting_states = &*arguments++;
        auto* const process_statuses_argument = &*arguments;

        auto* const block = llvm::BasicBlock::Create(
            *context, "entry", function);
        llvm::IRBuilder<> builder(block);
        auto* const update_slot_type = llvm::StructType::create(
            *context,
            { i64, i64, i64, i64, i64, i32, i32,
                pointer, pointer, pointer, i32, i32 },
            "fsim_jit_update_slot_v1");
        auto* const zero_i64 = llvm::ConstantInt::get(i64, 0U);
        auto* const one_i64 = llvm::ConstantInt::get(i64, 1U);

        auto* const lhs_signal_aval = builder.CreateLoad(
            i64, builder.CreateGEP(i64, signal_aval, lhs_signal));
        auto* const lhs_signal_bval = builder.CreateLoad(
            i64, builder.CreateGEP(i64, signal_bval, lhs_signal));
        auto* const rhs_signal_aval = builder.CreateLoad(
            i64, builder.CreateGEP(i64, signal_aval, rhs_signal));
        auto* const rhs_signal_bval = builder.CreateLoad(
            i64, builder.CreateGEP(i64, signal_bval, rhs_signal));

        for (std::size_t index = 0U; index < code_shapes.size(); ++index) {
            const auto& shape = code_shapes[index];
            auto* const member_index = llvm::ConstantInt::get(i32, index);
            const auto load_pointer = [&](llvm::Value* array) {
                return builder.CreateLoad(
                    pointer, builder.CreateGEP(pointer, array, member_index));
            };
            const auto bit_from_signal = [&](llvm::Value* input,
                                             const std::uint32_t offset) {
                auto* const shift = llvm::ConstantInt::get(i64, offset);
                return builder.CreateAnd(
                    builder.CreateLShr(input, shift), one_i64);
            };
            auto* const lhs_aval = bit_from_signal(
                lhs_signal_aval, shape.extract_lhs_offset);
            auto* const lhs_bval = bit_from_signal(
                lhs_signal_bval, shape.extract_lhs_offset);
            auto* const rhs_aval = bit_from_signal(
                rhs_signal_aval, shape.extract_rhs_offset);
            auto* const rhs_bval = bit_from_signal(
                rhs_signal_bval, shape.extract_rhs_offset);
            auto* const lhs_zero = builder.CreateAnd(
                builder.CreateICmpEQ(lhs_aval, zero_i64),
                builder.CreateICmpEQ(lhs_bval, zero_i64));
            auto* const rhs_zero = builder.CreateAnd(
                builder.CreateICmpEQ(rhs_aval, zero_i64),
                builder.CreateICmpEQ(rhs_bval, zero_i64));
            auto* const known_zero = builder.CreateOr(lhs_zero, rhs_zero);
            auto* const lhs_one = builder.CreateAnd(
                builder.CreateICmpNE(lhs_aval, zero_i64),
                builder.CreateICmpEQ(lhs_bval, zero_i64));
            auto* const rhs_one = builder.CreateAnd(
                builder.CreateICmpNE(rhs_aval, zero_i64),
                builder.CreateICmpEQ(rhs_bval, zero_i64));
            auto* const known_one = builder.CreateAnd(lhs_one, rhs_one);
            auto* const unknown = builder.CreateNot(
                builder.CreateOr(known_zero, known_one));
            auto* const result_aval = builder.CreateZExt(
                builder.CreateNot(known_zero), i64);
            auto* const result_bval = builder.CreateZExt(unknown, i64);

            const auto update_mask_value
                = std::uint64_t { 1 } << shape.update_offset;
            auto* const update_mask
                = llvm::ConstantInt::get(i64, update_mask_value);
            auto* const shifted_aval = builder.CreateSelect(
                builder.CreateICmpNE(result_aval, zero_i64),
                update_mask, zero_i64);
            auto* const shifted_bval = builder.CreateSelect(
                builder.CreateICmpNE(result_bval, zero_i64),
                update_mask, zero_i64);

            auto* const update_slot = load_pointer(update_slots);
            const auto slot_field = [&](const unsigned field) {
                return builder.CreateStructGEP(
                    update_slot_type, update_slot, field);
            };
            auto* const previous_aval = builder.CreateLoad(
                i64, slot_field(0U));
            auto* const previous_bval = builder.CreateLoad(
                i64, slot_field(1U));
            auto* const slot_reserved = builder.CreateLoad(
                i32, slot_field(6U));
            auto* const changed = builder.CreateICmpNE(
                builder.CreateAnd(
                    builder.CreateOr(
                        builder.CreateXor(previous_aval, shifted_aval),
                        builder.CreateXor(previous_bval, shifted_bval)),
                    update_mask),
                zero_i64);
            auto* const shadow_valid = builder.CreateICmpNE(
                builder.CreateAnd(
                    slot_reserved, llvm::ConstantInt::get(i32, 1U)),
                llvm::ConstantInt::get(i32, 0U));
            auto* const enabled = builder.CreateOr(
                builder.CreateNot(shadow_valid), changed);
            auto* const update_block = llvm::BasicBlock::Create(
                *context, "update", function);
            auto* const terminal_block = llvm::BasicBlock::Create(
                *context, "terminal", function);
            builder.CreateCondBr(enabled, update_block, terminal_block);
            builder.SetInsertPoint(update_block);
            const auto merge_update_plane = [&](
                const unsigned field, llvm::Value* shifted_value) {
                auto* const address = slot_field(field);
                auto* const previous = builder.CreateLoad(i64, address);
                auto* const merged = builder.CreateOr(
                    builder.CreateAnd(
                        previous, builder.CreateNot(update_mask)),
                    builder.CreateAnd(shifted_value, update_mask));
                builder.CreateStore(merged, address);
            };
            merge_update_plane(0U, shifted_aval);
            merge_update_plane(1U, shifted_bval);
            auto* const mask_address = slot_field(4U);
            builder.CreateStore(
                builder.CreateOr(
                    builder.CreateLoad(i64, mask_address), update_mask),
                mask_address);
            auto* const active_address = slot_field(5U);
            builder.CreateStore(
                builder.CreateOr(
                    builder.CreateLoad(i32, active_address),
                    llvm::ConstantInt::get(i32, 1U)),
                active_address);

            auto* const active_word = load_pointer(active_words_argument);
            auto* const bitmap_mask = builder.CreateLoad(
                i64, builder.CreateGEP(
                    i64, active_masks_argument, member_index));
            builder.CreateStore(
                builder.CreateOr(
                    builder.CreateLoad(i64, active_word), bitmap_mask),
                active_word);
            builder.CreateBr(terminal_block);
            builder.SetInsertPoint(terminal_block);
            builder.CreateStore(
                llvm::ConstantInt::get(i8, 0U), load_pointer(queued_states));
            builder.CreateStore(
                llvm::ConstantInt::get(i8, 1U), load_pointer(waiting_states));
            builder.CreateStore(
                llvm::ConstantInt::get(i8, 2U),
                load_pointer(process_statuses_argument));
        }
        builder.CreateRetVoid();
        if (auto message = verify_error(*module); !message.empty()) {
            throw LlvmJitError(
                "generated invalid LLVM compact Logic4 bit-and cohort: "
                + message);
        }
        optimize_module(*module, impl_->options.optimization);
        if (auto error = impl_->jit->addIRModule(
                llvm::orc::ThreadSafeModule(
                    std::move(module), std::move(context)))) {
            throw LlvmJitError(
                "cannot add LLVM compact Logic4 bit-and cohort: "
                + llvm_error(std::move(error)));
        }
        auto address = unwrap(
            impl_->jit->lookup(symbol),
            "cannot materialize LLVM compact Logic4 bit-and cohort");
        compact_cohort = address.template toPtr<NativeCompactCohort>();
        if (compact_cohort == nullptr) {
            throw LlvmJitError(
                "LLVM returned a null compact Logic4 bit-and cohort address");
        }
        impl_->compact_logic4_bit_and_cohort_functions[key].push_back(
            std::make_unique<Impl::NativeCompactLogic4BitAndCohortEntry>(
                Impl::NativeCompactLogic4BitAndCohortEntry {
                    compact_cohort, std::move(code_shapes) }));
    }

    if (impl_->next_bound_cohort_generation == 0U) {
        throw LlvmJitError("LLVM process cohort binding generation exhausted");
    }
    auto bound
        = std::make_shared<Impl::NativeBoundCompactLogic4BitAndCohortEntry>();
    bound->function = compact_cohort;
    bound->generation = impl_->next_bound_cohort_generation++;
    bound->input_aval = input_aval;
    bound->input_bval = input_bval;
    bound->input_lhs_signal = input_lhs_signal;
    bound->input_rhs_signal = input_rhs_signal;
    bound->output_slots = std::move(output_slots);
    bound->active_words = std::move(active_words);
    bound->active_masks = std::move(active_masks);
    bound->queued = std::move(queued);
    bound->waiting = std::move(waiting);
    bound->process_statuses = std::move(process_statuses);
    auto* const result = bound.get();
    impl_->bound_compact_logic4_bit_and_cohorts.push_back(
        std::move(bound));
    return JitProcessCohortBinding {
        impl_.get(), result, result->generation };
}

bool LlvmJit::release_cohort_binding(
    const JitProcessCohortBinding cohort) const
{
    if (!impl_ || cohort.owner_ != impl_.get()) {
        throw LlvmJitError("invalid prevalidated LLVM cohort binding");
    }
    const std::scoped_lock lock { impl_->cohort_mutex };
    const auto found = std::ranges::find_if(
        impl_->bound_cohorts, [&](const auto& candidate) {
            return candidate.get() == cohort.entry_
                && candidate->generation == cohort.generation_;
        });
    if (found != impl_->bound_cohorts.end()) {
        impl_->bound_cohorts.erase(found);
        return true;
    }
    const auto fused_found = std::ranges::find_if(
        impl_->bound_logic4_bit_and_cohorts,
        [&](const auto& candidate) {
            return candidate.get() == cohort.entry_
                && candidate->generation == cohort.generation_;
        });
    if (fused_found == impl_->bound_logic4_bit_and_cohorts.end()) {
        const auto compact_found = std::ranges::find_if(
            impl_->bound_compact_logic4_bit_and_cohorts,
            [&](const auto& candidate) {
                return candidate.get() == cohort.entry_
                    && candidate->generation == cohort.generation_;
            });
        if (compact_found
            == impl_->bound_compact_logic4_bit_and_cohorts.end()) {
            const auto* const pure_wave
                = static_cast<const Impl::NativeBoundPureWaveEntry*>(
                    cohort.entry_);
            const auto wave_found
                = impl_->bound_pure_waves.find(pure_wave);
            if (wave_found == impl_->bound_pure_waves.end()
                || wave_found->second->generation != cohort.generation_) {
                return false;
            }
            impl_->bound_pure_waves.erase(wave_found);
            return true;
        }
        impl_->bound_compact_logic4_bit_and_cohorts.erase(compact_found);
        return true;
    }
    impl_->bound_logic4_bit_and_cohorts.erase(fused_found);
    return true;
}

std::size_t LlvmJit::active_cohort_binding_count() const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::scoped_lock lock { impl_->cohort_mutex };
    return impl_->bound_cohorts.size()
        + impl_->bound_logic4_bit_and_cohorts.size()
        + impl_->bound_compact_logic4_bit_and_cohorts.size()
        + impl_->bound_pure_waves.size();
}

std::size_t LlvmJit::resume_cohort_prevalidated(
    const JitProcessCohortBinding cohort,
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || cohort.owner_ != impl_.get() || cohort.entry_ == nullptr) {
        throw LlvmJitError("invalid prevalidated LLVM cohort binding");
    }
    std::shared_ptr<Impl::NativeBoundCohortEntry> bound;
    {
        const std::scoped_lock lock { impl_->cohort_mutex };
        const auto found = std::ranges::find_if(
            impl_->bound_cohorts, [&](const auto& candidate) {
                return candidate.get() == cohort.entry_
                    && candidate->generation == cohort.generation_;
            });
        if (found == impl_->bound_cohorts.end()) {
            throw LlvmJitError("stale prevalidated LLVM cohort binding");
        }
        bound = *found;
    }
    if (entries.size() != bound->members.size()) {
        throw LlvmJitError("prevalidated LLVM cohort binding size changed");
    }

    for (std::size_t index = 0; index < bound->members.size(); ++index) {
        if (bound->runtimes[index] == nullptr) {
            throw LlvmJitError(
                "prevalidated LLVM cohort has a null runtime descriptor");
        }
        validate_native_service_callbacks(
            bound->members[index]->info, *bound->runtimes[index]);
    }

    const auto executed = bound->function(
        bound->runtimes.data(), bound->frames.data(), bound->results.data(),
        bound->statuses.data(), bound->queued.data(), bound->waiting.data(),
        bound->process_statuses.data(), bound->active.data(),
        static_cast<std::uint32_t>(bound->members.size()));
    if (executed == 0U || executed > entries.size()) {
        throw LlvmJitError(
            "bound LLVM process cohort returned an invalid count");
    }
    for (std::size_t index = 0; index < executed; ++index) {
        entries[index].failure = { };
        entries[index].status = bound->statuses[index];
        if (bound->statuses[index]
            == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        bound->results[index]->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        bound->results[index]->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error "
                    "reason");
            } catch (...) {
                entries[index].failure = std::current_exception();
            }
        }
    }
    return executed;
}

std::optional<std::size_t>
LlvmJit::try_resume_logic4_bit_and_cohort_prevalidated(
    const JitProcessCohortBinding cohort,
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    return try_resume_logic4_bit_and_cohort_prevalidated_impl(
        cohort, entries, true);
}

std::optional<std::size_t>
LlvmJit::try_resume_logic4_bit_and_cohort_trusted_prevalidated(
    const JitProcessCohortBinding cohort,
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    return try_resume_logic4_bit_and_cohort_prevalidated_impl(
        cohort, entries, false);
}

std::optional<std::size_t>
LlvmJit::try_resume_logic4_bit_and_cohort_prevalidated_impl(
    const JitProcessCohortBinding cohort,
    const std::span<JitProcessCohortResumeEntry> entries,
    const bool validate_immutable_state) const
{
    if (!impl_ || cohort.owner_ != impl_.get() || cohort.entry_ == nullptr) {
        throw LlvmJitError(
            "invalid prevalidated LLVM Logic4 bit-and cohort binding");
    }
    std::shared_ptr<Impl::NativeBoundLogic4BitAndCohortEntry> bound;
    {
        const std::scoped_lock lock { impl_->cohort_mutex };
        const auto found = std::ranges::find_if(
            impl_->bound_logic4_bit_and_cohorts,
            [&](const auto& candidate) {
                return candidate.get() == cohort.entry_
                    && candidate->generation == cohort.generation_;
            });
        if (found == impl_->bound_logic4_bit_and_cohorts.end()) {
            throw LlvmJitError(
                "stale prevalidated LLVM Logic4 bit-and cohort binding");
        }
        bound = *found;
    }
    const auto bound_member_count = bound->runtimes.size();
    if (entries.size() != bound_member_count) {
        throw LlvmJitError(
            "prevalidated LLVM Logic4 bit-and cohort binding size changed");
    }

    for (std::size_t index = 0U; index < bound_member_count; ++index) {
        const auto& runtime = *bound->runtimes[index];
        const auto& frame = *bound->frames[index];
        if (validate_immutable_state) {
            const auto& entry = entries[index];
            const auto* const native = bound->native_members[index];
            const auto& member = bound->members[index];
            if (native == nullptr
                || entry.process.owner_ != impl_.get()
                || entry.process.entry_ != native
                || entry.runtime != bound->runtimes[index]
                || entry.frame != bound->frames[index]
                || entry.result != bound->results[index]
                || entry.queued != bound->queued[index]
                || entry.waiting_on_static != bound->waiting[index]
                || entry.process_status != bound->process_statuses[index]
                || entry.active != nullptr) {
                return std::nullopt;
            }

            const auto& result = *bound->results[index];
            const auto& layout = native->info.frame_layout;
            const bool has_frame_resident_register
                = member.read_lhs.frame_resident
                || member.read_rhs.frame_resident
                || member.extract_lhs.frame_resident
                || member.extract_rhs.frame_resident
                || member.result.frame_resident;
            if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1
                || runtime.struct_size < sizeof(fsim_jit_runtime_v1)
                || frame.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V1
                || frame.struct_size < sizeof(fsim_jit_frame_v1)
                || frame.layout_id_low != layout.layout_id_low
                || frame.layout_id_high != layout.layout_id_high
                || frame.register_count != layout.register_count
                || (has_frame_resident_register
                    && (frame.register_aval == nullptr
                        || frame.register_bval == nullptr))
                || (member.tracks_register_initialization
                    && frame.register_initialized == nullptr)
                || result.abi_version
                    != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1
                || result.struct_size < sizeof(fsim_jit_resume_result_v1)
                || runtime.direct_signal_aval == nullptr
                || runtime.direct_signal_bval == nullptr
                || runtime.direct_read_signals == nullptr
                || runtime.direct_read_signal_count
                    <= std::max(
                        member.direct_read_lhs_slot,
                        member.direct_read_rhs_slot)
                || runtime.direct_signal_count == 0U
                || runtime.direct_update_slots == nullptr
                || runtime.direct_update_slot_count
                    <= member.direct_update_slot) {
                return std::nullopt;
            }

            const auto lhs_signal = runtime.direct_read_signals[
                member.direct_read_lhs_slot];
            const auto rhs_signal = runtime.direct_read_signals[
                member.direct_read_rhs_slot];
            if (lhs_signal >= runtime.direct_signal_count
                || rhs_signal >= runtime.direct_signal_count) {
                return std::nullopt;
            }
            const auto& update
                = runtime.direct_update_slots[member.direct_update_slot];
            if (update.width == 0U || update.width > 64U
                || member.update_offset >= update.width) {
                return std::nullopt;
            }
            if (impl_->options.require_direct_update_slots
                && (runtime.direct_update_active_words == nullptr
                    || runtime.direct_update_active_word_count
                        <= member.direct_update_slot / 64U)) {
                return std::nullopt;
            }
            if (runtime.direct_update_active_words != nullptr
                && runtime.direct_update_active_word_count
                    <= member.direct_update_slot / 64U) {
                return std::nullopt;
            }
        }

        if ((runtime.flags & FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS) != 0U
            || frame.program_counter != 9U
            || frame.state != FSIM_JIT_FRAME_STATE_READY
            || frame.last_instruction != 8U
            || frame.native_call_depth != 0U) {
            return std::nullopt;
        }
    }

    const auto executed = bound->function(
        bound->runtimes.data(), bound->frames.data(), bound->results.data(),
        bound->statuses.data(), bound->queued.data(), bound->waiting.data(),
        bound->process_statuses.data(), nullptr,
        static_cast<std::uint32_t>(bound_member_count));
    if (executed != entries.size()) {
        throw LlvmJitError(
            "Logic4 bit-and cohort returned an invalid execution count");
    }
    for (std::size_t index = 0U; index < executed; ++index) {
        entries[index].failure = { };
        entries[index].status = bound->statuses[index];
        if (bound->statuses[index]
            == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        bound->results[index]->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        bound->results[index]->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error "
                    "reason");
            } catch (...) {
                entries[index].failure = std::current_exception();
            }
        }
    }
    return executed;
}

bool LlvmJit::try_resume_compact_logic4_bit_and_cohort_prevalidated(
    const JitProcessCohortBinding cohort) const
{
    if (!impl_ || cohort.owner_ != impl_.get() || cohort.entry_ == nullptr) {
        throw LlvmJitError(
            "invalid prevalidated LLVM compact Logic4 bit-and binding");
    }
    std::shared_ptr<Impl::NativeBoundCompactLogic4BitAndCohortEntry> bound;
    {
        const std::scoped_lock lock { impl_->cohort_mutex };
        const auto found = std::ranges::find_if(
            impl_->bound_compact_logic4_bit_and_cohorts,
            [&](const auto& candidate) {
                return candidate.get() == cohort.entry_
                    && candidate->generation == cohort.generation_;
            });
        if (found == impl_->bound_compact_logic4_bit_and_cohorts.end()) {
            throw LlvmJitError(
                "stale prevalidated LLVM compact Logic4 bit-and binding");
        }
        bound = *found;
    }

    bound->function(
        bound->input_aval, bound->input_bval, bound->input_lhs_signal,
        bound->input_rhs_signal, bound->output_slots.data(),
        bound->active_words.data(), bound->active_masks.data(),
        bound->queued.data(), bound->waiting.data(),
        bound->process_statuses.data());
    return true;
}

bool LlvmJit::try_resume_pure_wave_prevalidated(
    const JitProcessCohortBinding wave) const
{
    if (!impl_ || wave.owner_ != impl_.get() || wave.entry_ == nullptr) {
        throw LlvmJitError("invalid prevalidated LLVM pure-wave binding");
    }
    std::shared_ptr<Impl::NativeBoundPureWaveEntry> bound;
    {
        const std::scoped_lock lock { impl_->cohort_mutex };
        const auto* const key
            = static_cast<const Impl::NativeBoundPureWaveEntry*>(
                wave.entry_);
        const auto found = impl_->bound_pure_waves.find(key);
        if (found == impl_->bound_pure_waves.end()
            || found->second->generation != wave.generation_) {
            return false;
        }
        bound = found->second;
    }

    // Check every member before the first slot or scheduler-state write.
    // The app's certificate covers scheduler addresses and runtime ownership;
    // release/rebind invalidates any plan whose immutable runtime backing moved.
    if (bound->members.empty()
        || bound->kernel_members.size() != bound->members.size()
        || bound->kernel_task_ends.size() != bound->task_ends.size()
        || bound->kernel_task_shapes.size() != bound->task_shapes.size()
        || bound->task_ends.empty()
        || bound->task_ends.back() != bound->members.size()
        || impl_->pure_wave_kernels.dispatch == nullptr
        || impl_->pure_wave_kernels.logic4_bit_and == nullptr
        || impl_->pure_wave_kernels.reducer31 == nullptr
        || impl_->pure_wave_kernels.reduction7 == nullptr
        || impl_->pure_wave_kernels.wide_copy6 == nullptr) {
        return false;
    }
    for (std::size_t index = 0U; index < bound->members.size(); ++index) {
        const auto& member = bound->members[index];
        if (member->released.load(std::memory_order_acquire)
            || member->runtime == nullptr || member->frame == nullptr
            || member->queued == nullptr || member->waiting == nullptr
            || member->process_status == nullptr
            || member->frame->abi_version
                != FSIM_JIT_FRAME_ABI_VERSION_V1
            || member->frame->struct_size < sizeof(fsim_jit_frame_v1)
            || member->frame->layout_id_low != member->layout_id_low
            || member->frame->layout_id_high != member->layout_id_high
            || member->frame->register_count
                != member->frame_register_count
            || member->frame->program_counter
                != member->expected_resume_instruction
            || member->frame->state != FSIM_JIT_FRAME_STATE_READY
            || member->frame->last_instruction
                != member->expected_wait_instruction
            || member->frame->native_call_depth != 0U
            || *member->queued == 0U || *member->waiting == 0U
            || *member->process_status != 2U) {
            return false;
        }
        const auto& runtime = *member->runtime;
        if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1
            || runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.flags != 0U
            || runtime.direct_read_signals
                != member->direct_read_signals
            || runtime.direct_read_signal_count
                != member->direct_read_signal_count
            || runtime.direct_update_slots != member->direct_update_slots
            || runtime.direct_update_slot_count
                != member->direct_update_slot_count
            || runtime.direct_update_active_words
                != member->direct_update_active_words
            || runtime.direct_update_active_word_count
                != member->direct_update_active_word_count
            || runtime.direct_signal_aval != member->direct_signal_aval
            || runtime.direct_signal_bval != member->direct_signal_bval
            || runtime.direct_signal_count != member->direct_signal_count
            || runtime.direct_wide_signal_aval
                != member->direct_wide_signal_aval
            || runtime.direct_wide_signal_bval
                != member->direct_wide_signal_bval
            || runtime.direct_wide_signal_offsets
                != member->direct_wide_signal_offsets
            || runtime.direct_wide_signal_offset_count
                != member->direct_wide_signal_offset_count
            || runtime.direct_wide_word_count
                != member->direct_wide_word_count) {
            return false;
        }
        for (std::uint32_t read_index = 0U;
            read_index < member->plan.read_count; ++read_index) {
            if (runtime.direct_read_signals[
                    member->plan.read_slots[read_index]]
                != member->plan.read_signals[read_index]) {
                return false;
            }
        }
        const auto& kernel_member = bound->kernel_members[index];
        if (member->plan.shape == Impl::PureWaveShape::reducer31
            && (runtime.direct_wide_signal_offsets == nullptr
                || member->plan.read_signals[0]
                    >= runtime.direct_wide_signal_offset_count
                || runtime.direct_wide_signal_offsets[
                       member->plan.read_signals[0]]
                    != kernel_member.wide_signal_word_offset)) {
            return false;
        }
        const auto& slot = runtime.direct_update_slots[
            member->plan.update_slot];
        if (slot.width != member->plan.update_width) {
            return false;
        }
        if (slot.width <= 64U) {
            if (member->plan.update_offset > slot.width
                || member->plan.register_widths[4]
                    > slot.width - member->plan.update_offset
                || member->plan.update_offset
                        + member->plan.register_widths[4]
                    > 64U) {
                return false;
            }
        } else if (slot.wide_aval == nullptr
            || slot.wide_bval == nullptr || slot.wide_mask == nullptr
            || slot.word_count < (slot.width + 63U) / 64U
            || member->plan.update_offset > slot.width
            || member->plan.register_widths[4]
                > slot.width - member->plan.update_offset) {
            return false;
        }
    }

    const std::scoped_lock lock { impl_->cohort_mutex };
    auto& member_pointers = impl_->pure_wave_member_scratch;
    auto& member_records = impl_->pure_wave_prepared_member_scratch;
    member_pointers.clear();
    member_records.clear();
    if (bound->members.size() > member_pointers.capacity()
        || bound->members.size() > member_records.capacity()) {
        return false;
    }
    for (const auto& member : bound->members) {
        runtime::simir::PureWavePreparedMember prepared;
        prepared.domain = impl_.get();
        prepared.compiler_view = &member->prepared_view;
        prepared.compiler_generation = member->generation;
        prepared.shape = static_cast<
            runtime::simir::PureWavePreparedShape>(member->plan.shape);
        prepared.and_lhs = member->plan.read_signals[0];
        prepared.and_rhs = member->plan.read_signals[1];
        prepared.valid = true;
        member_records.push_back(prepared);
        member_pointers.push_back(&member_records.back());
    }
    impl_->pure_wave_kernels.dispatch(
        member_pointers.data(), bound->kernel_task_ends.data(),
        bound->kernel_task_shapes.data(),
        static_cast<std::uint32_t>(bound->kernel_task_ends.size()));
    return true;
}

std::optional<std::size_t>
LlvmJit::try_resume_pure_wave_members_prevalidated(
    const std::span<const JitPureWaveMemberLease* const> members,
    const std::span<const std::size_t> task_ends) const
{
    if (!impl_ || members.empty() || task_ends.empty()
        || members.size() > 8192U || task_ends.size() > 256U
        || task_ends.back() != members.size()) {
        return std::nullopt;
    }

    // The prepared views and dispatch scratch belong to this JIT. Serialize
    // preflight through dispatch so independent app domains cannot race the
    // bounded scratch arrays or member release.
    const std::scoped_lock lock { impl_->cohort_mutex };
    impl_->ensure_pure_wave_kernels();
    if (impl_->pure_wave_kernels.dispatch == nullptr
        || impl_->pure_wave_kernels.logic4_bit_and == nullptr
        || impl_->pure_wave_kernels.reducer31 == nullptr
        || impl_->pure_wave_kernels.reduction7 == nullptr
        || impl_->pure_wave_kernels.wide_copy6 == nullptr) {
        return std::nullopt;
    }

    auto& member_pointers = impl_->pure_wave_member_scratch;
    auto& member_records = impl_->pure_wave_prepared_member_scratch;
    auto& kernel_task_ends = impl_->pure_wave_task_end_scratch;
    auto& kernel_task_shapes = impl_->pure_wave_task_shape_scratch;
    member_pointers.clear();
    member_records.clear();
    kernel_task_ends.clear();
    kernel_task_shapes.clear();
    if (member_pointers.capacity() < members.size()
        || member_records.capacity() < members.size()
        || kernel_task_ends.capacity() < task_ends.size()
        || kernel_task_shapes.capacity() < task_ends.size()) {
        return std::nullopt;
    }

    std::size_t task_begin { };
    for (const auto task_end : task_ends) {
        if (task_end <= task_begin || task_end > members.size()) {
            return std::nullopt;
        }

        const Impl::NativeBoundPureWaveMemberEntry* first_member { };
        std::optional<Impl::PureWaveShape> task_shape;
        for (std::size_t index = task_begin; index < task_end; ++index) {
            const auto* const lease = members[index];
            if (lease == nullptr || lease->owner_ != impl_.get()
                || lease->entry_ == nullptr
                || lease->storage_.get() != lease->entry_) {
                return std::nullopt;
            }
            const auto* const member
                = static_cast<
                    const Impl::NativeBoundPureWaveMemberEntry*>(
                        lease->entry_);
            if (member->generation != lease->generation_
                || member->released.load(std::memory_order_acquire)) {
                return std::nullopt;
            }

            const auto shape = member->plan.shape;
            if (shape > Impl::PureWaveShape::wide_copy6
                || (task_shape && *task_shape != shape)) {
                return std::nullopt;
            }
            if (first_member == nullptr) {
                first_member = member;
                task_shape = shape;
            } else if (shape == Impl::PureWaveShape::logic4_bit_and
                && (member->plan.read_signals[0]
                        != first_member->plan.read_signals[0]
                    || member->plan.read_signals[1]
                        != first_member->plan.read_signals[1]
                    || member->direct_signal_aval
                        != first_member->direct_signal_aval
                    || member->direct_signal_bval
                        != first_member->direct_signal_bval)) {
                return std::nullopt;
            }
            if (shape != Impl::PureWaveShape::logic4_bit_and
                && task_end - task_begin != 1U) {
                return std::nullopt;
            }
            runtime::simir::PureWavePreparedMember prepared;
            prepared.domain = impl_.get();
            prepared.compiler_view = &member->prepared_view;
            prepared.compiler_generation = member->generation;
            prepared.shape
                = static_cast<runtime::simir::PureWavePreparedShape>(shape);
            prepared.and_lhs = member->plan.read_signals[0];
            prepared.and_rhs = member->plan.read_signals[1];
            prepared.valid = true;
            member_records.push_back(prepared);
            member_pointers.push_back(&member_records.back());
        }

        if (first_member == nullptr || !task_shape
            || task_end > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        kernel_task_ends.push_back(static_cast<std::uint32_t>(task_end));
        kernel_task_shapes.push_back(
            static_cast<std::uint8_t>(*task_shape));
        task_begin = task_end;
    }
    if (task_begin != members.size()
        || member_records.size() != members.size()
        || member_pointers.size() != members.size()
        || kernel_task_ends.size() != task_ends.size()) {
        return std::nullopt;
    }

    impl_->pure_wave_kernels.dispatch(
        member_pointers.data(), kernel_task_ends.data(),
        kernel_task_shapes.data(),
        static_cast<std::uint32_t>(kernel_task_ends.size()));
    return task_ends.size();
}

std::optional<std::size_t>
LlvmJit::try_resume_pure_wave_prepared_members_prevalidated(
    const std::span<const runtime::simir::PureWavePreparedMember* const>
        members,
    const std::span<const std::size_t> task_ends) const
{
    using runtime::simir::PureWavePreparedShape;
    static_assert(static_cast<std::uint8_t>(PureWavePreparedShape::logic4_bit_and)
        == static_cast<std::uint8_t>(Impl::PureWaveShape::logic4_bit_and));
    static_assert(static_cast<std::uint8_t>(PureWavePreparedShape::reducer31)
        == static_cast<std::uint8_t>(Impl::PureWaveShape::reducer31));
    static_assert(static_cast<std::uint8_t>(PureWavePreparedShape::reduction7)
        == static_cast<std::uint8_t>(Impl::PureWaveShape::reduction7));
    static_assert(static_cast<std::uint8_t>(PureWavePreparedShape::wide_copy6)
        == static_cast<std::uint8_t>(Impl::PureWaveShape::wide_copy6));

    if (!impl_ || members.empty() || task_ends.empty()
        || members.size() > 8192U || task_ends.size() > 256U
        || task_ends.back() != members.size()) {
        return std::nullopt;
    }

    // Prepared descriptors, task scratch, and lease invalidation all share
    // this lock. The caller keeps every lease and external buffer alive for
    // the full call, so these checks precede all generated stores.
    const std::scoped_lock lock { impl_->cohort_mutex };
    impl_->ensure_pure_wave_kernels();
    if (impl_->pure_wave_kernels.dispatch == nullptr
        || impl_->pure_wave_kernels.logic4_bit_and == nullptr
        || impl_->pure_wave_kernels.reducer31 == nullptr
        || impl_->pure_wave_kernels.reduction7 == nullptr
        || impl_->pure_wave_kernels.wide_copy6 == nullptr) {
        return std::nullopt;
    }

    auto& kernel_task_ends = impl_->pure_wave_task_end_scratch;
    auto& kernel_task_shapes = impl_->pure_wave_task_shape_scratch;
    kernel_task_ends.clear();
    kernel_task_shapes.clear();
    if (kernel_task_ends.capacity() < task_ends.size()
        || kernel_task_shapes.capacity() < task_ends.size()) {
        return std::nullopt;
    }

    std::size_t task_begin { };
    for (const auto task_end : task_ends) {
        if (task_end <= task_begin || task_end > members.size()
            || task_end > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }

        const NativePureWavePreparedView* first_view { };
        std::optional<Impl::PureWaveShape> task_shape;
        for (std::size_t index = task_begin; index < task_end; ++index) {
            const auto* const prepared = members[index];
            if (prepared == nullptr || !prepared->valid
                || prepared->domain != impl_.get()
                || prepared->compiler_view == nullptr) {
                return std::nullopt;
            }
            const auto* const view
                = static_cast<const NativePureWavePreparedView*>(
                    prepared->compiler_view);
            if (view->owner != impl_.get()
                || view->generation != prepared->compiler_generation
                || view->released == nullptr
                || view->released->load(std::memory_order_acquire)
                || view->kernel_member == nullptr
                || view->shape
                    > static_cast<std::uint32_t>(
                        Impl::PureWaveShape::wide_copy6)
                || prepared->shape
                    != static_cast<PureWavePreparedShape>(view->shape)) {
                return std::nullopt;
            }

            const auto shape = static_cast<Impl::PureWaveShape>(view->shape);
            if (task_shape && *task_shape != shape) {
                return std::nullopt;
            }
            if (shape == Impl::PureWaveShape::logic4_bit_and
                && (prepared->and_lhs != view->signal0
                    || prepared->and_rhs != view->signal1)) {
                return std::nullopt;
            }
            if (first_view == nullptr) {
                first_view = view;
                task_shape = shape;
            } else if (shape == Impl::PureWaveShape::logic4_bit_and
                && (view->signal0 != first_view->signal0
                    || view->signal1 != first_view->signal1
                    || view->kernel_member->direct_signal_aval
                        != first_view->kernel_member->direct_signal_aval
                    || view->kernel_member->direct_signal_bval
                        != first_view->kernel_member->direct_signal_bval)) {
                return std::nullopt;
            }
            if (shape != Impl::PureWaveShape::logic4_bit_and
                && task_end - task_begin != 1U) {
                return std::nullopt;
            }
        }

        if (first_view == nullptr || !task_shape) {
            return std::nullopt;
        }
        kernel_task_ends.push_back(static_cast<std::uint32_t>(task_end));
        kernel_task_shapes.push_back(
            static_cast<std::uint8_t>(*task_shape));
        task_begin = task_end;
    }
    if (task_begin != members.size()
        || kernel_task_ends.size() != task_ends.size()
        || kernel_task_shapes.size() != task_ends.size()) {
        return std::nullopt;
    }

    impl_->pure_wave_kernels.dispatch(
        members.data(), kernel_task_ends.data(), kernel_task_shapes.data(),
        static_cast<std::uint32_t>(kernel_task_ends.size()));
    return task_ends.size();
}

std::size_t LlvmJit::resume_region_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries,
    const std::span<const std::size_t> active_indices) const
{
    if (!impl_ || entries.size() < 2U || active_indices.empty()
        || !std::ranges::is_sorted(active_indices)
        || std::ranges::adjacent_find(active_indices)
            != active_indices.end()
        || !std::ranges::all_of(
            active_indices,
            [&](const auto index) {
                if (index >= entries.size()) {
                    return false;
                }
                const auto& entry = entries[index];
                return entry.active != nullptr
                    && entry.queued != nullptr
                    && entry.waiting_on_static != nullptr
                    && entry.process_status != nullptr
                    && entry.process.owner_ == impl_.get()
                    && entry.process.entry_ != nullptr
                    && entry.runtime != nullptr && entry.frame != nullptr
                    && entry.result != nullptr;
            })) {
        throw LlvmJitError("invalid LLVM process region");
    }
    for (const auto index : active_indices) {
        const auto& entry = entries[index];
        const auto& native
            = *static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        validate_native_service_callbacks(native.info, *entry.runtime);
    }
    for (const auto index : active_indices) {
        auto& entry = entries[index];
        if (*entry.active == 0U) {
            throw LlvmJitError(
                "LLVM process region active index is not selected");
        }
        *entry.active = 0U;
        *entry.queued = 0U;
        *entry.waiting_on_static = 0U;
        *entry.process_status = 1U;
        const auto& native
            = *static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        entry.status = native.function(
            entry.runtime, entry.frame, entry.result);
        if (entry.status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY) {
            *entry.waiting_on_static = 1U;
            *entry.process_status = 2U;
            continue;
        }
        if (entry.status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        entry.result->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        entry.result->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error "
                    "reason");
            } catch (...) {
                entry.failure = std::current_exception();
            }
        }
        return index + 1U;
    }
    return entries.size();
}

JitResumeStatus
LlvmJit::resume(const JitProcessBinding process,
    const fsim_jit_runtime_v1& runtime,
    fsim_jit_frame_v1& frame,
    fsim_jit_resume_result_v1& result) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1) {
        throw LlvmJitError("JIT runtime ABI version mismatch");
    }
    if (runtime.struct_size < kJitRuntimeV1PrefixSize) {
        throw LlvmJitError("JIT runtime ABI structure is too small");
    }
    if (runtime.read_signal == nullptr || runtime.write_signal == nullptr || runtime.assert_failed == nullptr) {
        throw LlvmJitError("JIT runtime ABI requires all v1 callbacks");
    }
    if (result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1) {
        throw LlvmJitError("JIT resume-result ABI version mismatch");
    }
    if (result.struct_size < sizeof(fsim_jit_resume_result_v1)) {
        throw LlvmJitError("JIT resume-result ABI structure is too small");
    }

    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    if (impl_->options.require_direct_update_slots
        && !entry.info.frame_layout.direct_update_signals.empty()
        && (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.direct_update_slots == nullptr
            || runtime.direct_update_slot_count
                < entry.info.frame_layout.direct_update_signals.size()
            || runtime.direct_update_active_words == nullptr
            || runtime.direct_update_active_word_count
                < (entry.info.frame_layout.direct_update_signals.size() + 63U)
                    / 64U)) {
        throw LlvmJitError(
            "JIT runtime ABI requires every direct-update slot promised at "
            "lowering time");
    }
    if (entry.info.uses_write_update) {
        if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, write_after)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_update");
        }
        if (runtime.write_update == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_update for this process");
        }
    }
    if (entry.info.uses_code_coverage) {
        if (runtime.struct_size < sizeof(fsim_jit_runtime_v1)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include code coverage counters");
        }
        if (runtime.record_code_coverage_counter == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires the code coverage checked service");
        }
    }
    validate_native_service_callbacks(entry.info, runtime);
    if (entry.info.uses_write_after) {
        if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, flags)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_after");
        }
        if (runtime.write_after == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_after for this process");
        }
    }
    if (entry.info.uses_write_inertial) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_inertial_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_inertial");
        }
        if (runtime.write_inertial == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_inertial for this process");
        }
    }
    if (entry.info.uses_write_blocking_slice) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1, write_update_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_signal_slice");
        }
        if (runtime.write_signal_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_signal_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_update_slice) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1, write_after_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_update_slice");
        }
        if (runtime.write_update_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_update_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_after_slice) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_event)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_after_slice");
        }
        if (runtime.write_after_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_after_slice for this "
                "process");
        }
    }
    if (entry.info.uses_force_signal_slice) {
        if (runtime.struct_size < kJitRuntimeForceSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include force_signal_slice");
        }
        if (runtime.force_signal_slice == nullptr
            || (entry.info.frame_layout.uses_logic9
                && runtime.force_signal_slice_logic9 == nullptr)) {
            throw LlvmJitError(
                "JIT runtime ABI requires force_signal_slice callbacks for this "
                "process");
        }
    }
    if (entry.info.uses_release_signal_slice) {
        if (runtime.struct_size < kJitRuntimeForceSize
            || runtime.release_signal_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires release_signal_slice for this process");
        }
    }
    if (entry.info.uses_force_driver_signal_slice) {
        if (runtime.struct_size < kJitRuntimeDriverForceSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "force_driver_signal_slice");
        }
        if (runtime.force_driver_signal_slice == nullptr
            || (entry.info.frame_layout.uses_logic9
                && runtime.force_driver_signal_slice_logic9 == nullptr)) {
            throw LlvmJitError(
                "JIT runtime ABI requires force_driver_signal_slice callbacks for "
                "this process");
        }
    }
    if (entry.info.uses_release_driver_signal_slice) {
        if (runtime.struct_size < kJitRuntimeDriverForceSize
            || runtime.release_driver_signal_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires release_driver_signal_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_inertial_slice) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_projected)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_inertial_slice");
        }
        if (runtime.write_inertial_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_inertial_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_projected) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_projected_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected");
        }
        if (runtime.write_projected == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected for this process");
        }
    }
    if (entry.info.uses_write_projected_slice) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1, write_projected_waveform)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected_slice");
        }
        if (runtime.write_projected_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_projected_waveform) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1,
                write_projected_waveform_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected_waveform");
        }
        if (runtime.write_projected_waveform == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected_waveform for this "
                "process");
        }
    }
    if (entry.info.uses_write_projected_waveform_slice) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, read_signal_logic9)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected_waveform_slice");
        }
        if (runtime.write_projected_waveform_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected_waveform_slice for this "
                "process");
        }
    }
    if (entry.info.uses_debug_points
        && runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, reserved)) {
        throw LlvmJitError(
            "JIT runtime ABI structure does not include debug-point flags");
    }
    if (entry.info.uses_signal_event) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_last_value)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_event");
        }
        if (runtime.signal_event == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_event for this process");
        }
    }
    if (entry.info.uses_signal_last_value) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_last_event)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_last_value");
        }
        if (runtime.signal_last_value == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_last_value for this process");
        }
    }
    if (entry.info.uses_signal_last_event) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_active)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_last_event");
        }
        if (runtime.signal_last_event == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_last_event for this process");
        }
    }
    if (entry.info.uses_simulation_time) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, vital_timing_check)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include read_simulation_time");
        }
        if (runtime.read_simulation_time == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires read_simulation_time for this process");
        }
    }
    if (entry.info.uses_vital_timing) {
        if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, vital_delay)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include vital_timing_check");
        }
        if (runtime.vital_timing_check == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires vital_timing_check for this process");
        }
    }
    if (entry.info.uses_vital_delay) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, force_driver_signal_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include vital_delay");
        }
        if (runtime.vital_delay == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires vital_delay for this process");
        }
    }
    if (entry.info.uses_signal_active) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_output)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_active");
        }
        if (runtime.signal_active == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_active for this process");
        }
    }
    if (entry.info.uses_signal_last_active) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_driving)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_last_active");
        }
        if (runtime.signal_last_active == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_last_active for this process");
        }
    }
    if (entry.info.uses_signal_driving) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_driving_value)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_driving");
        }
        if (runtime.signal_driving == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_driving for this process");
        }
    }
    if (entry.info.uses_signal_driving_value) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, read_simulation_time)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_driving_value");
        }
        if (runtime.signal_driving_value == nullptr
            || runtime.signal_driving_value_logic9 == nullptr) {
            throw LlvmJitError(
                "JIT runtime requires signal driving-value callbacks for this process");
        }
    }
    if (entry.info.uses_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, schedule_output)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_output");
        }
        if (runtime.write_output == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_output for this process");
        }
    }
    if (entry.info.uses_postponed_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_report)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include schedule_output");
        }
        if (runtime.schedule_output == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires schedule_output for this process");
        }
    }
    if (entry.info.uses_report) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_formatted)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_report");
        }
        if (runtime.write_report == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_report for this process");
        }
    }
    if (entry.info.uses_formatted_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_time)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_formatted");
        }
        if (runtime.write_formatted == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_formatted for this process");
        }
    }
    if (entry.info.uses_time_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, install_monitor)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_time");
        }
        if (runtime.write_time == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_time for this process");
        }
    }
    if (entry.info.uses_monitor_install) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, control_monitor)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include install_monitor");
        }
        if (runtime.install_monitor == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires install_monitor for this process");
        }
    }
    if (entry.info.uses_monitor_control) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, random_value)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include control_monitor");
        }
        if (runtime.control_monitor == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires control_monitor for this process");
        }
    }
    if (entry.info.uses_random_value) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_inertial)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include random_value");
        }
        if (runtime.random_value == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires random_value for this process");
        }
    }
    if (entry.info.frame_layout.uses_logic9) {
        if (runtime.struct_size < kJitRuntimeLogic9Size) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include Logic9 callbacks");
        }
        if (runtime.read_signal_logic9 == nullptr
            || runtime.write_signal_logic9 == nullptr
            || runtime.write_update_logic9 == nullptr
            || runtime.write_after_logic9 == nullptr
            || runtime.write_signal_slice_logic9 == nullptr
            || runtime.write_update_slice_logic9 == nullptr
            || runtime.write_after_slice_logic9 == nullptr
            || runtime.signal_last_value_logic9 == nullptr
            || runtime.write_inertial_logic9 == nullptr
            || runtime.write_inertial_slice_logic9 == nullptr
            || runtime.write_projected_logic9 == nullptr
            || runtime.write_projected_slice_logic9 == nullptr
            || runtime.write_projected_waveform_logic9 == nullptr
            || runtime.write_projected_waveform_slice_logic9 == nullptr
            || runtime.write_formatted_logic9 == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires Logic9 callbacks for this process");
        }
    }
    if (entry.info.uses_strings) {
        if (runtime.struct_size < kJitRuntimeStringSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include mutable-string "
                "callbacks");
        }
        if (runtime.load_string == nullptr
            || runtime.copy_string == nullptr
            || runtime.read_string_object == nullptr
            || runtime.write_string_object == nullptr
            || runtime.concatenate_strings == nullptr
            || runtime.compare_strings == nullptr
            || runtime.string_length == nullptr
            || runtime.string_index == nullptr
            || runtime.string_replace_byte == nullptr
            || runtime.write_string_output == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires mutable-string callbacks for this "
                "process");
        }
    }
    if (entry.info.uses_files) {
        if (runtime.struct_size < kJitRuntimeFileSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include text-file callbacks");
        }
        if (runtime.file_open == nullptr
            || runtime.file_close == nullptr
            || runtime.file_write == nullptr
            || runtime.file_read_line == nullptr
            || runtime.file_end_of_file == nullptr
            || runtime.file_error == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires text-file callbacks for this process");
        }
    }
    if (entry.info.uses_containers) {
        if (runtime.struct_size
                < kJitRuntimeContainerWordSize
            || runtime.container_operation == nullptr
            || runtime.container_read_word == nullptr
            || runtime.container_write_word == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires bounded-container callbacks for this "
                "process");
        }
    }
    if (entry.info.uses_wide_container_operation) {
        if (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.container_read_packed == nullptr
            || runtime.container_write_packed == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires arbitrary-width packed-container "
                "callbacks for this process");
        }
    }
    if (entry.info.uses_exact_signal_operation) {
        if (runtime.struct_size < kJitRuntimeExactSignalSize
            || runtime.execute_signal_operation == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires execute_signal_operation for this "
                "process");
        }
    }
    if (entry.info.uses_wide_signal_read) {
        if (runtime.struct_size < kJitRuntimeWideSignalReadSize
            || runtime.read_signal_packed == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires read_signal_packed for this process");
        }
    }
    if (entry.info.uses_wide_signal_write) {
        if (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.write_signal_packed == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_signal_packed for this process");
        }
    }
    if (frame.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V1) {
        throw LlvmJitError("JIT frame ABI version mismatch");
    }
    if (frame.struct_size < kJitFrameV1PrefixSize
        || ((entry.info.frame_layout.uses_logic9
                || entry.info.uses_native_call_stack)
            && frame.struct_size < sizeof(fsim_jit_frame_v1))) {
        throw LlvmJitError("JIT frame ABI structure is too small");
    }
    if (frame.layout_id_low != entry.info.frame_layout.layout_id_low || frame.layout_id_high != entry.info.frame_layout.layout_id_high || frame.register_count != entry.info.frame_layout.register_count) {
        throw LlvmJitError("JIT frame layout mismatch");
    }
    if ((entry.info.frame_layout.register_word_count != 0
            && (frame.register_aval == nullptr
                || frame.register_bval == nullptr))
        || (frame.register_count != 0
            && frame.register_initialized == nullptr)) {
        throw LlvmJitError("JIT frame register storage is null");
    }
    if (entry.info.frame_layout.register_word_count != 0
        && frame.register_aval == frame.register_bval) {
        throw LlvmJitError(
            "JIT frame aval and bval register storage must be distinct");
    }
    if (entry.info.frame_layout.uses_logic9
        && entry.info.frame_layout.register_word_count != 0
        && (frame.register_logic9_plane2 == nullptr
            || frame.register_logic9_plane3 == nullptr)) {
        throw LlvmJitError("JIT frame Logic9 register storage is null");
    }

    const auto terminal_result =
        [&](const std::uint32_t status) -> JitResumeStatus {
        result.status = status;
        result.instruction = frame.last_instruction;
        result.delay = 0;
        return static_cast<JitResumeStatus>(status);
    };
    switch (frame.state) {
    case FSIM_JIT_FRAME_STATE_READY:
        if (frame.program_counter >= entry.info.operation_count) {
            throw LlvmJitError(
                "JIT frame program counter is outside the operation stream");
        }
        break;
    case FSIM_JIT_FRAME_STATE_COMPLETED:
        return terminal_result(FSIM_JIT_RESUME_STATUS_COMPLETED);
    case FSIM_JIT_FRAME_STATE_STOPPED:
        return terminal_result(FSIM_JIT_RESUME_STATUS_STOPPED);
    case FSIM_JIT_FRAME_STATE_ASSERTION_FAILED:
        return terminal_result(FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED);
    case FSIM_JIT_FRAME_STATE_RUNTIME_ERROR:
        if (const auto reason = decode_generated_runtime_error(frame.program_counter)) {
            throw LlvmJitGeneratedRuntimeError(
                frame.last_instruction, *reason);
        }
        throw LlvmJitError(
            "JIT frame contains an invalid generated runtime error reason");
    default:
        throw LlvmJitError("JIT frame state is invalid");
    }

    const auto raw_status = entry.function(&runtime, &frame, &result);
    if (raw_status != result.status) {
        throw LlvmJitError(
            "generated process returned an inconsistent resume status");
    }
    switch (raw_status) {
    case FSIM_JIT_RESUME_STATUS_COMPLETED:
        if (frame.state != FSIM_JIT_FRAME_STATE_COMPLETED) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::completed;
    case FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED:
        if (frame.state != FSIM_JIT_FRAME_STATE_ASSERTION_FAILED) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::assertion_failed;
    case FSIM_JIT_RESUME_STATUS_WAIT_FOR:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_for;
    case FSIM_JIT_RESUME_STATUS_WAIT_ON:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_on;
    case FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_sensitivity;
    case FSIM_JIT_RESUME_STATUS_WAIT_FOREVER:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_forever;
    case FSIM_JIT_RESUME_STATUS_DEBUG_POINT:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::debug_point;
    case FSIM_JIT_RESUME_STATUS_YIELDED:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::yielded;
    case FSIM_JIT_RESUME_STATUS_PAUSED:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::paused;
    case FSIM_JIT_RESUME_STATUS_FORK:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::fork;
    case FSIM_JIT_RESUME_STATUS_FORK_END:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::fork_end;
    case FSIM_JIT_RESUME_STATUS_WAIT_FORK:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_fork;
    case FSIM_JIT_RESUME_STATUS_DISABLE_FORK:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::disable_fork;
    case FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::simir_boundary;
    case FSIM_JIT_RESUME_STATUS_STOPPED:
        if (frame.state != FSIM_JIT_FRAME_STATE_STOPPED) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::stopped;
    case FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR:
        if (frame.state != FSIM_JIT_FRAME_STATE_RUNTIME_ERROR) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        if (const auto reason = decode_generated_runtime_error(result.delay)) {
            throw LlvmJitGeneratedRuntimeError(
                result.instruction, *reason);
        }
        throw LlvmJitError(
            "generated process returned an invalid runtime error reason");
    default:
        throw LlvmJitError("generated process returned an unknown resume status");
    }
}

JitExecutionStatus
LlvmJit::execute(const JitProcessHandle process,
    const fsim_jit_runtime_v1& runtime) const
{
    const auto binding = bind(process);
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(binding.entry_);
    if (entry.info.requires_resume) {
        throw LlvmJitUnsupportedError(
            "compiled process can suspend; use initialize_frame() and resume()");
    }

    std::vector<std::uint64_t> register_aval(
        entry.info.frame_layout.register_word_count);
    std::vector<std::uint64_t> register_bval(
        entry.info.frame_layout.register_word_count);
    std::vector<std::uint8_t> register_initialized(
        entry.info.frame_layout.register_count);
    std::vector<std::uint64_t> register_logic9_plane2(
        entry.info.frame_layout.uses_logic9
            ? entry.info.frame_layout.register_word_count
            : 0);
    std::vector<std::uint64_t> register_logic9_plane3(
        entry.info.frame_layout.uses_logic9
            ? entry.info.frame_layout.register_word_count
            : 0);
    fsim_jit_frame_v1 frame { };
    initialize_frame(
        binding,
        frame,
        register_aval,
        register_bval,
        register_initialized,
        register_logic9_plane2,
        register_logic9_plane3);
    fsim_jit_resume_result_v1 result {
        FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1,
        static_cast<std::uint32_t>(sizeof(fsim_jit_resume_result_v1)),
        0,
        FSIM_JIT_INVALID_INSTRUCTION,
        0,
    };
    switch (resume(binding, runtime, frame, result)) {
    case JitResumeStatus::completed:
        return JitExecutionStatus::completed;
    case JitResumeStatus::assertion_failed:
        return JitExecutionStatus::assertion_failed;
    case JitResumeStatus::stopped:
        return JitExecutionStatus::stopped;
    case JitResumeStatus::wait_for:
    case JitResumeStatus::wait_on:
    case JitResumeStatus::wait_sensitivity:
    case JitResumeStatus::wait_forever:
    case JitResumeStatus::yielded:
    case JitResumeStatus::debug_point:
    case JitResumeStatus::paused:
    case JitResumeStatus::fork:
    case JitResumeStatus::fork_end:
    case JitResumeStatus::wait_fork:
    case JitResumeStatus::disable_fork:
    case JitResumeStatus::simir_boundary:
        throw LlvmJitError(
            "compiled process suspended during one-shot execution");
    default:
        throw LlvmJitError("generated process returned an unknown resume status");
    }
}

} // namespace fsim::compiler

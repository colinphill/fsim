// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"
#include "fsim/compiler/fused_masked_process.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace fsim::tests::compiler {
namespace {
using namespace fsim::compiler;
using namespace fsim::runtime::simir;

std::vector<Process> make_masked_members(const std::uint32_t width)
{
    std::vector<Process> members(width);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        auto& member = members[bit];
        member.id = bit + 10U;
        member.name = "masked_diamond_" + std::to_string(bit);
        member.register_count = 7U;
        member.static_sensitivity = { { bit % 2U, EdgeKind::any } };
        member.driver_regions = { { 2U, bit, 1U, false } };
        member.operations = {
            DebugPoint { DebugPointKind::process_entry, { "masked.sv", 1U, 1U },
                member.name },
            ReadSignal { 0U, 0U }, ReadSignal { 1U, 1U },
            Extract { 2U, 0U, bit, 1U },
            LoadConstant { 3U, PackedLogic4(1U, Logic4::one) },
            Binary { BinaryOperator::case_equal, 4U, 2U, 3U },
            Branch { 4U, 7U, 9U, UnknownBranchPolicy::when_false },
            Binary { BinaryOperator::bit_xor, 5U, 0U, 1U }, Jump { 10U },
            Binary { BinaryOperator::bit_or, 5U, 0U, 1U },
            Extract { 6U, 5U, bit, 1U }, WriteUpdateSlice { 2U, 6U, bit },
            WaitSensitivity { }, Jump { 0U }
        };
    }
    return members;
}

std::vector<const Process*> pointers(const std::vector<Process>& members)
{
    std::vector<const Process*> result;
    for (const auto& member : members) {
        result.push_back(&member);
    }
    return result;
}

void check_masked_native(const JitOptimizationLevel optimization,
    const std::uint32_t width)
{
    const std::array<std::uint32_t, 4> widths { width, width, width, width };
    const std::array kinds { ValueKind::logic4, ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4 };
    auto members = make_masked_members(width);
    Process terminal;
    terminal.id = width + 10U;
    terminal.name = "masked_terminal";
    terminal.register_count = 1U;
    terminal.static_sensitivity = { { 2U, EdgeKind::any } };
    terminal.driver_regions = { { 3U, 0U, width, true } };
    terminal.operations = { ReadSignal { 0U, 2U }, WriteUpdate { 3U, 0U },
        WaitSensitivity { }, Jump { 0U } };
    members.push_back(std::move(terminal));
    const auto fused = fuse_masked_processes(pointers(members), widths, kinds, 999U);
    assert(fused && fused->gates.size() == members.size()
        && fused->writes.size() == members.size());
    for (std::size_t index = 0U; index < width; ++index) {
        assert(fused->gates[index].activation_bit == index);
        const auto& point = operation_get<DebugPoint>(
            fused->process.operations[fused->gates[index].begin_instruction]);
        assert(point.scope.empty() && point.source.path.empty());
        assert(fused->writes[index].regions.size() == 1U);
        assert(fused->writes[index].regions[0].offset == index);
        assert(fused->writes[index].regions[0].width == 1U);
    }
    LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    LlvmJit jit(options);
    jit.add_masked_process("masked_native", *fused, widths, kinds);
    const auto handle = jit.lookup("masked_native");
    const auto layout = jit.frame_layout(handle);
    if (width == 3U) {
        // Same symbol and SimIR body must not reuse an unmasked native layout.
        LlvmJit ordinary(options);
        ordinary.add_process("masked_native", fused->process, widths, kinds);
        const auto plain_layout = ordinary.frame_layout(ordinary.lookup("masked_native"));
        assert(plain_layout.layout_id_low != layout.layout_id_low
            || plain_layout.layout_id_high != layout.layout_id_high);
    }
    assert((layout.direct_update_signals == std::vector<SignalId> { 2U, 3U }));
    std::vector<std::uint64_t> registers_aval(layout.register_word_count);
    std::vector<std::uint64_t> registers_bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, registers_aval, registers_bval, initialized);
    TestRuntime callbacks;
    auto runtime = abi(callbacks);
    runtime.flags = 0U;
    std::array<std::uint64_t, 4> signal_aval { }, signal_bval { };
    std::array<std::uint64_t, 12> wide_aval { }, wide_bval { };
    const std::array<std::uint32_t, 4> offsets { 0U, 3U, 6U, 9U };
    runtime.direct_signal_aval = signal_aval.data();
    runtime.direct_signal_bval = signal_bval.data();
    runtime.direct_signal_count = 4U;
    runtime.direct_read_signals = layout.direct_read_signals.data();
    runtime.direct_read_signal_count = static_cast<std::uint32_t>(layout.direct_read_signals.size());
    runtime.direct_wide_signal_aval = wide_aval.data();
    runtime.direct_wide_signal_bval = wide_bval.data();
    runtime.direct_wide_signal_offsets = offsets.data();
    runtime.direct_wide_signal_offset_count = 4U;
    runtime.direct_wide_word_count = 12U;
    std::array<fsim_jit_update_slot_v1, 2> slots { };
    auto& slot = slots[0];
    auto& terminal_slot = slots[1];
    std::array<std::uint64_t, 3> output_aval { }, output_bval { }, output_mask { };
    std::array<std::uint64_t, 3> terminal_aval { }, terminal_bval { }, terminal_mask { };
    std::uint64_t slot_active { };
    runtime.direct_update_slots = slots.data();
    runtime.direct_update_slot_count = 2U;
    runtime.direct_update_active_words = &slot_active;
    runtime.direct_update_active_word_count = 1U;
    std::array<std::uint64_t, 3> activation { };
    runtime.fused_activation_words = activation.data();
    runtime.fused_activation_word_count = (width + 1U + 63U) / 64U;
    constexpr std::array states { Logic4::zero, Logic4::one, Logic4::x, Logic4::z };
    for (std::size_t row = 0U; row < 12U; ++row) {
        PackedLogic4 lhs(width, Logic4::zero), rhs(width, Logic4::zero);
        activation.fill(0U);
        for (std::uint32_t bit = 0U; bit < width; ++bit) {
            lhs.set(bit, states[(bit + row) % states.size()]);
            rhs.set(bit, states[(bit / 3U + row / 2U) % states.size()]);
            const bool selected = row % 6U == 1U
                || (row % 6U == 2U && bit % 3U == 0U)
                || (row % 6U == 3U && (bit == 63U || bit == 64U || bit == 128U))
                || (row % 6U == 4U && bit % 2U != 0U)
                || (row % 6U == 5U && (bit == 0U || bit + 1U == width));
            if (selected) {
                activation[bit / 64U] |= UINT64_C(1) << (bit % 64U);
            }
        }
        const auto terminal_active = row % 3U == 0U;
        if (terminal_active) {
            activation[width / 64U] |= UINT64_C(1) << (width % 64U);
        }
        for (const auto& [signal, value] : { std::pair { 0U, &lhs },
                 std::pair { 1U, &rhs }, std::pair { 2U, &rhs } }) {
            std::ranges::copy(value->aval_words(), wide_aval.begin() + offsets[signal]);
            std::ranges::copy(value->bval_words(), wide_bval.begin() + offsets[signal]);
            signal_aval[signal] = value->aval_words().front();
            signal_bval[signal] = value->bval_words().front();
        }
        slot = { };
        slot.width = width;
        slot.word_count = (width + 63U) / 64U;
        slot.wide_aval = output_aval.data();
        slot.wide_bval = output_bval.data();
        slot.wide_mask = output_mask.data();
        output_aval.fill(0U);
        output_bval.fill(0U);
        output_mask.fill(0U);
        terminal_slot = { };
        terminal_slot.width = width;
        terminal_slot.word_count = slot.word_count;
        terminal_slot.wide_aval = terminal_aval.data();
        terminal_slot.wide_bval = terminal_bval.data();
        terminal_slot.wide_mask = terminal_mask.data();
        terminal_aval.fill(0U);
        terminal_bval.fill(0U);
        terminal_mask.fill(0U);
        slot_active = 0U;
        auto result = new_resume_result();
        assert(jit.resume(handle, runtime, frame, result) == JitResumeStatus::wait_sensitivity);
        if (width <= 64U) {
            output_aval[0] = slot.aval;
            output_bval[0] = slot.bval;
            output_mask[0] = slot.mask;
            terminal_aval[0] = terminal_slot.aval;
            terminal_bval[0] = terminal_slot.bval;
            terminal_mask[0] = terminal_slot.mask;
        }
        const auto xor_value = binary_value(BinaryOperator::bit_xor, lhs, rhs);
        const auto or_value = binary_value(BinaryOperator::bit_or, lhs, rhs);
        const auto actual = PackedLogic4::from_word_planes(width,
            std::span(output_aval).first(slot.word_count),
            std::span(output_bval).first(slot.word_count));
        for (std::uint32_t bit = 0U; bit < width; ++bit) {
            const auto mask = UINT64_C(1) << (bit % 64U);
            const bool selected = (activation[bit / 64U] & mask) != 0U;
            assert(((output_mask[bit / 64U] & mask) != 0U) == selected);
            if (selected) {
                assert(actual.get(bit) == (lhs.get(bit) == Logic4::one
                    ? xor_value.get(bit) : or_value.get(bit)));
            }
        }
        const auto producer_active = row % 6U != 0U
            && (row % 6U != 3U || width > 63U);
        assert(slot_active == (static_cast<std::uint64_t>(producer_active)
            | (static_cast<std::uint64_t>(terminal_active) << 1U)));
        if (terminal_active) {
            // A terminal in the same native call reads the committed signal,
            // never an earlier member's newly staged output slot.
            const auto terminal_value = PackedLogic4::from_word_planes(width,
                std::span(terminal_aval).first(slot.word_count),
                std::span(terminal_bval).first(slot.word_count));
            assert(terminal_value == rhs);
        }
        for (std::uint32_t bit = 0U; bit < width; ++bit) {
            assert(((terminal_mask[bit / 64U] >> (bit % 64U)) & 1U)
                == static_cast<std::uint64_t>(terminal_active));
        }
        assert(callbacks.writes.empty() && callbacks.scheduled_writes.empty());
    }
    // The mask extension is checked before any body or output mutation.
    const auto saved_slot = slot;
    const auto saved_terminal_slot = terminal_slot;
    const auto saved_terminal_aval = terminal_aval;
    const auto saved_terminal_bval = terminal_bval;
    const auto saved_terminal_mask = terminal_mask;
    runtime.fused_activation_word_count = 0U;
    bool rejected = false;
    try {
        auto result = new_resume_result();
        static_cast<void>(jit.resume(handle, runtime, frame, result));
    } catch (const LlvmJitError&) {
        rejected = true;
    }
    assert(rejected && slot.mask == saved_slot.mask && slot.aval == saved_slot.aval);
    assert(terminal_slot.mask == saved_terminal_slot.mask
        && terminal_slot.aval == saved_terminal_slot.aval
        && terminal_aval == saved_terminal_aval
        && terminal_bval == saved_terminal_bval
        && terminal_mask == saved_terminal_mask);
    runtime.fused_activation_word_count = (width + 1U + 63U) / 64U;
    runtime.struct_size = static_cast<std::uint32_t>(offsetof(fsim_jit_runtime_v1, fused_activation_words));
    rejected = false;
    try {
        auto result = new_resume_result();
        static_cast<void>(jit.resume(handle, runtime, frame, result));
    } catch (const LlvmJitError&) {
        rejected = true;
    }
    assert(rejected);
    auto malformed = make_masked_members(width);
    operation_get<Jump>(malformed[0].operations[8]).target = 0U;
    assert(!fuse_masked_processes(pointers(malformed), widths, kinds, 999U));
    malformed = make_masked_members(width);
    malformed[0].operations[9] = CopyRegister { 5U, 6U };
    assert(!fuse_masked_processes(pointers(malformed), widths, kinds, 999U));
    malformed = make_masked_members(width);
    operation_get<Branch>(malformed[0].operations[6]).when_false = 12U;
    assert(!fuse_masked_processes(pointers(malformed), widths, kinds, 999U));
}
} // namespace

void test_fused_masked_process_at_level(const JitOptimizationLevel optimization)
{
    for (const auto width : { 3U, 65U, 129U }) {
        check_masked_native(optimization, width);
    }
}
} // namespace fsim::tests::compiler

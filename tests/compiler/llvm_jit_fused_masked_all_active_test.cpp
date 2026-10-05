// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"
#include "fsim/compiler/fused_masked_process.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace fsim::tests::compiler {
namespace {

using namespace fsim::compiler;
using namespace fsim::runtime::simir;

constexpr std::array<ValueKind, 3> signal_kinds {
    ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
};

std::array<Process, 2> make_all_active_members(
    const std::uint32_t width,
    const bool repeated_direct_read = false)
{
    std::array<Process, 2> members;
    for (std::uint32_t index = 0U; index < members.size(); ++index) {
        auto& process = members[index];
        process.id = 17U + index;
        process.name = "mixed_static_member_" + std::to_string(index);
        process.initialize = true;
        process.register_count = 6U;
        process.static_sensitivity = {
            { 0U, EdgeKind::any }, { 1U, EdgeKind::any }
        };
    }
    members[0].driver_regions = { { 2U, 0U, width - 1U, false } };
    if (repeated_direct_read) {
        members[0].register_count = 9U;
        members[0].operations = {
            ReadSignal { 0U, 0U },
            CopyRegister { 6U, 0U },
            ReadSignal { 7U, 0U },
            Binary { BinaryOperator::bit_and, 8U, 6U, 7U },
            ReadSignal { 1U, 1U },
            Binary { BinaryOperator::bit_xor, 2U, 8U, 1U },
            Extract { 3U, 2U, 0U, width - 1U },
            WriteUpdateSlice { 2U, 3U, 0U },
            Binary { BinaryOperator::bit_or, 4U, 8U, 1U },
            Extract { 5U, 4U, 0U, width - 1U },
            WriteUpdateSlice { 2U, 5U, 0U },
            WaitSensitivity { },
            Jump { 0U },
        };
    } else {
        members[0].operations = {
            ReadSignal { 0U, 0U },
            ReadSignal { 1U, 1U },
            Binary { BinaryOperator::bit_xor, 2U, 0U, 1U },
            Extract { 3U, 2U, 0U, width - 1U },
            WriteUpdateSlice { 2U, 3U, 0U },
            Binary { BinaryOperator::bit_or, 4U, 0U, 1U },
            Extract { 5U, 4U, 0U, width - 1U },
            WriteUpdateSlice { 2U, 5U, 0U },
            WaitSensitivity { },
            Jump { 0U },
        };
    }
    members[1].register_count = 8U;
    members[1].driver_regions = { { 2U, width - 1U, 1U, false } };
    members[1].operations = {
        ReadSignal { 0U, 0U },
        ReadSignal { 1U, 1U },
        Binary { BinaryOperator::bit_and, 2U, 0U, 1U },
        Reduction { ReductionOperator::bit_xor, 3U, 2U },
        CopyRegister { 4U, 3U },
        LoadConstant { 5U, PackedLogic4(1U, Logic4::z) },
        Concatenate { 6U, { 4U, 5U }, 2U },
        Extract { 7U, 6U, 1U, 1U },
        WriteUpdateSlice { 2U, 7U, width - 1U },
        WaitSensitivity { },
        Jump { 0U },
    };
    return members;
}

struct NativeState {
    JitProcessHandle handle;
    std::vector<std::uint64_t> registers_aval;
    std::vector<std::uint64_t> registers_bval;
    std::vector<std::uint8_t> initialized;
    std::vector<SignalId> reads;
    fsim_jit_frame_v2 frame { };
    fsim_jit_resume_result_v2 result { };
    TestRuntime callbacks;
    fsim_jit_runtime_instance_v2 runtime { };
    std::array<std::uint64_t, 3> signal_aval { };
    std::array<std::uint64_t, 3> signal_bval { };
    std::array<std::uint64_t, 6> wide_aval { };
    std::array<std::uint64_t, 6> wide_bval { };
    std::array<std::uint32_t, 3> offsets { 0U, 2U, 4U };
    fsim_jit_update_slot_v2 slot { };
    std::array<std::uint64_t, 2> update_aval { };
    std::array<std::uint64_t, 2> update_bval { };
    std::array<std::uint64_t, 2> update_mask { };
    std::uint64_t active { };
    std::array<std::uint64_t, 1> fused_activation { UINT64_C(3) };
};

void prepare(NativeState& state, LlvmJit& jit, const Process& process,
    const std::array<std::uint32_t, 3>& widths,
    const bool tiered_read_dedup = false,
    const FusedMaskedProcess* masked_process = nullptr)
{
    if (masked_process != nullptr) {
        jit.add_masked_process(process.name, *masked_process, widths,
            signal_kinds, 2U, true, tiered_read_dedup);
    } else {
        jit.add_process(process.name, process, widths, signal_kinds);
    }
    state.handle = jit.lookup(process.name);
    assert(state.handle);
    const auto layout = jit.frame_layout(state.handle);
    assert((layout.direct_update_signals == std::vector<SignalId> { 2U }));
    state.reads = layout.direct_read_signals;
    state.registers_aval.resize(layout.register_word_count);
    state.registers_bval.resize(layout.register_word_count);
    state.initialized.resize(layout.register_count);
    jit.initialize_frame(state.handle, state.frame, state.registers_aval,
        state.registers_bval, state.initialized);
    state.runtime = abi(state.callbacks);
    auto& runtime = state.runtime;
    runtime.flags = 0U;
    runtime.direct_signal_aval = state.signal_aval.data();
    runtime.direct_signal_bval = state.signal_bval.data();
    runtime.direct_signal_count = 3U;
    runtime.direct_read_signals = state.reads.data();
    runtime.direct_read_signal_count = static_cast<std::uint32_t>(state.reads.size());
    runtime.direct_wide_signal_aval = state.wide_aval.data();
    runtime.direct_wide_signal_bval = state.wide_bval.data();
    runtime.direct_wide_signal_offsets = state.offsets.data();
    runtime.direct_wide_signal_offset_count = 3U;
    runtime.direct_wide_word_count = 6U;
    runtime.direct_update_slots = &state.slot;
    runtime.direct_update_slot_count = 1U;
    runtime.direct_update_active_words = &state.active;
    runtime.direct_update_active_word_count = 1U;
    if (masked_process != nullptr) {
        runtime.fused_activation_words = state.fused_activation.data();
        runtime.fused_activation_word_count = 1U;
    }
}

void set_input(NativeState& state, const SignalId signal,
    const PackedLogic4& value)
{
    const auto aval = value.aval_words();
    const auto bval = value.bval_words();
    std::ranges::copy(aval, state.wide_aval.begin() + state.offsets[signal]);
    std::ranges::copy(bval, state.wide_bval.begin() + state.offsets[signal]);
    state.signal_aval[signal] = aval.front();
    state.signal_bval[signal] = bval.front();
}

void resume(NativeState& state, const LlvmJit& jit, const std::uint32_t width)
{
    state.slot = { };
    state.update_aval.fill(0U);
    state.update_bval.fill(0U);
    state.update_mask.fill(0U);
    state.active = 0U;
    state.slot.width = width;
    state.slot.word_count = (width + 63U) / 64U;
    state.slot.wide_aval = state.update_aval.data();
    state.slot.wide_bval = state.update_bval.data();
    state.slot.wide_mask = state.update_mask.data();
    state.result = new_resume_result();
    assert(jit.resume(state.handle, state.runtime, state.frame, state.result)
        == JitResumeStatus::wait_sensitivity);
    assert(state.slot.active == 1U);
    assert(state.active == 1U);
    assert(state.callbacks.writes.empty());
    assert(state.callbacks.scheduled_writes.empty());
    if (width <= 64U) {
        state.update_aval[0] = state.slot.aval;
        state.update_bval[0] = state.slot.bval;
        state.update_mask[0] = state.slot.mask;
    }
}

void check_masked_validation(const std::uint32_t width)
{
    const std::array<std::uint32_t, 3> widths { width, width, width };
    const auto rejects = [&](const std::array<Process, 2>& members) {
        const std::array<const Process*, 2> pointers { &members[0], &members[1] };
        assert(!fuse_masked_processes(pointers, widths, signal_kinds, 99U));
    };
    auto members = make_all_active_members(width);
    members[1].driver_regions[0].offset = 0U;
    operation_get<WriteUpdateSlice>(members[1].operations[8]).offset = 0U;
    rejects(members);

    members = make_all_active_members(width);
    // Another member's ownership cannot justify this member's output.
    std::swap(members[0].driver_regions, members[1].driver_regions);
    rejects(members);

    members = make_all_active_members(width);
    members[0].operations[0] = CopyRegister { 0U, 5U };
    rejects(members);

    members = make_all_active_members(width);
    operation_get<Binary>(members[0].operations[2]).operation = BinaryOperator::add_unsigned;
    rejects(members);

    members = make_all_active_members(width);
    members[1].static_sensitivity.pop_back();
    const std::array<const Process*, 2> differing_sensitivity_pointers {
        &members[0], &members[1]
    };
    const auto differently_sensitized = fuse_masked_processes(
        differing_sensitivity_pointers, widths, signal_kinds, 99U);
    assert(differently_sensitized
        && differently_sensitized->gates.size() == 2U
        && differently_sensitized->gates[0].activation_bit == 0U
        && differently_sensitized->gates[1].activation_bit == 1U);
}

void check_masked_all_active_native(const JitOptimizationLevel optimization,
    const std::uint32_t width)
{
    const bool tiered_read_dedup = width <= 64U;
    const auto members = make_all_active_members(width, tiered_read_dedup);
    const std::array<const Process*, 2> pointers { &members[0], &members[1] };
    const std::array<std::uint32_t, 3> widths { width, width, width };
    const auto fused = fuse_masked_processes(pointers, widths, signal_kinds, 99U);
    assert(fused);
    assert(fused->gates.size() == 2U && fused->writes.size() == 2U);
    assert(fused->gates[0].original_id == 17U
        && fused->gates[0].activation_bit == 0U);
    assert(fused->gates[1].original_id == 18U
        && fused->gates[1].activation_bit == 1U);
    assert(fused->writes[0].original_id == 17U
        && fused->writes[1].original_id == 18U);
    assert(!fused->process.initialize);
    assert(fused->process.register_count
        == (tiered_read_dedup ? 17U : 14U));
    LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    LlvmJit jit { options };
    std::array<NativeState, 3> native;
    prepare(native[0], jit, members[0], widths);
    prepare(native[1], jit, members[1], widths);
    prepare(native[2], jit, fused->process, widths, tiered_read_dedup, &*fused);
    constexpr std::array<Logic4, 4> values {
        Logic4::zero, Logic4::one, Logic4::x, Logic4::z
    };
    for (std::size_t row = 0U; row < 32U; ++row) {
        PackedLogic4 lhs(width, values[(row / 4U) % 4U]);
        PackedLogic4 rhs(width, values[row % 4U]);
        if (row >= 16U) {
            for (std::uint32_t bit = 0U; bit < width; ++bit) {
                lhs.set(bit, values[(bit + row / 4U) % 4U]);
                rhs.set(bit, values[(bit + row) % 4U]);
            }
        }
        for (auto& state : native) {
            set_input(state, 0U, lhs);
            set_input(state, 1U, rhs);
            // Frames persist across activations: exercise the final backedge.
            resume(state, jit, width);
        }
        const auto words = (width + 63U) / 64U;
        for (std::size_t word = 0U; word < words; ++word) {
            const auto first_mask = native[0].update_mask[word];
            const auto second_mask = native[1].update_mask[word];
            assert((first_mask & second_mask) == 0U);
            const auto mask = first_mask | second_mask;
            assert(native[2].update_mask[word] == mask);
            const auto aval = (native[0].update_aval[word] & first_mask)
                | (native[1].update_aval[word] & second_mask);
            const auto bval = (native[0].update_bval[word] & first_mask)
                | (native[1].update_bval[word] & second_mask);
            assert((native[2].update_aval[word] & mask) == aval);
            assert((native[2].update_bval[word] & mask) == bval);
        }
        if (width == 65U) {
            assert(native[2].update_mask[0] == UINT64_MAX);
            assert(native[2].update_mask[1] == 1U);
        }
    }
}

} // namespace

void test_fused_masked_all_active_process_at_level(
    const JitOptimizationLevel optimization)
{
    for (const auto width : { 3U, 9U, 65U }) {
        check_masked_validation(width);
        check_masked_all_active_native(optimization, width);
    }
}

} // namespace fsim::tests::compiler

// SPDX-License-Identifier: Apache-2.0
#if defined(FSIM_HAS_LLVM)
#include "../../src/app/application_fused_static_executor.hpp"
#include "fsim/compiler/fused_masked_process.hpp"
#include "fsim/compiler/fused_static_process.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace fsim;
using namespace fsim::runtime;
using namespace fsim::runtime::simir;
Process make_member(const ProcessId id, const std::uint32_t width)
{
    Process process;
    process.id = id;
    process.name = "mixed_member_" + std::to_string(id);
    process.register_count = 4U;
    process.static_sensitivity = { { 0U, EdgeKind::any }, { 1U, EdgeKind::any } };
    const auto output = 2U + id / 2U;
    const auto offset = id % 2U == 0U ? 0U : width - 1U;
    const auto count = id % 2U == 0U ? width - 1U : 1U;
    constexpr std::array<BinaryOperator, 4> operators {
        BinaryOperator::bit_xor, BinaryOperator::bit_and,
        BinaryOperator::bit_or, BinaryOperator::bit_xor
    };
    process.driver_regions = { { output, offset, count, false } };
    process.operations = {
        ReadSignal { 0U, 0U }, ReadSignal { 1U, 1U },
        Binary { operators[id], 2U, 0U, 1U },
        Extract { 3U, 2U, offset, count },
        WriteUpdateSlice { output, 3U, offset },
        WaitSensitivity { }, Jump { 0U }
    };
    return process;
}

void check_at_level(const compiler::JitOptimizationLevel optimization,
    const std::uint32_t width)
{
    std::array<Process, 4> members;
    std::array<const Process*, 4> pointers;
    for (ProcessId id = 0U; id < members.size(); ++id) {
        members[id] = make_member(id, width);
        pointers[id] = &members[id];
    }
    const std::array<std::uint32_t, 4> widths { width, width, width, width };
    const std::array<ValueKind, 4> kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    const auto fused = compiler::fuse_static_processes(pointers, widths, kinds, 0U);
    assert(fused);
    compiler::LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    compiler::LlvmJit jit { options };
    jit.add_process("shared_fused", fused->process, widths, kinds);
    const auto handle = jit.lookup("shared_fused");
    assert(handle);
    constexpr std::array<std::array<SignalId, 4>, 2> mappings {
        std::array<SignalId, 4> { 5U, 1U, 7U, 2U },
        std::array<SignalId, 4> { 6U, 3U, 0U, 4U }
    };
    std::array<std::unique_ptr<FusedStaticCohortExecutor>, 2> executors;
    for (std::size_t index = 0U; index < executors.size(); ++index) {
        const std::array<SignalId, 2> output_order {
            mappings[index][3], mappings[index][2]
        };
        executors[index] = app::make_fused_static_executor(
            jit, handle, mappings[index], widths, output_order);
    }
    std::array<std::uint64_t, 8> scalar_aval { };
    std::array<std::uint64_t, 8> scalar_bval { };
    const auto word_count = (width + 63U) / 64U;
    std::vector<std::uint64_t> wide_aval(8U * word_count);
    std::vector<std::uint64_t> wide_bval(8U * word_count);
    std::array<std::uint32_t, 8> offsets {};
    for (std::size_t index = 0U; index < offsets.size(); ++index) {
        offsets[index] = static_cast<std::uint32_t>(index) * word_count;
    }
    ProcessCohortNativeContext context {
        &scalar_aval, scalar_aval, scalar_bval, 0U, true, false,
        wide_aval, wide_bval, offsets, {}, {}, {}, {}, {}, {}
    };
    const auto set_signal = [&](const SignalId signal, const PackedLogic4& value) {
        scalar_aval[signal] = value.aval_words().front();
        scalar_bval[signal] = value.bval_words().front();
        std::ranges::copy(value.aval_words(), wide_aval.begin() + offsets[signal]);
        std::ranges::copy(value.bval_words(), wide_bval.begin() + offsets[signal]);
    };
    constexpr std::array<Logic4, 4> values {
        Logic4::zero, Logic4::one, Logic4::x, Logic4::z
    };
    for (std::size_t row = 0U; row < 32U; ++row) {
        for (std::size_t instance = 0U; instance < executors.size(); ++instance) {
            auto lhs = PackedLogic4(width, Logic4::zero);
            auto rhs = PackedLogic4(width, Logic4::zero);
            for (std::uint32_t bit = 0U; bit < width; ++bit) {
                const auto offset = row >= 16U ? bit : 0U;
                lhs.set(bit, values[(row / 4U + instance + offset) % 4U]);
                rhs.set(bit, values[(row + 2U * instance + offset) % 4U]);
            }
            set_signal(mappings[instance][0], lhs);
            set_signal(mappings[instance][1], rhs);
            if (row == 3U) {
                auto observed = context;
                observed.execution_points_enabled = true;
                assert(!executors[instance]->resume(observed));
            }
            const auto slots = executors[instance]->resume(context);
            assert(slots && slots->aggregate_slots.size() == 2U
                && slots->projected_writes.empty());
            for (std::size_t index = 0U;
                 index < slots->aggregate_slots.size(); ++index) {
                const auto canonical = 3U - index;
                const auto& slot = slots->aggregate_slots[index];
                assert(slot.signal == mappings[instance][canonical]);
                assert(slot.width == width && slot.word_count == word_count);
                assert(*slot.active != 0U);
                for (std::size_t word = 0U; word + 1U < word_count; ++word) {
                    assert(slot.mask[word] == UINT64_MAX);
                }
                assert(slot.mask[word_count - 1U] == 1U);
                auto expected = binary_value(canonical == 2U
                        ? BinaryOperator::bit_xor : BinaryOperator::bit_or,
                    lhs, rhs);
                expected.set(width - 1U, binary_value(canonical == 2U
                            ? BinaryOperator::bit_and : BinaryOperator::bit_xor,
                    lhs, rhs).get(width - 1U));
                const auto actual = PackedLogic4::from_word_planes(width,
                    std::span(slot.aval, slot.word_count),
                    std::span(slot.bval, slot.word_count));
                assert(actual == expected);
            }
        }
    }
}

void check_typed_context(const compiler::JitOptimizationLevel optimization,
    const std::uint32_t input_width, const std::uint32_t output_width,
    const bool masked = false)
{
    std::array<Process, 2> members;
    std::array<const Process*, 2> pointers;
    for (ProcessId id = 0U; id < members.size(); ++id) {
        auto& process = members[id];
        process.id = id;
        process.name = "typed_member_" + std::to_string(id);
        process.register_count = 2U;
        process.register_value_kinds = { ValueKind::logic9, ValueKind::logic9 };
        process.static_sensitivity = { { 0U, EdgeKind::any } };
        process.driver_regions = { { id + 1U, 0U, output_width, true } };
        process.operations = {
            ReadSignal { 0U, 0U },
            Extract { 1U, 0U, id == 0U ? input_width - output_width : 0U, output_width },
            WriteProjected { id + 1U, 1U }, WaitSensitivity {}, Jump { 0U }
        };
        pointers[id] = &process;
    }
    const std::array<std::uint32_t, 3> widths { input_width, output_width, output_width };
    const std::array<ValueKind, 3> kinds { ValueKind::logic9, ValueKind::logic9, ValueKind::logic9 };
    const auto fused = compiler::fuse_static_processes(pointers, widths, kinds, 0U);
    assert(fused);
    const auto masked_fused = compiler::fuse_masked_processes(pointers, widths, kinds, 0U);
    assert(masked_fused);
    compiler::LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    compiler::LlvmJit jit { options };
    if (masked) {
        jit.add_masked_process("typed_fused", *masked_fused, widths, kinds);
    } else {
        jit.add_process("typed_fused", fused->process, widths, kinds);
    }
    const auto handle = jit.lookup("typed_fused");
    assert(handle);
    const std::array<SignalId, 3> mapping { 2U, 0U, 1U };
    const std::array<SignalId, 2> output_order { 0U, 1U };
    auto executor = masked ? std::unique_ptr<FusedStaticCohortExecutor> { }
        : app::make_fused_static_executor(jit, handle, mapping, widths, output_order, kinds, true);
    auto masked_executor = masked ? app::make_fused_masked_region_executor(
        jit, handle, mapping, widths, output_order, kinds, true)
        : std::unique_ptr<FusedMaskedRegionExecutor> { };
    std::uint64_t active_members = 3U;
    const auto resume = [&](const ProcessCohortNativeContext& current) {
        return masked ? masked_executor->resume(current, std::span(&active_members, 1U))
                      : executor->resume(current);
    };
    // Logic9 storage ends at signal 2; signals 3 and 4 are ordinary Logic4.
    std::array<std::uint64_t, 5> aval {};
    std::array<std::uint64_t, 5> bval {};
    std::array<std::array<std::uint64_t, 3>, 4> narrow_planes {};
    std::array<std::vector<std::uint64_t>, 4> wide_planes;
    for (auto& plane : wide_planes) {
        plane.resize(2U + (input_width + 63U) / 64U);
    }
    const std::array<std::uint32_t, 5> offsets { 0U, 0U, 2U, 0U, 0U };
    ProcessCohortNativeContext context {
        &aval, aval, bval, 0U, true, false,
        wide_planes[0], wide_planes[1], offsets,
        narrow_planes[0], narrow_planes[1], narrow_planes[2], narrow_planes[3],
        wide_planes[2], wide_planes[3]
    };
    const std::string states = "UX01ZWLH-";
    for (std::size_t row = 0U; row < states.size(); ++row) {
        std::string bits(input_width, 'U');
        for (std::size_t bit = 0U; bit < bits.size(); ++bit) {
            bits[bit] = states[(row + bit) % states.size()];
        }
        const auto value = PackedLogic4::from_logic9_msb_string(bits);
        for (std::size_t plane = 0U; plane < narrow_planes.size(); ++plane) {
            const auto words = value.logic9_plane_words(plane);
            narrow_planes[plane][2] = words.front();
            std::ranges::copy(words, wide_planes[plane].begin() + 2U);
        }
        if (row == 0U) {
            auto short_context = context;
            if (input_width <= 64U) {
                short_context.signal_logic9_plane3 = std::span(narrow_planes[3]).first(2U);
                assert(!resume(short_context));
            } else {
                short_context = context;
                short_context.wide_signal_logic9_plane2
                    = std::span(wide_planes[2]).first(wide_planes[2].size() - 1U);
                assert(!resume(short_context));
                short_context = context;
                short_context.wide_signal_logic9_plane3
                    = std::span(wide_planes[3]).first(wide_planes[3].size() - 1U);
                assert(!resume(short_context));
            }
        }
        for (const auto mask : { UINT64_C(3), UINT64_C(1), UINT64_C(0), UINT64_C(2) }) {
            if (!masked && mask != 3U) {
                continue;
            }
            active_members = mask;
            const auto result = resume(context);
            assert(result && result->aggregate_slots.empty());
            std::size_t expected_index { };
            for (SignalId member = 0U; member < 2U; ++member) {
                if ((mask & (UINT64_C(1) << member)) == 0U) {
                    continue;
                }
                assert(expected_index < result->projected_writes.size());
                const auto& write = result->projected_writes[expected_index++];
                assert(write.signal == member);
                assert(write.value.to_msb_string() == (member == 0U
                    ? bits.substr(0U, output_width) : bits.substr(bits.size() - output_width)));
            }
            assert(result->projected_writes.size() == expected_index);
        }
    }
}

} // namespace

void test_fused_static_executor_bindings()
{
    for (const auto optimization : { compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        check_at_level(optimization, 65U);
        check_at_level(optimization, 129U);
        check_typed_context(optimization, 9U, 9U);
        check_typed_context(optimization, 65U, 9U);
        check_typed_context(optimization, 65U, 65U);
        check_typed_context(optimization, 129U, 129U);
        check_typed_context(optimization, 9U, 9U, true);
        check_typed_context(optimization, 65U, 9U, true);
        check_typed_context(optimization, 65U, 65U, true);
        check_typed_context(optimization, 129U, 129U, true);
    }
}
#endif

// SPDX-License-Identifier: Apache-2.0
#if defined(FSIM_HAS_LLVM)
#include "../../src/app/application_fused_static_executor.hpp"
#include "../../src/app/application_fused_read_capability.hpp"
#include "fsim/compiler/fused_masked_process.hpp"

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

void check_fused_read_lowering_capability()
{
    Process process;
    process.operations = { ReadSignal { 0U, 0U } };
    const std::array<std::uint32_t, 1> narrow_widths { 32U };
    const std::array<std::uint32_t, 4> wide_widths {
        65U, 129U, 256U, 1024U
    };
    const std::array<ValueKind, 1> logic4_kinds { ValueKind::logic4 };
    const std::array<ValueKind, 1> logic9_kinds { ValueKind::logic9 };

    const auto narrow_logic4 = app::fused_detail::read_lowering_capability(
        process, narrow_widths, logic4_kinds);
    assert(narrow_logic4.require_direct_read_signals);
    assert(narrow_logic4.tiered_read_dedup_safe);

    const auto narrow_logic9 = app::fused_detail::read_lowering_capability(
        process, narrow_widths, logic9_kinds);
    assert(narrow_logic9.require_direct_read_signals);
    assert(!narrow_logic9.tiered_read_dedup_safe);
    for (const auto width : wide_widths) {
        const std::array<std::uint32_t, 1> signal_width { width };
        const auto wide_logic4 = app::fused_detail::read_lowering_capability(
            process, signal_width, logic4_kinds);
        assert(wide_logic4.require_direct_read_signals);
        assert(!wide_logic4.tiered_read_dedup_safe);

        const auto wide_logic9 = app::fused_detail::read_lowering_capability(
            process, signal_width, logic9_kinds);
        assert(wide_logic9.require_direct_read_signals);
        assert(!wide_logic9.tiered_read_dedup_safe);
    }

    process.operations = {
        ReadSignal { 0U, 0U, SignalReadKind::sampled }
    };
    const auto sampled_logic4 = app::fused_detail::read_lowering_capability(
        process, wide_widths, logic4_kinds);
    assert(!sampled_logic4.require_direct_read_signals);
    assert(!sampled_logic4.tiered_read_dedup_safe);
}

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
    const std::uint32_t width, const bool strict_reads = false)
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
    const auto fused = compiler::fuse_masked_processes(pointers, widths, kinds, 0U);
    assert(fused);
    compiler::LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    compiler::LlvmJit jit { options };
    jit.add_masked_process("shared_fused", *fused, widths, kinds,
        2U, strict_reads);
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
            jit, handle, mappings[index], widths, output_order, kinds, false,
            strict_reads, fused->gates.size());
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
            // Switching valid plane addresses revokes and rebinds the lease.
            // A short context must decline even after a prior valid activation.
            auto alternate_aval = scalar_aval;
            auto alternate_bval = scalar_bval;
            auto current = context;
            std::vector<std::uint64_t> remapped_wide_aval;
            std::vector<std::uint64_t> remapped_wide_bval;
            std::array<std::uint32_t, 8> remapped_offsets {};
            if (row == 4U) {
                auto short_context = context;
                short_context.signal_aval = std::span(scalar_aval).first(1U);
                short_context.signal_bval = std::span(scalar_bval).first(1U);
                assert(!executors[instance]->resume(short_context));
                current.signal_aval = alternate_aval;
                current.signal_bval = alternate_bval;
                if (strict_reads && width > 64U) {
                    // Relocate every physical wide slice, then invalidate the
                    // prior lease once with an out-of-range map and twice
                    // with incomplete value-plane backing.
                    remapped_wide_aval.resize(9U * word_count);
                    remapped_wide_bval.resize(9U * word_count);
                    for (std::size_t signal = 0U;
                         signal < offsets.size(); ++signal) {
                        remapped_offsets[signal] = static_cast<std::uint32_t>(
                            offsets[signal] + word_count);
                        const auto source_offset = offsets[signal];
                        const auto destination_offset
                            = remapped_offsets[signal];
                        std::copy_n(
                            wide_aval.begin() + source_offset, word_count,
                            remapped_wide_aval.begin() + destination_offset);
                        std::copy_n(
                            wide_bval.begin() + source_offset, word_count,
                            remapped_wide_bval.begin() + destination_offset);
                    }
                    current.wide_signal_aval
                        = std::span<const std::uint64_t>(remapped_wide_aval);
                    current.wide_signal_bval
                        = std::span<const std::uint64_t>(remapped_wide_bval);
                    current.wide_signal_offsets
                        = std::span<const std::uint32_t>(remapped_offsets);

                    auto bad_offsets = remapped_offsets;
                    bad_offsets[mappings[instance][0]]
                        = static_cast<std::uint32_t>(
                            remapped_wide_aval.size());
                    auto bad_map_context = current;
                    bad_map_context.wide_signal_offsets = bad_offsets;
                    assert(!executors[instance]->resume(bad_map_context));

                    auto short_wide_context = current;
                    short_wide_context.wide_signal_aval
                        = std::span(remapped_wide_aval).first(
                            remapped_wide_aval.size() - 1U);
                    assert(!executors[instance]->resume(short_wide_context));
                    short_wide_context = current;
                    short_wide_context.wide_signal_bval
                        = std::span(remapped_wide_bval).first(
                            remapped_wide_bval.size() - 1U);
                    assert(!executors[instance]->resume(short_wide_context));

                    const auto first_input_signal
                        = mappings[instance][0];
                    const auto first_input_end
                        = static_cast<std::size_t>(
                            remapped_offsets[first_input_signal])
                            + word_count;
                    // Equal A/B span lengths still omit the final word of
                    // the first mapped input's actual range.
                    auto equal_short_context = current;
                    equal_short_context.wide_signal_aval
                        = std::span<const std::uint64_t>(remapped_wide_aval)
                            .first(first_input_end - 1U);
                    equal_short_context.wide_signal_bval
                        = std::span<const std::uint64_t>(remapped_wide_bval)
                            .first(first_input_end - 1U);
                    assert(equal_short_context.wide_signal_aval.size()
                        == equal_short_context.wide_signal_bval.size());
                    assert(equal_short_context.wide_signal_aval.size() + 1U
                        == first_input_end);
                    assert(!executors[instance]->resume(equal_short_context));
                }
            }
            const auto slots = executors[instance]->resume(current);
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
                const auto tail_bits = width % 64U;
                const auto tail_mask = tail_bits == 0U ? UINT64_MAX
                    : (UINT64_C(1) << tail_bits) - 1U;
                assert(slot.mask[word_count - 1U] == tail_mask);
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
    const bool strict_reads = false)
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
    const auto fused = compiler::fuse_masked_processes(pointers, widths, kinds, 0U);
    assert(fused);
    compiler::LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    compiler::LlvmJit jit { options };
    jit.add_masked_process("typed_fused", *fused, widths, kinds,
        1U, strict_reads);
    const auto handle = jit.lookup("typed_fused");
    assert(handle);
    const std::array<SignalId, 3> mapping { 2U, 0U, 1U };
    const std::array<SignalId, 2> output_order { 0U, 1U };
    auto executor = app::make_fused_static_executor(jit, handle, mapping,
        widths, output_order, kinds, true, strict_reads,
        fused->gates.size());
    std::uint64_t active_members = 3U;
    const auto resume_masked = [&](const ProcessCohortNativeContext& current) {
        return executor->resume_masked_all_active(
            current, std::span(&active_members, 1U));
    };
    const auto check_projected_writes = [&](
        const FusedStaticCohortResume& result,
        const std::uint64_t mask,
        const std::string& bits) {
        assert(result.aggregate_slots.empty());
        std::size_t expected_index { };
        for (SignalId member = 0U; member < 2U; ++member) {
            if ((mask & (UINT64_C(1) << member)) == 0U) {
                continue;
            }
            assert(expected_index < result.projected_writes.size());
            const auto& write = result.projected_writes[expected_index++];
            assert(write.signal == member);
            assert(write.value.to_msb_string() == (member == 0U
                ? bits.substr(0U, output_width)
                : bits.substr(bits.size() - output_width)));
        }
        assert(result.projected_writes.size() == expected_index);
    };
    const auto reject_both_entries = [&](
        const ProcessCohortNativeContext& invalid) {
        assert(!executor->resume(invalid));
        assert(!resume_masked(invalid));
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
        if (row == 0U || row == 3U) {
            auto short_context = context;
            if (input_width <= 64U) {
                short_context.signal_logic9_plane3 = std::span(narrow_planes[3]).first(2U);
                reject_both_entries(short_context);
            } else {
                short_context = context;
                short_context.wide_signal_logic9_plane2
                    = std::span(wide_planes[2]).first(wide_planes[2].size() - 1U);
                reject_both_entries(short_context);
                short_context = context;
                short_context.wide_signal_logic9_plane3
                    = std::span(wide_planes[3]).first(wide_planes[3].size() - 1U);
                reject_both_entries(short_context);
            }
        }
        // Keep sparse member-gate coverage on the retained static executor API.
        for (const auto mask : { UINT64_C(3), UINT64_C(1), UINT64_C(0), UINT64_C(2) }) {
            active_members = mask;
            auto result = std::optional<FusedStaticCohortResume> { };
            if (mask == UINT64_C(3)) {
                const auto ordinary_result = executor->resume(context);
                assert(ordinary_result);
                check_projected_writes(*ordinary_result, mask, bits);
                const std::vector<FusedStaticProjectedWrite> ordinary_writes(
                    ordinary_result->projected_writes.begin(),
                    ordinary_result->projected_writes.end());

                result = resume_masked(context);
                assert(result
                    && result->projected_writes.size() == ordinary_writes.size());
                check_projected_writes(*result, mask, bits);
                for (std::size_t index = 0U;
                     index < ordinary_writes.size(); ++index) {
                    assert(ordinary_writes[index].signal
                        == result->projected_writes[index].signal);
                    assert(ordinary_writes[index].value
                        == result->projected_writes[index].value);
                }
            } else {
                result = resume_masked(context);
                assert(result);
                check_projected_writes(*result, mask, bits);
            }
            if (row == 0U && mask == UINT64_C(3)
                && strict_reads && input_width > 64U) {
                // First bind successfully above, then prove all wide Logic9
                // planes and the physical offset map are revalidated.
                auto short_plane_context = context;
                short_plane_context.wide_signal_logic9_plane2
                    = std::span(wide_planes[2]).first(
                        wide_planes[2].size() - 1U);
                reject_both_entries(short_plane_context);
                short_plane_context = context;
                short_plane_context.wide_signal_logic9_plane3
                    = std::span(wide_planes[3]).first(
                        wide_planes[3].size() - 1U);
                reject_both_entries(short_plane_context);

                auto bad_offsets = offsets;
                bad_offsets[mapping[0]] = static_cast<std::uint32_t>(
                    wide_planes[0].size());
                auto bad_map_context = context;
                bad_map_context.wide_signal_offsets = bad_offsets;
                reject_both_entries(bad_map_context);

                const auto word_count = (input_width + 63U) / 64U;
                std::array<std::vector<std::uint64_t>, 4> remapped_planes;
                for (std::size_t plane = 0U; plane < remapped_planes.size();
                     ++plane) {
                    remapped_planes[plane].resize(3U + word_count);
                    std::copy_n(wide_planes[plane].begin() + 2U,
                        word_count, remapped_planes[plane].begin() + 3U);
                }
                auto remapped_offsets = offsets;
                remapped_offsets[mapping[0]] = 3U;
                auto remapped_context = context;
                remapped_context.wide_signal_aval
                    = std::span<const std::uint64_t>(remapped_planes[0]);
                remapped_context.wide_signal_bval
                    = std::span<const std::uint64_t>(remapped_planes[1]);
                remapped_context.wide_signal_offsets
                    = std::span<const std::uint32_t>(remapped_offsets);
                remapped_context.wide_signal_logic9_plane2
                    = std::span<const std::uint64_t>(remapped_planes[2]);
                remapped_context.wide_signal_logic9_plane3
                    = std::span<const std::uint64_t>(remapped_planes[3]);
                const auto remapped_result = executor->resume(remapped_context);
                assert(remapped_result
                    && remapped_result->projected_writes.size() == 2U);
                check_projected_writes(*remapped_result, UINT64_C(3), bits);
                const std::vector<FusedStaticProjectedWrite> ordinary_writes(
                    remapped_result->projected_writes.begin(),
                    remapped_result->projected_writes.end());
                const auto remapped_masked_result
                    = resume_masked(remapped_context);
                assert(remapped_masked_result
                    && remapped_masked_result->projected_writes.size() == 2U);
                check_projected_writes(*remapped_masked_result,
                    UINT64_C(3), bits);
                for (std::size_t member = 0U; member < 2U; ++member) {
                    assert(remapped_masked_result->projected_writes[member].signal
                        == ordinary_writes[member].signal);
                    assert(remapped_masked_result->projected_writes[member].value
                        == ordinary_writes[member].value);
                }
            }
        }
    }
}

} // namespace

void test_fused_static_executor_bindings()
{
    check_fused_read_lowering_capability();
    for (const auto optimization : { compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        check_at_level(optimization, 33U, true);
        check_at_level(optimization, 65U, true);
        check_at_level(optimization, 129U, true);
        check_at_level(optimization, 256U, true);
        check_at_level(optimization, 1024U, true);
        check_at_level(optimization, 65U);
        check_at_level(optimization, 129U);
        check_typed_context(optimization, 9U, 9U, true);
        check_typed_context(optimization, 65U, 9U, true);
        check_typed_context(optimization, 129U, 129U, true);
        check_typed_context(optimization, 256U, 9U, true);
        check_typed_context(optimization, 1024U, 9U, true);
        check_typed_context(optimization, 9U, 9U);
        check_typed_context(optimization, 65U, 9U);
        check_typed_context(optimization, 65U, 65U);
        check_typed_context(optimization, 129U, 129U);
    }
}
#endif

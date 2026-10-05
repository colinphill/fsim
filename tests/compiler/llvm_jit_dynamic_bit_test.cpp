// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace fsim::tests::compiler {

namespace {

using namespace llvm_jit_test_detail;
using namespace fsim::runtime::simir;

constexpr std::array<Logic4, 4> logic4_states {
    Logic4::zero, Logic4::one, Logic4::x, Logic4::z};
constexpr std::string_view logic9_states = "01UXZWLH-";

[[nodiscard]] PackedLogic4 make_logic4_pattern(
    const std::uint32_t width,
    const std::size_t rotation)
{
    PackedLogic4 value(width, Logic4::zero);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        value.set(bit, logic4_states[
            (static_cast<std::size_t>(bit) + rotation)
            % logic4_states.size()]);
    }
    return value;
}

[[nodiscard]] PackedLogic4 make_logic9_pattern(
    const std::uint32_t width,
    const std::size_t rotation)
{
    std::string msb;
    msb.reserve(width);
    for (std::uint32_t bit = width; bit > 0U; --bit) {
        msb.push_back(logic9_states[
            (static_cast<std::size_t>(bit - 1U) + rotation)
            % logic9_states.size()]);
    }
    return PackedLogic4::from_logic9_msb_string(msb);
}

[[nodiscard]] PackedLogic4 make_logic9_bit(const char state)
{
    return PackedLogic4::from_logic9_msb_string(std::string(1U, state));
}

void set_runtime_input(
    TestRuntime& runtime,
    const std::uint32_t signal,
    const PackedLogic4& value)
{
    if (value.width() > 64U) {
        runtime.wide_signal_aval[signal].assign(
            value.aval_words().begin(), value.aval_words().end());
        runtime.wide_signal_bval[signal].assign(
            value.bval_words().begin(), value.bval_words().end());
        if (value.is_logic9()) {
            const auto plane2 = value.logic9_plane_words(2U);
            const auto plane3 = value.logic9_plane_words(3U);
            runtime.wide_signal_logic9_plane2[signal].assign(
                plane2.begin(), plane2.end());
            runtime.wide_signal_logic9_plane3[signal].assign(
                plane3.begin(), plane3.end());
        } else {
            runtime.wide_signal_logic9_plane2[signal].clear();
            runtime.wide_signal_logic9_plane3[signal].clear();
        }
        return;
    }
    if (value.is_logic9()) {
        runtime.logic9_signals[signal] = value.logic9_low_word().planes;
    } else {
        runtime.signals[signal] = encode(value);
    }
}

[[nodiscard]] PackedLogic4 read_runtime_value(
    const TestRuntime& runtime,
    const std::uint32_t signal,
    const std::uint32_t width,
    const ValueKind kind)
{
    if (width > 64U) {
        if (kind == ValueKind::logic9) {
            return PackedLogic4::from_logic9_word_planes(
                width,
                runtime.wide_signal_aval[signal],
                runtime.wide_signal_bval[signal],
                runtime.wide_signal_logic9_plane2[signal],
                runtime.wide_signal_logic9_plane3[signal]);
        }
        return PackedLogic4::from_word_planes(
            width,
            runtime.wide_signal_aval[signal],
            runtime.wide_signal_bval[signal]);
    }
    if (kind == ValueKind::logic9) {
        const auto& planes = runtime.logic9_signals[signal];
        const std::array<std::uint64_t, 1> plane0 { planes[0] };
        const std::array<std::uint64_t, 1> plane1 { planes[1] };
        const std::array<std::uint64_t, 1> plane2 { planes[2] };
        const std::array<std::uint64_t, 1> plane3 { planes[3] };
        return PackedLogic4::from_logic9_word_planes(
            width, plane0, plane1, plane2, plane3);
    }
    return PackedLogic4::from_aval_bval(
        width,
        runtime.signals[signal].aval,
        runtime.signals[signal].bval);
}

[[nodiscard]] PackedLogic4 expected_extract(
    const PackedLogic4& source,
    const PackedLogic4& index,
    const DynamicIndex& selection)
{
    try {
        return source.extract_bits(
            dynamic_index_offset(index, selection), 1U);
    } catch (const std::invalid_argument&) {
        auto invalid = PackedLogic4(1U, Logic4::x);
        if (source.is_logic9()) {
            invalid = invalid.promoted_to_logic9();
        }
        return invalid;
    }
}

[[nodiscard]] bool index_is_valid(
    const PackedLogic4& index,
    const DynamicIndex& selection)
{
    try {
        static_cast<void>(dynamic_index_offset(index, selection));
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

[[nodiscard]] PackedLogic4 expected_insert(
    const PackedLogic4& target,
    const PackedLogic4& source,
    const PackedLogic4& index,
    const DynamicIndex& selection)
{
    auto result = target;
    try {
        result.insert_bits(
            source.extract_bits(0U, 1U),
            dynamic_index_offset(index, selection));
    } catch (const std::invalid_argument&) {
    }
    return result;
}

struct RuntimeIndex {
    std::int32_t value { };
    std::uint32_t bval { };
};

[[nodiscard]] PackedLogic4 pack_index(const RuntimeIndex index)
{
    return PackedLogic4::from_aval_bval(
        32U, static_cast<std::uint32_t>(index.value), index.bval);
}

[[nodiscard]] std::vector<RuntimeIndex> make_index_cases(
    const std::uint32_t extent,
    const std::uint32_t base_offset,
    const bool negative_range)
{
    std::vector<RuntimeIndex> result;
    const auto lower = negative_range
        ? -static_cast<std::int64_t>(extent - 1U)
        : std::int64_t { 0 };
    const auto upper = negative_range
        ? std::int64_t { 0 }
        : static_cast<std::int64_t>(extent - 1U);
    const auto append_if_in_range = [&](const std::int64_t index) {
        if (index >= lower && index <= upper) {
            result.push_back({ static_cast<std::int32_t>(index), 0U });
        }
    };
    append_if_in_range(lower);
    append_if_in_range(upper);
    append_if_in_range(lower + 1);
    append_if_in_range(upper - 1);
    for (std::uint64_t boundary = 64U;
         boundary < static_cast<std::uint64_t>(extent) + base_offset;
         boundary += 64U) {
        const auto packed_offset = static_cast<std::int64_t>(
            boundary - base_offset);
        append_if_in_range(negative_range
            ? -packed_offset : packed_offset);
        if (packed_offset > 0) {
            append_if_in_range(negative_range
                ? -(packed_offset - 1)
                : packed_offset - 1);
        }
    }
    if (negative_range) {
        append_if_in_range(-1);
        result.push_back({ 1, 0U });
        result.push_back({ -static_cast<std::int32_t>(extent), 0U });
    } else {
        append_if_in_range(1);
        result.push_back({ -1, 0U });
        result.push_back({ static_cast<std::int32_t>(extent), 0U });
    }
    result.push_back({ 0, UINT32_C(1) });
    result.push_back({ 1, UINT32_C(1) });
    return result;
}

[[nodiscard]] Process make_runtime_input_process(
    const std::uint32_t extent,
    const std::uint32_t base_offset,
    const bool negative_range)
{
    const auto left = negative_range
        ? -static_cast<std::int64_t>(extent - 1U)
        : static_cast<std::int64_t>(extent - 1U);
    const auto right = std::int64_t { 0 };
    const DynamicIndex selection {
        0U, left, right, base_offset, false };

    Process process;
    process.id = 300U + extent * 8U + base_offset * 2U
        + static_cast<std::uint32_t>(negative_range);
    process.name = "wide_dynamic_single_bit_runtime_input";
    process.register_count = 15U;
    process.register_value_kinds = {
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic4};
    process.operations = {
        ReadSignal { 0U, 0U },
        ReadSignal { 1U, 1U },
        ReadSignal { 2U, 2U },
        ReadSignal { 3U, 3U },
        ReadSignal { 4U, 4U },
        DynamicExtract { 5U, 3U, selection },
        DynamicExtract { 6U, 4U, selection },
        DynamicExtract { 13U, 4U, selection },
        DynamicInsert { 7U, 3U, 1U, selection },
        DynamicInsert { 8U, 3U, 2U, selection },
        DynamicInsert { 9U, 4U, 1U, selection },
        DynamicInsert { 10U, 4U, 2U, selection },
        DynamicInsert { 11U, 4U, 1U, selection },
        DynamicInsert { 12U, 4U, 2U, selection },
        WriteBlocking { 5U, 5U },
        WriteBlocking { 6U, 6U },
        WriteBlocking { 15U, 13U },
        WriteBlocking { 7U, 7U },
        WriteBlocking { 8U, 8U },
        WriteBlocking { 9U, 9U },
        WriteBlocking { 10U, 10U },
        WriteBlocking { 11U, 11U },
        WriteBlocking { 12U, 12U },
        DynamicInsert { 3U, 3U, 1U, selection },
        DynamicInsert { 4U, 4U, 1U, selection },
        // DynamicInsert requires a one-bit source. Capture the low bit of
        // each current wide value before the sequential aliasing update.
        Extract { 14U, 3U, 0U, 1U },
        DynamicInsert { 3U, 4U, 14U, selection },
        Extract { 14U, 3U, 0U, 1U },
        DynamicInsert { 3U, 3U, 14U, selection },
        WriteBlocking { 13U, 3U },
        WriteBlocking { 14U, 4U },
        Halt { }};
    return process;
}

void run_runtime_input_case(
    LlvmJit& jit,
    const JitProcessHandle handle,
    const std::uint32_t extent,
    const std::uint32_t base_offset,
    const bool negative_range,
    const RuntimeIndex raw_index,
    const std::size_t iteration)
{
    const auto target_width = extent + base_offset;
    const auto target4 = make_logic4_pattern(target_width, iteration);
    const auto target9 = make_logic9_pattern(target_width, iteration);
    const auto source4 = PackedLogic4(
        1U, logic4_states[iteration % logic4_states.size()]);
    const auto source9 = make_logic9_bit(
        logic9_states[iteration % logic9_states.size()]);
    const auto index = pack_index(raw_index);
    const auto selection = DynamicIndex {
        0U,
        negative_range
            ? -static_cast<std::int64_t>(extent - 1U)
            : static_cast<std::int64_t>(extent - 1U),
        0,
        base_offset,
        false};

    TestRuntime runtime;
    set_runtime_input(runtime, 0U, index);
    set_runtime_input(runtime, 1U, source4);
    set_runtime_input(runtime, 2U, source9);
    set_runtime_input(runtime, 3U, target4);
    set_runtime_input(runtime, 4U, target9);
    auto descriptor = abi(runtime);
    assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
    assert(runtime.dynamic_part_signal_reads == 0U);
    assert(runtime.packed_signal_reads == (target_width > 64U ? 2U : 0U));

    const auto expected4 = expected_extract(target4, index, selection);
    const auto expected9 = expected_extract(target9, index, selection);
    assert(read_runtime_value(runtime, 5U, 1U, ValueKind::logic4)
        == expected4);
    assert(read_runtime_value(runtime, 6U, 1U, ValueKind::logic9)
        == expected9);
    assert(read_runtime_value(runtime, 15U, 1U, ValueKind::logic4)
        == fsim::runtime::collapse_to_logic4(expected9));

    const auto expected49 = index_is_valid(index, selection)
        ? expected_insert(
              target4.promoted_to_logic9(), source9, index, selection)
        : target4.promoted_to_logic9();
    const auto source4_as_logic9 = source4.promoted_to_logic9();
    const auto target9_as_logic4 = fsim::runtime::collapse_to_logic4(target9);
    const auto source9_as_logic4 = fsim::runtime::collapse_to_logic4(source9);
    const std::array<PackedLogic4, 6> expected_inserts {
        expected_insert(target4, source4, index, selection),
        expected49,
        expected_insert(target9, source4_as_logic9, index, selection),
        expected_insert(target9, source9, index, selection),
        expected_insert(
            target9_as_logic4, source4, index, selection),
        expected_insert(
            target9_as_logic4, source9_as_logic4, index, selection)};
    const std::array<ValueKind, 6> kinds {
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic4};
    const std::array<std::uint32_t, 6> signals {
        7U, 8U, 9U, 10U, 11U, 12U};
    for (std::size_t result = 0U; result < expected_inserts.size(); ++result) {
        assert(read_runtime_value(
                   runtime,
                   signals[result],
                   target_width,
                   kinds[result])
            == expected_inserts[result]);
    }
    const auto target4_after_alias_update = expected_insert(
        target4, source4, index, selection);
    const auto target9_after_alias_update = expected_insert(
        target9, source4_as_logic9, index, selection);
    const auto expected_destination_source_alias = expected_insert(
        fsim::runtime::collapse_to_logic4(target9_after_alias_update),
        target4_after_alias_update,
        index,
        selection);
    const auto expected_all_registers_alias = expected_insert(
        expected_destination_source_alias,
        expected_destination_source_alias,
        index,
        selection);
    assert(read_runtime_value(runtime, 13U, target_width, ValueKind::logic4)
        == expected_all_registers_alias);
    assert(read_runtime_value(runtime, 14U, target_width, ValueKind::logic9)
        == target9_after_alias_update);
}

void test_scalar_alias_cases(
    LlvmJit& jit,
    const std::string_view symbol)
{
    Process process;
    process.id = 204U;
    process.name = "dynamic_single_bit_scalar_aliases";
    process.register_count = 5U;
    process.register_value_kinds = {
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic9};
    const DynamicIndex selection { 0U, 0, 0, 0U, false };
    process.operations = {
        ReadSignal { 0U, 0U },
        ReadSignal { 1U, 1U },
        ReadSignal { 2U, 2U },
        ReadSignal { 3U, 3U },
        ReadSignal { 4U, 4U },
        DynamicInsert { 1U, 2U, 1U, selection },
        WriteBlocking { 5U, 1U },
        DynamicInsert { 2U, 2U, 2U, selection },
        WriteBlocking { 6U, 2U },
        DynamicInsert { 3U, 4U, 3U, selection },
        WriteBlocking { 7U, 3U },
        DynamicInsert { 4U, 4U, 4U, selection },
        WriteBlocking { 8U, 4U },
        Halt { }};
    const std::vector<std::uint32_t> widths {
        32U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U};
    const std::vector<ValueKind> kinds {
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic9};
    const auto process_symbol = std::string { symbol } + "_scalar_alias";
    jit.add_process(process_symbol, process, widths, kinds);
    const auto handle = jit.lookup(process_symbol);

    const auto run_case = [&](const std::size_t iteration,
                              const RuntimeIndex raw_index) {
        const auto target4 = PackedLogic4(
            1U, logic4_states[iteration % logic4_states.size()]);
        const auto source4 = PackedLogic4(
            1U, logic4_states[(iteration + 1U) % logic4_states.size()]);
        const auto target9 = make_logic9_bit(
            logic9_states[iteration % logic9_states.size()]);
        const auto source9 = make_logic9_bit(
            logic9_states[(iteration + 1U) % logic9_states.size()]);
        const auto index = pack_index(raw_index);
        TestRuntime runtime;
        set_runtime_input(runtime, 0U, index);
        set_runtime_input(runtime, 1U, source4);
        set_runtime_input(runtime, 2U, target4);
        set_runtime_input(runtime, 3U, source9);
        set_runtime_input(runtime, 4U, target9);
        auto descriptor = abi(runtime);
        assert(jit.execute(handle, descriptor)
            == JitExecutionStatus::completed);
        const auto expected_source4 = expected_insert(
            target4, source4, index, selection);
        const auto expected_target4 = expected_insert(
            target4, target4, index, selection);
        const auto expected_source9 = expected_insert(
            target9, source9, index, selection);
        const auto expected_target9 = expected_insert(
            target9, target9, index, selection);
        assert(read_runtime_value(runtime, 5U, 1U, ValueKind::logic4)
            == expected_source4);
        assert(read_runtime_value(runtime, 6U, 1U, ValueKind::logic4)
            == expected_target4);
        assert(read_runtime_value(runtime, 7U, 1U, ValueKind::logic9)
            == expected_source9);
        assert(read_runtime_value(runtime, 8U, 1U, ValueKind::logic9)
            == expected_target9);
    };
    for (std::size_t state = 0U; state < logic9_states.size(); ++state) {
        run_case(state, { 0, 0U });
    }
    const std::array<RuntimeIndex, 3> invalid_indices {
        RuntimeIndex { 1, 0U },
        RuntimeIndex { 0, UINT32_C(1) },
        RuntimeIndex { 1, UINT32_C(1) }};
    for (std::size_t index = 0U; index < invalid_indices.size(); ++index) {
        run_case(index, invalid_indices[index]);
    }
}

void test_strict_runtime_failure(
    const JitOptimizationLevel optimization,
    const std::string_view symbol,
    const bool insertion)
{
    constexpr std::uint32_t extent = 129U;
    constexpr std::uint32_t target_width = extent + 3U;
    Process process;
    process.id = insertion ? 206U : 205U;
    process.name = insertion
        ? "wide_dynamic_insert_strict_runtime_input"
        : "wide_dynamic_extract_strict_runtime_input";
    process.register_count = insertion ? 4U : 3U;
    process.register_value_kinds = insertion
        ? std::vector<ValueKind> {
              ValueKind::logic4,
              ValueKind::logic4,
              ValueKind::logic4,
              ValueKind::logic4}
        : std::vector<ValueKind> {
              ValueKind::logic4,
              ValueKind::logic4,
              ValueKind::logic4};
    const DynamicIndex selection { 1U, 128, 0, 3U, true };
    if (insertion) {
        process.operations = {
            ReadSignal { 0U, 0U },
            ReadSignal { 1U, 1U },
            ReadSignal { 2U, 2U },
            DynamicInsert { 3U, 0U, 2U, selection },
            WriteBlocking { 3U, 3U },
            Halt { }};
    } else {
        process.operations = {
            ReadSignal { 0U, 0U },
            ReadSignal { 1U, 1U },
            DynamicExtract { 2U, 0U, selection },
            WriteBlocking { 2U, 2U },
            Halt { }};
    }
    const std::vector<std::uint32_t> signal_widths = insertion
        ? std::vector<std::uint32_t> { target_width, 32U, 1U, target_width }
        : std::vector<std::uint32_t> { target_width, 32U, 1U };
    const std::vector<ValueKind> signal_kinds(
        signal_widths.size(), ValueKind::logic4);
    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    const auto process_symbol = std::string { symbol }
        + (insertion ? "_strict_insert" : "_strict_extract");
    jit.add_process(process_symbol, process, signal_widths, signal_kinds);
    const auto handle = jit.lookup(process_symbol);
    const auto target = make_logic4_pattern(target_width, 2U);
    const auto source = PackedLogic4(1U, Logic4::one);
    constexpr std::string_view unknown_fragment
        = "dynamic packed index contains an unknown or high-impedance value";
    constexpr std::string_view range_fragment
        = "dynamic packed index is outside the declared range";
    const std::array<std::tuple<
        std::int32_t,
        std::uint32_t,
        JitGeneratedRuntimeErrorReason,
        std::string_view>, 3U> failures {
        std::tuple {
            std::int32_t { 0 }, UINT32_C(1),
            JitGeneratedRuntimeErrorReason::dynamic_index_unknown,
            unknown_fragment},
        std::tuple {
            std::int32_t { 1 }, UINT32_C(1),
            JitGeneratedRuntimeErrorReason::dynamic_index_unknown,
            unknown_fragment},
        std::tuple {
            std::int32_t { 129 }, UINT32_C(0),
            JitGeneratedRuntimeErrorReason::dynamic_index_range,
            range_fragment}};
    for (const auto& [index_value, index_bval, reason, fragment] : failures) {
        TestRuntime runtime;
        set_runtime_input(runtime, 0U, target);
        set_runtime_input(runtime, 1U, PackedLogic4::from_aval_bval(
            32U, static_cast<std::uint32_t>(index_value), index_bval));
        if (insertion) {
            set_runtime_input(runtime, 2U, source);
            runtime.wide_signal_aval[3U] = {
                UINT64_C(0x123456789abcdef0),
                UINT64_C(0x0fedcba987654321),
                UINT64_C(0x000000000000000f)};
            runtime.wide_signal_bval[3U] = {
                UINT64_C(0x1111111111111111),
                UINT64_C(0x2222222222222222),
                UINT64_C(0x0000000000000001)};
            const auto sentinel_aval = runtime.wide_signal_aval[3U];
            const auto sentinel_bval = runtime.wide_signal_bval[3U];
            auto descriptor = abi(runtime);
            expect_generated_runtime_error(
                [&] { static_cast<void>(jit.execute(handle, descriptor)); },
                3U,
                reason,
                fragment);
            assert(runtime.wide_signal_aval[3U] == sentinel_aval);
            assert(runtime.wide_signal_bval[3U] == sentinel_bval);
        } else {
            runtime.signals[2U] = { UINT64_C(1), UINT64_C(0) };
            const auto sentinel = runtime.signals[2U];
            auto descriptor = abi(runtime);
            expect_generated_runtime_error(
                [&] { static_cast<void>(jit.execute(handle, descriptor)); },
                2U,
                reason,
                fragment);
            assert(runtime.signals[2U] == sentinel);
        }
    }
}

}  // namespace

void test_wide_dynamic_single_bit_operations_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    constexpr std::array<std::uint32_t, 5> extents {
        1U, 65U, 129U, 256U, 1024U};
    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    for (const auto extent : extents) {
        for (const auto base_offset : { 0U, 3U }) {
            for (const bool negative_range : { false, true }) {
                const auto process = make_runtime_input_process(
                    extent, base_offset, negative_range);
                const auto target_width = extent + base_offset;
                const std::vector<std::uint32_t> signal_widths {
                    32U,
                    1U,
                    1U,
                    target_width,
                    target_width,
                    1U,
                    1U,
                    target_width,
                    target_width,
                    target_width,
                    target_width,
                    target_width,
                    target_width,
                    target_width,
                    target_width,
                    1U};
                const std::vector<ValueKind> signal_kinds {
                    ValueKind::logic4,
                    ValueKind::logic4,
                    ValueKind::logic9,
                    ValueKind::logic4,
                    ValueKind::logic9,
                    ValueKind::logic4,
                    ValueKind::logic9,
                    ValueKind::logic4,
                    ValueKind::logic9,
                    ValueKind::logic9,
                    ValueKind::logic9,
                    ValueKind::logic4,
                    ValueKind::logic4,
                    ValueKind::logic4,
                    ValueKind::logic9,
                    ValueKind::logic4};
                const auto instance_symbol = std::string { symbol }
                    + "_" + std::to_string(extent)
                    + "_offset" + std::to_string(base_offset)
                    + (negative_range ? "_negative" : "_positive");
                jit.add_process(
                    instance_symbol, process, signal_widths, signal_kinds);
                const auto handle = jit.lookup(instance_symbol);
                const auto cases = make_index_cases(
                    extent, base_offset, negative_range);
                for (std::size_t iteration = 0U;
                     iteration < cases.size();
                     ++iteration) {
                    run_runtime_input_case(
                        jit,
                        handle,
                        extent,
                        base_offset,
                        negative_range,
                        cases[iteration],
                        iteration);
                }
                const DynamicIndex selection {
                    0U,
                    negative_range
                        ? -static_cast<std::int64_t>(extent - 1U)
                        : static_cast<std::int64_t>(extent - 1U),
                    0,
                    base_offset,
                    false};
                std::vector<RuntimeIndex> valid_cases;
                for (const auto test_case : cases) {
                    if (index_is_valid(pack_index(test_case), selection)) {
                        valid_cases.push_back(test_case);
                    }
                }
                assert(!valid_cases.empty());
                // Rotate nine-state operands independently of invalid indices.
                for (std::size_t state = 0U;
                     state < logic9_states.size();
                     ++state) {
                    run_runtime_input_case(
                        jit,
                        handle,
                        extent,
                        base_offset,
                        negative_range,
                        valid_cases[state % valid_cases.size()],
                        state);
                }
            }
        }
    }

    test_scalar_alias_cases(jit, symbol);

    test_strict_runtime_failure(optimization, symbol, false);
    test_strict_runtime_failure(optimization, symbol, true);
}

}  // namespace fsim::tests::compiler

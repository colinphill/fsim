// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

#include <fsim/runtime/logic.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::compiler {

namespace {

using namespace llvm_jit_test_detail;

constexpr std::array<RegisterId, 16> kOutputRegisters {
    2U, 3U, 4U, 5U, 8U, 9U, 10U, 11U,
    12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U
};

[[nodiscard]] PackedLogic4 make_logic4(
    const std::uint32_t width,
    const std::size_t seed)
{
    constexpr std::array states {
        Logic4::zero, Logic4::one, Logic4::x, Logic4::z
    };
    auto result = PackedLogic4(width, Logic4::zero);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        result.set(bit, states[(bit * 3U + seed) % states.size()]);
    }
    return result;
}

[[nodiscard]] PackedLogic4 make_logic9(
    const std::uint32_t width,
    const std::size_t seed)
{
    constexpr std::array states {
        Logic9::u,
        Logic9::x,
        Logic9::zero,
        Logic9::one,
        Logic9::z,
        Logic9::w,
        Logic9::l,
        Logic9::h,
        Logic9::dont_care
    };
    auto result = PackedLogic4(width, Logic4::zero);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        result.set_logic9(bit, states[(bit * 5U + seed) % states.size()]);
    }
    return result;
}

void assign_wide_input(
    TestRuntime& runtime,
    const std::uint32_t signal,
    const PackedLogic4& value)
{
    runtime.wide_signal_aval[signal].assign(
        value.aval_words().begin(), value.aval_words().end());
    runtime.wide_signal_bval[signal].assign(
        value.bval_words().begin(), value.bval_words().end());
    if (value.is_logic9()) {
        for (std::size_t plane = 2U; plane < 4U; ++plane) {
            const auto words = value.logic9_plane_words(plane);
            auto& destination = plane == 2U
                ? runtime.wide_signal_logic9_plane2[signal]
                : runtime.wide_signal_logic9_plane3[signal];
            destination.assign(words.begin(), words.end());
        }
    } else {
        runtime.wide_signal_logic9_plane2[signal].clear();
        runtime.wide_signal_logic9_plane3[signal].clear();
    }
}

[[nodiscard]] PackedLogic4 read_register(
    const fsim::compiler::JitProcessFrameLayout& layout,
    const RegisterId register_id,
    const ValueKind kind,
    const std::vector<std::uint64_t>& aval,
    const std::vector<std::uint64_t>& bval,
    const std::vector<std::uint64_t>& plane2,
    const std::vector<std::uint64_t>& plane3)
{
    const auto width = layout.register_widths[register_id];
    const auto offset = layout.register_word_offsets[register_id];
    const auto words = static_cast<std::size_t>((width + 63U) / 64U);
    const auto aval_words = std::span { aval }.subspan(offset, words);
    const auto bval_words = std::span { bval }.subspan(offset, words);
    if (kind == ValueKind::logic9) {
        return PackedLogic4::from_logic9_word_planes(
            width,
            aval_words,
            bval_words,
            std::span { plane2 }.subspan(offset, words),
            std::span { plane3 }.subspan(offset, words));
    }
    return PackedLogic4::from_word_planes(width, aval_words, bval_words);
}

[[nodiscard]] Logic4 apply_logic4(
    const BinaryOperator operation,
    const Logic4 lhs,
    const Logic4 rhs)
{
    switch (operation) {
    case BinaryOperator::bit_and:
        return fsim::runtime::logic_and(lhs, rhs);
    case BinaryOperator::bit_or:
        return fsim::runtime::logic_or(lhs, rhs);
    case BinaryOperator::bit_xor:
        return fsim::runtime::logic_xor(lhs, rhs);
    default:
        assert(false);
        return Logic4::x;
    }
}

[[nodiscard]] Logic9 apply_logic9(
    const BinaryOperator operation,
    const Logic9 lhs,
    const Logic9 rhs)
{
    switch (operation) {
    case BinaryOperator::bit_and:
        return fsim::runtime::logic_and(lhs, rhs);
    case BinaryOperator::bit_or:
        return fsim::runtime::logic_or(lhs, rhs);
    case BinaryOperator::bit_xor:
        return fsim::runtime::logic_xor(lhs, rhs);
    default:
        assert(false);
        return Logic9::x;
    }
}

[[nodiscard]] PackedLogic4 expected_binary(
    const PackedLogic4& lhs,
    const ValueKind lhs_kind,
    const PackedLogic4& rhs,
    const ValueKind rhs_kind,
    const ValueKind destination_kind,
    const BinaryOperator operation)
{
    assert(lhs.width() == rhs.width());
    auto result = PackedLogic4(lhs.width(), Logic4::zero);
    for (std::size_t bit = 0U; bit < lhs.width(); ++bit) {
        if (lhs_kind == ValueKind::logic9
            || rhs_kind == ValueKind::logic9) {
            const auto left = lhs_kind == ValueKind::logic9
                ? lhs.get_logic9(bit)
                : fsim::runtime::to_logic9(lhs.get(bit));
            const auto right = rhs_kind == ValueKind::logic9
                ? rhs.get_logic9(bit)
                : fsim::runtime::to_logic9(rhs.get(bit));
            const auto value = apply_logic9(operation, left, right);
            result.set_logic9(bit, destination_kind == ValueKind::logic9
                    ? value
                    : fsim::runtime::to_logic9(
                        fsim::runtime::to_logic4(value)));
        } else {
            const auto value = apply_logic4(
                operation, lhs.get(bit), rhs.get(bit));
            if (destination_kind == ValueKind::logic9) {
                result.set_logic9(bit, fsim::runtime::to_logic9(value));
            } else {
                result.set(bit, value);
            }
        }
    }
    if (destination_kind == ValueKind::logic4 && result.is_logic9()) {
        return fsim::runtime::collapse_to_logic4(result);
    }
    return result;
}

[[nodiscard]] PackedLogic4 expected_not(
    const PackedLogic4& source,
    const ValueKind source_kind,
    const ValueKind destination_kind)
{
    auto result = PackedLogic4(source.width(), Logic4::zero);
    for (std::size_t bit = 0U; bit < source.width(); ++bit) {
        if (source_kind == ValueKind::logic9) {
            const auto value = fsim::runtime::logic_not(
                source.get_logic9(bit));
            if (destination_kind == ValueKind::logic9) {
                result.set_logic9(bit, value);
            } else {
                result.set(bit, fsim::runtime::to_logic4(value));
            }
        } else {
            const auto value = fsim::runtime::logic_not(source.get(bit));
            if (destination_kind == ValueKind::logic9) {
                result.set_logic9(bit, fsim::runtime::to_logic9(value));
            } else {
                result.set(bit, value);
            }
        }
    }
    return result;
}

void append_activation(
    Process& process,
    const RegisterId snapshots_begin)
{
    process.operations.emplace_back(ReadSignal { 0U, 0U });
    process.operations.emplace_back(ReadSignal { 1U, 1U });
    process.operations.emplace_back(ReadSignal { 6U, 2U });
    process.operations.emplace_back(ReadSignal { 7U, 3U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_and, 2U, 0U, 1U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_or, 3U, 0U, 1U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_xor, 4U, 0U, 1U });
    process.operations.emplace_back(UnaryNot { 5U, 0U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_and, 8U, 6U, 7U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_or, 9U, 6U, 7U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_xor, 10U, 6U, 7U });
    process.operations.emplace_back(UnaryNot { 11U, 6U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_xor, 12U, 0U, 6U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_or, 13U, 6U, 1U });
    process.operations.emplace_back(CopyRegister { 14U, 0U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_xor, 14U, 14U, 1U });
    process.operations.emplace_back(CopyRegister { 15U, 6U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_or, 15U, 15U, 7U });
    process.operations.emplace_back(CopyRegister { 16U, 1U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_and, 16U, 0U, 16U });
    process.operations.emplace_back(CopyRegister { 17U, 0U });
    process.operations.emplace_back(UnaryNot { 17U, 17U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_xor, 18U, 0U, 1U });
    process.operations.emplace_back(Binary {
        BinaryOperator::bit_and, 19U, 6U, 7U });
    process.operations.emplace_back(Pause { });
    for (std::size_t index = 0U; index < kOutputRegisters.size(); ++index) {
        process.operations.emplace_back(CopyRegister {
            static_cast<RegisterId>(snapshots_begin + index),
            kOutputRegisters[index] });
    }
}

void run_width(
    const JitOptimizationLevel optimization,
    const std::uint32_t width,
    const std::string_view symbol)
{
    Process process;
    process.id = 620U + width;
    process.name = "wide_bitwise_" + std::to_string(width);
    process.register_count = 52U;
    process.register_value_kinds.assign(
        process.register_count, ValueKind::logic4);
    for (RegisterId id : { 6U, 7U, 8U, 9U, 10U, 11U, 12U, 15U, 18U }) {
        process.register_value_kinds[id] = ValueKind::logic9;
    }
    process.operations.reserve(64U);
    append_activation(process, 20U);
    append_activation(process, 36U);
    process.operations.emplace_back(Stop { });

    const std::array signal_widths { width, width, width, width };
    const std::array signal_kinds {
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic9
    };
    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, signal_widths, signal_kinds));
    jit.add_process(symbol, process, signal_widths, signal_kinds);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.uses_logic9);
    assert(layout.tracks_register_initialization);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint64_t> plane2(layout.register_word_count);
    std::vector<std::uint64_t> plane3(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(
        handle, frame, aval, bval, initialized, plane2, plane3);

    const auto check_activation = [&](const std::size_t activation) {
        const auto l4_lhs = make_logic4(width, activation + 0U);
        const auto l4_rhs = make_logic4(width, activation + 1U);
        const auto l9_lhs = make_logic9(width, activation + 2U);
        const auto l9_rhs = make_logic9(width, activation + 3U);
        auto canonical_l9_lhs = l9_lhs;
        const auto malformed_bit = static_cast<std::size_t>(width - 1U);
        canonical_l9_lhs.set_logic9(malformed_bit, Logic9::x);
        TestRuntime runtime;
        assign_wide_input(runtime, 0U, l4_lhs);
        assign_wide_input(runtime, 1U, l4_rhs);
        assign_wide_input(runtime, 2U, l9_lhs);
        assign_wide_input(runtime, 3U, l9_rhs);
        const auto malformed_word = malformed_bit / 64U;
        const auto malformed_mask = std::uint64_t { 1U }
            << (malformed_bit % 64U);
        for (std::size_t plane = 0U; plane < 4U; ++plane) {
            auto& words = plane == 0U
                ? runtime.wide_signal_aval[2U]
                : plane == 1U
                    ? runtime.wide_signal_bval[2U]
                    : plane == 2U
                        ? runtime.wide_signal_logic9_plane2[2U]
                        : runtime.wide_signal_logic9_plane3[2U];
            if (((9U >> plane) & 1U) != 0U) {
                words[malformed_word] |= malformed_mask;
            } else {
                words[malformed_word] &= ~malformed_mask;
            }
        }
        auto descriptor = abi(runtime);
        auto result = new_resume_result();
        assert(jit.resume(handle, descriptor, frame, result)
            == JitResumeStatus::paused);
        assert(result.instruction != 0U);

        const std::array<PackedLogic4, kOutputRegisters.size()> expected {
            expected_binary(l4_lhs, ValueKind::logic4,
                l4_rhs, ValueKind::logic4, ValueKind::logic4,
                BinaryOperator::bit_and),
            expected_binary(l4_lhs, ValueKind::logic4,
                l4_rhs, ValueKind::logic4, ValueKind::logic4,
                BinaryOperator::bit_or),
            expected_binary(l4_lhs, ValueKind::logic4,
                l4_rhs, ValueKind::logic4, ValueKind::logic4,
                BinaryOperator::bit_xor),
            expected_not(l4_lhs, ValueKind::logic4, ValueKind::logic4),
            expected_binary(canonical_l9_lhs, ValueKind::logic9,
                l9_rhs, ValueKind::logic9, ValueKind::logic9,
                BinaryOperator::bit_and),
            expected_binary(canonical_l9_lhs, ValueKind::logic9,
                l9_rhs, ValueKind::logic9, ValueKind::logic9,
                BinaryOperator::bit_or),
            expected_binary(canonical_l9_lhs, ValueKind::logic9,
                l9_rhs, ValueKind::logic9, ValueKind::logic9,
                BinaryOperator::bit_xor),
            expected_not(
                canonical_l9_lhs, ValueKind::logic9, ValueKind::logic9),
            expected_binary(l4_lhs, ValueKind::logic4,
                canonical_l9_lhs, ValueKind::logic9, ValueKind::logic9,
                BinaryOperator::bit_xor),
            expected_binary(canonical_l9_lhs, ValueKind::logic9,
                l4_rhs, ValueKind::logic4, ValueKind::logic4,
                BinaryOperator::bit_or),
            expected_binary(l4_lhs, ValueKind::logic4,
                l4_rhs, ValueKind::logic4, ValueKind::logic4,
                BinaryOperator::bit_xor),
            expected_binary(canonical_l9_lhs, ValueKind::logic9,
                l9_rhs, ValueKind::logic9, ValueKind::logic9,
                BinaryOperator::bit_or),
            expected_binary(l4_lhs, ValueKind::logic4,
                l4_rhs, ValueKind::logic4, ValueKind::logic4,
                BinaryOperator::bit_and),
            expected_not(l4_lhs, ValueKind::logic4, ValueKind::logic4),
            expected_binary(l4_lhs, ValueKind::logic4,
                l4_rhs, ValueKind::logic4, ValueKind::logic9,
                BinaryOperator::bit_xor),
            expected_binary(canonical_l9_lhs, ValueKind::logic9,
                l9_rhs, ValueKind::logic9, ValueKind::logic4,
                BinaryOperator::bit_and)
        };
        for (std::size_t index = 0U; index < kOutputRegisters.size(); ++index) {
            const auto register_id = kOutputRegisters[index];
            assert(initialized[register_id] != 0U);
            assert(read_register(
                layout,
                register_id,
                process.register_value_kinds[register_id],
                aval,
                bval,
                plane2,
                plane3)
                == expected[index]);
            if (width % 64U != 0U) {
                const auto tail_mask = ~((std::uint64_t { 1U }
                    << (width % 64U)) - 1U);
                const auto offset = layout.register_word_offsets[register_id]
                    + (width / 64U);
                assert((aval[offset] & tail_mask) == 0U);
                assert((bval[offset] & tail_mask) == 0U);
                if (process.register_value_kinds[register_id]
                    == ValueKind::logic9) {
                    assert((plane2[offset] & tail_mask) == 0U);
                    assert((plane3[offset] & tail_mask) == 0U);
                }
            }
        }
        assert(runtime.packed_signal_reads == 4U);
    };

    check_activation(0U);
    check_activation(7U);
}

void run_untracked_wide_width(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    constexpr std::uint32_t width = 256U;
    Process process;
    process.id = 1876U;
    process.name = std::string { symbol };
    process.register_count = 4U;
    process.register_value_kinds.assign(
        process.register_count, ValueKind::logic4);
    process.operations = {
        ReadSignal { 0U, 0U },
        ReadSignal { 1U, 1U },
        Binary { BinaryOperator::bit_and, 2U, 0U, 1U },
        UnaryNot { 3U, 0U },
        Pause { },
        WriteBlocking { 2U, 2U },
        WriteBlocking { 3U, 3U },
        Halt { }
    };

    const std::array signal_widths { width, width, width, width };
    const std::array signal_kinds {
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic4
    };
    LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    assert(jit.supports_process(process, signal_widths, signal_kinds));
    jit.add_process(symbol, process, signal_widths, signal_kinds);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(!layout.tracks_register_initialization);

    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);

    const auto lhs = make_logic4(width, 5U);
    const auto rhs = make_logic4(width, 12U);
    TestRuntime runtime;
    assign_wide_input(runtime, 0U, lhs);
    assign_wide_input(runtime, 1U, rhs);
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(result.instruction == 4U);
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::completed);
    assert((initialized == std::vector<std::uint8_t>(layout.register_count)));
    const auto expected_and = expected_binary(
        lhs, ValueKind::logic4,
        rhs, ValueKind::logic4, ValueKind::logic4,
        BinaryOperator::bit_and);
    const auto expected_not_value = expected_not(
        lhs, ValueKind::logic4, ValueKind::logic4);
    assert((runtime.packed_signal_write_signals
        == std::vector<std::uint32_t> { 2U, 3U }));
    assert((runtime.wide_signal_aval[2U]
        == std::vector<std::uint64_t>(
            expected_and.aval_words().begin(),
            expected_and.aval_words().end())));
    assert((runtime.wide_signal_bval[2U]
        == std::vector<std::uint64_t>(
            expected_and.bval_words().begin(),
            expected_and.bval_words().end())));
    assert((runtime.wide_signal_aval[3U]
        == std::vector<std::uint64_t>(
            expected_not_value.aval_words().begin(),
            expected_not_value.aval_words().end())));
    assert((runtime.wide_signal_bval[3U]
        == std::vector<std::uint64_t>(
            expected_not_value.bval_words().begin(),
            expected_not_value.bval_words().end())));
    assert(runtime.packed_signal_reads == 2U);
}

void run_shape_width(
    const std::uint32_t width,
    const std::string_view symbol)
{
    Process process;
    process.id = 620U + width;
    process.name = "wide_bitwise_shape_" + std::to_string(width);
    process.register_count = 10U;
    process.register_value_kinds.assign(
        process.register_count, ValueKind::logic4);
    process.operations.emplace_back(ReadSignal { 0U, 0U });
    process.operations.emplace_back(ReadSignal { 1U, 1U });
    process.operations.emplace_back(
        Binary { BinaryOperator::bit_and, 2U, 0U, 1U });
    process.operations.emplace_back(
        Binary { BinaryOperator::bit_or, 3U, 0U, 1U });
    process.operations.emplace_back(
        Binary { BinaryOperator::bit_xor, 4U, 0U, 1U });
    process.operations.emplace_back(UnaryNot { 5U, 0U });
    process.operations.emplace_back(Pause { });
    for (RegisterId index = 0U; index < 4U; ++index) {
        process.operations.emplace_back(CopyRegister {
            static_cast<RegisterId>(6U + index),
            static_cast<RegisterId>(2U + index) });
    }
    process.operations.emplace_back(Stop { });

    const std::array signal_widths { width, width };
    const std::array signal_kinds {
        ValueKind::logic4, ValueKind::logic4
    };
    LlvmJit jit { LlvmJitOptions {
        JitOptimizationLevel::o2, { } } };
    assert(jit.supports_process(process, signal_widths, signal_kinds));
    jit.add_process(symbol, process, signal_widths, signal_kinds);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);

    const auto lhs = make_logic4(width, 3U);
    const auto rhs = make_logic4(width, 8U);
    TestRuntime runtime;
    assign_wide_input(runtime, 0U, lhs);
    assign_wide_input(runtime, 1U, rhs);
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    const std::array<PackedLogic4, 4> expected {
        expected_binary(lhs, ValueKind::logic4,
            rhs, ValueKind::logic4, ValueKind::logic4,
            BinaryOperator::bit_and),
        expected_binary(lhs, ValueKind::logic4,
            rhs, ValueKind::logic4, ValueKind::logic4,
            BinaryOperator::bit_or),
        expected_binary(lhs, ValueKind::logic4,
            rhs, ValueKind::logic4, ValueKind::logic4,
            BinaryOperator::bit_xor),
        expected_not(lhs, ValueKind::logic4, ValueKind::logic4)
    };
    for (RegisterId index = 0U; index < expected.size(); ++index) {
        const auto register_id = static_cast<RegisterId>(2U + index);
        assert(initialized[register_id] != 0U);
        assert(read_register(layout, register_id, ValueKind::logic4,
            aval, bval, { }, { }) == expected[index]);
    }
    assert(runtime.packed_signal_reads == 2U);
}

} // namespace

void test_wide_bitwise_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    for (const auto width : { 65U, 129U, 256U, 1024U }) {
        run_width(
            optimization,
            width,
            std::string { symbol } + "_" + std::to_string(width));
    }
    run_untracked_wide_width(
        optimization, std::string { symbol } + "_untracked");
}

void test_wide_bitwise_profile()
{
    run_shape_width(256U, "wide_bitwise_profile_256");
    run_shape_width(1024U, "wide_bitwise_profile_1024");
}

} // namespace fsim::tests::compiler

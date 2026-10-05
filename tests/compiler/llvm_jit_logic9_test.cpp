// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

extern "C" std::uint32_t read_reserved_logic9_dynamic_part(
    void* opaque,
    std::uint32_t,
    std::uint32_t,
    std::uint64_t,
    std::uint64_t,
    std::int64_t,
    std::int64_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    fsim_jit_logic9_word_v2* result)
{
    if (opaque == nullptr || result == nullptr) {
        return 1U;
    }
    auto& runtime = *static_cast<TestRuntime*>(opaque);
    ++runtime.dynamic_part_signal_reads;
    result->planes[0] = UINT64_MAX;
    result->planes[1] = UINT64_MAX;
    result->planes[2] = UINT64_MAX;
    result->planes[3] = UINT64_MAX;
    return 0U;
}

void test_logic9_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 91;
    process.name = "logic9_exact";
    process.register_count = 10;
    process.register_value_kinds = {
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic4
    };
    const auto all_high = PackedLogic4::from_logic9_msb_string("HHHHHHHH");
    process.operations = {
        ReadSignal { 0, 4 },
        UnaryNot { 1, 0 },
        LoadConstant { 2, all_high },
        Binary { BinaryOperator::bit_and, 3, 0, 2 },
        CopyRegister { 4, 0 },
        CopyRegister { 5, 4 },
        LoadConstant {
            6,
            PackedLogic4::from_logic9_msb_string("-01--01-") },
        Binary { BinaryOperator::vhdl_match_equal, 7, 0, 6 },
        LoadConstant {
            8,
            PackedLogic4::from_logic9_msb_string("U01ZWLH-") },
        Binary { BinaryOperator::vhdl_match_equal, 9, 0, 8 },
        WriteBlocking { 0, 1 },
        WriteUpdate { 1, 3 },
        WriteBlocking { 2, 4 },
        WriteAfter { 3, 5, 7 },
        WriteBlocking { 5, 7 },
        WriteBlocking { 6, 9 },
        FormatDisplay {
            0, OutputFormat::binary, "", "", true, false },
        Halt { }
    };

    const std::array<std::uint32_t, 7> widths { 8, 8, 8, 8, 8, 1, 1 };
    const std::array<ValueKind, 7> kinds {
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic4
    };
    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, widths, kinds));
    jit.add_process(symbol, process, widths, kinds);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.uses_logic9);
    std::vector<std::uint64_t> register_aval(layout.register_count);
    std::vector<std::uint64_t> register_bval(layout.register_count);
    std::vector<std::uint8_t> register_initialized(
        layout.register_count);
    fsim_jit_frame_v2 frame { };
    expect_error(
        [&] {
            jit.initialize_frame(
                handle,
                frame,
                register_aval,
                register_bval,
                register_initialized);
        },
        "smaller than the frame layout");
    std::vector<std::uint64_t> register_plane2(layout.register_count);
    std::vector<std::uint64_t> register_plane3(layout.register_count);
    jit.initialize_frame(
        handle,
        frame,
        register_aval,
        register_bval,
        register_initialized,
        register_plane2,
        register_plane3);
    assert(frame.register_logic9_plane2 == register_plane2.data());
    assert(frame.register_logic9_plane3 == register_plane3.data());

    const auto source = PackedLogic4::from_logic9_msb_string("U01ZWLH-");
    auto expected_not = source;
    auto expected_and = source;
    for (std::size_t bit = 0; bit < source.width(); ++bit) {
        expected_not.set_logic9(
            bit, fsim::runtime::logic_not(source.get_logic9(bit)));
        expected_and.set_logic9(
            bit,
            fsim::runtime::logic_and(
                source.get_logic9(bit), Logic9::h));
    }
    const auto collapsed = fsim::runtime::collapse_to_logic4(source);
    const auto reexpanded = collapsed.promoted_to_logic9();

    TestRuntime runtime;
    runtime.logic9_signals[4] = planes(source);
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    assert(runtime.logic9_signals[0] == planes(expected_not));
    assert(runtime.logic9_signals[1] == planes(expected_and));
    assert(runtime.signals[2] == encode(collapsed));
    assert(runtime.logic9_signals[3] == planes(reexpanded));
    assert(runtime.signals[5] == encode(PackedLogic4::from_msb_string("1")));
    assert(runtime.signals[6] == encode(PackedLogic4::from_msb_string("0")));
    assert(runtime.formatted_logic9_values.size() == 1);
    assert(runtime.formatted_logic9_values.front() == planes(source));

    // Exercise every wire encoding, including reserved ordinals, through
    // the public callback ABI before converting into a Logic4 register.
    Process coercion;
    coercion.id = 113;
    coercion.name = "logic9_all_wire_codes_to_logic4";
    coercion.register_count = 3;
    coercion.register_value_kinds = {
        ValueKind::logic9, ValueKind::logic4, ValueKind::logic9
    };
    coercion.operations = {
        ReadSignal { 0, 0 },
        CopyRegister { 1, 0 },
        UnaryNot { 2, 0 },
        WriteBlocking { 1, 1 },
        WriteBlocking { 2, 2 },
        Halt { }
    };
    const auto coercion_symbol = std::string { symbol } + "_all_wire_codes";
    jit.add_process(coercion_symbol, coercion,
        std::array<std::uint32_t, 3> { 16U, 16U, 16U },
        std::array<ValueKind, 3> {
            ValueKind::logic9, ValueKind::logic4, ValueKind::logic9 });
    TestRuntime coercion_runtime;
    constexpr std::array expected_codes {
        Logic4::x, Logic4::x, Logic4::zero, Logic4::one,
        Logic4::z, Logic4::x, Logic4::zero, Logic4::one,
        Logic4::x, Logic4::x, Logic4::x, Logic4::x,
        Logic4::x, Logic4::x, Logic4::x, Logic4::x
    };
    constexpr std::array expected_not_codes {
        Logic9::u, Logic9::x, Logic9::one, Logic9::zero,
        Logic9::x, Logic9::x, Logic9::one, Logic9::zero,
        Logic9::x, Logic9::x, Logic9::x, Logic9::x,
        Logic9::x, Logic9::x, Logic9::x, Logic9::x
    };
    auto coercion_expected = PackedLogic4(16U, Logic4::zero);
    auto all_codes_not_expected = PackedLogic4::from_logic9_msb_string(
        std::string(16U, 'U'));
    for (std::uint32_t code = 0U; code < 16U; ++code) {
        for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
            if ((code & (1U << plane)) != 0U) {
                coercion_runtime.logic9_signals[0][plane]
                    |= UINT64_C(1) << code;
            }
        }
        coercion_expected.set(code, expected_codes[code]);
        all_codes_not_expected.set_logic9(code, expected_not_codes[code]);
    }
    auto coercion_descriptor = abi(coercion_runtime);
    assert(jit.execute(jit.lookup(coercion_symbol), coercion_descriptor)
        == JitExecutionStatus::completed);
    assert(coercion_runtime.signals[1] == encode(coercion_expected));
    assert(coercion_runtime.logic9_signals[2]
        == planes(all_codes_not_expected));

    Process wide_case_equal;
    wide_case_equal.id = 112;
    wide_case_equal.name = "wide_logic9_case_equal";
    std::vector<std::uint8_t> expected_case_equal;
    const auto append_case_equal = [&](const std::uint32_t width,
                                       const PackedLogic4& lhs,
                                       const PackedLogic4& rhs,
                                       const bool expected) {
        assert(lhs.width() == width);
        assert(rhs.width() == width);
        const auto lhs_register = static_cast<RegisterId>(
            wide_case_equal.register_count);
        const auto rhs_register = static_cast<RegisterId>(
            wide_case_equal.register_count + 1U);
        const auto result_register = static_cast<RegisterId>(
            wide_case_equal.register_count + 2U);
        const auto signal = static_cast<SignalId>(
            expected_case_equal.size());
        wide_case_equal.register_count += 3U;
        wide_case_equal.register_value_kinds.insert(
            wide_case_equal.register_value_kinds.end(),
            { ValueKind::logic9, ValueKind::logic9, ValueKind::logic4 });
        wide_case_equal.operations.emplace_back(
            LoadConstant { lhs_register, lhs });
        wide_case_equal.operations.emplace_back(
            LoadConstant { rhs_register, rhs });
        wide_case_equal.operations.emplace_back(Binary {
            BinaryOperator::case_equal,
            result_register,
            lhs_register,
            rhs_register });
        wide_case_equal.operations.emplace_back(
            WriteBlocking { signal, result_register });
        expected_case_equal.push_back(expected ? 1U : 0U);
    };
    const std::array<Logic9, 4> high_bit_plane_changes {
        Logic9::x,
        Logic9::zero,
        Logic9::z,
        Logic9::dont_care
    };
    for (const auto width : { 65U, 130U }) {
        const auto high_bit = static_cast<std::size_t>(width - 1U);
        auto identical = PackedLogic4(width, Logic4::zero);
        identical.set_logic9(high_bit, Logic9::dont_care);
        identical.set_logic9(high_bit - 1U, Logic9::h);
        identical.set_logic9(high_bit - 2U, Logic9::zero);
        append_case_equal(width, identical, identical, true);

        auto lhs = PackedLogic4(width, Logic4::zero);
        lhs.set_logic9(high_bit, Logic9::u);
        for (const auto changed_state : high_bit_plane_changes) {
            auto rhs = lhs;
            rhs.set_logic9(high_bit, changed_state);
            append_case_equal(width, lhs, rhs, false);
        }
    }
    wide_case_equal.operations.emplace_back(Halt { });
    std::array<std::uint32_t, 10> case_equal_signal_widths { };
    case_equal_signal_widths.fill(1U);
    std::array<ValueKind, 10> case_equal_signal_kinds { };
    case_equal_signal_kinds.fill(ValueKind::logic4);
    const auto wide_case_equal_symbol
        = std::string { symbol } + "_wide_case_equal";
    assert(jit.supports_process(
        wide_case_equal,
        case_equal_signal_widths,
        case_equal_signal_kinds));
    jit.add_process(
        wide_case_equal_symbol,
        wide_case_equal,
        case_equal_signal_widths,
        case_equal_signal_kinds);
    TestRuntime wide_case_equal_runtime;
    auto wide_case_equal_descriptor = abi(wide_case_equal_runtime);
    assert(jit.execute(
               jit.lookup(wide_case_equal_symbol),
               wide_case_equal_descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0;
         index < expected_case_equal.size();
         ++index) {
        const auto expected = expected_case_equal[index] != 0U
            ? PackedLogic4::from_msb_string("1")
            : PackedLogic4::from_msb_string("0");
        assert(wide_case_equal_runtime.signals[index] == encode(expected));
    }

    Process direct_process;
    direct_process.id = 107;
    direct_process.name = "logic9_direct_update_accumulator";
    direct_process.register_count = 3;
    direct_process.register_value_kinds.assign(3U, ValueKind::logic9);
    const auto direct_whole = PackedLogic4::from_logic9_msb_string(
        "U01ZWLH-");
    const auto direct_slice = PackedLogic4::from_logic9_msb_string("10");
    const auto direct_projected = PackedLogic4::from_logic9_msb_string("HL");
    direct_process.operations = {
        LoadConstant { 0, direct_whole },
        WriteUpdate { 0, 0 },
        LoadConstant { 1, direct_slice },
        WriteUpdateSlice { 0, 1, 2 },
        LoadConstant { 2, direct_projected },
        WriteProjectedSlice {
            0, 2, 6, 0U, 0U, ProjectedDelayMode::inertial },
        Halt { }
    };
    const std::array<std::uint32_t, 1> direct_widths { 8U };
    const std::array<ValueKind, 1> direct_kinds { ValueKind::logic9 };
    LlvmJitOptions direct_options { optimization, { } };
    direct_options.require_direct_update_slots = true;
    LlvmJit direct_jit { direct_options };
    const auto direct_symbol = std::string { symbol } + "_direct_update";
    direct_jit.add_process(
        direct_symbol, direct_process, direct_widths, direct_kinds);
    const auto direct_handle = direct_jit.lookup(direct_symbol);
    assert((direct_jit.frame_layout(direct_handle).direct_update_signals
        == std::vector<SignalId> { 0U }));
    TestRuntime direct_runtime;
    auto direct_descriptor = abi(direct_runtime);
    fsim_jit_update_slot_v2 direct_slot { };
    direct_slot.width = 8U;
    direct_slot.word_count = 1U;
    std::array<std::uint64_t, 1> direct_active_words { };
    direct_descriptor.direct_update_slots = &direct_slot;
    direct_descriptor.direct_update_slot_count = 1U;
    direct_descriptor.direct_update_active_words
        = direct_active_words.data();
    direct_descriptor.direct_update_active_word_count = 1U;
    assert(direct_jit.execute(direct_handle, direct_descriptor)
        == JitExecutionStatus::completed);
    auto direct_expected = direct_whole;
    for (std::size_t bit = 0; bit < direct_slice.width(); ++bit) {
        direct_expected.set_logic9(
            2U + bit, direct_slice.get_logic9(bit));
    }
    for (std::size_t bit = 0; bit < direct_projected.width(); ++bit) {
        direct_expected.set_logic9(
            6U + bit, direct_projected.get_logic9(bit));
    }
    const auto expected_direct_word = direct_expected.logic9_low_word();
    assert((direct_runtime.logic9_signals[0]
        == std::array<std::uint64_t, 4> { }));
    assert(direct_slot.mask == UINT64_C(0xff));
    assert(direct_slot.active == 0U);
    assert(direct_active_words[0] == 0U);
    assert(direct_slot.aval == expected_direct_word.planes[0]);
    assert(direct_slot.bval == expected_direct_word.planes[1]);
    assert(direct_slot.logic9_plane2 == expected_direct_word.planes[2]);
    assert(direct_slot.logic9_plane3 == expected_direct_word.planes[3]);

    Process direct_read;
    direct_read.id = 108;
    direct_read.name = "logic9_direct_read";
    direct_read.register_count = 1;
    direct_read.register_value_kinds = { ValueKind::logic9 };
    direct_read.operations = {
        ReadSignal { 0, 1 },
        WriteBlocking { 0, 0 },
        Halt { }
    };
    const std::array<std::uint32_t, 2> direct_read_widths { 9U, 9U };
    const std::array<ValueKind, 2> direct_read_kinds {
        ValueKind::logic9, ValueKind::logic9
    };
    const auto direct_read_symbol
        = std::string { symbol } + "_direct_read";
    jit.add_process(
        direct_read_symbol, direct_read,
        direct_read_widths, direct_read_kinds);
    const auto direct_read_handle = jit.lookup(direct_read_symbol);
    assert((jit.frame_layout(direct_read_handle).direct_read_signals
        == std::vector<SignalId> { 1 }));

    const auto nine_states
        = PackedLogic4::from_logic9_msb_string("UX01ZWLH-");
    const auto direct_read_word = nine_states.logic9_low_word();
    TestRuntime direct_read_runtime;
    direct_read_runtime.logic9_signals[1]
        = planes(PackedLogic4::from_logic9_msb_string("XXXXXXXXX"));
    auto direct_read_descriptor = abi(direct_read_runtime);
    const std::array<std::uint32_t, 1> direct_read_map { 3U };
    std::array<std::array<std::uint64_t, 4>, 4> direct_read_planes { };
    for (std::size_t plane = 0; plane < 4U; ++plane) {
        direct_read_planes[plane][3] = direct_read_word.planes[plane];
    }
    direct_read_descriptor.direct_signal_logic9_plane0
        = direct_read_planes[0].data();
    direct_read_descriptor.direct_signal_logic9_plane1
        = direct_read_planes[1].data();
    direct_read_descriptor.direct_signal_logic9_plane2
        = direct_read_planes[2].data();
    direct_read_descriptor.direct_signal_logic9_plane3
        = direct_read_planes[3].data();
    direct_read_descriptor.direct_read_signals = direct_read_map.data();
    direct_read_descriptor.direct_read_signal_count = 1U;
    direct_read_descriptor.direct_signal_count = 4U;
    assert(jit.execute(direct_read_handle, direct_read_descriptor)
        == JitExecutionStatus::completed);
    assert(direct_read_runtime.logic9_signals[0] == planes(nine_states));

    const auto reserved_normalized = PackedLogic4::from_logic9_msb_string(
        "UUUUUUUUX");
    const auto reserved_logic9_symbol
        = std::string { symbol } + "_reserved_logic9_callback";
    Process reserved_logic9_callback;
    reserved_logic9_callback.id = 115;
    reserved_logic9_callback.name = "reserved_logic9_callback";
    reserved_logic9_callback.register_count = 1U;
    reserved_logic9_callback.register_value_kinds = { ValueKind::logic9 };
    reserved_logic9_callback.operations = {
        ReadSignal { 0U, 1U },
        WriteBlocking { 0U, 0U },
        Halt { }
    };
    const std::array<std::uint32_t, 2> reserved_widths { 9U, 9U };
    const std::array<ValueKind, 2> reserved_kinds {
        ValueKind::logic9, ValueKind::logic9
    };
    jit.add_process(
        reserved_logic9_symbol, reserved_logic9_callback,
        reserved_widths, reserved_kinds);
    TestRuntime reserved_callback_runtime;
    reserved_callback_runtime.logic9_signals[1] = { 1U, 1U, 1U, 1U };
    auto reserved_callback_descriptor = abi(reserved_callback_runtime);
    assert(jit.execute(
               jit.lookup(reserved_logic9_symbol),
               reserved_callback_descriptor) == JitExecutionStatus::completed);
    assert(reserved_callback_runtime.logic9_signals[0]
        == planes(reserved_normalized));

    const auto reserved_direct_symbol
        = std::string { symbol } + "_reserved_logic9_direct";
    Process reserved_logic9_direct = reserved_logic9_callback;
    reserved_logic9_direct.id = 116;
    reserved_logic9_direct.name = "reserved_logic9_direct";
    jit.add_process(
        reserved_direct_symbol, reserved_logic9_direct,
        reserved_widths, reserved_kinds);
    TestRuntime reserved_direct_runtime;
    auto reserved_direct_descriptor = abi(reserved_direct_runtime);
    std::array<std::array<std::uint64_t, 4>, 4> reserved_direct_planes { };
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        reserved_direct_planes[plane][2U] = 1U;
    }
    const std::array<std::uint32_t, 1> reserved_direct_map { 2U };
    reserved_direct_descriptor.direct_signal_logic9_plane0
        = reserved_direct_planes[0].data();
    reserved_direct_descriptor.direct_signal_logic9_plane1
        = reserved_direct_planes[1].data();
    reserved_direct_descriptor.direct_signal_logic9_plane2
        = reserved_direct_planes[2].data();
    reserved_direct_descriptor.direct_signal_logic9_plane3
        = reserved_direct_planes[3].data();
    reserved_direct_descriptor.direct_read_signals
        = reserved_direct_map.data();
    reserved_direct_descriptor.direct_read_signal_count = 1U;
    reserved_direct_descriptor.direct_signal_count = 3U;
    assert(jit.execute(
               jit.lookup(reserved_direct_symbol),
               reserved_direct_descriptor) == JitExecutionStatus::completed);
    assert(reserved_direct_runtime.logic9_signals[0]
        == planes(reserved_normalized));

    const auto malformed_dynamic_part_symbol
        = std::string { symbol } + "_malformed_dynamic_part_callback";
    Process malformed_dynamic_part;
    malformed_dynamic_part.id = 118U;
    malformed_dynamic_part.name = "malformed_dynamic_part_callback";
    malformed_dynamic_part.register_count = 3U;
    malformed_dynamic_part.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic9, ValueKind::logic9
    };
    malformed_dynamic_part.operations = {
        LoadConstant { 0U, PackedLogic4::from_aval_bval(32U, 64U, 0U) },
        ReadSignal { 1U, 0U },
        DynamicPartSelect {
            2U, 1U, 0U, 127, 0, 8U, true, true, false, 0U },
        WriteBlocking { 1U, 2U },
        Halt { }
    };
    const std::array<std::uint32_t, 2> malformed_dynamic_part_widths {
        128U, 8U
    };
    const std::array<ValueKind, 2> malformed_dynamic_part_kinds {
        ValueKind::logic9, ValueKind::logic9
    };
    LlvmJitOptions malformed_dynamic_part_options;
    malformed_dynamic_part_options.optimization = optimization;
    malformed_dynamic_part_options.debug_instrumentation = false;
    LlvmJit malformed_dynamic_part_jit {
        malformed_dynamic_part_options
    };
    malformed_dynamic_part_jit.add_process(
        malformed_dynamic_part_symbol,
        malformed_dynamic_part,
        malformed_dynamic_part_widths,
        malformed_dynamic_part_kinds);
    TestRuntime malformed_dynamic_part_runtime;
    malformed_dynamic_part_runtime.wide_signal_aval[0U] = { 0U, 0U };
    malformed_dynamic_part_runtime.wide_signal_bval[0U] = { 0U, 0U };
    malformed_dynamic_part_runtime.wide_signal_logic9_plane2[0U]
        = { 0U, 0U };
    malformed_dynamic_part_runtime.wide_signal_logic9_plane3[0U]
        = { 0U, 0U };
    auto malformed_dynamic_part_descriptor
        = abi(malformed_dynamic_part_runtime);
    auto malformed_dynamic_part_services
        = copy_jit_services(malformed_dynamic_part_descriptor);
    malformed_dynamic_part_services.read_signal_dynamic_part
        = &read_reserved_logic9_dynamic_part;
    malformed_dynamic_part_descriptor.services
        = &malformed_dynamic_part_services;
    assert(malformed_dynamic_part_jit.execute(
               malformed_dynamic_part_jit.lookup(
                   malformed_dynamic_part_symbol),
               malformed_dynamic_part_descriptor)
        == JitExecutionStatus::completed);
    assert(malformed_dynamic_part_runtime.dynamic_part_signal_reads == 1U);
    assert(malformed_dynamic_part_runtime.logic9_signals[1]
        == planes(PackedLogic4::from_logic9_msb_string("XXXXXXXX")));

    constexpr std::uint32_t malformed_wide_width = 129U;
    const auto malformed_wide_symbol
        = std::string { symbol } + "_malformed_wide_read";
    Process malformed_wide_read;
    malformed_wide_read.id = 117U;
    malformed_wide_read.name = "malformed_wide_logic9_read";
    malformed_wide_read.register_count = 1U;
    malformed_wide_read.register_value_kinds = { ValueKind::logic9 };
    malformed_wide_read.operations = {
        ReadSignal { 0U, 1U },
        WriteBlocking { 0U, 0U },
        Halt { },
    };
    const std::array<std::uint32_t, 2> malformed_wide_widths {
        malformed_wide_width, malformed_wide_width
    };
    const std::array<ValueKind, 2> malformed_wide_kinds {
        ValueKind::logic9, ValueKind::logic9
    };
    jit.add_process(
        malformed_wide_symbol, malformed_wide_read,
        malformed_wide_widths, malformed_wide_kinds);
    const auto malformed_wide_expected
        = PackedLogic4::from_logic9_msb_string(
            std::string(malformed_wide_width - 1U, 'U') + "X");
    const auto expected_wide_plane = [&malformed_wide_expected](
                                         const std::size_t plane) {
        const auto values
            = malformed_wide_expected.logic9_plane_words(plane);
        return std::vector<std::uint64_t>(
            values.begin(), values.end());
    };
    const auto require_normalized_wide_output =
        [&](const TestRuntime& runtime) {
            assert(runtime.wide_signal_aval[0]
                == expected_wide_plane(0U));
            assert(runtime.wide_signal_bval[0]
                == expected_wide_plane(1U));
            assert(runtime.wide_signal_logic9_plane2[0]
                == expected_wide_plane(2U));
            assert(runtime.wide_signal_logic9_plane3[0]
                == expected_wide_plane(3U));
        };

    TestRuntime malformed_wide_callback_runtime;
    malformed_wide_callback_runtime.wide_signal_aval[1U]
        = { 1U, 0U, 0U };
    malformed_wide_callback_runtime.wide_signal_bval[1U]
        = { 1U, 0U, 0U };
    malformed_wide_callback_runtime.wide_signal_logic9_plane2[1U]
        = { 1U, 0U, 0U };
    malformed_wide_callback_runtime.wide_signal_logic9_plane3[1U]
        = { 1U, 0U, 0U };
    auto malformed_wide_callback_descriptor
        = abi(malformed_wide_callback_runtime);
    // Exercise checked callback ingress independently of the direct-plane
    // malformed input below.
    assert(jit.execute(
               jit.lookup(malformed_wide_symbol),
               malformed_wide_callback_descriptor)
        == JitExecutionStatus::completed);
    assert(malformed_wide_callback_runtime.packed_signal_reads == 1U);
    require_normalized_wide_output(malformed_wide_callback_runtime);

    const auto malformed_wide_handle = jit.lookup(malformed_wide_symbol);
    assert((jit.frame_layout(malformed_wide_handle).direct_read_signals
        == std::vector<SignalId> { 1U }));
    TestRuntime malformed_wide_direct_runtime;
    auto malformed_wide_direct_descriptor
        = abi(malformed_wide_direct_runtime);
    const std::array<std::uint32_t, 1> malformed_wide_read_map { 1U };
    const std::array<std::uint32_t, 2> malformed_wide_offsets { 0U, 3U };
    const std::array<std::uint64_t, 6> malformed_wide_aval {
        0U, 0U, 0U, 1U, 0U, 0U
    };
    const std::array<std::uint64_t, 6> malformed_wide_bval {
        0U, 0U, 0U, 1U, 0U, 0U
    };
    const std::array<std::uint64_t, 6> malformed_wide_plane2 {
        0U, 0U, 0U, 1U, 0U, 0U
    };
    const std::array<std::uint64_t, 6> malformed_wide_plane3 {
        0U, 0U, 0U, 1U, 0U, 0U
    };
    malformed_wide_direct_descriptor.direct_read_signals
        = malformed_wide_read_map.data();
    malformed_wide_direct_descriptor.direct_read_signal_count = 1U;
    malformed_wide_direct_descriptor.direct_signal_count = 2U;
    malformed_wide_direct_descriptor.direct_wide_signal_aval
        = malformed_wide_aval.data();
    malformed_wide_direct_descriptor.direct_wide_signal_bval
        = malformed_wide_bval.data();
    malformed_wide_direct_descriptor.direct_wide_signal_logic9_plane2
        = malformed_wide_plane2.data();
    malformed_wide_direct_descriptor.direct_wide_signal_logic9_plane3
        = malformed_wide_plane3.data();
    malformed_wide_direct_descriptor.direct_wide_signal_offsets
        = malformed_wide_offsets.data();
    malformed_wide_direct_descriptor.direct_wide_signal_offset_count = 2U;
    malformed_wide_direct_descriptor.direct_wide_word_count = 6U;
    assert(jit.execute(
               malformed_wide_handle,
               malformed_wide_direct_descriptor)
        == JitExecutionStatus::completed);
    assert(malformed_wide_direct_runtime.packed_signal_reads == 0U);
    require_normalized_wide_output(malformed_wide_direct_runtime);

    const auto rhs_states = PackedLogic4::from_logic9_msb_string(
        "UX01ZWLH-");
    constexpr std::array<Logic9, 9> states {
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
    for (std::size_t left = 0; left < states.size(); ++left) {
        Process binary;
        binary.id = static_cast<ProcessId>(100U + left);
        binary.name = "logic9_binary_truth_table";
        binary.register_count = 5;
        binary.register_value_kinds.assign(5U, ValueKind::logic9);
        auto lhs_states = rhs_states;
        lhs_states.fill(states[left]);
        binary.operations = {
            LoadConstant { 0, lhs_states },
            LoadConstant { 1, rhs_states },
            Binary { BinaryOperator::bit_and, 2, 0, 1 },
            Binary { BinaryOperator::bit_or, 3, 0, 1 },
            Binary { BinaryOperator::bit_xor, 4, 0, 1 },
            WriteBlocking { 0, 2 },
            WriteBlocking { 1, 3 },
            WriteBlocking { 2, 4 },
            Halt { }
        };
        const auto binary_symbol = std::string { symbol }
            + "_binary_" + std::to_string(left);
        const std::array<std::uint32_t, 3> binary_widths { 9, 9, 9 };
        const std::array<ValueKind, 3> binary_kinds {
            ValueKind::logic9, ValueKind::logic9, ValueKind::logic9
        };
        assert(jit.supports_process(binary, binary_widths, binary_kinds));
        jit.add_process(
            binary_symbol, binary, binary_widths, binary_kinds);

        using Logic9Binary = Logic9 (*)(Logic9, Logic9) noexcept;
        auto expected_binary = [&](const Logic9Binary operation) {
            auto result = rhs_states;
            for (std::size_t bit = 0; bit < result.width(); ++bit) {
                result.set_logic9(
                    bit,
                    operation(
                        lhs_states.get_logic9(bit),
                        rhs_states.get_logic9(bit)));
            }
            return planes(result);
        };
        TestRuntime binary_runtime;
        auto binary_descriptor = abi(binary_runtime);
        assert(jit.execute(jit.lookup(binary_symbol), binary_descriptor)
            == JitExecutionStatus::completed);
        assert(binary_runtime.logic9_signals[0]
            == expected_binary(runtime::logic_and));
        assert(binary_runtime.logic9_signals[1]
            == expected_binary(runtime::logic_or));
        assert(binary_runtime.logic9_signals[2]
            == expected_binary(runtime::logic_xor));
    }

    auto missing_exact_callback = descriptor;
    auto missing_exact_services = copy_jit_services(missing_exact_callback);
    missing_exact_services.read_signal_logic9 = nullptr;
    missing_exact_callback.services = &missing_exact_services;
    expect_error(
        [&] {
            (void)jit.execute(
                handle, missing_exact_callback);
        },
        "require read_signal_logic9");

    Process wide;
    wide.id = 94;
    wide.name = "wide_logic9_alignment";
    wide.register_count = 3;
    wide.register_value_kinds = {
        ValueKind::logic9, ValueKind::logic9, ValueKind::logic9
    };
    std::string wide_digits;
    while (wide_digits.size() < 129U) {
        wide_digits += "UX01ZWLH-";
    }
    wide_digits.resize(129U);
    const auto wide_high = PackedLogic4::from_logic9_msb_string(wide_digits);
    const auto wide_low = PackedLogic4::from_logic9_msb_string(
        std::string(129, 'H'));
    wide.operations = {
        LoadConstant { 0, wide_high },
        LoadConstant { 1, wide_low },
        Binary { BinaryOperator::bit_and, 2, 0, 1 },
        Pause { },
        Halt { }
    };
    const auto wide_symbol = std::string { symbol } + "_wide_alignment";
    const std::array<std::uint32_t, 0> no_signals { };
    jit.add_process(wide_symbol, wide, no_signals);
    const auto wide_handle = jit.lookup(wide_symbol);
    const auto wide_layout = jit.frame_layout(wide_handle);
    assert((wide_layout.register_word_offsets
        == std::vector<std::uint32_t> { 0, 3, 6 }));
    assert(wide_layout.register_word_count == 9);

    alignas(64) std::array<std::uint64_t, 10> wide_aval { };
    alignas(64) std::array<std::uint64_t, 10> wide_bval { };
    alignas(64) std::array<std::uint64_t, 10> wide_plane2 { };
    alignas(64) std::array<std::uint64_t, 10> wide_plane3 { };
    std::array<std::uint8_t, 3> wide_initialized { };
    const auto shifted = [](auto& storage) {
        return std::span { storage }.subspan(1);
    };
    fsim_jit_frame_v2 wide_frame { };
    jit.initialize_frame(
        wide_handle,
        wide_frame,
        shifted(wide_aval),
        shifted(wide_bval),
        wide_initialized,
        shifted(wide_plane2),
        shifted(wide_plane3));
    assert(wide_frame.register_aval == wide_aval.data() + 1);
    assert(wide_frame.register_logic9_plane2 == wide_plane2.data() + 1);

    TestRuntime wide_runtime;
    auto wide_descriptor = abi(wide_runtime);
    auto wide_result = new_resume_result();
    assert(jit.resume(
               wide_handle, wide_descriptor, wide_frame, wide_result)
        == JitResumeStatus::paused);
    assert((wide_initialized == std::array<std::uint8_t, 3> { 1, 1, 1 }));
    const auto expect_wide_planes = [&](const std::size_t register_id,
                                        const PackedLogic4& expected) {
        const auto offset = 1U + wide_layout.register_word_offsets[register_id];
        const std::array<const std::uint64_t*, 4> storage {
            wide_aval.data(), wide_bval.data(),
            wide_plane2.data(), wide_plane3.data()
        };
        for (std::size_t bit = 0; bit < expected.width(); ++bit) {
            const auto encoded = static_cast<std::uint8_t>(
                expected.get_logic9(bit));
            for (std::size_t plane = 0; plane < storage.size(); ++plane) {
                assert(((storage[plane][offset + bit / 64U]
                            >> (bit % 64U))
                           & 1U)
                    == ((encoded >> plane) & 1U));
            }
        }
    };
    expect_wide_planes(0, wide_high);
    expect_wide_planes(1, wide_low);
    assert(jit.resume(
               wide_handle, wide_descriptor, wide_frame, wide_result)
        == JitResumeStatus::completed);

    Process branch_definitions_across_word;
    branch_definitions_across_word.id = 109;
    branch_definitions_across_word.name
        = "branch_definitions_across_word";
    branch_definitions_across_word.register_count = 65;
    branch_definitions_across_word.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        Branch { 0, 2, 4, UnknownBranchPolicy::when_false },
        LoadConstant {
            64, PackedLogic4::from_msb_string("10100101") },
        Jump { 6 },
        LoadConstant {
            64, PackedLogic4::from_msb_string("01011010") },
        Jump { 6 },
        CopyRegister { 63, 64 },
        WriteBlocking { 0, 63 },
        Pause { },
        Halt { },
    };
    const auto branch_definitions_symbol
        = std::string { symbol } + "_branch_definitions_word_boundary";
    const std::array<std::uint32_t, 1> branch_signal_widths { 8U };
    const std::array<ValueKind, 1> branch_signal_kinds {
        ValueKind::logic4
    };
    const auto verify_branch_definition_output =
        [&](const Process& branch_process, const std::string& process_symbol) {
            assert(jit.supports_process(
                branch_process, branch_signal_widths, branch_signal_kinds));
            jit.add_process(
                process_symbol,
                branch_process,
                branch_signal_widths,
                branch_signal_kinds);
            const auto branch_handle = jit.lookup(process_symbol);
            const auto branch_layout = jit.frame_layout(branch_handle);
            std::vector<std::uint64_t> branch_aval(
                branch_layout.register_count);
            std::vector<std::uint64_t> branch_bval(
                branch_layout.register_count);
            std::vector<std::uint8_t> branch_initialized(
                branch_layout.register_count);
            fsim_jit_frame_v2 branch_frame { };
            jit.initialize_frame(
                branch_handle,
                branch_frame,
                branch_aval,
                branch_bval,
                branch_initialized);
            TestRuntime branch_runtime;
            auto branch_descriptor = abi(branch_runtime);
            auto branch_result = new_resume_result();
            assert(jit.resume(
                       branch_handle,
                       branch_descriptor,
                       branch_frame,
                       branch_result)
                == JitResumeStatus::paused);
            assert(branch_runtime.signals[0]
                == encode(PackedLogic4::from_msb_string("10100101")));
            assert(jit.resume(
                       branch_handle,
                       branch_descriptor,
                       branch_frame,
                       branch_result)
                == JitResumeStatus::completed);
        };
    verify_branch_definition_output(
        branch_definitions_across_word, branch_definitions_symbol);

    Process branch_definitions_across_two_words;
    branch_definitions_across_two_words.id = 110;
    branch_definitions_across_two_words.name
        = "branch_definitions_across_two_words";
    branch_definitions_across_two_words.register_count = 133;
    branch_definitions_across_two_words.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        Branch { 0, 2, 4, UnknownBranchPolicy::when_false },
        LoadConstant {
            132, PackedLogic4::from_msb_string("10100101") },
        Jump { 6 },
        LoadConstant {
            132, PackedLogic4::from_msb_string("10100101") },
        Jump { 6 },
        CopyRegister { 131, 132 },
        WriteBlocking { 0, 131 },
        Pause { },
        Halt { },
    };
    const auto branch_definitions_two_words_symbol
        = std::string { symbol } + "_branch_definitions_two_words";
    verify_branch_definition_output(
        branch_definitions_across_two_words,
        branch_definitions_two_words_symbol);

    Process partial_word_path_use;
    partial_word_path_use.id = 111;
    partial_word_path_use.name = "partial_word_path_use";
    partial_word_path_use.register_count = 133;
    partial_word_path_use.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        Branch { 0, 2, 4, UnknownBranchPolicy::when_false },
        LoadConstant {
            132, PackedLogic4::from_msb_string("10100101") },
        Jump { 6 },
        Jump { 6 },
        Halt { },
        CopyRegister { 131, 132 },
        CopyRegister { 130, 132 },
        Halt { },
    };
    const auto partial_word_path_use_symbol
        = std::string { symbol } + "_partial_word_path_use";
    LlvmJit invalid_path_jit { LlvmJitOptions { optimization, { } } };
    expect_fatal_error(
        [&] {
            invalid_path_jit.add_process(
                partial_word_path_use_symbol,
                partial_word_path_use,
                no_signals);
        },
        "instruction 6: register 132 may be used before definition on a control-flow path");
}

void test_vital_timing_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 92;
    process.name = "vital_timing";
    process.register_count = 1;
    process.register_value_kinds = { ValueKind::logic9 };
    VitalTimingCheck check;
    check.destination = 0;
    check.kind = VitalTimingCheckKind::period_pulse;
    check.test_signal = 0;
    process.operations = { check, WriteBlocking { 1, 0 }, Halt { } };

    const std::array<std::uint32_t, 2> widths { 1, 1 };
    const std::array<ValueKind, 2> kinds {
        ValueKind::logic9, ValueKind::logic9
    };
    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, widths, kinds));
    jit.add_process(symbol, process, widths, kinds);
    TestRuntime runtime;
    runtime.vital_timing_result = static_cast<std::uint32_t>(Logic9::x);
    auto descriptor = abi(runtime);
    assert(
        jit.execute(jit.lookup(symbol), descriptor)
        == JitExecutionStatus::completed);
    assert(
        runtime.logic9_signals[1]
        == planes(PackedLogic4::from_logic9_msb_string("X")));

    Process wide_check = process;
    wide_check.id = 93;
    wide_check.name = "vital_timing_high_bit";
    auto* wide_operation = operation_get_if<VitalTimingCheck>(
        &wide_check.operations[0]);
    assert(wide_operation);
    wide_operation->test_offset = 96U;
    wide_operation->reference_signal = 2U;
    wide_operation->reference_offset = 127U;
    const std::array<std::uint32_t, 3> wide_widths { 129U, 1U, 129U };
    const std::array<ValueKind, 3> wide_kinds {
        ValueKind::logic9, ValueKind::logic9, ValueKind::logic9
    };
    const auto wide_symbol = std::string { symbol } + "_high_bit";
    assert(jit.supports_process(wide_check, wide_widths, wide_kinds));
    jit.add_process(wide_symbol, wide_check, wide_widths, wide_kinds);
    TestRuntime wide_runtime;
    wide_runtime.vital_timing_result
        = static_cast<std::uint32_t>(Logic9::h);
    auto wide_descriptor = abi(wide_runtime);
    assert(jit.execute(jit.lookup(wide_symbol), wide_descriptor)
        == JitExecutionStatus::completed);
    assert(wide_runtime.logic9_signals[1]
        == planes(PackedLogic4::from_logic9_msb_string("H")));
}

void test_vital_delay_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 93;
    process.name = "vital_delay";
    process.register_count = 8;
    process.register_value_kinds.assign(8U, ValueKind::logic4);
    process.register_value_kinds[0] = ValueKind::logic9;
    process.register_value_kinds[7] = ValueKind::logic9;
    process.operations.emplace_back(LoadConstant {
        0, PackedLogic4::from_logic9_msb_string("1") });
    for (RegisterId index = 1U; index <= 6U; ++index) {
        process.operations.emplace_back(LoadConstant {
            index, PackedLogic4::from_aval_bval(64U, index, 0U) });
    }
    process.operations.emplace_back(LoadConstant {
        7, PackedLogic4::from_logic9_msb_string("UX01ZWLH-") });
    VitalDelay delay;
    delay.kind = VitalDelayKind::wire;
    delay.shape = VitalDelayShape::delay01z;
    delay.output = 0U;
    delay.source = 0U;
    delay.output_map = 7U;
    delay.default_delays = { 1U, 2U, 3U, 4U, 5U, 6U };
    process.operations.emplace_back(delay);
    process.operations.emplace_back(Halt { });

    const std::array<std::uint32_t, 1> widths { 1U };
    const std::array<ValueKind, 1> kinds { ValueKind::logic9 };
    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, widths, kinds));
    jit.add_process(symbol, process, widths, kinds);
    const auto handle = jit.lookup(symbol);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    assert((
        runtime.vital_delay_calls
        == std::vector<std::pair<std::uint32_t, std::uint32_t>> { { 93U, 8U } }));

    auto missing_callback = descriptor;
    auto missing_callback_services = copy_jit_services(descriptor);
    missing_callback_services.vital_delay = nullptr;
    missing_callback.services = &missing_callback_services;
    expect_error(
        [&] { (void)jit.execute(handle, missing_callback); },
        "require vital_delay");
}

void test_rejections()
{
    LlvmJit jit;
    const std::array<std::uint32_t, 1> one_signal { 1 };
    const std::array<std::uint32_t, 0> no_signals { };

    Process empty_call_stack;
    empty_call_stack.name = "empty_call_stack";
    empty_call_stack.register_count = 2;
    empty_call_stack.operations = {
        Call { 1, 1, CallStack { 1, 0, 0 } },
        Halt { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "empty_call_stack", empty_call_stack, no_signals);
        },
        "dynamic call stack must not name fixed registers");

    Process oversized_call_stack;
    oversized_call_stack.name = "oversized_call_stack";
    oversized_call_stack.register_count = 2;
    oversized_call_stack.operations = {
        Return { CallStack { 0, 1, 2 } },
        Halt { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "oversized_call_stack",
                oversized_call_stack,
                no_signals);
        },
        "call-stack register range is outside register_count");

    Process invalid_call_target;
    invalid_call_target.name = "invalid_call_target";
    invalid_call_target.register_count = 2;
    invalid_call_target.operations = {
        Call { 99, 1, CallStack { 0, 1, 1 } },
        Halt { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "invalid_call_target",
                invalid_call_target,
                no_signals);
        },
        "call target is outside the operation stream");

    Process invalid_return_target;
    invalid_return_target.name = "invalid_return_target";
    invalid_return_target.register_count = 2;
    invalid_return_target.operations = {
        Call { 1, 99, CallStack { 0, 1, 1 } },
        Halt { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "invalid_return_target",
                invalid_return_target,
                no_signals);
        },
        "call return target is outside the operation stream");

    Process inverted_integer_check;
    inverted_integer_check.name = "inverted_integer_check";
    inverted_integer_check.register_count = 1;
    inverted_integer_check.operations = {
        LoadConstant {
            0,
            PackedLogic4::from_aval_bval(32, 0, 0) },
        IntegerCheck { 0, 2, -2 },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "inverted_integer_check",
                inverted_integer_check,
                no_signals);
        },
        "IntegerCheck has an inverted range");

    Process narrow_integer_operation;
    narrow_integer_operation.name = "narrow_integer_operation";
    narrow_integer_operation.register_count = 3;
    narrow_integer_operation.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("00000001") },
        LoadConstant { 1, PackedLogic4::from_msb_string("00000010") },
        IntegerBinary { IntegerBinaryOperator::add, 2, 0, 1 },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "narrow_integer_operation",
                narrow_integer_operation,
                no_signals);
        },
        "IntegerBinary requires 32- or 64-bit operands");

    const auto dynamic_part_process =
        [](const DynamicPartSelect selection) {
            Process process;
            process.name = "invalid_dynamic_part_select";
            process.register_count = 3;
            process.operations = {
                LoadConstant {
                    0,
                    PackedLogic4::from_msb_string("1010101111001101") },
                LoadConstant {
                    1,
                    PackedLogic4::from_aval_bval(32, 4, 0) },
                selection,
                Halt { }
            };
            return process;
        };
    expect_error(
        [&] {
            jit.add_process(
                "zero_width_dynamic_part",
                dynamic_part_process(
                    DynamicPartSelect {
                        2, 0, 1, 15, 0, 0, true, true, false }),
                no_signals);
        },
        "DynamicPartSelect width must be greater than zero");
    jit.add_process(
        "off_range_dynamic_part",
        dynamic_part_process(
            DynamicPartSelect {
                2, 0, 1, 15, 0, 17, true, true, false }),
        no_signals);
    expect_error(
        [&] {
            jit.add_process(
                "unrepresentable_dynamic_part",
                dynamic_part_process(
                    DynamicPartSelect {
                        2,
                        0,
                        1,
                        std::int64_t { 1 } << 32,
                        0,
                        4,
                        true,
                        true,
                        false }),
                no_signals);
        },
        "DynamicPartSelect bounds must fit signed 32-bit integers");
    expect_error(
        [&] {
            jit.add_process(
                "mismatched_dynamic_part_range",
                dynamic_part_process(
                    DynamicPartSelect {
                        2, 0, 1, 7, 0, 4, true, true, false, 12 }),
                no_signals);
        },
        "DynamicPartSelect declared range is outside its source register");
    const auto dynamic_part_write_process =
        [](const Operation operation) {
            Process process;
            process.name = "invalid_dynamic_part_write";
            process.register_count = 4;
            process.operations = {
                LoadConstant {
                    0,
                    PackedLogic4::from_msb_string("1010101111001101") },
                LoadConstant {
                    1, PackedLogic4::from_msb_string("1100") },
                LoadConstant {
                    2, PackedLogic4::from_aval_bval(32, 4, 0) },
                operation,
                Halt { }
            };
            return process;
        };
    expect_error(
        [&] {
            jit.add_process(
                "unrepresentable_dynamic_part_container_write",
                dynamic_part_write_process(
                    WriteContainerObjectElement {
                        0,
                        2,
                        1,
                        false,
                        false,
                        true,
                        std::nullopt,
                        DynamicPartIndex {
                            2,
                            std::int64_t { 1 } << 32,
                            0,
                            0,
                            4,
                            true,
                            true,
                        },
                    }),
                no_signals);
        },
        "dynamic part-select write bounds must fit signed 32-bit integers");
    expect_error(
        [&] {
            jit.add_process(
                "zero_width_dynamic_part_insert",
                dynamic_part_write_process(
                    DynamicPartInsert {
                        3, 0, 1,
                        DynamicPartIndex {
                            2, 7, 0, 0, 0, true, true } }),
                no_signals);
        },
        "dynamic part-select write width must be greater than zero");
    expect_error(
        [&] {
            jit.add_process(
                "outside_dynamic_part_insert",
                dynamic_part_write_process(
                    DynamicPartInsert {
                        3, 0, 1,
                        DynamicPartIndex {
                            2, 7, 0, 12, 4, true, true } }),
                no_signals);
        },
        "dynamic part-select write range is outside its packed target");
    const std::array<std::uint32_t, 1> wide_signal { 16 };
    expect_error(
        [&] {
            jit.add_process(
                "outside_dynamic_part_signal_write",
                dynamic_part_write_process(
                    WriteBlockingDynamicPartSlice {
                        0, 1,
                        DynamicPartIndex {
                            2, 7, 0, 12, 4, true, true } }),
                wide_signal);
        },
        "dynamic part-select write range is outside its packed target");

    const auto force_release_process =
        [](const Operation operation) {
            Process process;
            process.name = "invalid_force_release";
            process.register_count = 1;
            process.operations = {
                LoadConstant {
                    0, PackedLogic4::from_msb_string("1010") },
                operation,
                Halt { }
            };
            return process;
        };
    const std::array<std::uint32_t, 1> four_bit_signal { 4 };
    expect_error(
        [&] {
            jit.add_process(
                "outside_force_slice",
                force_release_process(
                    ForceSignalSlice { 0, 0, 1, std::nullopt }),
                four_bit_signal);
        },
        "ForceSignalSlice range is outside its signal");
    expect_error(
        [&] {
            jit.add_process(
                "zero_width_release_slice",
                force_release_process(
                    ReleaseSignalSlice { 0, 0, 0, std::nullopt }),
                four_bit_signal);
        },
        "ReleaseSignalSlice width must be greater than zero");
    expect_error(
        [&] {
            jit.add_process(
                "outside_release_slice",
                force_release_process(
                    ReleaseSignalSlice { 0, 3, 2, std::nullopt }),
                four_bit_signal);
        },
        "ReleaseSignalSlice range is outside its signal");
    const auto invalid_profile_process =
        [&](const std::uint32_t width,
            const ExpressionSizingKind sizing,
            const ExpressionValueDomain domain) {
            auto process = dynamic_part_process(
                DynamicPartSelect {
                    2, 0, 1, 15, 0, 4, true, true, false });
            process.expression_profiles = {
                ExpressionProfile {
                    SourceLocation { "invalid-profile.sv", 3, 7 },
                    width,
                    false,
                    sizing,
                    domain }
            };
            return process;
        };
    (void)jit.add_process(
        "zero_width_expression_profile",
        invalid_profile_process(
            0,
            ExpressionSizingKind::self_determined,
            ExpressionValueDomain::four_state),
        no_signals);
    expect_error(
        [&] {
            jit.add_process(
                "invalid_expression_sizing",
                invalid_profile_process(
                    4,
                    static_cast<ExpressionSizingKind>(99),
                    ExpressionValueDomain::four_state),
                no_signals);
        },
        "expression profile has an invalid sizing kind");
    expect_error(
        [&] {
            jit.add_process(
                "invalid_expression_domain",
                invalid_profile_process(
                    4,
                    ExpressionSizingKind::context_determined,
                    static_cast<ExpressionValueDomain>(99)),
                no_signals);
        },
        "expression profile has an invalid value domain");

    ContainerType locator_queue;
    locator_queue.element_width = 8;
    locator_queue.queue = true;
    Process invalid_container_predicate;
    invalid_container_predicate.name = "invalid_container_predicate";
    invalid_container_predicate.container_register_count = 2;
    invalid_container_predicate.container_register_types = {
        locator_queue, locator_queue
    };
    invalid_container_predicate.operations = {
        LocateContainer {
            ContainerLocatorOperator::find, 0, 1, { }, { } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_container_predicate",
                invalid_container_predicate,
                no_signals);
        },
        "invalid predicate metadata");

    auto invalid_index_type = invalid_container_predicate;
    invalid_index_type.name = "invalid_index_type";
    invalid_index_type.operations = {
        LocateContainer {
            ContainerLocatorOperator::find, 0, 1,
            { { ContainerPredicateOperator::index, 0, 0,
                PackedLogic4 { },
                ContainerPredicateValueKind::element } },
            { } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_index_type", invalid_index_type,
                no_signals);
        },
        "predicate index has the wrong type");

    auto mixed_predicate_types = invalid_container_predicate;
    mixed_predicate_types.name = "mixed_predicate_types";
    mixed_predicate_types.operations = {
        LocateContainer {
            ContainerLocatorOperator::find, 0, 1,
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::index, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::index },
                { ContainerPredicateOperator::equal, 0, 1,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::logical },
            },
            { } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "mixed_predicate_types", mixed_predicate_types,
                no_signals);
        },
        "comparison operands are invalid");

    Process invalid_reduction_root;
    invalid_reduction_root.name = "invalid_reduction_root";
    invalid_reduction_root.register_count = 1;
    invalid_reduction_root.container_register_count = 1;
    invalid_reduction_root.container_register_types = {
        locator_queue
    };
    invalid_reduction_root.operations = {
        ContainerReduction {
            ContainerReductionOperator::sum, 0, 0,
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::constant, 0, 0,
                    PackedLogic4::from_aval_bval(8, 0, 0),
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::greater, 0, 1,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::logical },
            } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_reduction_root",
                invalid_reduction_root, no_signals);
        },
        "transformation root has the wrong type");

    auto invalid_reduction_conditional = invalid_reduction_root;
    invalid_reduction_conditional.name = "invalid_reduction_conditional";
    invalid_reduction_conditional.operations = {
        ContainerReduction {
            ContainerReductionOperator::sum, 0, 0,
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::constant, 0, 0,
                    PackedLogic4::from_aval_bval(8, 0, 0),
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::conditional, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element, 99 },
            } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_reduction_conditional",
                invalid_reduction_conditional, no_signals);
        },
        "conditional operands are invalid");

    auto invalid_reduction_branch = invalid_reduction_root;
    invalid_reduction_branch.name = "invalid_reduction_branch";
    invalid_reduction_branch.operations = {
        ContainerReduction {
            ContainerReductionOperator::sum, 0, 0,
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::index, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::index },
                { ContainerPredicateOperator::conditional, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element, 1 },
            } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_reduction_branch",
                invalid_reduction_branch, no_signals);
        },
        "conditional operands are invalid");

    auto oversized_reduction = invalid_reduction_root;
    oversized_reduction.name = "oversized_reduction";
    std::vector<ContainerPredicateNode> oversized_graph(
        maximum_container_predicate_nodes + 1U,
        ContainerPredicateNode {
            ContainerPredicateOperator::item,
            0,
            0,
            PackedLogic4 { },
            ContainerPredicateValueKind::element });
    oversized_reduction.operations = {
        ContainerReduction {
            ContainerReductionOperator::sum, 0, 0,
            std::move(oversized_graph) },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "oversized_reduction",
                oversized_reduction, no_signals);
        },
        "invalid transformation metadata");

    Process invalid_ordering_root;
    invalid_ordering_root.name = "invalid_ordering_root";
    invalid_ordering_root.container_register_count = 1;
    invalid_ordering_root.container_register_types = {
        locator_queue
    };
    invalid_ordering_root.operations = {
        OrderContainer {
            ContainerOrderingOperator::ascending, 0,
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::constant, 0, 0,
                    PackedLogic4::from_aval_bval(8, 0, 0),
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::greater, 0, 1,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::logical },
            } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_ordering_root",
                invalid_ordering_root, no_signals);
        },
        "key root has the wrong type");

    auto invalid_reverse_key = invalid_ordering_root;
    invalid_reverse_key.name = "invalid_reverse_key";
    invalid_reverse_key.operations = {
        OrderContainer {
            ContainerOrderingOperator::reverse, 0,
            { { ContainerPredicateOperator::item, 0, 0,
                PackedLogic4 { },
                ContainerPredicateValueKind::element } } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_reverse_key",
                invalid_reverse_key, no_signals);
        },
        "key metadata requires sort or rsort");

    auto invalid_ordering_conditional = invalid_ordering_root;
    invalid_ordering_conditional.name = "invalid_ordering_conditional";
    invalid_ordering_conditional.operations = {
        OrderContainer {
            ContainerOrderingOperator::ascending, 0,
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::conditional, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element, 99 },
            } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_ordering_conditional",
                invalid_ordering_conditional, no_signals);
        },
        "conditional operands are invalid");

    auto oversized_ordering = invalid_ordering_root;
    oversized_ordering.name = "oversized_ordering";
    std::vector<ContainerPredicateNode> oversized_ordering_graph(
        maximum_container_predicate_nodes + 1U,
        ContainerPredicateNode {
            ContainerPredicateOperator::item,
            0,
            0,
            PackedLogic4 { },
            ContainerPredicateValueKind::element });
    oversized_ordering.operations = {
        OrderContainer {
            ContainerOrderingOperator::ascending, 0,
            std::move(oversized_ordering_graph) },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "oversized_ordering",
                oversized_ordering, no_signals);
        },
        "invalid key metadata");

    Process invalid_locator_transformation_root;
    invalid_locator_transformation_root.name = "invalid_locator_transformation_root";
    invalid_locator_transformation_root.container_register_count = 2;
    invalid_locator_transformation_root.container_register_types = {
        locator_queue, locator_queue
    };
    invalid_locator_transformation_root.operations = {
        LocateContainer {
            ContainerLocatorOperator::minimum, 0, 1, { },
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::constant, 0, 0,
                    PackedLogic4::from_aval_bval(8, 0, 0),
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::greater, 0, 1,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::logical },
            } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_locator_transformation_root",
                invalid_locator_transformation_root, no_signals);
        },
        "transformation root has the wrong type");

    auto invalid_find_transformation = invalid_locator_transformation_root;
    invalid_find_transformation.name = "invalid_find_transformation";
    invalid_find_transformation.operations = {
        LocateContainer {
            ContainerLocatorOperator::find, 0, 1,
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::constant, 0, 0,
                    PackedLogic4::from_aval_bval(8, 0, 0),
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::greater, 0, 1,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::logical },
            },
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
            } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_find_transformation",
                invalid_find_transformation, no_signals);
        },
        "transformation metadata requires");

    auto invalid_locator_conditional = invalid_locator_transformation_root;
    invalid_locator_conditional.name = "invalid_locator_conditional";
    invalid_locator_conditional.operations = {
        LocateContainer {
            ContainerLocatorOperator::unique, 0, 1, { },
            {
                { ContainerPredicateOperator::item, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element },
                { ContainerPredicateOperator::conditional, 0, 0,
                    PackedLogic4 { },
                    ContainerPredicateValueKind::element, 99 },
            } },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "invalid_locator_conditional",
                invalid_locator_conditional, no_signals);
        },
        "conditional operands are invalid");

    auto oversized_locator_transformation = invalid_locator_transformation_root;
    oversized_locator_transformation.name = "oversized_locator_transformation";
    std::vector<ContainerPredicateNode> oversized_locator_graph(
        maximum_container_predicate_nodes + 1U,
        ContainerPredicateNode {
            ContainerPredicateOperator::item,
            0,
            0,
            PackedLogic4 { },
            ContainerPredicateValueKind::element });
    oversized_locator_transformation.operations = {
        LocateContainer {
            ContainerLocatorOperator::maximum, 0, 1, { },
            std::move(oversized_locator_graph) },
        Halt { }
    };
    expect_error(
        [&] {
            jit.add_process(
                "oversized_locator_transformation",
                oversized_locator_transformation, no_signals);
        },
        "invalid transformation metadata");

    const auto projected_process =
        [](const ProjectedDelayMode mode,
            const std::uint64_t delay,
            const std::uint64_t rejection) {
            Process process;
            process.name = "invalid_projected";
            process.register_count = 1;
            process.operations = {
                LoadConstant {
                    0, PackedLogic4::from_msb_string("1") },
                WriteProjected {
                    0, 0, delay, rejection, mode },
                Halt { },
            };
            return process;
        };
    expect_error(
        [&] {
            jit.add_process(
                "projected_rejection_too_large",
                projected_process(
                    ProjectedDelayMode::inertial, 2, 3),
                one_signal);
        },
        "rejection exceeds");
    expect_error(
        [&] {
            jit.add_process(
                "transport_with_rejection",
                projected_process(
                    ProjectedDelayMode::transport, 2, 1),
                one_signal);
        },
        "transport projected write");
    expect_error(
        [&] {
            jit.add_process(
                "invalid_projected_mode",
                projected_process(
                    static_cast<ProjectedDelayMode>(99), 2, 0),
                one_signal);
        },
        "invalid delay mode");
    Process unordered_waveform;
    unordered_waveform.name = "unordered_projected_waveform";
    unordered_waveform.register_count = 2;
    unordered_waveform.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        LoadConstant { 1, PackedLogic4::from_msb_string("0") },
        WriteProjectedWaveform {
            0,
            { { 0, 5 }, { 1, 5 } },
            0,
            ProjectedDelayMode::transport },
        Halt { },
    };
    expect_error(
        [&] {
            jit.add_process(
                "unordered_projected_waveform",
                unordered_waveform,
                one_signal);
        },
        "strictly ascending");

    Process empty_wait_on;
    empty_wait_on.id = 0;
    empty_wait_on.name = "empty_wait_on";
    empty_wait_on.operations = { WaitOn { }, Halt { } };
    expect_fatal_error(
        [&] { jit.add_process("empty_wait_on", empty_wait_on, one_signal); },
        "WaitOn requires at least one signal");

    Process timeout_metadata_without_timeout;
    timeout_metadata_without_timeout.id = 0;
    timeout_metadata_without_timeout.name = "timeout_metadata_without_timeout";
    timeout_metadata_without_timeout.register_count = 1;
    WaitOn incomplete_timeout { { 0 } };
    incomplete_timeout.timeout_result = 0;
    timeout_metadata_without_timeout.operations = {
        std::move(incomplete_timeout), Halt { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "timeout_metadata_without_timeout",
                timeout_metadata_without_timeout,
                one_signal);
        },
        "WaitOn timeout metadata requires a timeout");

    Process mismatched_timeout_rearm;
    mismatched_timeout_rearm.id = 0;
    mismatched_timeout_rearm.name = "mismatched_timeout_rearm";
    mismatched_timeout_rearm.register_count = 1;
    WaitOn timeout_origin { { 0 } };
    timeout_origin.timeout = 2;
    timeout_origin.timeout_result = 0;
    WaitOn timeout_rearm { { 0 } };
    timeout_rearm.timeout = 3;
    timeout_rearm.timeout_result = 0;
    timeout_rearm.timeout_origin = 0;
    mismatched_timeout_rearm.operations = {
        std::move(timeout_origin),
        std::move(timeout_rearm),
        Halt { },
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "mismatched_timeout_rearm",
                mismatched_timeout_rearm,
                one_signal);
        },
        "WaitOn timeout rearm does not match its origin");

    Process mismatched_wait_edges;
    mismatched_wait_edges.id = 0;
    mismatched_wait_edges.name = "mismatched_wait_edges";
    mismatched_wait_edges.operations = {
        WaitOn { { 0 }, { EdgeKind::posedge, EdgeKind::negedge } }, Halt { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "mismatched_wait_edges", mismatched_wait_edges,
                one_signal);
        },
        "WaitOn edge count must match its signal count");

    Process empty_static_wait;
    empty_static_wait.id = 0;
    empty_static_wait.name = "empty_static_wait";
    empty_static_wait.operations = { WaitSensitivity { }, Halt { } };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "empty_static_wait", empty_static_wait, one_signal);
        },
        "WaitSensitivity requires a static sensitivity list");

    Process invalid_static_signal;
    invalid_static_signal.id = 0;
    invalid_static_signal.name = "invalid_static_signal";
    invalid_static_signal.static_sensitivity = { { 1, EdgeKind::any } };
    invalid_static_signal.operations = { Halt { } };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "invalid_static_signal", invalid_static_signal, one_signal);
        },
        "signal ID is outside signal_widths");

    Process vector_edge;
    vector_edge.id = 0;
    vector_edge.name = "vector_edge";
    vector_edge.static_sensitivity = { { 0, EdgeKind::posedge } };
    vector_edge.operations = { WaitSensitivity { }, Halt { } };
    const std::array<std::uint32_t, 1> vector_signal { 8 };
    expect_fatal_error(
        [&] { jit.add_process("vector_edge", vector_edge, vector_signal); },
        "edge sensitivity requires a scalar signal");

    Process invalid_edge;
    invalid_edge.id = 0;
    invalid_edge.name = "invalid_edge";
    invalid_edge.static_sensitivity = {
        { 0, static_cast<EdgeKind>(UINT8_MAX) }
    };
    invalid_edge.operations = { WaitSensitivity { }, Halt { } };
    expect_fatal_error(
        [&] { jit.add_process("invalid_edge", invalid_edge, one_signal); },
        "static sensitivity has an invalid edge kind");

    Process vector_dynamic_edge;
    vector_dynamic_edge.id = 0;
    vector_dynamic_edge.name = "vector_dynamic_edge";
    vector_dynamic_edge.operations = {
        WaitOn { { 0 }, { EdgeKind::posedge } }, Halt { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "vector_dynamic_edge", vector_dynamic_edge,
                vector_signal);
        },
        "WaitOn edge requires a scalar signal");

    Process invalid_dynamic_edge;
    invalid_dynamic_edge.id = 0;
    invalid_dynamic_edge.name = "invalid_dynamic_edge";
    invalid_dynamic_edge.operations = {
        WaitOn { { 0 }, { static_cast<EdgeKind>(UINT8_MAX) } }, Halt { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "invalid_dynamic_edge", invalid_dynamic_edge,
                one_signal);
        },
        "WaitOn has an invalid edge kind");

    Process zero_width_wait;
    zero_width_wait.id = 0;
    zero_width_wait.name = "zero_width_wait";
    zero_width_wait.operations = { WaitOn { { 0 } }, Halt { } };
    const std::array<std::uint32_t, 1> zero_width_signal { 0 };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "zero_width_wait", zero_width_wait, zero_width_signal);
        },
        "signal width must be greater than zero");

    const std::array<std::uint32_t, 1> scalar_signal { 1 };
    std::uint32_t scheduled_suffix = 0;
    for (const auto& operation :
        std::array<Operation, 2> { WriteUpdate { 0, 1 },
            WriteAfter { 0, 1, UINT64_MAX } }) {
        Process bad_source;
        bad_source.id = 0;
        bad_source.name = "scheduled_bad_source";
        bad_source.register_count = 1;
        bad_source.operations = { operation, Halt { } };
        expect_fatal_error(
            [&] {
                jit.add_process(
                    "scheduled_bad_source_" + std::to_string(scheduled_suffix++),
                    bad_source, scalar_signal);
            },
            "source register ID is out of range");
    }
    for (const auto& operation :
        std::array<Operation, 2> { WriteUpdate { 1, 0 },
            WriteAfter { 1, 0, UINT64_MAX } }) {
        Process bad_signal;
        bad_signal.id = 0;
        bad_signal.name = "scheduled_bad_signal";
        bad_signal.register_count = 1;
        bad_signal.operations = {
            LoadConstant { 0, PackedLogic4::from_msb_string("0") },
            operation,
            Halt { },
        };
        expect_fatal_error(
            [&] {
                jit.add_process(
                    "scheduled_bad_signal_" + std::to_string(scheduled_suffix++),
                    bad_signal, scalar_signal);
            },
            "signal ID is outside signal_widths");
    }
    for (const auto& operation :
        std::array<Operation, 2> { WriteUpdate { 0, 0 },
            WriteAfter { 0, 0, UINT64_MAX } }) {
        Process bad_width;
        bad_width.id = 0;
        bad_width.name = "scheduled_bad_width";
        bad_width.register_count = 1;
        bad_width.operations = {
            LoadConstant { 0, PackedLogic4::from_msb_string("10100101") },
            operation,
            Halt { },
        };
        expect_fatal_error(
            [&] {
                jit.add_process(
                    "scheduled_bad_width_" + std::to_string(scheduled_suffix++),
                    bad_width, scalar_signal);
            },
            "register width constraints are inconsistent");
    }

    Process invalid_extract;
    invalid_extract.id = 0;
    invalid_extract.name = "invalid_extract";
    invalid_extract.register_count = 2;
    invalid_extract.operations = {
        LoadConstant {
            0, PackedLogic4::from_msb_string("1010") },
        Extract { 1, 0, 3, 2 },
        Halt { },
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "invalid_extract", invalid_extract, no_signals);
        },
        "Extract range is outside its source register");

    Process invalid_insert;
    invalid_insert.id = 0;
    invalid_insert.name = "invalid_insert";
    invalid_insert.register_count = 3;
    invalid_insert.operations = {
        LoadConstant {
            0, PackedLogic4::from_msb_string("1010") },
        LoadConstant {
            1, PackedLogic4::from_msb_string("11") },
        Insert { 2, 0, 1, 3 },
        Halt { },
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "invalid_insert", invalid_insert, no_signals);
        },
        "Insert range is outside its target register");

    Process invalid_partial_write;
    invalid_partial_write.id = 0;
    invalid_partial_write.name = "invalid_partial_write";
    invalid_partial_write.register_count = 1;
    invalid_partial_write.operations = {
        LoadConstant {
            0, PackedLogic4::from_msb_string("11") },
        WriteUpdateSlice { 0, 0, 3 },
        Halt { },
    };
    const std::array<std::uint32_t, 1> nibble_signal { 4 };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "invalid_partial_write",
                invalid_partial_write,
                nibble_signal);
        },
        "partial write range is outside its target signal");

    Process invalid_concatenate;
    invalid_concatenate.id = 0;
    invalid_concatenate.name = "invalid_concatenate";
    invalid_concatenate.register_count = 3;
    invalid_concatenate.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        LoadConstant {
            1, PackedLogic4::from_msb_string("10") },
        Concatenate { 2, { 0, 1 }, 4 },
        Halt { },
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "invalid_concatenate", invalid_concatenate, no_signals);
        },
        "Concatenate operand widths do not match its result width");

    Process too_wide;
    too_wide.id = 0;
    too_wide.name = "wide";
    too_wide.register_count = 1;
    too_wide.operations = { ReadSignal { 0, 0 }, Halt { } };
    const std::array<std::uint32_t, 1> widths { 65 };
    jit.add_process("wide", too_wide, widths);
    TestRuntime wide_runtime;
    auto wide_descriptor = abi(wide_runtime);
    auto wide_services = copy_jit_services(wide_descriptor);
    wide_services.read_signal_packed = nullptr;
    wide_descriptor.services = &wide_services;
    expect_fatal_error(
        [&] {
            (void)jit.execute(jit.lookup("wide"), wide_descriptor);
        },
        "require read_signal_packed");

    Process zero_width;
    zero_width.id = 0;
    zero_width.name = "zero_width";
    zero_width.register_count = 1;
    zero_width.operations = { ReadSignal { 0, 0 }, Halt { } };
    const std::array<std::uint32_t, 1> invalid_widths { 0 };
    expect_fatal_error(
        [&] { jit.add_process("zero_width", zero_width, invalid_widths); },
        "signal width must be greater than zero");

    Process too_many_registers;
    too_many_registers.id = 0;
    too_many_registers.name = "too_many_registers";
    too_many_registers.register_count = static_cast<std::size_t>(
                                            std::numeric_limits<RegisterId>::max())
        + 1U;
    too_many_registers.operations = { Halt { } };
    const std::array<std::uint32_t, 0> no_signal_widths { };
    expect_unsupported(
        [&] {
            jit.add_process(
                "too_many_registers", too_many_registers, no_signal_widths);
        },
        "too many registers");

    Process unsupported_then_bad_signal;
    unsupported_then_bad_signal.id = 0;
    unsupported_then_bad_signal.name = "unsupported_then_bad_signal";
    unsupported_then_bad_signal.operations = { WaitOn { { 0 } }, WaitOn { { 1 } },
        Halt { } };
    expect_fatal_error(
        [&] {
            jit.add_process("unsupported_then_bad_signal",
                unsupported_then_bad_signal, one_signal);
        },
        "signal ID is outside signal_widths");

    Process unsupported_then_bad_target;
    unsupported_then_bad_target.id = 0;
    unsupported_then_bad_target.name = "unsupported_then_bad_target";
    unsupported_then_bad_target.operations = { WaitOn { { 0 } }, Jump { 99 }, Halt { } };
    expect_fatal_error(
        [&] {
            jit.add_process("unsupported_then_bad_target",
                unsupported_then_bad_target, one_signal);
        },
        "jump target is outside");

    Process unsupported_then_undefined_use;
    unsupported_then_undefined_use.id = 0;
    unsupported_then_undefined_use.name = "unsupported_then_undefined_use";
    unsupported_then_undefined_use.register_count = 1;
    unsupported_then_undefined_use.operations = {
        WaitOn { { 0 } }, WriteBlocking { 0, 0 }, Halt { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process("unsupported_then_undefined_use",
                unsupported_then_undefined_use, one_signal);
        },
        "source register is never defined");

    Process unsupported_then_width_contradiction;
    unsupported_then_width_contradiction.id = 0;
    unsupported_then_width_contradiction.name = "unsupported_then_width_contradiction";
    unsupported_then_width_contradiction.register_count = 1;
    unsupported_then_width_contradiction.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        WaitOn { { 0 } },
        WriteBlocking { 0, 0 },
        Halt { },
    };
    const std::array<std::uint32_t, 1> byte_signal_for_contradiction { 8 };
    expect_fatal_error(
        [&] {
            jit.add_process("unsupported_then_width_contradiction",
                unsupported_then_width_contradiction,
                byte_signal_for_contradiction);
        },
        "register width constraints are inconsistent");

    Process bad_jump;
    bad_jump.id = 0;
    bad_jump.name = "bad_jump";
    bad_jump.operations = { Jump { 2 }, Halt { } };
    expect_fatal_error(
        [&] { jit.add_process("bad_jump", bad_jump, no_signals); },
        "jump target is outside");

    Process bad_branch;
    bad_branch.id = 0;
    bad_branch.name = "bad_branch";
    bad_branch.register_count = 1;
    bad_branch.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        Branch { 0, 2, 3, UnknownBranchPolicy::when_false },
        Halt { },
    };
    expect_fatal_error(
        [&] { jit.add_process("bad_branch", bad_branch, no_signals); },
        "branch false target is outside");

    Process bad_fork;
    bad_fork.id = 0;
    bad_fork.name = "bad_fork";
    bad_fork.operations = { Fork { { 1 }, ForkJoinKind::all }, ForkEnd { } };
    expect_fatal_error(
        [&] { jit.add_process("bad_fork", bad_fork, no_signals); },
        "fork branch must follow its parent continuation");

    Process fork_without_continuation;
    fork_without_continuation.id = 0;
    fork_without_continuation.name = "fork_without_continuation";
    fork_without_continuation.operations = {
        Fork { { }, ForkJoinKind::all }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "fork_without_continuation",
                fork_without_continuation,
                no_signals);
        },
        "fork parent continuation is outside");

    Process invalid_fork_join;
    invalid_fork_join.id = 0;
    invalid_fork_join.name = "invalid_fork_join";
    invalid_fork_join.operations = {
        Fork { { 2 }, static_cast<ForkJoinKind>(99) }, Halt { }, ForkEnd { }
    };
    expect_fatal_error(
        [&] {
            jit.add_process(
                "invalid_fork_join", invalid_fork_join, no_signals);
        },
        "fork has an invalid join kind");

    Process path_use_before_definition;
    path_use_before_definition.id = 0;
    path_use_before_definition.name = "path_use_before_definition";
    path_use_before_definition.register_count = 2;
    path_use_before_definition.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        Branch { 0, 2, 4, UnknownBranchPolicy::when_false },
        LoadConstant { 1, PackedLogic4::from_msb_string("10100101") },
        Jump { 5 },
        Jump { 5 },
        WriteBlocking { 0, 1 },
        Halt { },
    };
    const std::array<std::uint32_t, 1> byte_signal { 8 };
    expect_fatal_error(
        [&] {
            jit.add_process("path_use_before_definition",
                path_use_before_definition, byte_signal);
        },
        "register 1 may be used before definition on a control-flow path");

    Process zero_time_cycle;
    zero_time_cycle.id = 0;
    zero_time_cycle.name = "zero_time_cycle";
    zero_time_cycle.operations = { Jump { 0 }, Halt { } };
    assert(jit.supports_process(zero_time_cycle, no_signals));
    jit.add_process("zero_time_cycle", zero_time_cycle, no_signals);
    assert(jit.lookup("zero_time_cycle"));

    Process reachable_cycle;
    reachable_cycle.id = 0;
    reachable_cycle.name = "reachable_cycle";
    reachable_cycle.register_count = 1;
    reachable_cycle.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        Branch { 0, 1, 2, UnknownBranchPolicy::when_false },
        Halt { },
    };
    assert(jit.supports_process(reachable_cycle, no_signals));
    jit.add_process("reachable_cycle", reachable_cycle, no_signals);
    assert(jit.lookup("reachable_cycle"));

    Process debug_safe_cycle;
    debug_safe_cycle.id = 0;
    debug_safe_cycle.name = "debug_safe_cycle";
    debug_safe_cycle.register_count = 1;
    debug_safe_cycle.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        DebugPoint {
            DebugPointKind::statement,
            SourceLocation { "runtime_loop.sv", 4, 5 } },
        Branch { 0, 3, 5, UnknownBranchPolicy::when_false },
        LoadConstant { 0, PackedLogic4::from_msb_string("0") },
        Jump { 1 },
        Halt { },
    };
    jit.add_process(
        "debug_safe_cycle", debug_safe_cycle, no_signals);
    TestRuntime debug_safe_runtime;
    auto debug_safe_descriptor = abi(debug_safe_runtime);
    assert(
        jit.execute(
            jit.lookup("debug_safe_cycle"),
            debug_safe_descriptor)
        == JitExecutionStatus::completed);

    expect_error(
        [&] {
            const std::array<std::uint32_t, 8> valid_widths { 8, 8, 8, 8,
                8, 8, 8, 1 };
            jit.add_process("not-a-c-identifier", make_arithmetic_process(),
                valid_widths);
        },
        "C identifier");
}

} // namespace fsim::tests::compiler

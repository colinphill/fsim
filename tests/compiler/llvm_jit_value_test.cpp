// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void test_systemverilog_scalar_transport_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol) {
  using fsim::runtime::SystemVerilogScalarKind;
  using fsim::runtime::SystemVerilogScalarValue;

  const auto short_value = SystemVerilogScalarValue::shortreal(-1.25F);
  const auto real_value = SystemVerilogScalarValue::real(-0.0);
  const auto time_value = SystemVerilogScalarValue::time(
      UINT64_C(9007199254740993));
  const auto short_payload =
      fsim::runtime::encode_systemverilog_scalar_payload(short_value);
  const auto real_payload =
      fsim::runtime::encode_systemverilog_scalar_payload(real_value);
  const auto time_payload =
      fsim::runtime::encode_systemverilog_scalar_payload(time_value);
  assert(short_payload && real_payload && time_payload);

  Process process;
  process.id = 0;
  process.name = "systemverilog_scalar_transport";
  process.register_count = 3;
  process.operations = {
      LoadConstant{0, short_payload.value},
      WriteBlocking{0, 0},
      LoadConstant{1, real_payload.value},
      WriteBlocking{1, 1},
      LoadConstant{2, time_payload.value},
      WriteBlocking{2, 2},
      Halt{}};
  const std::array<std::uint32_t, 3> widths{32, 64, 64};
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  jit.add_process(symbol, process, widths);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(jit.execute(jit.lookup(symbol), descriptor)
         == JitExecutionStatus::completed);
  assert((runtime.signals[0]
          == EncodedSignal{short_value.bits, 0}));
  assert((runtime.signals[1]
          == EncodedSignal{real_value.bits, 0}));
  assert((runtime.signals[2]
          == EncodedSignal{time_value.bits, 0}));
  const auto decoded = fsim::runtime::decode_systemverilog_scalar_payload(
      PackedLogic4::from_aval_bval(
          64, runtime.signals[2].aval, runtime.signals[2].bval),
      SystemVerilogScalarKind::Time);
  assert(decoded && decoded.value == time_value);

  const auto factor = fsim::runtime::encode_systemverilog_scalar_payload(
      SystemVerilogScalarValue::real(1.5));
  const auto multiplier = fsim::runtime::encode_systemverilog_scalar_payload(
      SystemVerilogScalarValue::real(2.0));
  assert(factor && multiplier);
  Process scalar_binary;
  scalar_binary.id = 1;
  scalar_binary.name = "systemverilog_scalar_binary";
  scalar_binary.register_count = 3;
  scalar_binary.operations = {
      LoadConstant{0, factor.value},
      LoadConstant{1, multiplier.value},
      SystemVerilogScalarBinary{
          fsim::runtime::SystemVerilogScalarBinaryOperator::Multiply,
          2, 0, 1,
          SystemVerilogScalarKind::Real,
          SystemVerilogScalarKind::Real,
          SystemVerilogScalarKind::Real},
      Halt{}};
  const auto binary_symbol = std::string{symbol} + "_binary";
  jit.add_process(
      binary_symbol,
      scalar_binary,
      std::array<std::uint32_t, 3>{64, 64, 64});
  (void)jit.lookup(binary_symbol);
}

void test_wide_register_frame_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    auto value = PackedLogic4(257, fsim::runtime::Logic4::zero);
    value.set(0, fsim::runtime::Logic4::one);
    value.set(64, fsim::runtime::Logic4::x);
    value.set(128, fsim::runtime::Logic4::z);
    value.set(256, fsim::runtime::Logic4::one);

    Process process;
    process.id = 91;
    process.name = "wide_register_frame";
    process.register_count = 2;
    process.operations = {
        LoadConstant { 0, value },
        CopyRegister { 1, 0 },
        Pause { },
        Stop { },
    };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, { }));
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.tracks_register_initialization);
    assert(layout.register_count == 2);
    assert(layout.register_word_count == 10);
    assert((layout.register_widths
        == std::vector<std::uint32_t> { 257, 257 }));
    assert((layout.register_word_offsets
        == std::vector<std::uint32_t> { 0, 5 }));

    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(result.instruction == 2);
    assert((initialized == std::vector<std::uint8_t> { 1, 1 }));
    for (std::size_t destination = 0; destination < 2; ++destination) {
        const auto offset = layout.register_word_offsets[destination];
        assert(std::ranges::equal(value.aval_words(),
            std::span { aval }.subspan(offset, value.aval_words().size())));
        assert(std::ranges::equal(value.bval_words(),
            std::span { bval }.subspan(offset, value.bval_words().size())));
    }
}

void test_wide_transient_register_frame_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    auto value = PackedLogic4(257, Logic4::zero);
    value.set(0, Logic4::one);
    value.set(64, Logic4::x);
    value.set(128, Logic4::z);
    value.set(256, Logic4::one);

    Process process;
    process.id = 98;
    process.name = "wide_transient_register_frame";
    process.register_count = 2;
    process.static_sensitivity.push_back({ 0U, EdgeKind::any });
    process.operations = {
        LoadConstant { 0, value },
        CopyRegister { 1, 0 },
        WriteUpdate { 0, 1 },
        WaitSensitivity { },
        Jump { 0 },
    };
    const std::array<std::uint32_t, 1> widths { 257 };

    auto options = LlvmJitOptions { optimization, { } };
    options.debug_instrumentation = false;
    LlvmJit jit { std::move(options) };
    assert(jit.supports_process(process, widths));
    jit.add_process(symbol, process, widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.register_word_count == 10U);

    std::vector<std::uint64_t> aval(
        layout.register_word_count, 0xaaaaaaaaaaaaaaaaULL);
    std::vector<std::uint64_t> bval(
        layout.register_word_count, 0x5555555555555555ULL);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    std::ranges::fill(aval, 0xaaaaaaaaaaaaaaaaULL);
    std::ranges::fill(bval, 0x5555555555555555ULL);
    const auto initial_aval = aval;
    const auto initial_bval = bval;
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    descriptor.execute_signal_operation = nullptr;
    auto result = new_resume_result();

    for (std::size_t resume = 0; resume < 2; ++resume) {
        assert(jit.resume(handle, descriptor, frame, result)
            == JitResumeStatus::wait_sensitivity);
        assert(result.instruction == 3U);
        assert(frame.program_counter == 4U);
        assert(std::ranges::equal(
            value.aval_words(), runtime.wide_signal_aval[0]));
        assert(std::ranges::equal(
            value.bval_words(), runtime.wide_signal_bval[0]));
        assert(aval == initial_aval);
        assert(bval == initial_bval);
    }
}

void test_optimized_frame_initialization_elision_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 99;
    process.name = "optimized_frame_initialization_elision";
    process.register_count = 2;
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(
                   64, UINT64_C(0x0123456789abcdef), 0) },
        CopyRegister { 1, 0 },
        Pause { },
        Stop { },
    };

    LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(!layout.tracks_register_initialization);

    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(result.instruction == 2);
    assert((initialized == std::vector<std::uint8_t> { 0, 0 }));
    assert((aval == std::vector<std::uint64_t> {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0x0123456789abcdef) }));
    assert((bval == std::vector<std::uint64_t> { 0, 0 }));
}

void test_wide_signal_read_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 96;
    process.name = "wide_signal_read";
    process.register_count = 1;
    process.operations = {
        ReadSignal { 0, 0 },
        Pause { },
        Stop { },
    };
    const std::array<std::uint32_t, 1> widths { 257 };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, widths));
    jit.add_process(symbol, process, widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert((layout.direct_read_signals
        == std::vector<runtime::simir::SignalId> { 0 }));
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);

    const std::vector<std::uint64_t> expected_aval {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210),
        UINT64_C(0x1111222233334444),
        UINT64_C(0xaaaabbbbccccdddd),
        UINT64_C(1),
    };
    const std::vector<std::uint64_t> expected_bval {
        0U, UINT64_C(0x8000000000000000), 0U, 1U, 0U,
    };
    TestRuntime runtime;
    runtime.wide_signal_aval[0].assign(expected_aval.size(), 0U);
    runtime.wide_signal_bval[0].assign(expected_bval.size(), 0U);
    auto descriptor = abi(runtime);
    descriptor.execute_signal_operation = nullptr;
    const std::array<std::uint32_t, 1> direct_signals { 0 };
    const std::array<std::uint32_t, 1> direct_offsets { 0 };
    descriptor.direct_read_signals = direct_signals.data();
    descriptor.direct_read_signal_count = 1;
    descriptor.direct_wide_signal_aval = expected_aval.data();
    descriptor.direct_wide_signal_bval = expected_bval.data();
    descriptor.direct_wide_signal_offsets = direct_offsets.data();
    descriptor.direct_wide_signal_offset_count = 1;
    descriptor.direct_wide_word_count = 5;
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(initialized[0] == 1U);
    assert(aval == expected_aval);
    assert(bval == expected_bval);
}

void test_wide_signal_write_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    auto whole = PackedLogic4(257, Logic4::zero);
    whole.set(0, Logic4::one);
    whole.set(128, Logic4::x);
    whole.set(256, Logic4::one);
    auto slice = PackedLogic4(129, Logic4::zero);
    slice.set(0, Logic4::z);
    slice.set(64, Logic4::one);
    slice.set(128, Logic4::x);

    Process process;
    process.id = 97;
    process.name = "wide_signal_write";
    process.register_count = 2;
    process.operations = {
        LoadConstant { 0, whole },
        LoadConstant { 1, slice },
        WriteBlocking { 0, 0 },
        WriteUpdate { 1, 0 },
        WriteAfter { 2, 0, 11 },
        WriteBlockingSlice { 3, 1, 64 },
        WriteUpdateSlice { 4, 1, 64 },
        WriteAfterSlice { 5, 1, 64, 13 },
        Pause { },
        Stop { },
    };
    const std::array<std::uint32_t, 6> widths {
        257, 257, 257, 257, 257, 257
    };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, widths));
    jit.add_process(symbol, process, widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    descriptor.execute_signal_operation = nullptr;
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert((runtime.packed_signal_write_modes
        == std::vector<std::uint32_t> {
            FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING,
            FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE,
            FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER,
            FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_SLICE,
            FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE,
            FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_SLICE,
        }));
    assert((runtime.packed_signal_write_signals
        == std::vector<std::uint32_t> { 0, 1, 2, 3, 4, 5 }));
    for (std::uint32_t signal = 0; signal < 3; ++signal) {
        assert(std::ranges::equal(
            whole.aval_words(), runtime.wide_signal_aval[signal]));
        assert(std::ranges::equal(
            whole.bval_words(), runtime.wide_signal_bval[signal]));
    }
    for (std::uint32_t signal = 3; signal < 6; ++signal) {
        assert(std::ranges::equal(
            slice.aval_words(), runtime.wide_signal_aval[signal]));
        assert(std::ranges::equal(
            slice.bval_words(), runtime.wide_signal_bval[signal]));
    }
    assert(runtime.packed_signal_write_offset == 64U);
    assert(runtime.packed_signal_write_width == 129U);
    assert(runtime.packed_signal_write_delay == 13U);
}

void test_wide_container_operations_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 94;
    process.name = "wide_container_operations";
    process.register_count = 2;
    process.container_register_count = 1;
    ContainerType type;
    type.fixed = true;
    type.index_left = 0;
    type.index_right = 0;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = 257;
    process.container_register_types.push_back(type);
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 0U, 0U) },
        ContainerRead { 1, 0, 0, true, false, false },
        ContainerWrite { 0, 0, 1, true, false, false },
        Pause { },
        Stop { },
    };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, { }));
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);

    TestRuntime runtime;
    runtime.container_read_aval = {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210),
        UINT64_C(0x1111222233334444),
        UINT64_C(0xaaaabbbbccccdddd),
        UINT64_C(1)
    };
    runtime.container_read_bval = {
        0U, UINT64_C(0x8000000000000000), 0U, 1U, 0U
    };
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(runtime.container_packed_reads == 1U);
    assert(runtime.container_packed_writes == 1U);
    assert(runtime.container_write_aval == runtime.container_read_aval);
    assert(runtime.container_write_bval == runtime.container_read_bval);
}

void test_fused_container_object_read_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 108;
    process.name = "fused_container_object_read";
    process.register_count = 2;
    process.container_register_count = 1;
    ContainerType type;
    type.fixed = true;
    type.index_left = 0;
    type.index_right = 0;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = 8;
    process.container_register_types.push_back(type);
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 0U, 0U) },
        ReadContainerObject { 0, 7 },
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 0U, 0U) },
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 0U, 0U) },
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 0U, 0U) },
        ContainerRead { 1, 0, 0, true, false, false },
        WriteBlocking { 0, 1 },
        Pause { },
        Stop { },
    };

    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    const std::array<std::uint32_t, 1> widths { 8 };
    assert(jit.supports_process(process, widths));
    jit.add_process(symbol, process, widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    descriptor.container_operation = [](
        void* opaque, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
        std::uint64_t*, std::uint64_t*) {
        auto& observed = *static_cast<TestRuntime*>(opaque);
        ++observed.container_packed_writes;
        return std::uint32_t { 1U };
    };
    descriptor.container_read_word = [](
        void* opaque, std::uint32_t, const std::uint32_t instruction,
        std::uint32_t, const std::uint32_t flags,
        std::uint64_t, std::uint64_t,
        std::uint64_t* result_aval, std::uint64_t* result_bval) {
        auto& observed = *static_cast<TestRuntime*>(opaque);
        ++observed.container_packed_reads;
        observed.container_write_aval[0] = instruction;
        observed.container_write_aval[1] = flags;
        *result_aval = UINT64_C(0x5a);
        *result_bval = 0U;
        return std::uint32_t { };
    };
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(runtime.container_packed_writes == 0U);
    assert(runtime.container_packed_reads == 1U);
    assert(runtime.container_write_aval[0] == 5U);
    assert((runtime.container_write_aval[1] >> 8U) == 4U);
    assert((runtime.writes
        == std::vector<std::pair<std::uint32_t, EncodedSignal>> {
            { 0U, { UINT64_C(0x5a), 0U } }
        }));
}

void test_wide_value_operations_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    auto wide = PackedLogic4(257, Logic4::zero);
    wide.set(0, Logic4::one);
    wide.set(2, Logic4::one);
    wide.set(256, Logic4::one);
    auto one = PackedLogic4(257, Logic4::zero);
    one.set(0, Logic4::one);
    auto three = one;
    three.set(1, Logic4::one);
    auto wide_part = PackedLogic4(129, Logic4::zero);
    wide_part.set(0, Logic4::one);
    wide_part.set(64, Logic4::x);
    wide_part.set(128, Logic4::z);
    auto unknown_addend = wide;
    unknown_addend.set(64, Logic4::x);
    auto all_ones = PackedLogic4(257, Logic4::one);

    Process process;
    process.id = 92;
    process.name = "wide_value_operations";
    process.register_count = 57;
    process.operations = {
        LoadConstant { 0, wide },
        LoadConstant { 1, one },
        LoadConstant { 2, three },
        Binary { BinaryOperator::bit_and, 3, 0, 2 },
        Binary { BinaryOperator::bit_or, 4, 0, 2 },
        Binary { BinaryOperator::bit_xor, 5, 0, 2 },
        Binary { BinaryOperator::add_unsigned, 6, 0, 2 },
        Binary { BinaryOperator::subtract_unsigned, 7, 0, 2 },
        Binary { BinaryOperator::multiply_unsigned, 8, 0, 2 },
        Binary { BinaryOperator::power_unsigned, 9, 0, 1 },
        Binary { BinaryOperator::divide_unsigned, 10, 0, 1 },
        Binary { BinaryOperator::modulo_unsigned, 11, 0, 1 },
        Binary { BinaryOperator::add_signed, 12, 0, 2 },
        Binary { BinaryOperator::subtract_signed, 13, 0, 2 },
        Binary { BinaryOperator::multiply_signed, 14, 0, 2 },
        Binary { BinaryOperator::power_signed, 15, 0, 1 },
        Binary { BinaryOperator::divide_signed, 16, 0, 1 },
        Binary { BinaryOperator::remainder_signed, 17, 0, 1 },
        Binary { BinaryOperator::modulo_signed, 18, 0, 1 },
        Binary { BinaryOperator::equal, 19, 0, 0 },
        Binary { BinaryOperator::not_equal, 20, 0, 2 },
        Binary { BinaryOperator::less_unsigned, 21, 2, 0 },
        Binary { BinaryOperator::less_signed, 22, 0, 2 },
        Binary { BinaryOperator::case_equal, 23, 0, 0 },
        UnaryNot { 24, 0 },
        LogicalNot { 25, 0 },
        LogicalBinary { LogicalBinaryOperator::logical_and, 26, 0, 2 },
        Reduction { ReductionOperator::bit_xor, 27, 0 },
        CountOnes { 28, 0 },
        Concatenate { 29, { 0, 2 }, 514 },
        Extract { 30, 29, 0, 257 },
        Insert { 31, 0, 19, 128 },
        ConditionalSelect { 32, 19, 0, 2 },
        Shift { ShiftOperator::logical_left, 33, 0, 1, false },
        Shift { ShiftOperator::logical_right, 34, 0, 1, false },
        Shift { ShiftOperator::arithmetic_left, 35, 0, 1, false },
        Shift { ShiftOperator::arithmetic_right, 36, 0, 1, false },
        Shift { ShiftOperator::rotate_left, 37, 0, 1, false },
        Shift { ShiftOperator::rotate_right, 38, 0, 1, false },
        LoadConstant { 39, PackedLogic4::from_aval_bval(32, 128, 0) },
        LoadConstant { 40, PackedLogic4::from_aval_bval(32, 256, 0) },
        DynamicExtract { 41, 0, DynamicIndex { 40, 256, 0, 0 } },
        DynamicInsert { 42, 0, 19, DynamicIndex { 39, 256, 0, 0 } },
        DynamicPartSelect { 43, 0, 40, 256, 0, 1, true, true, false, 0 },
        DynamicPartInsert {
            44, 0, 19, DynamicPartIndex { 39, 256, 0, 0, 1, true, true } },
        LoadConstant { 45, wide_part },
        LoadConstant { 46, PackedLogic4::from_aval_bval(32, 64, 0) },
        DynamicPartInsert {
            47, 0, 45,
            DynamicPartIndex { 46, 256, 0, 0, 129, true, true } },
        DynamicPartSelect {
            48, 47, 46, 256, 0, 129, true, true, false, 0 },
        ConvertToTwoState { 49, 45 },
        LoadConstant { 50, unknown_addend },
        Binary { BinaryOperator::add_unsigned, 51, 50, 1 },
        Binary { BinaryOperator::add_signed, 52, 50, 1 },
        LoadConstant { 53, all_ones },
        Binary { BinaryOperator::add_unsigned, 54, 53, 1 },
        Binary { BinaryOperator::add_signed, 55, 53, 1 },
        Binary { BinaryOperator::add_unsigned, 56, 1, 1 },
        Pause { },
        Stop { },
    };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, { }));
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);

    const auto read = [&](const RegisterId id) {
        const auto width = layout.register_widths[id];
        const auto offset = layout.register_word_offsets[id];
        const auto words = (static_cast<std::size_t>(width) + 63U) / 64U;
        return PackedLogic4::from_word_planes(
            width,
            std::span { aval }.subspan(offset, words),
            std::span { bval }.subspan(offset, words));
    };
    const auto scalar = [&](const RegisterId id) {
        const auto value = read(id).known_unsigned_value();
        assert(value);
        return *value;
    };
    const auto expect_wide = [&](const RegisterId id,
                                 const PackedLogic4& expected) {
        assert(read(id).to_msb_string() == expected.to_msb_string());
    };

    assert(scalar(3) == 1);
    auto expected_or = wide;
    expected_or.set(1, Logic4::one);
    expect_wide(4, expected_or);
    auto expected_xor = expected_or;
    expected_xor.set(0, Logic4::zero);
    expect_wide(5, expected_xor);
    auto expected_add = PackedLogic4(257, Logic4::zero);
    expected_add.set(3, Logic4::one);
    expected_add.set(256, Logic4::one);
    expect_wide(6, expected_add);
    auto expected_subtract = PackedLogic4(257, Logic4::zero);
    expected_subtract.set(1, Logic4::one);
    expected_subtract.set(256, Logic4::one);
    expect_wide(7, expected_subtract);
    auto expected_multiply = PackedLogic4(257, Logic4::zero);
    for (const auto bit : { 0U, 1U, 2U, 3U, 256U }) {
        expected_multiply.set(bit, Logic4::one);
    }
    expect_wide(8, expected_multiply);
    for (const auto id : { 9U, 10U, 15U, 16U }) {
        expect_wide(id, wide);
    }
    for (const auto id : { 11U, 17U, 18U }) {
        assert(scalar(id) == 0);
    }
    expect_wide(12, expected_add);
    expect_wide(13, expected_subtract);
    expect_wide(14, expected_multiply);
    for (const auto id : { 19U, 20U, 21U, 22U, 23U, 26U, 27U }) {
        assert(scalar(id) == 1);
    }
    const auto inverted = read(24);
    assert(inverted.get(0) == Logic4::zero);
    assert(inverted.get(1) == Logic4::one);
    assert(inverted.get(2) == Logic4::zero);
    assert(inverted.get(255) == Logic4::one);
    assert(inverted.get(256) == Logic4::zero);
    assert(scalar(25) == 0);
    assert(scalar(28) == 3);
    const auto concatenated = read(29);
    assert(concatenated.get(0) == Logic4::one);
    assert(concatenated.get(1) == Logic4::one);
    assert(concatenated.get(256) == Logic4::zero);
    assert(concatenated.get(257) == Logic4::one);
    assert(concatenated.get(259) == Logic4::one);
    assert(concatenated.get(513) == Logic4::one);
    expect_wide(30, three);
    auto expected_insert = wide;
    expected_insert.set(128, Logic4::one);
    expect_wide(31, expected_insert);
    expect_wide(32, wide);
    auto expected_left = PackedLogic4(257, Logic4::zero);
    expected_left.set(1, Logic4::one);
    expected_left.set(3, Logic4::one);
    expect_wide(33, expected_left);
    auto expected_right = PackedLogic4(257, Logic4::zero);
    expected_right.set(1, Logic4::one);
    expected_right.set(255, Logic4::one);
    expect_wide(34, expected_right);
    auto expected_arithmetic_left = expected_left;
    expected_arithmetic_left.set(0, Logic4::one);
    expect_wide(35, expected_arithmetic_left);
    auto expected_arithmetic_right = expected_right;
    expected_arithmetic_right.set(256, Logic4::one);
    expect_wide(36, expected_arithmetic_right);
    expect_wide(37, expected_arithmetic_left);
    expect_wide(38, expected_arithmetic_right);
    assert(scalar(41) == 1);
    expect_wide(42, expected_insert);
    assert(scalar(43) == 1);
    expect_wide(44, expected_insert);
    auto expected_wide_part_insert = wide;
    for (std::size_t bit = 0; bit < wide_part.width(); ++bit) {
        expected_wide_part_insert.set(64 + bit, wide_part.get(bit));
    }
    expect_wide(47, expected_wide_part_insert);
    expect_wide(48, wide_part);
    auto expected_two_state = PackedLogic4(129, Logic4::zero);
    expected_two_state.set(0, Logic4::one);
    expect_wide(49, expected_two_state);
    const auto all_unknown = PackedLogic4(257, Logic4::x);
    expect_wide(51, all_unknown);
    expect_wide(52, all_unknown);
    const auto wrapped_zero = PackedLogic4(257, Logic4::zero);
    expect_wide(54, wrapped_zero);
    expect_wide(55, wrapped_zero);
    auto expected_two = PackedLogic4(257, Logic4::zero);
    expected_two.set(1, Logic4::one);
    expect_wide(56, expected_two);
}

void test_constant_dynamic_part_select_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    const auto rom = PackedLogic4::from_logic9_msb_string(
        "01UXZWLH-10HLWZX");
    Process process;
    process.id = 108;
    process.name = "constant_dynamic_part_select";
    process.register_count = 3;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic9, ValueKind::logic9
    };
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 8, 0) },
        LoadConstant { 1, rom },
        DynamicPartSelect {
            2, 1, 0, 15, 0, 8, true, true, false, 0 },
        WriteBlocking { 0, 2 },
        Halt { }
    };
    const std::array<std::uint32_t, 1> widths { 8 };
    const std::array<ValueKind, 1> kinds { ValueKind::logic9 };
    auto options = LlvmJitOptions { optimization, { } };
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    jit.add_process(symbol, process, widths, kinds);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    assert(jit.execute(jit.lookup(symbol), descriptor)
        == JitExecutionStatus::completed);
    const auto expected = rom.extract_bits(8, 8).logic9_low_word().planes;
    assert(runtime.logic9_signals[0] == expected);
}

void test_logic4_constant_dynamic_part_select_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    const auto rom = PackedLogic4::from_aval_bval(
        16, UINT64_C(0xa53c), UINT64_C(0x0082));
    Process process;
    process.id = 109;
    process.name = "logic4_constant_dynamic_part_select";
    process.register_count = 3;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 8, 0) },
        LoadConstant { 1, rom },
        DynamicPartSelect {
            2, 1, 0, 15, 0, 8, true, true, false, 0 },
        WriteBlocking { 0, 2 },
        Halt { }
    };
    const std::array<std::uint32_t, 1> widths { 8 };
    const std::array<ValueKind, 1> kinds { ValueKind::logic4 };
    auto options = LlvmJitOptions { optimization, { } };
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    jit.add_process(symbol, process, widths, kinds);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    assert(jit.execute(jit.lookup(symbol), descriptor)
        == JitExecutionStatus::completed);
    const auto expected = rom.extract_bits(8, 8).low_word();
    const auto encoded_expected
        = EncodedSignal { expected.aval, expected.bval };
    assert(runtime.signals[0] == encoded_expected);
}

void test_affine_dynamic_extract_fusion_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    constexpr std::uint32_t result_width = 128U;
    auto source = PackedLogic4(result_width * 2U, Logic4::zero);
    for (std::uint32_t bit = 0U; bit < result_width; ++bit) {
        if (bit % 5U == 1U || bit % 7U == 3U) {
            source.set(result_width + bit, Logic4::one);
        }
    }
    const auto expected = source.extract_bits(result_width, result_width);

    Process process;
    process.id = 110;
    process.name = "affine_dynamic_extract_fusion";
    constexpr RegisterId source_register = 0U;
    constexpr RegisterId index_register = 1U;
    constexpr RegisterId result_register = 2U;
    constexpr RegisterId first_temporary = 3U;
    process.register_count = first_temporary + 7U * result_width;
    process.operations = {
        LoadConstant { source_register, source },
        LoadConstant {
            index_register,
            PackedLogic4::from_aval_bval(32U, 1U, 0U) },
        LoadConstant {
            result_register,
            PackedLogic4(result_width, Logic4::zero) },
    };
    for (std::uint32_t element = 0U; element < result_width; ++element) {
        const auto temporary = static_cast<RegisterId>(
            first_temporary + 7U * element);
        process.operations.emplace_back(LoadConstant {
            temporary,
            PackedLogic4::from_aval_bval(32U, 0U, 0U) });
        process.operations.emplace_back(LoadConstant {
            temporary + 1U,
            PackedLogic4::from_aval_bval(32U, result_width, 0U) });
        process.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::multiply,
            temporary + 2U,
            index_register,
            temporary + 1U });
        process.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::add,
            temporary + 3U,
            temporary,
            temporary + 2U });
        process.operations.emplace_back(LoadConstant {
            temporary + 4U,
            PackedLogic4::from_aval_bval(32U, element, 0U) });
        process.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::add,
            temporary + 5U,
            temporary + 3U,
            temporary + 4U });
        process.operations.emplace_back(DynamicExtract {
            temporary + 6U,
            source_register,
            DynamicIndex {
                temporary + 5U,
                static_cast<std::int64_t>(result_width - 1U),
                0,
                0 } });
        process.operations.emplace_back(Insert {
            result_register,
            result_register,
            temporary + 6U,
            element });
    }
    process.operations.emplace_back(Pause { });
    process.operations.emplace_back(Stop { });

    auto options = LlvmJitOptions { optimization, { } };
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    assert(jit.supports_process(process, { }));
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v1 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);

    const auto offset = layout.register_word_offsets[result_register];
    const auto words = (result_width + 63U) / 64U;
    const auto actual = PackedLogic4::from_word_planes(
        result_width,
        std::span { aval }.subspan(offset, words),
        std::span { bval }.subspan(offset, words));
    assert(actual.to_msb_string() == expected.to_msb_string());
}

void test_fused_dynamic_part_signal_read_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 109;
    process.name = "fused_dynamic_part_signal_read";
    process.register_count = 3;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic9, ValueKind::logic9
    };
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 64, 0) },
        ReadSignal { 1, 0 },
        DynamicPartSelect {
            2, 1, 0, 127, 0, 8, true, true, false, 0 },
        WriteBlocking { 1, 2 },
        Halt { }
    };
    const std::array<std::uint32_t, 2> widths { 128, 8 };
    const std::array<ValueKind, 2> kinds {
        ValueKind::logic9, ValueKind::logic9
    };
    auto options = LlvmJitOptions { optimization, { } };
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    jit.add_process(symbol, process, widths, kinds);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    const auto run = [&](const std::string_view text) {
        const auto source = PackedLogic4::from_logic9_msb_string(text);
        assert(source.width() == 128);
        const auto assign_plane = [&](const std::size_t plane,
                                      std::vector<std::uint64_t>& destination) {
            const auto words = source.logic9_plane_words(plane);
            destination.assign(words.begin(), words.end());
        };
        assign_plane(0, runtime.wide_signal_aval[0]);
        assign_plane(1, runtime.wide_signal_bval[0]);
        assign_plane(2, runtime.wide_signal_logic9_plane2[0]);
        assign_plane(3, runtime.wide_signal_logic9_plane3[0]);
        assert(jit.execute(jit.lookup(symbol), descriptor)
            == JitExecutionStatus::completed);
        assert(runtime.logic9_signals[1]
            == source.extract_bits(64, 8).logic9_low_word().planes);
    };
    run("01UXZWLH-10HLWZX01UXZWLH-10HLWZX01UXZWLH-10HLWZX01UXZWLH-10HLWZX"
        "01UXZWLH-10HLWZX01UXZWLH-10HLWZX01UXZWLH-10HLWZX01UXZWLH-10HLWZX");
    run("HLWZX-1001UXZWLHHLWZX-1001UXZWLHHLWZX-1001UXZWLHHLWZX-1001UXZWLH"
        "HLWZX-1001UXZWLHHLWZX-1001UXZWLHHLWZX-1001UXZWLHHLWZX-1001UXZWLH");
    assert(runtime.dynamic_part_signal_reads == 2U);
    assert(runtime.packed_signal_reads == 0U);
}

void test_scalar_truth_tables_and_64_bits() {
  LlvmJit jit;
  Process scalar;
  scalar.id = 0;
  scalar.name = "scalar";
  scalar.register_count = 9;
  scalar.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::bit_and, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::bit_or, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::bit_xor, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::add_unsigned, 5, 0, 1},
      WriteBlocking{5, 5},
      UnaryNot{6, 0},
      WriteBlocking{6, 6},
      Binary{BinaryOperator::equal, 7, 0, 1},
      WriteBlocking{7, 7},
      Binary{BinaryOperator::case_equal, 8, 0, 1},
      WriteBlocking{8, 8},
      Halt{},
  };
  const std::array<std::uint32_t, 9> scalar_widths{
      1, 1, 1, 1, 1, 1, 1, 1, 1};
  jit.add_process("scalar_truth_table", scalar, scalar_widths);
  const auto scalar_handle = jit.lookup("scalar_truth_table");

  constexpr std::array states{Logic4::zero, Logic4::one, Logic4::x,
                              Logic4::z};
  for (const auto lhs : states) {
    for (const auto rhs : states) {
      TestRuntime runtime;
      runtime.signals[0] = encode(lhs);
      runtime.signals[1] = encode(rhs);
      auto descriptor = abi(runtime);
      assert(jit.execute(scalar_handle, descriptor) ==
             JitExecutionStatus::completed);
      assert(runtime.signals[2] == encode(fsim::runtime::logic_and(lhs, rhs)));
      assert(runtime.signals[3] == encode(fsim::runtime::logic_or(lhs, rhs)));
      assert(runtime.signals[4] == encode(fsim::runtime::logic_xor(lhs, rhs)));
      // The carry is discarded for a one-bit unsigned addition, while any
      // arithmetic X/Z operand makes the complete result unknown.
      const auto known =
          [](const Logic4 value) {
            return value == Logic4::zero || value == Logic4::one;
          };
      assert(
          runtime.signals[5]
          == encode(
              known(lhs) && known(rhs)
                  ? fsim::runtime::logic_xor(lhs, rhs)
                  : Logic4::x));
      assert(runtime.signals[6] == encode(fsim::runtime::logic_not(lhs)));
      assert(runtime.signals[7] == encode(equality(lhs, rhs)));
      assert(
          runtime.signals[8]
          == encode(
              lhs == rhs ? Logic4::one : Logic4::zero));
    }
  }

  Process wide;
  wide.id = 1;
  wide.name = "wide";
  wide.register_count = 2;
  wide.operations = {
      ReadSignal{0, 0},
      UnaryNot{1, 0},
      WriteBlocking{1, 1},
      Halt{},
  };
  const std::array<std::uint32_t, 2> wide_widths{64, 64};
  jit.add_process("wide_not", wide, wide_widths);
  const auto wide_handle = jit.lookup("wide_not");
  TestRuntime runtime;
  runtime.signals[0] = {UINT64_C(0x0123456789abcdef),
                        UINT64_C(0x8000000000000000)};
  auto descriptor = abi(runtime);
  assert(jit.execute(wide_handle, descriptor) ==
         JitExecutionStatus::completed);
  assert((runtime.signals[1] ==
          EncodedSignal{UINT64_C(0xfedcba9876543210) |
                            UINT64_C(0x8000000000000000),
                        UINT64_C(0x8000000000000000)}));
}

void test_wildcard_case_matching_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 5;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::casez_equal, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::casex_equal, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::wildcard_equal, 4, 0, 1},
      WriteBlocking{4, 4},
      Halt{},
  };
  const std::array<std::uint32_t, 5> widths{1, 1, 1, 1, 1};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  constexpr std::array states{
      Logic4::zero, Logic4::one, Logic4::x, Logic4::z};
  for (const auto lhs : states) {
    for (const auto rhs : states) {
      TestRuntime runtime;
      runtime.signals[0] = encode(lhs);
      runtime.signals[1] = encode(rhs);
      auto descriptor = abi(runtime);
      assert(
          jit.execute(handle, descriptor)
          == JitExecutionStatus::completed);
      const auto casez_match =
          lhs == Logic4::z || rhs == Logic4::z || lhs == rhs;
      const auto casex_match =
          lhs == Logic4::x || lhs == Logic4::z
          || rhs == Logic4::x || rhs == Logic4::z || lhs == rhs;
      const auto wildcard_match =
          rhs == Logic4::x || rhs == Logic4::z
              ? Logic4::one
              : lhs == Logic4::x || lhs == Logic4::z
                  ? Logic4::x
                  : lhs == rhs ? Logic4::one : Logic4::zero;
      assert(
          runtime.signals[2]
          == encode(casez_match ? Logic4::one : Logic4::zero));
      assert(
          runtime.signals[3]
          == encode(casex_match ? Logic4::one : Logic4::zero));
      assert(runtime.signals[4] == encode(wildcard_match));
    }
  }

  const auto vector_symbol = std::string{symbol} + "_vectors";
  Process vector;
  vector.id = 1;
  vector.name = vector_symbol;
  vector.register_count = 4;
  vector.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::equal, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::wildcard_equal, 3, 0, 1},
      WriteBlocking{3, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 4> vector_widths{2, 2, 1, 1};
  jit.add_process(vector_symbol, vector, vector_widths);
  const auto vector_handle = jit.lookup(vector_symbol);
  for (const auto& [lhs, rhs, wildcard_expected] :
       std::array{
           std::tuple{
               std::string_view{"X0"},
               std::string_view{"X1"},
               Logic4::zero},
           std::tuple{
               std::string_view{"X0"},
               std::string_view{"01"},
               Logic4::x}}) {
    TestRuntime runtime;
    runtime.signals[0] = encode(PackedLogic4::from_msb_string(lhs));
    runtime.signals[1] = encode(PackedLogic4::from_msb_string(rhs));
    auto descriptor = abi(runtime);
    assert(
        jit.execute(vector_handle, descriptor)
        == JitExecutionStatus::completed);
    assert(runtime.signals[2] == encode(Logic4::x));
    assert(runtime.signals[3] == encode(wildcard_expected));
  }
}

void test_conditional_select_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 4;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      ReadSignal{2, 2},
      ConditionalSelect{3, 0, 1, 2},
      WriteBlocking{3, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{1, 4, 4, 4};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  const auto when_true =
      PackedLogic4::from_msb_string("101Z").low_word();
  const auto when_false =
      PackedLogic4::from_msb_string("100Z").low_word();
  for (const auto& [condition, expected] :
       std::array{
           std::pair{Logic4::zero, std::string_view{"100Z"}},
           std::pair{Logic4::one, std::string_view{"101Z"}},
           std::pair{Logic4::x, std::string_view{"10XZ"}},
           std::pair{Logic4::z, std::string_view{"10XZ"}}}) {
    TestRuntime runtime;
    runtime.signals[0] = encode(condition);
    runtime.signals[1] = {when_true.aval, when_true.bval};
    runtime.signals[2] = {when_false.aval, when_false.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    const auto expected_word =
        PackedLogic4::from_msb_string(expected).low_word();
    assert((runtime.signals[3] == EncodedSignal{
        expected_word.aval, expected_word.bval}));
  }
}

void test_comparisons_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 9;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::not_equal, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::less_unsigned, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::less_equal_unsigned, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::greater_unsigned, 5, 0, 1},
      WriteBlocking{5, 5},
      Binary{BinaryOperator::greater_equal_unsigned, 6, 0, 1},
      WriteBlocking{6, 6},
      LogicalNot{7, 0},
      WriteBlocking{7, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 8> widths{
      4, 4, 1, 1, 1, 1, 1, 1};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    std::array<Logic4, 6> expected;
  };
  const std::array cases{
      TestCase{
          "0010",
          "0011",
          {Logic4::one, Logic4::one, Logic4::one,
           Logic4::zero, Logic4::zero, Logic4::zero}},
      TestCase{
          "0000",
          "0000",
          {Logic4::zero, Logic4::zero, Logic4::one,
           Logic4::zero, Logic4::one, Logic4::one}},
      TestCase{
          "00X0",
          "0011",
          {Logic4::x, Logic4::x, Logic4::x,
           Logic4::x, Logic4::x, Logic4::x}},
      TestCase{
          "01Z0",
          "0011",
          {Logic4::x, Logic4::x, Logic4::x,
           Logic4::x, Logic4::x, Logic4::zero}},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0; index < test.expected.size();
         ++index) {
      assert(
          runtime.signals[index + 2]
          == encode(test.expected[index]));
    }
  }
}

void test_logical_binary_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 4;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      LogicalBinary{
          LogicalBinaryOperator::logical_and, 2, 0, 1},
      WriteBlocking{2, 2},
      LogicalBinary{
          LogicalBinaryOperator::logical_or, 3, 0, 1},
      WriteBlocking{3, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{4, 2, 1, 1};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    Logic4 expected_and;
    Logic4 expected_or;
  };
  const std::array cases{
      TestCase{"0000", "X1", Logic4::zero, Logic4::one},
      TestCase{"00X0", "00", Logic4::zero, Logic4::x},
      TestCase{"00X0", "01", Logic4::x, Logic4::one},
      TestCase{"0010", "ZZ", Logic4::x, Logic4::one},
      TestCase{"0010", "01", Logic4::one, Logic4::one},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    assert(runtime.signals[2] == encode(test.expected_and));
    assert(runtime.signals[3] == encode(test.expected_or));
  }
}

void test_reduction_and_shift_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 11;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Reduction{ReductionOperator::bit_and, 2, 0},
      WriteBlocking{2, 2},
      Reduction{ReductionOperator::bit_or, 3, 0},
      WriteBlocking{3, 3},
      Reduction{ReductionOperator::bit_xor, 4, 0},
      WriteBlocking{4, 4},
      Shift{ShiftOperator::logical_left, 5, 0, 1},
      WriteBlocking{5, 5},
      Shift{ShiftOperator::logical_right, 6, 0, 1},
      WriteBlocking{6, 6},
      Shift{ShiftOperator::arithmetic_right, 7, 0, 1},
      WriteBlocking{7, 7},
      Shift{ShiftOperator::arithmetic_left, 8, 0, 1},
      WriteBlocking{8, 8},
      Shift{ShiftOperator::rotate_left, 9, 0, 1},
      WriteBlocking{9, 9},
      Shift{ShiftOperator::rotate_right, 10, 0, 1},
      WriteBlocking{10, 10},
      Halt{},
  };
  const std::array<std::uint32_t, 11> widths{
      4, 3, 1, 1, 1, 4, 4, 4, 4, 4, 4};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view value;
    std::string_view amount;
    std::string_view expected_and;
    std::string_view expected_or;
    std::string_view expected_xor;
    std::string_view expected_left;
    std::string_view expected_right;
    std::string_view expected_arithmetic_right;
    std::string_view expected_arithmetic_left;
    std::string_view expected_rotate_left;
    std::string_view expected_rotate_right;
  };
  const std::array cases{
      TestCase{
          "1111", "001", "1", "1", "0", "1110", "0111",
          "1111", "1111", "1111", "1111"},
      TestCase{
          "1011", "000", "0", "1", "1", "1011", "1011",
          "1011", "1011", "1011", "1011"},
      TestCase{
          "10X1", "001", "0", "1", "X", "0X10", "010X",
          "110X", "0X11", "0X11", "110X"},
      TestCase{
          "11X1", "011", "X", "1", "X", "1000", "0001",
          "1111", "1111", "111X", "1X11"},
      TestCase{
          "00X0", "0X1", "0", "X", "X", "XXXX", "XXXX",
          "XXXX", "XXXX", "XXXX", "XXXX"},
      TestCase{
          "Z001", "100", "0", "1", "X", "0000", "0000",
          "ZZZZ", "1111", "Z001", "Z001"},
      TestCase{
          "1001", "101", "0", "1", "0", "0000", "0000",
          "1111", "1111", "0011", "1100"},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto value =
        PackedLogic4::from_msb_string(test.value).low_word();
    const auto amount =
        PackedLogic4::from_msb_string(test.amount).low_word();
    runtime.signals[0] = {value.aval, value.bval};
    runtime.signals[1] = {amount.aval, amount.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    const std::array expected{
        test.expected_and,
        test.expected_or,
        test.expected_xor,
        test.expected_left,
        test.expected_right,
        test.expected_arithmetic_right,
        test.expected_arithmetic_left,
        test.expected_rotate_left,
        test.expected_rotate_right};
    for (std::size_t index = 0; index < expected.size();
         ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(expected[index]).low_word();
      assert((
          runtime.signals[index + 2]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }
}

void test_signed_shift_counts_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 8;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Shift{
          ShiftOperator::logical_left, 2, 0, 1, true},
      WriteBlocking{2, 2},
      Shift{
          ShiftOperator::logical_right, 3, 0, 1, true},
      WriteBlocking{3, 3},
      Shift{
          ShiftOperator::arithmetic_left, 4, 0, 1, true},
      WriteBlocking{4, 4},
      Shift{
          ShiftOperator::arithmetic_right, 5, 0, 1, true},
      WriteBlocking{5, 5},
      Shift{
          ShiftOperator::rotate_left, 6, 0, 1, true},
      WriteBlocking{6, 6},
      Shift{
          ShiftOperator::rotate_right, 7, 0, 1, true},
      WriteBlocking{7, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 8> widths{
      4, 4, 4, 4, 4, 4, 4, 4};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view amount;
    std::array<std::string_view, 6> expected;
  };
  const std::array cases{
      TestCase{
          "0001",
          {"0X10", "010X", "0X11", "110X", "0X11", "110X"}},
      TestCase{
          "1111",
          {"010X", "0X10", "110X", "0X11", "110X", "0X11"}},
      TestCase{
          "1011",
          {"0000", "0000", "1111", "1111", "110X", "0X11"}},
      TestCase{
          "0X01",
          {"XXXX", "XXXX", "XXXX", "XXXX", "XXXX", "XXXX"}},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto value =
        PackedLogic4::from_msb_string("10X1").low_word();
    const auto amount =
        PackedLogic4::from_msb_string(test.amount).low_word();
    runtime.signals[0] = {value.aval, value.bval};
    runtime.signals[1] = {amount.aval, amount.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0;
         index < test.expected.size(); ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(
              test.expected[index])
              .low_word();
      assert((
          runtime.signals[index + 2]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }
}

void test_unsigned_arithmetic_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 8;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::add_unsigned, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::subtract_unsigned, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::multiply_unsigned, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::divide_unsigned, 5, 0, 1},
      WriteBlocking{5, 5},
      Binary{BinaryOperator::modulo_unsigned, 6, 0, 1},
      WriteBlocking{6, 6},
      Binary{BinaryOperator::power_unsigned, 7, 0, 1},
      WriteBlocking{7, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 8> widths{
      8, 8, 8, 8, 8, 8, 8, 8};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    std::array<std::string_view, 6> expected;
  };
  const std::array cases{
      TestCase{
          "11001000",
          "00000111",
          {"11001111", "11000001", "01111000",
           "00011100", "00000100", "00000000"}},
      TestCase{
          "00000000",
          "00000000",
          {"00000000", "00000000", "00000000",
           "XXXXXXXX", "XXXXXXXX", "00000001"}},
      TestCase{
          "10X01000",
          "00000111",
          {"XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "XXXXXXXX", "XXXXXXXX", "XXXXXXXX"}},
      TestCase{
          "11001000",
          "00000Z11",
          {"XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "XXXXXXXX", "XXXXXXXX", "XXXXXXXX"}},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0; index < test.expected.size();
         ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(
              test.expected[index]).low_word();
      assert((
          runtime.signals[index + 2]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }
}

void test_signed_arithmetic_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 13;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::add_signed, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::subtract_signed, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::multiply_signed, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::divide_signed, 5, 0, 1},
      WriteBlocking{5, 5},
      Binary{BinaryOperator::remainder_signed, 6, 0, 1},
      WriteBlocking{6, 6},
      Binary{BinaryOperator::modulo_signed, 7, 0, 1},
      WriteBlocking{7, 7},
      Binary{BinaryOperator::less_signed, 8, 0, 1},
      WriteBlocking{8, 8},
      Binary{BinaryOperator::less_equal_signed, 9, 0, 1},
      WriteBlocking{9, 9},
      Binary{BinaryOperator::greater_signed, 10, 0, 1},
      WriteBlocking{10, 10},
      Binary{
          BinaryOperator::greater_equal_signed, 11, 0, 1},
      WriteBlocking{11, 11},
      Binary{BinaryOperator::power_signed, 12, 0, 1},
      WriteBlocking{12, 12},
      Halt{},
  };
  const std::array<std::uint32_t, 13> widths{
      8, 8, 8, 8, 8, 8, 8, 8, 1, 1, 1, 1, 8};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    std::array<std::string_view, 11> expected;
  };
  const std::array cases{
      TestCase{
          "11111011",
          "00000011",
          {"11111110", "11111000", "11110001",
           "11111111", "11111110", "00000001",
           "1", "1", "0", "0", "10000011"}},
      TestCase{
          "00000101",
          "11111101",
          {"00000010", "00001000", "11110001",
           "11111111", "00000010", "11111111",
           "0", "0", "1", "1", "00000000"}},
      TestCase{
          "10000000",
          "11111111",
          {"01111111", "10000001", "10000000",
           "10000000", "00000000", "00000000",
           "1", "1", "0", "0", "00000000"}},
      TestCase{
          "10X01000",
          "00000111",
          {"XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "X", "X", "X", "X", "XXXXXXXX"}},
      TestCase{
          "00000101",
          "00000000",
          {"00000101", "00000101", "00000000",
           "XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
           "0", "0", "1", "1", "00000001"}},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0; index < test.expected.size();
         ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(
              test.expected[index]).low_word();
      assert((
          runtime.signals[index + 2]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }

  Process full_width;
  full_width.id = 1;
  full_width.name = std::string{symbol} + "_64";
  full_width.register_count = 6;
  full_width.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::divide_signed, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::remainder_signed, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::modulo_signed, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::less_signed, 5, 0, 1},
      WriteBlocking{5, 5},
      Halt{},
  };
  const std::array<std::uint32_t, 6> full_widths{
      64, 64, 64, 64, 64, 1};
  const auto full_symbol = std::string{symbol} + "_64";
  jit.add_process(full_symbol, full_width, full_widths);
  TestRuntime full_runtime;
  full_runtime.signals[0] = {
      UINT64_C(0x8000000000000000), 0};
  full_runtime.signals[1] = {
      std::numeric_limits<std::uint64_t>::max(), 0};
  auto full_descriptor = abi(full_runtime);
  assert(
      jit.execute(jit.lookup(full_symbol), full_descriptor)
      == JitExecutionStatus::completed);
  assert((
      full_runtime.signals[2]
      == EncodedSignal{UINT64_C(0x8000000000000000), 0}));
  assert((full_runtime.signals[3] == EncodedSignal{0, 0}));
  assert((full_runtime.signals[4] == EncodedSignal{0, 0}));
  assert((full_runtime.signals[5] == EncodedSignal{1, 0}));
}

void test_extract_and_concatenate_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 5;
  process.operations = {
      ReadSignal{0, 0},
      Extract{1, 0, 2, 1},
      WriteBlocking{1, 1},
      Extract{2, 0, 4, 4},
      WriteBlocking{2, 2},
      LoadConstant{
          3, PackedLogic4::from_msb_string("XZ")},
      Concatenate{4, {2, 1, 3}, 7},
      WriteBlocking{3, 4},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{8, 1, 4, 7};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view source;
    std::string_view expected_bit;
    std::string_view expected_part;
    std::string_view expected_joined;
  };
  const std::array cases{
      TestCase{"10XZ0110", "1", "10XZ", "10XZ1XZ"},
      TestCase{"Z10100X1", "0", "Z101", "Z1010XZ"},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto source =
        PackedLogic4::from_msb_string(test.source).low_word();
    runtime.signals[0] = {source.aval, source.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    const std::array expected{
        test.expected_bit,
        test.expected_part,
        test.expected_joined};
    for (std::size_t index = 0; index < expected.size();
         ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(expected[index]).low_word();
      assert((
          runtime.signals[index + 1]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }
}

void test_insert_and_partial_writes_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 4;
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string("11000011")},
      LoadConstant{1, PackedLogic4::from_msb_string("XZ")},
      Insert{2, 0, 1, 3},
      WriteBlocking{0, 2},
      LoadConstant{3, PackedLogic4::from_msb_string("10")},
      WriteBlockingSlice{1, 3, 1},
      WriteUpdateSlice{2, 1, 4},
      WriteAfterSlice{3, 3, 2, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{8, 8, 8, 8};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  runtime.signals[1] = {UINT64_C(0xff), 0};
  auto descriptor = abi(runtime);
  assert(
      jit.execute(handle, descriptor)
      == JitExecutionStatus::completed);
  const auto inserted =
      PackedLogic4::from_msb_string("110XZ011").low_word();
  assert((
      runtime.signals[0]
      == EncodedSignal{inserted.aval, inserted.bval}));
  assert((runtime.signals[1] == EncodedSignal{UINT64_C(0xfd), 0}));
  assert(runtime.scheduled_writes.size() == 2);
  assert((
      runtime.scheduled_writes[0]
      == ScheduledWrite{
          ScheduledWriteKind::slice_update,
          2,
          encode(PackedLogic4::from_msb_string("XZ")),
          0,
          0,
          0,
          4,
          2}));
  assert((
      runtime.scheduled_writes[1]
      == ScheduledWrite{
          ScheduledWriteKind::slice_after,
          3,
          encode(PackedLogic4::from_msb_string("10")),
          0,
          7,
          7,
          2,
          2}));

  {
    auto legacy = descriptor;
    legacy.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_signal_slice));
    expect_fatal_error(
        [&] { (void)jit.execute(handle, legacy); },
        "does not include write_signal_slice");
  }
  {
    auto missing = descriptor;
    missing.write_update_slice = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_update_slice");
  }
  {
    auto missing = descriptor;
    missing.write_after_slice = nullptr;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "requires write_after_slice");
  }
}

void test_dynamic_packed_indices_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  const auto integer = [](const std::int32_t value) {
    return PackedLogic4::from_aval_bval(
        32, static_cast<std::uint32_t>(value), 0);
  };

  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 41;
  process.name = std::string{symbol};
  process.register_count = 8;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      DynamicExtract{
          2, 0, DynamicIndex{1, 7, 0, 0}},
      WriteBlocking{2, 2},
      LoadConstant{
          3, PackedLogic4::from_msb_string("1")},
      DynamicInsert{
          4, 0, 3, DynamicIndex{1, 7, 0, 0}},
      WriteBlocking{3, 4},
      WriteBlockingDynamicSlice{
          4, 3, DynamicIndex{1, 7, 0, 0}},
      WriteUpdateDynamicSlice{
          5, 3, DynamicIndex{1, 7, 0, 0}},
      WriteAfterDynamicSlice{
          6, 3, DynamicIndex{1, 7, 0, 0}, 7},
      WriteInertialDynamicSlice{
          7,
          3,
          DynamicIndex{1, 7, 0, 0},
          TransitionDelays{2, 3, 4}},
      LoadConstant{5, PackedLogic4::from_msb_string("1010")},
      LoadConstant{6, PackedLogic4::from_msb_string("X01Z")},
      LoadConstant{7, integer(4)},
      WriteProjectedDynamicSlice{
          8,
          5,
          DynamicIndex{7, 7, 0, 0},
          5,
          0,
          ProjectedDelayMode::transport},
      WriteProjectedWaveformDynamicSlice{
          9,
          {
              ProjectedWaveformElement{5, 1},
              ProjectedWaveformElement{6, 6},
          },
          DynamicIndex{7, 7, 0, 0},
          0,
          ProjectedDelayMode::transport},
      Halt{},
  };
  const std::array<std::uint32_t, 10> widths{
      8, 32, 1, 8, 8, 8, 8, 8, 8, 8};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  runtime.signals[0] =
      encode(PackedLogic4::from_msb_string("10XZ0110"));
  runtime.signals[1] = encode(integer(5));
  auto descriptor = abi(runtime);
  assert(
      jit.execute(handle, descriptor)
      == JitExecutionStatus::completed);
  assert((
      runtime.signals[2]
      == encode(PackedLogic4::from_msb_string("X"))));
  assert((
      runtime.signals[3]
      == encode(PackedLogic4::from_msb_string("101Z0110"))));
  assert((
      runtime.signals[4]
      == encode(PackedLogic4::from_msb_string("00100000"))));
  assert(runtime.scheduled_writes.size() == 2);
  assert(
      runtime.scheduled_writes[0].kind
              == ScheduledWriteKind::slice_update
          && runtime.scheduled_writes[0].signal == 5
          && runtime.scheduled_writes[0].offset == 5
          && runtime.scheduled_writes[0].width == 1);
  assert(
      runtime.scheduled_writes[1].kind
              == ScheduledWriteKind::slice_after
          && runtime.scheduled_writes[1].signal == 6
          && runtime.scheduled_writes[1].offset == 5
          && runtime.scheduled_writes[1].width == 1
          && runtime.scheduled_writes[1].delay == 7);
  assert(
      runtime.inertial_writes.size() == 1
          && runtime.inertial_writes[0].signal == 7
          && runtime.inertial_writes[0].offset == 5
          && runtime.inertial_writes[0].width == 1);
  assert(
      runtime.projected_writes.size() == 3
          && runtime.projected_writes[0].signal == 8
          && runtime.projected_writes[0].offset == 4
          && runtime.projected_writes[0].width == 4
          && runtime.projected_writes[1].signal == 9
          && runtime.projected_writes[1].offset == 4
          && runtime.projected_writes[1].width == 4
          && runtime.projected_writes[2].offset == 4
          && runtime.projected_writes[2].width == 4);

  const auto run_invalid_read =
      [&](const std::string_view suffix,
          PackedLogic4 index) {
        Process failure;
        failure.id = 42;
        failure.name =
            std::string{symbol} + "_" + std::string{suffix};
        failure.register_count = 3;
        failure.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("1010")},
            LoadConstant{1, std::move(index)},
            DynamicExtract{
                2, 0, DynamicIndex{1, 3, 0, 0}},
            WriteBlocking{0, 2},
            Halt{}};
        const auto failure_symbol =
            std::string{symbol} + "_" + std::string{suffix};
        const std::array<std::uint32_t, 1> one_bit_signal { 1 };
        jit.add_process(failure_symbol, failure, one_bit_signal);
        TestRuntime invalid_runtime;
        invalid_runtime.signals[0] =
            encode(PackedLogic4::from_msb_string("0"));
        auto invalid_descriptor = abi(invalid_runtime);
        assert(
            jit.execute(
                jit.lookup(failure_symbol), invalid_descriptor)
            == JitExecutionStatus::completed);
        assert(
            invalid_runtime.signals[0]
            == encode(PackedLogic4::from_msb_string("X")));
      };
  run_invalid_read("range", integer(4));
  auto unknown = integer(0);
  unknown.set(0, Logic4::x);
  run_invalid_read("unknown", std::move(unknown));

  Process logic9;
  logic9.id = 43;
  logic9.name = std::string{symbol} + "_logic9";
  logic9.register_count = 6;
  logic9.register_value_kinds = {
      ValueKind::logic9,
      ValueKind::logic4,
      ValueKind::logic9,
      ValueKind::logic9,
      ValueKind::logic9,
      ValueKind::logic9};
  logic9.operations = {
      ReadSignal{0, 0},
      LoadConstant{1, integer(3)},
      DynamicExtract{
          2, 0, DynamicIndex{1, -2, 5, 0}},
      WriteBlocking{1, 2},
      LoadConstant{
          3, PackedLogic4::from_logic9_msb_string("H")},
      DynamicInsert{
          4, 0, 3, DynamicIndex{1, -2, 5, 0}},
      WriteBlocking{2, 4},
      WriteProjectedDynamicSlice{
          3,
          3,
          DynamicIndex{1, -2, 5, 0},
          0,
          0,
          ProjectedDelayMode::transport},
      LoadConstant{
          5, PackedLogic4::from_logic9_msb_string("L")},
      WriteProjectedWaveformDynamicSlice{
          4,
          {
              ProjectedWaveformElement{5, 1},
              ProjectedWaveformElement{3, 2},
          },
          DynamicIndex{1, -2, 5, 0},
          0,
          ProjectedDelayMode::transport},
      Halt{}};
  const std::array<std::uint32_t, 5> logic9_widths{
      8, 1, 8, 8, 8};
  const std::array<ValueKind, 5> logic9_kinds{
      ValueKind::logic9,
      ValueKind::logic9,
      ValueKind::logic9,
      ValueKind::logic9,
      ValueKind::logic9};
  const auto logic9_symbol = std::string{symbol} + "_logic9";
  jit.add_process(
      logic9_symbol,
      logic9,
      logic9_widths,
      logic9_kinds);
  TestRuntime logic9_runtime;
  const auto source =
      PackedLogic4::from_logic9_msb_string("UX01ZWLH")
          .logic9_low_word();
  logic9_runtime.logic9_signals[0] = source.planes;
  logic9_runtime.logic9_signals[1] =
      PackedLogic4::from_logic9_msb_string("0")
          .logic9_low_word()
          .planes;
  const auto logic9_zero =
      PackedLogic4::from_logic9_msb_string("00000000")
          .logic9_low_word()
          .planes;
  logic9_runtime.logic9_signals[2] = logic9_zero;
  logic9_runtime.logic9_signals[3] = logic9_zero;
  logic9_runtime.logic9_signals[4] = logic9_zero;
  auto logic9_descriptor = abi(logic9_runtime);
  assert(
      jit.execute(
          jit.lookup(logic9_symbol), logic9_descriptor)
      == JitExecutionStatus::completed);
  assert(
      logic9_runtime.logic9_signals[1]
      == PackedLogic4::from_logic9_msb_string("W")
             .logic9_low_word()
             .planes);
  assert(
      logic9_runtime.logic9_signals[2]
      == PackedLogic4::from_logic9_msb_string("UX01ZHLH")
             .logic9_low_word()
             .planes);
  assert(
      logic9_runtime.logic9_signals[3]
      == PackedLogic4::from_logic9_msb_string("00000H00")
             .logic9_low_word()
             .planes);
  assert(
      logic9_runtime.logic9_signals[4]
      == PackedLogic4::from_logic9_msb_string("00000H00")
             .logic9_low_word()
             .planes);
}

void test_initialized_bval_slot(const JitOptimizationLevel optimization,
                                const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  Process process;
  process.id = 2;
  process.name = "initialized_bval";
  process.register_count = 1;
  process.operations = {
      ReadSignal{0, 0},
      WriteBlocking{1, 0},
      Halt{},
  };
  const std::array<std::uint32_t, 2> widths{8, 8};
  jit.add_process(symbol, process, widths);

  TestRuntime runtime;
  runtime.signals[0] = {UINT64_C(0xa5), UINT64_C(0xff)};
  runtime.leave_bval_untouched = true;
  auto descriptor = abi(runtime);
  assert(jit.execute(jit.lookup(symbol), descriptor) ==
         JitExecutionStatus::completed);
  assert((runtime.signals[1] == EncodedSignal{UINT64_C(0xa5), 0}));
}

void test_direct_signal_read_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
  LlvmJit jit { LlvmJitOptions { optimization, { } } };
  Process process;
  process.id = 106;
  process.name = "direct_signal_read";
  process.register_count = 1;
  process.operations = {
      ReadSignal { 0, 3 },
      WriteBlocking { 0, 0 },
      Halt { },
  };
  const std::array<std::uint32_t, 4> widths { 8, 8, 8, 8 };
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);
  assert((jit.frame_layout(handle).direct_read_signals
      == std::vector<runtime::simir::SignalId> { 3 }));

  TestRuntime runtime;
  runtime.signals[3] = { UINT64_C(0x11), UINT64_C(0) };
  auto descriptor = abi(runtime);
  const std::array<std::uint64_t, 8> aval {
      0, 0, 0, 0, 0, 0, 0, UINT64_C(0xa5)
  };
  const std::array<std::uint64_t, 8> bval {
      0, 0, 0, 0, 0, 0, 0, UINT64_C(0x80)
  };
  const std::array<std::uint32_t, 1> remap { 7 };
  descriptor.direct_signal_aval = aval.data();
  descriptor.direct_signal_bval = bval.data();
  descriptor.direct_read_signals = remap.data();
  descriptor.direct_read_signal_count = 1;
  descriptor.direct_signal_count = 8;
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  assert((runtime.signals[0]
      == EncodedSignal { UINT64_C(0xa5), UINT64_C(0x80) }));
}

void test_direct_update_accumulator_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
  Process process;
  process.id = 104;
  process.name = "direct_update_accumulator";
  process.register_count = 5;
  process.operations = {
      LoadConstant { 0, PackedLogic4::from_aval_bval(8, 0xa5, 0) },
      WriteUpdate { 0, 0 },
      LoadConstant { 1, PackedLogic4::from_aval_bval(4, 0, 0) },
      WriteUpdateSlice { 0, 1, 2 },
      LoadConstant { 2, PackedLogic4::from_aval_bval(2, 3, 0) },
      WriteUpdateSlice { 0, 2, 4 },
      LoadConstant { 3, PackedLogic4::from_aval_bval(32, 7, 0) },
      LoadConstant { 4, PackedLogic4::from_aval_bval(1, 0, 0) },
      WriteUpdateDynamicSlice {
          0, 4, DynamicIndex { 3, 7, 0, 0 } },
      Halt { },
  };
  const std::array<std::uint32_t, 1> widths { 8 };
  LlvmJitOptions options { optimization, { } };
  options.require_direct_update_slots = true;
  LlvmJit jit { options };
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);
  assert((jit.frame_layout(handle).direct_update_signals
      == std::vector<runtime::simir::SignalId> { 0 }));

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  fsim_jit_update_slot_v1 slot { };
  std::array<std::uint64_t, 1> active_words { };
  descriptor.direct_update_slots = &slot;
  descriptor.direct_update_slot_count = 1;
  descriptor.direct_update_active_words = active_words.data();
  descriptor.direct_update_active_word_count = 1;
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  assert(runtime.writes.empty());
  assert(slot.active == 1U);
  assert(slot.mask == UINT64_C(0xff));
  assert(slot.aval == UINT64_C(0x31));
  assert(slot.bval == 0U);
  assert(active_words[0] == UINT64_C(1));

  Process stable_process;
  stable_process.id = 106;
  stable_process.name = "stable_direct_update";
  stable_process.register_count = 1;
  stable_process.operations = {
      LoadConstant { 0, PackedLogic4::from_aval_bval(8, 0xa5, 0) },
      WriteUpdate { 0, 0 },
      Halt { },
  };
  const auto stable_symbol = std::string { symbol } + "_stable";
  jit.add_process(stable_symbol, stable_process, widths);
  const auto stable_handle = jit.lookup(stable_symbol);
  slot = { };
  slot.width = 8U;
  slot.word_count = 1U;
  slot.aval = UINT64_C(0xa5);
  slot.reserved = 1U;
  active_words[0] = 0U;
  assert(jit.execute(stable_handle, descriptor)
         == JitExecutionStatus::completed);
  assert(slot.active == 0U);
  assert(slot.mask == 0U);
  assert(active_words[0] == 0U);

  slot.aval = UINT64_C(0xa4);
  assert(jit.execute(stable_handle, descriptor)
         == JitExecutionStatus::completed);
  assert(slot.active == 1U);
  assert(slot.mask == UINT64_C(0xff));
  assert(slot.aval == UINT64_C(0xa5));
  assert(active_words[0] == UINT64_C(1));

  Process wide_process;
  wide_process.id = 105;
  wide_process.name = "direct_wide_update_accumulator";
  wide_process.register_count = 4;
  auto whole = PackedLogic4(130, Logic4::zero);
  whole.set(0, Logic4::one);
  whole.set(64, Logic4::one);
  whole.set(129, Logic4::one);
  wide_process.operations = {
      LoadConstant { 0, whole },
      WriteUpdate { 0, 0 },
      LoadConstant { 1, PackedLogic4::from_aval_bval(2, 3, 0) },
      WriteUpdateSlice { 0, 1, 63 },
      LoadConstant { 2, PackedLogic4::from_aval_bval(32, 64, 0) },
      LoadConstant { 3, PackedLogic4::from_aval_bval(1, 0, 0) },
      WriteUpdateDynamicSlice {
          0, 3, DynamicIndex { 2, 129, 0, 0 } },
      Halt { },
  };
  const std::array<std::uint32_t, 1> wide_widths { 130 };
  const auto wide_symbol = std::string { symbol } + "_wide";
  jit.add_process(wide_symbol, wide_process, wide_widths);
  const auto wide_handle = jit.lookup(wide_symbol);
  assert((jit.frame_layout(wide_handle).direct_update_signals
      == std::vector<runtime::simir::SignalId> { 0 }));

  TestRuntime wide_runtime;
  auto wide_descriptor = abi(wide_runtime);
  fsim_jit_update_slot_v1 wide_slot { };
  std::array<std::uint64_t, 3> wide_aval { };
  std::array<std::uint64_t, 3> wide_bval { };
  std::array<std::uint64_t, 3> wide_mask { };
  std::array<std::uint64_t, 1> wide_active_words { };
  wide_slot.wide_aval = wide_aval.data();
  wide_slot.wide_bval = wide_bval.data();
  wide_slot.wide_mask = wide_mask.data();
  wide_slot.word_count = 3;
  wide_slot.width = 130;
  wide_descriptor.direct_update_slots = &wide_slot;
  wide_descriptor.direct_update_slot_count = 1;
  wide_descriptor.direct_update_active_words = wide_active_words.data();
  wide_descriptor.direct_update_active_word_count = 1;
  wide_descriptor.execute_signal_operation
      = [](void*, std::uint32_t, std::uint32_t, fsim_jit_frame_v1*) {
          assert(false && "direct wide update used its exact callback");
          return std::uint32_t { };
        };
  assert(jit.execute(wide_handle, wide_descriptor)
      == JitExecutionStatus::completed);
  assert(wide_runtime.writes.empty());
  assert(wide_runtime.packed_signal_write_modes.empty());
  assert(wide_slot.active == 1U);
  assert((wide_mask == std::array<std::uint64_t, 3> {
      std::numeric_limits<std::uint64_t>::max(),
      std::numeric_limits<std::uint64_t>::max(),
      UINT64_C(3) }));
  assert((wide_aval == std::array<std::uint64_t, 3> {
      UINT64_C(0x8000000000000001), UINT64_C(0), UINT64_C(2) }));
  assert((wide_bval == std::array<std::uint64_t, 3> { 0, 0, 0 }));
  assert(wide_active_words[0] == UINT64_C(1));

}

void test_static_trigger_regions_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
  Process process;
  process.id = 107;
  process.name = "static_trigger_regions";
  process.register_count = 2;
  process.static_sensitivity = {
      { 0, EdgeKind::any }, { 1, EdgeKind::any }
  };
  process.static_trigger_regions = {
      { 0, 2, UINT64_C(1) << 0U },
      { 2, 4, UINT64_C(1) << 1U }
  };
  process.operations = {
      LoadConstant { 0, PackedLogic4::from_aval_bval(8, 0x11, 0) },
      WriteBlocking { 2, 0 },
      LoadConstant { 1, PackedLogic4::from_aval_bval(8, 0x22, 0) },
      WriteBlocking { 3, 1 },
      Halt { }
  };
  const std::array<std::uint32_t, 4> widths { 1, 1, 8, 8 };
  LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  options.debug_instrumentation = false;
  LlvmJit jit { options };
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  const auto run = [&](const std::uint64_t trigger_mask) {
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    descriptor.static_trigger_mask = trigger_mask;
    assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
    std::vector<std::uint32_t> signals;
    for (const auto& [signal, value] : runtime.writes) {
      (void)value;
      signals.push_back(signal);
    }
    return signals;
  };
  assert((run(UINT64_C(1) << 0U) == std::vector<std::uint32_t> { 2 }));
  assert((run(UINT64_C(1) << 1U) == std::vector<std::uint32_t> { 3 }));
  assert((run(Process::full_static_trigger_mask)
      == std::vector<std::uint32_t> { 2, 3 }));
}

} // namespace fsim::tests::compiler

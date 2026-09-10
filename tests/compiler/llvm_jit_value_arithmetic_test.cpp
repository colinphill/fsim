// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

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

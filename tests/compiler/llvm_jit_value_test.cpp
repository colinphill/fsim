// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

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

  const auto run_failure =
      [&](const std::string_view suffix,
          PackedLogic4 index,
          const JitGeneratedRuntimeErrorReason reason,
          const std::string_view message) {
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
            Halt{}};
        const auto failure_symbol =
            std::string{symbol} + "_" + std::string{suffix};
        jit.add_process(failure_symbol, failure, {});
        TestRuntime failed_runtime;
        auto failed_descriptor = abi(failed_runtime);
        expect_generated_runtime_error(
            [&] {
              (void)jit.execute(
                  jit.lookup(failure_symbol),
                  failed_descriptor);
            },
            2,
            reason,
            message);
      };
  run_failure(
      "range",
      integer(4),
      JitGeneratedRuntimeErrorReason::dynamic_index_range,
      "instruction 2: dynamic packed index is outside the declared range");
  auto unknown = integer(0);
  unknown.set(0, Logic4::x);
  run_failure(
      "unknown",
      std::move(unknown),
      JitGeneratedRuntimeErrorReason::dynamic_index_unknown,
      "instruction 2: dynamic packed index contains an unknown or "
      "high-impedance value");

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

} // namespace fsim::tests::compiler

// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void test_logic9_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol) {
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
      ValueKind::logic4};
  const auto all_high =
      PackedLogic4::from_logic9_msb_string("HHHHHHHH");
  process.operations = {
      ReadSignal{0, 4},
      UnaryNot{1, 0},
      LoadConstant{2, all_high},
      Binary{BinaryOperator::bit_and, 3, 0, 2},
      CopyRegister{4, 0},
      CopyRegister{5, 4},
      LoadConstant{
          6,
          PackedLogic4::from_logic9_msb_string("-01--01-")},
      Binary{BinaryOperator::vhdl_match_equal, 7, 0, 6},
      LoadConstant{
          8,
          PackedLogic4::from_logic9_msb_string("U01ZWLH-")},
      Binary{BinaryOperator::vhdl_match_equal, 9, 0, 8},
      WriteBlocking{0, 1},
      WriteUpdate{1, 3},
      WriteBlocking{2, 4},
      WriteAfter{3, 5, 7},
      WriteBlocking{5, 7},
      WriteBlocking{6, 9},
      FormatDisplay{
          0, OutputFormat::binary, "", "", true, false},
      Halt{}};

  const std::array<std::uint32_t, 7> widths{8, 8, 8, 8, 8, 1, 1};
  const std::array<ValueKind, 7> kinds{
      ValueKind::logic9,
      ValueKind::logic9,
      ValueKind::logic4,
      ValueKind::logic9,
      ValueKind::logic9,
      ValueKind::logic4,
      ValueKind::logic4};
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  assert(jit.supports_process(process, widths, kinds));
  jit.add_process(symbol, process, widths, kinds);
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  assert(layout.uses_logic9);
  std::vector<std::uint64_t> register_aval(layout.register_count);
  std::vector<std::uint64_t> register_bval(layout.register_count);
  std::vector<std::uint8_t> register_initialized(
      layout.register_count);
  fsim_jit_frame_v1 frame{};
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

  const auto source =
      PackedLogic4::from_logic9_msb_string("U01ZWLH-");
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
  const auto collapsed =
      fsim::runtime::collapse_to_logic4(source);
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

  auto missing_exact_callback = descriptor;
  missing_exact_callback.read_signal_logic9 = nullptr;
  expect_error(
      [&] {
        (void)jit.execute(
            handle, missing_exact_callback);
      },
      "Logic9 callbacks");
}

void test_rejections() {
  LlvmJit jit;
  const std::array<std::uint32_t, 1> one_signal{1};
  const std::array<std::uint32_t, 0> no_signals{};

  Process empty_call_stack;
  empty_call_stack.name = "empty_call_stack";
  empty_call_stack.register_count = 2;
  empty_call_stack.operations = {
      Call{1, 1, CallStack{0, 1, 0}},
      Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "empty_call_stack", empty_call_stack, no_signals);
      },
      "call-stack capacity must be greater than zero");

  Process oversized_call_stack;
  oversized_call_stack.name = "oversized_call_stack";
  oversized_call_stack.register_count = 2;
  oversized_call_stack.operations = {
      Return{CallStack{0, 1, 2}},
      Halt{}};
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
      Call{99, 1, CallStack{0, 1, 1}},
      Halt{}};
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
      Call{1, 99, CallStack{0, 1, 1}},
      Halt{}};
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
      LoadConstant{
          0,
          PackedLogic4::from_aval_bval(32, 0, 0)},
      IntegerCheck{0, 2, -2},
      Halt{}};
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
      LoadConstant{0, PackedLogic4::from_msb_string("00000001")},
      LoadConstant{1, PackedLogic4::from_msb_string("00000010")},
      IntegerBinary{IntegerBinaryOperator::add, 2, 0, 1},
      Halt{}};
  expect_error(
      [&] {
        jit.add_process(
            "narrow_integer_operation",
            narrow_integer_operation,
            no_signals);
      },
      "width constraints are inconsistent");

  const auto dynamic_part_process =
      [](const DynamicPartSelect selection) {
        Process process;
        process.name = "invalid_dynamic_part_select";
        process.register_count = 3;
        process.operations = {
            LoadConstant{
                0,
                PackedLogic4::from_msb_string("1010101111001101")},
            LoadConstant{
                1,
                PackedLogic4::from_aval_bval(32, 4, 0)},
            selection,
            Halt{}};
        return process;
      };
  expect_error(
      [&] {
        jit.add_process(
            "zero_width_dynamic_part",
            dynamic_part_process(
                DynamicPartSelect{
                    2, 0, 1, 15, 0, 0, true, true, false}),
            no_signals);
      },
      "DynamicPartSelect width must be from 1 through 64");
  expect_error(
      [&] {
        jit.add_process(
            "wide_dynamic_part",
            dynamic_part_process(
                DynamicPartSelect{
                    2, 0, 1, 15, 0, 65, true, true, false}),
            no_signals);
      },
      "DynamicPartSelect width must be from 1 through 64");
  expect_error(
      [&] {
        jit.add_process(
            "unrepresentable_dynamic_part",
            dynamic_part_process(
                DynamicPartSelect{
                    2,
                    0,
                    1,
                    std::int64_t{1} << 32,
                    0,
                    4,
                    true,
                    true,
                    false}),
            no_signals);
      },
      "DynamicPartSelect bounds must fit signed 32-bit integers");
  expect_error(
      [&] {
        jit.add_process(
            "mismatched_dynamic_part_range",
            dynamic_part_process(
                DynamicPartSelect{
                    2, 0, 1, 7, 0, 4, true, true, false, 12}),
            no_signals);
      },
      "DynamicPartSelect declared range is outside its source register");
  const auto dynamic_part_write_process =
      [](const Operation operation) {
        Process process;
        process.name = "invalid_dynamic_part_write";
        process.register_count = 4;
        process.operations = {
            LoadConstant{
                0,
                PackedLogic4::from_msb_string("1010101111001101")},
            LoadConstant{
                1, PackedLogic4::from_msb_string("1100")},
            LoadConstant{
                2, PackedLogic4::from_aval_bval(32, 4, 0)},
            operation,
            Halt{}};
        return process;
      };
  expect_error(
      [&] {
        jit.add_process(
            "zero_width_dynamic_part_insert",
            dynamic_part_write_process(
                DynamicPartInsert{
                    3, 0, 1,
                    DynamicPartIndex{
                        2, 7, 0, 0, 0, true, true}}),
            no_signals);
      },
      "dynamic part-select write width must be from 1 through 64");
  expect_error(
      [&] {
        jit.add_process(
            "outside_dynamic_part_insert",
            dynamic_part_write_process(
                DynamicPartInsert{
                    3, 0, 1,
                    DynamicPartIndex{
                        2, 7, 0, 12, 4, true, true}}),
            no_signals);
      },
      "dynamic part-select write range is outside its packed target");
  const std::array<std::uint32_t, 1> wide_signal{16};
  expect_error(
      [&] {
        jit.add_process(
            "outside_dynamic_part_signal_write",
            dynamic_part_write_process(
                WriteBlockingDynamicPartSlice{
                    0, 1,
                    DynamicPartIndex{
                        2, 7, 0, 12, 4, true, true}}),
            wide_signal);
      },
      "dynamic part-select write range is outside its packed target");

  const auto force_release_process =
      [](const Operation operation) {
        Process process;
        process.name = "invalid_force_release";
        process.register_count = 1;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("1010")},
            operation,
            Halt{}};
        return process;
      };
  const std::array<std::uint32_t, 1> four_bit_signal{4};
  expect_error(
      [&] {
        jit.add_process(
            "outside_force_slice",
            force_release_process(ForceSignalSlice{0, 0, 1}),
            four_bit_signal);
      },
      "ForceSignalSlice range is outside its signal");
  expect_error(
      [&] {
        jit.add_process(
            "zero_width_release_slice",
            force_release_process(ReleaseSignalSlice{0, 0, 0}),
            four_bit_signal);
      },
      "ReleaseSignalSlice width must be greater than zero");
  expect_error(
      [&] {
        jit.add_process(
            "outside_release_slice",
            force_release_process(ReleaseSignalSlice{0, 3, 2}),
            four_bit_signal);
      },
      "ReleaseSignalSlice range is outside its signal");
  const auto invalid_profile_process =
      [&](const std::uint32_t width,
          const ExpressionSizingKind sizing,
          const ExpressionValueDomain domain) {
        auto process = dynamic_part_process(
            DynamicPartSelect{
                2, 0, 1, 15, 0, 4, true, true, false});
        process.expression_profiles = {
            ExpressionProfile{
                SourceLocation{"invalid-profile.sv", 3, 7},
                width,
                false,
                sizing,
                domain}};
        return process;
      };
  expect_error(
      [&] {
        jit.add_process(
            "zero_width_expression_profile",
            invalid_profile_process(
                0,
                ExpressionSizingKind::self_determined,
                ExpressionValueDomain::four_state),
            no_signals);
      },
      "expression profile width must be greater than zero");
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
  invalid_container_predicate.name =
      "invalid_container_predicate";
  invalid_container_predicate.container_register_count = 2;
  invalid_container_predicate.container_register_types = {
      locator_queue, locator_queue};
  invalid_container_predicate.operations = {
      LocateContainer{
          ContainerLocatorOperator::find, 0, 1, {}, {}},
      Halt{}};
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
      LocateContainer{
          ContainerLocatorOperator::find, 0, 1,
          {{ContainerPredicateOperator::index, 0, 0,
            PackedLogic4{},
            ContainerPredicateValueKind::element}},
          {}},
      Halt{}};
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
      LocateContainer{
          ContainerLocatorOperator::find, 0, 1,
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::index, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::index},
              {ContainerPredicateOperator::equal, 0, 1,
               PackedLogic4{},
               ContainerPredicateValueKind::logical},
          },
          {}},
      Halt{}};
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
      locator_queue};
  invalid_reduction_root.operations = {
      ContainerReduction{
          ContainerReductionOperator::sum, 0, 0,
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(8, 0, 0),
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::greater, 0, 1,
               PackedLogic4{},
               ContainerPredicateValueKind::logical},
          }},
      Halt{}};
  expect_error(
      [&] {
        jit.add_process(
            "invalid_reduction_root",
            invalid_reduction_root, no_signals);
      },
      "transformation root has the wrong type");

  auto invalid_reduction_conditional =
      invalid_reduction_root;
  invalid_reduction_conditional.name =
      "invalid_reduction_conditional";
  invalid_reduction_conditional.operations = {
      ContainerReduction{
          ContainerReductionOperator::sum, 0, 0,
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(8, 0, 0),
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::conditional, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element, 99},
          }},
      Halt{}};
  expect_error(
      [&] {
        jit.add_process(
            "invalid_reduction_conditional",
            invalid_reduction_conditional, no_signals);
      },
      "conditional operands are invalid");

  auto invalid_reduction_branch =
      invalid_reduction_root;
  invalid_reduction_branch.name =
      "invalid_reduction_branch";
  invalid_reduction_branch.operations = {
      ContainerReduction{
          ContainerReductionOperator::sum, 0, 0,
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::index, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::index},
              {ContainerPredicateOperator::conditional, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element, 1},
          }},
      Halt{}};
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
      ContainerPredicateNode{
          ContainerPredicateOperator::item,
          0,
          0,
          PackedLogic4{},
          ContainerPredicateValueKind::element});
  oversized_reduction.operations = {
      ContainerReduction{
          ContainerReductionOperator::sum, 0, 0,
          std::move(oversized_graph)},
      Halt{}};
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
      locator_queue};
  invalid_ordering_root.operations = {
      OrderContainer{
          ContainerOrderingOperator::ascending, 0,
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(8, 0, 0),
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::greater, 0, 1,
               PackedLogic4{},
               ContainerPredicateValueKind::logical},
          }},
      Halt{}};
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
      OrderContainer{
          ContainerOrderingOperator::reverse, 0,
          {{ContainerPredicateOperator::item, 0, 0,
            PackedLogic4{},
            ContainerPredicateValueKind::element}}},
      Halt{}};
  expect_error(
      [&] {
        jit.add_process(
            "invalid_reverse_key",
            invalid_reverse_key, no_signals);
      },
      "key metadata requires sort or rsort");

  auto invalid_ordering_conditional =
      invalid_ordering_root;
  invalid_ordering_conditional.name =
      "invalid_ordering_conditional";
  invalid_ordering_conditional.operations = {
      OrderContainer{
          ContainerOrderingOperator::ascending, 0,
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::conditional, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element, 99},
          }},
      Halt{}};
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
      ContainerPredicateNode{
          ContainerPredicateOperator::item,
          0,
          0,
          PackedLogic4{},
          ContainerPredicateValueKind::element});
  oversized_ordering.operations = {
      OrderContainer{
          ContainerOrderingOperator::ascending, 0,
          std::move(oversized_ordering_graph)},
      Halt{}};
  expect_error(
      [&] {
        jit.add_process(
            "oversized_ordering",
            oversized_ordering, no_signals);
      },
      "invalid key metadata");

  Process invalid_locator_transformation_root;
  invalid_locator_transformation_root.name =
      "invalid_locator_transformation_root";
  invalid_locator_transformation_root.container_register_count = 2;
  invalid_locator_transformation_root.container_register_types = {
      locator_queue, locator_queue};
  invalid_locator_transformation_root.operations = {
      LocateContainer{
          ContainerLocatorOperator::minimum, 0, 1, {},
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(8, 0, 0),
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::greater, 0, 1,
               PackedLogic4{},
               ContainerPredicateValueKind::logical},
          }},
      Halt{}};
  expect_error(
      [&] {
        jit.add_process(
            "invalid_locator_transformation_root",
            invalid_locator_transformation_root, no_signals);
      },
      "transformation root has the wrong type");

  auto invalid_find_transformation =
      invalid_locator_transformation_root;
  invalid_find_transformation.name =
      "invalid_find_transformation";
  invalid_find_transformation.operations = {
      LocateContainer{
          ContainerLocatorOperator::find, 0, 1,
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(8, 0, 0),
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::greater, 0, 1,
               PackedLogic4{},
               ContainerPredicateValueKind::logical},
          },
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
          }},
      Halt{}};
  expect_error(
      [&] {
        jit.add_process(
            "invalid_find_transformation",
            invalid_find_transformation, no_signals);
      },
      "transformation metadata requires");

  auto invalid_locator_conditional =
      invalid_locator_transformation_root;
  invalid_locator_conditional.name =
      "invalid_locator_conditional";
  invalid_locator_conditional.operations = {
      LocateContainer{
          ContainerLocatorOperator::unique, 0, 1, {},
          {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::conditional, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element, 99},
          }},
      Halt{}};
  expect_error(
      [&] {
        jit.add_process(
            "invalid_locator_conditional",
            invalid_locator_conditional, no_signals);
      },
      "conditional operands are invalid");

  auto oversized_locator_transformation =
      invalid_locator_transformation_root;
  oversized_locator_transformation.name =
      "oversized_locator_transformation";
  std::vector<ContainerPredicateNode> oversized_locator_graph(
      maximum_container_predicate_nodes + 1U,
      ContainerPredicateNode{
          ContainerPredicateOperator::item,
          0,
          0,
          PackedLogic4{},
          ContainerPredicateValueKind::element});
  oversized_locator_transformation.operations = {
      LocateContainer{
          ContainerLocatorOperator::maximum, 0, 1, {},
          std::move(oversized_locator_graph)},
      Halt{}};
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
            LoadConstant{
                0, PackedLogic4::from_msb_string("1")},
            WriteProjected{
                0, 0, delay, rejection, mode},
            Halt{},
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
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      LoadConstant{1, PackedLogic4::from_msb_string("0")},
      WriteProjectedWaveform{
          0,
          {{0, 5}, {1, 5}},
          0,
          ProjectedDelayMode::transport},
      Halt{},
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
  empty_wait_on.operations = {WaitOn{}, Halt{}};
  expect_fatal_error(
      [&] { jit.add_process("empty_wait_on", empty_wait_on, one_signal); },
      "WaitOn requires at least one signal");

  Process timeout_metadata_without_timeout;
  timeout_metadata_without_timeout.id = 0;
  timeout_metadata_without_timeout.name =
      "timeout_metadata_without_timeout";
  timeout_metadata_without_timeout.register_count = 1;
  WaitOn incomplete_timeout{{0}};
  incomplete_timeout.timeout_result = 0;
  timeout_metadata_without_timeout.operations = {
      std::move(incomplete_timeout), Halt{}};
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
  mismatched_timeout_rearm.name =
      "mismatched_timeout_rearm";
  mismatched_timeout_rearm.register_count = 1;
  WaitOn timeout_origin{{0}};
  timeout_origin.timeout = 2;
  timeout_origin.timeout_result = 0;
  WaitOn timeout_rearm{{0}};
  timeout_rearm.timeout = 3;
  timeout_rearm.timeout_result = 0;
  timeout_rearm.timeout_origin = 0;
  mismatched_timeout_rearm.operations = {
      std::move(timeout_origin),
      std::move(timeout_rearm),
      Halt{},
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
      WaitOn{{0}, {EdgeKind::posedge, EdgeKind::negedge}}, Halt{}};
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
  empty_static_wait.operations = {WaitSensitivity{}, Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "empty_static_wait", empty_static_wait, one_signal);
      },
      "WaitSensitivity requires a static sensitivity list");

  Process invalid_static_signal;
  invalid_static_signal.id = 0;
  invalid_static_signal.name = "invalid_static_signal";
  invalid_static_signal.static_sensitivity = {{1, EdgeKind::any}};
  invalid_static_signal.operations = {Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "invalid_static_signal", invalid_static_signal, one_signal);
      },
      "signal ID is outside signal_widths");

  Process vector_edge;
  vector_edge.id = 0;
  vector_edge.name = "vector_edge";
  vector_edge.static_sensitivity = {{0, EdgeKind::posedge}};
  vector_edge.operations = {WaitSensitivity{}, Halt{}};
  const std::array<std::uint32_t, 1> vector_signal{8};
  expect_fatal_error(
      [&] { jit.add_process("vector_edge", vector_edge, vector_signal); },
      "edge sensitivity requires a scalar signal");

  Process invalid_edge;
  invalid_edge.id = 0;
  invalid_edge.name = "invalid_edge";
  invalid_edge.static_sensitivity = {
      {0, static_cast<EdgeKind>(UINT8_MAX)}};
  invalid_edge.operations = {WaitSensitivity{}, Halt{}};
  expect_fatal_error(
      [&] { jit.add_process("invalid_edge", invalid_edge, one_signal); },
      "static sensitivity has an invalid edge kind");

  Process vector_dynamic_edge;
  vector_dynamic_edge.id = 0;
  vector_dynamic_edge.name = "vector_dynamic_edge";
  vector_dynamic_edge.operations = {
      WaitOn{{0}, {EdgeKind::posedge}}, Halt{}};
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
      WaitOn{{0}, {static_cast<EdgeKind>(UINT8_MAX)}}, Halt{}};
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
  zero_width_wait.operations = {WaitOn{{0}}, Halt{}};
  const std::array<std::uint32_t, 1> zero_width_signal{0};
  expect_fatal_error(
      [&] {
        jit.add_process(
            "zero_width_wait", zero_width_wait, zero_width_signal);
      },
      "signal width must be greater than zero");

  const std::array<std::uint32_t, 1> scalar_signal{1};
  std::uint32_t scheduled_suffix = 0;
  for (const auto &operation :
       std::array<Operation, 2>{WriteUpdate{0, 1},
                                WriteAfter{0, 1, UINT64_MAX}}) {
    Process bad_source;
    bad_source.id = 0;
    bad_source.name = "scheduled_bad_source";
    bad_source.register_count = 1;
    bad_source.operations = {operation, Halt{}};
    expect_fatal_error(
        [&] {
          jit.add_process(
              "scheduled_bad_source_" +
                  std::to_string(scheduled_suffix++),
              bad_source, scalar_signal);
        },
        "source register ID is out of range");
  }
  for (const auto &operation :
       std::array<Operation, 2>{WriteUpdate{1, 0},
                                WriteAfter{1, 0, UINT64_MAX}}) {
    Process bad_signal;
    bad_signal.id = 0;
    bad_signal.name = "scheduled_bad_signal";
    bad_signal.register_count = 1;
    bad_signal.operations = {
        LoadConstant{0, PackedLogic4::from_msb_string("0")},
        operation,
        Halt{},
    };
    expect_fatal_error(
        [&] {
          jit.add_process(
              "scheduled_bad_signal_" +
                  std::to_string(scheduled_suffix++),
              bad_signal, scalar_signal);
        },
        "signal ID is outside signal_widths");
  }
  for (const auto &operation :
       std::array<Operation, 2>{WriteUpdate{0, 0},
                                WriteAfter{0, 0, UINT64_MAX}}) {
    Process bad_width;
    bad_width.id = 0;
    bad_width.name = "scheduled_bad_width";
    bad_width.register_count = 1;
    bad_width.operations = {
        LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
        operation,
        Halt{},
    };
    expect_fatal_error(
        [&] {
          jit.add_process(
              "scheduled_bad_width_" +
                  std::to_string(scheduled_suffix++),
              bad_width, scalar_signal);
        },
        "register width constraints are inconsistent");
  }

  Process invalid_extract;
  invalid_extract.id = 0;
  invalid_extract.name = "invalid_extract";
  invalid_extract.register_count = 2;
  invalid_extract.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string("1010")},
      Extract{1, 0, 3, 2},
      Halt{},
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
      LoadConstant{
          0, PackedLogic4::from_msb_string("1010")},
      LoadConstant{
          1, PackedLogic4::from_msb_string("11")},
      Insert{2, 0, 1, 3},
      Halt{},
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
      LoadConstant{
          0, PackedLogic4::from_msb_string("11")},
      WriteUpdateSlice{0, 0, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 1> nibble_signal{4};
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
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      LoadConstant{
          1, PackedLogic4::from_msb_string("10")},
      Concatenate{2, {0, 1}, 4},
      Halt{},
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
  too_wide.operations = {ReadSignal{0, 0}, Halt{}};
  const std::array<std::uint32_t, 1> widths{65};
  expect_unsupported(
      [&] { jit.add_process("wide", too_wide, widths); },
      "widths in [1, 64]");

  Process zero_width;
  zero_width.id = 0;
  zero_width.name = "zero_width";
  zero_width.register_count = 1;
  zero_width.operations = {ReadSignal{0, 0}, Halt{}};
  const std::array<std::uint32_t, 1> invalid_widths{0};
  expect_fatal_error(
      [&] { jit.add_process("zero_width", zero_width, invalid_widths); },
      "signal width must be greater than zero");

  Process too_many_registers;
  too_many_registers.id = 0;
  too_many_registers.name = "too_many_registers";
  too_many_registers.register_count =
      static_cast<std::size_t>(
          std::numeric_limits<RegisterId>::max()) +
      1U;
  too_many_registers.operations = {Halt{}};
  const std::array<std::uint32_t, 0> no_signal_widths{};
  expect_unsupported(
      [&] {
        jit.add_process(
            "too_many_registers", too_many_registers, no_signal_widths);
      },
      "too many registers");

  Process unsupported_then_bad_signal;
  unsupported_then_bad_signal.id = 0;
  unsupported_then_bad_signal.name = "unsupported_then_bad_signal";
  unsupported_then_bad_signal.operations = {WaitOn{{0}}, WaitOn{{1}},
                                             Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process("unsupported_then_bad_signal",
                        unsupported_then_bad_signal, one_signal);
      },
      "signal ID is outside signal_widths");

  Process unsupported_then_bad_target;
  unsupported_then_bad_target.id = 0;
  unsupported_then_bad_target.name = "unsupported_then_bad_target";
  unsupported_then_bad_target.operations = {WaitOn{{0}}, Jump{99}, Halt{}};
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
      WaitOn{{0}}, WriteBlocking{0, 0}, Halt{}};
  expect_fatal_error(
      [&] {
        jit.add_process("unsupported_then_undefined_use",
                        unsupported_then_undefined_use, one_signal);
      },
      "source register is never defined");

  Process unsupported_then_width_contradiction;
  unsupported_then_width_contradiction.id = 0;
  unsupported_then_width_contradiction.name =
      "unsupported_then_width_contradiction";
  unsupported_then_width_contradiction.register_count = 1;
  unsupported_then_width_contradiction.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WaitOn{{0}},
      WriteBlocking{0, 0},
      Halt{},
  };
  const std::array<std::uint32_t, 1> byte_signal_for_contradiction{8};
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
  bad_jump.operations = {Jump{2}, Halt{}};
  expect_fatal_error(
      [&] { jit.add_process("bad_jump", bad_jump, no_signals); },
      "jump target is outside");

  Process bad_branch;
  bad_branch.id = 0;
  bad_branch.name = "bad_branch";
  bad_branch.register_count = 1;
  bad_branch.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      Branch{0, 2, 3, UnknownBranchPolicy::when_false},
      Halt{},
  };
  expect_fatal_error(
      [&] { jit.add_process("bad_branch", bad_branch, no_signals); },
      "branch false target is outside");

  Process bad_fork;
  bad_fork.id = 0;
  bad_fork.name = "bad_fork";
  bad_fork.operations = {Fork{{1}, ForkJoinKind::all}, ForkEnd{}};
  expect_fatal_error(
      [&] { jit.add_process("bad_fork", bad_fork, no_signals); },
      "fork branch must follow its parent continuation");

  Process fork_without_continuation;
  fork_without_continuation.id = 0;
  fork_without_continuation.name = "fork_without_continuation";
  fork_without_continuation.operations = {
      Fork{{}, ForkJoinKind::all}};
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
      Fork{{2}, static_cast<ForkJoinKind>(99)}, Halt{}, ForkEnd{}};
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
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      Branch{0, 2, 4, UnknownBranchPolicy::when_false},
      LoadConstant{1, PackedLogic4::from_msb_string("10100101")},
      Jump{5},
      Jump{5},
      WriteBlocking{0, 1},
      Halt{},
  };
  const std::array<std::uint32_t, 1> byte_signal{8};
  expect_fatal_error(
      [&] {
        jit.add_process("path_use_before_definition",
                        path_use_before_definition, byte_signal);
      },
      "register may be used before definition on a control-flow path");

  Process zero_time_cycle;
  zero_time_cycle.id = 0;
  zero_time_cycle.name = "zero_time_cycle";
  zero_time_cycle.operations = {Jump{0}, Halt{}};
  expect_unsupported(
      [&] {
        jit.add_process("zero_time_cycle", zero_time_cycle, no_signals);
      },
      "cycle has no suspension safe point");

  Process reachable_cycle;
  reachable_cycle.id = 0;
  reachable_cycle.name = "reachable_cycle";
  reachable_cycle.register_count = 1;
  reachable_cycle.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      Branch{0, 1, 2, UnknownBranchPolicy::when_false},
      Halt{},
  };
  expect_unsupported(
      [&] { jit.add_process("reachable_cycle", reachable_cycle, no_signals); },
      "cycle has no suspension safe point");

  Process debug_safe_cycle;
  debug_safe_cycle.id = 0;
  debug_safe_cycle.name = "debug_safe_cycle";
  debug_safe_cycle.register_count = 1;
  debug_safe_cycle.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      DebugPoint{
          DebugPointKind::statement,
          SourceLocation{"runtime_loop.sv", 4, 5}},
      Branch{0, 3, 5, UnknownBranchPolicy::when_false},
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      Jump{1},
      Halt{},
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
        const std::array<std::uint32_t, 8> valid_widths{8, 8, 8, 8,
                                                        8, 8, 8, 1};
        jit.add_process("not-a-c-identifier", make_arithmetic_process(),
                        valid_widths);
      },
      "C identifier");
}

} // namespace fsim::tests::compiler

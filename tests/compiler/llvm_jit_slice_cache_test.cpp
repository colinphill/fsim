// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {
namespace {

enum class SliceConsumer : std::uint8_t {
  size,
  reduction,
  minimum,
  find_index,
};

enum class SliceCallMode : std::uint8_t {
  function_input,
  task_input,
  task_output,
  task_inout,
};

enum class SlicePortMode : std::uint8_t {
  input,
  output,
  inout,
};

void expect_slice_cache_statistics(
    const LlvmJit& jit,
    const std::uint64_t hits,
    const std::uint64_t misses) {
  const auto statistics = jit.cache_statistics();
  assert(statistics.hits == hits);
  assert(statistics.misses == misses);
  assert(statistics.stores == misses);
  assert(statistics.rejected_entries == 0);
  assert(statistics.load_failures == 0);
  assert(statistics.store_failures == 0);
  assert(statistics.prune_failures == 0);
}

[[nodiscard]] std::size_t slice_cached_object_count(
    const std::filesystem::path& root) {
  if (!std::filesystem::exists(root)) {
    return 0;
  }
  return static_cast<std::size_t>(
      std::ranges::count_if(
          std::filesystem::recursive_directory_iterator{root},
          [](const auto& entry) {
            return entry.is_regular_file()
                && entry.path().extension() == ".fobj";
          }));
}

[[nodiscard]] Process make_slice_consumer_process(
    const std::int32_t left,
    const std::uint32_t element_width,
    const SliceConsumer consumer,
    const std::optional<std::uint8_t> graph_constant,
    const std::uint32_t source_line) {
  ContainerType slice;
  slice.element_width = element_width;
  slice.fixed = true;
  slice.index_left = left;
  slice.index_right = left - 2;
  ContainerType result;
  result.element_width = element_width;
  result.queue = true;
  if (consumer == SliceConsumer::find_index) {
    result.element_width = 32;
    result.two_state = true;
    result.signed_elements = true;
  }

  Process process;
  process.id = 33;
  process.name = "cached_static_slice_consumer";
  process.register_count = 1;
  process.container_register_count = 2;
  process.container_register_types = {slice, result};
  process.operations.push_back(
      DebugPoint{
          DebugPointKind::statement,
          SourceLocation{
              "cached_static_slice_consumer.sv",
              source_line,
              5}});
  if (consumer == SliceConsumer::size) {
    process.operations.push_back(ContainerSize{0, 0});
  } else if (consumer == SliceConsumer::reduction) {
    std::vector<ContainerPredicateNode> transformation;
    if (graph_constant) {
      transformation.push_back(
          ContainerPredicateNode{
              ContainerPredicateOperator::constant,
              0,
              0,
              PackedLogic4::from_aval_bval(
                  element_width, *graph_constant, 0),
              ContainerPredicateValueKind::element});
    }
    process.operations.push_back(
        ContainerReduction{
            ContainerReductionOperator::sum,
            0,
            0,
            std::move(transformation)});
  } else if (consumer == SliceConsumer::minimum) {
    process.operations.push_back(
        LocateContainer{
            ContainerLocatorOperator::minimum,
            1,
            0,
            {},
            {}});
  } else {
    const auto constant =
        graph_constant.value_or(std::uint8_t{0});
    std::vector<ContainerPredicateNode> predicate{
        ContainerPredicateNode{
            ContainerPredicateOperator::index,
            0,
            0,
            PackedLogic4{},
            ContainerPredicateValueKind::index},
        ContainerPredicateNode{
            ContainerPredicateOperator::constant,
            0,
            0,
            PackedLogic4::from_aval_bval(
                32, constant, 0),
            ContainerPredicateValueKind::index},
        ContainerPredicateNode{
            ContainerPredicateOperator::equal,
            0,
            1,
            PackedLogic4{},
            ContainerPredicateValueKind::logical},
    };
    process.operations.push_back(
        LocateContainer{
            ContainerLocatorOperator::find_index,
            1,
            0,
            std::move(predicate),
            {}});
  }
  process.operations.push_back(Halt{});
  return process;
}

[[nodiscard]] Process make_slice_call_process(
    const std::int32_t actual_left,
    const std::int32_t formal_left,
    const std::uint32_t element_width,
    const SliceCallMode mode,
    const std::string_view callable_source,
    const std::uint32_t return_line) {
  ContainerType actual;
  actual.element_width = element_width;
  actual.fixed = true;
  actual.index_left = actual_left;
  actual.index_right = actual_left - 1;
  auto formal = actual;
  formal.index_left = formal_left;
  formal.index_right = formal_left - 1;

  Process process;
  process.id = 41;
  process.name = "cached_static_slice_call";
  process.register_count = 4;
  process.container_register_count = 4;
  process.container_register_types = {
      actual, formal, formal, formal};
  process.operations.push_back(
      DebugPoint{
          DebugPointKind::call,
          SourceLocation{
              "cached_static_slice_call.sv", 19, 7}});
  process.operations.push_back(
      LoadConstant{
          0,
          PackedLogic4::from_aval_bval(
              32,
              static_cast<std::uint32_t>(actual_left),
              0)});
  process.operations.push_back(
      ContainerRead{1, 0, 0, true});
  process.operations.push_back(
      LoadConstant{
          2,
          PackedLogic4::from_aval_bval(
              32,
              static_cast<std::uint32_t>(formal_left),
              0)});
  process.operations.push_back(
      ContainerWrite{1, 2, 1, true});
  if (mode == SliceCallMode::task_output) {
    process.operations.push_back(
        CopyContainerRegister{2, 3});
  } else {
    process.operations.push_back(
        CopyContainerRegister{2, 1});
  }
  process.operations.push_back(
      DebugPoint{
          DebugPointKind::statement,
          SourceLocation{
              std::string{callable_source},
              return_line,
              3}});
  if (mode == SliceCallMode::task_output
      || mode == SliceCallMode::task_inout) {
    process.operations.push_back(
        ContainerRead{3, 2, 2, true});
    process.operations.push_back(
        ContainerWrite{0, 0, 3, true});
  } else if (mode == SliceCallMode::task_input) {
    process.operations.push_back(
        DebugPoint{
            DebugPointKind::wait,
            SourceLocation{
                std::string{callable_source},
                return_line + 1U,
                3}});
  }
  process.operations.push_back(Halt{});
  return process;
}

[[nodiscard]] Process make_slice_ordering_process(
    const std::int32_t selected_left,
    const std::uint32_t element_width,
    const ContainerOrderingOperator ordering,
    const std::optional<std::uint8_t> key_constant,
    const bool atomic_commit,
    const std::uint32_t source_line) {
  ContainerType whole;
  whole.element_width = element_width;
  whole.fixed = true;
  whole.index_left = selected_left + 1;
  whole.index_right = selected_left - 2;
  auto selected = whole;
  selected.index_left = selected_left;
  selected.index_right = selected_left - 1;

  Process process;
  process.id = 42;
  process.name = "cached_static_slice_ordering";
  process.register_count = 4;
  process.container_register_count = 3;
  process.container_register_types = {
      whole, selected, whole};
  process.operations.push_back(
      DebugPoint{
          DebugPointKind::statement,
          SourceLocation{
              "cached_static_slice_ordering.sv",
              source_line,
              5}});
  for (std::uint32_t ordinal = 0; ordinal < 2; ++ordinal) {
    const auto index =
        static_cast<std::uint32_t>(
            selected_left - static_cast<std::int32_t>(ordinal));
    process.operations.push_back(
        LoadConstant{
            0,
            PackedLogic4::from_aval_bval(32, index, 0)});
    process.operations.push_back(ContainerRead{1, 0, 0, true});
    process.operations.push_back(
        LoadConstant{
            2,
            PackedLogic4::from_aval_bval(32, index, 0)});
    process.operations.push_back(ContainerWrite{1, 2, 1, true});
  }
  std::vector<ContainerPredicateNode> key;
  if (key_constant) {
    key.push_back(
        ContainerPredicateNode{
            ContainerPredicateOperator::constant,
            0,
            0,
            PackedLogic4::from_aval_bval(
                element_width, *key_constant, 0),
            ContainerPredicateValueKind::element});
  }
  process.operations.push_back(
      OrderContainer{ordering, 1, std::move(key)});
  process.operations.push_back(CopyContainerRegister{2, 0});
  for (std::uint32_t ordinal = 0; ordinal < 2; ++ordinal) {
    const auto index =
        static_cast<std::uint32_t>(
            selected_left - static_cast<std::int32_t>(ordinal));
    process.operations.push_back(
        LoadConstant{
            0,
            PackedLogic4::from_aval_bval(32, index, 0)});
    process.operations.push_back(ContainerRead{1, 1, 0, true});
    process.operations.push_back(
        LoadConstant{
            2,
            PackedLogic4::from_aval_bval(32, index, 0)});
    process.operations.push_back(ContainerWrite{2, 2, 1, true});
  }
  if (atomic_commit) {
    process.operations.push_back(CopyContainerRegister{0, 2});
  }
  process.operations.push_back(Halt{});
  return process;
}

[[nodiscard]] Process make_slice_port_process(
    const std::int32_t actual_left,
    const std::int32_t formal_left,
    const std::int32_t formal_right,
    const std::uint32_t element_width,
    const bool two_state,
    const bool signed_elements,
    const SlicePortMode mode,
    const ContainerObjectId object,
    const std::string_view specialization,
    const std::string_view source_path,
    const std::uint32_t source_line) {
  // The selected actual range belongs to the runtime ContainerSliceAlias,
  // not to native code. Varying it must therefore reuse an otherwise
  // identical process object while the formal profile and binding operation
  // remain cache-key inputs.
  (void)actual_left;
  ContainerType formal;
  formal.element_width = element_width;
  formal.two_state = two_state;
  formal.signed_elements = signed_elements;
  formal.fixed = true;
  formal.index_left = formal_left;
  formal.index_right = formal_right;

  Process process;
  process.id = 43;
  process.name =
      "cached_static_slice_port." + std::string{specialization};
  process.container_register_count = 1;
  process.container_register_types = {formal};
  process.operations.push_back(
      DebugPoint{
          DebugPointKind::statement,
          SourceLocation{
              std::string{source_path}, source_line, 5}});
  if (mode == SlicePortMode::input
      || mode == SlicePortMode::inout) {
    process.operations.push_back(
        ReadContainerObject{0, object});
  }
  if (mode == SlicePortMode::output
      || mode == SlicePortMode::inout) {
    process.operations.push_back(
        WriteContainerObject{object, 0});
  }
  process.operations.push_back(Halt{});
  return process;
}

[[nodiscard]] Process make_indexed_slice_process(
    const std::int32_t base,
    const std::int32_t width,
    const bool plus,
    const bool declared_descending,
    const std::uint32_t element_width,
    const bool two_state,
    const bool signed_elements,
    const std::uint32_t source_line) {
  assert(width > 0);
  const auto distance =
      static_cast<std::int64_t>(width) - 1;
  const auto lower =
      plus
          ? static_cast<std::int64_t>(base)
          : static_cast<std::int64_t>(base) - distance;
  const auto upper =
      plus
          ? static_cast<std::int64_t>(base) + distance
          : static_cast<std::int64_t>(base);
  ContainerType selected;
  selected.element_width = element_width;
  selected.two_state = two_state;
  selected.signed_elements = signed_elements;
  selected.fixed = true;
  selected.index_left =
      static_cast<std::int32_t>(
          declared_descending ? upper : lower);
  selected.index_right =
      static_cast<std::int32_t>(
          declared_descending ? lower : upper);

  Process process;
  process.id = 44;
  process.name = "cached_static_indexed_slice";
  process.register_count = 1;
  process.container_register_count = 1;
  process.container_register_types = {selected};
  process.operations = {
      DebugPoint{
          DebugPointKind::statement,
          SourceLocation{
              "cached-static-indexed-slice.sv",
              source_line,
              7}},
      ContainerSize{0, 0},
      Halt{}};
  return process;
}

[[nodiscard]] Process make_fixed_array_return_process(
    const std::int32_t result_left,
    const std::int32_t result_right,
    const std::int32_t source_left,
    const std::uint32_t element_width,
    const bool two_state,
    const bool signed_elements,
    const bool selected_return,
    const std::string_view function_source,
    const std::uint32_t function_line) {
  ContainerType result_type;
  result_type.element_width = element_width;
  result_type.two_state = two_state;
  result_type.signed_elements = signed_elements;
  result_type.fixed = true;
  result_type.index_left = result_left;
  result_type.index_right = result_right;
  auto source_type = result_type;
  source_type.index_left = source_left;
  const auto count =
      result_left >= result_right
          ? result_left - result_right
          : result_right - result_left;
  source_type.index_right =
      source_left
      + (result_left >= result_right ? -count : count);
  if (!selected_return) {
    source_type = result_type;
  }

  Process process;
  process.id = 45;
  process.name = "cached_fixed_array_function_return";
  process.register_count = 5;
  process.container_register_count = 4;
  process.container_register_types = {
      result_type, result_type, result_type, source_type};
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{
          1, PackedLogic4::from_aval_bval(32, 0, 0)},
      CopyContainerRegister{0, 1},
      Call{6, 4, CallStack{0, 1, 1}},
      CopyContainerRegister{2, 0},
      Halt{},
      DebugPoint{
          DebugPointKind::statement,
          SourceLocation{
              std::string{function_source},
              function_line,
              5}}};
  if (selected_return) {
    process.operations.push_back(
        LoadConstant{
            2,
            PackedLogic4::from_aval_bval(
                32,
                static_cast<std::uint32_t>(source_left),
                0)});
    process.operations.push_back(
        ContainerRead{4, 3, 2, true});
    process.operations.push_back(
        LoadConstant{
            3,
            PackedLogic4::from_aval_bval(
                32,
                static_cast<std::uint32_t>(result_left),
                0)});
    process.operations.push_back(
        ContainerWrite{0, 3, 4, true});
  } else {
    process.operations.push_back(
        CopyContainerRegister{0, 3});
  }
  process.operations.push_back(Return{CallStack{0, 1, 1}});
  return process;
}

[[nodiscard]] Process make_nonstatic_return_process(
    const ContainerType& type,
    const bool clear_before_return,
    const bool reverse_conditional,
    const bool case_equality,
    const bool reverse_membership,
    const bool reverse_case_inside,
    const bool priority_qualifier,
    const bool wildcard_pattern,
    const std::string_view function_source,
    const std::uint32_t function_line) {
  Process process;
  process.id = 46;
  process.name = "cached_nonstatic_function_return";
  process.register_count = 3;
  process.container_register_count = 4;
  process.container_register_types = {type, type, type, type};
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{
          1, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{
          2, PackedLogic4::from_aval_bval(1, 1, 1)},
      CopyContainerRegister{0, 1},
      Call{15, 5, CallStack{0, 1, 1}},
      CopyContainerRegister{2, 0},
      ConditionalContainerSelect{
          3, 2,
          reverse_conditional ? 1U : 2U,
          reverse_conditional ? 2U : 1U},
      CompareContainers{2, 2, 1, case_equality},
      Binary{
          BinaryOperator::wildcard_equal, 2,
          reverse_membership ? 1U : 0U,
          reverse_membership ? 0U : 1U},
      Binary{
          BinaryOperator::less_equal_unsigned, 2,
          reverse_case_inside ? 1U : 0U,
          reverse_case_inside ? 0U : 1U},
      Branch{
          2,
          reverse_case_inside ? 12U : 11U,
          reverse_case_inside ? 11U : 12U,
          UnknownBranchPolicy::when_false},
      Jump{13},
      Jump{13},
      Report{
          priority_qualifier
              ? "priority case has no matching item"
              : "unique case has multiple matching items",
          AssertionSeverity::warning,
          SourceLocation{"qualified-case.sv", 21, 7}},
      Halt{},
      DebugPoint{
          DebugPointKind::statement,
          SourceLocation{
              std::string{function_source},
              function_line,
              5}}};
  if (wildcard_pattern) {
    process.operations[9] = LoadConstant{
        2, PackedLogic4::from_aval_bval(1, 1, 0)};
  }
  if (clear_before_return) {
    process.operations.push_back(
        DeleteContainer{0, std::nullopt});
  }
  process.operations.push_back(CopyContainerRegister{0, 3});
  process.operations.push_back(Return{CallStack{0, 1, 1}});
  return process;
}

}  // namespace

void test_static_slice_consumer_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol =
      "cached_static_slice_consumer";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_slice_cache_statistics(
            jit, hits, misses);
      };

  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::size, std::nullopt, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::size, std::nullopt, 17),
      1,
      0);
  materialize(
      make_slice_consumer_process(
          3, 8, SliceConsumer::size, std::nullopt, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 4, SliceConsumer::size, std::nullopt, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::reduction, std::nullopt, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::reduction,
          std::uint8_t{1}, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::reduction,
          std::uint8_t{2}, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::minimum, std::nullopt, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::find_index,
          std::uint8_t{3}, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::find_index,
          std::uint8_t{2}, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::size, std::nullopt, 18),
      0,
      1);
  assert(slice_cached_object_count(cache_directory) == 10);
}

void test_static_slice_call_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol =
      "cached_static_slice_call";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_slice_cache_statistics(
            jit, hits, misses);
      };
  const auto make =
      [&](const std::int32_t actual_left = 3,
          const std::int32_t formal_left = 7,
          const std::uint32_t width = 8,
          const SliceCallMode mode =
              SliceCallMode::function_input,
          const std::string_view source = "callable.sv",
          const std::uint32_t line = 31) {
        return make_slice_call_process(
            actual_left, formal_left, width,
            mode, source, line);
      };

  materialize(make(), 0, 1);
  materialize(make(), 1, 0);
  materialize(make(2), 0, 1);
  materialize(make(3, 6), 0, 1);
  materialize(make(3, 7, 4), 0, 1);
  materialize(
      make(
          3, 7, 8,
          SliceCallMode::task_input),
      0,
      1);
  materialize(
      make(
          3, 7, 8,
          SliceCallMode::task_output),
      0,
      1);
  materialize(
      make(
          3, 7, 8,
          SliceCallMode::task_inout),
      0,
      1);
  materialize(
      make(
          3, 7, 8,
          SliceCallMode::function_input,
          "edited-callable.sv"),
      0,
      1);
  materialize(
      make(
          3, 7, 8,
          SliceCallMode::function_input,
          "callable.sv", 32),
      0,
      1);
  assert(slice_cached_object_count(cache_directory) == 9);
}

void test_static_slice_ordering_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol =
      "cached_static_slice_ordering";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_slice_cache_statistics(
            jit, hits, misses);
      };
  const auto make =
      [&](const std::int32_t left = 4,
          const std::uint32_t width = 8,
          const ContainerOrderingOperator ordering =
              ContainerOrderingOperator::reverse,
          const std::optional<std::uint8_t> key = std::nullopt,
          const bool commit = true,
          const std::uint32_t line = 23) {
        return make_slice_ordering_process(
            left, width, ordering, key, commit, line);
      };

  materialize(make(), 0, 1);
  materialize(make(), 1, 0);
  materialize(make(3), 0, 1);
  materialize(make(4, 4), 0, 1);
  materialize(
      make(
          4, 8,
          ContainerOrderingOperator::ascending),
      0,
      1);
  materialize(
      make(
          4, 8,
          ContainerOrderingOperator::descending),
      0,
      1);
  materialize(
      make(
          4, 8,
          ContainerOrderingOperator::ascending,
          std::uint8_t{1}),
      0,
      1);
  materialize(
      make(
          4, 8,
          ContainerOrderingOperator::ascending,
          std::uint8_t{2}),
      0,
      1);
  materialize(make(4, 8, ContainerOrderingOperator::reverse,
                   std::nullopt, false), 0, 1);
  materialize(make(4, 8, ContainerOrderingOperator::reverse,
                   std::nullopt, true, 24), 0, 1);
  assert(slice_cached_object_count(cache_directory) == 9);
}

void test_static_slice_port_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol =
      "cached_static_slice_port";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_slice_cache_statistics(
            jit, hits, misses);
      };
  const auto make =
      [&](const std::int32_t actual_left = 4,
          const std::int32_t formal_left = -2,
          const std::int32_t formal_right = 0,
          const std::uint32_t width = 8,
          const bool two_state = false,
          const bool signed_elements = false,
          const SlicePortMode mode = SlicePortMode::input,
          const ContainerObjectId object = 3,
          const std::string_view specialization = "WIDTH=8",
          const std::string_view source = "slice-port-leaf.sv",
          const std::uint32_t line = 27) {
        return make_slice_port_process(
            actual_left,
            formal_left,
            formal_right,
            width,
            two_state,
            signed_elements,
            mode,
            object,
            specialization,
            source,
            line);
      };

  materialize(make(), 0, 1);
  materialize(make(3), 1, 0);
  materialize(make(4, -3, -1), 0, 1);
  materialize(make(4, 2, 0), 0, 1);
  materialize(make(4, -2, 0, 4), 0, 1);
  materialize(make(4, -2, 0, 8, true), 0, 1);
  materialize(make(4, -2, 0, 8, false, true), 0, 1);
  materialize(
      make(
          4, -2, 0, 8, false, false,
          SlicePortMode::output),
      0,
      1);
  materialize(
      make(
          4, -2, 0, 8, false, false,
          SlicePortMode::inout),
      0,
      1);
  materialize(
      make(
          4, -2, 0, 8, false, false,
          SlicePortMode::input, 4),
      0,
      1);
  materialize(
      make(
          4, -2, 0, 8, false, false,
          SlicePortMode::input, 3, "WIDTH=4"),
      0,
      1);
  materialize(
      make(
          4, -2, 0, 8, false, false,
          SlicePortMode::input, 3, "WIDTH=8",
          "edited-slice-port-leaf.sv"),
      0,
      1);
  materialize(
      make(
          4, -2, 0, 8, false, false,
          SlicePortMode::input, 3, "WIDTH=8",
          "slice-port-leaf.sv", 28),
      0,
      1);
  assert(slice_cached_object_count(cache_directory) == 12);
}

void test_static_indexed_slice_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol =
      "cached_static_indexed_slice";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_slice_cache_statistics(
            jit, hits, misses);
      };
  const auto make =
      [&](const std::int32_t base = 2,
          const std::int32_t width = 3,
          const bool plus = true,
          const bool descending = true,
          const std::uint32_t element_width = 8,
          const bool two_state = false,
          const bool signed_elements = false,
          const std::uint32_t line = 31) {
        return make_indexed_slice_process(
            base,
            width,
            plus,
            descending,
            element_width,
            two_state,
            signed_elements,
            line);
      };

  materialize(make(), 0, 1);
  materialize(make(4, 3, false), 1, 0);
  materialize(make(1), 0, 1);
  materialize(make(2, 2), 0, 1);
  materialize(make(4, 3, false, false), 0, 1);
  materialize(make(2, 3, true, true, 4), 0, 1);
  materialize(make(2, 3, true, true, 8, true), 0, 1);
  materialize(
      make(2, 3, true, true, 8, false, true),
      0,
      1);
  materialize(
      make(2, 3, true, true, 8, false, false, 32),
      0,
      1);
  assert(slice_cached_object_count(cache_directory) == 8);
}

void test_fixed_array_function_return_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol =
      "cached_fixed_array_function_return";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_slice_cache_statistics(jit, hits, misses);
      };
  const auto make =
      [&](const std::int32_t result_left = 3,
          const std::int32_t result_right = 0,
          const std::int32_t source_left = 7,
          const std::uint32_t element_width = 8,
          const bool two_state = false,
          const bool signed_elements = false,
          const bool selected_return = true,
          const std::string_view source =
              "fixed-array-function-return.sv",
          const std::uint32_t line = 18) {
        return make_fixed_array_return_process(
            result_left,
            result_right,
            source_left,
            element_width,
            two_state,
            signed_elements,
            selected_return,
            source,
            line);
      };

  materialize(make(), 0, 1);
  // A second source spelling which normalized to the same return and source
  // ranges has no spelling-only cache dimension.
  materialize(make(), 1, 0);
  materialize(make(4, 1), 0, 1);
  materialize(make(0, 3), 0, 1);
  materialize(make(3, 0, 8), 0, 1);
  materialize(make(3, 0, 7, 4), 0, 1);
  materialize(make(3, 0, 7, 8, true), 0, 1);
  materialize(make(3, 0, 7, 8, false, true), 0, 1);
  materialize(
      make(3, 0, 7, 8, false, false, false), 0, 1);
  materialize(
      make(
          3, 0, 7, 8, false, false, true,
          "edited-fixed-array-function-return.sv"),
      0,
      1);
  materialize(
      make(
          3, 0, 7, 8, false, false, true,
          "fixed-array-function-return.sv", 19),
      0,
      1);
  assert(slice_cached_object_count(cache_directory) == 10);
}

void test_nonstatic_function_return_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol =
      "cached_nonstatic_function_return";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_slice_cache_statistics(jit, hits, misses);
      };
  const auto make =
      [&](const ContainerType& type,
          const bool clear_before_return = false,
          const bool reverse_conditional = false,
          const bool case_equality = false,
          const bool reverse_membership = false,
          const bool reverse_case_inside = false,
          const bool priority_qualifier = false,
          const bool wildcard_pattern = false,
          const std::string_view source =
              "nonstatic-function-return.sv",
          const std::uint32_t line = 18) {
        return make_nonstatic_return_process(
            type, clear_before_return, reverse_conditional,
            case_equality, reverse_membership, reverse_case_inside,
            priority_qualifier, wildcard_pattern, source, line);
      };
  const auto dynamic_type = [] {
    ContainerType type;
    type.element_width = 8;
    return type;
  };

  materialize(make(dynamic_type()), 0, 1);
  materialize(make(dynamic_type()), 1, 0);
  auto queue = dynamic_type();
  queue.queue = true;
  materialize(make(queue), 0, 1);
  queue.maximum_elements = 4;
  materialize(make(queue), 0, 1);
  auto associative = dynamic_type();
  associative.associative = true;
  associative.two_state_indices = true;
  associative.signed_indices = true;
  materialize(make(associative), 0, 1);
  auto changed = associative;
  changed.index_width = 8;
  materialize(make(changed), 0, 1);
  changed = associative;
  changed.two_state_indices = false;
  materialize(make(changed), 0, 1);
  changed = associative;
  changed.signed_indices = false;
  materialize(make(changed), 0, 1);
  changed = dynamic_type();
  changed.element_width = 4;
  materialize(make(changed), 0, 1);
  changed = dynamic_type();
  changed.two_state = true;
  materialize(make(changed), 0, 1);
  changed = dynamic_type();
  changed.signed_elements = true;
  materialize(make(changed), 0, 1);
  materialize(make(dynamic_type(), true), 0, 1);
  materialize(make(dynamic_type(), false, true), 0, 1);
  materialize(make(dynamic_type(), false, false, true), 0, 1);
  materialize(make(dynamic_type(), false, false, false, true), 0, 1);
  materialize(
      make(dynamic_type(), false, false, false, false, true),
      0, 1);
  materialize(
      make(dynamic_type(), false, false, false, false, false, true),
      0, 1);
  materialize(
      make(dynamic_type(), false, false, false, false, false, false, true),
      0, 1);
  materialize(
      make(
          dynamic_type(), false, false, false, false, false, false, false,
          "edited-nonstatic-function-return.sv"),
      0,
      1);
  materialize(make(dynamic_type(), false, false, false, false, false, false,
                   false,
                   "nonstatic-function-return.sv", 19),
              0, 1);
  assert(slice_cached_object_count(cache_directory) == 19);
}

void test_expression_selection_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol =
      "cached_expression_selection";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto make =
      [](const std::uint32_t selection_width = 4,
         const bool increasing = true,
         const bool source_descending = true,
         const bool two_state = false,
         const std::string_view source_path =
             "expression-selection.sv",
         const std::uint32_t source_line = 17,
         const std::uint32_t source_column = 9,
         const std::uint32_t expression_width = 4,
         const bool expression_signed = false,
         const ExpressionSizingKind sizing =
             ExpressionSizingKind::context_determined,
         const ExpressionValueDomain domain =
             ExpressionValueDomain::four_state,
         const std::uint32_t base_offset = 0) {
        Process process;
        process.id = 101;
        process.name = "cached_expression_selection";
        process.register_count = 3;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("1010101111001101")},
            LoadConstant{
                1,
                PackedLogic4::from_aval_bval(32, 4, 0)},
            DynamicPartSelect{
                2,
                0,
                1,
                source_descending ? 7 : 0,
                source_descending ? 0 : 7,
                selection_width,
                increasing,
                source_descending,
                two_state,
                base_offset},
            Halt{},
        };
        process.expression_profiles = {
            ExpressionProfile{
                SourceLocation{
                    std::string{source_path},
                    source_line,
                    source_column},
                expression_width,
                expression_signed,
                sizing,
                domain}};
        return process;
      };
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_slice_cache_statistics(jit, hits, misses);
      };

  materialize(make(), 0, 1);
  materialize(make(), 1, 0);
  materialize(make(3), 0, 1);
  materialize(make(4, false), 0, 1);
  materialize(make(4, true, false), 0, 1);
  materialize(make(4, true, true, true), 0, 1);
  materialize(
      make(4, true, true, false, "edited-expression-selection.sv"),
      0,
      1);
  materialize(
      make(4, true, true, false, "expression-selection.sv", 18),
      0,
      1);
  materialize(
      make(4, true, true, false, "expression-selection.sv", 17, 10),
      0,
      1);
  materialize(
      make(4, true, true, false, "expression-selection.sv", 17, 9, 5),
      0,
      1);
  materialize(
      make(4, true, true, false, "expression-selection.sv", 17, 9, 4,
           true),
      0,
      1);
  materialize(
      make(4, true, true, false, "expression-selection.sv", 17, 9, 4,
           false, ExpressionSizingKind::self_determined),
      0,
      1);
  materialize(
      make(4, true, true, false, "expression-selection.sv", 17, 9, 4,
           false, ExpressionSizingKind::context_determined,
           ExpressionValueDomain::two_state),
      0,
      1);
  materialize(
      make(4, true, true, false, "expression-selection.sv", 17, 9, 4,
           false, ExpressionSizingKind::context_determined,
           ExpressionValueDomain::four_state, 8),
      0,
      1);
  assert(slice_cached_object_count(cache_directory) == 13);
}

void test_procedural_update_cache_identity(
    const std::filesystem::path& cache_directory) {
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const std::string_view symbol,
          const Process& process,
          const std::span<const std::uint32_t> signals,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, signals);
        assert(jit.lookup(symbol));
        expect_slice_cache_statistics(jit, hits, misses);
      };
  const auto insert_process =
      [](const std::uint32_t base_offset,
         const bool increasing) {
        Process process;
        process.id = 102;
        process.name = "cached_dynamic_part_insert";
        process.register_count = 4;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("1010101111001101")},
            LoadConstant{
                1, PackedLogic4::from_msb_string("1100")},
            LoadConstant{
                2, PackedLogic4::from_aval_bval(32, 4, 0)},
            DynamicPartInsert{
                3,
                0,
                1,
                DynamicPartIndex{
                    2, 7, 0, base_offset, 4, increasing, true}},
            Halt{},
        };
        return process;
      };
  const std::array<std::uint32_t, 0> no_signals{};
  materialize(
      "cached_dynamic_part_insert", insert_process(0, true), no_signals,
      0, 1);
  materialize(
      "cached_dynamic_part_insert", insert_process(0, true), no_signals,
      1, 0);
  materialize(
      "cached_dynamic_part_insert", insert_process(8, true), no_signals,
      0, 1);
  materialize(
      "cached_dynamic_part_insert", insert_process(0, false), no_signals,
      0, 1);

  const auto force_process =
      [](const std::uint32_t force_offset,
         const std::uint32_t release_offset,
         const std::uint32_t release_width) {
        Process process;
        process.id = 103;
        process.name = "cached_force_release";
        process.register_count = 1;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("10xz")},
            ForceSignalSlice{0, 0, force_offset, std::nullopt},
            ReleaseSignalSlice{
                0, release_offset, release_width, std::nullopt},
            Halt{},
        };
        return process;
      };
  const std::array<std::uint32_t, 1> signal_widths{8};
  materialize(
      "cached_force_release", force_process(0, 0, 4), signal_widths,
      0, 1);
  materialize(
      "cached_force_release", force_process(0, 0, 4), signal_widths,
      1, 0);
  materialize(
      "cached_force_release", force_process(4, 0, 4), signal_widths,
      0, 1);
  materialize(
      "cached_force_release", force_process(0, 4, 4), signal_widths,
      0, 1);
  materialize(
      "cached_force_release", force_process(0, 0, 2), signal_widths,
      0, 1);
  assert(slice_cached_object_count(cache_directory) == 7);
}

void test_container_construction_cache_identity(
    const std::filesystem::path& cache_directory) {
  const std::array<std::uint32_t, 0> no_signals{};
  const auto materialize = [&]<typename MakeProcess>(
      const std::filesystem::path& directory,
      const std::string_view symbol,
      MakeProcess&& make_process,
      const std::uint64_t hits,
      const std::uint64_t misses) {
    LlvmJit jit{LlvmJitOptions{
        JitOptimizationLevel::o2, directory}};
    jit.add_process(symbol, make_process(), no_signals);
    assert(jit.lookup(symbol));
    expect_slice_cache_statistics(jit, hits, misses);
  };
  const auto resize = [](
      const std::optional<ContainerRegisterId> initializer,
      std::string nominal_type = {}) {
    ContainerType dynamic;
    dynamic.element_width = 8;
    dynamic.two_state = true;
    dynamic.element_nominal_type = std::move(nominal_type);
    Process process;
    process.id = 104;
    process.name = "cached_dynamic_construction";
    process.register_count = 1;
    process.container_register_count = 2;
    process.container_register_types = {dynamic, dynamic};
    process.operations = {
        LoadConstant{0, PackedLogic4::from_aval_bval(32, 3, 0)},
        ResizeContainer{0, 0, initializer}, Halt{}};
    return process;
  };
  const auto resize_directory = cache_directory / "resize";
  materialize(
      resize_directory, "cached_dynamic_construction",
      [&] { return resize(std::nullopt); }, 0, 1);
  materialize(
      resize_directory, "cached_dynamic_construction",
      [&] { return resize(std::nullopt); }, 1, 0);
  materialize(
      resize_directory, "cached_dynamic_construction",
      [&] { return resize(1); }, 0, 1);
  materialize(
      resize_directory, "cached_dynamic_construction",
      [&] { return resize(std::nullopt, "packet_t"); }, 0, 1);
  assert(slice_cached_object_count(resize_directory) == 3);

  const auto push = [](
      const std::optional<RegisterId> index) {
    ContainerType queue;
    queue.element_width = 8;
    queue.queue = true;
    Process process;
    process.id = 105;
    process.name = "cached_queue_insert";
    process.register_count = 2;
    process.container_register_count = 1;
    process.container_register_types = {queue};
    process.operations = {
        LoadConstant{0, PackedLogic4::from_aval_bval(8, 7, 0)},
        LoadConstant{1, PackedLogic4::from_aval_bval(32, 0, 0)},
        PushContainer{0, 0, false, index}, Halt{}};
    return process;
  };
  const auto push_directory = cache_directory / "push";
  materialize(
      push_directory, "cached_queue_insert",
      [&] { return push(std::nullopt); }, 0, 1);
  materialize(
      push_directory, "cached_queue_insert",
      [&] { return push(std::nullopt); }, 1, 0);
  materialize(
      push_directory, "cached_queue_insert",
      [&] { return push(1); }, 0, 1);
  assert(slice_cached_object_count(push_directory) == 2);
}

}  // namespace fsim::tests::compiler

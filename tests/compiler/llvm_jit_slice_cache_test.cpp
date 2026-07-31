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
          4, 8, SliceConsumer::reduction, 1, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::reduction, 2, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::minimum, std::nullopt, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::find_index, 3, 17),
      0,
      1);
  materialize(
      make_slice_consumer_process(
          4, 8, SliceConsumer::find_index, 2, 17),
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
          ContainerOrderingOperator::ascending, 1),
      0,
      1);
  materialize(
      make(
          4, 8,
          ContainerOrderingOperator::ascending, 2),
      0,
      1);
  materialize(make(4, 8, ContainerOrderingOperator::reverse,
                   std::nullopt, false), 0, 1);
  materialize(make(4, 8, ContainerOrderingOperator::reverse,
                   std::nullopt, true, 24), 0, 1);
  assert(slice_cached_object_count(cache_directory) == 9);
}

}  // namespace fsim::tests::compiler

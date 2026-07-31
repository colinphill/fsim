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

}  // namespace fsim::tests::compiler

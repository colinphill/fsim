// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_extract_test.hpp"

#include "llvm_jit_region_frontier_staging_test.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir_region_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler::test {
namespace {

using runtime::Logic4;
using runtime::PackedLogic4;
using runtime::simir::RegionConeActivationKernel;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

struct ExtractCase {
    std::uint32_t source_width { };
    std::uint32_t offset { };
    std::uint32_t width { };
};

[[nodiscard]] PackedLogic4 patterned_source(const ExtractCase& test_case)
{
    constexpr std::array values {
        Logic4::zero, Logic4::one, Logic4::x, Logic4::z,
    };
    PackedLogic4 source(test_case.source_width, Logic4::zero);
    for (std::uint32_t bit = 0U; bit < test_case.source_width; ++bit) {
        const auto value_index = static_cast<std::size_t>(
            (bit * 5U + bit / 63U) % values.size());
        source.set(bit, values[value_index]);
    }
    // Force a real internal value change so the consumer activation and the
    // boundary publication traverse the same generated event prefix.
    source.set(test_case.offset, Logic4::one);
    return source;
}

void run_extract_case(const ExtractCase& test_case,
    const JitOptimizationLevel optimization)
{
    const auto kernel = make_certified_extract_frontier_kernel(
        test_case.source_width, test_case.offset, test_case.width);
    LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    const auto identity = std::string { "region-frontier-extract-" }
        + std::to_string(test_case.source_width) + "-"
        + std::to_string(test_case.offset) + "-"
        + std::to_string(test_case.width) + "-"
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto executor = LlvmRegionFrontierExecutor::try_create(
        kernel, options, identity);
    require(executor != nullptr,
        "the generated Extract kernel is supported and materializes");

    const auto source = patterned_source(test_case);
    const auto expected = source.extract_bits(test_case.offset,
        test_case.width);
    RegionFrontierStagingValues values;
    values.external_input = {
        std::vector<std::uint64_t>(source.aval_words().begin(),
            source.aval_words().end()),
        std::vector<std::uint64_t>(source.bval_words().begin(),
            source.bval_words().end()),
    };
    values.initial_internal = {
        std::vector<std::uint64_t>(expected.aval_words().size(), 0U),
        std::vector<std::uint64_t>(expected.bval_words().size(), 0U),
    };
    values.expected_internal = {
        std::vector<std::uint64_t>(expected.aval_words().begin(),
            expected.aval_words().end()),
        std::vector<std::uint64_t>(expected.bval_words().begin(),
            expected.bval_words().end()),
    };
    values.initial_boundary = values.initial_internal;
    values.expected_boundary = values.expected_internal;
    run_region_frontier_staging_tests(kernel, executor->step_entry(),
        executor->layout(), 71U, std::move(values));
}

} // namespace

void run_region_frontier_extract_tests()
{
    constexpr std::array cases {
        ExtractCase { 65U, 1U, 64U },
        ExtractCase { 129U, 63U, 65U },
        ExtractCase { 193U, 63U, 129U },
        ExtractCase { 193U, 127U, 66U },
        ExtractCase { 129U, 128U, 1U },
    };
    for (const auto& test_case : cases) {
        for (const auto optimization : {
                 JitOptimizationLevel::o0,
                 JitOptimizationLevel::o2,
             }) {
            run_extract_case(test_case, optimization);
        }
    }
}

} // namespace fsim::compiler::test

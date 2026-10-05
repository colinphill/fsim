// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_concatenate_test.hpp"

#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "fsim/runtime/simir.hpp"
#include "llvm_jit_region_frontier_staging_test.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::compiler::test {
namespace {

using runtime::Logic4;
using runtime::PackedLogic4;
using runtime::simir::Concatenate;
using runtime::simir::RegionConeActivationKernel;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] RegionFrontierTestValue to_test_value(
    const PackedLogic4& value)
{
    return {
        std::vector<std::uint64_t>(value.aval_words().begin(),
            value.aval_words().end()),
        std::vector<std::uint64_t>(value.bval_words().begin(),
            value.bval_words().end()),
    };
}

[[nodiscard]] std::string make_operand_pattern(
    const std::uint32_t width, const std::size_t operand_index)
{
    constexpr std::string_view states { "01XZ" };
    std::string pattern;
    pattern.reserve(width);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        const auto state = (bit + operand_index) % states.size();
        pattern.push_back(states[state]);
    }
    if (width == 1U) {
        pattern[0] = operand_index % 2U == 0U ? 'Z' : 'X';
    } else if (width > 1U) {
        pattern[0] = 'X';
        pattern[1] = 'Z';
    }
    return pattern;
}

[[nodiscard]] std::unique_ptr<LlvmRegionFrontierExecutor> make_executor(
    const RegionConeActivationKernel& kernel,
    const JitOptimizationLevel optimization,
    const std::string_view identity)
{
    LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.code_coverage_identity = "disabled";
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    return LlvmRegionFrontierExecutor::try_create(kernel, options, identity);
}

void check_concatenate_case(
    const std::vector<std::uint32_t>& operand_widths,
    const JitOptimizationLevel optimization)
{
    auto kernel = make_certified_concatenate_frontier_kernel(operand_widths);
    std::vector<std::string> operand_patterns;
    operand_patterns.reserve(operand_widths.size());
    std::string expected_pattern;
    for (std::size_t index = 0U; index < operand_widths.size(); ++index) {
        operand_patterns.push_back(
            make_operand_pattern(operand_widths[index], index));
        expected_pattern.append(operand_patterns.back());
    }

    RegionFrontierStagingValues values;
    for (const auto& input : kernel.inputs) {
        if (input.internal) {
            continue;
        }
        require(input.signal < operand_patterns.size(),
            "each external concatenation input maps to its source operand");
        const auto packed
            = PackedLogic4::from_msb_string(operand_patterns[input.signal]);
        values.external_inputs.push_back({ input.signal,
            to_test_value(packed) });
    }
    require(values.external_inputs.size() == operand_widths.size(),
        "the certified kernel retains every concatenation source input");

    const auto result
        = PackedLogic4::from_msb_string(expected_pattern);
    const auto initial = PackedLogic4(result.width(), Logic4::zero);
    values.initial_internal = to_test_value(initial);
    values.initial_boundary = values.initial_internal;
    values.expected_internal = to_test_value(result);
    values.expected_boundary = values.expected_internal;

    const auto identity = std::string { "frontier-concatenate-" }
        + std::to_string(result.width()) + "-"
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto executor = make_executor(kernel, optimization, identity);
    require(executor != nullptr,
        "production lowering accepts graph-certified Logic4 concatenation");
    require(executor->step_entry() != nullptr,
        "the concatenation kernel has a generated frontier entry");
    run_region_frontier_staging_tests(kernel, executor->step_entry(),
        executor->layout(), 83U, std::move(values));
}

enum class InvalidConcatenationShape : std::uint8_t {
    empty_operands,
    zero_width,
    mismatched_width,
};

void check_invalid_concatenation_declines(
    const InvalidConcatenationShape shape)
{
    auto kernel = make_certified_concatenate_frontier_kernel(
        std::array<std::uint32_t, 2U> { 31U, 34U });
    bool found_concatenate { };
    for (std::size_t index = 0U;
         index < kernel.program.operations.size(); ++index) {
        auto* const concatenate = runtime::simir::operation_get_if<Concatenate>(
            &kernel.program.operations[index]);
        if (concatenate == nullptr) {
            continue;
        }
        found_concatenate = true;
        if (shape == InvalidConcatenationShape::empty_operands) {
            concatenate->operands.clear();
        } else if (shape == InvalidConcatenationShape::zero_width) {
            concatenate->width = 0U;
        } else {
            ++concatenate->width;
        }
        break;
    }
    require(found_concatenate,
        "the malformed-shape witness starts from a graph-built concatenate");

    auto executor = make_executor(kernel, JitOptimizationLevel::o2,
        shape == InvalidConcatenationShape::empty_operands
            ? "frontier-concatenate-empty-decline"
            : shape == InvalidConcatenationShape::zero_width
            ? "frontier-concatenate-zero-width-decline"
            : "frontier-concatenate-width-decline");
    require(executor == nullptr,
        "invalid Concatenate operand shapes decline before JIT materialization");
}

} // namespace

void run_region_frontier_concatenate_tests()
{
    const std::array<std::vector<std::uint32_t>, 5U> operand_widths {{
        { 1U },
        { 31U, 34U },
        { 62U, 63U, 4U },
        { 65U, 64U, 63U, 64U },
        { 129U, 257U, 511U, 127U },
    }};
    for (const auto optimization : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2,
         }) {
        for (const auto& widths : operand_widths) {
            check_concatenate_case(widths, optimization);
        }
    }
    check_invalid_concatenation_declines(
        InvalidConcatenationShape::empty_operands);
    check_invalid_concatenation_declines(
        InvalidConcatenationShape::zero_width);
    check_invalid_concatenation_declines(
        InvalidConcatenationShape::mismatched_width);
}

} // namespace fsim::compiler::test

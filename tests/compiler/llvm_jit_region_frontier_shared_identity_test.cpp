// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_shared_identity_test.hpp"

#include "llvm_jit_region_frontier_test_support.hpp"

#include "fsim/runtime/packed_value.hpp"
#include "llvm/region_frontier_kernel_plan.hpp"

#include <algorithm>
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
using runtime::simir::CopyRegister;
using runtime::simir::EdgeKind;
using runtime::simir::LoadConstant;
using runtime::simir::Operation;
using runtime::simir::ProcessId;
using runtime::simir::RegionConeActivationKernel;
using runtime::simir::SignalId;

constexpr ProcessId producer_process_id { 7U };
constexpr std::uint32_t fixture_width { 8U };

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] RegionConeActivationKernel with_producer_constant(
    RegionConeActivationKernel kernel, const Logic4 value)
{
    const auto producer = std::ranges::find(kernel.members,
        producer_process_id, &runtime::simir::RegionConeKernelMember::process);
    require(producer != kernel.members.end(),
        "the constant identity fixture retains its producer member");

    const auto is_output_instruction = [&kernel](const std::uint32_t instruction) {
        return std::ranges::any_of(kernel.outputs,
            [instruction](const auto& output) {
                return output.kernel_instruction == instruction;
            });
    };

    bool replaced_transform { };
    for (std::uint32_t instruction = producer->begin;
         instruction < producer->end; ++instruction) {
        auto operation = kernel.program.operations.expanded(instruction);
        const auto* copy = runtime::simir::operation_get_if<CopyRegister>(
            &operation);
        if (copy == nullptr || is_output_instruction(instruction)) {
            continue;
        }
        kernel.program.operations.replace(instruction,
            Operation { LoadConstant {
                copy->destination, PackedLogic4(fixture_width, value) } });
        replaced_transform = true;
        break;
    }
    require(replaced_transform,
        "the producer has a non-output transform instruction to replace");
    return kernel;
}

void set_internal_sensitivity_range(
    RegionConeActivationKernel& kernel, const std::uint32_t offset,
    const std::uint32_t width)
{
    require(kernel.internal_signals.size() == 1U,
        "the range identity fixture has one internal signal");
    const SignalId internal_signal = kernel.internal_signals.front();
    const auto reader = std::ranges::find_if(kernel.members,
        [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(reader != kernel.members.end(),
        "the range identity fixture has an internal reader");
    std::erase_if(reader->sensitivities,
        [internal_signal](const auto& sensitivity) {
            return sensitivity.signal == internal_signal;
        });
    reader->sensitivities.push_back(
        { internal_signal, EdgeKind::any, offset, width });
}

[[nodiscard]] RegionFrontierKernelPlan make_plan(
    const RegionConeActivationKernel& kernel,
    const std::string_view description)
{
    auto plan = RegionFrontierKernelPlan::try_create(kernel);
    require(plan.has_value(), description);
    return std::move(*plan);
}

[[nodiscard]] std::string shared_identity(
    const RegionFrontierKernelPlan& plan, const std::string_view description)
{
    const auto identity = plan.shared_body_identity();
    require(identity.has_value() && !identity->empty(), description);
    return std::string { *identity };
}

} // namespace

void run_region_frontier_shared_identity_tests()
{
    const auto kernel = make_certified_frontier_kernel(fixture_width);
    auto baseline = make_plan(kernel,
        "the baseline two-member frontier kernel is certified");
    const auto baseline_shared = shared_identity(baseline,
        "the plan exposes a certified shared-body identity");
    const auto baseline_census = baseline.structural_census_identity();
    require(baseline_census.has_value() && !baseline_census->empty()
            && baseline_shared != *baseline_census,
        "the code-sharing identity is a distinct proof from the structural census");

    constexpr SignalId signal_delta { 1000U };
    constexpr ProcessId process_delta { 2000U };
    const auto renamed_kernel = remap_frontier_physical_ids(
        kernel, signal_delta, process_delta);
    auto renamed = make_plan(renamed_kernel,
        "an order-preserving physical remap remains certified");
    const auto renamed_shared = shared_identity(renamed,
        "the remapped plan exposes a certified shared-body identity");
    const auto renamed_census = renamed.structural_census_identity();
    require(renamed.cache_identity() != baseline.cache_identity()
            && renamed_shared == baseline_shared
            && renamed_census.has_value()
            && *renamed_census == *baseline_census,
        "physical identities change exact cache keys while preserving "
        "certified body identity");

    auto zero_kernel = with_producer_constant(kernel, Logic4::zero);
    auto one_kernel = with_producer_constant(kernel, Logic4::one);
    auto zero_plan = make_plan(zero_kernel,
        "the constant-zero member body remains certified");
    auto one_plan = make_plan(one_kernel,
        "the constant-one member body remains certified");
    const auto zero_shared = shared_identity(zero_plan,
        "the constant-zero plan exposes a shared-body identity");
    const auto one_shared = shared_identity(one_plan,
        "the constant-one plan exposes a shared-body identity");
    require(zero_shared != one_shared,
        "changing a lowered packed constant changes the certified "
        "code-sharing identity");

    const auto wider_kernel = make_certified_frontier_kernel(
        fixture_width * 2U);
    auto wider_plan = make_plan(wider_kernel,
        "the wider layout remains certified");
    require(shared_identity(wider_plan,
                "the wider plan exposes a shared-body identity")
            != baseline_shared,
        "changing packed shape changes the certified code-sharing identity");

    auto ranged_kernel = kernel;
    set_internal_sensitivity_range(ranged_kernel, 1U, 3U);
    auto ranged_plan = RegionFrontierKernelPlan::try_create(ranged_kernel);
    require(!ranged_plan.has_value()
            || !ranged_plan->shared_body_identity().has_value()
            || *ranged_plan->shared_body_identity() != baseline_shared,
        "an internal fanout range either changes or refuses the shared-body proof");
}

} // namespace fsim::compiler::test

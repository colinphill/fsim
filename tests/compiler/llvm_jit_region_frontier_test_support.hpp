// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_graph.hpp"
#include "fsim/runtime/simir_region_frontier_v2.hpp"

#include <cstdint>
#include <span>

namespace fsim::compiler::test {

[[nodiscard]] constexpr std::uint64_t encode_region_frontier_payload_v2(
    const runtime::simir::RegionFrontierEventKindV2 kind,
    const std::uint64_t index) noexcept
{
    return (static_cast<std::uint64_t>(kind)
            << runtime::simir::kRegionFrontierPayloadKindShiftV2)
        | index;
}


[[nodiscard]] runtime::simir::RegionConeActivationKernel
make_certified_frontier_kernel(
    std::uint32_t width,
    runtime::simir::SignalUpdateDomain write_domain
        = runtime::simir::SignalUpdateDomain::systemverilog_active,
    runtime::simir::ValueKind value_kind
        = runtime::simir::ValueKind::logic4);

/// Repeated packed bodies with distinct instance coordinates and negative
/// constant/input-alias cases, plus a read-only member without outputs.
[[nodiscard]] runtime::simir::RegionConeActivationKernel
make_repeated_member_frontier_kernel();

/// Four-member copy chain whose middle bodies share a shape across widths and
/// value kinds while retaining separate source and physical bindings.
[[nodiscard]] runtime::simir::RegionConeActivationKernel
make_repeated_copy_member_frontier_kernel(
    std::uint32_t width,
    runtime::simir::ValueKind value_kind,
    runtime::simir::SignalUpdateDomain write_domain);

/// Copy an activation kernel while changing only its physical process and
/// signal identities. Local member, input, and output order stays fixed.
/// Sentinel owners remain sentinels; unrepresentable remaps are rejected.
[[nodiscard]] runtime::simir::RegionConeActivationKernel
remap_frontier_physical_ids(
    runtime::simir::RegionConeActivationKernel kernel,
    runtime::simir::SignalId signal_delta,
    runtime::simir::ProcessId process_delta);

[[nodiscard]] runtime::simir::RegionConeActivationKernel
make_certified_reduction_frontier_kernel(
    std::uint32_t input_width,
    runtime::simir::ReductionOperator operation);

[[nodiscard]] runtime::simir::RegionConeActivationKernel
make_certified_unary_not_frontier_kernel(
    std::uint32_t width,
    runtime::simir::ValueKind value_kind
        = runtime::simir::ValueKind::logic4,
    runtime::simir::SignalUpdateDomain write_domain
        = runtime::simir::SignalUpdateDomain::systemverilog_active);

[[nodiscard]] runtime::simir::RegionConeActivationKernel
make_certified_binary_frontier_kernel(
    std::uint32_t width,
    runtime::simir::BinaryOperator operation,
    runtime::simir::ValueKind value_kind
        = runtime::simir::ValueKind::logic4,
    runtime::simir::SignalUpdateDomain write_domain
        = runtime::simir::SignalUpdateDomain::systemverilog_active);

[[nodiscard]] runtime::simir::RegionConeActivationKernel
make_certified_conditional_select_frontier_kernel(std::uint32_t width);

[[nodiscard]] runtime::simir::RegionConeActivationKernel
make_certified_extract_frontier_kernel(
    std::uint32_t source_width,
    std::uint32_t offset,
    std::uint32_t width);

[[nodiscard]] runtime::simir::RegionConeActivationKernel
make_certified_concatenate_frontier_kernel(
    std::span<const std::uint32_t> operand_widths);

} // namespace fsim::compiler::test

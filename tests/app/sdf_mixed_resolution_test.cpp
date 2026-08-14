// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mixed_resolution.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

std::shared_ptr<const fsim::app::SdfMixedVerilogApplication> make_verilog(
    const bool invalid_direction = false)
{
    using namespace fsim;
    auto timing = std::make_shared<const app::SdfInterconnectTimingApplication>(
        nullptr, std::vector<app::SdfAppliedInterconnectTiming> { },
        "resolution-timing");
    app::SdfMixedVerilogBoundaryTiming boundary;
    boundary.direction = app::SdfMixedVerilogDirection::VhdlToVerilog;
    boundary.boundary_path = "top.u.result";
    boundary.target_identity = "vhdl-to-sv";
    boundary.source_language = app::SdfScopeRootLanguage::Vhdl;
    boundary.destination_language = invalid_direction
        ? app::SdfScopeRootLanguage::Vhdl
        : app::SdfScopeRootLanguage::SystemVerilog;
    boundary.source_path = "top.u.result";
    boundary.destination_path = "top.bus.data";
    boundary.transition_delays = { 11U, 13U };
    boundary.canonical_identity = invalid_direction
        ? "invalid-direction"
        : "verilog-boundary";
    return std::make_shared<const app::SdfMixedVerilogApplication>(timing,
        std::vector<app::SdfMixedVerilogBoundaryTiming> { std::move(boundary) },
        "resolution-verilog");
}

std::shared_ptr<const fsim::app::SdfMixedSystemVerilogApplication>
make_systemverilog(
    const std::shared_ptr<const fsim::app::SdfMixedVerilogApplication>& mixed)
{
    fsim::app::SdfMixedSystemVerilogEndpoint endpoint;
    endpoint.boundary_path = "top.u.result";
    endpoint.owner_kind
        = fsim::app::SdfMixedSystemVerilogOwnerKind::Interface;
    endpoint.owner_identity = "work.bus_if";
    endpoint.endpoint_identity = "bus_if.data";
    endpoint.canonical_identity = "systemverilog-owned-endpoint";
    return std::make_shared<
        const fsim::app::SdfMixedSystemVerilogApplication>(mixed,
        std::vector<fsim::app::SdfMixedSystemVerilogEndpoint> {
            std::move(endpoint) },
        "resolution-systemverilog");
}

std::shared_ptr<const fsim::app::SdfMixedSystemCApplication> make_systemc()
{
    using namespace fsim;
    auto timing = std::make_shared<const app::SdfInterconnectTimingApplication>(
        nullptr, std::vector<app::SdfAppliedInterconnectTiming> { },
        "resolution-systemc-timing");
    app::SdfMixedSystemCProxyTiming proxy;
    proxy.target_identity = "vhdl-to-systemc";
    proxy.target_instance_path = "native_top";
    proxy.object_path = "native_top.u_proxy.data";
    proxy.parent_path = "native_top.u_proxy";
    proxy.source_language = app::SdfScopeRootLanguage::Vhdl;
    proxy.destination_language = app::SdfScopeRootLanguage::SystemC;
    proxy.transition_delays = { 17U };
    proxy.canonical_identity = "systemc-proxy";
    return std::make_shared<const app::SdfMixedSystemCApplication>(timing,
        std::vector<app::SdfMixedSystemCProxyTiming> { std::move(proxy) },
        "resolution-systemc");
}

std::vector<fsim::app::SdfMixedResolutionSource> sources()
{
    auto verilog = make_verilog();
    fsim::app::SdfMixedResolutionSource first;
    first.root_identity = "root-a";
    first.library_identity = "work";
    first.verilog = verilog;
    first.systemverilog = make_systemverilog(verilog);
    fsim::app::SdfMixedResolutionSource second;
    second.root_identity = "root-b";
    second.library_identity = "native";
    second.systemc = make_systemc();
    return { std::move(first), std::move(second) };
}

void test_multiple_roots_libraries_and_nested_identity()
{
    using namespace fsim;
    auto requested = sources();
    const auto result = app::resolve_sdf_mixed_boundaries(requested);
    require(result.ok() && result.application->boundaries().size() == 3U,
        "multiple-root Verilog, nested SystemVerilog, and SystemC records must publish");
    const auto& boundaries = result.application->boundaries();
    require(std::ranges::is_sorted(boundaries, { },
                &app::SdfMixedResolvedBoundary::canonical_identity),
        "mixed boundaries must publish in canonical identity order");
    require(std::ranges::any_of(boundaries, [](const auto& boundary) {
        return boundary.root_identity == "root-a"
            && boundary.library_identity == "work"
            && boundary.kind
            == app::SdfMixedResolutionKind::SystemVerilogOwnedEndpoint
            && boundary.owner_identity == "work.bus_if";
    }) && std::ranges::any_of(boundaries, [](const auto& boundary) {
        return boundary.root_identity == "root-b"
            && boundary.library_identity == "native"
            && boundary.source_language
            == app::SdfScopeRootLanguage::Vhdl
            && boundary.destination_language
            == app::SdfScopeRootLanguage::SystemC
            && boundary.effective_ticks
            == std::vector<runtime::SimulationTick> { 17U };
    }),
        "root, library, nested owner, and direction identities must remain explicit");
    std::ranges::reverse(requested);
    const auto reordered = app::resolve_sdf_mixed_boundaries(requested);
    require(reordered.ok()
            && reordered.application->semantic_identity()
                == result.application->semantic_identity(),
        "source input order must not change canonical mixed identity");
}

void test_ambiguity_direction_and_resource_rejection()
{
    using namespace fsim;
    auto requested = sources();
    requested.push_back(requested.front());
    auto result = app::resolve_sdf_mixed_boundaries(requested);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-RESOLUTION-002",
        "duplicate root/library/direction paths must reject atomically");

    requested = sources();
    requested.front().systemverilog.reset();
    requested.front().verilog = make_verilog(true);
    result = app::resolve_sdf_mixed_boundaries(requested);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-RESOLUTION-003",
        "same-language or incomplete direction must reject");

    requested = sources();
    app::SdfMixedResolutionLimits limits;
    limits.max_boundaries = 2U;
    result = app::resolve_sdf_mixed_boundaries(requested, limits);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-RESOLUTION-004",
        "flattened-boundary resource overflow must roll back atomically");
}
} // namespace

int main()
{
    try {
        test_multiple_roots_libraries_and_nested_identity();
        test_ambiguity_direction_and_resource_rejection();
    } catch (const std::exception& error) {
        std::cerr << "sdf mixed resolution test failure: " << error.what()
                  << '\n';
        return 1;
    }
    std::cout << "sdf mixed resolution tests passed\n";
    return 0;
}

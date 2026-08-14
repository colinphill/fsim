// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mixed_systemverilog.hpp"

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

fsim::frontend::SourceSpan source_span()
{
    fsim::frontend::SourceSpan result;
    result.source_name = "mixed_systemverilog.sdf";
    result.begin = { 5U, 3U, 2U };
    result.end = { 12U, 3U, 9U };
    return result;
}

std::shared_ptr<const fsim::app::SdfMixedVerilogApplication> make_mixed()
{
    using namespace fsim;
    auto timing = std::make_shared<const app::SdfInterconnectTimingApplication>(
        nullptr, std::vector<app::SdfAppliedInterconnectTiming> { },
        "mixed-systemverilog-timing");
    app::SdfMixedVerilogBoundaryTiming boundary;
    boundary.direction = app::SdfMixedVerilogDirection::VhdlToVerilog;
    boundary.conversion = elaboration::BoundaryConversionKind::width_adapter;
    boundary.boundary_path = "top.component.result";
    boundary.target_instance_path = "top";
    boundary.target_identity = "vhdl-to-systemverilog";
    boundary.source_path = "top.component.result";
    boundary.destination_path = "top.bus_if.data";
    boundary.source_language = app::SdfScopeRootLanguage::Vhdl;
    boundary.destination_language = app::SdfScopeRootLanguage::SystemVerilog;
    boundary.source_signal = 2U;
    boundary.destination_signal = 3U;
    boundary.formal_signal = 2U;
    boundary.actual_signal = 3U;
    boundary.formal_width = 2U;
    boundary.actual_width = 4U;
    boundary.formal_domain = frontend::ValueDomain::Logic9;
    boundary.actual_domain = frontend::ValueDomain::Logic4;
    boundary.canonical_identity = "mixed-boundary-identity";
    return std::make_shared<const app::SdfMixedVerilogApplication>(timing,
        std::vector<app::SdfMixedVerilogBoundaryTiming> { std::move(boundary) },
        "mixed-systemverilog-base");
}

fsim::semantic::sv::Hir make_hir()
{
    using namespace fsim::semantic;
    sv::Hir hir;
    const auto add_unit = [&](const sv::UnitKind kind, std::string name) {
        sv::Unit unit;
        unit.kind = kind;
        unit.library = "work";
        unit.name = std::move(name);
        hir.mutable_units().push_back(std::move(unit));
    };
    add_unit(sv::UnitKind::interface, "bus_if");
    add_unit(sv::UnitKind::program, "test_program");
    add_unit(sv::UnitKind::package, "timings_pkg");
    add_unit(sv::UnitKind::module, "assertion_owner");
    sv::ConcurrentAssertion assertion;
    assertion.name = "latency";
    assertion.explicit_label = true;
    hir.mutable_units().back().concurrent_assertions.push_back(
        std::move(assertion));
    sv::ClassDeclaration declaration;
    declaration.name = "timing_class";
    declaration.canonical_identity = "work::timing_class";
    declaration.enclosing_identity = "work.timings_pkg";
    hir.mutable_classes().push_back(std::move(declaration));
    return hir;
}

fsim::app::SdfMixedSystemVerilogBinding binding(
    const fsim::app::SdfMixedSystemVerilogOwnerKind kind,
    std::string owner, std::string endpoint,
    const fsim::app::SdfMixedSystemVerilogRegion region)
{
    fsim::app::SdfMixedSystemVerilogBinding result;
    result.boundary_path = "top.component.result";
    result.owner_kind = kind;
    result.owner_identity = std::move(owner);
    result.endpoint_identity = std::move(endpoint);
    result.signal = 3U;
    result.width = 4U;
    result.domain = fsim::frontend::ValueDomain::Logic4;
    result.event_region = region;
    result.source = source_span();
    return result;
}

std::vector<fsim::app::SdfMixedSystemVerilogBinding> bindings()
{
    using Kind = fsim::app::SdfMixedSystemVerilogOwnerKind;
    using Region = fsim::app::SdfMixedSystemVerilogRegion;
    return {
        binding(Kind::Interface, "work.bus_if", "bus_if.data", Region::Active),
        binding(Kind::Program, "work.test_program", "test_program.sample",
            Region::Reactive),
        binding(Kind::Class, "work::timing_class", "timing_class.value",
            Region::Active),
        binding(Kind::Assertion, "work.assertion_owner.latency",
            "assertion_owner.latency", Region::Observed),
        binding(Kind::Package, "work.timings_pkg", "timings_pkg.limit",
            Region::Active),
    };
}

void test_owned_endpoints_and_regions()
{
    using namespace fsim;
    const auto mixed = make_mixed();
    const auto hir = make_hir();
    const auto requested = bindings();
    const auto result
        = app::apply_sdf_mixed_systemverilog(mixed, hir, requested);
    require(result.ok() && result.application->endpoints().size() == 5U,
        "all SystemVerilog endpoint owner kinds must publish atomically");
    const auto& endpoints = result.application->endpoints();
    require(endpoints[0].owner_kind
                == app::SdfMixedSystemVerilogOwnerKind::Interface
            && endpoints[0].width == 4U
            && endpoints[0].domain == frontend::ValueDomain::Logic4
            && endpoints[1].event_region
                == app::SdfMixedSystemVerilogRegion::Reactive
            && endpoints[2].owner_identity == "work::timing_class"
            && endpoints[4].owner_kind
                == app::SdfMixedSystemVerilogOwnerKind::Package,
        "interface, program, class, and package profiles must remain exact");
    require(endpoints[3].sampling_region
                == app::SdfMixedSystemVerilogRegion::Preponed
            && endpoints[3].evaluation_region
                == app::SdfMixedSystemVerilogRegion::Observed
            && endpoints[3].action_region
                == app::SdfMixedSystemVerilogRegion::Reactive,
        "assertion sampling, evaluation, and action regions must not collapse");
    const auto repeated
        = app::apply_sdf_mixed_systemverilog(mixed, hir, requested);
    require(repeated.ok()
            && repeated.application->semantic_identity()
                == result.application->semantic_identity(),
        "SystemVerilog endpoint publication identity must be stable");
}

void test_owner_shape_region_and_resource_rejection()
{
    using namespace fsim;
    const auto mixed = make_mixed();
    const auto hir = make_hir();
    auto requested = bindings();
    requested.front().owner_identity = "work.missing_if";
    auto result = app::apply_sdf_mixed_systemverilog(mixed, hir, requested);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-SYSTEMVERILOG-003",
        "missing HIR ownership must reject the complete publication");

    requested = bindings();
    requested.front().width = 5U;
    result = app::apply_sdf_mixed_systemverilog(mixed, hir, requested);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-SYSTEMVERILOG-003",
        "implicit SystemVerilog widening must be rejected");

    requested = bindings();
    requested[1].event_region = app::SdfMixedSystemVerilogRegion::Active;
    result = app::apply_sdf_mixed_systemverilog(mixed, hir, requested);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-SYSTEMVERILOG-003",
        "program endpoints must not move out of the reactive region");

    app::SdfMixedSystemVerilogLimits limits;
    limits.max_bindings = 4U;
    requested = bindings();
    result
        = app::apply_sdf_mixed_systemverilog(mixed, hir, requested, limits);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-SYSTEMVERILOG-004",
        "binding overflow must roll back atomically");
}
} // namespace

int main()
{
    try {
        test_owned_endpoints_and_regions();
        test_owner_shape_region_and_resource_rejection();
    } catch (const std::exception& error) {
        std::cerr << "sdf mixed SystemVerilog test failure: " << error.what()
                  << '\n';
        return 1;
    }
    std::cout << "sdf mixed SystemVerilog tests passed\n";
    return 0;
}

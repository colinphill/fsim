// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mixed_verilog.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

fsim::frontend::SourceSpan span(std::string source)
{
    fsim::frontend::SourceSpan result;
    result.source_name = std::move(source);
    result.begin = { 4U, 2U, 1U };
    result.end = { 8U, 2U, 5U };
    return result;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    const auto add_signal = [&](std::string name, const std::size_t width,
                                const frontend::ValueDomain domain,
                                const bool port,
                                const frontend::PortDirection direction) {
        const auto id
            = static_cast<runtime::simir::SignalId>(state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = width;
        info.type_name = domain == frontend::ValueDomain::Logic9
            ? "std_logic_vector"
            : "logic";
        info.source_domain = domain;
        info.resolution = domain == frontend::ValueDomain::Logic9
            ? runtime::simir::ResolutionKind::std_logic
            : runtime::simir::ResolutionKind::sv_wire;
        info.is_port = port;
        info.direction = direction;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(name, runtime::PackedLogic4(width));
        state.signal_names.emplace_back(std::move(name), id);
    };
    add_signal("top.sv_source", 4U, frontend::ValueDomain::Logic4, false,
        frontend::PortDirection::Unknown);
    add_signal("top.component.data", 2U, frontend::ValueDomain::Logic9, true,
        frontend::PortDirection::Input);
    add_signal("top.component.result", 2U, frontend::ValueDomain::Logic9, true,
        frontend::PortDirection::Output);
    add_signal("top.sv_result", 4U, frontend::ValueDomain::Logic4, false,
        frontend::PortDirection::Unknown);

    const auto add_process = [&](std::string name,
                                 const std::optional<runtime::simir::SignalId> load,
                                 const std::optional<runtime::simir::SignalId> drive) {
        runtime::simir::Process process;
        process.id
            = static_cast<runtime::simir::ProcessId>(state.processes.size());
        process.name = std::move(name);
        if (load)
            process.static_sensitivity.push_back({ *load, runtime::simir::EdgeKind::any });
        if (drive)
            process.driver_regions.push_back({ *drive, 0U, 1U, true });
        state.processes.push_back(std::move(process));
    };
    add_process("top.sv_driver", std::nullopt, 0U);
    add_process("top.component.data$boundary_integral", 0U, 1U);
    add_process("top.component.vhdl_load", 1U, std::nullopt);
    add_process("top.component.vhdl_driver", std::nullopt, 2U);
    add_process("top.component.result$boundary_integral", 2U, 3U);
    add_process("top.sv_load", 3U, std::nullopt);

    state.boundary_conversions.push_back(
        elaboration::BoundaryConversionInfo {
            elaboration::BoundaryConversionKind::width_adapter,
            "top.component.data", 1U, 0U, 1U,
            frontend::PortDirection::Input, 2U, 4U,
            frontend::ValueDomain::Logic9, frontend::ValueDomain::Logic4,
            false, false, true, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, span("top.sv"), span("cell.vhd"), span("top.sv") });
    state.boundary_conversions.push_back(
        elaboration::BoundaryConversionInfo {
            elaboration::BoundaryConversionKind::width_adapter,
            "top.component.result", 2U, 3U, 4U,
            frontend::PortDirection::Output, 2U, 4U,
            frontend::ValueDomain::Logic9, frontend::ValueDomain::Logic4,
            false, false, true, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, span("top.sv"), span("cell.vhd"), span("top.sv") });
    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "mixed-language design fixture must be valid");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfAnnotationSummary> make_summary(
    const std::size_t count)
{
    using namespace fsim;
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "mixed.sdf", std::nullopt, std::vector<frontend::SdfIrCell> { },
        std::vector<frontend::SdfIrNode> { }, "mixed-ir");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "mixed.sdf", "mixed-source", std::nullopt, '.', "project", "design",
        std::vector<app::SdfAnnotationRoot> { }, "mixed-scope");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "mixed-cells");
    auto endpoints = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { }, "mixed-endpoints");
    return std::make_shared<const app::SdfAnnotationSummary>(endpoints,
        std::vector<app::SdfAnnotationTargetSummary> { }, count, 0U, count, 0U,
        0U, "mixed-summary");
}

fsim::app::SdfResolvedEndpoint endpoint(
    const fsim::app::SdfEndpointRole role,
    const fsim::app::SdfEndpointObjectKind object_kind,
    const fsim::runtime::simir::SignalId signal, std::string object_path,
    const std::size_t width, const fsim::app::SdfScopeRootLanguage language,
    const fsim::frontend::PortDirection direction
    = fsim::frontend::PortDirection::Unknown)
{
    fsim::app::SdfResolvedEndpoint result;
    result.role = role;
    result.object_kind = object_kind;
    result.instance_path = object_path.substr(0U, object_path.rfind('.'));
    result.object_path = std::move(object_path);
    result.signal = signal;
    result.object_width = width;
    result.language = language;
    result.direction = direction;
    return result;
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> make_plan(
    const bool stale_peer = false)
{
    using namespace fsim;
    using app::SdfEndpointObjectKind;
    using app::SdfEndpointRole;
    std::vector<app::SdfPlannedAnnotation> annotations;
    const auto add = [&](const std::uint64_t id, std::string target,
                         std::vector<app::SdfResolvedEndpoint> endpoints) {
        app::SdfPlannedAnnotation annotation;
        annotation.node_id = id;
        annotation.cell_id = 1U;
        annotation.construct_kind = frontend::SdfConstructKind::Interconnect;
        annotation.target_kind = app::SdfTimingTargetKind::Interconnect;
        annotation.delay_mode = app::SdfDelayApplicationMode::Absolute;
        annotation.target_instance_path = "top";
        annotation.target_identity = std::move(target);
        annotation.endpoints = std::move(endpoints);
        for (const auto& resolved : annotation.endpoints)
            annotation.endpoint_signals.push_back(resolved.signal);
        annotation.after_ticks = { id + 2U, id + 4U };
        annotation.source = span("mixed.sdf");
        annotation.source_identity = "mixed-source";
        annotation.canonical_identity = "mixed-annotation-" + annotation.target_identity;
        annotations.push_back(std::move(annotation));
    };
    auto sv_source = endpoint(SdfEndpointRole::InterconnectSource,
        SdfEndpointObjectKind::HdlNet, 0U, "top.sv_source", 4U,
        app::SdfScopeRootLanguage::SystemVerilog);
    auto vhdl_data = endpoint(SdfEndpointRole::InterconnectDestination,
        SdfEndpointObjectKind::HdlPort, 1U, "top.component.data", 2U,
        app::SdfScopeRootLanguage::Vhdl, frontend::PortDirection::Input);
    vhdl_data.conversion = elaboration::BoundaryConversionKind::width_adapter;
    vhdl_data.conversion_peer = stale_peer ? 3U : 0U;
    add(1U, "sv-to-vhdl", { std::move(sv_source), std::move(vhdl_data) });

    auto vhdl_result = endpoint(SdfEndpointRole::InterconnectSource,
        SdfEndpointObjectKind::HdlPort, 2U, "top.component.result", 2U,
        app::SdfScopeRootLanguage::Vhdl, frontend::PortDirection::Output);
    vhdl_result.conversion
        = elaboration::BoundaryConversionKind::width_adapter;
    vhdl_result.conversion_peer = 3U;
    auto sv_result = endpoint(SdfEndpointRole::InterconnectDestination,
        SdfEndpointObjectKind::HdlNet, 3U, "top.sv_result", 4U,
        app::SdfScopeRootLanguage::SystemVerilog);
    add(2U, "vhdl-to-sv", { std::move(vhdl_result), std::move(sv_result) });
    return std::make_shared<const app::SdfAnnotationPlan>(
        make_summary(annotations.size()), app::SdfValuePolicy { },
        std::move(annotations), "mixed-plan");
}

void test_direction_conversion_and_ownership()
{
    using namespace fsim;
    const auto design = make_design();
    const auto state_before = design.state();
    const auto interconnect
        = app::apply_sdf_interconnect_timing(make_plan(), design);
    require(interconnect.ok() && interconnect.application->timings().size() == 2U,
        "mixed VHDL/Verilog endpoints must retain interconnect timing");
    const auto result
        = app::apply_sdf_mixed_verilog(interconnect.application, design);
    require(result.ok() && result.application->boundaries().size() == 2U,
        "both mixed-language directions must publish atomically");
    const auto* input = result.application->find_boundary("top.component.data");
    require(input != nullptr
            && input->direction
                == app::SdfMixedVerilogDirection::VerilogToVhdl
            && input->source_signal == 0U && input->destination_signal == 1U
            && input->source_driver_processes
                == std::vector<runtime::simir::ProcessId> { 0U }
            && input->destination_load_processes
                == std::vector<runtime::simir::ProcessId> { 2U }
            && input->conversion_process == 1U && input->formal_width == 2U
            && input->actual_width == 4U && input->state_domain_changed,
        "Verilog-to-VHDL timing must retain conversion and exact driver/load owners");
    const auto* output
        = result.application->find_boundary("top.component.result");
    require(output != nullptr
            && output->direction
                == app::SdfMixedVerilogDirection::VhdlToVerilog
            && output->source_driver_processes
                == std::vector<runtime::simir::ProcessId> { 3U }
            && output->destination_load_processes
                == std::vector<runtime::simir::ProcessId> { 5U }
            && output->conversion_process == 4U
            && output->source_path == "top.component.result"
            && output->destination_path == "top.sv_result"
            && output->source_resolution
                == runtime::simir::ResolutionKind::std_logic
            && output->destination_resolution
                == runtime::simir::ResolutionKind::sv_wire
            && output->transition_delays
                == std::vector<runtime::SimulationTick> { 4U, 6U },
        "VHDL-to-Verilog timing must retain direction, delay, and owner identity");
    const auto repeated
        = app::apply_sdf_mixed_verilog(interconnect.application, design);
    require(repeated.ok()
            && repeated.application->semantic_identity()
                == result.application->semantic_identity(),
        "mixed-language canonical identity must be deterministic");
    const auto state_after = design.state();
    require(state_before.processes.size() == state_after.processes.size()
            && state_before.processes[1].driver_regions
                == state_after.processes[1].driver_regions
            && state_before.boundary_conversions.size()
                == state_after.boundary_conversions.size()
            && state_before.boundary_conversions[0].path
                == state_after.boundary_conversions[0].path
            && state_before.boundary_conversions[1].process
                == state_after.boundary_conversions[1].process,
        "mixed-language timing publication must not mutate elaboration state");
}

void test_stale_topology_and_resource_rejection()
{
    using namespace fsim;
    const auto design = make_design();
    const auto stale_interconnect
        = app::apply_sdf_interconnect_timing(make_plan(true), design);
    require(stale_interconnect.ok(),
        "base timing layer must preserve conversion metadata for mixed validation");
    const auto stale
        = app::apply_sdf_mixed_verilog(stale_interconnect.application, design);
    require(!stale.ok() && stale.application == nullptr
            && !stale.diagnostics.empty()
            && stale.diagnostics.front().code == "FSIM-SDF-MIXED-VERILOG-002",
        "stale conversion peers must reject the whole mixed publication");

    const auto interconnect
        = app::apply_sdf_interconnect_timing(make_plan(), design);
    app::SdfMixedVerilogLimits limits;
    limits.max_boundaries = 1U;
    const auto bounded
        = app::apply_sdf_mixed_verilog(interconnect.application, design, limits);
    require(!bounded.ok() && bounded.application == nullptr
            && !bounded.diagnostics.empty()
            && bounded.diagnostics.front().code == "FSIM-SDF-MIXED-VERILOG-004",
        "boundary resource overflow must roll back atomically");

    auto invalid_state = design.state();
    invalid_state.boundary_conversions.front().direction
        = frontend::PortDirection::Inout;
    const auto invalid_design
        = elaboration::ElaboratedDesign::from_state(std::move(invalid_state));
    require(invalid_design.has_value(), "invalid-direction fixture must load");
    const auto invalid = app::apply_sdf_mixed_verilog(
        interconnect.application, *invalid_design);
    require(!invalid.ok() && invalid.application == nullptr
            && !invalid.diagnostics.empty()
            && invalid.diagnostics.front().code
                == "FSIM-SDF-MIXED-VERILOG-003",
        "unsupported bidirectional conversion must reject atomically");
}
} // namespace

int main()
{
    try {
        test_direction_conversion_and_ownership();
        test_stale_topology_and_resource_rejection();
    } catch (const std::exception& error) {
        std::cerr << "sdf mixed Verilog test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "sdf mixed Verilog tests passed\n";
    return 0;
}

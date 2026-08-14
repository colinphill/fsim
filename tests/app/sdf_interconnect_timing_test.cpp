// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_interconnect_timing.hpp"

#include <iostream>
#include <memory>
#include <ranges>
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

fsim::frontend::SourceSpan source_span()
{
    fsim::frontend::SourceSpan result;
    result.source_name = "interconnect.sdf";
    result.begin = { 8U, 3U, 2U };
    result.end = { 18U, 3U, 12U };
    return result;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    const auto add_signal = [&](std::string name, const bool port,
                                const frontend::PortDirection direction) {
        const auto id
            = static_cast<runtime::simir::SignalId>(state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "logic";
        info.source_domain = frontend::ValueDomain::Logic4;
        info.is_port = port;
        info.direction = direction;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(name, runtime::PackedLogic4(1U));
        state.signal_names.emplace_back(std::move(name), id);
    };
    add_signal("top.u.src", false, frontend::PortDirection::Unknown);
    add_signal("top.u.dst", false, frontend::PortDirection::Unknown);
    add_signal("top.u.Z", true, frontend::PortDirection::Output);

    runtime::simir::Process continuous;
    continuous.id = 0U;
    continuous.name = "top.u.continuous";
    continuous.driver_regions.push_back({ 1U, 0U, 1U, true });
    state.processes.push_back(std::move(continuous));
    runtime::simir::Process gate;
    gate.id = 1U;
    gate.name = "top.u.gate";
    gate.driver_regions.push_back({ 1U, 0U, 1U, true });
    gate.driver_regions.push_back({ 2U, 0U, 1U, true });
    state.processes.push_back(std::move(gate));
    runtime::simir::Process switch_process;
    switch_process.id = 2U;
    switch_process.name = "top.u.tranif";
    switch_process.switch_source = 0U;
    switch_process.switch_target = 1U;
    switch_process.switch_bidirectional = true;
    state.processes.push_back(std::move(switch_process));

    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "interconnect design fixture must be valid");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfAnnotationSummary> make_summary(
    const std::size_t annotation_count)
{
    using namespace fsim;
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "interconnect.sdf", std::nullopt, std::vector<frontend::SdfIrCell> { },
        std::vector<frontend::SdfIrNode> { }, "interconnect-ir-identity");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "interconnect.sdf", "interconnect-source-identity", std::nullopt, '.',
        "project", "design", std::vector<app::SdfAnnotationRoot> { },
        "interconnect-scope-identity");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "interconnect-cells-identity");
    auto endpoints = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { },
        "interconnect-endpoint-identity");
    return std::make_shared<const app::SdfAnnotationSummary>(endpoints,
        std::vector<app::SdfAnnotationTargetSummary> { }, annotation_count, 0U,
        annotation_count, 0U, 0U, "interconnect-summary-identity");
}

fsim::app::SdfResolvedEndpoint endpoint(const fsim::app::SdfEndpointRole role,
    const fsim::app::SdfEndpointObjectKind kind,
    const fsim::runtime::simir::SignalId signal, std::string path,
    const fsim::frontend::PortDirection direction
    = fsim::frontend::PortDirection::Unknown)
{
    fsim::app::SdfResolvedEndpoint result;
    result.role = role;
    result.object_kind = kind;
    result.instance_path = "top.u";
    result.object_path = std::move(path);
    result.signal = signal;
    result.object_width = 1U;
    result.language = fsim::app::SdfScopeRootLanguage::SystemVerilog;
    result.direction = direction;
    return result;
}

fsim::app::SdfPlannedAnnotation annotation(
    const fsim::frontend::SdfConstructKind construct,
    const fsim::app::SdfTimingTargetKind target_kind,
    std::vector<fsim::app::SdfResolvedEndpoint> endpoints,
    std::string identity)
{
    fsim::app::SdfPlannedAnnotation result;
    result.node_id = static_cast<std::uint64_t>(construct) + 1U;
    result.cell_id = 1U;
    result.construct_kind = construct;
    result.target_kind = target_kind;
    result.delay_mode = fsim::app::SdfDelayApplicationMode::Absolute;
    result.target_instance_path = "top.u";
    result.target_identity = std::move(identity);
    result.endpoints = std::move(endpoints);
    for (const auto& resolved : result.endpoints)
        result.endpoint_signals.push_back(resolved.signal);
    result.after_ticks = { 3U, 5U };
    result.source = source_span();
    result.source_identity = "interconnect-source";
    result.canonical_identity = "planned-" + result.target_identity;
    return result;
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> make_plan(
    std::vector<fsim::app::SdfPlannedAnnotation> annotations)
{
    return std::make_shared<const fsim::app::SdfAnnotationPlan>(
        make_summary(annotations.size()), fsim::app::SdfValuePolicy { },
        std::move(annotations), "interconnect-plan-identity");
}

std::vector<fsim::app::SdfPlannedAnnotation> valid_annotations()
{
    using namespace fsim;
    using app::SdfEndpointObjectKind;
    using app::SdfEndpointRole;
    return {
        annotation(frontend::SdfConstructKind::Interconnect,
            app::SdfTimingTargetKind::Interconnect,
            { endpoint(SdfEndpointRole::InterconnectSource,
                  SdfEndpointObjectKind::HdlNet, 0U, "top.u.src"),
                endpoint(SdfEndpointRole::InterconnectDestination,
                    SdfEndpointObjectKind::HdlNet, 1U, "top.u.dst") },
            "interconnect:src:dst"),
        annotation(frontend::SdfConstructKind::Port,
            app::SdfTimingTargetKind::Port,
            { endpoint(SdfEndpointRole::Output,
                SdfEndpointObjectKind::HdlPort, 2U, "top.u.Z",
                frontend::PortDirection::Output) },
            "port:Z"),
        annotation(frontend::SdfConstructKind::Mipd,
            app::SdfTimingTargetKind::Mipd,
            { endpoint(SdfEndpointRole::Net,
                SdfEndpointObjectKind::HdlNet, 1U, "top.u.dst") },
            "mipd:dst"),
        annotation(frontend::SdfConstructKind::Device,
            app::SdfTimingTargetKind::Device,
            { endpoint(SdfEndpointRole::Device,
                SdfEndpointObjectKind::HdlPort, 2U, "top.u.Z",
                frontend::PortDirection::Output) },
            "device:Z"),
    };
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfInterconnectTimingResult& result,
    const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error(
            "missing interconnect diagnostic " + std::string { code });
    return *found;
}

void test_all_endpoint_targets_and_driver_ownership()
{
    const auto design = make_design();
    const auto before = design.state();
    const auto result = fsim::app::apply_sdf_interconnect_timing(
        make_plan(valid_annotations()), design);
    require(result.ok() && result.application->timings().size() == 4U,
        "INTERCONNECT, PORT, MIPD, and DEVICE must publish atomically");
    const auto* interconnect
        = result.application->find_target("interconnect:src:dst");
    require(interconnect != nullptr
            && interconnect->driver_processes
                == std::vector<fsim::runtime::simir::ProcessId>({ 0U, 1U, 2U })
            && interconnect->transition_delays
                == std::vector<fsim::runtime::SimulationTick>({ 3U, 5U }),
        "net timing must retain continuous, gate, switch, and exact profile ownership");
    require(interconnect->endpoints.front().role
                == fsim::app::SdfEndpointRole::InterconnectSource
            && interconnect->endpoints.back().role
                == fsim::app::SdfEndpointRole::InterconnectDestination,
        "driver/load roles and instance boundaries must remain explicit");
    const auto after = design.state();
    require(before.processes.size() == after.processes.size()
            && before.processes[0].driver_regions
                == after.processes[0].driver_regions
            && before.processes[1].driver_regions
                == after.processes[1].driver_regions
            && before.processes[2].switch_source
                == after.processes[2].switch_source
            && before.processes[2].switch_target
                == after.processes[2].switch_target,
        "interconnect timing application must not mutate source processes");
}

void test_endpoint_and_profile_rejection()
{
    const auto design = make_design();
    auto annotations = valid_annotations();
    annotations.front().endpoints.front().object_kind
        = fsim::app::SdfEndpointObjectKind::SystemCSignal;
    auto result = fsim::app::apply_sdf_interconnect_timing(
        make_plan(std::move(annotations)), design);
    require(!result.ok() && !result.application,
        "unsupported endpoint kinds must reject the full publication");
    require_diagnostic(result, "FSIM-SDF-INTERCONNECT-002");

    annotations = valid_annotations();
    annotations[1].endpoints.front().direction
        = fsim::frontend::PortDirection::Input;
    result = fsim::app::apply_sdf_interconnect_timing(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "stale port direction must reject");
    require_diagnostic(result, "FSIM-SDF-INTERCONNECT-002");

    annotations = valid_annotations();
    annotations[2].after_ticks = { 1U, 2U, 3U, 4U };
    result = fsim::app::apply_sdf_interconnect_timing(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "unsupported transition profiles must reject");
    require_diagnostic(result, "FSIM-SDF-INTERCONNECT-003");
}

void test_ambiguity_and_resource_rejection()
{
    const auto design = make_design();
    auto annotations = valid_annotations();
    annotations.front().endpoints.push_back(annotations.front().endpoints.front());
    auto result = fsim::app::apply_sdf_interconnect_timing(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "ambiguous duplicate endpoints must reject");
    require_diagnostic(result, "FSIM-SDF-INTERCONNECT-002");

    auto limits = fsim::app::SdfInterconnectTimingLimits { };
    limits.max_drivers_per_target = 2U;
    result = fsim::app::apply_sdf_interconnect_timing(
        make_plan(valid_annotations()), design, limits);
    require(!result.ok() && !result.application,
        "driver ownership limits must reject atomically");
    require_diagnostic(result, "FSIM-SDF-INTERCONNECT-004");
}
} // namespace

int main()
{
    try {
        test_all_endpoint_targets_and_driver_ownership();
        test_endpoint_and_profile_rejection();
        test_ambiguity_and_resource_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}

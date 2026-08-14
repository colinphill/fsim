// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_path_timing.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
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

fsim::frontend::SourceSpan span(const std::size_t offset)
{
    fsim::frontend::SourceSpan result;
    result.source_name = "vital-plan.sdf";
    result.begin = { offset, 4U, offset + 1U };
    result.end = { offset + 1U, 4U, offset + 2U };
    return result;
}

fsim::frontend::SdfExactDecimal decimal(
    std::string coefficient, const std::int64_t exponent)
{
    fsim::frontend::SdfExactDecimal result;
    result.coefficient = std::move(coefficient);
    result.exponent10 = exponent;
    result.canonical
        = result.coefficient + 'e' + std::to_string(result.exponent10);
    return result;
}

fsim::frontend::SdfNormalizedTimescale nanoseconds()
{
    fsim::frontend::SdfNormalizedTimescale result;
    result.unit = fsim::frontend::SdfTimeUnit::Nanosecond;
    result.magnitude = decimal("1", 0);
    result.femtoseconds = decimal("1", 6);
    result.canonical = "1ns";
    return result;
}

fsim::frontend::SdfExactValue scalar(fsim::frontend::SdfExactDecimal value)
{
    fsim::frontend::SdfExactValue result;
    result.kind = fsim::frontend::SdfExactValueKind::Scalar;
    result.components[0] = std::move(value);
    return result;
}

struct FixtureOptions {
    bool missing_specialization { };
    bool width_mismatch { };
    bool generic_identity_mismatch { };
    bool duplicate { };
    bool non_vhdl { };
    bool with_vital_call { };
    bool timing_check { };
};

fsim::elaboration::ElaboratedDesign make_design(const FixtureOptions& options)
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    const auto add_port = [&](std::string name,
                              const frontend::PortDirection direction) {
        const auto id = static_cast<runtime::simir::SignalId>(
            state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = options.width_mismatch && id == 0U ? 2U : 1U;
        info.type_name = "std_logic";
        info.source_domain = frontend::ValueDomain::Logic9;
        info.is_port = true;
        info.direction = direction;
        info.resolution = runtime::simir::ResolutionKind::std_logic;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(std::move(name),
            runtime::PackedLogic4(state.signal_info.back().width),
            runtime::simir::ResolutionKind::std_logic,
            runtime::simir::ValueKind::logic9);
        state.signal_names.emplace_back(
            state.signal_info.back().name, id);
    };
    add_port("top.u.a", frontend::PortDirection::Input);
    add_port("top.u.z", frontend::PortDirection::Output);
    if (options.with_vital_call) {
        runtime::simir::Process process;
        process.id = 0U;
        process.name = "top.u.vital";
        process.language_standard = "vhdl-2008";
        if (options.timing_check) {
            process.register_count = 1U;
            process.register_value_kinds = {
                runtime::simir::ValueKind::logic9,
            };
            runtime::simir::VitalTimingCheck check;
            check.destination = 0U;
            check.kind = runtime::simir::VitalTimingCheckKind::setup_hold;
            check.test_signal = 0U;
            check.reference_signal = 1U;
            check.limits = { 5U, 7U, 11U, 13U };
            check.source = { "buf.vhd", 42U, 9U };
            process.operations.emplace_back(std::move(check));
        } else {
            process.register_count = 7U;
            process.register_value_kinds.assign(
                7U, runtime::simir::ValueKind::logic4);
            process.register_value_kinds[0] = runtime::simir::ValueKind::logic9;
            process.driver_regions = { { 1U, 0U, 1U, true } };
            process.operations.emplace_back(runtime::simir::ReadSignal { 0U, 0U });
            const std::array<std::uint64_t, 6> delays { 5U, 7U, 0U, 0U, 0U, 0U };
            for (std::size_t index = 0; index < delays.size(); ++index) {
                process.operations.emplace_back(runtime::simir::LoadConstant {
                    static_cast<runtime::simir::RegisterId>(index + 1U),
                    runtime::PackedLogic4::from_aval_bval(
                        64U, delays[index], 0U) });
            }
            runtime::simir::VitalDelay delay;
            delay.kind = runtime::simir::VitalDelayKind::path;
            delay.shape = runtime::simir::VitalDelayShape::delay01;
            delay.output = 1U;
            delay.source = 0U;
            delay.default_delays = { 1U, 2U, 3U, 4U, 5U, 6U };
            delay.source_location = { "buf.vhd", 31U, 7U };
            process.operations.emplace_back(std::move(delay));
        }
        process.operations.emplace_back(runtime::simir::Halt { });
        state.processes.push_back(std::move(process));
    }
    if (!options.missing_specialization) {
        elaboration::SpecializationInfo specialization;
        specialization.id = 0U;
        specialization.unit = "buf(rtl)";
        specialization.instance = "top.u";
        specialization.source = "buf.vhd";
        specialization.language = frontend::Language::Vhdl2008;
        specialization.library = "work";
        specialization.is_cell = true;
        if (options.with_vital_call)
            specialization.processes = { 0U };
        specialization.parameter_values = {
            { "tipd_a", "1 ns" },
            { "tpd_a_z", "2 ns" },
        };
        specialization.parameter_identity_values = {
            { "tipd_a", "vhdl-time:1000000fs" },
        };
        if (!options.generic_identity_mismatch) {
            specialization.parameter_identity_values.emplace_back(
                "tpd_a_z", "vhdl-time:2000000fs");
        }
        state.specializations.push_back(std::move(specialization));
    }
    auto result
        = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(result.has_value(), "VITAL target-plan design fixture must be valid");
    return std::move(*result);
}

fsim::app::SdfResolvedEndpoint endpoint(
    const fsim::app::SdfEndpointRole role,
    const fsim::runtime::simir::SignalId signal, std::string object,
    const fsim::frontend::PortDirection direction,
    const fsim::app::SdfScopeRootLanguage language)
{
    fsim::app::SdfResolvedEndpoint result;
    result.role = role;
    result.object_kind = fsim::app::SdfEndpointObjectKind::HdlPort;
    result.instance_path = "top.u";
    result.object_path = std::move(object);
    result.signal = signal;
    result.object_width = 1U;
    result.language = language;
    result.direction = direction;
    return result;
}

struct Fixture {
    fsim::elaboration::ElaboratedDesign design;
    std::shared_ptr<const fsim::app::SdfAnnotationSummary> summary;
};

Fixture make_fixture(const FixtureOptions options = { })
{
    using namespace fsim;
    frontend::SdfIrNode node;
    node.id = 1U;
    node.cell_id = 1U;
    node.kind = options.timing_check
        ? frontend::SdfConstructKind::SetupHold
        : frontend::SdfConstructKind::Iopath;
    node.span = span(20U);
    node.source_identity = "vital-root-source";
    node.canonical_identity = "vital-root-canonical";
    frontend::SdfIrNode value;
    value.id = 2U;
    value.cell_id = 1U;
    value.parent_id = 1U;
    value.depth = 1U;
    value.kind = frontend::SdfConstructKind::Value;
    value.exact_value = scalar(decimal("2", 0));
    value.span = span(21U);
    value.source_identity = "vital-value-source";
    value.canonical_identity = "vital-value-canonical";
    frontend::SdfIrCell ir_cell;
    ir_cell.id = 1U;
    ir_cell.cell_type = "BUF";
    ir_cell.node_count = 2U;
    ir_cell.source_identity = "vital-cell-source";
    ir_cell.canonical_identity = "vital-cell-canonical";
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "vital-plan.sdf", nanoseconds(),
        std::vector<frontend::SdfIrCell> { std::move(ir_cell) },
        std::vector<frontend::SdfIrNode> {
            std::move(node), std::move(value) },
        "vital-ir-identity");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "vital-plan.sdf", "vital-source-semantic", std::nullopt, '.',
        "project", "design", std::vector<app::SdfAnnotationRoot> { },
        "vital-scope-identity");

    const auto language = options.non_vhdl
        ? app::SdfScopeRootLanguage::SystemVerilog
        : app::SdfScopeRootLanguage::Vhdl;
    app::SdfResolvedInstance target;
    target.declaration_id = 7U;
    target.root_alias = "top";
    target.instance_path = "top.u";
    target.unit_identity = "vhdl:work.buf(rtl)";
    target.cell_type = "BUF";
    target.language = language;
    target.case_policy = app::SdfHierarchyCasePolicy::AsciiInsensitive;
    app::SdfResolvedCell resolved_cell;
    resolved_cell.cell_id = 1U;
    resolved_cell.source_identity = "vital-cell-source";
    resolved_cell.targets.push_back(std::move(target));
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { std::move(resolved_cell) },
        "vital-cells-identity");

    const auto make_mapping = [&] {
        app::SdfResolvedNodeEndpoints mapping;
        mapping.node_id = 1U;
        mapping.cell_id = 1U;
        mapping.construct_kind = options.timing_check
            ? frontend::SdfConstructKind::SetupHold
            : frontend::SdfConstructKind::Iopath;
        mapping.target_instance_path = "top.u";
        if (options.timing_check) {
            mapping.endpoints = {
                endpoint(app::SdfEndpointRole::TimingData, 0U, "top.u.a",
                    frontend::PortDirection::Input, language),
                endpoint(app::SdfEndpointRole::TimingReference, 1U, "top.u.z",
                    frontend::PortDirection::Output, language),
            };
        } else {
            mapping.endpoints = {
                endpoint(app::SdfEndpointRole::Input, 0U, "top.u.a",
                    frontend::PortDirection::Input, language),
                endpoint(app::SdfEndpointRole::Output, 1U, "top.u.z",
                    frontend::PortDirection::Output, language),
            };
        }
        mapping.endpoints.front().edge_identity = "posedge:a";
        mapping.condition_identity = "vital-condition:true";
        return mapping;
    };
    std::vector<app::SdfResolvedNodeEndpoints> mappings { make_mapping() };
    if (options.duplicate)
        mappings.push_back(make_mapping());
    auto resolution = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::move(mappings), "vital-endpoint-identity");
    app::SdfAnnotationTargetSummary target_summary;
    target_summary.cell_id = 1U;
    target_summary.target_instance_path = "top.u";
    target_summary.language = language;
    target_summary.annotation_count = options.duplicate ? 2U : 1U;
    target_summary.endpoint_count = options.duplicate ? 4U : 2U;
    target_summary.signals = options.duplicate
        ? std::vector<runtime::simir::SignalId> { 0U, 1U, 0U, 1U }
        : std::vector<runtime::simir::SignalId> { 0U, 1U };
    auto summary = std::make_shared<const app::SdfAnnotationSummary>(resolution,
        std::vector<app::SdfAnnotationTargetSummary> {
            std::move(target_summary) },
        options.duplicate ? 2U : 1U, options.duplicate ? 4U : 2U,
        options.duplicate ? 2U : 1U, 0U, 0U, "vital-summary-identity");
    return { make_design(options), std::move(summary) };
}

void require_diagnostic(const fsim::app::SdfVitalTargetPlanResult& result,
    const std::string_view code)
{
    require(std::ranges::any_of(result.diagnostics,
                [&](const auto& diagnostic) { return diagnostic.code == code; }),
        "expected VITAL target-plan diagnostic was not emitted");
}

void require_diagnostic(const fsim::app::SdfVitalPathTimingResult& result,
    const std::string_view code)
{
    require(std::ranges::any_of(result.diagnostics,
                [&](const auto& diagnostic) { return diagnostic.code == code; }),
        "expected VITAL path-plan diagnostic was not emitted");
}

void test_atomic_vital_plan()
{
    auto fixture = make_fixture();
    const auto result = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design);
    require(result.ok(), "valid VHDL target plan should publish atomically");
    require(result.plan->targets().size() == 1U,
        "valid VHDL target plan should retain one timing target");
    const auto& target = result.plan->targets().front();
    require(target.instance_path == "top.u"
            && target.unit_identity == "vhdl:work.buf(rtl)"
            && target.specialization_unit == "buf(rtl)"
            && target.library == "work",
        "VITAL plan should retain entity architecture and library identity");
    require(target.ports.size() == 2U
            && target.ports.front().value_domain
                == fsim::frontend::ValueDomain::Logic9
            && target.ports.front().resolution
                == fsim::runtime::simir::ResolutionKind::std_logic,
        "VITAL plan should retain exact nine-state port metadata");
    require(target.generics.size() == 2U
            && target.generics.front().name == "tipd_a"
            && target.generics.front().semantic_identity
                == "vhdl-time:1000000fs",
        "VITAL plan should retain sorted generic values and semantic identity");
    require(result.plan->semantic_identity().starts_with(
                "sdf-vital-target-plan-v1"),
        "VITAL plan should publish a versioned semantic identity");
    require(fixture.summary->semantic_identity() == "vital-summary-identity",
        "VITAL planning must not mutate the validated SDF summary");
}

void test_binding_type_generic_and_language_rejection()
{
    auto fixture = make_fixture({ .missing_specialization = true });
    auto result = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design);
    require(!result.ok() && result.plan == nullptr,
        "missing VHDL specialization must prevent partial publication");
    require_diagnostic(result, "FSIM-SDF-VITAL-PLAN-002");

    fixture = make_fixture({ .width_mismatch = true });
    result = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design);
    require(!result.ok() && result.plan == nullptr,
        "VHDL port shape mismatch must prevent partial publication");
    require_diagnostic(result, "FSIM-SDF-VITAL-PLAN-003");

    fixture = make_fixture({ .generic_identity_mismatch = true });
    result = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design);
    require(!result.ok() && result.plan == nullptr,
        "VHDL generic identity mismatch must prevent partial publication");
    require_diagnostic(result, "FSIM-SDF-VITAL-PLAN-004");

    fixture = make_fixture({ .non_vhdl = true });
    result = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design);
    require(!result.ok() && result.plan == nullptr,
        "non-VHDL targets must remain outside the Batch 170 VITAL plan");
    require_diagnostic(result, "FSIM-SDF-VITAL-PLAN-001");
}

void test_duplicate_and_resource_rejection()
{
    auto fixture = make_fixture({ .duplicate = true });
    auto result = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design);
    require(!result.ok() && result.plan == nullptr,
        "duplicate VHDL target ownership must reject the whole plan");
    require_diagnostic(result, "FSIM-SDF-VITAL-PLAN-005");

    fixture = make_fixture();
    fsim::app::SdfVitalTargetPlanLimits limits;
    limits.max_ports_per_target = 1U;
    result = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design, limits);
    require(!result.ok() && result.plan == nullptr,
        "VHDL target resource overflow must reject the whole plan");
    require_diagnostic(result, "FSIM-SDF-VITAL-PLAN-006");
}

void test_vital_path_and_check_call_plans()
{
    auto fixture = make_fixture({ .with_vital_call = true });
    auto targets = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design);
    require(targets.ok(), "VITAL path fixture should build its target plan");
    auto result = fsim::app::build_sdf_vital_path_timing_plan(
        targets.plan, fixture.design, fsim::app::SdfValuePolicy { });
    require(result.ok() && result.plan->records().size() == 1U,
        "VITAL IOPATH should bind one stable delay call");
    const auto& path = result.plan->records().front();
    require(path.call.kind == fsim::app::SdfVitalCallKind::Delay
            && path.call.process == 0U && path.call.instruction == 7U
            && path.before_delay_ticks == std::vector<std::uint64_t>({ 5U, 7U })
            && path.after_delays.size() == 1U
            && path.after_delays.front().ticks == 2'000U,
        "VITAL IOPATH plan should retain exact call and before/after delays");
    require(path.edge_identities == std::vector<std::string>({ "posedge:a" })
            && path.condition_identity == "vital-condition:true",
        "VITAL IOPATH plan should retain edge and condition identity");

    fixture = make_fixture(
        { .with_vital_call = true, .timing_check = true });
    targets = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design);
    require(targets.ok(), "VITAL timing fixture should build its target plan");
    result = fsim::app::build_sdf_vital_path_timing_plan(
        targets.plan, fixture.design, fsim::app::SdfValuePolicy { });
    require(result.ok() && result.plan->records().size() == 1U,
        "SDF SETUPHOLD should bind one stable VITAL timing call");
    const auto& check = result.plan->records().front();
    require(check.call.kind == fsim::app::SdfVitalCallKind::TimingCheck
            && check.call.instruction == 0U
            && check.before_check_ticks
                == std::vector<std::int64_t>({ 5, 7, 11, 13 })
            && check.after_checks.size() == 1U
            && check.after_checks.front().ticks == 2'000,
        "VITAL timing plan should retain call identity and signed limits");
}

void test_vital_path_atomic_rejection()
{
    auto fixture = make_fixture();
    const auto targets = fsim::app::build_sdf_vital_target_plan(
        fixture.summary, fixture.design);
    require(targets.ok(), "missing-call fixture should retain a target plan");
    auto result = fsim::app::build_sdf_vital_path_timing_plan(
        targets.plan, fixture.design, fsim::app::SdfValuePolicy { });
    require(!result.ok() && result.plan == nullptr,
        "missing VITAL call must prevent partial path publication");
    require_diagnostic(result, "FSIM-SDF-VITAL-PATH-002");

    fsim::app::SdfVitalPathTimingLimits limits;
    limits.max_records = 0U;
    result = fsim::app::build_sdf_vital_path_timing_plan(
        targets.plan, fixture.design, fsim::app::SdfValuePolicy { }, limits);
    require(!result.ok() && result.plan == nullptr,
        "zero VITAL path resource limits must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-PATH-001");
}
} // namespace

int main()
{
    try {
        test_atomic_vital_plan();
        test_binding_type_generic_and_language_rejection();
        test_duplicate_and_resource_rejection();
        test_vital_path_and_check_call_plans();
        test_vital_path_atomic_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}

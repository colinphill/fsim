// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_models.hpp"

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
    result.source_name = "vital-model.sdf";
    result.begin = { offset, 7U, offset + 1U };
    result.end = { offset + 1U, 7U, offset + 2U };
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
    bool state_table { };
    bool memory_path { };
};

fsim::runtime::simir::Process delay_process()
{
    using namespace fsim::runtime::simir;
    Process process;
    process.id = 0U;
    process.name = "top.u.vital-delay";
    process.language_standard = "vhdl-2008";
    process.register_count = 7U;
    process.register_value_kinds.assign(7U, ValueKind::logic4);
    process.register_value_kinds[0] = ValueKind::logic9;
    process.driver_regions = { { 1U, 0U, 1U, true } };
    process.operations.emplace_back(ReadSignal { 0U, 0U });
    const std::array<std::uint64_t, 6> delays { 3U, 5U, 0U, 0U, 0U, 0U };
    for (std::size_t index = 0; index < delays.size(); ++index) {
        process.operations.emplace_back(LoadConstant {
            static_cast<RegisterId>(index + 1U),
            fsim::runtime::PackedLogic4::from_aval_bval(
                64U, delays[index], 0U) });
    }
    VitalDelay delay;
    delay.kind = VitalDelayKind::path;
    delay.shape = VitalDelayShape::delay01;
    delay.output = 1U;
    delay.source = 0U;
    delay.default_delays = { 1U, 2U, 3U, 4U, 5U, 6U };
    delay.source_location = { "mystery.vhd", 50U, 3U };
    process.operations.emplace_back(std::move(delay));
    process.operations.emplace_back(Halt { });
    return process;
}

fsim::runtime::simir::Process state_table_process()
{
    using namespace fsim::runtime::simir;
    Process process;
    process.id = 1U;
    process.name = "top.u.state-structure";
    process.language_standard = "vhdl-2008";
    process.register_count = 4U;
    process.register_value_kinds = {
        ValueKind::logic9,
        ValueKind::logic9,
        ValueKind::logic4,
        ValueKind::logic9,
    };
    process.operations.emplace_back(ReadSignal { 0U, 0U });
    process.operations.emplace_back(SignalLastValue { 1U, 0U });
    process.operations.emplace_back(Binary {
        BinaryOperator::case_equal, 2U, 0U, 1U });
    process.operations.emplace_back(ConditionalSelect { 3U, 2U, 0U, 1U });
    process.operations.emplace_back(Halt { });
    return process;
}

fsim::runtime::simir::Process memory_process(
    const fsim::runtime::simir::ProcessId id)
{
    using namespace fsim::runtime::simir;
    Process process;
    process.id = id;
    process.name = "top.u.memory-structure";
    process.language_standard = "vhdl-2008";
    process.register_count = 1U;
    process.string_register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    VitalMemoryDeclare memory;
    memory.destination = 0U;
    memory.word_count = 16U;
    memory.word_width = 8U;
    memory.subword_width = 8U;
    memory.load_file = 0U;
    memory.embedded_load = true;
    memory.source = { "mystery.vhd", 61U, 5U };
    process.operations.emplace_back(std::move(memory));
    process.operations.emplace_back(Halt { });
    return process;
}

fsim::elaboration::ElaboratedDesign make_design(const FixtureOptions options)
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
        info.width = 1U;
        info.type_name = "std_logic";
        info.source_domain = frontend::ValueDomain::Logic9;
        info.is_port = true;
        info.direction = direction;
        info.resolution = runtime::simir::ResolutionKind::std_logic;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(std::move(name), runtime::PackedLogic4(1U),
            runtime::simir::ResolutionKind::std_logic,
            runtime::simir::ValueKind::logic9);
        state.signal_names.emplace_back(state.signal_info.back().name, id);
    };
    add_port("top.u.a", frontend::PortDirection::Input);
    add_port("top.u.z", frontend::PortDirection::Output);
    state.processes.push_back(delay_process());
    if (options.state_table)
        state.processes.push_back(state_table_process());
    if (options.memory_path) {
        state.processes.push_back(memory_process(
            static_cast<runtime::simir::ProcessId>(state.processes.size())));
    }
    elaboration::SpecializationInfo specialization;
    specialization.id = 0U;
    specialization.unit = "unrecognized_wrapper_name(rtl)";
    specialization.instance = "top.u";
    specialization.source = "mystery.vhd";
    specialization.language = frontend::Language::Vhdl2008;
    specialization.library = "work";
    specialization.is_cell = true;
    for (const auto& process : state.processes)
        specialization.processes.push_back(process.id);
    specialization.parameter_values = { { "tpd_a_z", "2 ns" } };
    specialization.parameter_identity_values = {
        { "tpd_a_z", "vhdl-time:2000000fs" },
    };
    state.specializations.push_back(std::move(specialization));
    auto result = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(result.has_value(), "VITAL model design fixture must be valid");
    return std::move(*result);
}

fsim::app::SdfResolvedEndpoint endpoint(
    const fsim::app::SdfEndpointRole role,
    const fsim::runtime::simir::SignalId signal, std::string object,
    const fsim::frontend::PortDirection direction)
{
    fsim::app::SdfResolvedEndpoint result;
    result.role = role;
    result.object_kind = fsim::app::SdfEndpointObjectKind::HdlPort;
    result.instance_path = "top.u";
    result.object_path = std::move(object);
    result.signal = signal;
    result.object_width = 1U;
    result.language = fsim::app::SdfScopeRootLanguage::Vhdl;
    result.direction = direction;
    return result;
}

struct Fixture {
    fsim::elaboration::ElaboratedDesign design;
    std::shared_ptr<const fsim::app::SdfVitalPathTimingPlan> paths;
};

Fixture make_fixture(const FixtureOptions options = { })
{
    using namespace fsim;
    frontend::SdfIrNode node;
    node.id = 1U;
    node.cell_id = 1U;
    node.kind = frontend::SdfConstructKind::Iopath;
    node.span = span(20U);
    node.source_identity = "vital-model-root-source";
    node.canonical_identity = "vital-model-root-canonical";
    frontend::SdfIrNode value;
    value.id = 2U;
    value.cell_id = 1U;
    value.parent_id = 1U;
    value.depth = 1U;
    value.kind = frontend::SdfConstructKind::Value;
    value.exact_value = scalar(decimal("2", 0));
    value.span = span(21U);
    value.source_identity = "vital-model-value-source";
    value.canonical_identity = "vital-model-value-canonical";
    frontend::SdfIrCell ir_cell;
    ir_cell.id = 1U;
    ir_cell.cell_type = "ARBITRARY_MODEL";
    ir_cell.node_count = 2U;
    ir_cell.source_identity = "vital-model-cell-source";
    ir_cell.canonical_identity = "vital-model-cell-canonical";
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "vital-model.sdf", nanoseconds(),
        std::vector<frontend::SdfIrCell> { std::move(ir_cell) },
        std::vector<frontend::SdfIrNode> {
            std::move(node), std::move(value) },
        "vital-model-ir-identity");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "vital-model.sdf", "vital-model-source-semantic", std::nullopt, '.',
        "project", "design", std::vector<app::SdfAnnotationRoot> { },
        "vital-model-scope-identity");
    app::SdfResolvedInstance target;
    target.declaration_id = 9U;
    target.root_alias = "top";
    target.instance_path = "top.u";
    target.unit_identity = "vhdl:work.unrecognized_wrapper_name(rtl)";
    target.cell_type = "ARBITRARY_MODEL";
    target.language = app::SdfScopeRootLanguage::Vhdl;
    target.case_policy = app::SdfHierarchyCasePolicy::AsciiInsensitive;
    app::SdfResolvedCell resolved_cell;
    resolved_cell.cell_id = 1U;
    resolved_cell.source_identity = "vital-model-cell-source";
    resolved_cell.targets.push_back(std::move(target));
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { std::move(resolved_cell) },
        "vital-model-cells-identity");
    app::SdfResolvedNodeEndpoints mapping;
    mapping.node_id = 1U;
    mapping.cell_id = 1U;
    mapping.construct_kind = frontend::SdfConstructKind::Iopath;
    mapping.target_instance_path = "top.u";
    mapping.endpoints = {
        endpoint(app::SdfEndpointRole::Input, 0U, "top.u.a",
            frontend::PortDirection::Input),
        endpoint(app::SdfEndpointRole::Output, 1U, "top.u.z",
            frontend::PortDirection::Output),
    };
    auto resolution = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { std::move(mapping) },
        "vital-model-endpoint-identity");
    app::SdfAnnotationTargetSummary target_summary;
    target_summary.cell_id = 1U;
    target_summary.target_instance_path = "top.u";
    target_summary.language = app::SdfScopeRootLanguage::Vhdl;
    target_summary.annotation_count = 1U;
    target_summary.endpoint_count = 2U;
    target_summary.signals = { 0U, 1U };
    auto summary = std::make_shared<const app::SdfAnnotationSummary>(resolution,
        std::vector<app::SdfAnnotationTargetSummary> {
            std::move(target_summary) },
        1U, 2U, 1U, 0U, 0U, "vital-model-summary-identity");
    auto design = make_design(options);
    const auto targets = app::build_sdf_vital_target_plan(summary, design);
    require(targets.ok(), "VITAL model fixture should build its target plan");
    const auto paths = app::build_sdf_vital_path_timing_plan(
        targets.plan, design, app::SdfValuePolicy { });
    require(paths.ok(), "VITAL model fixture should build its path plan");
    return { std::move(design), paths.plan };
}

fsim::app::SdfVitalWrapperRegistration wrapper_for(
    const fsim::app::SdfVitalPlannedTarget& target)
{
    fsim::app::SdfVitalWrapperRegistration result;
    result.target_identity = target.canonical_identity;
    result.governance_identity = "explicit-wrapper-contract-v1";
    for (const auto& port : target.ports) {
        result.ports.push_back({ port.canonical_identity, port.signal,
            port.role, port.width, port.direction, port.value_domain });
    }
    return result;
}

void require_diagnostic(const fsim::app::SdfVitalModelResult& result,
    const std::string_view code)
{
    require(std::ranges::any_of(result.diagnostics,
                [&](const auto& diagnostic) { return diagnostic.code == code; }),
        "expected VITAL model diagnostic was not emitted");
}

void test_structural_models()
{
    using fsim::app::SdfVitalStructuralModelKind;
    auto fixture = make_fixture();
    auto result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, fixture.design);
    require(result.ok()
            && result.plan->records().front().structural_kind
                == SdfVitalStructuralModelKind::Primitive,
        "plain VITAL delay structure should map as a primitive model");

    fixture = make_fixture({ .state_table = true });
    result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, fixture.design);
    require(result.ok()
            && result.plan->records().front().structural_kind
                == SdfVitalStructuralModelKind::StateTable,
        "case-select and prior-state structure should map as a state table");

    fixture = make_fixture({ .memory_path = true });
    result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, fixture.design);
    require(result.ok()
            && result.plan->records().front().structural_kind
                == SdfVitalStructuralModelKind::MemoryPath,
        "VITAL memory declaration structure should map as a memory path");
}

void test_explicit_wrapper_governance()
{
    auto fixture = make_fixture();
    auto wrapper = wrapper_for(
        fixture.paths->targets()->targets().front());
    auto result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, fixture.design,
        std::span<const fsim::app::SdfVitalWrapperRegistration> {
            &wrapper, 1U });
    require(result.ok() && result.plan->records().front().governed_wrapper
            && result.plan->records().front().governance_identity
                == "explicit-wrapper-contract-v1",
        "explicit structural registration should govern arbitrary wrapper names");
    require(result.plan->semantic_identity().starts_with(
                "sdf-vital-model-plan-v1"),
        "VITAL model plans should publish a versioned semantic identity");

    ++wrapper.ports.front().width;
    result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, fixture.design,
        std::span<const fsim::app::SdfVitalWrapperRegistration> {
            &wrapper, 1U });
    require(!result.ok() && result.plan == nullptr,
        "wrapper port mismatch must reject the complete model plan");
    require_diagnostic(result, "FSIM-SDF-VITAL-MODEL-004");
}

void test_atomic_shape_ownership_and_resource_rejection()
{
    auto fixture = make_fixture(
        { .state_table = true, .memory_path = true });
    auto result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, fixture.design);
    require(!result.ok() && result.plan == nullptr,
        "ambiguous state-table memory shape must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-MODEL-003");

    fixture = make_fixture();
    auto unsupported_state = fixture.design.state();
    unsupported_state.specializations.front().processes.clear();
    auto unsupported_design = fsim::elaboration::ElaboratedDesign::from_state(
        std::move(unsupported_state));
    require(unsupported_design.has_value(),
        "unsupported model ownership fixture must remain structurally valid");
    result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, *unsupported_design);
    require(!result.ok() && result.plan == nullptr,
        "unsupported unowned model process must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-MODEL-002");

    fixture = make_fixture();
    auto wrapper = wrapper_for(
        fixture.paths->targets()->targets().front());
    const std::array duplicate_wrappers { wrapper, wrapper };
    result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, fixture.design, duplicate_wrappers);
    require(!result.ok() && result.plan == nullptr,
        "duplicate wrapper ownership must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-MODEL-005");

    fsim::app::SdfVitalModelLimits limits;
    limits.max_identity_bytes = 1U;
    result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, fixture.design, { }, limits);
    require(!result.ok() && result.plan == nullptr,
        "model identity overflow must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-MODEL-006");

    limits = { };
    limits.max_records = 0U;
    result = fsim::app::build_sdf_vital_model_plan(
        fixture.paths, fixture.design, { }, limits);
    require(!result.ok() && result.plan == nullptr,
        "zero model limits must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-MODEL-001");
}
} // namespace

int main()
{
    try {
        test_structural_models();
        test_explicit_wrapper_governance();
        test_atomic_shape_ownership_and_resource_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
